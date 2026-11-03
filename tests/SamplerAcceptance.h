// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "LegacySamplerVoices.h"
#include "../src/SamplerTempo.h"
#include "../src/SamplerWaveform.h"

static void samplerTests(const juce::File &folder)
{
    Sample ramp;
    ramp.data.setSize(2, 17);
    for (int i = 0; i < 17; ++i)
        for (int c = 0; c < 2; ++c)
            ramp.data.setSample(c, i, float(i));
    auto loopRender = [&](bool reverse, bool pingPong, double cents)
    {
        VoiceBank bank;
        bank.prepare(ramp, 48000);
        SamplerSettings s;
        s.loop = true;
        s.pingPong = pingPong;
        s.loopStart = .25;
        s.loopEnd = .75;
        s.reverse = reverse;
        s.cents = cents;
        s.envelope = true;
        s.attack = s.decay = s.release = 0;
        bank.configure(PreparedSamplerSettings(s));
        bank.noteOn(60, 1);
        juce::AudioBuffer<float> output(2, 100);
        output.clear();
        bank.render(output, 0, 14);
        bank.configure(PreparedSamplerSettings(s)); // Preserve the current reflected direction.
        bank.render(output, 14, 86);
        bank.noteOff(60);
        juce::AudioBuffer<float> tail(2, 32);
        tail.clear();
        bank.render(tail, 0, 32);
        require(tail.getMagnitude(0, 0, 32) == 0, "looping one-shot obeys note-off without stuck voice");
        return output;
    };
    for (bool reverse : {false, true})
        for (bool pingPong : {false, true})
        {
            auto output = loopRender(reverse, pingPong, 0);
            int position = reverse ? 15 : 0, direction = reverse ? -1 : 1;
            for (int i = 0; i < 100; ++i)
            {
                require(output.getSample(0, i) == float(position) &&
                            output.getSample(1, i) == float(position),
                        "loop intro and exact forward/reverse/ping-pong stereo traversal");
                position += direction;
                if (pingPong && direction > 0 && position > 11)
                {
                    position = 10;
                    direction = -1;
                }
                else if (pingPong && direction < 0 && position < 4)
                {
                    position = 5;
                    direction = 1;
                }
                else if (!pingPong && position >= 12 && direction > 0)
                    position = 4;
                else if (!pingPong && position < 4 && direction < 0)
                    position = 11;
            }
        }
    auto fractional = loopRender(false, false, -1200);
    require(fractional.getSample(0, 23) == 7.5f && fractional.getSample(0, 24) == 4,
            "fractional loop end interpolates to loop start instead of source tail");
    for (bool reverse : {false, true})
        for (bool pingPong : {false, true})
        {
            auto fast = loopRender(reverse, pingPong, 4800);
            for (int i = 0; i < 100; ++i)
                require(std::isfinite(fast.getSample(0, i)), "large loop steps remain bounded and finite");
        }
    juce::AudioBuffer<float> waveformPcm(2, 1031);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < 1031; ++i)
            waveformPcm.setSample(c, i, float(std::sin(i * .13 + c) * .8));
    waveformPcm.setSample(1, 1000, -1);
    SamplerWaveform waveform;
    waveform.prepare(waveformPcm);
    for (int begin = 0; begin < 1031; begin += 17)
        for (int length : {1, 7, 64, 65, 129, 513, 1031})
        {
            int end = std::min(1031, begin + length);
            float low = 1, high = -1;
            for (int c = 0; c < 2; ++c)
                for (int i = begin; i < end; ++i)
                {
                    low = std::min(low, waveformPcm.getSample(c, i));
                    high = std::max(high, waveformPcm.getSample(c, i));
                }
            const auto extent = waveform.query(waveformPcm, begin, end);
            require(extent.low == low && extent.high == high,
                    "waveform pyramid exact arbitrary interval including right-channel extrema");
        }
    Sample sample;
    sample.data.setSize(2, 48000);
    for (int i = 0; i < 48000; ++i)
        for (int c = 0; c < 2; ++c)
            sample.data.setSample(
                c, i, float(.5 * std::sin(2 * juce::MathConstants<double>::pi * 440 * i / 48000.)));
    auto render = [&](SamplerSettings settings, int pitch = 60)
    {
        VoiceBank voices;
        voices.prepare(sample, 48000);
        voices.configure(PreparedSamplerSettings(settings));
        voices.noteOn(pitch, 1);
        juce::AudioBuffer<float> result(2, 48000);
        result.clear();
        voices.render(result, 0, 48000);
        return result;
    };
    auto frequency = [](const juce::AudioBuffer<float> &buffer)
    {
        int crossings = 0;
        for (int i = 1001; i < 10000; ++i)
            crossings += buffer.getSample(0, i - 1) <= 0 && buffer.getSample(0, i) > 0;
        return crossings * 48000. / 9000.;
    };
    require(std::abs(frequency(render({})) - 440) < 6, "sampler default reference pitch");
    SamplerSettings settings;
    settings.sourceTempo = 60;
    settings.fitTempo = true;
    require(std::abs(frequency(render(settings)) - 880) < 6,
            "fit source 60 BPM to project 120 changes pitch and speed");
    auto fitted = render(settings);
    require(fitted.getMagnitude(0, 26000, 22000) == 0, "fit tempo halves sample duration");
    settings.fitTempo = false;
    require(std::abs(frequency(render(settings)) - 440) < 6, "set source tempo alone preserves sound");
    settings.sourceTempo = 0;
    settings.fitTempo = true;
    require(!settings.valid(), "fit rejects unknown tempo");
    settings = {};
    Sample rhythm;
    rhythm.sampleRate = 48000;
    rhythm.data.setSize(1, 48000 * 12);
    rhythm.data.clear();
    for (int i = 0; i < rhythm.data.getNumSamples(); ++i)
        if (i % 24000 < 240)
            rhythm.data.setSample(0, i, .8f);
    require(std::abs(estimateSampleTempo(rhythm) - 120) < 1, "original synthetic 120 BPM pulses detected");
    rhythm.data.clear();
    require(estimateSampleTempo(rhythm) == 0, "silent source has no tempo estimate");
    settings.cents = 1200;
    require(std::abs(frequency(render(settings)) - 880) < 6, "sampler octave pitch render");
    settings.cents = 0;
    settings.root = 72;
    require(std::abs(frequency(render(settings)) - 220) < 6, "sampler root calibration render");
    settings.root = -1;
    settings.start = .25;
    settings.end = .5;
    auto trimmed = render(settings);
    require(trimmed.getMagnitude(0, 1000, 10000) > .4f && trimmed.getMagnitude(0, 12001, 35999) == 0,
            "non-destructive source range controls rendered duration");
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < 48000; ++i)
            sample.data.setSample(c, i, i < 24000 ? .2f : -.4f);
    settings = {};
    require(render(settings).getSample(0, 1000) > .19f, "forward sample starts in first source half");
    settings.reverse = true;
    require(render(settings).getSample(0, 1000) < -.39f, "reverse reads last source half");
    settings = {};
    settings.envelope = true;
    settings.attack = .1;
    settings.decay = .1;
    settings.sustain = .25;
    settings.release = .05;
    VoiceBank voices;
    voices.prepare(sample, 48000);
    voices.configure(PreparedSamplerSettings(settings));
    voices.noteOn(60, 1);
    juce::AudioBuffer<float> envelope(2, 24000);
    envelope.clear();
    voices.render(envelope, 0, 14400);
    require(std::abs(envelope.getSample(0, 2399) - .1f) < .001f &&
                std::abs(envelope.getSample(0, 11999) - .05f) < .001f,
            "volume envelope attack/decay/sustain PCM");
    voices.noteOff(60);
    voices.render(envelope, 14400, 9600);
    require(envelope.getMagnitude(0, 17000, 7000) == 0,
            "enabled envelope releases an otherwise one-shot sample");
    voices.prepare(sample, 48000);
    voices.noteOn(60, 1);
    voices.noteOff(60);
    envelope.clear();
    voices.render(envelope, 0, 24000);
    require(envelope.getSample(0, 12000) > .19f, "disabled envelope preserves legacy one-shot release");

    const auto tone = folder.getChildFile("sampler-original-tone.wav");
    {
        for (int c = 0; c < 2; ++c)
            for (int frame = 0; frame < 48000; ++frame)
                sample.data.setSample(
                    c, frame,
                    float(.5 * std::sin(2 * juce::MathConstants<double>::pi * 440 * frame / 48000.)));
        auto stream = tone.createOutputStream();
        require(bool(stream), "original sampler calibration stream");
        stream->setPosition(0);
        stream->truncate();
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            format.createWriterFor(stream.release(), 48000., 2, 24, {}, 0));
        require(writer && writer->writeFromAudioSampleBuffer(sample.data, 0, 48000),
                "original sampler calibration WAV");
    }
    auto exported = Project::empty();
    exported.channels.resize(1);
    exported.channels.front().source = tone.getFullPathName();
    exported.channels.front().sampler.cents = 0;
    exported.channels.front().sampler.end = .5;
    exported.channels.front().sampler.sourceTempo = 60;
    exported.channels.front().sampler.fitTempo = true;
    exported.patterns.front().notes.push_back(
        {exported.allocate(), exported.channels.front().id, 0, 4, 60, 1, false});
    exported.placements.push_back({exported.allocate(), exported.patterns.front().id, 0, 0, 4, 0});
    AudioEngine offline(false);
    const auto wav = folder.getChildFile("sampler-offline.wav");
    require(offline.renderSync(exported, wav).wasOk(), "sampler configured offline export");
    double renderRate = 0;
    auto pcm = readWav(wav, renderRate);
    require(renderRate == 48000 && std::abs(frequency(pcm) - 880) < 6 &&
                pcm.getMagnitude(0, 14000, 20000) == 0,
            "offline graph applies tempo-fit resampling and source range");
    exported.channels.front().sampler.loop = true;
    exported.channels.front().sampler.loopStart = .1;
    exported.channels.front().sampler.loopEnd = .4;
    exported.channels.front().sampler.envelope = true;
    exported.channels.front().sampler.attack = exported.channels.front().sampler.decay = 0;
    exported.channels.front().sampler.release = 0;
    exported.patterns.front().length = 8;
    exported.placements.front().length = 8;
    for (bool pingPong : {false, true})
    {
        exported.channels.front().sampler.pingPong = pingPong;
        const auto loopWav =
            folder.getChildFile(pingPong ? "sampler-loop-ping-pong.wav" : "sampler-loop-forward.wav");
        require(offline.renderSync(exported, loopWav).wasOk(), "looped sampler offline WAV export");
        const auto loopPcm = readWav(loopWav, renderRate);
        require(loopPcm.getNumSamples() >= 98000, "loop release probe lies inside exported arrangement");
        require(loopPcm.getMagnitude(0, 48000, 10000) > .1f,
                "offline sampler loop sustains beyond trimmed source duration");
        require(loopPcm.getMagnitude(0, 97000, 1000) == 0,
                "offline sampler loop releases at sequenced note end");
    }

    Controller controller;
    controller.replace(Project::demo());
    const auto original = controller.project.state();
    CommandArgs args;
    args.channel = controller.project.channels.front().id;
    args.value = 300;
    require(controller.execute("channel.sampler.cents", args).wasOk(), "canonical sampler pitch command");
    controller.undoEdit();
    require(controller.project.state().isEquivalentTo(original), "sampler settings exact undo");
    controller.redoEdit();
    args.value = .8;
    require(controller.execute("channel.sampler.start", args).wasOk(), "sampler range command");
    auto valid = controller.project.state();
    args.value = .2;
    require(controller.execute("channel.sampler.end", args).failed() &&
                controller.project.state().isEquivalentTo(valid),
            "inverted sampler range fails atomically");
    args.value = std::numeric_limits<double>::quiet_NaN();
    require(controller.execute("channel.sampler.attack", args).failed() &&
                controller.project.state().isEquivalentTo(valid),
            "nonfinite sampler settings fail atomically");
    args.value = 60.5;
    require(controller.execute("channel.sampler.root", args).failed(), "fractional root rejected");
    args.enabled = true;
    require(controller.execute("channel.sampler.envelope", args).wasOk(), "sampler envelope command");
    args.value = 90;
    require(controller.execute("channel.sampler.sourceTempo", args).wasOk(), "set source tempo command");
    args.enabled = true;
    require(controller.execute("channel.sampler.fitTempo", args).wasOk(), "fit source tempo command");
    controller.undoEdit();
    require(!controller.project.channels.front().sampler.fitTempo, "fit undo restores prior timing");
    controller.redoEdit();
    require(controller.project.channels.front().sampler.fitTempo, "fit redo restores timing");
    args.value = 0;
    auto validTempoState = controller.project.state();
    require(controller.execute("channel.sampler.sourceTempo", args).failed() &&
                controller.project.state().isEquivalentTo(validTempoState),
            "unknown source BPM while fitting rejected atomically");
    const auto path = folder.getChildFile("sampler-settings.vibra");
    require(saveProject(controller.project, path).wasOk(), "sampler project save");
    Project restored;
    require(loadProject(path, restored).wasOk() && restored.version == Project::currentVersion &&
                restored.state().isEquivalentTo(controller.project.state()),
            "sampler schema7 exact round trip");
    auto schema5 = Project::demo().state();
    schema5.setProperty("version", 5, nullptr);
    stripSamplerVoiceFieldsForLegacy(schema5);
    for (auto c : schema5.getChildWithName("CHANNELS"))
    {
        auto sampler = c.getChildWithName("SAMPLER");
        sampler.removeProperty("sourceTempo", nullptr);
        sampler.removeProperty("fitTempo", nullptr);
        for (const auto *field : {"loop", "pingPong", "loopStart", "loopEnd"})
            sampler.removeProperty(field, nullptr);
    }
    require(Project::parse(schema5, restored).wasOk() && restored.channels.front().sampler.sourceTempo == 0 &&
                !restored.channels.front().sampler.fitTempo,
            "schema5 migrates tempo without changing sound");
    auto schema6 = Project::demo().state();
    schema6.setProperty("version", 6, nullptr);
    stripSamplerVoiceFieldsForLegacy(schema6);
    for (auto c : schema6.getChildWithName("CHANNELS"))
        for (const auto *field : {"loop", "pingPong", "loopStart", "loopEnd"})
            c.getChildWithName("SAMPLER").removeProperty(field, nullptr);
    require(Project::parse(schema6, restored).wasOk() && !restored.channels.front().sampler.loop,
            "schema6 migrates to disabled loops without changing sound");
    Controller loopController;
    loopController.replace(Project::demo());
    CommandArgs loopArgs;
    loopArgs.channel = loopController.project.channels.front().id;
    loopArgs.beat = .25;
    loopArgs.length = .75;
    const auto beforeLoop = loopController.project.state();
    require(loopController.execute("channel.sampler.loopRange", loopArgs).wasOk(),
            "atomic loop bounds command");
    loopArgs.enabled = true;
    require(loopController.execute("channel.sampler.loop", loopArgs).wasOk() &&
                loopController.execute("channel.sampler.pingPong", loopArgs).wasOk(),
            "loop mode commands");
    const auto loopState = loopController.project.state();
    Project loopRestored;
    require(Project::parse(loopState, loopRestored).wasOk() && loopRestored.state().isEquivalentTo(loopState),
            "loop project exact round trip");
    loopController.undoEdit();
    loopController.undoEdit();
    loopController.undoEdit();
    require(loopController.project.state().isEquivalentTo(beforeLoop), "loop controls exact Undo");
    loopController.redoEdit();
    loopController.redoEdit();
    loopController.redoEdit();
    require(loopController.project.state().isEquivalentTo(loopState), "loop controls exact Redo");
    loopArgs.beat = .9;
    loopArgs.length = .1;
    require(loopController.execute("channel.sampler.loopRange", loopArgs).failed() &&
                loopController.project.state().isEquivalentTo(loopState),
            "invalid loop bounds preserve model");
    auto invalid = restored.state();
    invalid.setProperty("version", 4, nullptr);
    stripSamplerVoiceFieldsForLegacy(invalid);
    require(Project::parse(invalid, restored).failed(), "sampler DSP state cannot masquerade as schema4");
    for (int version = 1; version <= 4; ++version)
    {
        auto legacy = Project::demo().state();
        legacy.setProperty("version", version, nullptr);
        stripSamplerVoiceFieldsForLegacy(legacy);
        for (auto c : legacy.getChildWithName("CHANNELS"))
            c.removeChild(c.getChildWithName("SAMPLER"), nullptr);
        if (version < 3)
            legacy.removeChild(legacy.getChildWithName("AUDIO_PLACEMENTS"), nullptr);
        require(Project::parse(legacy, restored).wasOk() && restored.version == Project::currentVersion &&
                    restored.channels.front().sampler == SamplerSettings{},
                "legacy sampler settings migration defaults");
    }
    auto missing = controller.project.state();
    missing.getChildWithName("CHANNELS")
        .getChild(0)
        .getChildWithName("SAMPLER")
        .removeProperty("release", nullptr);
    auto before = restored.state();
    require(Project::parse(missing, restored).failed() && restored.state().isEquivalentTo(before),
            "incomplete sampler settings preserve destination project");
}

