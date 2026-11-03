// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <thread>
static void loopBoundsTests(const juce::File &folder)
{
    auto owner = std::make_unique<AudioEngine>(false);
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
    require(device != nullptr, "Loop bounds synthetic device");
    Controller controller;
    controller.project.channels.resize(2);
    controller.project.patterns.front().length = 2;
    controller.project.patterns.front().autoLength = false; // Explicit initial loop bounds.
    const auto pattern = controller.project.patterns.front().id;
    require(awaitProjection(audio, controller.project, true, pattern).wasOk(), "Initial loop projection");
    audio.playStop();
    dispatchMessages(20);
    juce::AudioBuffer<float> block(2, 256);
    device->process(block);
    const auto before = audio.projectionDiagnostics();
    require(before.headLoopAvailable && before.headLoopEnabled && before.headLoopEndBeat == 2,
            "Initial requested loop reaches the active playhead");
    CommandArgs args;
    args.pattern = pattern;
    args.length = 4;
    require(controller.execute("pattern.length", args).wasOk(), "Canonical Pattern grow");
    require(audio.rebuild(controller.project, true, pattern).wasOk(), "Incremental Pattern grow");
    const auto after = audio.projectionDiagnostics();
    folder.getChildFile("loop-bounds-immediate.txt")
        .replaceWithText("requested=" + juce::String(after.loopEndBeat) +
                         " active=" + juce::String(after.headLoopEndBeat) +
                         " before=" + juce::String(before.headLoopEndBeat) + "\n");
    require(after.loopEndBeat == 4 && after.headLoopEndBeat == 4,
            "Pattern growth updates active loop before another timer or callback");
    audio.stop();
}

