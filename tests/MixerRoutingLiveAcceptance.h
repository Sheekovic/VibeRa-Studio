// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
static void mixerRoutingLiveTests(const juce::File &folder, const PluginSpec *instrument = nullptr,
                                  bool phaseDiagnostic = false)
{
    juce::String report;
    for (bool patternMode : {true, false})
    {
        AudioEngine audio(false, {}, 256); // This driver actually submits fixed256-frame outer calls.
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
        require(device != nullptr, "Mixer routing virtual device");
        Controller c;
        c.project.tempo = 240;
        const auto patternId = c.project.patterns.front().id, channel = c.project.channels.front().id;
        c.project.channels.front().source = "builtin:bass";
        c.project.channels.front().sampler.loop = true;
        c.project.channels.front().pan = -1;
        c.project.channels.front().mixer = 1;
        Channel held;
        held.id = c.project.allocate();
        held.source = "builtin:bass";
        held.sampler.loop = true;
        held.pan = 1;
        held.mixer = 2;
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
        // Native VST3 instruments receive the real score MIDI path. The separate
        // sampler-settings preview API deliberately rejects hosted instruments.
        if (instrument)
            pattern.notes.front().length = pattern.length;
        c.project.placements = {{c.project.allocate(), patternId, 0, 0, 4, 0},
                                {c.project.allocate(), patternId, 0, 4, 4, 0}};
        require(setMixerRoute(c.project, 1, 3, true, true).wasOk(), "Prepare initial bus");
        const auto projection = awaitProjection(audio, c.project, patternMode, patternId);
        if (projection.failed())
            std::cerr << "Mixer projection refused: " << projection.getErrorMessage() << '\n';
        require(projection.wasOk(), "Mixer routing prepared projection");
        audio.setMidiTarget(held.id);
        audio.setLooping(true);
        audio.playStop();
        dispatchMessages(20);
        juce::AudioBuffer<float> block(2, 256);
        int callbacks = 0, overruns = 0, silentHeld = 0, loops = 0;
        double maximum = 0, previous = 0;
        bool monitor = false;
        juce::String trace = "callback,ms,left,right,seconds\n";
        juce::String phaseTrace = "callback,outerMs,cyclesKnown,threadCyclesBefore,threadCyclesAfter," +
                                  juce::String(SyntheticCallbackTiming::csvHeader) + "\n";
        bool phasesCoherent = true;
        auto tick = [&](int count)
        {
            for (int i = 0; i < count; ++i)
            {
                callbackAllocationProbe = monitor;
                ULONG64 cycleStart = 0, cycleEnd = 0;
                const bool cycleKnown =
                    !phaseDiagnostic || QueryThreadCycleTime(GetCurrentThread(), &cycleStart);
                auto start = juce::Time::getHighResolutionTicks();
                device->process(block);
                const auto end = juce::Time::getHighResolutionTicks();
                auto ms = juce::Time::highResolutionTicksToSeconds(end - start) * 1000;
                callbackAllocationProbe = false;
                if (phaseDiagnostic)
                {
                    const bool cycleComplete = cycleKnown &&
                                               QueryThreadCycleTime(GetCurrentThread(), &cycleEnd) &&
                                               cycleEnd >= cycleStart;
                    const auto &phase = device->lastTiming();
                    phasesCoherent = phasesCoherent && cycleComplete && phase.containedBy(start, end);
                    phaseTrace += juce::String(callbacks) + "," + juce::String(ms, 7) + "," +
                                  juce::String(int(cycleComplete)) + "," +
                                  juce::String(juce::uint64(cycleStart)) + "," +
                                  juce::String(juce::uint64(cycleEnd)) + "," + phase.csv(start, end) + "\n";
                }
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
        if (!instrument)
            require(audio.previewChannelNote(channel, 57, true), "Hold a routed sampler preview note");
        tick(8);
        monitor = true;
        const auto before = audio.projectionDiagnostics();
        const auto workers = audio.pluginDiagnostics();
        if (instrument)
        {
            const auto status = audio.pluginStatus(c.project.channels.front().instrument->id);
            folder.getChildFile(juce::String(patternMode ? "pattern" : "song") + "-provider-admission.txt")
                .replaceWithText("workers=" + juce::String(int(workers.size())) + " status=" + status + "\n");
            require(!workers.empty() && workers.front().alive,
                    "Actual installed worker admission; inspect provider-admission receipt");
        }
        const auto starts = device->starts, stops = device->stops;
        const auto allocations = callbackAllocations.load(), destructions = callbackDestructions.load();
        CommandArgs args;
        args.mixer = 1;
        args.target = 3;
        args.value = .5;
        const auto initialGraphs = audio.mixerGraphInitialisations();
        auto publish = [&]
        {
            const auto head = audio.projectionDiagnostics().headSeconds;
            const auto published = awaitProjection(audio, c.project, patternMode, patternId);
            if (published.failed())
            {
                const auto prefix = juce::String(patternMode ? "pattern" : "song");
                folder.getChildFile(prefix + "-publication-refusal.txt")
                    .replaceWithText("callbacks=" + juce::String(callbacks) + " error=" +
                                     published.getErrorMessage() + "\n");
                folder.getChildFile(prefix + "-partial-callbacks.csv").replaceWithText(trace);
                if (phaseDiagnostic)
                    folder.getChildFile(prefix + "-partial-phases.csv").replaceWithText(phaseTrace);
            }
            require(published.wasOk(), "Mixer routing incremental publication: " +
                                          published.getErrorMessage().toStdString());
            const auto now = audio.projectionDiagnostics();
            require(now.headPlaying && now.transportPlaying && std::abs(now.headSeconds - head) < 2. / 48000.,
                    "Mixer routing never restarts or seeks transport");
        };
        for (int edit = 0; edit < 40; ++edit)
        {
            const auto baseline = c.project.state();
            args.value = edit % 2 ? 1. : .25;
            require(c.execute("mixer.send", args).wasOk(), "Live note Mixer routing");
            publish();
            tick(4);
            require(audio.projectionDiagnostics().channels.front().midiNotes == (patternMode ? 2 : 4),
                    "Bus level updates retain linked score schedule");
            c.undoEdit();
            publish();
            tick(4);
            require(c.project.state().isEquivalentTo(baseline), "Live Mixer routing exact Undo");
            c.redoEdit();
            publish();
            tick(2);
            c.undoEdit();
            publish();
            tick(4);
            require(audio.midiInput.capture.active &&
                        audio.projectionDiagnostics().channels.back().heldInputKeys == 1,
                    "Mixer routing keeps unaffected held MIDI/capture ownership");
        }
        require(initialGraphs == audio.mixerGraphInitialisations(),
                "Send levels never rebuild the retained graph");
        const auto topologyBaseline = c.project.state();
        args.target = 4;
        require(c.execute("mixer.routeOnly", args).wasOk(), "Live route to another bus only");
        publish();
        tick(12);
        c.undoEdit();
        publish();
        tick(12);
        require(c.project.state().isEquivalentTo(topologyBaseline), "Topology Undo restores exact routes");
        c.redoEdit();
        publish();
        tick(12);
        c.undoEdit();
        publish();
        tick(12);
        tick(208);
        require(block.getMagnitude(0, 0, 256) > 0, "Routed sustained voice survives bus changes");
        if (!instrument)
            require(audio.previewChannelNote(channel, 57, false), "Release the routed sampler preview note");
        args.pattern = patternId;
        args.channel = channel;
        args.ids.clear();
        for (const auto &note : c.project.pattern(patternId)->notes)
            if (note.channel == channel)
                args.ids.push_back(note.id);
        require(c.execute("notes.delete", args).wasOk(), "Remove sustained routed score");
        publish();
        tick(instrument ? 750 : 60);
        require(block.getMagnitude(0, 0, 256) < (instrument ? 1e-5f : 1e-8f),
                "Deleted routed score voices release without stuck notes");
        const auto after = audio.projectionDiagnostics();
        juce::String graphCounts;
        const auto finalGraphs = audio.mixerGraphInitialisations();
        for (const auto &[insert, count] : finalGraphs)
        {
            const auto initial = std::find_if(initialGraphs.begin(), initialGraphs.end(),
                                              [&](const auto &entry) { return entry.first == insert; });
            graphCounts +=
                juce::String(insert) + ":" +
                juce::String(juce::int64(count - (initial == initialGraphs.end() ? 0 : initial->second))) +
                ",";
        }
        require(before.edit == after.edit && before.playbackContext == after.playbackContext &&
                    before.replacements == after.replacements &&
                    before.samplerCreations == after.samplerCreations &&
                    before.cache.preparations == after.cache.preparations && starts == device->starts &&
                    stops == device->stops,
                "Mixer routing retains edit/context/device/prepared cache");
        for (size_t i = 0; i < before.channels.size(); ++i)
            require(before.channels[i].sample == after.channels[i].sample &&
                        before.channels[i].sampler == after.channels[i].sampler,
                    "Mixer routing retains prepared sample/voice bank");
        if (instrument)
        {
            const auto current = audio.pluginDiagnostics();
            require(!current.empty() && current.size() == workers.size(),
                    "Mixer routing real worker retained");
            for (size_t i = 0; i < workers.size(); ++i)
                require(current[i].alive && current[i].processId == workers[i].processId &&
                            current[i].packets.missed == workers[i].packets.missed &&
                            current[i].packets.dropped == workers[i].packets.dropped &&
                            current[i].packets.midiOverflow == workers[i].packets.midiOverflow,
                        "Mixer routing real plugin has no new misses/drops/overflow");
        }
        auto mode = juce::String(patternMode ? "pattern" : "song");
        folder.getChildFile("mixer-routing-" + mode + "-callbacks.csv").replaceWithText(trace);
        if (phaseDiagnostic)
        {
            require(folder.getChildFile("mixer-routing-" + mode + "-driver-phases.csv")
                        .replaceWithText(phaseTrace),
                    "Retain diagnostic provider callback phases and raw thread cycles");
            require(phasesCoherent, "Provider diagnostic uses coherent actual outer/dispatch intervals");
        }
        report +=
            mode + " callbacks=" + juce::String(callbacks) + " maxMs=" + juce::String(maximum, 4) +
            " overruns=" + juce::String(overruns) + " silentHeld=" + juce::String(silentHeld) +
            " loops=" + juce::String(loops) +
            " allocations=" + juce::String(callbackAllocations.load() - allocations) +
            " destructions=" + juce::String(callbackDestructions.load() - destructions) +
            " edit/context/device replacements=0; level graph initialisations=0; topology graph deltas=" +
            graphCounts +
            (instrument ? "; routed voice=sustained VST3 score MIDI\n"
                        : "; routed voice=sampler settings preview\n");
        folder.getChildFile("mixer-routing-live.txt").replaceWithText(report);
        require(!overruns && !silentHeld && loops >= 2, "Mixer routing actual virtual PCM/loops/deadline");
        require(callbackAllocations.load() == allocations && callbackDestructions.load() == destructions,
                "Mixer routing callback host heap/lifetime safety");
        monitor = false;
        audio.midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 72));
        tick(8);
        require(audio.projectionDiagnostics().channels.back().heldInputKeys == 0,
                "Held MIDI releases without stuck key");
        audio.midiInput.capture.active = false;
        audio.stop();
    }
}
