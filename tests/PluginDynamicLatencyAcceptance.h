// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "SyntheticDevice.h"
#include "MixerRouting.h"
#include <array>
#include <chrono>
#include <functional>
#include <thread>

// Draft internal LIVE-worker risk test. Include after ParameterAcceptance.h,
// CallbackAllocationProbe.h and awaitProjection declaration. No build/desktop work
// is performed by preparing this header. It deliberately asserts the musical
// constant-signal oracle even when the current graph-history transition fails.
namespace mixer_dynamic_detail
{
constexpr int block = 256, rate = 48000, capacity = 3000;
// Independent production budget:ceil(256*1.1)=282; K=1+ceil(282/256)=3.
constexpr int preparedTransportSamples = 768;
struct Frame
{
    double callbackMs = 0, releaseLatenessMs = 0;
    SyntheticCallbackTiming driverPhase;
    juce::int64 callbackBegan = 0, callbackEnded = 0;
    std::uint64_t cyclesBefore = 0, cyclesAfter = 0;
    bool cyclesKnown = false;
    std::uint64_t missed = 0, dropped = 0, submitted = 0, processed = 0;
    unsigned allocations = 0, destructions = 0;
    int latestReportedLatency = 0;
    int consumedNativeLatency = -1;
    std::uint32_t declaredGeneration = 0, consumedGeneration = 0;
    std::uint64_t consumedSequence = 0;
};
class Capture
{
    std::function<void(juce::AudioBuffer<float> &)> process;
    // Diagnostic-only alternative: the ordinary test keeps its existing driver
    // and timing oracle. Instrumentation is never release qualification.
    std::function<void(juce::AudioBuffer<float> &, SyntheticCallbackTiming &)> phaseProcess;
    std::shared_ptr<PluginHost> host;
    juce::AudioBuffer<float> buffer{2, block};
    std::unique_ptr<void, decltype(&CloseHandle)> pacingTimer{
        CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                               TIMER_MODIFY_STATE | SYNCHRONIZE),
        &CloseHandle};
    std::jthread thread;

