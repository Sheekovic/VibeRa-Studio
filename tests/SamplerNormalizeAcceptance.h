// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
static void samplerNormalizeTests(const juce::File &folder)
{
    Controller model;
    const auto id = model.project.channels.front().id;
    const auto original = model.project.state();
    CommandArgs args;
    args.channel = id;
    args.enabled = true;
    require(model.execute("channel.sampler.normalize", args).wasOk(), "Canonical Normalize");
    const auto enabled = model.project.state();
    const auto revision = model.revision;
    require(model.project.channel(id)->sampler.normalize && !model.project.channels[1].sampler.normalize,
            "Normalize targets one Channel");
    require(model.execute("channel.sampler.normalize", args).wasOk() && model.revision == revision,
            "Normalize setter is idempotent");
    model.undoEdit();
    require(model.project.state().isEquivalentTo(original), "Normalize one exact Undo");
    model.redoEdit();
    require(model.project.state().isEquivalentTo(enabled), "Normalize exact Redo");
    Project loaded;
    const auto path = folder.getChildFile("sampler-normalize.vibra");
    require(saveProject(model.project, path).wasOk() && loadProject(path, loaded).wasOk() &&
                loaded.state().isEquivalentTo(enabled),
            "Normalize real schema15 round trip");
    auto old = Project::empty().state();
    old.setProperty("version", 14, nullptr);
    stripNoteMuteForLegacy(old);
    old.removeChild(old.getChildWithName("CLIP_GROUPS"), nullptr);
    for (auto channel : old.getChildWithName("CHANNELS"))
        channel.getChildWithName("SAMPLER").removeProperty("normalize", nullptr);
    require(Project::parse(old, loaded).wasOk() && !loaded.channels.front().sampler.normalize,
            "Real schema14 retains original sound with Normalize disabled");
    const auto preserved = loaded.state();
    for (const auto bad : {juce::var(), juce::var(2), juce::var("true"), juce::var(.5)})
    {
        auto tree = enabled.createCopy();
        auto sampler = tree.getChildWithName("CHANNELS").getChild(0).getChildWithName("SAMPLER");
        if (bad.isVoid())
            sampler.removeProperty("normalize", nullptr);
        else
            sampler.setProperty("normalize", bad, nullptr);
        require(Project::parse(tree, loaded).failed() && loaded.state().isEquivalentTo(preserved),
                "Missing/malformed Normalize fails without changing destination");
    }
    auto masquerade = enabled.createCopy();
    masquerade.setProperty("version", 14, nullptr);
    require(Project::parse(masquerade, loaded).failed(), "Normalize cannot masquerade as an old writer");
    Controller unsupported;
    unsupported.project = model.project;
    unsupported.project.channels.front().audioClip = true;
    require(unsupported.execute("channel.sampler.normalize", args).failed(),
            "Audio Clip Normalize is refused");

    Customization customization;
    Controller macroModel;
    MacroContext context{
        macroModel.project.patterns.front().id, macroModel.project.channels.front().id, 0, {}, {}};
    CommandArgs enable, level;
    enable.enabled = true;
    level.value = .5;
    Macro macro{"macro.normalize-level",
                "Normalize and set Channel level",
                {{"channel.sampler.normalize", enable, true}, {"channel.gain", level, true}}};
    const auto beforeMacro = macroModel.project.state();
    require(customization.addMacro(macro).wasOk() &&
                customization.runMacro(macro.id, macroModel, context).wasOk(),
            "Normalize participates in a real multi-command user macro");
    require(macroModel.project.channel(context.channel)->sampler.normalize &&
                macroModel.project.channel(context.channel)->gain == .5f,
            "Macro preserves canonical command order and Channel context");
    macroModel.undoEdit();
    require(macroModel.project.state().isEquivalentTo(beforeMacro), "Normalize macro has one exact Undo");

    macroModel.redoEdit();
    Macro disable{"macro.normalize-disable", "Disable Normalize", {}};
    CommandArgs offArgs;
    offArgs.enabled = false;
    disable.steps.push_back({"channel.sampler.normalize", offArgs, true});
    require(customization.addMacro(disable).wasOk() &&
                customization.runMacro(disable.id, macroModel, context).wasOk() &&
                !macroModel.project.channel(context.channel)->sampler.normalize,
            "Explicit off macro uses the same boolean setting and preserves other fields");
    macroModel.undoEdit();
    require(macroModel.project.channel(context.channel)->sampler.normalize &&
                macroModel.project.channel(context.channel)->gain == .5f,
            "Disable macro Undo retains earlier level macro");

    Sample sample;
    sample.data.setSize(2, 8192);
    sample.oneShot = false;
    auto fill = [&](float left, float right)
    {
        for (int i = 0; i < 8192; ++i)
        {
            sample.data.setSample(0, i, left);
            sample.data.setSample(1, i, right);
        }
        sample.prepareNormalization();
    };
    auto render = [&](bool normalized)
    {
        VoiceBank bank;
        bank.prepare(sample, 48000);
        SamplerSettings s;
        s.normalize = normalized;
        s.envelope = true;
        s.attack = 0;
        s.decay = 0;
        s.sustain = 1;
        bank.configure(PreparedSamplerSettings(s));
        bank.noteOn(60, 1);
        juce::AudioBuffer<float> output(2, 2048);
        output.clear();
        bank.render(output, 0, 2048);
        return output;
    };
    fill(.25f, -.0625f);
    auto off = render(false), on = render(true);
    require(sample.sourcePeak == .25 && sample.normalizationGain == 4, "Prepared stereo peak reciprocal");
    require(std::abs(off.getSample(0, 100) - .25f) < 1e-6f && std::abs(on.getSample(0, 100) - 1) < 1e-6f &&
                std::abs(on.getSample(1, 100) + .25f) < 1e-6f,
            "Normalize reaches unity and preserves stereo balance/polarity");
    require(sample.data.getSample(0, 100) == .25f, "Normalization does not modify immutable PCM");
    // Peak belongs to the entire loaded sample, independently of source trim.
    sample.data.setSample(0, 7000, .5f);
    sample.prepareNormalization();
    VoiceBank held;
    held.prepare(sample, 48000);
    SamplerSettings settings;
    settings.envelope = true;
    settings.attack = settings.decay = 0;
    settings.sustain = 1;
    settings.end = .5;
    held.configure(PreparedSamplerSettings(settings));
    held.noteOn(60, 1);
    juce::AudioBuffer<float> block(2, 1024);
    block.clear();
    held.render(block, 0, 64);
    settings.normalize = true;
    held.configure(PreparedSamplerSettings(settings));
    block.clear();
    held.render(block, 0, 1024);
    require(held.activeVoices() == 1 && block.getSample(0, 0) > .25f && block.getSample(0, 0) < .26f &&
                block.getSample(0, 1023) > .49f && block.getSample(0, 1023) < .501f,
            "Held voice smoothly approaches whole-source normalization without retriggering");
    settings.normalize = false;
    held.configure(PreparedSamplerSettings(settings));
    block.clear();
    held.render(block, 0, 1024);
    require(block.getSample(0, 1023) > .249f && block.getSample(0, 1023) < .26f,
            "Live disable returns to original gain");
    for (const auto amplitude : {0.f, 1e-30f, 4.f, std::numeric_limits<float>::max() * .75f})
    {
        fill(amplitude, -amplitude * .25f);
        const auto audio = render(true);
        require(std::isfinite(audio.getSample(0, 100)) &&
                    std::abs(audio.getSample(0, 100) - (amplitude == 0 ? 0 : 1)) < 1e-6f,
                "Silent/quiet/over-unity finite sources normalize without NaN or overflow");
    }
    folder.getChildFile("normalize-core.txt")
        .replaceWithText("PASS canonical/history/schema15, actual stereo PCM, whole-source trim policy, held "
                         "smoothing, silence/extremes; not listening.\n");
}

