// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
namespace mixer_held_phase_detail
{
using namespace mixer_dynamic_detail;
struct HeldDrive
{
    Drive phase;
    int firstChanged = -1, heldEnd = -1, releaseCallback = -1, adoptedCallback = -1;
    bool fourOldCallbacks = false;
    std::uint64_t oldEpoch = 0;
    juce::String failure;
};
inline HeldDrive driveHeld(Capture &capture, AudioEngine &audio, PluginHost &host, unsigned generation,
                           const std::vector<mixer_waveform_detail::Frame> &frames,
                           const std::function<void()> &service, const std::function<void()> &afterWarm)
{
    HeldDrive result;
    result.phase.events =
        "request,requestCallback,ackCallback,firstConsumed1024Callback,heldEnd,releaseCallback,"
        "adoptedCallback,oldEpoch,fourOldCallbacks,failure\n";
    const auto began = juce::Time::getMillisecondCounter();
    const auto withinBudget = [&]
    {
        return juce::uint32(juce::Time::getMillisecondCounter() - began) < 12000 &&
               !capture.exhausted.load() && host.alive();
    };
    const auto wait = [&](const auto &predicate)
    {
        while (withinBudget())
        {
            if (predicate())
                return true; // A parameter receipt is consuming.
            service();
            dispatchMessages(1);
        }
        return predicate();
    };
    result.phase.controls = wait([&] { return capture.complete.load(std::memory_order_acquire) >= 200; });
    result.phase.begin = capture.complete.load(std::memory_order_acquire);
    if (!result.phase.controls)
    {
        result.failure = "warmup_timeout_or_worker_failure";
        result.phase.end = result.phase.begin;
        return result;
    }
    afterWarm();
    const auto marker = audio.compensationGraphObservation();
    result.oldEpoch = marker.adopted;
    result.phase.controls = marker.adopted && marker.desired == marker.adopted &&
                            marker.prepared == marker.adopted &&
                            audio.holdCompensationGraphPublicationForTest(true).wasOk();
    const auto requestCallback = capture.complete.load(std::memory_order_acquire);
    const auto request = result.phase.controls ? host.setParameter(17, 1.f, generation) : 0;
    PluginParameterResult ack{};
    const bool acknowledged = request && wait([&] { return host.readParameterResult(request, ack); });
    const auto ackCallback = capture.complete.load(std::memory_order_acquire);
    const auto changed = [&]
    {
        const auto count = capture.complete.load(std::memory_order_acquire);
        for (int index = requestCallback; index < count; ++index)
            if (frames[size_t(index)].wetAfter.valid && frames[size_t(index)].wetAfter.native == 1024)
            {
                result.firstChanged = index;
                return true;
            }
        return false;
    };
    result.phase.controls =
        result.phase.controls && acknowledged && ack.result == 0 && wait(changed) &&
        wait([&] { return capture.complete.load(std::memory_order_acquire) >= result.firstChanged + 4; });
    result.heldEnd = capture.complete.load(std::memory_order_acquire);
    result.fourOldCallbacks = result.phase.controls;
    if (result.firstChanged >= 0 && result.firstChanged + 4 <= result.heldEnd)
        for (int index = result.firstChanged; index < result.firstChanged + 4; ++index)
        {
            const auto &frame = frames[size_t(index)];
            result.fourOldCallbacks = result.fourOldCallbacks && frame.wetAfter.native == 1024 &&
                                      frame.markerBefore.desired == result.oldEpoch &&
                                      frame.markerBefore.adopted == result.oldEpoch &&
                                      frame.markerAfter.desired == result.oldEpoch &&
                                      frame.markerAfter.adopted == result.oldEpoch;
        }
    else
        result.fourOldCallbacks = false;
    // Only GRAPH publication was held. Native parameter acknowledgement, packet
    // consumption and audio have continued at their ordinary unchanged cadence.
    result.releaseCallback = capture.complete.load(std::memory_order_acquire);
    const bool released = audio.holdCompensationGraphPublicationForTest(false).wasOk();
    const bool adopted = released && result.phase.controls &&
                         wait(
                             [&]
                             {
                                 const auto now = audio.compensationGraphObservation();
                                 return now.desired != result.oldEpoch && now.prepared == now.desired &&
                                        now.adopted == now.desired;
                             });
    result.adoptedCallback = capture.complete.load(std::memory_order_acquire);
    const auto tail = result.adoptedCallback + 12;
    result.phase.controls = result.phase.controls && result.fourOldCallbacks && adopted &&
                            wait([&] { return capture.complete.load(std::memory_order_acquire) >= tail; });
    if (!result.phase.controls)
        result.failure = !request                   ? "scope_or_request_refused"
                         : !acknowledged            ? "ack_timeout_or_worker_failure"
                         : ack.result != 0          ? "parameter_failure"
                         : result.firstChanged < 0  ? "changed_packet_unconfirmed"
                         : !result.fourOldCallbacks ? "four_changed_callbacks_under_old_root_unconfirmed"
                         : !adopted                 ? "replacement_adoption_unconfirmed"
                                                    : "tail_timeout_or_worker_failure";
    result.phase.end = capture.complete.load(std::memory_order_acquire);
    result.phase.events += juce::String(request) + "," + juce::String(requestCallback) + "," +
                           juce::String(ackCallback) + "," + juce::String(result.firstChanged) + "," +
                           juce::String(result.heldEnd) + "," + juce::String(result.releaseCallback) + "," +
                           juce::String(result.adoptedCallback) + "," +
                           juce::String(juce::uint64(result.oldEpoch)) + "," +
                           juce::String(int(result.fourOldCallbacks)) + "," + result.failure + "\n";
    return result;
}
} // namespace mixer_held_phase_detail

