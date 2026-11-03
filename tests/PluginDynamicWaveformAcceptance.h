// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "ClockWaveform.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
// Include AFTER PluginDynamicLatencyAcceptance.h. Its existing constant/held
// scenarios remain unchanged. This is an additional real-worker experiment.
namespace mixer_waveform_detail
{
using namespace mixer_dynamic_detail;
struct Packet
{
    bool valid = false;
    std::uint64_t sequence = 0, submitted = 0, missed = 0, dropped = 0;
    std::uint32_t generation = 0;
    int native = -1, cursor = -1;
};
inline Packet packet(PluginHost &host) noexcept
{
    Packet result;
    const auto statistics = host.statistics();
    result.submitted = statistics.submitted;
    result.missed = statistics.missed;
    result.dropped = statistics.dropped;
    result.cursor = host.packetFrame(); // Called ONLY by its sole processing owner.
    if (const auto value = host.consumedLatency())
    {
        result.valid = true;
        result.sequence = value->sequence;
        result.generation = value->stamp.generation;
        result.native = value->stamp.nativeSamples;
    }
    return result;
}
struct Frame
{
    Packet sourceBefore, sourceAfter, wetBefore, wetAfter;
    CompensationGraphObservation markerBefore, markerAfter;
};
struct Result
{
    bool pass = false;
    bool sourceClockQualified = true;
    int badSamples = 0, longestBadRun = 0, nonfinite = 0, invalidQuanta = 0;
    int overruns = 0, pacingMisses = 0;
    unsigned allocations = 0, destructions = 0;
    double maximumError = 0;
};
inline void writeExpected(const juce::File &file, const std::array<std::vector<float>, 2> &pcm, int samples)
{
    auto stream = file.createOutputStream();
    require(bool(stream), "Owned waveform oracle output");
    stream->setPosition(0); stream->truncate();
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.release(), rate, 2, 32, {}, 0));
    const float *pointers[]{pcm[0].data(), pcm[1].data()};
    require(writer && writer->writeFromFloatArrays(pointers, 2, samples), "Expected waveform retained");
}
inline juce::String packetCsv(const Packet &p)
{
    return juce::String(int(p.valid)) + "," + juce::String(juce::int64(p.sequence)) + "," +
           juce::String(p.generation) + "," + juce::String(p.native) + "," + juce::String(p.cursor) + "," +
           juce::String(juce::int64(p.submitted)) + "," + juce::String(juce::int64(p.missed)) + "," +
           juce::String(juce::int64(p.dropped));
}
inline juce::String markerCsv(const CompensationGraphObservation &m)
{
    return juce::String(juce::int64(m.desired)) + "," + juce::String(juce::int64(m.prepared)) + "," +
           juce::String(juce::int64(m.adopted)) + "," +
           (m.firstAdoptedReference ? juce::String(*m.firstAdoptedReference) : juce::String("unavailable"));
}
inline Result analyze(const Capture &capture, const std::vector<Frame> &frames, const Drive &drive,
                      const juce::File &folder, const juce::String &name,
                      int expectedWetTransportSamples = block)
{
    Result result;
    const int count = capture.complete.load(std::memory_order_acquire);
    std::array<std::vector<float>, 2> expected{std::vector<float>(size_t(count * block)),
                                               std::vector<float>(size_t(count * block))};
    juce::String trace = "callback,firstAbsoluteSample,sourceBeforeValid,sourceBeforeSequence,sourceBeforeGeneration,sourceBeforeNative,sourceBeforeCursor,sourceBeforeSubmitted,sourceBeforeMissed,sourceBeforeDropped,sourceValid,sourceSequence,sourceGeneration,sourceNative,sourceCursor,sourceSubmitted,sourceMissed,sourceDropped,wetBeforeValid,wetBeforeSequence,wetBeforeGeneration,wetBeforeNative,wetBeforeCursor,wetBeforeSubmitted,wetBeforeMissed,wetBeforeDropped,wetValid,wetSequence,wetGeneration,wetNative,wetCursor,wetSubmitted,wetMissed,wetDropped,markerBeforeDesired,markerBeforePrepared,markerBeforeAdopted,markerBeforeFirstReference,markerDesired,markerPrepared,markerAdopted,markerFirstReference,oracleFirstSourceSample,qualifiedQuantum,maxError,badSamples\n";
    int run = 0;
    bool firstSourcePacket = true;
    std::uint64_t lastSourceSequence = 0;
    for (int i = 0; i < count; ++i)
    {
        const auto &f = frames[size_t(i)];
        const auto &s = f.sourceAfter, &w = f.wetAfter;
        const bool clockRange = s.valid && s.sequence <=
            std::uint64_t(std::numeric_limits<std::int64_t>::max() / block);
        if (s.valid)
        {
            result.sourceClockQualified = result.sourceClockQualified &&
                (firstSourcePacket ? s.sequence == 0 : s.sequence == lastSourceSequence + 1) &&
                s.native == 0 && s.missed == 0 && s.dropped == 0;
            firstSourcePacket = false;
            lastSourceSequence = s.sequence;
        }
        // An entire256-frame callback must consume ONE new packet per worker.
        // Never apply its last tag to earlier portions of a multi-quantum span.
        const bool quantum = clockRange && w.valid && f.sourceBefore.valid && f.wetBefore.valid &&
                             s.native == 0 && s.cursor == 0 && w.cursor == 0 &&
                             f.sourceBefore.cursor == 0 && f.wetBefore.cursor == 0 &&
                             s.sequence == f.sourceBefore.sequence + 1 &&
                             w.sequence == f.wetBefore.sequence + 1 &&
                             s.submitted == f.sourceBefore.submitted + 1 &&
                             w.submitted == f.wetBefore.submitted + 1 &&
                             s.missed == 0 && s.dropped == 0 && w.missed == 0 && w.dropped == 0;
        const auto first =
            clockRange ? std::int64_t(s.sequence) * block - expectedWetTransportSamples - w.native : -1;
        double blockError = 0;
        int blockBad = 0;
        for (int n = 0; n < block; ++n)
        {
            double error = 0;
            for (int channel = 0; channel < 2; ++channel)
            {
                const auto reference = quantum ? mixer_clock_waveform::sample(first + n, channel) : 0.f;
                expected[size_t(channel)][size_t(i * block + n)] = reference;
                const auto actual = capture.pcm[size_t(channel)][size_t(i * block + n)];
                if (!std::isfinite(actual))
                    error = std::numeric_limits<double>::infinity();
                else
                    error = std::max(error, std::abs(double(actual) - reference));
            }
            blockError = std::max(blockError, error);
            if (error > 2.e-6) ++blockBad;
            // Only one explicitly declared initial200-callback warmup. Every
            // transition sample after it is compared, without settling windows.
            if (i < drive.begin || i >= drive.end) continue;
            if (!std::isfinite(error)) ++result.nonfinite;
            result.maximumError = std::max(result.maximumError, error);
            if (error > 2.e-6)
            { ++result.badSamples; result.longestBadRun = std::max(result.longestBadRun, ++run); }
            else run = 0;
        }
        if (i >= drive.begin && i < drive.end)
        {
            if (!quantum) ++result.invalidQuanta;
            const auto &timing = capture.frames[size_t(i)];
            result.overruns += timing.callbackMs > block * 1000. / rate;
            result.pacingMisses += timing.releaseLatenessMs > block * 1000. / rate;
            result.allocations += timing.allocations;
            result.destructions += timing.destructions;
        }
        trace += juce::String(i) + "," + juce::String(i * block) + "," + packetCsv(f.sourceBefore) + "," +
                 packetCsv(s) + "," + packetCsv(f.wetBefore) + "," + packetCsv(w) + "," +
                 markerCsv(f.markerBefore) + "," + markerCsv(f.markerAfter) + "," + juce::String(first) + "," +
                 juce::String(int(quantum)) + "," + juce::String(blockError, 9) + "," + juce::String(blockBad) + "\n";
    }
    require(folder.getChildFile(name + "-phase.csv").replaceWithText(trace), "Full source/packet/marker traces retained");
    writeExpected(folder.getChildFile(name + "-expected-pcm.wav"), expected, count * block);
    result.pass = drive.controls && drive.begin >= 200 && drive.end > drive.begin &&
                  !capture.exhausted.load() && !capture.pacingFailed.load() && result.sourceClockQualified &&
                  !firstSourcePacket && !result.invalidQuanta && !result.nonfinite && !result.badSamples &&
                  !result.overruns && !result.pacingMisses && !result.allocations && !result.destructions;
    return result;
}
} // namespace mixer_waveform_detail

