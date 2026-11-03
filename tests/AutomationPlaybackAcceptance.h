// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
static void automationPlaybackTests(const juce::File &folder)
{
    const auto sourceFile = folder.getChildFile("automation-original-constant.wav");
    {
        juce::AudioBuffer<float> pcm(2, 48000 * 4);
        for (int ch = 0; ch < 2; ++ch)
            for (int frame = 0; frame < pcm.getNumSamples(); ++frame)
                pcm.setSample(ch, frame, .125f);
        auto stream = sourceFile.createOutputStream();
        require(bool(stream), "automation original fixture stream");
        stream->setPosition(0);
        stream->truncate();
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            format.createWriterFor(stream.release(), 48000., 2, 24, {}, 0));
        require(writer && writer->writeFromAudioSampleBuffer(pcm, 0, pcm.getNumSamples()),
                "original constant WAV");
    }
    Controller c;
    c.project.channels.clear();
    CommandArgs a;
    a.text = sourceFile.getFullPathName();
    a.length = 4;
    a.mixer = 17;
    require(c.execute("audio.add", a).wasOk(), "actual automation audio Channel routed to Insert17");
    c.project.channels[0].gain = 1;
    c.project.mixer[0].gain = c.project.mixer[17].gain = 1;
    const auto dryProject = c.project;
    a.text.clear();
    a.beat = 1;
    a.length = 4;
    a.lane = 1;
    require(c.execute("automation.create", a).wasOk(), "insert fade command");
    auto &insert = c.project.automation.sources.front();
    insert.points.front().value = 0;
    insert.points.back().value = .75f;
    c.project.automation.initialValues.front().value = .3f;
    c.project.mixer[17].gain = 0; // Automation replaces the manual fader, including zero.
    a.mixer = 0;
    a.beat = 2;
    a.length = 2;
    a.lane = 2;
    require(c.execute("automation.create", a).wasOk(), "Master fade command");
    auto &master = c.project.automation.sources.back();
    master.points.front().value = .5f;
    master.points.back().value = .25f;
    master.points.back().incoming = AutomationShape::smooth;
    c.project.automation.initialValues.back().value = .8f;
    const auto automated = c.project;
    const auto oracle = [](double beat)
    {
        const double insertGain = beat < 1 ? .3 : beat >= 5 ? 1.5 : (beat - 1) * 1.5 / 4;
        const double x = std::clamp((beat - 2) / 2., 0., 1.);
        const double masterGain = beat < 2 ? .8 : 1 - .5 * (x * x * (3 - 2 * x));
        return insertGain * masterGain;
    };
    AudioEngine renderer(false);
    double maxError = 0;
    for (double rate : {44100., 48000.})
    {
        const auto dryFile = folder.getChildFile("automation-dry-" + juce::String(int(rate)) + ".wav");
        require(renderer.renderSync(dryProject, dryFile, rate, 24, 256).wasOk(), "real DAW dry export");
        double decodedRate;
        const auto dry = readWav(dryFile, decodedRate);
        require(decodedRate == rate && dry.getNumSamples() == int(rate * 4), "dry export dimensions");
        for (int block : {256, 512})
        {
            const auto wetFile = folder.getChildFile("automation-wet-" + juce::String(int(rate)) + "-" +
                                                     juce::String(block) + ".wav");
            require(renderer.renderSync(automated, wetFile, rate, 24, block).wasOk(),
                    "real DAW Insert/Master curve export");
            const auto wet = readWav(wetFile, decodedRate);
            require(decodedRate == rate && wet.getNumSamples() == dry.getNumSamples() &&
                        wet.getNumChannels() == 2,
                    "automated export dimensions");
            for (int frame = int(rate * .05); frame < wet.getNumSamples() - int(rate * .05); frame += 97)
                for (int ch = 0; ch < 2; ++ch)
                {
                    const auto expected = dry.getSample(ch, frame) * oracle(frame * 2. / rate);
                    const auto error = std::abs(wet.getSample(ch, frame) - expected);
                    maxError = std::max(maxError, error);
                    require(error < 3.e-5,
                            "sample-time DAW export agrees with independent composed-gain oracle");
                }
        }
    }
    auto muted = automated;
    muted.mixer[17].muted = true;
    const auto muteFile = folder.getChildFile("automation-muted.wav");
    require(renderer.renderSync(muted, muteFile).wasOk(), "mute overrides curve export");
    double decodedRate;
    const auto silent = readWav(muteFile, decodedRate);
    require(silent.getMagnitude(0, silent.getNumSamples()) < 1.e-6f,
            "muted insert remains silent despite curve");

    AudioEngine live(false);
    auto &manager = live.engine.getDeviceManager();
    manager.deviceManager.addAudioDeviceType(std::make_unique<SyntheticDeviceType>());
    juce::XmlElement setup("DEVICESETUP");
    setup.setAttribute("deviceType", "VibeRa synthetic");
    setup.setAttribute("audioOutputDeviceName", "In-memory output");
    setup.setAttribute("audioDeviceRate", 48000.);
    setup.setAttribute("audioDeviceBufferSize", 256);
    live.engine.getPropertyStorage().setXmlProperty(te::SettingID::audio_device_setup, setup);
    manager.initialise(0, 2);
    dispatchMessages(20);
    auto *device = dynamic_cast<SyntheticDevice *>(manager.deviceManager.getCurrentAudioDevice());
    require(device != nullptr, "automation virtual output");
    require(awaitProjection(live, automated, false, automated.patterns[0].id).wasOk(),
            "live curves prepared");
    juce::AudioBuffer<float> buffer(2, 256);
    live.setLooping(true);
    live.playStop();
    dispatchMessages(30);
    for (int i = 0; i < 10; ++i)
        device->process(buffer);
    const auto before = live.projectionDiagnostics();
    const auto gainStages = live.mixerGraphInitialisations();
    const int starts = device->starts, stops = device->stops;
    Controller edits;
    edits.replace(automated);
    double maxMs = 0;
    int overruns = 0;
    for (int i = 0; i < 320; ++i)
    {
        if (i == 32 || i == 96 || i == 192)
        {
            a.target = edits.project.automation.sources.front().points.back().id;
            a.beat = 4;
            a.value = i == 32 ? .6 : .75;
            require(edits.execute("automation.point.move", a).wasOk(), "point edit while Song plays");
            require(live.rebuild(edits.project, false, edits.project.patterns[0].id).wasOk(),
                    "curve publication while Song plays");
        }
        if (i == 64)
        {
            edits.undoEdit();
            require(live.rebuild(edits.project, false, edits.project.patterns[0].id).wasOk(),
                    "live automation Undo");
        }
        if (i == 80)
        {
            edits.redoEdit();
            require(live.rebuild(edits.project, false, edits.project.patterns[0].id).wasOk(),
                    "live automation Redo");
        }
        if (i == 128 || i == 256)
        {
            live.seekBeat(i == 128 ? 7.8 : 2.5);
            dispatchMessages(10);
        }
        const auto start = juce::Time::getMillisecondCounterHiRes();
        device->process(buffer);
        const auto ms = juce::Time::getMillisecondCounterHiRes() - start;
        maxMs = std::max(maxMs, ms);
        overruns += ms > 256 * 1000. / 48000.;
        for (int frame : {0, 128, 255})
            require(std::isfinite(buffer.getSample(0, frame)),
                    "live automation finite through edit/seek/loop");
        if (i % 8 == 0)
            dispatchMessages(1);
    }
    const auto after = live.projectionDiagnostics();
    require(gainStages == live.mixerGraphInitialisations() && gainStages.size() == automated.mixer.size(),
            "automation edits retain every Mixer/Master gain stage without reinitialisation");
    require(live.playing() && before.edit == after.edit && before.playbackContext == after.playbackContext &&
                before.replacements == after.replacements &&
                before.samplerCreations == after.samplerCreations && starts == device->starts &&
                stops == device->stops,
            "routine automation retains Edit/context/device/graph");
    require(overruns == 0, "virtual live automation callback deadlines");
    live.stop();
    folder.getChildFile("automation-playback-verification.txt")
        .replaceWithText("PASS: actual DAW offline Insert17/Master gain, initialization, manual-zero "
                         "replacement, smooth/linear curves, post-clip hold and mute; 44100/48000Hz, "
                         "256/512-frame exports. Independent PCM oracle max error=" +
                         juce::String(maxError, 9) +
                         ". Live320 virtual callbacks, point edits/Undo/Redo, seeks/loop; maxMs=" +
                         juce::String(maxMs, 4) + "; overruns=" + juce::String(overruns) +
                         ". No physical device, listening, plugin automation or full FL parity claim.\n");
}