static bool pluginHeldPublicationWaveformTests(const juce::File &folder, const PluginSpec &generator,
                                               const PluginSpec &effect, bool directChainPassed)
{
    using namespace mixer_dynamic_detail;
    juce::String report = "DRAFT actual-worker Pattern/Song consumed-phase acceptance.48k256.\n"
                          "Each actual native0->1024 change is consumed through at least4complete callbacks "
                          "while old root remains selected; then publication resumes.\n"
                          "No worker or fixture gating,PCM fitting,excluded transition "
                          "samples,buffer/deadline changes,native-input/hardware/listening claim.\n";
    const auto reportFile = folder.getChildFile("held-phase-publication.txt");
    require(reportFile.replaceWithText(report + "stage=starting\n"),
            "Held-publication experiment start retained");
    bool allPassed = true;
    for (const bool patternMode : {true, false})
    {
        const juce::String name = patternMode ? "held-phase-pattern" : "held-phase-song";
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
            pattern.length = 64;
            pattern.notes = {{project.allocate(), channel.id, 0, 64, 60, 1, false}};
            project.placements = {{project.allocate(), pattern.id, 0, 0, 64, 0}};
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
            const auto rejectedBefore = audio.projectionDiagnostics();
            require(
                audio.configureCompensationHistory(4096, 16 * 1024 * 1024, wet.id, source.id).failed() &&
                    audio.configureCompensationHistory(4096, 16 * 1024 * 1024, source.id, 0).failed() &&
                    audio.configureCompensationHistory(128, 16 * 1024 * 1024, source.id, wet.id).failed(),
                "Closed-scope admission rejects reversed routes, incomplete pair and below-quantum capacity");
            const auto rejectedAfter = audio.projectionDiagnostics();
            require(rejectedBefore.edit == rejectedAfter.edit &&
                        rejectedBefore.playbackContext == rejectedAfter.playbackContext &&
                        rejectedBefore.replacements == rejectedAfter.replacements &&
                        audio.compensationGraphObservation().desired == 0 && sourceHost->alive() &&
                        wetHost->alive(),
                    "Refused phase scopes preserve the selected graph and owned native workers");
            const auto history =
                audio.configureCompensationHistory(4096, 16 * 1024 * 1024, source.id, wet.id);
            require(history.wasOk(),
                    "Bounded retained-history experiment: " + history.getErrorMessage().toStdString());
            const auto sourcePid = sourceHost->processId(), wetPid = wetHost->processId();
            int starts = device->starts, stops = device->stops;
            auto before = audio.projectionDiagnostics();
            auto gainsBefore = audio.mixerGraphInitialisations();
            std::vector<mixer_waveform_detail::Frame> frames(capacity);
            int callback = 0;
            audio.setLooping(false);
            audio.playStop();
            dispatchMessages(20);
            Capture capture(
                [&](juce::AudioBuffer<float> &buffer)
                {
                    auto &frame = frames[size_t(callback++)];
                    frame.sourceBefore = mixer_waveform_detail::packet(*sourceHost);
                    frame.wetBefore = mixer_waveform_detail::packet(*wetHost);
                    frame.markerBefore = audio.compensationGraphObservation();
                    device->process(buffer);
                    frame.sourceAfter = mixer_waveform_detail::packet(*sourceHost);
                    frame.wetAfter = mixer_waveform_detail::packet(*wetHost);
                    frame.markerAfter = audio.compensationGraphObservation();
                },
                wetHost);
            juce::String heads = "completeBefore,completeAfter,headSeconds,headPlaying,transportPlaying,"
                                 "desired,prepared,adopted,firstReference\n";
            int last = -1;
            capture.start();
            mixer_held_phase_detail::HeldDrive driven;
            juce::String controlFailure;
            try
            {
                driven = mixer_held_phase_detail::driveHeld(
                    capture, audio, *wetHost, catalogue.generation, frames,
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
            const auto resources = before.edit == after.edit &&
                                   before.playbackContext == after.playbackContext &&
                                   before.replacements == after.replacements &&
                                   before.samplerCreations == after.samplerCreations &&
                                   before.cache.preparations == after.cache.preparations &&
                                   gainsBefore == audio.mixerGraphInitialisations() && sourceHost->alive() &&
                                   wetHost->alive() && sourceHost->processId() == sourcePid &&
                                   wetHost->processId() == wetPid && device->starts == starts &&
                                   device->stops == stops && after.headPlaying && after.transportPlaying;
            const bool passed = phase.pass && driven.fourOldCallbacks && resources && marker.adopted != 0 &&
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
                " heldFailure=" + driven.failure + " firstChanged=" + juce::String(driven.firstChanged) +
                " heldEnd=" + juce::String(driven.heldEnd) +
                " releaseCallback=" + juce::String(driven.releaseCallback) +
                " adoptedCallback=" + juce::String(driven.adoptedCallback) +
                " oldEpoch=" + juce::String(juce::uint64(driven.oldEpoch)) +
                " fourOldCallbacks=" + juce::String(int(driven.fourOldCallbacks)) + "\n";
            if (!phase.pass)
                report += !phase.sourceClockQualified || phase.invalidQuanta
                              ? "  classification=clock/whole-quantum prerequisite unavailable; phase "
                                "attribution refused.\n"
                          : directChainPassed ? "  classification=graph/parallel-phase candidate; direct "
                                                "changing-waveform host chain passed.\n"
                                              : "  classification=direct native/packet-tag baseline also "
                                                "failed; graph-only attribution unavailable.\n";
            allPassed = allPassed && passed;
            require(reportFile.replaceWithText(report + "stage=" + name + "-captured\n"),
                    "Intermediate actual graph outcome retained");
            audio.stop();
        }
        catch (const std::exception &error)
        {
            allPassed = false;
            report += name + " pass=0 classification=setup_or_evidence_failure error=" + error.what() + "\n";
            require(reportFile.replaceWithText(report + "stage=" + name + "-failed\n"),
                    "Held-publication failure retained");
        }
    }
    require(reportFile.replaceWithText(report + "stage=finished\n"),
            "Both held-publication outcomes retained before assertion");
    return allPassed;
}
