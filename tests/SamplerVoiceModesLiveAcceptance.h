// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../src/AudioEngine.h"
#include "../src/Commands.h"
#include "SyntheticDevice.h"
#include <array>
#include <limits>

// Include after the existing require, awaitProjection, dispatchMessages and
// callback allocation/destruction probes. Original virtual application evidence;
// provider sound is deliberately not compared with a fabricated unity oracle.
static void samplerVoiceModesLiveTests(const juce::File &folder,
                                       const vibera::Project *genuineHostedFixture = nullptr,
                                       bool noteModes = false)
{
    using namespace vibera;
    const PluginSpec *instrument = nullptr;
    if (genuineHostedFixture)
    {
        for (const auto &channel : genuineHostedFixture->channels)
            if (channel.instrument)
            {
                instrument = &*channel.instrument;
                break;
            }
        require(instrument != nullptr, "Genuine hosted voice-mode fixture contains an instrument");
    }
    // 257 frames includes the interpolation endpoint; the actual loop is256
    // samples. A root60 source therefore repeats once per256-frame callback.
    // The signed nonconstant reference is independent of the five edited controls.
    const auto source = folder.getChildFile("voice-modes-original-periodic.wav");
    require(!source.exists(), "Fresh owned voice-mode source");
    {
        juce::AudioBuffer<float> pcm(2, 257);
        for (int frame = 0; frame < 257; ++frame)
            for (int channel = 0; channel < 2; ++channel)
                pcm.setSample(channel, frame, float((frame % 256) - 128) / 2048.f);
        auto stream = source.createOutputStream();
        require(bool(stream), "Original voice-mode WAV stream");
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            format.createWriterFor(stream.release(), 48000., 2, 24, {}, 0));
        require(writer && writer->writeFromAudioSampleBuffer(pcm, 0, 257), "Original signed PCM persisted");
    }
    for (const bool patternMode : {true, false})
    {
        const juce::String mode = patternMode ? "pattern" : "song";
        const auto evidence = folder.getChildFile("voice-modes-" + mode + "-live.txt");
        juce::String report = "stage=setup\nvirtual256/48k; no hardware/listening or vendor-unity claim\n";
        struct Retain
        {
            juce::File file;
            juce::String &text;
            ~Retain()
            {
                file.replaceWithText(text);
            }
        } retain{evidence, report};
        auto owner = std::make_unique<AudioEngine>(false, juce::File{}, 256);
        auto &audio = *owner;
        auto &manager = audio.engine.getDeviceManager();
        manager.deviceManager.addAudioDeviceType(std::make_unique<SyntheticDeviceType>());
        juce::XmlElement setup("DEVICESETUP");
        setup.setAttribute("deviceType", "VibeRa synthetic");
        setup.setAttribute("audioOutputDeviceName", "In-memory output");
        setup.setAttribute("audioDeviceRate", 48000.);
        setup.setAttribute("audioDeviceBufferSize", 256);
        audio.engine.getPropertyStorage().setXmlProperty(te::SettingID::audio_device_setup, setup);
        manager.initialise(0, 2);
        dispatchMessages(20);
        auto *device = dynamic_cast<SyntheticDevice *>(manager.deviceManager.getCurrentAudioDevice());
        require(device && device->getCurrentSampleRate() == 48000 &&
                    device->getCurrentBufferSizeSamples() == 256,
                "Exact virtual output without opening physical input");
        Controller controller;
        controller.project.channels.resize(instrument ? 3 : 2);
        auto &project = controller.project;
        project.tempo = 281.25; // Exactly10240frames per beat:40 callbacks per musical loop.
        for (size_t index = 0; index < project.channels.size(); ++index)
        {
            auto &channel = project.channels[index];
            channel.source = source.getFullPathName();
            channel.mixer = int(index + 1);
            channel.pan = index == 1 ? 1.f : -1.f;
            channel.gain = .5f;
            channel.sampler.root = 60;
            channel.sampler.loop = channel.sampler.envelope = true;
            channel.sampler.attack = channel.sampler.decay = channel.sampler.release = 0;
            channel.sampler.sustain = 1;
            channel.sampler.filterType = 0;
        }
        if (instrument)
        {
            project.channels[2].instrument = *instrument;
            project.channels[2].instrument->id = project.allocate();
            require(project.channels[2].instrument->descriptor == instrument->descriptor &&
                        project.channels[2].instrument->state == instrument->state,
                    "Actual supplied provider descriptor/opaque state retained exactly");
        }
        const auto edited = project.channels[0].id, independent = project.channels[1].id;
        auto &pattern = project.patterns.front();
        const auto patternID = pattern.id;
        pattern.autoLength = false;
        pattern.length = 1;
        pattern.notes.clear();
        for (const auto &channel : project.channels)
            pattern.notes.push_back({project.allocate(), channel.id, 0, 1, 60, .4f, false});
        const auto ordinaryID = pattern.notes.front().id;
        const auto hostedNoteID = instrument ? pattern.notes[2].id : 0;
        Id slideID = 0;
        if (noteModes)
        {
            Note control{project.allocate(), edited, .1, .5, 72, .3f, false};
            control.slide = true;
            slideID = control.id;
            pattern.notes.push_back(control);
        }
        project.placements = {{project.allocate(), patternID, 0, 0, 1, 0}};
        require(awaitProjection(audio, project, patternMode, patternID).wasOk(),
                "Voice-mode real projection");
        audio.setCountInBars(0);
        audio.setMidiTarget(independent);
        audio.setLooping(true);
        audio.seekBeat(0);
        audio.playStop();
        dispatchMessages(20);
        juce::AudioBuffer<float> block(2, 256);
        std::array<float, 256> reference{};
        bool measured = false, unchangedRight = true;
        int callbacks = 0, loops = 0, overruns = 0, invalid = 0, nonfinite = 0;
        double maximum = 0, rightError = 0, previousHead = 0;
        const char *phase = "warmup";
        auto tick = [&](int count)
        {
            for (int index = 0; index < count; ++index)
            {
                const auto oldProbe = callbackAllocationProbe;
                callbackAllocationProbe = measured;
                const auto began = juce::Time::getHighResolutionTicks();
                device->process(block);
                const auto ended = juce::Time::getHighResolutionTicks();
                callbackAllocationProbe = oldProbe;
                const auto head = audio.projectionDiagnostics().headSeconds;
                if (measured)
                {
                    const double ms = SyntheticCallbackTiming::milliseconds(began, ended);
                    maximum = std::max(maximum, ms);
                    overruns += ms >= 256000. / 48000.;
                    invalid += !device->lastTiming().containedBy(began, ended);
                    loops += head + .02 < previousHead;
                    ++callbacks;
                    for (int frame = 0; frame < 256; ++frame)
                    {
                        for (int channel = 0; channel < 2; ++channel)
                            nonfinite += !std::isfinite(block.getSample(channel, frame));
                        if (unchangedRight)
                            rightError = std::max(rightError, std::abs(double(block.getSample(1, frame)) -
                                                                       reference[size_t(frame)]));
                    }
                    report += juce::String(callbacks) + "," + phase + "," + juce::String(ms, 6) + "," +
                              juce::String(head, 9) + "," + juce::String(rightError, 9) + "\n";
                }
                previousHead = head;
                dispatchMessages(1);
            }
        };
        tick(20);
        audio.midiInput.capture.begin();
        audio.midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOn(1, 60, juce::uint8(90)));
        require(audio.previewChannelNote(edited, 60, true), "Actual independent held sampler preview");
        tick(12);
        require(block.getMagnitude(0, 0, 256) > .001f && block.getMagnitude(1, 0, 256) > .001f,
                "Real score and held input produce stereo PCM before edits");
        for (int frame = 0; frame < 256; ++frame)
            reference[size_t(frame)] = block.getSample(1, frame);
        require(*std::min_element(reference.begin(), reference.end()) < 0 &&
                    *std::max_element(reference.begin(), reference.end()) > 0,
                "Independent unchanged-output witness is signed and nonconstant");
        const auto lane = audio.midiInput.retainRecordingLane();
        require(bool(lane), "Retain real performance owner");
        const auto overflow = lane->overflows.load(), panic = lane->panicEpoch.load();
        const auto captured = audio.midiInput.capture.write.load();
        const auto before = audio.projectionDiagnostics();
        const auto heldBefore = std::find_if(before.channels.begin(), before.channels.end(),
                                             [&](const auto &row) { return row.channel == independent; });
        require(heldBefore != before.channels.end() && heldBefore->heldInputKeys == 1,
                "Real callback acknowledged held performance owner before edits");
        const auto graphs = audio.mixerGraphInitialisations();
        const auto workers = audio.pluginDiagnostics();
        const auto starts = device->starts, stops = device->stops;
        const auto allocations = callbackAllocations.load(), destructions = callbackDestructions.load();
        measured = true;
        const auto publish = [&]
        {
            const auto head = audio.projectionDiagnostics().headSeconds;
            require(awaitProjection(audio, project, patternMode, patternID).wasOk() &&
                        std::abs(audio.projectionDiagnostics().headSeconds - head) < 1. / 48000.,
                    "Scalar projection preserves actual playhead without another callback");
        };
        const std::array<const char *, 5> commands{"channel.sampler.mono", "channel.sampler.portamento",
                                                   "channel.sampler.slideTime", "channel.sampler.gateBeats",
                                                   "channel.sampler.fullPortamento"};
        for (size_t index = 0; index < commands.size(); ++index)
        {
            phase = commands[index];
            report += "stage=" + juce::String(phase) + "\n";
            if (index == 3 && !instrument)
            {
                // Earlier Mono admissions at loop boundaries intentionally
                // release the original preview. Establish a new held preview
                // with Gate Off before testing a later Gate edit; never infer
                // survival of a note that the earlier Mono scenario suppressed.
                require(audio.previewChannelNote(edited, 60, false), "Release prior preview owner");
                tick(1);
                require(audio.previewChannelNote(edited, 60, true), "Fresh Gate-Off held preview");
                tick(1);
                require(block.getMagnitude(0, 0, 256) > .001f,
                        "Fresh preview actually sounds before changing Gate");
            }
            const auto baseline = project.state();
            CommandArgs args;
            args.channel = edited;
            args.enabled = true;
            args.value = index == 2 ? .15 : .125;
            require(controller.execute(phase, args).wasOk(), "Actual canonical voice-mode edit");
            const auto changed = project.state();
            const auto revision = controller.revision;
            require(controller.execute(phase, args).wasOk() && controller.revision == revision &&
                        project.state().isEquivalentTo(changed),
                    "Exact voice-mode no-op");
            publish();
            tick(6);
            if (index == 3 && !instrument)
                require(block.getMagnitude(0, 0, 256) > .001f,
                        "New Gate does not retroactively close the already admitted held preview");
            require(controller.undoEditChecked().wasOk() && project.state().isEquivalentTo(baseline),
                    "One exact voice-mode Undo");
            publish();
            tick(2);
            require(controller.redoEditChecked().wasOk() && project.state().isEquivalentTo(changed),
                    "One exact voice-mode Redo");
            publish();
            tick(2);
            require(controller.undoEditChecked().wasOk(), "Restore actual opening settings");
            publish();
            tick(2);
            const juce::String gesture = "original-live-voice-modes";
            require(controller.beginGesture(gesture) && controller.execute(phase, args, gesture).wasOk(),
                    "Owned real live preview");
            publish();
            tick(2);
            require(controller.cancelGestureChecked(gesture).wasOk() &&
                        project.state().isEquivalentTo(baseline),
                    "Cancel restores exact source/history");
            publish();
            tick(2);
        }
        if (noteModes)
        {
            for (const char *command :
                 {"notes.portamento", "notes.colourGroup", "notes.properties", "notes.slide"})
            {
                phase = command;
                const auto original = project.state();
                CommandArgs edit;
                edit.pattern = patternID;
                edit.channel = edited;
                edit.ids = {std::string(command) == "notes.properties" ? slideID : ordinaryID};
                edit.enabled = true;
                edit.value = 5;
                if (std::string(command) == "notes.properties")
                {
                    NotePropertiesPatch patch;
                    patch.pitch = 67;
                    patch.velocity = .5;
                    patch.pan = .3;
                    patch.length = .4;
                    edit.noteProperties = patch;
                }
                require(controller.execute(command, edit).wasOk(), "Canonical live per-note edit");
                const auto changed = project.state();
                publish();
                tick(6);
                require(controller.undoEditChecked().wasOk() && project.state().isEquivalentTo(original),
                        "Per-note live Undo preserves exact raw source");
                publish();
                tick(3);
                require(controller.redoEditChecked().wasOk() && project.state().isEquivalentTo(changed),
                        "Per-note live Redo restores intended mode/properties");
                publish();
                tick(3);
                require(controller.undoEditChecked().wasOk(), "Restore source before gesture");
                publish();
                tick(2);
                const juce::String owner = "note-mode-live-owned";
                require(controller.beginGesture(owner) && controller.execute(command, edit, owner).wasOk(),
                        "Actual owned per-note preview");
                publish();
                tick(2);
                require(controller.cancelGestureChecked(owner).wasOk() &&
                            project.state().isEquivalentTo(original),
                        "Actual per-note cancellation");
                publish();
                tick(2);
            }
        }
        if (noteModes && instrument)
        {
            phase = "actual-hosted-note-color16";
            const auto baseline = project.state();
            CommandArgs color;
            color.pattern = patternID;
            color.channel = project.channels[2].id;
            color.ids = {hostedNoteID};
            color.value = 15;
            require(controller.execute("notes.colourGroup", color).wasOk(),
                    "Actual supplied provider score changes to MIDI channel16");
            publish();
            tick(6);
            require(controller.undoEditChecked().wasOk() && project.state().isEquivalentTo(baseline),
                    "Actual supplied provider color Undo is exact");
            publish();
            tick(3);
            require(controller.redoEditChecked().wasOk(), "Actual supplied provider color Redo");
            publish();
            tick(3);
            require(controller.undoEditChecked().wasOk(), "Restore actual provider source color");
            publish();
            tick(3);
        }
        phase = "invalid-and-loop";
        const auto baseline = project.state();
        const auto revision = controller.revision;
        CommandArgs bad;
        bad.channel = edited;
        bad.value = std::numeric_limits<double>::quiet_NaN();
        require(controller.execute("channel.sampler.slideTime", bad).failed() &&
                    controller.revision == revision && project.state().isEquivalentTo(baseline),
                "Invalid live scalar refuses atomically");
        if (instrument)
        {
            bad.channel = project.channels[2].id;
            bad.enabled = true;
            require(controller.execute("channel.sampler.mono", bad).failed() &&
                        controller.revision == revision && project.state().isEquivalentTo(baseline),
                    "Actual hosted sibling refuses local sampler controls without publication");
        }
        // Deliberately add and release another performance note after the
        // unchanged-lane PCM phase. Its changed PCM is not an unchanged oracle;
        // deadline, heap, capture and original held-owner checks remain active.
        unchangedRight = false;
        phase = "additional-performance-note";
        audio.midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOn(1, 67, juce::uint8(75)));
        audio.midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 67));
        tick(4);
        const auto after = audio.projectionDiagnostics();
        report += "callbacks=" + juce::String(callbacks) + " maximumMs=" + juce::String(maximum, 6) +
                  " overruns=" + juce::String(overruns) + " loops=" + juce::String(loops) +
                  " rightMaximumError=" + juce::String(rightError, 9) +
                  " allocations=" + juce::String(juce::int64(callbackAllocations.load() - allocations)) +
                  " destructions=" + juce::String(juce::int64(callbackDestructions.load() - destructions)) +
                  "\n";
        evidence.replaceWithText(report); // Keep actual observations even if a following invariant refuses.
        require(before.edit == after.edit && before.playbackContext == after.playbackContext &&
                    before.replacements == after.replacements &&
                    before.samplerCreations == after.samplerCreations &&
                    before.cache.preparations == after.cache.preparations &&
                    graphs == audio.mixerGraphInitialisations() && starts == device->starts &&
                    stops == device->stops && audio.playing() && before.headPlaying && after.headPlaying &&
                    loops >= 2 &&
                    std::llround(after.headSeconds * 48000.) - std::llround(before.headSeconds * 48000.) +
                            juce::int64(loops) * 10240 ==
                        juce::int64(callbacks) * 256,
                "Looping actual playhead advances without replacing graph/device/prepared PCM");
        for (const auto &old : before.channels)
        {
            const auto now = std::find_if(after.channels.begin(), after.channels.end(),
                                          [&](const auto &row) { return row.channel == old.channel; });
            require(now != after.channels.end() && now->track == old.track && now->sample == old.sample &&
                        now->sampler == old.sampler &&
                        now->graphInitialisations == old.graphInitialisations &&
                        now->renderCalls > old.renderCalls && now->timelineMisses == old.timelineMisses &&
                        now->samplerAutomationFailures == old.samplerAutomationFailures &&
                        (old.channel != independent || now->heldInputKeys == old.heldInputKeys),
                    "Every actual source and independent held owner remains processed and retained");
        }
        require(audio.midiInput.capture.active && !audio.midiInput.capture.failed &&
                    audio.midiInput.capture.write.load() > captured &&
                    lane == audio.midiInput.retainRecordingLane() && lane->overflows.load() == overflow &&
                    lane->panicEpoch.load() == panic,
                "Actual performance capture/lane stays owned without overflow/panic");
        const auto currentWorkers = audio.pluginDiagnostics();
        require(currentWorkers.size() == workers.size() && (!instrument || !workers.empty()),
                "Expected actual provider ownership");
        for (const auto &old : workers)
        {
            const auto now = std::find_if(currentWorkers.begin(), currentWorkers.end(),
                                          [&](const auto &row) { return row.id == old.id; });
            report +=
                "provider=" + juce::String(juce::int64(old.id)) +
                " pidBefore=" + juce::String(old.processId) +
                " pidAfter=" + juce::String(now == currentWorkers.end() ? 0u : now->processId) +
                " processedBefore=" + juce::String(juce::int64(old.packets.processed)) + " processedAfter=" +
                juce::String(juce::int64(now == currentWorkers.end() ? 0 : now->packets.processed)) + "\n";
            require(now != currentWorkers.end() && old.alive && now->alive && old.processId != 0 &&
                        now->processId == old.processId && now->packets.processed > old.packets.processed &&
                        now->packets.missed == old.packets.missed &&
                        now->packets.dropped == old.packets.dropped &&
                        now->packets.midiOverflow == old.packets.midiOverflow,
                    "Genuine supplied provider actually processes without restart or packet loss");
        }
        evidence.replaceWithText(report); // Retain actual measurements before the gate assertion.
        require(!overruns && !invalid && !nonfinite && rightError <= .000001 &&
                    callbackAllocations.load() == allocations && callbackDestructions.load() == destructions,
                "Every measured256/48k callback meets5.333ms with unchanged right PCM and no probed "
                "heap/destruction");
        measured = false;
        audio.midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 60));
        audio.stopChannelPreview(edited);
        tick(4);
        for (const auto &row : audio.projectionDiagnostics().channels)
            if (row.channel == independent)
                require(row.heldInputKeys == 0, "Natural key-off clears exact held owner without panic");
        audio.midiInput.capture.active = false;
        audio.stop();
        report += "stage=passed\n";
    }
}
