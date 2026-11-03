// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "LegacySamplerVoices.h"
#include <juce_cryptography/juce_cryptography.h>
static void audioClipTests(const juce::File &folder)
{
    const auto source = folder.getChildFile("audio-original-45s.wav");
    {
        juce::AudioBuffer<float> pcm(2, 48000 * 45);
        for (int channel = 0; channel < 2; ++channel)
            for (int frame = 0; frame < pcm.getNumSamples(); ++frame)
                pcm.setSample(channel, frame, frame < 48000 * 30 ? .125f : -.125f);
        auto stream = source.createOutputStream();
        require(bool(stream), "audio clip source stream");
        stream->setPosition(0);
        stream->truncate();
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            format.createWriterFor(stream.release(), 48000., 2, 16, {}, 0));
        require(bool(writer) && writer->writeFromAudioSampleBuffer(pcm, 0, pcm.getNumSamples()),
                "audio clip original long WAV");
    }
    Controller controller;
    controller.project.channels.clear();
    CommandArgs add;
    add.text = source.getFullPathName();
    add.length = 45;
    add.beat = 1;
    add.lane = 6;
    add.mixer = 2;
    const auto before = controller.project.state();
    require(controller.execute("audio.add", add).wasOk(), "audio source and placement canonical creation");
    const Id clip = controller.project.audioPlacements.front().id;
    const Id channel = controller.project.audioPlacements.front().channel;
    require(controller.project.channel(channel)->audioClip && controller.project.endBeat() == 91 &&
                controller.project.channel(channel)->mixer == 2,
            "audio source duration/routing independent of Playlist lane");
    controller.undoEdit();
    require(controller.project.state().isEquivalentTo(before), "audio import exact undo");
    controller.redoEdit();
    CommandArgs trim;
    trim.target = clip;
    trim.offset = 35.25;
    trim.length = 1.25;
    require(controller.execute("audio.trim", trim).wasOk(), "audio window trim past sampler limit");
    const auto trimmed = controller.project.state();
    trim.offset = 44.5;
    require(controller.execute("audio.trim", trim).failed() &&
                controller.project.state().isEquivalentTo(trimmed),
            "audio invalid source window preserves project");
    CommandArgs duplicate;
    duplicate.ids = {clip};
    duplicate.beat = 4;
    require(controller.execute("clip.duplicate", duplicate).wasOk() &&
                controller.project.audioPlacements.back().channel == channel,
            "audio duplicate keeps linked Channel");
    CommandArgs unique;
    unique.target = controller.project.audioPlacements.back().id;
    require(controller.execute("clip.unique", unique).wasOk() &&
                controller.project.audioPlacements.back().channel != channel &&
                controller.project.channels.back().source == source.getFullPathName(),
            "audio Make Unique separates settings but reuses media");
    controller.undoEdit();
    controller.undoEdit();
    require(controller.project.state().isEquivalentTo(trimmed), "audio duplication/unique exact undo");
    const auto file = folder.getChildFile("audio-window.vibra");
    require(saveProject(controller.project, file).wasOk(), "audio portable media collection");
    auto downgraded = controller.project;
    downgraded.version = 2;
    const auto savedHash = juce::MD5(file).toHexString();
    require(saveProject(downgraded, file).failed() && juce::MD5(file).toHexString() == savedHash,
            "audio schema downgrade refuses to overwrite saved project");
    Project loaded;
    require(loadProject(file, loaded).wasOk() && loaded.version == Project::currentVersion &&
                loaded.audioPlacements.front().offset == 35.25 && loaded.channel(channel)->audioClip &&
                juce::File(loaded.channel(channel)->source).existsAsFile(),
            "audio versioned project and source-window round trip");
    auto legacy = Project::empty().state();
    legacy.setProperty("version", 2, nullptr);
    stripSamplerVoiceFieldsForLegacy(legacy);
    for (auto channel : legacy.getChildWithName("CHANNELS"))
        channel.removeChild(channel.getChildWithName("SAMPLER"), nullptr);
    legacy.removeChild(legacy.getChildWithName("AUDIO_PLACEMENTS"), nullptr);
    require(Project::parse(legacy, loaded).wasOk() && loaded.audioPlacements.empty() &&
                loaded.version == Project::currentVersion,
            "version 2 project migrates without invented audio");
    {
        Customization custom = Customization::familiarDefaults();
        CommandArgs move;
        move.beat = 1;
        move.length = 0;
        CommandArgs copy;
        copy.beat = 2;
        require(custom
                    .addMacro({"macro.audio-copy-move",
                               "Audio copy move",
                               {{"clip.duplicate", copy, true}, {"clip.move", move, true}}})
                    .wasOk(),
                "audio macro registration");
        MacroContext context;
        context.clips = {clip};
        const auto beforeMacro = controller.project.state();
        require(custom.runMacro("macro.audio-copy-move", controller, context).wasOk() &&
                    context.clips.size() == 1 && context.clips.front() != clip &&
                    controller.project.audioPlacements.back().beat == 4,
                "audio macro follows newly created linked clip");
        controller.undoEdit();
        require(controller.project.state().isEquivalentTo(beforeMacro), "audio macro one exact undo");
    }
    AudioEngine audio(false);
    const auto wav = folder.getChildFile("audio-window-render.wav");
    require(audio.renderSync(controller.project, wav).wasOk(), "audio-only streamed Tracktion render");
    double rate = 0;
    const auto rendered = readWav(wav, rate);
    require(rate == 48000 && std::abs(rendered.getNumSamples() - 84000) <= 1 &&
                rendered.getMagnitude(0, 22000) < .000001f && rendered.getSample(0, 36000) < -.01f,
            "audio source offset, lead-in and unchanged-speed duration in actual PCM");
    auto halfGain = controller.project;
    halfGain.channel(channel)->gain *= .5f;
    const auto quieter = folder.getChildFile("audio-window-half-gain.wav");
    require(audio.renderSync(halfGain, quieter).wasOk(), "audio shared Channel gain render");
    const auto quietPcm = readWav(quieter, rate);
    require(std::abs(quietPcm.getSample(0, 36000) - rendered.getSample(0, 36000) * .5f) < .001f,
            "audio Channel gain reaches source PCM");
    auto muteInsert = controller.project;
    for (auto &insert : muteInsert.mixer)
        if (insert.index == 2)
            insert.muted = true;
    const auto muted = folder.getChildFile("audio-window-muted-bus.wav");
    require(audio.renderSync(muteInsert, muted).wasOk(), "audio Mixer route render");
    const auto mutePcm = readWav(muted, rate);
    require(mutePcm.getMagnitude(0, mutePcm.getNumSamples()) < .000001f,
            "audio follows assigned Mixer insert, not Playlist lane");
    auto changedTempo = controller.project;
    changedTempo.tempo = 80;
    const auto slower = folder.getChildFile("audio-window-tempo.wav");
    require(audio.renderSync(changedTempo, slower).wasOk(), "audio tempo placement render");
    const auto slowerPcm = readWav(slower, rate);
    require(std::abs(slowerPcm.getNumSamples() - 96000) <= 1 && slowerPcm.getSample(0, 48000) < -.01f,
            "audio beat anchor follows tempo but duration/pitch are not stretched");
    auto missing = controller.project;
    missing.channel(channel)->source = source.getSiblingFile("missing-audio.wav").getFullPathName();
    const auto previous = juce::MD5(wav).toHexString();
    require(audio.renderSync(missing, wav).failed() && juce::MD5(wav).toHexString() == previous,
            "audio missing source preserves previous export");
    {
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
        require(device != nullptr, "audio clip virtual device type");
        auto song = Project::empty();
        auto fileChannel = *controller.project.channel(channel);
        fileChannel.id = song.allocate();
        fileChannel.pan = -1;
        song.channels.push_back(fileChannel);
        song.audioPlacements.push_back({song.allocate(), fileChannel.id, 3, 0, 35.25, 1.25});
        const auto bass = song.channels[3].id;
        song.patterns.front().notes.push_back({song.allocate(), bass, 0, 4, 48, .35f, false});
        song.placements.push_back({song.allocate(), song.patterns.front().id, 0, 0, 4, 0});
        require(awaitProjection(live, song, false, song.patterns.front().id).wasOk(),
                "audio clip live setup");
        live.setLooping(true);
        live.playStop();
        dispatchMessages(20);
        juce::AudioBuffer<float> block(2, 256);
        for (int i = 0; i < 12; ++i)
        {
            device->process(block);
            dispatchMessages(1);
        }
        const auto beforeLive = live.projectionDiagnostics();
        const int starts = device->starts, stops = device->stops;
        int silence = 0, overruns = 0;
        double maximum = 0;
        for (int i = 0; i < 480; ++i)
        {
            if (i % 4 == 0)
            {
                song.audioPlacements.front().beat = i % 8 ? .25 : 0;
                require(awaitProjection(live, song, false, song.patterns.front().id).wasOk(),
                        "audio clip control-thread position update");
            }
            const double start = juce::Time::getMillisecondCounterHiRes();
            device->process(block);
            const double elapsed = juce::Time::getMillisecondCounterHiRes() - start;
            maximum = std::max(maximum, elapsed);
            overruns += elapsed > 256. / 48000. * 1000.;
            silence += block.getMagnitude(1, 0, 256) == 0;
            dispatchMessages(1);
        }
        const auto afterLive = live.projectionDiagnostics();
        require(beforeLive.edit == afterLive.edit &&
                    beforeLive.playbackContext == afterLive.playbackContext &&
                    beforeLive.replacements == afterLive.replacements &&
                    beforeLive.samplerCreations == afterLive.samplerCreations && starts == device->starts &&
                    stops == device->stops && live.playing(),
                "audio edits preserve Edit/context/sampler/device transport lifetime");
        folder.getChildFile("audio-clip-live-verification.txt")
            .replaceWithText(
                "Song: 480 callbacks / 120 audio moves; right-only sustained bass silent blocks " +
                juce::String(silence) + ", overruns " + juce::String(overruns) + ", max callback " +
                juce::String(maximum, 4) + " ms; initial/final sampler graph initialisations " +
                juce::String(beforeLive.channels.back().graphInitialisations) + "/" +
                juce::String(afterLive.channels.back().graphInitialisations) +
                ". Synthetic device; no listening/hardware/native Playlist input.\n");
        require(silence == 0 && overruns == 0, "audio edits preserve sustained bass and callback deadline");
        const auto beforeNotes = live.projectionDiagnostics();
        for (int i = 0; i < 80; ++i)
        {
            song.patterns.front().notes.front().velocity = i % 2 ? .35f : .4f;
            require(awaitProjection(live, song, false, song.patterns.front().id).wasOk(),
                    "note edit with streamed audio clip");
            device->process(block);
            dispatchMessages(1);
        }
        const auto afterNotes = live.projectionDiagnostics();
        require(beforeNotes.replacements == afterNotes.replacements &&
                    beforeNotes.playbackContext == afterNotes.playbackContext &&
                    beforeNotes.channels.back().graphInitialisations ==
                        afterNotes.channels.back().graphInitialisations,
                "note edits with audio do not rebuild sampler graph or transport");
    }
    require(
        folder.getChildFile("audio-clip-verification.txt")
            .replaceWithText(
                "Original 45-second WAV; linked/unique Channels; trim/undo/invalid window; v2 migration/v3 "
                "portable round trip; "
                "audio-only source-window PCM, beat/tempo anchoring without stretch and missing-source "
                "export preservation: PASS. "
                "No native Playlist import/edit, live graph-update, hardware or listening qualification.\n"),
        "audio clip evidence");
}
