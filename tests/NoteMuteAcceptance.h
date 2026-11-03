// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../src/MidiScore.h"
static void noteMuteTests(const juce::File &folder)
{
    Controller c;
    auto &pattern = c.project.patterns.front();
    const auto patternId = pattern.id;
    const auto first = c.project.channels[0].id, ghost = c.project.channels[1].id;
    c.project.channels[0].source = "builtin:bass";
    c.project.channels[0].sampler.loop = true;
    pattern.notes = {{c.project.allocate(), first, 0, 1, 60, .7f, false},
                     {c.project.allocate(), first, 1, .25, 64, .3f, true},
                     {c.project.allocate(), ghost, 0, 1, 48, .6f, false}};
    c.project.placements = {{c.project.allocate(), patternId, 0, 0, 4, 0},
                            {c.project.allocate(), patternId, 1, 4, 4, 0}};
    const auto initial = c.project.state();
    const auto ids = std::vector<Id>{pattern.notes[0].id, pattern.notes[1].id, pattern.notes[2].id};
    CommandArgs args;
    args.pattern = patternId;
    args.channel = first;
    args.ids = {ids[0]};
    require(c.execute("notes.mute", args).wasOk(), "Canonical Note mute");
    const auto muted = c.project.state();
    const auto revision = c.revision;
    require(c.project.pattern(patternId)->notes[0].muted && !c.project.pattern(patternId)->notes[1].muted &&
                c.project.pattern(patternId)->notes[0].velocity == .7f,
            "Mute preserves velocity/other notes");
    require(c.execute("notes.mute", args).wasOk() && c.revision == revision, "Mute is idempotent");
    c.undoEdit();
    require(c.project.state().isEquivalentTo(initial), "One exact mute Undo");
    c.redoEdit();
    require(c.project.state().isEquivalentTo(muted), "Exact mute Redo");
    auto sounding = sequence(c.project, false, patternId);
    require(sounding.size() == 4 &&
                std::none_of(sounding.begin(), sounding.end(), [&](auto &n) { return n.note == ids[0]; }),
            "Both linked Pattern placements omit the muted source note");
    args.ids = {ids[0], ids[2]};
    require(c.execute("notes.unmute", args).wasOk() && c.project.state().isEquivalentTo(initial),
            "Explicit cross-Channel selection unmute");
    args.ids.clear();
    require(c.execute("notes.mute", args).wasOk() && c.project.pattern(patternId)->notes[0].muted &&
                c.project.pattern(patternId)->notes[1].muted && !c.project.pattern(patternId)->notes[2].muted,
            "Empty selection applies only current Channel part including steps");
    auto before = c.project.state();
    const auto rev = c.revision;
    args.ids = {ids[0], 99999999};
    require(c.execute("notes.mute", args).failed() && c.project.state().isEquivalentTo(before) &&
                c.revision == rev,
            "Invalid mixed target is atomic");
    args.ids = {ids[0]};
    args.beat = 2;
    require(c.execute("notes.duplicate", args).wasOk() && c.project.pattern(patternId)->notes.back().muted,
            "Duplicate preserves mute state with fresh ID");
    c.undoEdit();
    require(c.beginGesture(), "Mute gesture begins");
    args.ids = {ids[2]};
    require(c.execute("notes.mute", args).wasOk(), "Mute gesture edit");
    c.cancelGesture();
    require(c.project.state().isEquivalentTo(before),
            "Mute gesture cancel restores exact flags/source/history");
    Project restored;
    const auto file = folder.getChildFile("note-mute.vibra");
    require(saveProject(c.project, file).wasOk() && loadProject(file, restored).wasOk() &&
                restored.state().isEquivalentTo(before),
            "Actual schema17 Note mute project round trip");
    auto old = before.createCopy();
    old.setProperty("version", 16, nullptr);
    stripNoteMuteForLegacy(old);
    require(Project::parse(old, restored).wasOk() &&
                std::none_of(restored.pattern(patternId)->notes.begin(),
                             restored.pattern(patternId)->notes.end(), [](auto &n) { return n.muted; }),
            "Genuine schema16 migration keeps former audible notes audible");
    const auto unchanged = restored.state();
    for (const auto bad : {juce::var{}, juce::var(2), juce::var("true"), juce::var(.5)})
    {
        auto tree = before.createCopy();
        auto note = tree.getChildWithName("PATTERNS").getChild(0).getChild(0);
        if (bad.isVoid())
            note.removeProperty("muted", nullptr);
        else
            note.setProperty("muted", bad, nullptr);
        require(Project::parse(tree, restored).failed() && restored.state().isEquivalentTo(unchanged),
                "Missing/invalid Note mute flag preserves destination");
    }
    auto masquerade = before.createCopy();
    masquerade.setProperty("version", 16, nullptr);
    require(Project::parse(masquerade, restored).failed(), "Mute state cannot masquerade as old schema");
    juce::MemoryBlock bytes;
    require(writeMidiScore(*c.project.pattern(patternId), first, 120, 4, 4, bytes).wasOk(),
            "All-muted part exports valid silent SMF");
    juce::MemoryInputStream input(bytes, false);
    juce::MidiFile midi;
    require(midi.readFrom(input) && midi.getNumTracks() == 1, "Actual silent SMF decodes");
    require(midi.getTrack(0)->getNumEvents() == 4,
            "Silent SMF preserves metadata/end with no playable notes");
    args.ids = {ids[2]};
    require(c.execute("notes.mute", args).wasOk(), "Mute last audible part");
    require(sequence(c.project, true, patternId).empty() && sequence(c.project, false, patternId).empty(),
            "All-muted Pattern/Song scheduling is silent with retained source data");
    AudioEngine audio(false);
    const auto silent = folder.getChildFile("muted-score.wav");
    require(audio.renderSync(c.project, silent).wasOk(), "Real all-muted stereo WAV export");
    double rate = 0;
    const auto pcm = readWav(silent, rate);
    require(pcm.getNumSamples() > 0 && pcm.getMagnitude(0, pcm.getNumSamples()) == 0,
            "Real all-muted PCM is silence, with nonzero arrangement duration");
    c.undoEdit();
    args.ids.clear();
    require(c.execute("notes.unmute", args).wasOk(), "Restore active part");
    const auto active = folder.getChildFile("unmuted-score.wav");
    require(audio.renderSync(c.project, active).wasOk(), "Restored real WAV");
    const auto played = readWav(active, rate);
    require(played.getMagnitude(0, played.getNumSamples()) > 0, "Unmuted score produces actual PCM");
    Controller macroModel;
    macroModel.replace(c.project);
    Customization settings;
    MacroContext context{patternId, first, 0, {ids[0]}, {}};
    CommandArgs move;
    move.beat = .25;
    move.length = 0;
    move.pitch = 0;
    Macro macro{"macro.mute-move", "Mute and move", {{"notes.mute", {}, true}, {"notes.move", move, true}}};
    const auto baseline = macroModel.project.state();
    require(settings.addMacro(macro).wasOk() && settings.runMacro(macro.id, macroModel, context).wasOk() &&
                macroModel.project.pattern(patternId)->notes[0].muted,
            "Real mute/move user macro uses shared selection/order");
    macroModel.undoEdit();
    require(macroModel.project.state().isEquivalentTo(baseline), "Mute macro one exact Undo");
    Controller glue;
    glue.project.patterns.front().notes = {
        {glue.project.allocate(), glue.project.channels[0].id, 0, 1, 60, .8f, false, false},
        {glue.project.allocate(), glue.project.channels[0].id, 0, 1, 60, .8f, false, true}};
    CommandArgs combine;
    combine.pattern = glue.project.patterns.front().id;
    combine.channel = glue.project.channels.front().id;
    require(glue.execute("notes.glue", combine).wasOk() && glue.project.patterns.front().notes.size() == 2,
            "Glue never collapses audible and muted source notes into one event");
    folder.getChildFile("note-mute-core.txt")
        .replaceWithText("PASS source-preserving mute, linked Pattern playback, exact history/macro/cancel, "
                         "schema17/legacy/invalid files, real MIDI/WAV, duplicate/Glue policies. "
                         "Listening/hardware separate.\n");
}
