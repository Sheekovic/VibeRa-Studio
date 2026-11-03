// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
static void midiScoreLiveTests(const juce::File &folder, const PluginSpec *plugin = nullptr)
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
        require(device != nullptr, "MIDI import virtual device");
        Controller controller;
        auto project = Project::empty();
        project.tempo = 240;
        project.channels.resize(2);
        for (auto &channel : project.channels)
            channel.source = "builtin:bass";
        if (plugin)
        {
            project.channels[0].instrument = *plugin;
            project.channels[0].instrument->id = project.allocate();
        }
        auto &pattern = project.patterns.front();
        const auto patternId = pattern.id;
        const auto affected = project.channels[0].id, held = project.channels[1].id;
        pattern.notes.push_back({project.allocate(), held, 0, 4, 48, .7f, false});
        project.placements.push_back({project.allocate(), patternId, 0, 0, 4, 0});
        controller.replace(project);
        require(awaitProjection(audio, controller.project, patternMode, patternId).wasOk(),
                "MIDI import initial projection");
        audio.setMidiTarget(held);
        audio.setLooping(true);
        audio.playStop();
        dispatchMessages(30);
        juce::AudioBuffer<float> block(2, 256);
        std::vector<int> silentIndices;
        double maximum = 0;
        int callbacks = 0, overruns = 0, silent = 0;
        float peak = 0;
        auto tick = [&]
        {
            block.clear();
            const auto start = juce::Time::getHighResolutionTicks();
            device->process(block);
            const auto ms =
                juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - start) *
                1000.;
            maximum = std::max(maximum, ms);
            ++callbacks;
            overruns += ms > 256. / 48000. * 1000.;
            const auto magnitude = block.getMagnitude(0, 256);
            peak = std::max(peak, magnitude);
            silent += magnitude == 0;
            if (magnitude == 0)
                silentIndices.push_back(callbacks - 1);
            dispatchMessages(plugin ? 6 : 1);
        };
        for (int i = 0; i < 16; ++i)
            tick();
        audio.midiInput.handleIncomingMidiMessage(nullptr,
                                                  juce::MidiMessage::noteOn(1, 55, juce::uint8(100)));
        for (int i = 0; i < 4; ++i)
            tick();
        const auto baseline = audio.projectionDiagnostics();
        const auto pluginBefore = audio.pluginDiagnostics();
        const auto warmupSilent = silent;
        const auto warmupCallbacks = callbacks;
        const auto starts = device->starts, stops = device->stops;
        for (int i = 0; i < 80; ++i)
        {
            MidiScore score;
            score.notes = {{0, 0, .25 * (i % 8), .5 + .125 * (i % 4), 60 + i % 5, .6f, false}};
            auto head = audio.positionBeat();
            require(applyMidiScore(controller, patternId, affected, score, i % 5 == 0).wasOk(),
                    "MIDI live blend/replace");
            require(awaitProjection(audio, controller.project, patternMode, patternId).wasOk(),
                    "MIDI live atomic projection");
            require(audio.playing() && std::abs(audio.positionBeat() - head) < .01,
                    "MIDI import retains exact playhead before callbacks");
            if (i % 7 == 0)
            {
                controller.undoEdit();
                require(awaitProjection(audio, controller.project, patternMode, patternId).wasOk(),
                        "MIDI live undo projection");
                controller.redoEdit();
                require(awaitProjection(audio, controller.project, patternMode, patternId).wasOk(),
                        "MIDI live redo projection");
            }
            for (int j = 0; j < 4; ++j)
                tick();
        }
        const auto after = audio.projectionDiagnostics();
        report +=
            juce::String(patternMode ? "Pattern" : "Song") + ": callbacks=" + juce::String(callbacks) +
            " maxMs=" + juce::String(maximum, 4) + " overruns=" + juce::String(overruns) +
            " silentBlocks=" + juce::String(silent) + " peak=" + juce::String(peak, 5) +
            " graphReplacements=" + juce::String(juce::int64(after.replacements - baseline.replacements)) +
            " deviceStarts=" + juce::String(device->starts - starts) +
            " deviceStops=" + juce::String(device->stops - stops) + "\n";
        report += "WarmupCallbacks=" + juce::String(warmupCallbacks) +
                  " warmupSilent=" + juce::String(warmupSilent) +
                  " editCallbacks=" + juce::String(callbacks - warmupCallbacks) +
                  " editSilent=" + juce::String(silent - warmupSilent) + " silentIndices=";
        for (auto index : silentIndices)
            report += juce::String(index) + " ";
        report += "\n";
        const auto pluginAfter = audio.pluginDiagnostics();
        if (plugin)
        {
            require(pluginBefore.size() == 1 && pluginAfter.size() == 1 && pluginAfter[0].alive &&
                        pluginBefore[0].processId == pluginAfter[0].processId,
                    "MIDI import retains actual instrument worker");
            require(pluginAfter[0].packets.submitted > pluginBefore[0].packets.submitted,
                    "MIDI import schedules actual plugin processing");
            report += "VST3 worker=" + juce::String(pluginAfter[0].processId) +
                      " packets=" + juce::String(juce::int64(pluginAfter[0].packets.submitted)) +
                      " missed=" + juce::String(juce::int64(pluginAfter[0].packets.missed)) +
                      " dropped=" + juce::String(juce::int64(pluginAfter[0].packets.dropped)) + "\n";
        }
        folder.getChildFile(plugin ? "midi-import-vst3-live.txt" : "midi-import-live.txt")
            .replaceWithText(report + (plugin ? "Actual installed VST3 instrument plus built-in unaffected "
                                                "Channel; virtual callbacks, no hardware/listening.\n"
                                              : "Virtual synchronous callbacks, not hardware deadlines or "
                                                "listening; built-in instruments only.\n"));
        require(after.replacements == baseline.replacements &&
                    after.samplerCreations == baseline.samplerCreations && after.edit == baseline.edit &&
                    after.playbackContext == baseline.playbackContext && device->starts == starts &&
                    device->stops == stops && after.cache.preparations == baseline.cache.preparations,
                "MIDI import retains graph, device and prepared samples");
        require(silent == warmupSilent, "MIDI import edit window has no silent blocks");
        require(peak > .001f && overruns == 0, "MIDI import virtual PCM and callback deadline");
        for (size_t i = 0; i < baseline.channels.size(); ++i)
            require(after.channels[i].sample == baseline.channels[i].sample &&
                        after.channels[i].graphInitialisations == baseline.channels[i].graphInitialisations,
                    "MIDI import per-Channel lifetime stable");
        auto heldChannel = std::find_if(after.channels.begin(), after.channels.end(),
                                        [&](auto &d) { return d.channel == held; });
        require(heldChannel != after.channels.end() && heldChannel->midiNotes == 1,
                "MIDI import preserves unrelated held MIDI note");
        audio.midiInput.panic();
        audio.stop();
    }
}