#include "PluginPhasePublicationAcceptance.h"
#include "PluginRapidWaveformAcceptance.h"

static void pluginDynamicWaveformTests(const juce::File &folder, const juce::File &generatorBinary,
                                       const juce::File &latencyBinary)
{
    using namespace mixer_dynamic_detail;
    require(folder.createDirectory().wasOk(), "Owned dynamic waveform evidence");
    auto generator = parameterSpec(generatorBinary), effect = parameterSpec(latencyBinary);
    require(generator.name == "VibeRa Clock Waveform Fixture" && !generator.instrument &&
                effect.name == "VibeRa Mixer Latency Fixture" && !effect.instrument,
            "Independent original native source and changing-latency effect descriptors");
    juce::String report = "DRAFT variable-waveform live-worker experiment,48000Hz/256frames. No "
                          "listening/hardware/native-input claim.\n"
                          "Legacy direct oracle subtracts256; independently declared production3Q app oracle "
                          "subtracts768. Master averages direct+wet.\n"
                          "The native read-tap phase jump is inherent and allowed; both branches must share "
                          "its consumed-packet phase.\n"
                          "Physical callback/source sequence clock, not score time. No offset "
                          "fitting/calibration against output.\n";
    const auto reportFile = folder.getChildFile("dynamic-waveform-live.txt");
    require(reportFile.replaceWithText(report + "stage=starting-direct-clock\n"), "Waveform experiment startup retained");
    bool allPassed = true;
    // Independently verify packet0->source sample0 using a REAL paced worker.
    // The scanner, control flushes and setProcessing must not advance its clock.
    {
        auto host = std::make_shared<PluginHost>();
        require(host->start(generator, rate, block, false).wasOk(), "Actual live generator baseline worker");
        std::int64_t samples = 0;
        Capture capture([&](juce::AudioBuffer<float> &buffer)
                        { buffer.clear(); host->process(buffer, 0, block, nullptr, 0,
                              double(samples) / rate, 120, true, false); samples += block; }, host);
        capture.start();
        const auto deadline = juce::Time::getMillisecondCounter() + 4000;
        while (capture.complete.load(std::memory_order_acquire) < 220 &&
               juce::Time::getMillisecondCounter() < deadline)
        { host->service(); dispatchMessages(1); }
        capture.stop(); capture.write(folder, "direct-clock");
        const int count = capture.complete.load();
        double error = 0;
        bool valid = count >= 220 && host->alive();
        for (int i = 1; i < count; ++i)
        {
            const auto &frame = capture.frames[size_t(i)];
            valid = valid && frame.consumedNativeLatency == 0 && frame.consumedSequence == std::uint64_t(i - 1) &&
                    !frame.missed && !frame.dropped && !frame.allocations && !frame.destructions;
            for (int channel = 0; channel < 2; ++channel)
                for (int n = 0; n < block; ++n)
                {
                    const auto actual = capture.pcm[size_t(channel)][size_t(i * block + n)];
                    error = !std::isfinite(actual) ? std::numeric_limits<double>::infinity() :
                        std::max(error, std::abs(double(actual) - mixer_clock_waveform::sample(std::int64_t(i - 1) * block + n, channel)));
                }
        }
        valid = valid && error < 2.e-6;
        report += "directClock valid=" + juce::String(int(valid)) + " maxError=" + juce::String(error, 9) + "\n";
        allPassed = allPassed && valid;
        require(reportFile.replaceWithText(report + "stage=starting-direct-variable-chain\n"), "Direct source outcome retained");
    }
    bool directChainPassed = false;
    {
        auto source = std::make_shared<PluginHost>(), wet = std::make_shared<PluginHost>();
        require(source->start(generator, rate, block, false).wasOk() &&
                    wet->start(effect, rate, block, false).wasOk(), "Actual live variable host-chain baseline");
        const auto catalogue = parameterCatalogue(*wet);
        require(parameterResult(*wet, wet->setParameter(17, 0, catalogue.generation)).result == 0,
                "Direct variable chain native0 control acknowledged");
        std::vector<mixer_waveform_detail::Frame> frames(capacity);
        int callback = 0;
        Capture capture([&](juce::AudioBuffer<float> &buffer)
        {
            auto &frame = frames[size_t(callback)];
            frame.sourceBefore = mixer_waveform_detail::packet(*source);
            frame.wetBefore = mixer_waveform_detail::packet(*wet);
            buffer.clear();
            const auto seconds = double(callback * block) / rate;
            source->process(buffer, 0, block, nullptr, 0, seconds, 120, true, false);
            wet->process(buffer, 0, block, nullptr, 0, seconds, 120, true, false);
            frame.sourceAfter = mixer_waveform_detail::packet(*source);
            frame.wetAfter = mixer_waveform_detail::packet(*wet);
            ++callback;
        }, wet);
        capture.start();
        Drive driven;
        juce::String controlFailure;
        try { driven = drive(capture, *wet, catalogue.generation, [&] { source->service(); wet->service(); }); }
        catch (const std::exception &error)
        { controlFailure = error.what(); driven.controls = false; driven.end = capture.complete.load(); }
        capture.stop(); driven.end = capture.complete.load(); capture.write(folder, "direct-variable-chain");
        require(folder.getChildFile("direct-variable-chain-controls.csv").replaceWithText(driven.events),
                "Direct variable chain controls retained");
        const auto result = mixer_waveform_detail::analyze(capture, frames, driven, folder, "direct-variable-chain");
        directChainPassed = result.pass && source->alive() && wet->alive();
        report += "directVariableChain pass=" + juce::String(int(directChainPassed)) +
                  " maxError=" + juce::String(result.maximumError, 9) + " badSamples=" + juce::String(result.badSamples) +
                  " invalidQuanta=" + juce::String(result.invalidQuanta) +
                  " sourceClockQualified=" + juce::String(int(result.sourceClockQualified)) +
                  " controlFailure=" + controlFailure + "\n";
        allPassed = allPassed && directChainPassed;
        require(reportFile.replaceWithText(report + "stage=starting-actual-graphs\n"), "Direct variable outcome retained");
    }
    for (const bool patternMode : {true, false})
    {
        const juce::String name = patternMode ? "wave-pattern" : "wave-song";
        AudioEngine audio(false, {}, block); // Actual physical callbacks are256 frames.
        auto &manager = audio.engine.getDeviceManager();
        manager.deviceManager.addAudioDeviceType(std::make_unique<SyntheticDeviceType>());
        juce::XmlElement setup("DEVICESETUP");
        setup.setAttribute("deviceType", "VibeRa synthetic");
        setup.setAttribute("audioOutputDeviceName", "In-memory output");
        setup.setAttribute("audioDeviceRate", double(rate)); setup.setAttribute("audioDeviceBufferSize", block);
        audio.engine.getPropertyStorage().setXmlProperty(te::SettingID::audio_device_setup, setup);
        manager.initialise(0, 2); dispatchMessages(20);
        auto *device = dynamic_cast<SyntheticDevice *>(manager.deviceManager.getCurrentAudioDevice());
        require(device && device->getCurrentSampleRate() == rate && device->getCurrentBufferSizeSamples() == block &&
                    te::getDefaultPanLaw() == te::PanLawLinear,
                "Exact48k256 virtual driver and unchanged linear-pan prerequisite");
        auto project = Project::empty();
        for (auto &insert : project.mixer) { insert.gain = 1; insert.pan = 0; }
        project.mixer[0].gain = .5f; // Independently specified average, not fitted amplitude.
        auto &channel = project.channels.front(); channel.mixer = 1; channel.gain = 1; channel.pan = 0;
        auto &pattern = project.patterns.front(); pattern.length = 64;
        pattern.notes = {{project.allocate(), channel.id, 0, 64, 60, 1, false}};
        project.placements = {{project.allocate(), pattern.id, 0, 0, 64, 0}};
        auto source = generator, wet = effect;
        source.id = project.allocate(); wet.id = project.allocate();
        project.mixer[1].effects = {source}; project.mixer[3].effects = {wet};
        require(setMixerRoute(project, 1, 3, true).wasOk(), "Dry Master plus unity wet parallel branch");
        require(awaitProjection(audio, project, patternMode, pattern.id).wasOk(), "Actual app-created Pattern/Song graph");
        auto sourceHost = audio.parameterHost(source.id), wetHost = audio.parameterHost(wet.id);
        require(sourceHost && wetHost && !sourceHost->offline() && !wetHost->offline(), "Two genuine LIVE workers");
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
        require(history.wasOk(), "Bounded retained-history experiment: " + history.getErrorMessage().toStdString());
        const auto sourcePid = sourceHost->processId(), wetPid = wetHost->processId();
        int starts = device->starts, stops = device->stops;
        auto before = audio.projectionDiagnostics();
        auto gainsBefore = audio.mixerGraphInitialisations();
        std::vector<mixer_waveform_detail::Frame> frames(capacity);
        int callback = 0;
        audio.setLooping(false); audio.playStop(); dispatchMessages(20);
        Capture capture([&](juce::AudioBuffer<float> &buffer)
        {
            auto &frame = frames[size_t(callback++)];
            frame.sourceBefore = mixer_waveform_detail::packet(*sourceHost);
            frame.wetBefore = mixer_waveform_detail::packet(*wetHost);
            frame.markerBefore = audio.compensationGraphObservation();
            device->process(buffer);
            frame.sourceAfter = mixer_waveform_detail::packet(*sourceHost);
            frame.wetAfter = mixer_waveform_detail::packet(*wetHost);
            frame.markerAfter = audio.compensationGraphObservation();
        }, wetHost);
        juce::String heads = "completeBefore,completeAfter,headSeconds,headPlaying,transportPlaying,desired,prepared,adopted,firstReference\n";
        int last = -1;
        capture.start();
        Drive driven;
        juce::String controlFailure;
        try {
            driven = drive(
                capture, *wetHost, catalogue.generation,
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
                             juce::String(d.headSeconds, 9) + "," + juce::String(int(d.headPlaying)) + "," +
                             juce::String(int(d.transportPlaying)) + "," +
                             mixer_waveform_detail::markerCsv(marker) + "\n";
                    last = lo;
                },
                [&]
                {
                    before = audio.projectionDiagnostics();
                    gainsBefore = audio.mixerGraphInitialisations();
                    starts = device->starts;
                    stops = device->stops;
                },
                preparedTransportSamples);
        }
        catch (const std::exception &error)
        { controlFailure = error.what(); driven.controls = false; driven.end = capture.complete.load(); }
        capture.stop(); // No concurrent reader of trace storage below.
        driven.end = capture.complete.load(); // Include the last in-flight callback, too.
        const auto after = audio.projectionDiagnostics();
        const auto marker = audio.compensationGraphObservation();
        capture.write(folder, name);
        require(folder.getChildFile(name + "-controls.csv").replaceWithText(driven.events) &&
                    folder.getChildFile(name + "-head.csv").replaceWithText(heads), "All waveform control/head outcomes retained");
        const auto phase =
            mixer_waveform_detail::analyze(capture, frames, driven, folder, name, preparedTransportSamples);
        const auto resources = before.edit == after.edit && before.playbackContext == after.playbackContext &&
                               before.replacements == after.replacements && before.samplerCreations == after.samplerCreations &&
                               before.cache.preparations == after.cache.preparations &&
                               gainsBefore == audio.mixerGraphInitialisations() && sourceHost->alive() && wetHost->alive() &&
                               sourceHost->processId() == sourcePid && wetHost->processId() == wetPid &&
                               device->starts == starts && device->stops == stops && after.headPlaying && after.transportPlaying;
        const bool passed = phase.pass && resources &&
                            marker.adopted != 0 && marker.adopted == marker.desired && audio.compensationGraphFailure().isEmpty();
        report += name + " pass=" + juce::String(int(passed)) + " maxError=" + juce::String(phase.maximumError, 9) +
                  " badSamples=" + juce::String(phase.badSamples) + " longestBadRun=" + juce::String(phase.longestBadRun) +
                  " nonfinite=" + juce::String(phase.nonfinite) + " invalidQuantum=" + juce::String(phase.invalidQuanta) +
                  " sourceClockQualified=" + juce::String(int(phase.sourceClockQualified)) +
                  " resources=" + juce::String(int(resources)) + " overruns=" + juce::String(phase.overruns) +
                  " pacingMisses=" + juce::String(phase.pacingMisses) + " allocations=" + juce::String(phase.allocations) +
                  " destructions=" + juce::String(phase.destructions) + " graphFailure=" + audio.compensationGraphFailure() +
                  " controlFailure=" + controlFailure + "\n";
        if (!phase.pass)
            report += !phase.sourceClockQualified || phase.invalidQuanta
                          ? "  classification=clock/whole-quantum prerequisite unavailable; phase attribution refused.\n"
                      : directChainPassed ? "  classification=graph/parallel-phase candidate; direct changing-waveform host chain passed.\n"
                                        : "  classification=direct native/packet-tag baseline also failed; graph-only attribution unavailable.\n";
        allPassed = allPassed && passed;
        require(reportFile.replaceWithText(report + "stage=" + name + "-captured\n"), "Intermediate actual graph outcome retained");
        audio.stop();
    }
    require(reportFile.replaceWithText(report + "stage=finished\n"), "Aggregate waveform findings retained BEFORE assertion");
    allPassed = pluginHeldPublicationWaveformTests(folder, generator, effect, directChainPassed) && allPassed;
    allPassed = pluginRapidWaveformTests(folder, generator, effect, directChainPassed) && allPassed;
    require(allPassed, "REAL-worker changing native latency keeps complete variable-waveform dry/wet phase and callback budgets");
}
