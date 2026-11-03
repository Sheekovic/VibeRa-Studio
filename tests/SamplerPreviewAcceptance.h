// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "CallbackOverloadRecoveryTests.h"
static void samplerPreviewTests(const juce::File &folder)
{
    juce::String report;
    for (int mode = 0; mode < 3; ++mode)
    {
        AudioEngine audio(false);
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
        require(device != nullptr, "sampler preview virtual device");
        auto project = Project::empty();
        project.tempo = 240;
        project.channels.resize(2);
        for (auto &channel : project.channels)
        {
            channel.source = "builtin:bass";
            channel.sampler.envelope = true;
            channel.sampler.release = .012;
        }
        project.channels[0].pan = -1;
        project.channels[1].pan = 1;
        const auto first = project.channels[0].id, second = project.channels[1].id;
        project.placements.push_back({project.allocate(), project.patterns.front().id, 0, 0, 4, 0});
        const auto original = project.state();
        require(awaitProjection(audio, project, mode != 2, project.patterns.front().id).wasOk(),
                "sampler preview initial projection");
        audio.setMidiTarget(second);
        dispatchMessages(30); // Deliver graph/parameter setup before measuring a stopped/live context.
        audio.setLooping(true);
        if (mode != 0)
            audio.playStop();
        juce::AudioBuffer<float> block(2, 256);
        int callbacks = 0, overruns = 0, loops = 0;
        std::array<double, 512> callbackMs{};
        std::array<std::uint64_t, 512> callbackCycles{};
        std::array<SyntheticCallbackSpan, 512> callbackSpans{};
        struct Observation
        {
            const char *phase;
            int cycle, starts, stops;
            float left, right;
            AudioEngine::ProjectionDiagnostics projection;
            double engineCpuBefore, engineCpuAfter, deviceCpuAfter;
        };
        std::vector<Observation> observations;
        observations.reserve(callbackMs.size());
        const char *phase = "initial-preview";
        int cycle = -1;
#if JUCE_WINDOWS
        const auto observedThread = GetCurrentThreadId();
        const auto observedPriority = GetThreadPriority(GetCurrentThread());
#endif
        double maximum = 0, previous = 0;
        auto tick = [&](int count)
        {
            for (int i = 0; i < count; ++i)
            {
                require(callbacks < int(callbackMs.size()), "sampler preview diagnostic capacity");
                block.clear();
#if JUCE_WINDOWS
                ULONG64 cyclesBefore = 0, cyclesAfter = 0;
                const bool cycleClock = QueryThreadCycleTime(GetCurrentThread(), &cyclesBefore) != 0;
#endif
                const auto cpuBefore = manager.getCpuUsage();
                const auto began = juce::Time::getHighResolutionTicks();
                device->process(block);
                const auto ended = juce::Time::getHighResolutionTicks();
                const auto ms = juce::Time::highResolutionTicksToSeconds(ended - began) * 1000;
#if JUCE_WINDOWS
                if (cycleClock && QueryThreadCycleTime(GetCurrentThread(), &cyclesAfter) &&
                    cyclesAfter >= cyclesBefore)
                    callbackCycles[size_t(callbacks)] = cyclesAfter - cyclesBefore;
#endif
                callbackSpans[size_t(callbacks)] = {device->lastTiming(), began, ended};
                require(device->lastTiming().containedBy(began, ended),
                        "sampler preview timing spans nested");
                callbackMs[size_t(callbacks)] = ms;
                maximum = std::max(maximum, ms);
                overruns += ms > 256. / 48000. * 1000;
                ++callbacks;
                auto projection = audio.projectionDiagnostics(); // Outside the measured callback.
                const auto head = projection.headSeconds;
                loops += head + .025 < previous;
                previous = head;
                observations.push_back({phase, cycle, device->starts, device->stops,
                                        block.getMagnitude(0, 0, 256), block.getMagnitude(1, 0, 256),
                                        std::move(projection), cpuBefore, manager.getCpuUsage(),
                                        manager.deviceManager.getCpuUsage()});
            }
        };
        auto writeDiagnostics = [&]
        {
            juce::String trace = "callback,wallMs,threadCycles,deadlineOverrun,phase,cycle,left,right,"
                                 "headSeconds,headPlaying,transportPlaying,playPending,deviceStarts,"
                                 "deviceStops,edit,playbackContext,replacements,samplerCreations,"
                                 "engineCpuBefore,engineCpuAfter,deviceCpuAfter\n";
            juce::String channels = "callback,channel,renderCalls,timelineMisses,graphInitialisations,"
                                    "lastTime,lastLeadSkip,lastPlaying,sampler,sample\n";
            juce::String phases =
                "callback,outerMs," + juce::String(SyntheticCallbackTiming::csvHeader) + "\n";
            for (int i = 0; i < callbacks; ++i)
            {
                const auto &o = observations[size_t(i)];
                const auto &p = o.projection;
                const auto &span = callbackSpans[size_t(i)];
                phases += juce::String(i) + "," + juce::String(callbackMs[size_t(i)], 6) + "," +
                          span.timing.csv(span.began, span.ended) + "\n";
                trace += juce::String(i) + "," + juce::String(callbackMs[size_t(i)], 6) + "," +
                         juce::String(juce::int64(callbackCycles[size_t(i)])) + "," +
                         juce::String(int(callbackMs[size_t(i)] > 256000. / 48000.)) + "," + o.phase + "," +
                         juce::String(o.cycle) + "," + juce::String(o.left, 9) + "," +
                         juce::String(o.right, 9) + "," + juce::String(p.headSeconds, 9) + "," +
                         juce::String(int(p.headPlaying)) + "," + juce::String(int(p.transportPlaying)) +
                         "," + juce::String(int(p.playPending)) + "," + juce::String(o.starts) + "," +
                         juce::String(o.stops) + "," + juce::String::toHexString(juce::int64(p.edit)) + "," +
                         juce::String::toHexString(juce::int64(p.playbackContext)) + "," +
                         juce::String(juce::int64(p.replacements)) + "," +
                         juce::String(juce::int64(p.samplerCreations)) + "," +
                         juce::String(o.engineCpuBefore, 9) + "," + juce::String(o.engineCpuAfter, 9) + "," +
                         juce::String(o.deviceCpuAfter, 9) + "\n";
                for (const auto &c : p.channels)
                    channels += juce::String(i) + "," + juce::String(c.channel) + "," +
                                juce::String(juce::int64(c.renderCalls)) + "," +
                                juce::String(juce::int64(c.timelineMisses)) + "," +
                                juce::String(juce::int64(c.graphInitialisations)) + "," +
                                juce::String(c.lastTime, 9) + "," + juce::String(c.lastLeadSkip) + "," +
                                juce::String(int(c.lastPlaying)) + "," +
                                juce::String::toHexString(juce::int64(c.sampler)) + "," +
                                juce::String::toHexString(juce::int64(c.sample)) + "\n";
            }
            const auto prefix = "sampler-preview-" + juce::String(mode);
            folder.getChildFile(prefix + "-callbacks.csv").replaceWithText(trace);
            folder.getChildFile(prefix + "-channels.csv").replaceWithText(channels);
            folder.getChildFile(prefix + "-phases.csv").replaceWithText(phases);
#if JUCE_WINDOWS
            folder.getChildFile(prefix + "-thread.txt")
                .replaceWithText("Owned synthetic output caller TID=" + juce::String(int(observedThread)) +
                                 " priority=" + juce::String(observedPriority) +
                                 "; unchanged test scheduling, no priority adjustment. Raw cycle counts are "
                                 "not CPU-time conversion or proof of a failure cause.\n");
#endif
        };
        auto checked = [&](bool condition, const char *message)
        {
            if (!condition)
                writeDiagnostics();
            require(condition, message);
        };
        require(audio.previewChannelNote(first, 60, true), "stopped/live sampler preview entry");
        tick(2);
        audio.stopChannelPreview(first);
        phase = "initial-release";
        tick(8);
        const auto before = audio.projectionDiagnostics();
        const auto starts = device->starts, stops = device->stops;
        audio.midiInput.capture.begin();
        audio.typingInput.capture.begin();
        audio.midiInput.handleIncomingMidiMessage(nullptr,
                                                  juce::MidiMessage::noteOn(1, 48, juce::uint8(100)));
        audio.typingInput.route(juce::MidiMessage::noteOn(1, 48, juce::uint8(90)), true, true);
        phase = "independent-input-attack";
        tick(8);
        const auto physicalWrites = audio.midiInput.capture.write.load(),
                   typingWrites = audio.typingInput.capture.write.load();
        const auto physicalHistory = audio.midiInput.history.write.load(),
                   typingHistory = audio.typingInput.history.write.load();
        for (int i = 0; i < 48; ++i)
        {
            cycle = i;
            phase = "preview-attack";
            const auto head = audio.projectionDiagnostics().headSeconds;
            require(audio.previewChannelNote(first, 60 + i % 8, true, 40 + i % 80),
                    "preview note routed to its own Channel");
            require(audio.projectionDiagnostics().headSeconds == head,
                    "preview onset cannot seek the audio playhead");
            tick(2);
            checked(block.getMagnitude(0, 0, 256) > .0001f && block.getMagnitude(1, 0, 256) > .0001f,
                    "preview sounds on left while independent MIDI/typing remain on right");
            if (i % 2)
                audio.stopChannelPreview(first);
            else
                require(audio.previewChannelNote(first, 60 + i % 8, false), "preview mouse-up note release");
            phase = "preview-release";
            tick(6);
            if (block.getMagnitude(0, 0, 256) != 0 || block.getMagnitude(1, 0, 256) <= .0001f)
            {
                juce::String trace = "mode=" + juce::String(mode) + " cycle=" + juce::String(i) +
                                     " callback=" + juce::String(callbacks - 1) +
                                     " left=" + juce::String(block.getMagnitude(0, 0, 256), 9) +
                                     " right=" + juce::String(block.getMagnitude(1, 0, 256), 9) + "\n";
                phase = "failure-followup";
                for (int j = 0; j < 12; ++j)
                {
                    tick(1);
                    trace += juce::String(j) + ": left=" + juce::String(block.getMagnitude(0, 0, 256), 9) +
                             " right=" + juce::String(block.getMagnitude(1, 0, 256), 9) + "\n";
                }
                folder.getChildFile("sampler-preview-release-failure.txt").replaceWithText(trace);
                writeDiagnostics();
                require(false, "preview release retains independent sustained input");
            }
        }
        require(audio.previewChannelNote(second, 48, true), "same-pitch same-Channel preview");
        phase = "same-pitch-preview";
        cycle = -1;
        tick(3);
        audio.stopChannelPreview(second);
        phase = "same-pitch-release";
        tick(8);
        checked(block.getMagnitude(1, 0, 256) > .0001f,
                "preview panic cannot release physical/typing note at the same pitch");
        require(audio.midiInput.capture.active && audio.typingInput.capture.active &&
                    audio.midiInput.capture.write == physicalWrites &&
                    audio.typingInput.capture.write == typingWrites &&
                    audio.midiInput.history.write == physicalHistory &&
                    audio.typingInput.history.write == typingHistory,
                "preview cannot contaminate or stop MIDI takes/score history");
        require(!audio.previewChannelNote(-1, 60, true) && !audio.previewChannelNote(first, 128, true),
                "invalid preview target rejected");
        const auto after = audio.projectionDiagnostics();
        require(project.state().isEquivalentTo(original) && after.edit == before.edit &&
                    after.playbackContext == before.playbackContext &&
                    after.replacements == before.replacements &&
                    after.samplerCreations == before.samplerCreations &&
                    after.cache.preparations == before.cache.preparations && device->starts == starts &&
                    device->stops == stops,
                "preview retains model, graph, device and source cache");
        for (size_t i = 0; i < after.channels.size(); ++i)
            require(after.channels[i].sample == before.channels[i].sample &&
                        after.channels[i].graphInitialisations == before.channels[i].graphInitialisations,
                    "preview per-Channel PCM and graph lifecycle stable");
        writeDiagnostics();
        if (overruns || (mode != 0 && loops < 2))
            folder.getChildFile("sampler-preview-budget-failure.txt")
                .replaceWithText("mode=" + juce::String(mode) + " callbacks=" + juce::String(callbacks) +
                                 " loops=" + juce::String(loops) + " maximumMs=" + juce::String(maximum, 6) +
                                 " overruns=" + juce::String(overruns) + "\n");
        require(overruns == 0 && (mode == 0 || loops >= 2),
                "preview repeated gesture callback budget and live loops");
        report += juce::String(mode == 0   ? "Stopped"
                               : mode == 1 ? "Pattern"
                                           : "Song") +
                  ": callbacks=" + juce::String(callbacks) + " loops=" + juce::String(loops) +
                  " maxMs=" + juce::String(maximum, 4) + " overruns=" + juce::String(overruns) +
                  " MIDI/typing capture retained; separate preview ownership; graph/device/PCM retained\n";
        folder.getChildFile("sampler-preview-verification.txt")
            .replaceWithText(
                report + "Synthetic synchronous audio and injected MIDI; not physical input or listening.\n");
        audio.midiInput.capture.active = false;
        audio.typingInput.capture.active = false;
        audio.midiInput.panic();
        audio.typingInput.panic();
        audio.stop();
    }
    callbackOverloadRecoveryTests(folder);
}
