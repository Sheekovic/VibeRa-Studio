// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
static void clipGroupsLiveTests(const juce::File &folder, const PluginSpec *instrument = nullptr)
{
    juce::String report;
    for (const bool patternMode : {true, false})
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
        require(device != nullptr, "Grouped virtual audio device");
        Controller c;
        c.project = groupedClipFixture(folder);
        c.project.tempo = 240;
        c.project.placements.resize(1);
        auto &pattern = c.project.patterns.front();
        pattern.length = 2;
        pattern.autoLength = false; // Group edits retain this exact source window.
        c.project.placements[0].beat = 0;
        c.project.placements[0].length = 1;
        c.project.audioPlacements[0].beat = 0;
        c.project.audioPlacements[0].duration = .25;
        c.project.automation.placements[0].beat = 0;
        c.project.automation.placements[0].length = 2;
        Channel held;
        held.id = c.project.allocate();
        held.source = "builtin:bass";
        held.sampler.loop = true;
        held.pan = 1;
        if (instrument)
        {
            held.instrument = *instrument;
            held.instrument->id = c.project.allocate();
        }
        c.project.channels.push_back(held);
        c.project.channels.front().source = "builtin:bass";
        c.project.channels.front().sampler.loop = true;
        c.project.channels.front().pan = -1;
        const auto first = c.project.channels.front().id, second = held.id, selectedPattern = pattern.id;
        CommandArgs args;
        args.ids = {c.project.placements[0].id, c.project.audioPlacements[0].id,
                    c.project.automation.placements[0].id};
        require(c.execute("clip.group", args).wasOk(), "Live heterogeneous group");
        args.ids.resize(1);
        args.length = 0;
        args.beat = .125;
        require(awaitProjection(audio, c.project, patternMode, selectedPattern).wasOk(),
                "Live group prepared projection");
        audio.setMidiTarget(second);
        audio.setLooping(true);
        audio.playStop();
        dispatchMessages(20);
        juce::AudioBuffer<float> block(2, 256);
        int callbacks = 0, overruns = 0, silentLeft = 0, silentRight = 0, loops = 0;
        double maximum = 0, previous = 0;
        bool monitoring = false;
        juce::String trace = "callback,wallMs,left,right,headSeconds\n";
        auto tick = [&](int count)
        {
            for (int i = 0; i < count; ++i)
            {
                callbackAllocationProbe = monitoring;
                const auto began = juce::Time::getHighResolutionTicks();
                device->process(block);
                const auto ms =
                    juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - began) *
                    1000;
                callbackAllocationProbe = false;
                maximum = std::max(maximum, ms);
                overruns += ms > 256000. / 48000.;
                ++callbacks;
                const auto head = audio.projectionDiagnostics().headSeconds;
                loops += head + .02 < previous;
                previous = head;
                silentLeft += monitoring && block.getMagnitude(0, 0, 256) == 0;
                silentRight += monitoring && block.getMagnitude(1, 0, 256) == 0;
                trace += juce::String(callbacks) + "," + juce::String(ms, 6) + "," +
                         juce::String(block.getMagnitude(0, 0, 256), 9) + "," +
                         juce::String(block.getMagnitude(1, 0, 256), 9) + "," + juce::String(head, 9) + "\n";
                dispatchMessages(1);
            }
        };
        tick(16);
        audio.midiInput.capture.begin();
        audio.midiInput.handleIncomingMidiMessage(nullptr,
                                                  juce::MidiMessage::noteOn(1, 72, juce::uint8(100)));
        require(audio.previewChannelNote(first, 48, true), "Held sample preview beside grouped arrangement");
        tick(8);
        monitoring = true;
        const auto before = audio.projectionDiagnostics();
        const auto workers = audio.pluginDiagnostics();
        const auto starts = device->starts, stops = device->stops;
        const auto allocations = callbackAllocations.load(), destructions = callbackDestructions.load();
        int boundaryWraps = 0;
        auto verifyHead = [&](double head)
        {
            const auto current = audio.projectionDiagnostics();
            const auto end = current.headLoopEndBeat * 60. / c.project.tempo;
            const bool wrapped = head >= end;
            boundaryWraps += wrapped;
            if (wrapped)
                previous = 0; // Count explicit range wraps separately from natural callback loops.
            require(current.headPlaying && current.transportPlaying &&
                        std::abs(current.headSeconds - (wrapped ? 0. : head)) < 2. / 48000.,
                    "Grouped edits preserve in-range playhead; shortened out-of-range loop wraps by existing "
                    "policy");
        };
        for (int edit = 0; edit < 40; ++edit)
        {
            const auto baseline = c.project.state();
            const auto head = audio.projectionDiagnostics().headSeconds;
            const auto operation = edit % 4 == 0   ? "clip.move"
                                   : edit % 4 == 1 ? "clip.duplicate"
                                   : edit % 4 == 2 ? "clip.delete"
                                                   : "clip.ungroup";
            require(c.execute(operation, args).wasOk() &&
                        awaitProjection(audio, c.project, patternMode, selectedPattern).wasOk(),
                    "Live grouped edit publication");
            verifyHead(head);
            tick(4);
            auto previousHead = audio.projectionDiagnostics().headSeconds;
            c.undoEdit();
            require(c.project.state().isEquivalentTo(baseline) &&
                        awaitProjection(audio, c.project, patternMode, selectedPattern).wasOk(),
                    "Grouped live exact Undo");
            verifyHead(previousHead);
            tick(4);
            previousHead = audio.projectionDiagnostics().headSeconds;
            c.redoEdit();
            require(awaitProjection(audio, c.project, patternMode, selectedPattern).wasOk(),
                    "Grouped live Redo");
            verifyHead(previousHead);
            tick(2);
            previousHead = audio.projectionDiagnostics().headSeconds;
            c.undoEdit();
            require(awaitProjection(audio, c.project, patternMode, selectedPattern).wasOk(),
                    "Grouped baseline projection");
            verifyHead(previousHead);
            tick(4);
            require(audio.midiInput.capture.active &&
                        audio.projectionDiagnostics().channels.back().heldInputKeys == 1,
                    "Group edits retain held MIDI/capture ownership");
        }
        tick(208); // At least two natural Song loops after the final baseline restore.
        const auto after = audio.projectionDiagnostics();
        require(before.edit == after.edit && before.playbackContext == after.playbackContext &&
                    before.replacements == after.replacements &&
                    before.samplerCreations == after.samplerCreations &&
                    before.cache.preparations == after.cache.preparations && starts == device->starts &&
                    stops == device->stops,
                "Group editing retains graph/context/device/cache");
        std::uint64_t graphRefreshes = 0;
        for (size_t i = 0; i < before.channels.size(); ++i)
        {
            require(before.channels[i].sample == after.channels[i].sample &&
                        before.channels[i].sampler == after.channels[i].sampler,
                    "Group edits retain prepared Channel/voice bank");
            graphRefreshes +=
                after.channels[i].graphInitialisations - before.channels[i].graphInitialisations;
        }
        if (instrument)
        {
            const auto current = audio.pluginDiagnostics();
            require(current.size() == workers.size() && !current.empty(), "Grouped actual plugin retained");
            for (size_t i = 0; i < current.size(); ++i)
                require(current[i].alive && current[i].processId == workers[i].processId &&
                            current[i].packets.missed == workers[i].packets.missed &&
                            current[i].packets.dropped == workers[i].packets.dropped &&
                            current[i].packets.midiOverflow == workers[i].packets.midiOverflow,
                        "Grouped editing retains actual worker without new misses/drops/overflow");
        }
        const auto mode = juce::String(patternMode ? "pattern" : "song");
        folder.getChildFile("clip-groups-" + mode + "-callbacks.csv").replaceWithText(trace);
        report +=
            mode + " callbacks=" + juce::String(callbacks) + " maxMs=" + juce::String(maximum, 4) +
            " overruns=" + juce::String(overruns) + " silentLeft=" + juce::String(silentLeft) +
            " silentRight=" + juce::String(silentRight) + " loops=" + juce::String(loops) +
            " allocations=" + juce::String(callbackAllocations.load() - allocations) +
            " destructions=" + juce::String(callbackDestructions.load() - destructions) +
            " graphReplacements=0 deviceStarts=0 deviceStops=0 boundaryWraps=" + juce::String(boundaryWraps) +
            " streamedGraphRefreshes=" + juce::String(graphRefreshes) + "\n";
        folder.getChildFile("clip-groups-live.txt").replaceWithText(report);
        require(!overruns && !silentLeft && !silentRight && loops >= 2,
                "Actual grouped Pattern/Song PCM/loops/deadline");
        require(callbackAllocations.load() == allocations && callbackDestructions.load() == destructions,
                "Grouped callback heap/lifetime safety");
        monitoring = false;
        audio.midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 72));
        audio.stopChannelPreview(first);
        tick(4);
        require(audio.projectionDiagnostics().channels.back().heldInputKeys == 0,
                "Grouped held key releases without stuck ownership");
        audio.midiInput.capture.active = false;
        audio.stop();
    }
}