  public:
    std::vector<Frame> frames{capacity};
    std::array<std::vector<float>, 2> pcm{std::vector<float>(capacity * block),
                                          std::vector<float>(capacity *block)};
    std::atomic<int> complete{0};
    std::atomic<bool> exhausted{false};
    std::atomic<bool> pacingFailed{false};
    Capture(std::function<void(juce::AudioBuffer<float> &)> callback, std::shared_ptr<PluginHost> worker,
            std::function<void(juce::AudioBuffer<float> &, SyntheticCallbackTiming &)> diagnostic = {})
        : process(std::move(callback)), phaseProcess(std::move(diagnostic)), host(std::move(worker))
    {
    }
    ~Capture()
    {
        stop();
    }
    void start()
    {
        if (!pacingTimer)
            throw std::runtime_error("Private dynamic-latency driver timer unavailable");
        thread = std::jthread(
            [this](std::stop_token stop)
            {
                using Clock = std::chrono::steady_clock;
                const auto period = std::chrono::duration_cast<Clock::duration>(
                    std::chrono::duration<double>(double(block) / rate));
                auto due = Clock::now();
                for (int index = 0; !stop.stop_requested(); ++index)
                {
                    if (index >= capacity)
                    {
                        exhausted = true;
                        break;
                    }
                    // Exact existing physical cadence, outside measured DSP.
                    // Coarse sleep_until can overshoot by an entire Windows tick.
                    if (const auto remaining = due - Clock::now(); remaining > Clock::duration::zero())
                    {
                        LARGE_INTEGER delay;
                        delay.QuadPart = -std::max<LONGLONG>(
                            1, std::chrono::duration_cast<std::chrono::nanoseconds>(remaining).count() / 100);
                        if (!SetWaitableTimer(pacingTimer.get(), &delay, 0, nullptr, nullptr, FALSE) ||
                            WaitForSingleObject(pacingTimer.get(), 2000) != WAIT_OBJECT_0)
                        {
                            pacingFailed = true;
                            break;
                        }
                    }
                    const auto beganWall = Clock::now();
                    auto &record = frames[size_t(index)];
                    record.releaseLatenessMs =
                        std::chrono::duration<double, std::milli>(beganWall - due).count();
                    const auto allocations = callbackAllocations.load(),
                               destructions = callbackDestructions.load();
                    ULONG64 cyclesBefore = 0, cyclesAfter = 0;
                    const bool cyclesBegan =
                        phaseProcess && QueryThreadCycleTime(GetCurrentThread(), &cyclesBefore);
                    const auto began = juce::Time::getHighResolutionTicks();
                    callbackAllocationProbe = true;
                    if (phaseProcess)
                        phaseProcess(buffer, record.driverPhase);
                    else
                        process(buffer); // Sole synthetic-device/host processing owner.
                    callbackAllocationProbe = false;
                    const auto ended = juce::Time::getHighResolutionTicks();
                    record.callbackMs = juce::Time::highResolutionTicksToSeconds(ended - began) * 1000;
                    if (phaseProcess)
                    {
                        const bool cyclesEnded = QueryThreadCycleTime(GetCurrentThread(), &cyclesAfter);
                        record.cyclesKnown = cyclesBegan && cyclesEnded && cyclesAfter >= cyclesBefore;
                        record.cyclesBefore = cyclesBefore;
                        record.cyclesAfter = cyclesAfter;
                        record.callbackBegan = began;
                        record.callbackEnded = ended;
                    }
                    record.allocations = callbackAllocations.load() - allocations;
                    record.destructions = callbackDestructions.load() - destructions;
                    const auto statistics = host->statistics(); // Fixed atomic scalar snapshot only.
                    record.missed = statistics.missed;
                    record.dropped = statistics.dropped;
                    record.submitted = statistics.submitted;
                    record.processed = statistics.processed;
                    record.latestReportedLatency = host->latencySamples();
                    record.declaredGeneration = host->declaredLatency().generation;
                    if (const auto consumed = host->consumedLatency())
                    {
                        record.consumedNativeLatency = consumed->stamp.nativeSamples;
                        record.consumedGeneration = consumed->stamp.generation;
                        record.consumedSequence = consumed->sequence;
                    }
                    for (int channel = 0; channel < 2; ++channel)
                        std::copy_n(buffer.getReadPointer(channel), block,
                                    pcm[size_t(channel)].data() + index * block);
                    complete.store(index + 1, std::memory_order_release);
                    due += period;
                    // Never burst callbacks to catch up after an OS scheduling miss.
                    // The missed cadence remains explicitly recorded and fails the
                    // pacing gate; no device buffer or musical threshold is enlarged.
                    const auto now = Clock::now();
                    if (now > due + period)
                        due = now + period;
                }
                callbackAllocationProbe = false;
            });
    }
    void stop()
    {
        if (thread.joinable())
        {
            thread.request_stop();
            thread.join();
        }
    }
    void write(const juce::File &folder, const juce::String &name) const
    {
        const auto count = complete.load(std::memory_order_acquire);
        juce::String trace =
            "callback,firstAbsoluteSample,callbackMs,releaseLatenessMs,latestReportedLatency,"
            "declaredGeneration,consumedNativeLatency,consumedGeneration,consumedSequence,submitted,"
            "processed,missed,dropped,allocations,destructions,minL,maxL,minR,maxR\n";
        for (int i = 0; i < count; ++i)
        {
            const auto &f = frames[size_t(i)];
            trace += juce::String(i) + "," + juce::String(i * block) + "," + juce::String(f.callbackMs, 7) +
                     "," + juce::String(f.releaseLatenessMs, 7) + "," +
                     juce::String(f.latestReportedLatency) + "," + juce::String(f.declaredGeneration) + "," +
                     juce::String(f.consumedNativeLatency) + "," + juce::String(f.consumedGeneration) + "," +
                     juce::String(f.consumedSequence) + "," + juce::String(juce::int64(f.submitted)) + "," +
                     juce::String(juce::int64(f.processed)) + "," + juce::String(juce::int64(f.missed)) +
                     "," + juce::String(juce::int64(f.dropped)) + "," + juce::String(f.allocations) + "," +
                     juce::String(f.destructions);
            for (int channel = 0; channel < 2; ++channel)
            {
                const auto *begin = pcm[size_t(channel)].data() + i * block;
                const auto bounds = std::minmax_element(begin, begin + block);
                trace += "," + juce::String(*bounds.first, 9) + "," + juce::String(*bounds.second, 9);
            }
            trace += "\n";
        }
        require(folder.getChildFile(name + "-callbacks.csv").replaceWithText(trace),
                "Dynamic-latency callback/absolute sample trace retained");
        if (phaseProcess)
        {
            juce::String phases = "callback,outerMs,cyclesKnown,threadCyclesBefore,threadCyclesAfter," +
                                  juce::String(SyntheticCallbackTiming::csvHeader) + "\n";
            bool coherent = count > 0;
            for (int i = 0; i < count; ++i)
            {
                const auto &f = frames[size_t(i)];
                coherent &= f.cyclesKnown && f.driverPhase.containedBy(f.callbackBegan, f.callbackEnded);
                phases += juce::String(i) + "," + juce::String(f.callbackMs, 7) + "," +
                          juce::String(int(f.cyclesKnown)) + "," +
                          juce::String(juce::uint64(f.cyclesBefore)) + "," +
                          juce::String(juce::uint64(f.cyclesAfter)) + "," +
                          f.driverPhase.csv(f.callbackBegan, f.callbackEnded) + "\n";
            }
            require(folder.getChildFile(name + "-driver-phases.csv").replaceWithText(phases),
                    "Retain every diagnostic callback phase and raw thread cycle counter");
            require(coherent, "Diagnostic phases enclose actual driver dispatch and cycle queries succeed");
        }
        auto stream = folder.getChildFile(name + "-actual-pcm.wav").createOutputStream();
        require(bool(stream), "Owned raw PCM output stream");
        stream->setPosition(0);
        stream->truncate();
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            format.createWriterFor(stream.release(), rate, 2, 32, {}, 0));
        const float *pointers[]{pcm[0].data(), pcm[1].data()};
        require(writer && writer->writeFromFloatArrays(pointers, 2, count * block),
                "Actual callback PCM retained before assertions");
    }
};
struct Drive
{
    int begin = 0, end = 0;
    bool controls = true;
    juce::String events =
        "targetNativeSamples,request,requestCallback,ackCallback,publishedCallback,endCallback,result\n";
};
inline Drive drive(Capture &capture, PluginHost &host, unsigned generation,
                   const std::function<void()> &service, const std::function<void()> &afterWarm = {},
                   int expectedTransportSamples = block)
{
    Drive result;
    const auto deadline = juce::Time::getMillisecondCounter() + 12000;
    const auto pump = [&]
    {
        service();
        dispatchMessages(1);
    };
    const auto wait = [&](int callbacks)
    {
        while (capture.complete.load(std::memory_order_acquire) < callbacks && !capture.exhausted.load() &&
               juce::Time::getMillisecondCounter() < deadline)
            pump();
        return capture.complete.load(std::memory_order_acquire) >= callbacks;
    };
    //>1s fills the complete original4096-sample ring many times, and exceeds
    // native1024+worker256+maximum parallel compensation. No transition is masked
    // by subsequent warmup or excluded settling windows.
    result.controls = wait(200);
    result.begin = capture.complete.load(std::memory_order_acquire);
    if (afterWarm)
        afterWarm();
    const std::array<float, 4> values{1.f, 1.f / 3, 2.f / 3, 0.f};
    const std::array<int, 4> native{1024, 31, 257, 0};
    for (int i = 0; i < 4 && result.controls; ++i)
    {
        const auto before = capture.complete.load(std::memory_order_acquire);
        const auto request = host.setParameter(17, values[size_t(i)], generation);
        PluginParameterResult ack;
        bool acknowledged = false;
        while (request && !acknowledged && juce::Time::getMillisecondCounter() < deadline)
        {
            acknowledged = host.readParameterResult(request, ack);
            if (!acknowledged)
                pump();
        }
        const auto afterAck = capture.complete.load(std::memory_order_acquire);
        while (acknowledged && ack.result == 0 &&
               host.latencySamples() != expectedTransportSamples + native[size_t(i)] &&
               juce::Time::getMillisecondCounter() < deadline)
            pump();
        const auto published = capture.complete.load(std::memory_order_acquire);
        const bool observed = host.latencySamples() == expectedTransportSamples + native[size_t(i)];
        result.controls = request && acknowledged && ack.result == 0 && observed && wait(published + 80);
        result.events += juce::String(native[size_t(i)]) + "," + juce::String(request) + "," +
                         juce::String(before) + "," + juce::String(afterAck) + "," + juce::String(published) +
                         "," + juce::String(capture.complete.load(std::memory_order_acquire)) + "," +
                         (acknowledged ? juce::String(ack.result) : juce::String("no_ack")) + "\n";
    }
    result.end = capture.complete.load(std::memory_order_acquire);
    return result;
}
struct Analysis
{
    bool pass = false;
    double maximumError = 0, maximumMs = 0;
    int overruns = 0, pacingMisses = 0, badSamples = 0, longestBadRun = 0;
    int nonfiniteSamples = 0, firstNonfiniteFrame = -1;
    unsigned allocations = 0, destructions = 0;
    std::uint64_t missed = 0, dropped = 0;
};
inline Analysis analyze(const Capture &capture, const Drive &drive, std::array<float, 2> reference)
{
    Analysis result;
    if (drive.begin <= 0 || drive.end <= drive.begin)
        return result;
    const auto &first = capture.frames[size_t(drive.begin - 1)],
               &last = capture.frames[size_t(drive.end - 1)];
    result.missed = last.missed - first.missed;
    result.dropped = last.dropped - first.dropped;
    int run = 0;
    for (int i = drive.begin; i < drive.end; ++i)
    {
        const auto &f = capture.frames[size_t(i)];
        result.maximumMs = std::max(result.maximumMs, f.callbackMs);
        result.overruns += f.callbackMs > block * 1000. / rate;
        result.pacingMisses += f.releaseLatenessMs > block * 1000. / rate;
        result.allocations += f.allocations;
        result.destructions += f.destructions;
        for (int n = 0; n < block; ++n)
        {
            double error = 0;
            for (int channel = 0; channel < 2; ++channel)
            {
                const auto sample = capture.pcm[size_t(channel)][size_t(i * block + n)];
                if (!std::isfinite(sample) || !std::isfinite(reference[size_t(channel)]))
                {
                    ++result.nonfiniteSamples;
                    if (result.firstNonfiniteFrame < 0)
                        result.firstNonfiniteFrame = i * block + n;
                    error = std::numeric_limits<double>::infinity();
                }
                else
                    error = std::max(error, std::abs(double(sample) - reference[size_t(channel)]));
            }
            result.maximumError = std::max(result.maximumError, error);
            if (error > 2.e-6)
            {
                ++result.badSamples;
                ++run;
                result.longestBadRun = std::max(result.longestBadRun, run);
            }
            else
                run = 0;
        }
    }
    result.pass = drive.controls && !capture.exhausted.load() && !capture.pacingFailed.load() &&
                  result.nonfiniteSamples == 0 && result.maximumError < 2.e-6 && result.overruns == 0 &&
                  result.pacingMisses == 0 && result.missed == 0 && result.dropped == 0 &&
                  result.allocations == 0 && result.destructions == 0;
    return result;
}
inline juce::String describe(const juce::String &name, const Analysis &a)
{
    return name + " maximumPCMError=" + juce::String(a.maximumError, 9) +
           " badSamples=" + juce::String(a.badSamples) + " longestBadRun=" + juce::String(a.longestBadRun) +
           " nonfiniteSamples=" + juce::String(a.nonfiniteSamples) +
           " firstNonfiniteAbsoluteSample=" + juce::String(a.firstNonfiniteFrame) +
           " maximumCallbackMs=" + juce::String(a.maximumMs, 6) + " overruns=" + juce::String(a.overruns) +
           " missedReleaseIntervals=" + juce::String(a.pacingMisses) +
           " workerMissed=" + juce::String(juce::int64(a.missed)) +
           " workerDropped=" + juce::String(juce::int64(a.dropped)) +
           " callbackAllocations=" + juce::String(a.allocations) +
           " callbackDestructions=" + juce::String(a.destructions) + "\n";
}
} // namespace mixer_dynamic_detail

