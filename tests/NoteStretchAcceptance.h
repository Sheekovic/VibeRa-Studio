// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../src/NoteStretch.h"
#include "../src/MacroStretchInput.h"
static void noteStretchTests(const juce::File &folder)
{
    Controller c;
    auto &p = c.project.patterns.front();
    const auto pattern = p.id, channel = c.project.channels[0].id, ghost = c.project.channels[1].id;
    p.notes = {{c.project.allocate(), channel, 1, 1, 60, .7f, true, false},
               {c.project.allocate(), channel, 2, .5, 64, .4f, false, true},
               {c.project.allocate(), ghost, 0, .5, 67, .6f, false, false}};
    c.project.placements = {{c.project.allocate(), pattern, 0, 0, 4, 0},
                            {c.project.allocate(), pattern, 1, 4, 4, 0}};
    const auto baseline = c.project.state();
    const auto original = p.notes;
    CommandArgs a;
    a.pattern = pattern;
    a.channel = channel;
    a.ids = {p.notes[0].id, p.notes[1].id};
    a.beat = 1;
    a.value = 2;
    NoteStretchPlan plan;
    require(planNoteStretch(c.project, pattern, channel, a.ids, 1, 2, plan).wasOk(),
            "Pure proportional stretch plan");
    require(c.project.state().isEquivalentTo(baseline) && plan.notes.size() == 2,
            "Stretch planning never mutates source");
    require(plan.notes[0].beat == 1 && plan.notes[0].length == 2 && plan.notes[1].beat == 3 &&
                plan.notes[1].length == 1,
            "Anchor-relative onsets and durations");
    require(plan.minimumRatio > 0 && plan.maximumRatio >= 2, "Source-dependent ratio limits read back");
    require(c.execute("notes.stretch", a).wasOk(), "Canonical selected-note stretch");
    const auto stretched = c.project.state();
    const auto notes = c.project.pattern(pattern)->notes;
    require(notes[0].id == original[0].id && notes[1].id == original[1].id &&
                notes[2].beat == original[2].beat,
            "Stable IDs/unrelated ghost");
    require(notes[1].muted && notes[0].pitch == 60 && notes[0].velocity == .7f && !notes[0].step,
            "Properties retained; stretched steps become score");
    require(sequence(c.project, false, pattern).size() == 4,
            "Linked placements follow stretched audible score");
    c.undoEdit();
    require(c.project.state().isEquivalentTo(baseline), "One exact stretch Undo");
    c.redoEdit();
    require(c.project.state().isEquivalentTo(stretched), "Exact stretch Redo");
    c.undoEdit();
    a.value = 1;
    require(c.execute("notes.stretch", a).wasOk() && c.project.state().isEquivalentTo(baseline),
            "Ratio1 is exact no-op including step flags");
    for (double bad : {0., -1., 1.e-8, 1000001., std::numeric_limits<double>::infinity(),
                       std::numeric_limits<double>::quiet_NaN()})
    {
        a.value = bad;
        require(c.execute("notes.stretch", a).failed() && c.project.state().isEquivalentTo(baseline),
                "Atomic invalid stretch ratio");
    }
    a.value = 2;
    a.beat = 1.5;
    require(c.execute("notes.stretch", a).failed(), "Anchor cannot follow selected onset");
    a.beat = 1;
    a.ids = {original[0].id, original[0].id};
    require(c.execute("notes.stretch", a).failed(), "Duplicate targets rejected");
    a.ids = {999999};
    require(c.execute("notes.stretch", a).failed(), "Missing target rejected");
    a.ids.clear();
    require(c.execute("notes.stretch", a).failed(), "Empty target cannot silently stretch whole part");
    a.ids = {original[0].id, original[2].id};
    a.beat = 0;
    require(c.execute("notes.stretch", a).wasOk() && c.project.pattern(pattern)->notes[2].length == 1,
            "Explicit ghost targets supported");
    c.undoEdit();
    a.ids = {original[0].id, original[1].id};
    a.beat = 1;
    a.value = 4;
    require(c.execute("notes.stretch", a).wasOk() && c.project.pattern(pattern)->length == 8,
            "Pattern grows by bars for scaled notes");
    require(c.project.placements[0].length == 4 && c.project.placements[1].length == 4,
            "Placement extents never silently stretched");
    auto stored = c.project.state();
    Project reopened;
    require(Project::parse(stored, reopened).wasOk() && reopened.state().isEquivalentTo(stored),
            "Stretch project17 round trip");
    c.undoEdit();
    a.value = .5;
    require(c.execute("notes.stretch", a).wasOk() && c.project.pattern(pattern)->notes[1].beat == 1.5,
            "Compression preserves fixed anchor");
    c.undoEdit();
    Customization custom = Customization::familiarDefaults();
    Macro macro;
    macro.id = "macro.stretch";
    macro.name = "Double selected phrase";
    a.value = 2;
    a.beat = 999;
    macro.steps.push_back({"notes.stretch", a, true});
    require(custom.addMacro(macro).wasOk(), "Stretch macro accepted");
    MacroContext context;
    context.pattern = pattern;
    context.channel = channel;
    context.notes = a.ids;
    require(custom.runMacro(macro.id, c, context).wasOk() && c.project.pattern(pattern)->notes[1].beat == 3,
            "Current-context macro derives current earliest anchor");
    c.undoEdit();
    require(c.project.state().isEquivalentTo(baseline), "Stretch macro one Undo");
    Controller shortLoop;
    const auto shortPattern = shortLoop.project.patterns.front().id,
               shortChannel = shortLoop.project.channels.front().id;
    shortLoop.project.patterns.front().length = 1;
    shortLoop.project.patterns.front().autoLength = false; // Intentionally shortened source window.
    shortLoop.project.patterns.front().notes = {
        {shortLoop.project.allocate(), shortChannel, .5, 2, 60, .7f, false, false}};
    const auto shortSource = shortLoop.project.state();
    CommandArgs noop;
    noop.pattern = shortPattern;
    noop.channel = shortChannel;
    noop.ids = {shortLoop.project.patterns.front().notes.front().id};
    noop.beat = .5;
    noop.value = 1;
    require(shortLoop.execute("notes.stretch", noop).wasOk() &&
                shortLoop.project.state().isEquivalentTo(shortSource),
            "Ratio1 preserves intentionally shortened Pattern loop without history");
    double parsedRatio = 7;
    for (auto invalid : {"1junk", "0", "-1", "nan", "inf", "1e999", ""})
        require(parseMacroStretchRatio(invalid, parsedRatio).failed() && parsedRatio == 7,
                "Invalid ratio text is atomic and complete");
    require(parseMacroStretchRatio(".5", parsedRatio).wasOk() && parsedRatio == .5,
            "Strict valid ratio text");
    const auto saved = folder.getChildFile("stretched-score.vibra");
    Project loaded;
    require(saveProject(c.project, saved).wasOk() && loadProject(saved, loaded).wasOk() &&
                loaded.state().isEquivalentTo(c.project.state()),
            "Actual stretch file save/reopen");
    juce::MemoryBlock midi;
    require(writeMidiScore(*loaded.pattern(pattern), channel, loaded.tempo, loaded.numerator,
                           loaded.denominator, midi)
                    .wasOk() &&
                midi.getSize() > 50,
            "Actual stretched SMF source export");
    AudioEngine render(false);
    const auto wav = folder.getChildFile("stretched-score.wav");
    require(render.renderSync(loaded, wav).wasOk(), "Actual stretched WAV export");
    double sampleRate = 0;
    const auto pcm = readWav(wav, sampleRate);
    require(pcm.getNumSamples() > 0 && pcm.getMagnitude(0, pcm.getNumSamples()) > 0,
            "Stretched WAV contains musical PCM");
    folder.getChildFile("note-stretch-core.txt")
        .replaceWithText("PASS pure anchored stretch, stable properties/IDs, linked sources, exact "
                         "history/no-op, typed atomic errors/limits, Pattern growth, project17 round trip "
                         "and current-context macros. UI/live/render/native separate.\n");
}