static void loopBoundsLiveTests(const juce::File &folder, const PluginSpec *instrument = nullptr)
{
    juce::String report;
    for (const bool patternMode : {true, false})
        for (const auto rate : {48000., 44100.})
        {
            auto owner = std::make_unique<AudioEngine>(false, juce::File{}, rate == 48000 ? 256 : 128);
            auto &audio = *owner;
            auto &manager = audio.engine.getDeviceManager();
            manager.deviceManager.addAudioDeviceType(std::make_unique<SyntheticDeviceType>());
            const int blockSize = rate == 48000 ? 256 : 128;
            juce::XmlElement setup("DEVICESETUP");
            setup.setAttribute("deviceType", "VibeRa synthetic");
            setup.setAttribute("audioOutputDeviceName", "In-memory output");
            setup.setAttribute("audioDeviceRate", rate);
            setup.setAttribute("audioDeviceBufferSize", blockSize);
            audio.engine.getPropertyStorage().setXmlProperty(te::SettingID::audio_device_setup, setup);
            manager.initialise(0, 2);
            dispatchMessages(20);
            auto *device = dynamic_cast<SyntheticDevice *>(manager.deviceManager.getCurrentAudioDevice());
            require(device != nullptr, "Loop-bound workflow virtual device");
            Controller c;
            c.project.channels.resize(2);
            c.project.tempo = 240;
            const auto pattern = c.project.patterns.front().id;
            const auto left = c.project.channels[0].id, right = c.project.channels[1].id;
            for (auto &channel : c.project.channels)
            {
                channel.source = "builtin:bass";
                channel.sampler.loop = true;
            }
            c.project.channels[0].pan = -1;
            c.project.channels[1].pan = 1;
            if (instrument)
            {
                c.project.channels[0].instrument = *instrument;
                c.project.channels[0].instrument->id = c.project.allocate();
            }
            c.project.patterns.front().length = 2;
            c.project.patterns.front().autoLength = false; // Numeric range/history subcase is Manual.
            c.project.patterns.front().notes = {{c.project.allocate(), left, 0, 2, 60, .8f, false}};
            const auto placement = c.project.allocate();
            c.project.placements.push_back({placement, pattern, 0, 0, 2, 0});
            require(awaitProjection(audio, c.project, patternMode, pattern).wasOk(),
                    "Loop workflow projection");
            audio.setMidiTarget(instrument ? left : right);
            audio.setLooping(true);
            audio.playStop();
            dispatchMessages(20);
            juce::AudioBuffer<float> pcm(2, blockSize);
            int callbacks = 0, overruns = 0, silentInput = 0, silentHostedInput = 0, naturalLoops = 0;
            double maximum = 0, previous = 0;
            bool monitorInput = false;
            const char *phase = "warmup";
            juce::String trace = "callback,phase,wallMs,headBeat,requestedEnd,appliedEnd,left,right\n";
            auto tick = [&](int count)
            {
                for (int i = 0; i < count; ++i)
                {
                    const auto began = juce::Time::getHighResolutionTicks();
                    device->process(pcm);
                    const auto ms = juce::Time::highResolutionTicksToSeconds(
                                        juce::Time::getHighResolutionTicks() - began) *
                                    1000;
                    maximum = std::max(maximum, ms);
                    overruns += ms > blockSize * 1000. / rate;
                    ++callbacks;
                    const auto state = audio.projectionDiagnostics();
                    naturalLoops += state.headSeconds + .02 < previous;
                    previous = state.headSeconds;
                    silentInput += monitorInput && pcm.getMagnitude(1, 0, blockSize) == 0;
                    silentHostedInput += monitorInput && instrument && pcm.getMagnitude(0, 0, blockSize) == 0;
                    trace += juce::String(callbacks) + "," + phase + "," + juce::String(ms, 6) + "," +
                             juce::String(state.headSeconds * 4, 9) + "," + juce::String(state.loopEndBeat) +
                             "," + juce::String(state.headLoopEndBeat) + "," +
                             juce::String(pcm.getMagnitude(0, 0, blockSize), 9) + "," +
                             juce::String(pcm.getMagnitude(1, 0, blockSize), 9) + "\n";
                    if (instrument)
                        dispatchMessages(1);
                }
            };
            tick(16);
            audio.midiInput.capture.begin();
            audio.midiInput.handleIncomingMidiMessage(
                nullptr, juce::MidiMessage::noteOn(1, instrument ? 72 : 48, juce::uint8(100)));
            if (instrument)
                require(audio.previewChannelNote(right, 48, true),
                        "Independent right sampler preview with actual hosted MIDI");
            tick(4);
            monitorInput = true;
            const auto original = audio.projectionDiagnostics();
            const auto workers = audio.pluginDiagnostics();
            const auto starts = device->starts, stops = device->stops;
            auto verify = [&](double expected, double previousHead)
            {
                const auto current = audio.projectionDiagnostics();
                require(current.headLoopAvailable && current.headLoopEnabled && current.loopEnabled,
                        "Range edit retains active looping");
                require(std::abs(current.loopEndBeat - expected) < 1e-8 &&
                            std::abs(current.headLoopEndBeat - expected) < 8. / rate,
                        "Current model/transport/active loop match without timer dispatch");
                const auto wrapped = previousHead >= expected / 4. ? 0. : previousHead;
                require(std::abs(current.headSeconds - wrapped) < 2. / rate,
                        "Grow/in-range shrink preserves phase; out-of-range shrink resumes at loop start");
                require(current.edit == original.edit &&
                            current.playbackContext == original.playbackContext &&
                            current.replacements == original.replacements && current.headPlaying &&
                            current.transportPlaying,
                        "Loop edit retains Edit/context/transport and graph");
                for (size_t i = 0; i < current.channels.size(); ++i)
                    require(current.channels[i].sampler == original.channels[i].sampler &&
                                current.channels[i].sample == original.channels[i].sample &&
                                current.channels[i].graphInitialisations ==
                                    original.channels[i].graphInitialisations,
                            "Loop edit retains voices/prepared samples/graph initialisation");
                require(current.channels[instrument ? 0 : 1].heldInputKeys == 1,
                        "Held MIDI ownership survives range/history publication");
                require(audio.midiInput.capture.active.load(),
                        "Loop publication keeps the input capture session");
            };
            for (int edit = 0; edit < 40; ++edit)
            {
                const auto baseline = c.project.state();
                const auto oldEnd = audio.projectionDiagnostics().loopEndBeat;
                CommandArgs args;
                args.pattern = pattern;
                const double target = std::array<double, 4>{4, 1, 8, 2}[size_t(edit % 4)];
                auto name = juce::String("pattern.length");
                if (patternMode)
                    args.length = target;
                else
                {
                    name = "clip.move";
                    args.ids = {placement};
                    args.length = target - oldEnd;
                }
                require(c.execute(name, args).wasOk(), "Canonical length/Playlist end change");
                auto head = audio.projectionDiagnostics().headSeconds;
                require(audio.rebuild(c.project, patternMode, pattern).wasOk(),
                        "Incremental loop range publication");
                verify(target, head);
                phase = "edit";
                tick(4);
                c.undoEdit();
                head = audio.projectionDiagnostics().headSeconds;
                require(c.project.state().isEquivalentTo(baseline) &&
                            audio.rebuild(c.project, patternMode, pattern).wasOk(),
                        "Exact range history Undo");
                verify(oldEnd, head);
                phase = "undo";
                tick(4);
                c.redoEdit();
                head = audio.projectionDiagnostics().headSeconds;
                require(audio.rebuild(c.project, patternMode, pattern).wasOk(), "Range Redo publication");
                verify(target, head);
                phase = "redo";
                tick(4);
            }
            if (patternMode)
            {
                const auto manualSource = c.project.state();
                const auto manualEnd = c.project.pattern(pattern)->length;
                CommandArgs extent;
                extent.pattern = pattern;
                extent.text = "auto";
                auto head = audio.projectionDiagnostics().headSeconds;
                require(c.execute("pattern.extent", extent).wasOk() &&
                            c.project.pattern(pattern)->length == 4 &&
                            audio.rebuild(c.project, true, pattern).wasOk(),
                        "Auto-growth prerequisite resolves the minimum four-quarter-note bar");
                verify(4, head);
                const auto source = c.project.state();
                const auto oldEnd = c.project.pattern(pattern)->length;
                CommandArgs added;
                added.pattern = pattern;
                added.channel = left;
                added.beat = 6;
                added.length = .25;
                added.pitch = 64;
                added.value = .7;
                require(c.execute("notes.add", added).wasOk() && c.project.pattern(pattern)->length == 8,
                        "Drawing a late note grows the current Pattern to the next bar");
                head = audio.projectionDiagnostics().headSeconds;
                require(audio.rebuild(c.project, true, pattern).wasOk(),
                        "Note-driven loop growth publication");
                verify(8, head);
                phase = "note-growth";
                tick(4);
                c.undoEdit();
                head = audio.projectionDiagnostics().headSeconds;
                require(c.project.state().isEquivalentTo(source) &&
                            audio.rebuild(c.project, true, pattern).wasOk(),
                        "Auto-growth Undo restores notes/IDs and original loop");
                verify(oldEnd, head);
                require(c.undoEditChecked().wasOk() && c.project.state().isEquivalentTo(manualSource),
                        "Undo explicit Auto prerequisite restores original Manual loop and exact history");
                head = audio.projectionDiagnostics().headSeconds;
                require(audio.rebuild(c.project, true, pattern).wasOk(),
                        "Restore fixed natural-loop scenario");
                verify(manualEnd, head);
            }
            phase = "natural-loops";
            naturalLoops = 0;
            previous = audio.projectionDiagnostics().headSeconds;
            tick(int(std::ceil(rate * 1.2 / blockSize)));
            require(naturalLoops >= 2 && silentInput == 0 && silentHostedInput == 0 && overruns == 0,
                    "Loop boundaries preserve held input and callback budget");
            const auto beforeToggle = audio.projectionDiagnostics();
            audio.setLooping(false);
            auto disabled = audio.projectionDiagnostics();
            require(!disabled.headLoopEnabled && !disabled.loopEnabled && disabled.headPlaying &&
                        std::abs(disabled.headSeconds - beforeToggle.headSeconds) < 2. / rate,
                    "Loop disable publishes immediately without resetting the playhead");
            audio.setLooping(true);
            require(audio.projectionDiagnostics().headLoopEnabled,
                    "Loop enable publishes without waiting for timer");
            require(device->starts == starts && device->stops == stops,
                    "No loop edit resets the audio device");
            if (instrument)
            {
                const auto after = audio.pluginDiagnostics();
                require(after.size() == workers.size() && !after.empty(), "Hosted loop retains workers");
                for (size_t i = 0; i < after.size(); ++i)
                    require(after[i].processId == workers[i].processId && after[i].alive &&
                                after[i].packets.missed == workers[i].packets.missed &&
                                after[i].packets.dropped == workers[i].packets.dropped &&
                                after[i].packets.midiOverflow == workers[i].packets.midiOverflow,
                            "Actual installed worker retained without missed/dropped/overflow packets across "
                            "loop/history edits");
            }
            const auto key = juce::String(patternMode ? "pattern" : "song") + "-" + juce::String(int(rate));
            folder.getChildFile("loop-bounds-" + key + "-callbacks.csv").replaceWithText(trace);
            report += key + " callbacks=" + juce::String(callbacks) + " maxMs=" + juce::String(maximum, 4) +
                      " overruns=" + juce::String(overruns) + " silentInput=" + juce::String(silentInput) +
                      " silentHostedInput=" + juce::String(silentHostedInput) +
                      " loops=" + juce::String(naturalLoops) +
                      " graphReplacements=0 deviceStarts=0 deviceStops=0\n";
            audio.midiInput.handleIncomingMidiMessage(nullptr,
                                                      juce::MidiMessage::noteOff(1, instrument ? 72 : 48));
            if (instrument)
                audio.stopChannelPreview(right);
            tick(12);
            require(audio.projectionDiagnostics().channels[instrument ? 0 : 1].heldInputKeys == 0,
                    "Released MIDI is not stuck after loop changes");
            audio.midiInput.stopCapture();
            audio.stop();
        }
    folder.getChildFile("loop-bounds-live.txt")
        .replaceWithText(
            report +
            "Virtual callbacks and actual installed provider only when supplied; no listening/hardware.\n");
}

