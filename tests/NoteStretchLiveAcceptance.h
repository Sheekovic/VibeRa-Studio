// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
static void noteStretchLiveTests(const juce::File &folder, const PluginSpec *instrument = nullptr)
{
    juce::String report;
    for (bool patternMode : {true, false})
    {
        AudioEngine audio(false, {}, 256);
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
        require(device != nullptr, "Note Stretch virtual device");
        Controller c;
        c.project.tempo = 240;
        const auto patternId = c.project.patterns.front().id, channel = c.project.channels.front().id;
        c.project.channels.front().source = "builtin:bass";
        c.project.channels.front().sampler.loop = true;
        c.project.channels.front().pan = -1;
        Channel held;
        held.id = c.project.allocate();
        held.source = "builtin:bass";
        held.sampler.loop = true;
        held.pan = 1;
        if (instrument)
        {
            c.project.channels.front().instrument = *instrument;
            c.project.channels.front().instrument->id = c.project.allocate();
        }
        c.project.channels.push_back(held);
        auto &pattern = c.project.patterns.front();
        pattern.length = 4;
        const auto edited = c.project.allocate();
        const auto edited2 = c.project.allocate();
        pattern.notes = {{edited, channel, 0, 2, 48, .8f, false}, {edited2, channel, .5, .5, 53, .6f, false}};
        c.project.placements = {{c.project.allocate(), patternId, 0, 0, 4, 0},
                                {c.project.allocate(), patternId, 0, 4, 4, 0}};
        require(awaitProjection(audio, c.project, patternMode, patternId).wasOk(),
                "Stretch prepared projection");
        audio.setMidiTarget(held.id);
        audio.setLooping(true);
        audio.playStop();
        dispatchMessages(20);
        juce::AudioBuffer<float> block(2, 256);
        int callbacks = 0, overruns = 0, silentHeld = 0, loops = 0;
        double maximum = 0, previous = 0;
        bool monitor = false;
        juce::String trace = "callback,ms,left,right,seconds\n";
        auto tick = [&](int count)
        {
            for (int i = 0; i < count; ++i)
            {
                callbackAllocationProbe = monitor;
                auto start = juce::Time::getHighResolutionTicks();
                device->process(block);
                auto ms =
                    juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - start) *
                    1000;
                callbackAllocationProbe = false;
                maximum = std::max(maximum, ms);
                overruns += ms > 256000. / 48000.;
                ++callbacks;
                auto head = audio.projectionDiagnostics().headSeconds;
                loops += head + .02 < previous;
                previous = head;
                silentHeld += monitor && block.getMagnitude(1, 0, 256) == 0;
                trace += juce::String(callbacks) + "," + juce::String(ms, 6) + "," +
                         juce::String(block.getMagnitude(0, 0, 256), 9) + "," +
                         juce::String(block.getMagnitude(1, 0, 256), 9) + "," + juce::String(head, 9) + "\n";
                dispatchMessages(1);
            }
        };
        tick(12);
        audio.midiInput.capture.begin();
        audio.midiInput.handleIncomingMidiMessage(nullptr,
                                                  juce::MidiMessage::noteOn(1, 72, juce::uint8(100)));
        tick(8);
        monitor = true;
        const auto before = audio.projectionDiagnostics();
        const auto workers = audio.pluginDiagnostics();
        const auto starts = device->starts, stops = device->stops;
        const auto allocations = callbackAllocations.load(), destructions = callbackDestructions.load();
        CommandArgs args;
        args.pattern = patternId;
        args.channel = channel;
        args.ids = {edited, edited2};
        args.beat = 0;
        args.value = .75;
        auto publish = [&]
        {
            const auto head = audio.projectionDiagnostics().headSeconds;
            require(awaitProjection(audio, c.project, patternMode, patternId).wasOk(),
                    "Stretch incremental publication");
            const auto now = audio.projectionDiagnostics();
            require(now.headPlaying && now.transportPlaying && std::abs(now.headSeconds - head) < 2. / 48000.,
                    "Note Stretch never restarts or seeks transport");
        };
        for (int edit = 0; edit < 40; ++edit)
        {
            const auto baseline = c.project.state();
            args.value = edit % 2 ? 2. : .75;
            require(c.execute("notes.stretch", args).wasOk(), "Live note Stretch");
            publish();
            tick(4);
            require(audio.projectionDiagnostics().channels.front().midiNotes == (patternMode ? 2 : 4),
                    "Split halves published into both linked score schedules");
            c.undoEdit();
            publish();
            tick(4);
            require(c.project.state().isEquivalentTo(baseline), "Live Stretch exact Undo");
            c.redoEdit();
            publish();
            tick(2);
            c.undoEdit();
            publish();
            tick(4);
            require(audio.midiInput.capture.active &&
                        audio.projectionDiagnostics().channels.back().heldInputKeys == 1,
                    "Stretch keeps unaffected held MIDI/capture ownership");
        }
        tick(208);
        require(c.execute("notes.stretch", args).wasOk(), "Final sustained-note Stretch");
        publish();
        args.ids.clear();
        for (const auto &note : c.project.pattern(patternId)->notes)
            if (note.channel == channel)
                args.ids.push_back(note.id);
        require(c.execute("notes.delete", args).wasOk(), "Remove both sustained split halves");
        publish();
        tick(instrument ? 750 : 60);
        require(block.getMagnitude(0, 0, 256) < (instrument ? 1e-5f : 1e-8f),
                "Deleted split score voices release without stuck notes");
        const auto after = audio.projectionDiagnostics();
        require(before.edit == after.edit && before.playbackContext == after.playbackContext &&
                    before.replacements == after.replacements &&
                    before.samplerCreations == after.samplerCreations &&
                    before.cache.preparations == after.cache.preparations && starts == device->starts &&
                    stops == device->stops,
                "Stretch retains edit/context/device/prepared cache");
        for (size_t i = 0; i < before.channels.size(); ++i)
            require(before.channels[i].sample == after.channels[i].sample &&
                        before.channels[i].sampler == after.channels[i].sampler,
                    "Stretch retains prepared sample/voice bank");
        if (instrument)
        {
            const auto current = audio.pluginDiagnostics();
            require(!current.empty() && current.size() == workers.size(), "Stretch real worker retained");
            for (size_t i = 0; i < workers.size(); ++i)
                require(current[i].alive && current[i].processId == workers[i].processId &&
                            current[i].packets.missed == workers[i].packets.missed &&
                            current[i].packets.dropped == workers[i].packets.dropped &&
                            current[i].packets.midiOverflow == workers[i].packets.midiOverflow,
                        "Stretch real plugin has no new misses/drops/overflow");
        }
        auto mode = juce::String(patternMode ? "pattern" : "song");
        folder.getChildFile("note-stretch-" + mode + "-callbacks.csv").replaceWithText(trace);
        report += mode + " callbacks=" + juce::String(callbacks) + " maxMs=" + juce::String(maximum, 4) +
                  " overruns=" + juce::String(overruns) + " silentHeld=" + juce::String(silentHeld) +
                  " loops=" + juce::String(loops) +
                  " allocations=" + juce::String(callbackAllocations.load() - allocations) +
                  " destructions=" + juce::String(callbackDestructions.load() - destructions) +
                  " edit/context/device replacements=0\n";
        folder.getChildFile("note-stretch-live.txt").replaceWithText(report);
        require(!overruns && !silentHeld && loops >= 2, "Stretch actual virtual PCM/loops/deadline");
        require(callbackAllocations.load() == allocations && callbackDestructions.load() == destructions,
                "Stretch callback host heap/lifetime safety");
        monitor = false;
        audio.midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 72));
        tick(8);
        require(audio.projectionDiagnostics().channels.back().heldInputKeys == 0,
                "Held MIDI releases without stuck key");
        audio.midiInput.capture.active = false;
        audio.stop();
    }
}
