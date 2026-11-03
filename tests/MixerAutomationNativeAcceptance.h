// SPDX-License-Identifier: AGPL-3.0-or-later
// Original native graph oracle; include after MixerStereoNativeAcceptance.h.
#pragma once
namespace vibera::test::mixer_automation_native
{
__declspec(noinline) inline void mode(const juce::File &output, bool patternMode, const juce::File &sample)
{
    using namespace mixer_stereo_native;
    constexpr int loopFrames = 1920; // 375BPM * quarter beat at48k; no buffer change.
    Id panEnd = 0, stereoEnd = 0, stereoSource = 0;
    auto fixture = std::make_unique<routing_lifetime::Fixture>(
        output, false, patternMode,
        [&](Project &p)
        {
            p.tempo = 375;
            p.patterns.front().length = .25;
            p.placements.front().length = .25;
            auto &channel = p.channels.front();
            channel.source = sample.getFullPathName();
            channel.sampler = {};
            channel.sampler.root = 60;
            channel.sampler.envelope = channel.sampler.normalize = false;
            // This is a held-note/release oracle. Imported one-shot samples
            // intentionally ignore NoteOff unless looping or enveloped.
            channel.sampler.loop = true;
            for (const auto kind :
                 {AutomationParameter::mixerPan, AutomationParameter::mixerStereoSeparation})
            {
                AutomationSource source;
                source.id = p.allocate();
                source.length = .25;
                source.points = {{p.allocate(), 0, 0}, {p.allocate(), .25, 1}};
                const AutomationKey key{kind, 1, 0};
                source.links = {{key, -1, 1}};
                p.automation.initialValues.push_back({key, 0});
                p.automation.placements.push_back({p.allocate(), source.id, 0, .25, 0, 1, false});
                if (kind == AutomationParameter::mixerPan)
                    panEnd = source.points.back().id;
                else
                {
                    stereoEnd = source.points.back().id;
                    stereoSource = source.id;
                }
                p.automation.sources.push_back(std::move(source));
            }
        });
    auto &f = *fixture;
    auto &project = f.controller.project;
    require(te::getDefaultPanLaw() == te::PanLawLinear && project.validate().wasOk() &&
                project.channels.front().sampler.loop && !project.channels.front().sampler.envelope,
            "Native automation independent original linear-law fixture");
    f.audio->setLooping(true);
    auto *context = f.nativeEdit().getTransport().getCurrentPlaybackContext();
    require(context && context->getNodePlayHead(), "Actual native automation context");
    const auto *head = context->getNodePlayHead();
    const auto origin = head->getReferenceSampleRange().getEnd();
    require(head->getPosition() == 0 && head->getLoopRange() == juce::Range<juce::int64>{0, loopFrames},
            "Original native automation oracle starts at zero with the exact1920-frame loop");
    auto baseline = f.audio->projectionDiagnostics();
    auto initialisations = f.audio->mixerGraphInitialisations();
    const int starts = f.device->starts, stops = f.device->stops;
    juce::AudioBuffer<float> block(2, quantum), actual(2, capacity), expected(2, capacity);
    actual.clear();
    expected.clear();
    VoiceOracle voice;
    int frames = 0, callbacks = 0, crossings = 0, completedLoops = 0;
    double panTop = 1, separationTop = 1;
    bool separationLinked = true, pinned = false;
    double maximumError = 0, maximumMs = 0;
    juce::String csv = "phase,frame,error,ms,new,delete,sourceCalls,loopCrossing,held\n";
    const auto drive = [&](const char *phase)
    {
        require(frames + quantum <= capacity, "Bounded native automation PCM capacity");
        const auto before = f.audio->projectionDiagnostics();
        const auto allocations = callbackAllocations.load(), destructions = callbackDestructions.load();
        const auto began = juce::Time::getHighResolutionTicks();
        {
            const juce::ScopedValueSetter<bool> probe(callbackAllocationProbe, true);
            f.device->process(block);
        }
        const auto ms = SyntheticCallbackTiming::milliseconds(began, juce::Time::getHighResolutionTicks());
        const auto added = callbackAllocations.load() - allocations,
                   deleted = callbackDestructions.load() - destructions;
        double error = 0;
        for (int n = 0; n < quantum; ++n)
        {
            const double fraction = double((frames + n) % loopFrames) / loopFrames;
            const double pan = patternMode ? 0 : -1 + 2 * fraction * panTop;
            const double separation =
                patternMode || !separationLinked ? 0 : -1 + 2 * fraction * separationTop;
            const double own = separation > 0 ? 1 - separation / 2 : 1;
            const double cross = separation > 0 ? separation / 2 : separation;
            const double envelope = voice.envelope(frames + n);
            const double left = envelope * (own * bed[0] + cross * bed[1]) * (1 - pan);
            const double right = envelope * (cross * bed[0] + own * bed[1]) * (1 + pan);
            for (int ch = 0; ch < 2; ++ch)
            {
                const double wanted = ch == 0 ? left : right;
                const auto value = block.getSample(ch, n);
                actual.setSample(ch, frames + n, value);
                expected.setSample(ch, frames + n, float(wanted));
                error = std::isfinite(value) ? std::max(error, std::abs(double(value) - wanted))
                                             : std::numeric_limits<double>::infinity();
            }
        }
        const auto now = f.audio->projectionDiagnostics();
        auto *currentContext = f.nativeEdit().getTransport().getCurrentPlaybackContext();
        require(currentContext == context && currentContext && currentContext->getNodePlayHead() == head &&
                    now.channels.size() == 1 && before.channels.size() == 1,
                "Native automation retains the actual one-channel context before readback");
        const auto range = head->getReferenceSampleRange();
        const bool crossing = frames / loopFrames != (frames + quantum - 1) / loopFrames;
        // TracktionNodePlayer stores each loop/tempo subrange on the head;
        // readback is its LAST processed subrange, not the physical callback.
        // Independently derive that exact tail, while retaining the whole
        // physical PCM/reference end and one source call per native subrange.
        const auto expectedStart = origin + (crossing ? (frames / loopFrames + 1) * loopFrames : frames);
        crossings += int(crossing);
        completedLoops += (frames + quantum) / loopFrames - frames / loopFrames;
        const auto calls = now.channels.front().renderCalls - before.channels.front().renderCalls;
        maximumError = std::max(maximumError, error);
        maximumMs = std::max(maximumMs, ms);
        csv += juce::String(phase) + "," + juce::String(frames) + "," + juce::String(error, 12) + "," +
               juce::String(ms, 6) + "," + juce::String(juce::int64(added)) + "," +
               juce::String(juce::int64(deleted)) + "," + juce::String(juce::int64(calls)) + "," +
               juce::String(int(crossing)) + "," + juce::String(now.channels.front().heldInputKeys) + "\n";
        frames += quantum;
        ++callbacks;
        require(output.getChildFile("callbacks.csv").replaceWithText(csv),
                "Retain every native automation callback");
        writeRaw(output.getChildFile("actual.raw"), actual, frames);
        writeRaw(output.getChildFile("expected.raw"), expected, frames);
        const bool stable =
            now.edit == baseline.edit && now.playbackContext == baseline.playbackContext &&
            now.replacements == baseline.replacements && now.samplerCreations == baseline.samplerCreations &&
            now.cache.preparations == baseline.cache.preparations &&
            now.midiUpdates == baseline.midiUpdates &&
            now.channels.front().sampler == baseline.channels.front().sampler &&
            now.channels.front().sample == baseline.channels.front().sample &&
            now.channels.front().graphInitialisations == baseline.channels.front().graphInitialisations &&
            f.audio->mixerGraphInitialisations() == initialisations;
        require(output.getChildFile("latest.txt")
                    .replaceWithText("referenceStart=" + juce::String(range.getStart()) +
                                     " referenceEnd=" + juce::String(range.getEnd()) +
                                     " expectedStart=" + juce::String(expectedStart) + " expectedEnd=" +
                                     juce::String(origin + frames) + " stable=" + juce::String(int(stable)) +
                                     " started=" + juce::String(f.device->starts) +
                                     " stopped=" + juce::String(f.device->stops) +
                                     " playing=" + juce::String(int(f.audio->playing())) +
                                     " error=" + juce::String(error, 12) + "\n"),
                "Retain exact reference/ownership diagnostic before assertions");
        require(error <= 2e-6 && added == 0 && deleted == 0 && ms <= 1000. * quantum / rate &&
                    range.getStart() == expectedStart && range.getEnd() == origin + frames &&
                    range.getLength() == origin + frames - expectedStart && calls == (crossing ? 2 : 1) &&
                    (!pinned || stable) && f.device->starts == starts && f.device->stops == stops &&
                    f.audio->playing() && f.device->getCurrentSampleRate() == rate &&
                    f.device->getCurrentBufferSizeSamples() == quantum,
                "Native automation complete PCM, loop clock, single source per subrange, owners, heap and "
                "deadline");
        dispatchMessages(6);
    };
    for (int n = 0; n < 4; ++n)
        drive("startup_exact_silence");
    baseline = f.audio->projectionDiagnostics();
    initialisations = f.audio->mixerGraphInitialisations();
    pinned = true;
    voice.on = frames;
    f.audio->midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOn(1, 60, juce::uint8(127)));
    for (int n = 0; n < 5; ++n)
        drive("held_input_across_loop");
    const auto publish = [&]
    {
        const auto position = f.audio->positionBeat();
        require(awaitProjection(*f.audio, project, patternMode, f.pattern).wasOk() &&
                    f.audio->positionBeat() == position,
                "Automation edit retains exact transport cursor");
    };
    const auto endpoint = [&](Id id, double value)
    {
        CommandArgs args;
        args.target = id;
        args.beat = .25;
        args.value = value;
        require(f.controller.execute("automation.point.move", args).wasOk(), "Canonical live endpoint edit");
        publish();
    };
    endpoint(panEnd, .25);
    panTop = .25;
    drive("live_pan_endpoint");
    endpoint(stereoEnd, .75);
    separationTop = .75;
    drive("live_separation_endpoint");
    require(f.controller.undoEditChecked().wasOk(), "Actual automation Undo");
    publish();
    separationTop = 1;
    drive("undo_separation");
    require(f.controller.redoEditChecked().wasOk(), "Actual automation Redo");
    publish();
    separationTop = .75;
    drive("redo_separation");
    const auto baselineState = project.state();
    require(f.controller.beginGesture("native-automation-owner"), "Owned native automation gesture");
    for (double value : {.2, .8, .4})
    {
        CommandArgs args;
        args.target = panEnd;
        args.beat = .25;
        args.value = value;
        require(f.controller.execute("automation.point.move", args, "native-automation-owner").wasOk(),
                "Actual owned rapid pan automation edit");
        publish();
        panTop = value;
        drive("rapid_owned_pan_edit");
    }
    require(f.controller.cancelGestureChecked("native-automation-owner").wasOk() &&
                baselineState.isEquivalentTo(project.state()),
            "Actual gesture cancel restores exact automation project");
    publish();
    panTop = .25;
    drive("cancel_pan_preview");
    CommandArgs remove;
    remove.target = stereoSource;
    require(f.controller.execute("automation.source.delete", remove).wasOk(), "Actual live source removal");
    publish();
    separationLinked = false;
    drive("remove_source_retains_zero_initialization");
    remove = {};
    remove.mixer = 1;
    remove.automationControl = MixerAutomationControl::stereoSeparation;
    require(f.controller.execute("automation.init.remove", remove).wasOk(),
            "Actual manual fallback removal of init");
    publish();
    drive("removed_init_manual_fallback");
    require(f.controller.undoEditChecked().wasOk() && f.controller.undoEditChecked().wasOk(),
            "Separate source and initialization history restores both");
    publish();
    separationLinked = true;
    drive("undo_source_and_init");
    for (int n = 0; n < 4; ++n)
        drive("restored_lanes_across_loop");
    voice.off = frames;
    f.audio->midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 60));
    for (int n = 0; n < 4; ++n)
        drive("held_release_complete_tail");
    require(block.getMagnitude(0, 0, quantum) == 0 && block.getMagnitude(1, 0, quantum) == 0 &&
                f.audio->projectionDiagnostics().channels.front().heldInputKeys == 0 && completedLoops >= 3 &&
                crossings >= 2,
            "No stuck input note, exact final silence, three complete loops and two straddling callbacks");
    require(output.getChildFile("passed.txt")
                .replaceWithText(
                    "PASS native " + juce::String(patternMode ? "Pattern" : "Song") +
                    " callbacks=" + juce::String(callbacks) + " frames=" + juce::String(frames) +
                    " maxError=" + juce::String(maximumError, 12) + " maxMs=" + juce::String(maximumMs, 6) +
                    "; synthetic device/programmatic MIDI only, no hardware/listening/native input\n"),
            "Retain actual terminal native automation result");
}
inline void tests(const juce::File &folder)
{
    require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh owned native automation attempt");
    const auto sample = mixer_stereo_native::writeSource(folder);
    routing_lifetime::Storage storage(folder.getChildFile("settings"));
    juce::String failures;
    for (bool pattern : {true, false})
        try
        {
            mode(folder.getChildFile(pattern ? "pattern" : "song"), pattern, sample);
        }
        catch (const std::exception &error)
        {
            failures += juce::String(pattern ? "Pattern: " : "Song: ") + error.what() + "\n";
        }
    require(folder.getChildFile("outcomes.txt")
                .replaceWithText(failures.isEmpty() ? "PASS both native modes\n" : failures),
            "Retain both independent native automation outcomes");
    require(failures.isEmpty(), failures.toStdString());
}
} // namespace vibera::test::mixer_automation_native
