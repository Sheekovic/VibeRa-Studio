// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "LegacySamplerVoices.h"
static void playlistEditTests(const juce::File &folder)
{
    Controller c;
    CommandArgs place;
    place.pattern = c.project.patterns.front().id;
    place.beat = 1;
    place.length = 4;
    require(c.execute("clip.add", place).wasOk(), "pattern slice fixture");
    const auto original = c.project.state();
    const auto first = c.project.placements.front().id;
    CommandArgs cut;
    cut.ids = {first};
    cut.beat = 2;
    require(c.execute("clip.slice", cut).wasOk(), "pattern slice canonical action");
    const auto &left = c.project.placements.front(), &right = c.project.placements.back();
    require(left.id == first && left.length == 1 && right.length == 3 && right.beat == 2 &&
                right.offset == 1 && right.pattern == left.pattern,
            "slice retains linked pattern and contiguous source windows");
    const auto split = c.project.state();
    c.undoEdit();
    require(c.project.state().isEquivalentTo(original), "slice exact undo");
    c.redoEdit();
    require(c.project.state().isEquivalentTo(split), "slice exact redo");
    CommandArgs slip;
    slip.ids = {first};
    slip.beat = 1;
    require(c.execute("clip.slip", slip).wasOk() && c.project.placements.front().offset == 1 &&
                c.project.placements.front().beat == 1 && c.project.placements.front().length == 1,
            "pattern slip keeps timeline bounds");
    slip.beat = 100;
    require(c.execute("clip.slip", slip).wasOk() && c.project.placements.front().offset == 3,
            "pattern slip source end bound");
    auto valid = c.project.state();
    slip.ids = {999999};
    require(c.execute("clip.slip", slip).failed() && c.project.state().isEquivalentTo(valid),
            "stale slip rejected without mutation");
    const auto source = folder.getChildFile("slice-original-varying-45s.wav");
    {
        juce::AudioBuffer<float> pcm(2, 48000 * 45);
        for (int channel = 0; channel < 2; ++channel)
            for (int frame = 0; frame < pcm.getNumSamples(); ++frame)
                pcm.setSample(channel, frame, float(.2 * std::sin(frame * (.041 + channel * .007))));
        auto stream = source.createOutputStream();
        require(bool(stream), "slice varying source stream");
        stream->setPosition(0);
        stream->truncate();
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            format.createWriterFor(stream.release(), 48000., 2, 16, {}, 0));
        require(writer && writer->writeFromAudioSampleBuffer(pcm, 0, pcm.getNumSamples()),
                "slice varying long WAV");
    }
    Controller audio;
    CommandArgs add;
    add.text = source.getFullPathName();
    add.length = 45;
    require(audio.execute("audio.add", add).wasOk(), "audio slice source fixture");
    const auto id = audio.project.audioPlacements.front().id;
    CommandArgs trim;
    trim.target = id;
    trim.offset = 35.25;
    trim.length = 1.25;
    require(audio.execute("audio.trim", trim).wasOk(), "audio slice window fixture");
    AudioEngine engine(false);
    auto before = folder.getChildFile("slice-before.wav"), after = folder.getChildFile("slice-after.wav");
    require(engine.renderSync(audio.project, before).wasOk(), "unsliced audio PCM");
    cut.ids = {id};
    cut.beat = 1;
    const auto beforeSlice = audio.project.state();
    require(audio.execute("clip.slice", cut).wasOk(), "audio seconds/beat slice");
    require(audio.project.audioPlacements.size() == 2 &&
                audio.project.audioPlacements.front().duration == .5 &&
                audio.project.audioPlacements.back().offset == 35.75 &&
                audio.project.audioPlacements.back().duration == .75 &&
                audio.project.audioPlacements.front().channel == audio.project.audioPlacements.back().channel,
            "audio linked contiguous source split");
    require(engine.renderSync(audio.project, after).wasOk(), "sliced audio PCM");
    double rate = 0;
    auto a = readWav(before, rate), b = readWav(after, rate);
    require(a.getNumSamples() == b.getNumSamples(), "slice preserves rendered duration");
    float error = 0;
    for (int channel = 0; channel < 2; ++channel)
        for (int frame = 0; frame < a.getNumSamples(); ++frame)
            error = std::max(error, std::abs(a.getSample(channel, frame) - b.getSample(channel, frame)));
    require(error < .00001f, "slice source-window PCM remains continuous");
    audio.undoEdit();
    require(audio.project.state().isEquivalentTo(beforeSlice), "audio slice exact undo");
    Macro macro{"macro.slice-slip",
                "Slice and slip",
                {{"clip.slice", cut, true}, {"clip.slip", CommandArgs{}, true}}};
    macro.steps[1].parameters.beat = .5;
    auto config = Customization::familiarDefaults();
    config.macros = {macro};
    MacroContext context;
    context.clips = {id};
    require(config.runMacro(macro.id, audio, context).wasOk(), "slice/slip macro context");
    require(audio.project.audioPlacements.front().offset == 35.5 &&
                audio.project.audioPlacements.back().offset == 36.,
            "macro follows both split halves");
    audio.undoEdit();
    require(audio.project.state().isEquivalentTo(beforeSlice), "slice/slip macro one undo");
    const auto beforeMute = audio.project.state();
    CommandArgs mute;
    mute.ids = {id};
    mute.enabled = true;
    require(audio.execute("clip.mute", mute).wasOk(), "audio placement mute action");
    require(audio.project.audioPlacements.front().muted && !audio.project.channels.back().muted,
            "mute placement independently of Channel");
    const auto mutedWave = folder.getChildFile("placement-muted.wav");
    require(engine.renderSync(audio.project, mutedWave).wasOk(), "muted audio placement export");
    const auto mutedPcm = readWav(mutedWave, rate);
    require(mutedPcm.getMagnitude(0, mutedPcm.getNumSamples()) < .000001f, "audio placement actually silent");
    const auto mutedProject = folder.getChildFile("placement-muted.vibra");
    require(saveProject(audio.project, mutedProject).wasOk(), "schema4 muted project save");
    Project restored;
    require(loadProject(mutedProject, restored).wasOk() && restored.version == Project::currentVersion &&
                restored.audioPlacements.front().muted,
            "schema4 preserves mute on reopen");
    auto downgrade = audio.project.state();
    downgrade.setProperty("version", 3, nullptr);
    stripSamplerVoiceFieldsForLegacy(downgrade);
    require(Project::parse(downgrade, restored).failed(), "muted schema3 downgrade rejected");
    audio.undoEdit();
    require(audio.project.state().isEquivalentTo(beforeMute), "audio mute exact undo");
    auto legacy = audio.project.state();
    legacy.setProperty("version", 3, nullptr);
    stripSamplerVoiceFieldsForLegacy(legacy);
    for (auto channel : legacy.getChildWithName("CHANNELS"))
        channel.removeChild(channel.getChildWithName("SAMPLER"), nullptr);
    require(Project::parse(legacy, restored).wasOk() && restored.version == Project::currentVersion &&
                !restored.audioPlacements.front().muted,
            "schema3 migration with unmuted defaults");
    Controller music;
    auto song = Project::empty();
    song.patterns.front().length = 8;
    song.patterns.front().notes = {{song.allocate(), song.channels.front().id, 0, 8, 60, .8f, false}};
    song.placements = {{song.allocate(), song.patterns.front().id, 0, 0, 8, 0},
                       {song.allocate(), song.patterns.front().id, 1, 8, 8, 0}};
    music.replace(song);
    mute.ids = {song.placements.front().id};
    require(music.execute("clip.mute", mute).wasOk(), "pattern placement mute");
    auto events = sequence(music.project, false, song.patterns.front().id);
    require(events.size() == 1 && events.front().placement == song.placements.back().id &&
                sequence(music.project, true, song.patterns.front().id).size() == 1,
            "mute affects only one linked Song instance, Pattern playback unchanged");
    auto sample = makeOriginalSample("bass");
    VoiceBank voices;
    voices.prepare(sample, 48000);
    LiveSequencer sequencer;
    juce::AudioBuffer<float> block(2, 256);
    auto originalDoc = compileLiveDocument(song, false, song.patterns.front().id, 1);
    sequencer.process(*originalDoc, song.channels.front().id, 0, 256, 48000, true, voices, block, 0);
    require(block.getMagnitude(0, 256) > .01f, "pattern voice before clip mute");
    auto mutedDoc = compileLiveDocument(music.project, false, song.patterns.front().id, 2);
    for (int index = 1; index < 7; ++index)
    {
        block.clear();
        sequencer.process(*mutedDoc, song.channels.front().id, index * 256. / 48000., 256, 48000, true,
                          voices, block, 0);
    }
    require(block.getMagnitude(0, 256) < .000001f, "muting sounding Pattern releases without stuck notes");
    folder.getChildFile("playlist-mute-verification.txt")
        .replaceWithText(
            "PASS: independent Pattern/Audio placement mute, silent actual WAV, linked-instance/Pattern-mode "
            "isolation, sounding-note release, schema4 round trip, schema3 migration and downgrade "
            "rejection, exact undo. No native/hardware/listening qualification.\n");
    folder.getChildFile("playlist-slice-slip-verification.txt")
        .replaceWithText(
            "PASS: pattern/audio linked source split, exact undo/redo, source-bounded slip, stale IDs, macro "
            "split-half context/one undo and actual unsplit/split WAV continuity. No UI/native musical "
            "input, live DSP, hardware/listening or full truncated-note behavior qualification.\n");
}
