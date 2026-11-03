// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../src/NoteSlice.h"
#include "../src/MacroSliceInput.h"
#include "../src/MidiScore.h"
static void noteSliceTests(const juce::File &folder)
{
    Controller c;
    auto &p = c.project.patterns.front();
    const auto pattern = p.id;
    const auto channel = c.project.channels[0].id, ghost = c.project.channels[1].id;
    c.project.channels[0].source = "builtin:bass";
    c.project.channels[0].sampler.loop = true;
    p.notes = {{c.project.allocate(), channel, 0, 2, 60, .7f, true, false},
               {c.project.allocate(), channel, 0, 2, 64, .4f, false, true},
               {c.project.allocate(), ghost, 0, 2, 67, .6f, false, false}};
    c.project.placements = {{c.project.allocate(), pattern, 0, 0, 4, 0},
                            {c.project.allocate(), pattern, 1, 4, 4, 0}};
    const auto baseline = c.project.state();
    const auto original = p.notes;
    CommandArgs a;
    a.pattern = pattern;
    a.channel = channel;
    a.slice = NoteSliceLine{.5, 1.5, 59.5, 64.5, .25, false};
    std::vector<NoteSliceCut> plan;
    require(planNoteSlices(c.project, pattern, channel, {}, *a.slice, plan).wasOk() && plan.size() == 2,
            "Pure musical cut-line plan");
    require(c.project.state().isEquivalentTo(baseline) && plan[0].beat == .5 && plan[1].beat == 1.5,
            "Preview is read-only with independent snapped intersections");
    require(c.execute("notes.slice", a).wasOk(), "Canonical manual Slice");
    auto split = c.project.state();
    auto notes = c.project.pattern(pattern)->notes;
    require(notes.size() == 5 && notes[0].id == original[0].id && notes[1].id == original[1].id,
            "Left IDs retained; right halves appended");
    require(notes[0].length == .5 && notes[3].beat == .5 && notes[3].length == 1.5 &&
                notes[3].id != notes[0].id,
            "Exact left/right geometry and fresh identity");
    require(!notes[0].step && !notes[3].step && notes[3].velocity == original[0].velocity &&
                notes[3].pitch == 60,
            "Slice converts step to score; pitch and velocity retained");
    require(notes[1].muted && notes[4].muted && notes[2].id == original[2].id && notes[2].length == 2,
            "Muted source copied; unrelated ghost untouched");
    require(sequence(c.project, false, pattern).size() == 6,
            "Both linked placements follow split audible source");
    c.undoEdit();
    require(c.project.state().isEquivalentTo(baseline), "One exact Slice Undo");
    c.redoEdit();
    require(c.project.state().isEquivalentTo(split), "Exact Slice Redo");
    c.undoEdit();
    a.slice = NoteSliceLine{.75, 1.5, 60, 60, 0, false};
    require(c.execute("notes.slice", a).wasOk() && c.project.pattern(pattern)->notes.size() == 4 &&
                c.project.pattern(pattern)->notes.front().length == .75,
            "Horizontal/point row cut uses start beat");
    c.undoEdit();
    a.slice = NoteSliceLine{1.5, .5, 64.5, 59.5, 0, false};
    require(c.execute("notes.slice", a).wasOk() && c.project.pattern(pattern)->notes.size() == 5,
            "Reverse unsnapped diagonal line");
    c.undoEdit();
    for (double edge : {0., 2., minimumSliceLength * .5, 2 - minimumSliceLength * .5})
    {
        a.slice = NoteSliceLine{edge, edge, 59.5, 64.5, 0, false};
        auto revision = c.revision;
        require(c.execute("notes.slice", a).wasOk() && c.project.state().isEquivalentTo(baseline) &&
                    c.revision == revision,
                "Edge/outside/sub-tick cuts are inert without phantom history");
    }
    a.slice = NoteSliceLine{minimumSliceLength, minimumSliceLength, 59.5, 60.5, 0, false};
    require(c.execute("notes.slice", a).wasOk(), "One MIDI tick cut supported");
    const auto tiny = c.project.pattern(pattern)->notes.front();
    require(std::abs(tiny.length - minimumSliceLength) < 1e-12, "One-tick source length");
    CommandArgs move;
    move.pattern = pattern;
    move.channel = channel;
    move.ids = {tiny.id};
    move.beat = .25;
    move.pitch = 0;
    move.length = 0;
    require(c.execute("notes.move", move).wasOk() &&
                c.project.pattern(pattern)->notes.front().length == tiny.length,
            "Moving a fine Slice never expands duration");
    c.undoEdit();
    c.undoEdit();
    a.slice = NoteSliceLine{1, 1, -.5, 127.5, .25, true};
    a.ids = {original[0].id, 99999999};
    require(c.execute("notes.slice", a).failed() && c.project.state().isEquivalentTo(baseline),
            "Unknown targets fail atomically");
    a.ids = {original[0].id, original[0].id};
    require(c.execute("notes.slice", a).failed(), "Duplicate Slice targets rejected");
    a.ids.clear();
    a.slice->grid = .00001;
    require(c.execute("notes.slice", a).failed() && c.project.state().isEquivalentTo(baseline),
            "Sub-tick grid refused atomically");
    a.slice->grid = 0;
    a.slice->startBeat = std::numeric_limits<double>::infinity();
    require(c.execute("notes.slice", a).failed(), "Nonfinite Slice refused");
    a.slice = NoteSliceLine{1, 1, -.5, 127.5, .25, true};
    require(c.execute("notes.slice", a).wasOk() && c.project.pattern(pattern)->notes.size() == 6,
            "Explicit editable-ghost scope");
    Project restored;
    const auto file = folder.getChildFile("sliced-score.vibra");
    require(saveProject(c.project, file).wasOk() && loadProject(file, restored).wasOk() &&
                restored.state().isEquivalentTo(c.project.state()),
            "Real schema17 Slice round trip");
    juce::MemoryBlock bytes;
    require(writeMidiScore(restored.patterns.front(), channel, 120, 4, 4, bytes).wasOk(),
            "Real sliced MIDI export");
    juce::MemoryInputStream stream(bytes, false);
    juce::MidiFile midi;
    require(midi.readFrom(stream), "Actual sliced SMF decodes");
    int ons = 0, offs = 0;
    for (int i = 0; i < midi.getTrack(0)->getNumEvents(); ++i)
    {
        const auto &m = midi.getTrack(0)->getEventPointer(i)->message;
        ons += m.isNoteOn();
        offs += m.isNoteOff();
    }
    require(ons == 2 && offs == 2, "SMF retriggers both audible halves and omits muted halves");
    AudioEngine audio(false);
    auto wav = folder.getChildFile("sliced-score.wav");
    require(audio.renderSync(restored, wav).wasOk(), "Actual sliced WAV export");
    double rate = 0;
    const auto pcm = readWav(wav, rate);
    require(pcm.getNumSamples() > 0 && pcm.getMagnitude(0, pcm.getNumSamples()) > 0,
            "Sliced real PCM is audible data");
    Customization macros;
    CommandArgs line;
    a.ids.clear();
    line.slice = *a.slice;
    Macro m{"macro.slice-move", "Slice then move", {{"notes.slice", line, true}, {"notes.move", move, true}}};
    require(macros.addMacro(m).wasOk(), "Slice is a real user macro action");
    Macro whole{"macro.mute-part", "Mute whole part", {{"notes.mute", {}, true}}};
    require(macros.addMacro(whole).wasOk(), "Whole-part mute macro registered");
    const auto profile = macros.state();
    require(int(profile["version"]) == 3, "Slice geometry uses profile3");
    Customization parsed;
    require(parsed.importState(profile).wasOk() && parsed.macros[0].steps[0].parameters.slice.has_value(),
            "Exact Slice macro profile restore");
    auto bad = juce::JSON::parse(juce::JSON::toString(profile));
    bad.getDynamicObject()->setProperty("version", 2);
    require(parsed.importState(bad).failed(), "New Slice data cannot masquerade as old profile");
    Controller macroModel;
    macroModel.replace(Project::empty());
    auto &part = macroModel.project.patterns.front();
    auto target = macroModel.project.channels.front().id;
    part.notes = {{macroModel.project.allocate(), target, 0, 2, 60, .8f, false}};
    const auto before = macroModel.project.state();
    MacroContext context{part.id, target, 0, {part.notes.front().id}, {}};
    require(macros.runMacro(m.id, macroModel, context).wasOk() &&
                macroModel.project.pattern(context.pattern)->notes.size() == 2,
            "Real ordered Slice macro targets current part");
    macroModel.undoEdit();
    require(macroModel.project.state().isEquivalentTo(before), "Slice macro one exact Undo");
    MacroContext emptySelection{context.pattern, target, 0, {}, {}};
    require(macros.runMacro(whole.id, macroModel, emptySelection).wasOk() &&
                macroModel.project.pattern(context.pattern)->notes.front().muted,
            "Whole-part mute macro accepts empty selection like UI/canonical command");
    macroModel.undoEdit();
    require(macroModel.project.state().isEquivalentTo(before), "Whole-part macro one Undo");
    NoteSliceLine textLine;
    for (const auto *text : {".5", "0.5", "1e2"})
        require(parseMacroSliceBeat(text, textLine).wasOk() && textLine.grid == 0,
                "Macro Slice accepts finite musical text");
    const auto prior = juce::JSON::toString(sliceLineState(textLine), true);
    for (const auto *text : {"", "1junk", "false", "nan", "inf", "1e999", "-1", "1000001"})
        require(parseMacroSliceBeat(text, textLine).failed() &&
                    juce::JSON::toString(sliceLineState(textLine), true) == prior,
                "Invalid macro input fails without replacing prior line");
    Controller capped;
    auto &large = capped.project.patterns.front();
    const auto largeChannel = capped.project.channels.front().id;
    large.notes.reserve(100000);
    for (int i = 0; i < 100000; ++i)
        large.notes.push_back({capped.project.allocate(), largeChannel, 0, 1, i == 0 ? 60 : 67, .5f, false});
    CommandArgs limit;
    limit.pattern = large.id;
    limit.channel = largeChannel;
    limit.slice = NoteSliceLine{.5, .5, 59.5, 60.5, 0, false};
    const auto nextId = capped.project.nextId;
    require(capped.execute("notes.slice", limit).failed() && capped.project.nextId == nextId &&
                capped.project.pattern(limit.pattern)->notes.size() == 100000 &&
                capped.project.pattern(limit.pattern)->notes.front().length == 1,
            "Pattern limit fails atomically before source mutation or identity allocation");
    folder.getChildFile("note-slice-core.txt")
        .replaceWithText("PASS musical cut geometry/snapping/reverse/point/ghosts, stable "
                         "flags/IDs/selection context, one Undo/Redo, limits/errors, one-tick movement, "
                         "schema17 project/WAV/SMF and profile3 user macros. Listening/hardware separate.\n");
}
