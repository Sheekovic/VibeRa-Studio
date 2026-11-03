// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
static void samplerVoiceTests(const juce::File &folder)
{
    auto sample = makeOriginalSample("bass");
    sample.oneShot = true;
    for (int ch = 0; ch < 2; ++ch)
        for (int frame = 0; frame < sample.data.getNumSamples(); ++frame)
            sample.data.setSample(ch, frame, .2f);
    Controller model;
    const auto first = model.project.channels[0].id, second = model.project.channels[1].id;
    const auto original = model.project.state();
    CommandArgs args;
    args.channel = second;
    args.value = 1;
    require(model.execute("channel.sampler.cut", args).wasOk(), "Cut sender canonical command");
    args.channel = first;
    require(model.execute("channel.sampler.cutBy", args).wasOk(), "Cut receiver canonical command");
    auto grouped = model.project.state();
    model.undoEdit();
    model.undoEdit();
    require(model.project.state().isEquivalentTo(original), "Cut pair exact separate Undo");
    model.redoEdit();
    model.redoEdit();
    require(model.project.state().isEquivalentTo(grouped), "Cut pair exact Redo");
    require(model.execute("channel.sampler.cutItself", args).wasOk() &&
                model.project.channel(first)->sampler.cut == 2 &&
                model.project.channel(first)->sampler.cutBy == 2,
            "Cut itself assigns an independent pair without changing another Channel");
    const auto revision = model.revision;
    require(model.execute("channel.sampler.cutItself", args).wasOk() && model.revision == revision,
            "Unique Cut itself is idempotent");
    for (auto value : {-1., .5, 100., std::numeric_limits<double>::infinity()})
    {
        args.value = value;
        for (const auto *name : {"cut", "cutBy", "maxPoly"})
            require(model.execute("channel.sampler." + juce::String(name), args).failed() &&
                        model.revision == revision,
                    "Invalid voice value leaves model/history untouched");
    }
    const auto path = folder.getChildFile("sampler-voices.vibra");
    Project loaded;
    require(saveProject(model.project, path).wasOk() && loadProject(path, loaded).wasOk() &&
                loaded.state().isEquivalentTo(model.project.state()),
            "Voice settings exact schema14 round trip");
    auto legacy = Project::empty().state();
    legacy.setProperty("version", 13, nullptr);
    stripNoteMuteForLegacy(legacy);
    legacy.removeChild(legacy.getChildWithName("CLIP_GROUPS"), nullptr);
    for (auto channel : legacy.getChildWithName("CHANNELS"))
        for (const auto *name : {"cut", "cutBy", "maxPoly", "normalize"})
            channel.getChildWithName("SAMPLER").removeProperty(name, nullptr);
    require(Project::parse(legacy, loaded).wasOk() && loaded.channels[0].sampler.cut == 0 &&
                loaded.channels[0].sampler.maxPoly == 0,
            "Schema13 migrates silent-sound-compatible voice defaults");
    auto incomplete = model.project.state();
    incomplete.getChildWithName("CHANNELS")
        .getChild(0)
        .getChildWithName("SAMPLER")
        .removeProperty("cut", nullptr);
    const auto retained = loaded.state();
    require(Project::parse(incomplete, loaded).failed() && loaded.state().isEquivalentTo(retained),
            "Incomplete schema14 refuses atomically");
    auto masquerade = model.project.state();
    masquerade.setProperty("version", 13, nullptr);
    require(Project::parse(masquerade, loaded).failed() && loaded.state().isEquivalentTo(retained),
            "Audible voice fields cannot masquerade as an old format and silently disappear");

    VoiceBank score, midi, typing, preview;
    SamplerVoiceBudget budget(score, midi, typing, preview);
    const std::array<VoiceBank *, 4> banks{&score, &midi, &typing, &preview};
    for (auto *bank : banks)
        bank->prepare(sample, 48000);
    budget.configure(99);
    for (int i = 0; i < 120; ++i)
        banks[size_t(i % 4)]->noteOn(60, .5f, i + 1);
    auto count = [&]
    {
        int n = 0;
        for (auto *bank : banks)
            n += bank->activeVoices();
        return n;
    };
    require(count() == 99,
            "Max99 works across score/MIDI/typing/preview, including beyond the old64 bank ceiling");
    budget.configure(1);
    require(count() == 1 && preview.activeVoices() == 1,
            "Lowering maximum steals oldest Channel voices immediately");
    midi.noteOn(60, .5f, 123);
    require(count() == 1 && midi.activeVoices() == 1, "New live input shares the same Channel voice budget");
    for (auto *bank : banks)
        bank->reset();
    budget.configure(0);
    for (int i = 0; i < 160; ++i)
        score.noteOn(60, .5f, i + 1);
    require(count() == int(VoiceBank::capacity),
            "Auto has a real bounded128 voice ceiling, not an unlimited claim");

    auto p = Project::empty();
    p.tempo = 120;
    auto &pattern = p.patterns.front();
    const auto receiver = p.channels[0].id, sender = p.channels[1].id;
    p.channels[0].sampler.cutBy = 1;
    p.channels[0].sampler.envelope = true;
    p.channels[0].sampler.attack = p.channels[0].sampler.decay = 0;
    p.channels[0].sampler.release = .1;
    p.channels[1].sampler.cut = 1;
    pattern.notes = {{p.allocate(), receiver, 0, 3, 60, .8f, false},
                     {p.allocate(), sender, 1, .25, 60, .8f, true}};
    auto doc = compileLiveDocument(p, true, pattern.id, 1);
    require(doc && doc->cuts[1].size() == 1 && doc->cuts[1][0].step,
            "Real source step metadata reaches the compiled cut timeline");
    VoiceBank voice;
    voice.prepare(sample, 48000);
    voice.configure(doc->find(receiver)->sampler);
    juce::AudioBuffer<float> pcm(2, 48000);
    pcm.clear();
    LiveSequencer sequence;
    sequence.process(*doc, receiver, 0, 48000, 48000, true, voice, pcm, 0);
    require(pcm.getMagnitude(0, 21000, 1000) > .01f && pcm.getMagnitude(0, 25000, 1000) > .01f &&
                pcm.getMagnitude(0, 30000, 1000) == 0,
            "Cross cut retains the configured100ms release after exact half-second cue");
    voice.reset();
    sequence.reset();
    pcm.clear();
    sequence.process(*doc, receiver, .75, 256, 48000, true, voice, pcm, 0);
    require(pcm.getMagnitude(0, 0, 256) == 0, "Seek cannot chase a score note already cut before the cursor");
    auto changed = compileLiveDocument(p, true, pattern.id, 2);
    pcm.clear();
    sequence.process(*changed, receiver, .75 + 256. / 48000., 256, 48000, true, voice, pcm, 0);
    require(pcm.getMagnitude(0, 0, 256) == 0, "Unrelated live publication does not resurrect a choked note");
    p.patterns.front().notes.front().beat = .75;
    p.placements = {{p.allocate(), pattern.id, 0, 0, 4, 0}, {p.allocate(), pattern.id, 1, 8, 2, .5}};
    auto song = compileLiveDocument(p, false, pattern.id, 3);
    voice.reset();
    sequence.reset();
    pcm.clear();
    sequence.process(*song, receiver, 4, 48000, 48000, true, voice, pcm, 0);
    require(pcm.getMagnitude(0, 7000, 1000) > .01f && pcm.getMagnitude(0, 13000, 1000) > .01f &&
                pcm.getMagnitude(0, 20000, 1000) == 0,
            "Linked Song placement/offset cuts occur at actual source-window cue positions");
    p.channels[1].muted = true;
    auto muted = compileLiveDocument(p, false, pattern.id, 4);
    voice.reset();
    sequence.reset();
    pcm.clear();
    sequence.process(*muted, receiver, 4, 48000, 48000, true, voice, pcm, 0);
    require(muted->cuts[1].empty() && pcm.getMagnitude(0, 30000, 1000) > .01f,
            "Muted trigger Channels do not cut another instrument");
    p.channels[1].muted = false;
    p.patterns.front().notes.front().beat = 1;
    auto simultaneous = compileLiveDocument(p, true, pattern.id, 5);
    voice.reset();
    sequence.reset();
    pcm.clear();
    sequence.process(*simultaneous, receiver, 0, 48000, 48000, true, voice, pcm, 0);
    require(pcm.getMagnitude(0, 30000, 1000) > .01f,
            "Independent simultaneous policy applies old-voice cuts before new local note-ons");

    // A cut must stay effective after gate overflow and a later document publish.
    auto dense = Project::empty();
    dense.tempo = 120;
    dense.channels[0].sampler = p.channels[0].sampler;
    dense.channels[0].sampler.release = 0;
    auto &densePattern = dense.patterns.front();
    densePattern.notes.clear();
    for (int n = 0; n < 128; ++n)
        densePattern.notes.push_back(
            {dense.allocate(), dense.channels[0].id, 0, n == 1 ? .6 : 4., 60, .8f, false});
    densePattern.notes.push_back({dense.allocate(), dense.channels[0].id, .4, 3., 60, .8f, false});
    auto denseDoc = compileLiveDocument(dense, true, densePattern.id, 10);
    voice.reset();
    voice.configure(denseDoc->find(dense.channels[0].id)->sampler);
    sequence.reset();
    sequence.process(*denseDoc, dense.channels[0].id, 0, 256, 48000, true, voice, pcm, 0);
    voice.releaseConfigured();
    sequence.process(*denseDoc, dense.channels[0].id, 256. / 48000, 8192, 48000, true, voice, pcm, 0, {},
                     true);
    sequence.process(*denseDoc, dense.channels[0].id, 8448. / 48000, 8192, 48000, true, voice, pcm, 0);
    require(voice.activeVoices() == 1, "Only the later score note survives a live cut after gate overflow");
    auto densePublish = compileLiveDocument(dense, true, densePattern.id, 11);
    sequence.process(*densePublish, dense.channels[0].id, 16640. / 48000, 256, 48000, true, voice, pcm, 0);
    require(voice.activeVoices() == 1, "Publishing after gate overflow cannot resurrect a live-cut note");

    auto clock = std::make_shared<SamplerCutClock>(0);
    auto a = std::make_shared<SamplerCutSignal>(), b = std::make_shared<SamplerCutSignal>();
    a->clock = b->clock = clock;
    std::vector<SamplerCutInput> routes{{1, a, 0, 0}, {2, b, 0, 0}};
    SamplerCutMonitor monitor;
    require(monitor.poll(1, routes) == 0, "New input-cut routes have no phantom trigger");
    const auto firstOn = a->trigger();
    require(monitor.poll(1, routes) == firstOn && monitor.poll(1, routes) == 0,
            "Each live producer cue is consumed once without clearing another receiver's observation");
    const auto secondOn = b->trigger();
    std::vector<SamplerCutInput> removed{{2, b, secondOn, 0}};
    require(monitor.poll(1, removed) == secondOn,
            "Removing an earlier Channel must not lose an unconsumed live cut");
    const auto thirdOn = b->trigger();
    midi.reset();
    midi.noteOn(60, .8f, 500, 0, 0, thirdOn + 1);
    midi.releaseConfigured(thirdOn);
    require(midi.activeVoices() == 1, "An older same-quantum cut does not kill a later live note");
    const auto allocated = callbackAllocations.load(), destroyed = callbackDestructions.load();
    pcm.clear();
    callbackAllocationProbe = true;
    for (int i = 0; i < 100; ++i)
    {
        score.noteOn(60, .7f, i + 1);
        score.releaseConfigured();
        score.render(pcm, 0, 256);
        monitor.poll(1, removed);
    }
    callbackAllocationProbe = false;
    require(callbackAllocations.load() == allocated && callbackDestructions.load() == destroyed,
            "New voice-budget/cut/ordinal paths perform no instrumented allocation or destruction");
    folder.getChildFile("sampler-voice-core.txt")
        .replaceWithText(
            "PASS canonical groups/history/schema14, shared99/128 budget, configured release, cut-aware "
            "chase and live source ordinals. Virtual/component evidence only; no listening/hardware.\n");
}