static void samplerNormalizeLiveTests(const juce::File &folder, const PluginSpec *instrument = nullptr)
{
    juce::String report;
    for (const bool patternMode : {true, false})
    {
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
        require(device != nullptr, "Normalize virtual audio device");
        Controller c;
        c.project.channels.resize(2);
        c.project.tempo = 240;
        const auto pattern = c.project.patterns.front().id, first = c.project.channels[0].id,
                   second = c.project.channels[1].id;
        for (auto &ch : c.project.channels)
        {
            ch.source = "builtin:bass";
            ch.sampler.loop = true;
        }
        c.project.channels[0].pan = -1;
        c.project.channels[1].pan = 1;
        if (instrument)
        {
            c.project.channels[1].instrument = *instrument;
            c.project.channels[1].instrument->id = c.project.allocate();
        }
        c.project.patterns.front().length = 2;
        c.project.patterns.front().autoLength = false; // Normalize edits preserve exact two-beat loop.
        c.project.placements.push_back({c.project.allocate(), pattern, 0, 0, 2, 0});
        require(awaitProjection(audio, c.project, patternMode, pattern).wasOk(), "Normalize live projection");
        audio.setMidiTarget(instrument ? second : first);
        audio.setLooping(true);
        audio.playStop();
        dispatchMessages(20);
        juce::AudioBuffer<float> block(2, 256);
        int callbacks = 0, overruns = 0, silentLeft = 0, silentRight = 0, loops = 0;
        double maximum = 0, previous = 0;
        bool monitoring = false;
        struct Observation
        {
            SyntheticCallbackSpan span;
            double threadCpuMs = -1;
            juce::uint64 threadCycles = 0;
        };
        std::array<Observation, 512> observations{};
        juce::String trace = "callback,wallMs,left,right,headSeconds\n";
        auto tick = [&](int count)
        {
            for (int i = 0; i < count; ++i)
            {
#if JUCE_WINDOWS
                FILETIME created{}, exited{}, kernelBefore{}, userBefore{}, kernelAfter{}, userAfter{};
                ULONG64 cyclesBefore = 0, cyclesAfter = 0;
                const bool cpuClock =
                    GetThreadTimes(GetCurrentThread(), &created, &exited, &kernelBefore, &userBefore) != 0;
                const bool cycleClock = QueryThreadCycleTime(GetCurrentThread(), &cyclesBefore) != 0;
#endif
                callbackAllocationProbe = monitoring;
                const auto began = juce::Time::getHighResolutionTicks();
                device->process(block);
                const auto ended = juce::Time::getHighResolutionTicks();
                const auto ms = juce::Time::highResolutionTicksToSeconds(ended - began) * 1000;
                callbackAllocationProbe = false;
                double threadCpuMs = -1;
                juce::uint64 threadCycles = 0;
#if JUCE_WINDOWS
                if (cycleClock && QueryThreadCycleTime(GetCurrentThread(), &cyclesAfter) &&
                    cyclesAfter >= cyclesBefore)
                    threadCycles = cyclesAfter - cyclesBefore;
                if (cpuClock &&
                    GetThreadTimes(GetCurrentThread(), &created, &exited, &kernelAfter, &userAfter))
                {
                    const auto ticks = [](FILETIME value)
                    { return (juce::uint64(value.dwHighDateTime) << 32) | value.dwLowDateTime; };
                    threadCpuMs = double(ticks(kernelAfter) + ticks(userAfter) - ticks(kernelBefore) -
                                         ticks(userBefore)) /
                                  10000.;
                }
#endif
                require(callbacks < int(observations.size()), "Normalize observation capacity");
                require(device->lastTiming().containedBy(began, ended), "Normalize timing spans nested");
                observations[size_t(callbacks)] = {
                    {device->lastTiming(), began, ended}, threadCpuMs, threadCycles};
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
        audio.midiInput.handleIncomingMidiMessage(
            nullptr, juce::MidiMessage::noteOn(1, instrument ? 72 : 48, juce::uint8(100)));
        require(audio.previewChannelNote(instrument ? first : second, instrument ? 48 : 60, true),
                "Independent held preview beside normalized sampler");
        tick(8);
        monitoring = true;
        const auto before = audio.projectionDiagnostics();
        const auto workers = audio.pluginDiagnostics();
        const auto starts = device->starts, stops = device->stops;
        const auto allocations = callbackAllocations.load(), destructions = callbackDestructions.load();
        CommandArgs args;
        args.channel = first;
        args.enabled = true;
        if (instrument)
        {
            auto unsupported = args;
            unsupported.channel = second;
            const auto rev = c.revision;
            require(c.execute("channel.sampler.normalize", unsupported).failed() && c.revision == rev,
                    "Actual hosted instrument refuses sampler Normalize atomically");
        }
        for (int edit = 0; edit < 40; ++edit)
        {
            const auto baseline = c.project.state();
            const auto head = audio.projectionDiagnostics().headSeconds;
            require(c.execute("channel.sampler.normalize", args).wasOk() &&
                        awaitProjection(audio, c.project, patternMode, pattern).wasOk(),
                    "Live Normalize edit publication");
            require(std::abs(audio.projectionDiagnostics().headSeconds - head) < 1. / 48000.,
                    "Normalize preserves actual playhead before another callback");
            tick(4);
            c.undoEdit();
            require(c.project.state().isEquivalentTo(baseline) &&
                        awaitProjection(audio, c.project, patternMode, pattern).wasOk(),
                    "Live Normalize exact Undo");
            tick(2);
            c.redoEdit();
            require(awaitProjection(audio, c.project, patternMode, pattern).wasOk(), "Live Normalize Redo");
            tick(2);
            c.undoEdit();
            require(awaitProjection(audio, c.project, patternMode, pattern).wasOk(),
                    "Live baseline publication");
            tick(2);
            require(audio.midiInput.capture.active &&
                        audio.projectionDiagnostics().channels[instrument ? 1 : 0].heldInputKeys == 1,
                    "Held key and real capture remain owned through edits/history");
        }
        const auto after = audio.projectionDiagnostics();
        require(before.edit == after.edit && before.playbackContext == after.playbackContext &&
                    before.replacements == after.replacements &&
                    before.samplerCreations == after.samplerCreations &&
                    before.cache.preparations == after.cache.preparations && device->starts == starts &&
                    device->stops == stops,
                "Normalize retains graph/context/device/prepared cache");
        for (size_t i = 0; i < before.channels.size(); ++i)
            require(before.channels[i].sample == after.channels[i].sample &&
                        before.channels[i].sampler == after.channels[i].sampler &&
                        before.channels[i].graphInitialisations == after.channels[i].graphInitialisations,
                    "Normalize retains every prepared Channel and voice-bank identity");
        if (instrument)
        {
            const auto current = audio.pluginDiagnostics();
            require(current.size() == workers.size() && !current.empty(), "Hosted worker retained");
            for (size_t i = 0; i < current.size(); ++i)
                require(current[i].alive && current[i].processId == workers[i].processId &&
                            current[i].packets.missed == workers[i].packets.missed &&
                            current[i].packets.dropped == workers[i].packets.dropped &&
                            current[i].packets.midiOverflow == workers[i].packets.midiOverflow,
                        "Normalize does not restart or miss/drop the unrelated real plugin worker");
        }
        const auto mode = juce::String(patternMode ? "pattern" : "song");
        folder.getChildFile("normalize-" + mode + "-callbacks.csv").replaceWithText(trace);
        juce::String phases = "callback,outerMs,threadCpuMs,threadCycles," +
                              juce::String(SyntheticCallbackTiming::csvHeader) + "\n";
        for (int i = 0; i < callbacks; ++i)
        {
            const auto &observation = observations[size_t(i)];
            const auto &span = observation.span;
            phases += juce::String(i + 1) + "," +
                      juce::String(SyntheticCallbackTiming::milliseconds(span.began, span.ended), 6) + "," +
                      juce::String(observation.threadCpuMs, 6) + "," +
                      juce::String(observation.threadCycles) + "," + span.timing.csv(span.began, span.ended) +
                      "\n";
        }
        require(folder.getChildFile("normalize-" + mode + "-phases.csv").replaceWithText(phases),
                "Normalize observations retained after processing");
        report += mode + " callbacks=" + juce::String(callbacks) + " maxMs=" + juce::String(maximum, 4) +
                  " overruns=" + juce::String(overruns) + " silentLeft=" + juce::String(silentLeft) +
                  " silentRight=" + juce::String(silentRight) + " loops=" + juce::String(loops) +
                  " allocations=" + juce::String(callbackAllocations.load() - allocations) +
                  " destructions=" + juce::String(callbackDestructions.load() - destructions) +
                  " graphReplacements=0 deviceStarts=0 deviceStops=0\n";
        folder.getChildFile("normalize-live.txt").replaceWithText(report);
        require(
            overruns == 0 && silentLeft == 0 && silentRight == 0 && loops >= 2,
            "Actual Pattern/Song held PCM survives repeated Normalize edits/loops within callback deadline");
        require(callbackAllocations.load() == allocations && callbackDestructions.load() == destructions,
                "Normalize processing introduces no instrumented callback allocation/destruction");
        monitoring = false;
        audio.midiInput.handleIncomingMidiMessage(nullptr,
                                                  juce::MidiMessage::noteOff(1, instrument ? 72 : 48));
        audio.stopChannelPreview(instrument ? first : second);
        tick(4);
        require(audio.projectionDiagnostics().channels[instrument ? 1 : 0].heldInputKeys == 0,
                "Normalized held note releases without stuck ownership");
        audio.midiInput.capture.active = false;
        audio.stop();
    }
}