static void samplerLiveTests(const juce::File &folder)
{
    require(folder.getChildFile("sampler-live-deadline-trace.txt")
                .replaceWithText("Fresh run; thread CPU/cycle observations wrap the virtual callback; "
                                 "no physical audio/listening.\n"),
            "sampler live trace starts fresh");
    juce::String report;
    for (bool patternMode : {true, false})
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
        require(device != nullptr, "sampler live virtual device");
        auto project = Project::empty();
        project.tempo = 240;
        project.channels.resize(2);
        for (auto &channel : project.channels)
            channel.source = "builtin:bass";
        project.channels[0].pan = -1;
        project.channels[0].sampler.sourceTempo = 100;
        project.channels[1].pan = 1;
        auto &pattern = project.patterns.front();
        const auto patternId = pattern.id, affected = project.channels[0].id;
        for (auto &channel : project.channels)
            pattern.notes.push_back({project.allocate(), channel.id, 0, 4, 48, .7f, false});
        project.placements.push_back({project.allocate(), patternId, 0, 0, 4, 0});
        Controller controller;
        controller.replace(project);
        require(awaitProjection(audio, controller.project, patternMode, patternId).wasOk(),
                "sampler live initial projection");
        audio.setLooping(true);
        audio.playStop();
        dispatchMessages(30);
        juce::AudioBuffer<float> block(2, 256);
        double maximum = 0;
        int callbacks = 0, overruns = 0, silent = 0, loops = 0;
        std::array<SyntheticCallbackSpan, 1024> callbackSpans{};
        double previous = 0;
        auto tick = [&]
        {
            block.clear();
#if JUCE_WINDOWS
            FILETIME created{}, exited{}, kernelBefore{}, userBefore{}, kernelAfter{}, userAfter{};
            ULONG64 cyclesBefore = 0, cyclesAfter = 0;
            const bool cpuClock =
                GetThreadTimes(GetCurrentThread(), &created, &exited, &kernelBefore, &userBefore) != 0;
            const bool cycleClock = QueryThreadCycleTime(GetCurrentThread(), &cyclesBefore) != 0;
#endif
            const auto began = juce::Time::getHighResolutionTicks();
            device->process(block);
            const auto ended = juce::Time::getHighResolutionTicks();
            const auto ms = juce::Time::highResolutionTicksToSeconds(ended - began) * 1000;
            double threadCpuMs = -1;
            juce::uint64 threadCycles = 0;
#if JUCE_WINDOWS
            if (cycleClock && QueryThreadCycleTime(GetCurrentThread(), &cyclesAfter) &&
                cyclesAfter >= cyclesBefore)
                threadCycles = cyclesAfter - cyclesBefore;
            if (cpuClock && GetThreadTimes(GetCurrentThread(), &created, &exited, &kernelAfter, &userAfter))
            {
                const auto ticks = [](FILETIME value)
                { return (juce::uint64(value.dwHighDateTime) << 32) | value.dwLowDateTime; };
                threadCpuMs =
                    double(ticks(kernelAfter) + ticks(userAfter) - ticks(kernelBefore) - ticks(userBefore)) /
                    10000.;
            }
#endif
            require(callbacks < int(callbackSpans.size()), "sampler live timing capacity");
            callbackSpans[size_t(callbacks)] = {device->lastTiming(), began, ended};
            require(device->lastTiming().containedBy(began, ended), "sampler live timing spans are nested");
            maximum = std::max(maximum, ms);
            overruns += ms > 256. / 48000. * 1000;
            if (ms > 256. / 48000. * 1000 || block.getMagnitude(1, 0, 256) == 0)
                folder.getChildFile("sampler-live-deadline-trace.txt")
                    .appendText(juce::String(patternMode ? "Pattern" : "Song") +
                                " callback=" + juce::String(callbacks) + " elapsedMs=" + juce::String(ms, 4) +
                                " threadCpuMs=" + juce::String(threadCpuMs, 4) + " threadCycles=" +
                                juce::String(threadCycles) + device->lastTiming().describe(began, ended) +
                                " right=" + juce::String(block.getMagnitude(1, 0, 256), 9) + " headSeconds=" +
                                juce::String(audio.projectionDiagnostics().headSeconds, 6) + "\n");
            silent += block.getMagnitude(1, 0, 256) == 0;
            ++callbacks;
            const auto position = audio.projectionDiagnostics().headSeconds * project.tempo / 60.;
            loops += position + .1 < previous;
            previous = position;
        };
        for (int i = 0; i < 16; ++i)
            tick();
        const auto before = audio.projectionDiagnostics();
        const int warmupSilent = silent;
        const auto starts = device->starts, stops = device->stops;
        for (int i = 0; i < 80; ++i)
        {
            const std::array<juce::String, 16> actions{"cents",   "root",     "start",       "end",
                                                       "reverse", "envelope", "attack",      "decay",
                                                       "sustain", "release",  "sourceTempo", "fitTempo",
                                                       "loop",    "pingPong", "loopStart",   "loopEnd"};
            CommandArgs args;
            args.channel = affected;
            const auto action = actions[size_t(i % actions.size())];
            args.value = action == "sourceTempo" ? 100
                         : action == "loopStart" ? .2
                         : action == "loopEnd"   ? .8
                         : action == "cents"     ? (i % 2 ? 300 : -300)
                         : action == "root"      ? 60
                         : action == "start"     ? .1
                         : action == "end"       ? .9
                         : action == "sustain"   ? .7
                                                 : .02;
            args.enabled = i % 20 < 10;
            auto head = audio.positionBeat();
            require(controller.execute("channel.sampler." + action, args).wasOk(), "live sampler command");
            require(awaitProjection(audio, controller.project, patternMode, patternId).wasOk(),
                    "live sampler incremental projection");
            require(audio.playing() && std::abs(audio.positionBeat() - head) < .01,
                    "sampler edit preserves playhead");
            if (i % 7 == 0)
            {
                controller.undoEdit();
                require(awaitProjection(audio, controller.project, patternMode, patternId).wasOk(),
                        "live sampler undo projection");
                controller.redoEdit();
                require(awaitProjection(audio, controller.project, patternMode, patternId).wasOk(),
                        "live sampler redo projection");
            }
            for (int j = 0; j < 12; ++j)
                tick();
        }
        const auto after = audio.projectionDiagnostics();
        juce::String phaseTrace =
            "callback,outerMs," + juce::String(SyntheticCallbackTiming::csvHeader) + "\n";
        for (int i = 0; i < callbacks; ++i)
        {
            const auto &span = callbackSpans[size_t(i)];
            phaseTrace += juce::String(i) + "," +
                          juce::String(SyntheticCallbackTiming::milliseconds(span.began, span.ended), 6) +
                          "," + span.timing.csv(span.began, span.ended) + "\n";
        }
        require(folder
                    .getChildFile(patternMode ? "sampler-live-pattern-phases.csv"
                                              : "sampler-live-song-phases.csv")
                    .replaceWithText(phaseTrace),
                "sampler live timing phases retained after processing");
        report += juce::String(patternMode ? "Pattern" : "Song") + ": callbacks=" + juce::String(callbacks) +
                  " loops=" + juce::String(loops) + " maxMs=" + juce::String(maximum, 4) +
                  " overruns=" + juce::String(overruns) +
                  " unaffectedSilent=" + juce::String(silent - warmupSilent) +
                  " replacements=" + juce::String(juce::int64(after.replacements - before.replacements)) +
                  " deviceStarts=" + juce::String(device->starts - starts) +
                  " deviceStops=" + juce::String(device->stops - stops) + "\n";
        folder.getChildFile("sampler-live-verification.txt")
            .replaceWithText(report + "Virtual callbacks only; not listening/hardware.\n");
        require(after.edit == before.edit && after.playbackContext == before.playbackContext &&
                    after.replacements == before.replacements &&
                    after.samplerCreations == before.samplerCreations &&
                    after.cache.preparations == before.cache.preparations && device->starts == starts &&
                    device->stops == stops,
                "sampler controls reuse graph, device and prepared PCM");
        for (size_t i = 0; i < after.channels.size(); ++i)
            require(after.channels[i].sample == before.channels[i].sample &&
                        after.channels[i].graphInitialisations == before.channels[i].graphInitialisations,
                    "sampler controls preserve per-Channel lifetime");
        require(overruns == 0 && silent == warmupSilent && loops >= 2,
                "sampler rapid edits across loops retain unaffected voices and callback budget");
        audio.stop();
    }
}