static void samplerVoicesLiveTests(const juce::File &folder)
{
    juce::String report;
    for (bool patternMode : {true, false})
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
        require(device != nullptr, "Sampler voice virtual device");
        Controller c;
        c.project.channels.resize(2);
        c.project.tempo = 240;
        const auto pattern = c.project.patterns.front().id;
        const auto first = c.project.channels[0].id, second = c.project.channels[1].id;
        for (auto &channel : c.project.channels)
        {
            channel.source = "builtin:bass";
            channel.sampler.loop = true;
        }
        c.project.channels[0].pan = -1;
        c.project.channels[0].sampler.cutBy = 1;
        c.project.channels[0].sampler.envelope = true;
        c.project.channels[0].sampler.release = .05;
        c.project.channels[1].pan = 1;
        c.project.channels[1].sampler.cut = 1;
        c.project.placements.push_back({c.project.allocate(), pattern, 0, 0, 4, 0});
        require(awaitProjection(audio, c.project, patternMode, pattern).wasOk(), "Voice initial projection");
        audio.setMidiTarget(second);
        audio.setLooping(true);
        audio.playStop();
        dispatchMessages(20);
        juce::AudioBuffer<float> block(2, 256);
        int callbacks = 0, overruns = 0, silentOther = 0, loops = 0;
        bool monitorOther = false;
        double maximum = 0, previous = 0;
        const char *phase = "warmup";
        juce::String trace = "callback,phase,wallMs,left,right,headSeconds\n";
        auto tick = [&](int n)
        {
            for (int i = 0; i < n; ++i)
            {
                const auto began = juce::Time::getHighResolutionTicks();
                device->process(block);
                const auto ms =
                    juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - began) *
                    1000;
                maximum = std::max(maximum, ms);
                overruns += ms > 256000. / 48000.;
                ++callbacks;
                if (monitorOther)
                    silentOther += block.getMagnitude(1, 0, 256) == 0;
                const auto head = audio.projectionDiagnostics().headSeconds;
                loops += head + .025 < previous;
                previous = head;
                trace += juce::String(callbacks) + "," + phase + "," + juce::String(ms, 6) + "," +
                         juce::String(block.getMagnitude(0, 0, 256), 9) + "," +
                         juce::String(block.getMagnitude(1, 0, 256), 9) + "," + juce::String(head, 9) + "\n";
                dispatchMessages(1);
            }
        };
        auto on = [&]
        {
            audio.midiInput.handleIncomingMidiMessage(nullptr,
                                                      juce::MidiMessage::noteOn(1, 60, juce::uint8(100)));
        };
        auto preview = [&](int pitch, bool enabled)
        { require(audio.previewChannelNote(first, pitch, enabled), "Live sampler preview lane"); };
        tick(16);
        on();
        tick(4);
        monitorOther = true;
        preview(60, true);
        tick(4);
        require(block.getMagnitude(0, 0, 256) > .001f && block.getMagnitude(1, 0, 256) > .001f,
                "Both live Channels sound before a cut");
        const auto before = audio.projectionDiagnostics();
        const auto starts = device->starts, stops = device->stops;
        CommandArgs args;
        args.channel = first;
        args.value = .5;
        require(c.execute("channel.sampler.release", args).wasOk() &&
                    awaitProjection(audio, c.project, patternMode, pattern).wasOk(),
                "Live release edit publication");
        phase = "captured-release";
        on();
        tick(1);
        require(block.getMagnitude(0, 0, 256) > .001f,
                "Cross live cut preserves captured release instead of muting");
        tick(12);
        require(block.getMagnitude(0, 0, 256) == 0, "Old50ms release survives a later500ms setting change");
        preview(60, false);
        args.value = .012;
        require(c.execute("channel.sampler.release", args).wasOk() &&
                    awaitProjection(audio, c.project, patternMode, pattern).wasOk(),
                "Prepare short release for ordinal cases");
        phase = "later-note-survives";
        on();
        preview(62, true);
        tick(12);
        require(block.getMagnitude(0, 0, 256) > .001f,
                "Same-quantum later live note survives an earlier cross cut");
        audio.stopChannelPreview(first);
        tick(5);
        phase = "later-cut-wins";
        preview(63, true);
        on();
        tick(5);
        require(block.getMagnitude(0, 0, 256) == 0,
                "Same-quantum later cross cut releases the earlier live note");
        audio.stopChannelPreview(first);
        tick(5);
        for (int i = 0; i < 16; ++i)
        {
            args.pattern = pattern;
            args.channel = first;
            args.beat = i * .25;
            args.enabled = true;
            args.value = .8;
            require(c.execute("steps.set", args).wasOk(), "Prepare real step self-cut score");
        }
        require(awaitProjection(audio, c.project, patternMode, pattern).wasOk(),
                "Score publication retains live MIDI");
        tick(2);
        audio.midiInput.capture.begin();
        const auto captured = audio.midiInput.capture.write.load();
        phase = "rapid-edits-history";
        for (int edit = 0; edit < 40; ++edit)
        {
            const auto initial = c.project.state();
            const auto head = audio.positionBeat();
            const auto &settings = c.project.channel(first)->sampler;
            args.channel = first;
            args.value = edit % 4 == 0           ? (settings.cut + 1) % 3
                         : edit % 4 == 1         ? (settings.cutBy + 1) % 3
                         : settings.maxPoly == 1 ? 2
                                                 : 1;
            args.enabled = !(settings.cut > 0 && settings.cut == settings.cutBy);
            const auto name =
                juce::StringArray{"channel.sampler.cut", "channel.sampler.cutBy", "channel.sampler.maxPoly",
                                  "channel.sampler.cutItself"}[edit % 4];
            require(c.execute(name, args).wasOk() &&
                        awaitProjection(audio, c.project, patternMode, pattern).wasOk(),
                    "Rapid canonical voice edit");
            require(audio.playing() && std::abs(audio.positionBeat() - head) < .01,
                    "Voice edit retains transport position");
            if (edit % 5 == 0)
                audio.midiInput.handleIncomingMidiMessage(nullptr,
                                                          juce::MidiMessage::noteOn(1, 64, juce::uint8(90)));
            tick(4);
            if (edit % 5 == 0)
                audio.midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 64));
            c.undoEdit();
            require(c.project.state().isEquivalentTo(initial) &&
                        awaitProjection(audio, c.project, patternMode, pattern).wasOk(),
                    "Voice control exact Undo with live capture");
            tick(2);
            c.redoEdit();
            require(awaitProjection(audio, c.project, patternMode, pattern).wasOk(),
                    "Voice control Redo publication");
            tick(2);
            c.undoEdit();
            require(awaitProjection(audio, c.project, patternMode, pattern).wasOk(),
                    "Voice control baseline publication");
            tick(2);
        }
        const auto after = audio.projectionDiagnostics();
        require(audio.midiInput.capture.active && !audio.midiInput.capture.failed &&
                    audio.midiInput.capture.write > captured,
                "Real injected MIDI capture continues receiving notes through voice edits/history");
        require(before.edit == after.edit && before.playbackContext == after.playbackContext &&
                    before.replacements == after.replacements &&
                    before.samplerCreations == after.samplerCreations &&
                    before.cache.preparations == after.cache.preparations && device->starts == starts &&
                    device->stops == stops,
                "Voice controls retain graph/device/prepared sample lifetimes");
        for (const auto &old : before.channels)
        {
            const auto now = std::find_if(after.channels.begin(), after.channels.end(),
                                          [&](const auto &v) { return v.channel == old.channel; });
            require(now != after.channels.end() && now->track == old.track && now->sampler == old.sampler &&
                        now->sample == old.sample && now->graphInitialisations == old.graphInitialisations,
                    "Every Channel runtime identity remains unchanged");
        }
        const auto mode = juce::String(patternMode ? "pattern" : "song");
        folder.getChildFile("sampler-voices-" + mode + "-callbacks.csv").replaceWithText(trace);
        report += mode + " callbacks=" + juce::String(callbacks) + " maxMs=" + juce::String(maximum, 4) +
                  " overruns=" + juce::String(overruns) + " silentOther=" + juce::String(silentOther) +
                  " loops=" + juce::String(loops) + " graphReplacements=0 deviceStarts=0 deviceStops=0\n";
        folder.getChildFile("sampler-voices-live.txt").replaceWithText(report);
        require(overruns == 0 && silentOther == 0 && loops >= 2,
                "Voice live/score edits/history remain within deadline with unaffected live PCM");
        audio.midiInput.capture.active = false;
        audio.midiInput.panic();
        audio.stop();
    }
}
