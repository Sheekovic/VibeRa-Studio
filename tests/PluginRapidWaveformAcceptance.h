// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// Include only after mixer_waveform_detail in PluginDynamicWaveformAcceptance.h.
// Original additional test: these helpers are never application DSP code.
namespace mixer_rapid_waveform_detail
{
using namespace mixer_dynamic_detail;
// At125BPM one beat is exactly23040 frames,90 complete256-frame callbacks.
// This bounded case deliberately does NOT qualify off-grid/split-quantum loops.
constexpr std::int64_t loopSamples = 23040;
constexpr int bursts = 3, changesPerBurst = 4;
struct LoopFrame
{
    std::int64_t positionBefore = 0, positionAfter = 0;
    std::int64_t unloopedBefore = 0, unloopedAfter = 0;
    std::int64_t referenceBefore = 0, referenceAfter = 0;
    bool playing = false, looping = false;
};
struct RapidDrive
{
    Drive phase;
    int requests = 0, observed = 0, straddledBursts = 0, maximumRequestGap = 0;
    std::array<std::int64_t, bursts> boundaries{};
    juce::String failure;
};
inline RapidDrive driveRapid(Capture &capture, PluginHost &host, unsigned generation,
                             const std::vector<mixer_waveform_detail::Frame> &packets,
                             const std::vector<LoopFrame> &heads, const std::function<void()> &service,
                             const std::function<void()> &afterWarm)
{
    RapidDrive result;
    result.phase.events = "burst,change,boundaryUnloopedSample,targetUnloopedSample,targetNativeSamples,"
                          "request,requestCallback,requestUnloopedSample,ackCallback,declaredGeneration,"
                          "consumedCallback,consumedUnloopedSample,consumedSequence,result\n";
    const auto started = juce::Time::getMillisecondCounter();
    const auto withinBudget = [&]
    {
        // Same12000ms driver budget as the existing waveform drive; wrap-safe.
        return juce::uint32(juce::Time::getMillisecondCounter() - started) < 12000 &&
               !capture.exhausted.load(std::memory_order_acquire) && host.alive();
    };
    const auto pump = [&]
    {
        service();
        dispatchMessages(1);
    };
    const auto latest = [&]() -> const LoopFrame *
    {
        const auto count = capture.complete.load(std::memory_order_acquire);
        return count > 0 ? &heads[size_t(count - 1)] : nullptr;
    };
    const auto waitUntil = [&](const auto &predicate)
    {
        while (withinBudget())
        {
            if (predicate())
                return true; // Acknowledgement reads consume receipts.
            pump();
        }
        return predicate();
    };
    result.phase.controls =
        waitUntil([&] { return capture.complete.load(std::memory_order_acquire) >= 200; });
    result.phase.begin = capture.complete.load(std::memory_order_acquire);
    if (!result.phase.controls)
    {
        result.failure = "warmup_timeout_or_worker_failure";
        result.phase.end = result.phase.begin;
        return result;
    }
    afterWarm();
    const auto initial = latest()->unloopedAfter;
    if (initial < 0)
    {
        result.phase.controls = false;
        result.failure = "unconfirmed_positive_unlooped_clock";
        result.phase.end = capture.complete.load(std::memory_order_acquire);
        return result;
    }
    // This schedules from the musical PLAYHEAD, not from fitted audio phase.
    // Each burst straddles one actual upcoming boundary. Apart from these short
    // target intervals, the next request follows its actual ack+packet receipt;
    // there are no80-callback post-change settling/exclusion windows.
    // Choose the next boundary with the complete prescribed12-callback lead;
    // this is control scheduling, never PCM/source-phase calibration.
    const auto firstBoundary = ((initial + 12 * block) / loopSamples + 1) * loopSamples;
    constexpr std::array<int, changesPerBurst> offsetCallbacks{-12, -2, 2, 6};
    constexpr std::array<float, changesPerBurst> values{1.f, 1.f / 3, 2.f / 3, 0.f};
    constexpr std::array<int, changesPerBurst> native{1024, 31, 257, 0};
    for (int burst = 0; burst < bursts && result.phase.controls; ++burst)
    {
        const auto boundary = firstBoundary + burst * loopSamples;
        result.boundaries[size_t(burst)] = boundary;
        std::int64_t firstConsumed = -1, lastConsumed = -1;
        int previousRequest = -1;
        for (int change = 0; change < changesPerBurst && result.phase.controls; ++change)
        {
            const auto target = boundary + offsetCallbacks[size_t(change)] * block;
            if (!waitUntil([&] { return latest() && latest()->unloopedAfter >= target; }))
            {
                result.phase.controls = false;
                result.failure = "scheduled_request_timeout_or_worker_failure";
                break;
            }
            const auto requestCallback = capture.complete.load(std::memory_order_acquire);
            const auto requestHead = heads[size_t(requestCallback - 1)].unloopedAfter;
            const auto previousGeneration = packets[size_t(requestCallback - 1)].wetAfter.generation;
            const auto request = host.setParameter(17, values[size_t(change)], generation);
            ++result.requests;
            if (previousRequest >= 0)
                result.maximumRequestGap =
                    std::max(result.maximumRequestGap, requestCallback - previousRequest);
            previousRequest = requestCallback;
            PluginParameterResult ack{};
            bool acknowledged = false;
            if (request)
                acknowledged = waitUntil([&] { return host.readParameterResult(request, ack); });
            const auto ackCallback = capture.complete.load(std::memory_order_acquire);
            int consumedCallback = -1;
            std::int64_t consumedHead = -1;
            std::uint64_t consumedSequence = 0;
            std::uint32_t declaredGeneration = 0;
            const auto consumed = [&]
            {
                const auto declared = host.declaredLatency();
                declaredGeneration = declared.generation;
                if (declared.nativeSamples != native[size_t(change)] ||
                    declared.generation == previousGeneration ||
                    host.latencySamples() != preparedTransportSamples + native[size_t(change)])
                    return false;
                const auto count = capture.complete.load(std::memory_order_acquire);
                for (int index = requestCallback; index < count; ++index)
                {
                    const auto &packet = packets[size_t(index)].wetAfter;
                    if (packet.valid && packet.native == native[size_t(change)] &&
                        packet.generation == declared.generation)
                    {
                        consumedCallback = index;
                        consumedHead = heads[size_t(index)].unloopedAfter;
                        consumedSequence = packet.sequence;
                        return true;
                    }
                }
                return false;
            };
            const bool observed = request && acknowledged && ack.result == 0 && waitUntil(consumed);
            if (observed)
            {
                ++result.observed;
                if (change == 0)
                    firstConsumed = consumedHead;
                if (change == changesPerBurst - 1)
                    lastConsumed = consumedHead;
            }
            // A slow provider/scheduler cannot quietly turn this into the old
            // widely-spaced test.32callbacks is a new cadence coverage gate,
            // not a relaxed callback, IPC or timing budget.
            const bool requestSide = change < 2 ? requestHead < boundary : requestHead >= boundary;
            result.phase.controls = observed && requestSide && result.maximumRequestGap <= 32;
            if (!result.phase.controls)
                result.failure = !request          ? "request_refused"
                                 : !acknowledged   ? "ack_timeout_or_worker_failure"
                                 : ack.result != 0 ? "parameter_failure"
                                 : !observed       ? "declared_or_consumed_packet_unconfirmed"
                                 : !requestSide    ? "request_missed_intended_boundary_side"
                                                   : "rapid_cadence_unconfirmed";
            result.phase.events += juce::String(burst) + "," + juce::String(change) + "," +
                                   juce::String(boundary) + "," + juce::String(target) + "," +
                                   juce::String(native[size_t(change)]) + "," + juce::String(request) + "," +
                                   juce::String(requestCallback) + "," + juce::String(requestHead) + "," +
                                   juce::String(ackCallback) + "," + juce::String(declaredGeneration) + "," +
                                   juce::String(consumedCallback) + "," + juce::String(consumedHead) + "," +
                                   juce::String(juce::int64(consumedSequence)) + "," +
                                   (acknowledged ? juce::String(ack.result) : juce::String("no_ack")) + "\n";
        }
        if (firstConsumed >= 0 && firstConsumed < boundary && lastConsumed >= boundary)
            ++result.straddledBursts;
    }
    const auto tail = capture.complete.load(std::memory_order_acquire) + 12;
    if (result.phase.controls &&
        !waitUntil([&] { return capture.complete.load(std::memory_order_acquire) >= tail; }))
    {
        result.phase.controls = false;
        result.failure = "post_burst_capture_timeout_or_worker_failure";
    }
    result.phase.end = capture.complete.load(std::memory_order_acquire);
    return result;
}
struct LoopAnalysis
{
    bool pass = false;
    int actualWraps = 0, matchedBoundaries = 0, clockDiscontinuities = 0, invalidLoopStates = 0;
};
inline LoopAnalysis analyzeLoops(const std::vector<LoopFrame> &heads, const RapidDrive &drive,
                                 const juce::File &folder, const juce::String &name)
{
    LoopAnalysis result;
    std::array<bool, bursts> matched{};
    juce::String trace = "callback,positionBefore,positionAfter,unloopedBefore,unloopedAfter,referenceBefore,"
                         "referenceAfter,playing,looping,expectedLoopedPosition,actualWrap\n";
    for (int index = 0; index < drive.phase.end; ++index)
    {
        const auto &head = heads[size_t(index)];
        const auto expected = ((head.unloopedAfter % loopSamples) + loopSamples) % loopSamples;
        bool wrapped = false;
        if (index >= drive.phase.begin && index > 0)
        {
            const auto &previous = heads[size_t(index - 1)];
            result.clockDiscontinuities += head.referenceAfter != previous.referenceAfter + block ||
                                           head.unloopedAfter != previous.unloopedAfter + block;
            result.invalidLoopStates += !head.playing || !head.looping || head.positionAfter != expected;
            wrapped =
                head.positionAfter < previous.positionAfter && head.unloopedAfter > previous.unloopedAfter;
            result.actualWraps += wrapped;
            for (int burst = 0; burst < bursts; ++burst)
                if (drive.boundaries[size_t(burst)] > 0 && wrapped &&
                    previous.unloopedAfter < drive.boundaries[size_t(burst)] &&
                    head.unloopedAfter >= drive.boundaries[size_t(burst)])
                    matched[size_t(burst)] = true;
        }
        trace += juce::String(index) + "," + juce::String(head.positionBefore) + "," +
                 juce::String(head.positionAfter) + "," + juce::String(head.unloopedBefore) + "," +
                 juce::String(head.unloopedAfter) + "," + juce::String(head.referenceBefore) + "," +
                 juce::String(head.referenceAfter) + "," + juce::String(int(head.playing)) + "," +
                 juce::String(int(head.looping)) + "," + juce::String(expected) + "," +
                 juce::String(int(wrapped)) + "\n";
    }
    for (const auto value : matched)
        result.matchedBoundaries += value;
    require(folder.getChildFile(name + "-loop-clock.csv").replaceWithText(trace),
            "Actual musical loop and absolute clock traces retained");
    result.pass = drive.phase.controls && drive.requests == bursts * changesPerBurst &&
                  drive.observed == bursts * changesPerBurst && drive.straddledBursts == bursts &&
                  result.matchedBoundaries == bursts && result.actualWraps >= bursts &&
                  !result.clockDiscontinuities && !result.invalidLoopStates;
    return result;
}
} // namespace mixer_rapid_waveform_detail