static void loopBoundsConcurrentTests(const juce::File &folder)
{
    auto owner = std::make_unique<AudioEngine>(false);
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
    require(device != nullptr, "Concurrent loop virtual device");
    Controller c;
    c.project.channels.resize(1);
    c.project.channels.front().source = "builtin:bass";
    c.project.channels.front().sampler.loop = true;
    c.project.tempo = 240;
    const auto pattern = c.project.patterns.front().id;
    require(awaitProjection(audio, c.project, true, pattern).wasOk(), "Concurrent loop projection");
    audio.setMidiTarget(c.project.channels.front().id);
    audio.playStop();
    dispatchMessages(20);
    juce::AudioBuffer<float> block(2, 256);
    for (int i = 0; i < 16; ++i)
        device->process(block);
    audio.midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOn(1, 48, juce::uint8(100)));
    for (int i = 0; i < 4; ++i)
        device->process(block);
    const auto before = audio.projectionDiagnostics();
    const auto allocations = callbackAllocations.load(), destructions = callbackDestructions.load();
    std::atomic<int> callbacks{0};
    int overruns = 0, silent = 0;
    double maximum = 0;
    std::jthread thread(
        [&](std::stop_token stop)
        {
            while (!stop.stop_requested())
            {
                callbackAllocationProbe = callbacks.load() > 8;
                const auto began = juce::Time::getHighResolutionTicks();
                device->process(block);
                const auto ms =
                    juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - began) *
                    1000;
                callbackAllocationProbe = false;
                maximum = std::max(maximum, ms);
                overruns += ms > 256000. / 48000.;
                silent += block.getMagnitude(0, 0, 256) == 0;
                ++callbacks;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    while (callbacks.load() < 12)
        dispatchMessages(1);
    for (int i = 0; i < 80; ++i)
    {
        CommandArgs args;
        args.pattern = pattern;
        args.length = (i % 2) ? 1 : 8;
        require(c.execute("pattern.length", args).wasOk(), "Concurrent canonical extent edit");
        auto result = audio.rebuild(c.project, true, pattern);
        for (int retry = 0;
             result.failed() && result.getErrorMessage() == "Audio edit queue busy; retrying" && retry < 100;
             ++retry)
        {
            dispatchMessages(1);
            result = audio.rebuild(c.project, true, pattern);
        }
        require(result.wasOk(), "Concurrent loop publication completes outside callback");
        dispatchMessages(2);
    }
    thread.request_stop();
    thread.join();
    const auto after = audio.projectionDiagnostics();
    folder.getChildFile("loop-bounds-concurrent.txt")
        .replaceWithText("callbacks=" + juce::String(callbacks.load()) +
                         " maxMs=" + juce::String(maximum, 4) + " overruns=" + juce::String(overruns) +
                         " silentHeld=" + juce::String(silent) +
                         " allocations=" + juce::String(callbackAllocations.load() - allocations) +
                         " destructions=" + juce::String(callbackDestructions.load() - destructions) + "\n");
    require(callbacks.load() > 12 && overruns == 0 && silent == 0,
            "Concurrent callbacks retain held audio and deadline");
    require(callbackAllocations.load() == allocations && callbackDestructions.load() == destructions,
            "Concurrent range edits introduce no instrumented callback allocation/destruction");
    require(before.edit == after.edit && before.playbackContext == after.playbackContext &&
                before.replacements == after.replacements &&
                before.channels.front().graphInitialisations == after.channels.front().graphInitialisations &&
                after.headLoopEndBeat == 1,
            "Concurrent publication retains graph and latest applied range");
    audio.stop();
}