static void pluginDynamicLatencyTests(const juce::File &folder, const juce::File &binary,
                                      bool phaseDiagnostic = false)
{
    using namespace mixer_dynamic_detail;
    require(folder.createDirectory().wasOk() && binary.existsAsFile(),
            "Dynamic-latency owned evidence and original fixture");
    const auto spec = parameterSpec(binary);
    require(spec.name == "VibeRa Mixer Latency Fixture" && !spec.instrument,
            "Actual original native-delay effect");
    juce::String report =
        "LIVE workers, owned paced virtual callback thread,48k256, no hardware/listening/native-input "
        "claim.\n"
        "Latest declared and consumed packet latency/generation/sequence and actual audio-output marker "
        "adoption are recorded; this is not all-device/atomic wet-graph epoch qualification.\n"
        "Warm constant inputs eliminate intrinsic native readtap phase changes; no transition settling "
        "window is excluded. No buffer/deadline workaround.\n";
    bool allPassed = true;
    Analysis directAnalysis;
    {
        auto host = std::make_shared<PluginHost>();
        require(host->start(spec, rate, block, false).wasOk() && !host->offline(),
                "Actual LIVE direct baseline worker");
        const auto catalogue = parameterCatalogue(*host);
        require(parameterResult(*host, host->setParameter(17, 0, catalogue.generation)).result == 0,
                "Native0 baseline control acknowledged");
        constexpr std::array<float, 2> source{.125f, .25f};
        std::int64_t samples = 0;
        Capture capture(
            [&](juce::AudioBuffer<float> &buffer)
            {
                for (int channel = 0; channel < 2; ++channel)
                    std::fill_n(buffer.getWritePointer(channel), block, source[size_t(channel)]);
                host->process(buffer, 0, block, nullptr, 0, double(samples) / rate, 120, true, false);
                samples += block;
            },
            host);
        capture.start();
        const auto driveResult = drive(capture, *host, catalogue.generation, [&] { host->service(); });
        capture.stop();
        capture.write(folder, "direct-host");
        require(folder.getChildFile("direct-host-controls.csv").replaceWithText(driveResult.events),
                "Direct native control outcomes retained");
        directAnalysis = analyze(capture, driveResult, source);
        report += describe("direct-host", directAnalysis);
        allPassed = allPassed && directAnalysis.pass && host->alive();
    }
    const auto sourceFile = folder.getChildFile("original-dense-positive-stereo.wav");
    {
        juce::AudioBuffer<float> source(2, rate * 4);
        for (int channel = 0; channel < 2; ++channel)
            std::fill_n(source.getWritePointer(channel), source.getNumSamples(), channel == 0 ? .125f : .25f);
        auto stream = sourceFile.createOutputStream();
        require(bool(stream), "Owned original dense source stream");
        stream->setPosition(0);
        stream->truncate();
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            format.createWriterFor(stream.release(), rate, 2, 24, {}, 0));
        require(writer && writer->writeFromAudioSampleBuffer(source, 0, source.getNumSamples()),
                "Original constant PCM written");
    }
    for (const bool patternMode : {true, false})
    {
        const auto name = juce::String(patternMode ? "pattern" : "song");
        AudioEngine audio(false, {}, block); // This fixture actually dispatches only256-frame callbacks.
        auto &manager = audio.engine.getDeviceManager();
        manager.deviceManager.addAudioDeviceType(std::make_unique<SyntheticDeviceType>());
        juce::XmlElement setup("DEVICESETUP");
        setup.setAttribute("deviceType", "VibeRa synthetic");
        setup.setAttribute("audioOutputDeviceName", "In-memory output");
        setup.setAttribute("audioDeviceRate", double(rate));
        setup.setAttribute("audioDeviceBufferSize", block);
        audio.engine.getPropertyStorage().setXmlProperty(te::SettingID::audio_device_setup, setup);
        manager.initialise(0, 2);
        dispatchMessages(20);
        auto *device = dynamic_cast<SyntheticDevice *>(manager.deviceManager.getCurrentAudioDevice());
        require(device && device->getCurrentSampleRate() == rate &&
                    device->getCurrentBufferSizeSamples() == block,
                "Exact48k256 virtual device, no larger buffer");
        auto project = Project::empty();
        require(te::getDefaultPanLaw() == te::PanLawLinear,
                "Known original linear pan-law prerequisite; no mutation of user pan settings");
        project.channels.resize(2);
        for (auto &insert : project.mixer)
            insert.gain = 1;
        for (auto &channel : project.channels)
        {
            channel.source = sourceFile.getFullPathName();
            channel.gain = 1;
            channel.sampler.loop = true;
        }
        auto &affected = project.channels[0], &held = project.channels[1];
        affected.mixer = 1;
        affected.pan = -1;
        held.mixer = 2;
        held.pan = 1;
        auto &pattern = project.patterns.front();
        const auto patternId = pattern.id, heldId = held.id;
        pattern.length = 64;
        pattern.notes = {{project.allocate(), affected.id, 0, 64, 60, 1, false}};
        project.placements = {{project.allocate(), patternId, 0, 0, 64, 0}};
        require(setMixerRoute(project, 1, 3, true).wasOk(), "Parallel dry/unity effect path retained");
        auto changed = spec;
        changed.id = project.allocate();
        project.mixer[3].effects = {changed};
        auto unchanged = spec;
        unchanged.id = project.allocate();
        project.mixer[2].effects = {unchanged};
        require(awaitProjection(audio, project, patternMode, patternId).wasOk(),
                "Actual prepared Pattern/Song graph with two LIVE workers");
        auto changedHost = audio.parameterHost(changed.id), unchangedHost = audio.parameterHost(unchanged.id);
        require(changedHost && unchangedHost && !changedHost->offline() && !unchangedHost->offline(),
                "Actual retained live workers, not offline processing");
        require(changedHost->transportPacketCount() == 3 && unchangedHost->transportPacketCount() == 3 &&
                    changedHost->transportLatencySamples() == preparedTransportSamples &&
                    unchangedHost->transportLatencySamples() == preparedTransportSamples,
                "Independently declared production3Q/768 transport, not a fitted PCM offset");
        const auto catalogue = parameterCatalogue(*changedHost);
        require(
            parameterResult(*changedHost, changedHost->setParameter(17, 0, catalogue.generation)).result == 0,
            "Affected native latency0 prepared before play");
        const auto changedPid = changedHost->processId(), unchangedPid = unchangedHost->processId();
        audio.setMidiTarget(heldId);
        const auto refusedBefore = audio.projectionDiagnostics();
        const auto refused = audio.configureCompensationHistory(4096, 1);
        const auto refusedAfter = audio.projectionDiagnostics();
        require(refused.failed() && audio.compensationGraphObservation().desired == 0 &&
                    refusedBefore.edit == refusedAfter.edit &&
                    refusedBefore.playbackContext == refusedAfter.playbackContext && changedHost->alive() &&
                    unchangedHost->alive(),
                "Explicit insufficient history budget refuses before publication and retires binding");
        const auto historyReady = audio.configureCompensationHistory(4096, 16 * 1024 * 1024);
        require(historyReady.wasOk(), "Actual bounded retained compensation graph: " +
                                          historyReady.getErrorMessage().toStdString());
        const auto preparedMarker = audio.compensationGraphObservation();
        require(preparedMarker.desired != 0 && preparedMarker.prepared == preparedMarker.desired &&
                    preparedMarker.adopted == 0 && !preparedMarker.firstAdoptedReference,
                "Prepared marker is not falsely reported adopted before actual callbacks");
        audio.setLooping(false);
        audio.playStop();
        dispatchMessages(20);
        audio.midiInput.capture.begin();
        audio.midiInput.handleIncomingMidiMessage(nullptr,
                                                  juce::MidiMessage::noteOn(1, 60, juce::uint8(100)));
        std::function<void(juce::AudioBuffer<float> &, SyntheticCallbackTiming &)> diagnostic;
        if (phaseDiagnostic)
            diagnostic = [&](juce::AudioBuffer<float> &buffer, SyntheticCallbackTiming &phase)
            {
                device->process(buffer);
                phase = device->lastTiming();
            };
        Capture capture([&](juce::AudioBuffer<float> &buffer) { device->process(buffer); }, changedHost,
                        std::move(diagnostic));
        AudioEngine::ProjectionDiagnostics before;
        std::vector<std::pair<int, std::uint64_t>> graphInitialisations;
        PluginHost::Statistics unaffectedBefore;
        int starts = 0, stops = 0;
        int originLo = 0, originHi = 0;
        double originHead = 0, maximumHeadOutsideBracket = 0;
        bool headOriginSet = false, continuousHead = true;
        juce::String heads = "completedBefore,completedAfter,headSeconds,headPlaying,transportPlaying\n";
        int lastHeadCount = -1;
        const auto service = [&]
        {
            audio.reclaimPluginHosts();
            const auto lo = capture.complete.load(std::memory_order_acquire);
            if (lo != lastHeadCount)
            {
                const auto state = audio.projectionDiagnostics();
                const auto hi = capture.complete.load(std::memory_order_acquire);
                if (headOriginSet)
                {
                    // Position can advance inside an in-flight callback before
                    // the driver publishes complete. Bracket both snapshots by
                    // that single in-flight quantum instead of inventing a seek
                    // or allowing arbitrary post-rebuild transport movement.
                    const auto minimum = originHead + double(lo - originHi - 1) * block / rate - 2. / rate;
                    const auto maximum = originHead + double(hi + 1 - originLo) * block / rate + 2. / rate;
                    maximumHeadOutsideBracket =
                        std::max(maximumHeadOutsideBracket,
                                 std::max({0., minimum - state.headSeconds, state.headSeconds - maximum}));
                    continuousHead = continuousHead && state.headPlaying && state.transportPlaying &&
                                     state.headSeconds >= minimum && state.headSeconds <= maximum;
                }
                heads += juce::String(lo) + "," + juce::String(hi) + "," +
                         juce::String(state.headSeconds, 9) + "," + juce::String(int(state.headPlaying)) +
                         "," + juce::String(int(state.transportPlaying)) + "\n";
                lastHeadCount = lo;
            }
        };
        capture.start();
        const auto driveResult = drive(
            capture, *changedHost, catalogue.generation, service,
            [&]
            {
                originLo = capture.complete.load(std::memory_order_acquire);
                before = audio.projectionDiagnostics();
                originHi = capture.complete.load(std::memory_order_acquire);
                originHead = before.headSeconds;
                headOriginSet = true;
                graphInitialisations = audio.mixerGraphInitialisations();
                unaffectedBefore = unchangedHost->statistics();
                starts = device->starts;
                stops = device->stops;
            },
            preparedTransportSamples);
        const auto after = audio.projectionDiagnostics();
        const auto adoptedMarker = audio.compensationGraphObservation();
        const auto unaffectedAfter = unchangedHost->statistics();
        const auto finalGraphs = audio.mixerGraphInitialisations();
        const auto heldState = std::find_if(after.channels.begin(), after.channels.end(),
                                            [&](const auto &c) { return c.channel == heldId; });
        const bool heldRetained = audio.midiInput.capture.active && heldState != after.channels.end() &&
                                  heldState->heldInputKeys == 1;
        // Release only this owned test's note, with actual callbacks still running.
        audio.midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 60));
        const int releaseUntil = capture.complete.load() + 40;
        const auto releaseDeadline = juce::Time::getMillisecondCounter() + 3000;
        while (capture.complete.load() < releaseUntil &&
               juce::Time::getMillisecondCounter() < releaseDeadline)
        {
            service();
            dispatchMessages(1);
        }
        const auto released = audio.projectionDiagnostics();
        const auto releasedHeld = std::find_if(released.channels.begin(), released.channels.end(),
                                               [&](const auto &c) { return c.channel == heldId; });
        const bool releaseKeys = releasedHeld != released.channels.end() && releasedHeld->heldInputKeys == 0;
        capture.stop();
        capture.write(folder, name);
        require(folder.getChildFile(name + "-controls.csv").replaceWithText(driveResult.events) &&
                    folder.getChildFile(name + "-head.csv").replaceWithText(heads),
                "Actual shared control/head outcomes retained");
        std::array<float, 2> reference{};
        bool flatWarmup = driveResult.begin >= 16;
        if (flatWarmup)
            for (int channel = 0; channel < 2; ++channel)
            {
                const int end = driveResult.begin * block, begin = end - 8 * block;
                double sum = 0;
                float minimum = 100, maximum = -100;
                for (int n = begin; n < end; ++n)
                {
                    const auto sample = capture.pcm[size_t(channel)][size_t(n)];
                    sum += sample;
                    minimum = std::min(minimum, sample);
                    maximum = std::max(maximum, sample);
                }
                reference[size_t(channel)] = float(sum / (end - begin));
                flatWarmup = flatWarmup && minimum > .01f && maximum - minimum < 2.e-6;
            }
        // Independent musical amplitude oracle under the explicitly checked
        // linear law: hard-left doubles sourceL(.125), parallel unity doubles it
        // again. Held right is sourceR(.25)*hard-right2*MIDIvelocity100/127.
        constexpr std::array<float, 2> expectedReference{.5f, .5f * (100.f / 127.f)};
        flatWarmup = flatWarmup && std::abs(reference[0] - expectedReference[0]) < 2.e-6 &&
                     std::abs(reference[1] - expectedReference[1]) < 2.e-6;
        const auto analysis = analyze(capture, driveResult, expectedReference);
        float releaseRightPeak = 0;
        const auto capturedFrames = capture.complete.load(std::memory_order_acquire);
        if (capturedFrames > 0)
            for (int n = (capturedFrames - 1) * block; n < capturedFrames * block; ++n)
                releaseRightPeak = std::max(releaseRightPeak, std::abs(capture.pcm[1][size_t(n)]));
        const bool releasePCM = releaseRightPeak < 2.e-6;
        bool resources =
            before.edit == after.edit && before.playbackContext == after.playbackContext &&
            before.replacements == after.replacements && before.samplerCreations == after.samplerCreations &&
            before.cache.preparations == after.cache.preparations && graphInitialisations == finalGraphs &&
            device->starts == starts && device->stops == stops && after.headPlaying &&
            after.transportPlaying && after.headSeconds > before.headSeconds && changedHost->alive() &&
            unchangedHost->alive() && changedHost->processId() == changedPid &&
            unchangedHost->processId() == unchangedPid;
        for (const auto &channel : before.channels)
        {
            const auto found = std::find_if(after.channels.begin(), after.channels.end(),
                                            [&](const auto &c) { return c.channel == channel.channel; });
            resources = resources && found != after.channels.end() && found->sample == channel.sample &&
                        found->sampler == channel.sampler && found->track == channel.track;
            if (found != after.channels.end())
                report += "  channel=" + juce::String(channel.channel) +
                          " graphPreparationBefore=" + juce::String(channel.graphInitialisations) +
                          " graphPreparationAfter=" + juce::String(found->graphInitialisations) + "\n";
        }
        const bool unaffected = unaffectedAfter.missed == unaffectedBefore.missed &&
                                unaffectedAfter.dropped == unaffectedBefore.dropped;
        report += describe(name, analysis);
        report += "  graphMarkerDesired=" + juce::String(adoptedMarker.desired) +
                  " prepared=" + juce::String(adoptedMarker.prepared) +
                  " adopted=" + juce::String(adoptedMarker.adopted) + " firstReference=" +
                  (adoptedMarker.firstAdoptedReference ? juce::String(*adoptedMarker.firstAdoptedReference)
                                                       : juce::String("unavailable")) +
                  "\n";
        report += "  graphPreparationFailure=" + audio.compensationGraphFailure() + "\n";
        report += "  flatWarmup=" + juce::String(int(flatWarmup)) +
                  " referenceL=" + juce::String(reference[0], 9) +
                  " referenceR=" + juce::String(reference[1], 9) +
                  " resourceRetention=" + juce::String(int(resources)) +
                  " heldInputRetained=" + juce::String(int(heldRetained)) +
                  " heldRelease=" + juce::String(int(releaseKeys)) +
                  " heldReleasePCM=" + juce::String(int(releasePCM)) +
                  " unaffectedWorkerClean=" + juce::String(int(unaffected)) +
                  " continuousHead=" + juce::String(int(continuousHead)) +
                  " maximumHeadOutsideBracket=" + juce::String(maximumHeadOutsideBracket, 9) +
                  " analyticL=" + juce::String(expectedReference[0], 9) +
                  " analyticR=" + juce::String(expectedReference[1], 9) + "\n";
        if (analysis.badSamples)
            report +=
                analysis.missed || analysis.dropped || !unaffected
                    ? "  classification=worker_starvation_or_loss; inspect exact callback/PCM boundaries.\n"
                : directAnalysis.pass
                    ? "  classification=graph_or_compensation_transition_loss_with_constant_native_baseline; "
                      "inspect marker/reference trace and retained-history diagnostics; zero-filled FIFO "
                      "remains a source-backed "
                      "hypothesis.\n"
                    : "  classification=direct_host_baseline_also_failed; cannot attribute to graph alone.\n";
        allPassed = allPassed && flatWarmup && analysis.pass && resources && heldRetained && releaseKeys &&
                    releasePCM && unaffected && continuousHead && adoptedMarker.adopted != 0 &&
                    adoptedMarker.adopted == adoptedMarker.desired && adoptedMarker.firstAdoptedReference &&
                    audio.compensationGraphFailure().isEmpty();
        audio.midiInput.stopCapture();
        audio.stop();
    }
    report += "No loop-boundary, rapid-before-adoption, changing-waveform phase, hardware/listening or "
              "arbitrary-plugin seamless-latency claim. All-device epoch and MIDI transition semantics "
              "remain open.\n";
    require(folder.getChildFile("dynamic-native-latency-live.txt").replaceWithText(report),
            "All direct/Pattern/Song failure evidence retained before aggregate assertion");
    require(allPassed, "LIVE native latency changes retain warmed constant PCM, unaffected held voice, exact "
                       "callback budgets and resources");
}