static bool pluginRapidWaveformTests(const juce::File &folder, const PluginSpec &generator,
                                     const PluginSpec &effect, bool directChainPassed)
{
    using namespace mixer_dynamic_detail;
    juce::String report =
        "UNQUALIFIED additional actual-worker loop experiment:48000Hz/256frames,125BPM,one-beat23040sample "
        "loop.\n"
        "2scenarios x3bursts x4changes=24requested/acknowledged/consumed native-latency transitions.\n"
        "Within each burst:1024,31,257,0samples at musical-boundary offsets[-12,-2,+2,+6]callbacks.\n"
        "Exact existing sourceConsumedSequence PCM oracle and2e-6 bound; no phase fitting, excluded "
        "transition samples or buffer/deadline changes.\n"
        "Actual scalar PlayHead loop/unlooped/reference snapshots distinguish musical wraps from source "
        "clock.\n"
        "No arbitrary/off-grid/split-quantum loops,production activation,native-input,hardware or listening "
        "claim.\n";
    const auto reportFile = folder.getChildFile("rapid-waveform-loops.txt");
    require(reportFile.replaceWithText(report + "stage=starting\n"),
            "Additional waveform-loop startup evidence retained");
    bool allPassed = true;
    for (const bool patternMode : {true, false})
    {
        const juce::String name = patternMode ? "rapid-wave-pattern-loop" : "rapid-wave-song-loop";
        try
        {
            AudioEngine audio(false, {}, block); // Actual physical callbacks are256 frames.
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
                        device->getCurrentBufferSizeSamples() == block &&
                        te::getDefaultPanLaw() == te::PanLawLinear,
                    "Exact48k256 virtual driver and unchanged linear-pan prerequisite");
            auto project = Project::empty();
            project.tempo = 125;
            for (auto &insert : project.mixer)
            {
                insert.gain = 1;
                insert.pan = 0;
            }
            project.mixer[0].gain = .5f; // Independently specified average, not fitted amplitude.
            auto &channel = project.channels.front();
            channel.mixer = 1;
            channel.gain = 1;
            channel.pan = 0;
            auto &pattern = project.patterns.front();
            pattern.length = 1;
            pattern.autoLength = false; // Exact musical loop period for independent PCM oracle.
            pattern.notes = {{project.allocate(), channel.id, 0, 1, 60, 1, false}};
            project.placements = {{project.allocate(), pattern.id, 0, 0, 1, 0}};
            auto source = generator, wet = effect;
            source.id = project.allocate();
            wet.id = project.allocate();
            project.mixer[1].effects = {source};
            project.mixer[3].effects = {wet};
            require(setMixerRoute(project, 1, 3, true).wasOk(), "Dry Master plus unity wet parallel branch");
            require(awaitProjection(audio, project, patternMode, pattern.id).wasOk(),
                    "Actual app-created Pattern/Song graph");
            auto sourceHost = audio.parameterHost(source.id), wetHost = audio.parameterHost(wet.id);
            require(sourceHost && wetHost && !sourceHost->offline() && !wetHost->offline(),
                    "Two genuine LIVE workers");
            require(sourceHost->transportPacketCount() == 3 && wetHost->transportPacketCount() == 3 &&
                        sourceHost->transportLatencySamples() == preparedTransportSamples &&
                        wetHost->transportLatencySamples() == preparedTransportSamples,
                    "Independently declared production3Q/768 workers before PCM capture");
            const auto catalogue = parameterCatalogue(*wetHost);
            require(parameterResult(*wetHost, wetHost->setParameter(17, 0, catalogue.generation)).result == 0,
                    "Native0 request acknowledged before initial warmup");
            // An ordinary selected Channel allocates the playback context for live
            // input, just as the existing constant-signal experiment does. Preparing
            // workers alone does not promise that a stopped Edit has a graph yet.
            audio.setMidiTarget(channel.id);
            const auto history = audio.configureCompensationHistory(4096, 16 * 1024 * 1024, source.id, wet.id);
            require(history.wasOk(),
                    "Bounded retained-history experiment: " + history.getErrorMessage().toStdString());
            const auto sourcePid = sourceHost->processId(), wetPid = wetHost->processId();
            int starts = device->starts, stops = device->stops;
            auto before = audio.projectionDiagnostics();
            auto gainsBefore = audio.mixerGraphInitialisations();
            std::vector<mixer_waveform_detail::Frame> frames(capacity);
            std::vector<mixer_rapid_waveform_detail::LoopFrame> loopFrames(capacity);
            // Test-only borrow of the exact current context already exposed in the
            // diagnostic. Its lifetime is retained by AudioEngine for the capture;
            // no control-side rebuilding/recovery is allowed during this scenario.
            auto *context = reinterpret_cast<te::EditPlaybackContext *>(before.playbackContext);
            require(context && context->getSampleRate() == rate && context->getNodePlayHead(),
                    "Prepared exact playback context and scalar PlayHead probe");
            auto *head = context->getNodePlayHead();
            int callback = 0;
            audio.setLooping(true);
            audio.playStop();
            dispatchMessages(20);
            const auto loopState = audio.projectionDiagnostics();
            require(loopState.loopEnabled && loopState.headLoopEnabled && loopState.headLoopAvailable &&
                        loopState.loopStartBeat == 0 && loopState.loopEndBeat == 1 &&
                        loopState.headLoopStartBeat == 0 && loopState.headLoopEndBeat == 1 &&
                        head->getLoopRange() ==
                            juce::Range<juce::int64>(0, mixer_rapid_waveform_detail::loopSamples),
                    "Actual Pattern/Song one-beat musical loop configured at exact23040samples");
            Capture capture(
                [&](juce::AudioBuffer<float> &buffer)
                {
                    auto &frame = frames[size_t(callback)];
                    auto &loop = loopFrames[size_t(callback++)];
                    loop.positionBefore = head->getPosition();
                    loop.unloopedBefore = head->getUnloopedPosition();
                    loop.referenceBefore = head->getReferenceSampleRange().getStart();
                    frame.sourceBefore = mixer_waveform_detail::packet(*sourceHost);
                    frame.wetBefore = mixer_waveform_detail::packet(*wetHost);
                    frame.markerBefore = audio.compensationGraphObservation();
                    device->process(buffer);
                    frame.sourceAfter = mixer_waveform_detail::packet(*sourceHost);
                    frame.wetAfter = mixer_waveform_detail::packet(*wetHost);
                    frame.markerAfter = audio.compensationGraphObservation();
                    loop.positionAfter = head->getPosition();
                    loop.unloopedAfter = head->getUnloopedPosition();
                    loop.referenceAfter = head->getReferenceSampleRange().getStart();
                    loop.playing = head->isPlaying();
                    loop.looping = head->isLooping();
                },
                wetHost);
            juce::String heads = "completeBefore,completeAfter,headSeconds,headPlaying,transportPlaying,"
                                 "desired,prepared,adopted,firstReference\n";
            int last = -1;
            capture.start();
            mixer_rapid_waveform_detail::RapidDrive driven;
            juce::String controlFailure;
            try
            {
                driven = mixer_rapid_waveform_detail::driveRapid(
                    capture, *wetHost, catalogue.generation, frames, loopFrames,
                    [&]
                    {
                        audio.reclaimPluginHosts();
                        const int lo = capture.complete.load(std::memory_order_acquire);
                        if (lo == last)
                            return;
                        const auto d = audio.projectionDiagnostics();
                        const auto marker = audio.compensationGraphObservation();
                        const int hi = capture.complete.load(std::memory_order_acquire);
                        heads += juce::String(lo) + "," + juce::String(hi) + "," +
                                 juce::String(d.headSeconds, 9) + "," + juce::String(int(d.headPlaying)) +
                                 "," + juce::String(int(d.transportPlaying)) + "," +
                                 mixer_waveform_detail::markerCsv(marker) + "\n";
                        last = lo;
                    },
                    [&]
                    {
                        before = audio.projectionDiagnostics();
                        gainsBefore = audio.mixerGraphInitialisations();
                        starts = device->starts;
                        stops = device->stops;
                    });
            }
            catch (const std::exception &error)
            {
                controlFailure = error.what();
                driven.phase.controls = false;
                driven.phase.end = capture.complete.load();
            }
            capture.stop();                             // No concurrent reader of trace storage below.
            driven.phase.end = capture.complete.load(); // Include the last in-flight callback, too.
            const auto after = audio.projectionDiagnostics();
            const auto marker = audio.compensationGraphObservation();
            capture.write(folder, name);
            require(folder.getChildFile(name + "-controls.csv").replaceWithText(driven.phase.events) &&
                        folder.getChildFile(name + "-head.csv").replaceWithText(heads),
                    "All waveform control/head outcomes retained");
            const auto phase = mixer_waveform_detail::analyze(capture, frames, driven.phase, folder, name,
                                                              preparedTransportSamples);
            const auto loopAnalysis =
                mixer_rapid_waveform_detail::analyzeLoops(loopFrames, driven, folder, name);
            const auto resources = before.edit == after.edit &&
                                   before.playbackContext == after.playbackContext &&
                                   before.replacements == after.replacements &&
                                   before.samplerCreations == after.samplerCreations &&
                                   before.cache.preparations == after.cache.preparations &&
                                   gainsBefore == audio.mixerGraphInitialisations() && sourceHost->alive() &&
                                   wetHost->alive() && sourceHost->processId() == sourcePid &&
                                   wetHost->processId() == wetPid && device->starts == starts &&
                                   device->stops == stops && after.headPlaying && after.transportPlaying;
            const bool passed = phase.pass && loopAnalysis.pass && resources && marker.adopted != 0 &&
                                marker.adopted == marker.desired &&
                                audio.compensationGraphFailure().isEmpty();
            report +=
                name + " pass=" + juce::String(int(passed)) +
                " maxError=" + juce::String(phase.maximumError, 9) +
                " badSamples=" + juce::String(phase.badSamples) +
                " longestBadRun=" + juce::String(phase.longestBadRun) +
                " nonfinite=" + juce::String(phase.nonfinite) +
                " invalidQuantum=" + juce::String(phase.invalidQuanta) +
                " sourceClockQualified=" + juce::String(int(phase.sourceClockQualified)) +
                " resources=" + juce::String(int(resources)) + " overruns=" + juce::String(phase.overruns) +
                " pacingMisses=" + juce::String(phase.pacingMisses) +
                " allocations=" + juce::String(phase.allocations) +
                " destructions=" + juce::String(phase.destructions) +
                " graphFailure=" + audio.compensationGraphFailure() + " controlFailure=" + controlFailure +
                " rapidFailure=" + driven.failure + " requests=" + juce::String(driven.requests) +
                " consumedTransitions=" + juce::String(driven.observed) +
                " straddledBursts=" + juce::String(driven.straddledBursts) +
                " maximumRequestGap=" + juce::String(driven.maximumRequestGap) +
                " actualWraps=" + juce::String(loopAnalysis.actualWraps) +
                " matchedBoundaries=" + juce::String(loopAnalysis.matchedBoundaries) +
                " clockDiscontinuities=" + juce::String(loopAnalysis.clockDiscontinuities) +
                " invalidLoopStates=" + juce::String(loopAnalysis.invalidLoopStates) + "\n";
            if (!phase.pass)
                report += !phase.sourceClockQualified || phase.invalidQuanta
                              ? "  classification=clock/whole-quantum prerequisite failed; phase attribution "
                                "refused.\n"
                          : directChainPassed
                              ? "  classification=rapid graph/parallel-phase candidate; slower direct chain "
                                "passed; rapid native-only isolation remains unqualified.\n"
                              : "  classification=direct native/packet-tag baseline also failed; graph-only "
                                "attribution unavailable.\n";
            if (!loopAnalysis.pass)
                report += "  classification=rapid/actual-loop coverage failed; see explicit request, "
                          "consumed-transition, straddled-burst, matched-boundary and clock counters.\n";
            allPassed = allPassed && passed;
            require(reportFile.replaceWithText(report + "stage=" + name + "-captured\n"),
                    "Intermediate actual graph outcome retained");
            audio.stop();
        }
        catch (const std::exception &error)
        {
            // Run both additional modes even when one setup/control path fails.
            // Capture's RAII owner joins before the enclosing Engine is retired.
            allPassed = false;
            report += name + " pass=0 classification=setup_or_evidence_failure error=" + error.what() + "\n";
            require(reportFile.replaceWithText(report + "stage=" + name + "-failed\n"),
                    "Actual loop scenario failure retained BEFORE aggregate assertion");
        }
    }
    require(reportFile.replaceWithText(report + "stage=finished\n"),
            "All actual loop scenario outcomes retained");
    return allPassed;
}
