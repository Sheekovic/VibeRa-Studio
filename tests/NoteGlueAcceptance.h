// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
static void noteGlueTests(const juce::File &folder)
{
    Controller c;
    const auto pattern = c.project.patterns[0].id;
    const auto channel = c.project.channels[0].id, ghost = c.project.channels[1].id;
    auto &p = c.project.patterns[0];
    const auto first = c.project.allocate(), middle = c.project.allocate(), last = c.project.allocate();
    const auto overlap = c.project.allocate(), nested = c.project.allocate(), gap = c.project.allocate();
    const auto ghostFirst = c.project.allocate(), ghostLast = c.project.allocate();
    p.notes = {{last, channel, .75, .125, 60, .9f, false},  {first, channel, 0, .25, 60, .4f, false},
               {middle, channel, .25, .5, 60, .8f, false},  {overlap, channel, 0, .75, 64, .6f, false},
               {nested, channel, .5, .75, 64, .2f, true},   {gap, channel, .87501, .125, 60, .5f, false},
               {ghostFirst, ghost, 0, .25, 60, .3f, false}, {ghostLast, ghost, .25, .5, 60, .7f, false}};
    c.project.placements = {{c.project.allocate(), pattern, 0, 0, 4, 0},
                            {c.project.allocate(), pattern, 1, 4, 4, 0}};
    const auto original = c.project.state();
    CommandArgs a;
    a.pattern = pattern;
    a.channel = channel;
    require(c.execute("notes.glue", a).wasOk(), "Glue all current Channel part");
    auto notes = c.project.pattern(pattern)->notes;
    require(notes.size() == 5 && notes[0].id == first && notes[0].beat == 0 && notes[0].length == .875 &&
                notes[0].velocity == .4f && notes[1].id == overlap && notes[1].length == 1.25 &&
                notes[1].velocity == .6f && notes[2].id == gap && notes[3].id == ghostFirst &&
                notes[3].length == .25 && notes[4].id == ghostLast,
            "Glue unsorted chain/overlap union, earliest ID/velocity, real gap and ghost isolation");
    require(sequence(c.project, false, pattern).size() == 10 && c.project.placements[0].pattern == pattern &&
                c.project.placements[1].pattern == pattern,
            "Glue updates both linked Playlist placements without making unique");
    const auto glued = c.project.state();
    c.undoEdit();
    require(c.project.state().isEquivalentTo(original), "Glue exact one Undo restores IDs and ordering");
    c.redoEdit();
    require(c.project.state().isEquivalentTo(glued), "Glue exact Redo");
    const auto revision = c.revision;
    require(c.execute("notes.glue", a).wasOk() && c.revision == revision &&
                c.project.state().isEquivalentTo(glued),
            "Glue no-op neither changes revision nor adds Undo");
    c.undoEdit();
    require(c.project.state().isEquivalentTo(original), "No-op Glue preserves preceding history");
    a.ids = {first, last};
    require(c.execute("notes.glue", a).wasOk() && c.project.state().isEquivalentTo(original),
            "Glue does not bridge an unselected note/gap");
    a.ids = {ghostFirst, ghostLast};
    require(c.execute("notes.glue", a).wasOk() && c.project.pattern(pattern)->notes.size() == 7 &&
                c.project.pattern(pattern)->notes[1].length == .25 &&
                c.project.pattern(pattern)->notes.back().id == ghostFirst &&
                c.project.pattern(pattern)->notes.back().length == .75,
            "Explicit editable ghost IDs target their own Channel");
    c.undoEdit();
    a.ids = {first, middle, last, ghostFirst, ghostLast};
    require(c.execute("notes.glue", a).wasOk() && c.project.pattern(pattern)->notes.size() == 5,
            "Explicit multi-Channel selection is independent per part");
    const auto selectedGlued = c.project.state();
    a.ids = {first, middle};
    require(c.execute("notes.glue", a).failed() && c.project.state().isEquivalentTo(selectedGlued),
            "Deleted/stale Glue identity refuses atomically");
    a.ids.clear();
    a.channel = 999999;
    require(c.execute("notes.glue", a).failed() && c.project.state().isEquivalentTo(selectedGlued),
            "Whole-part Glue requires valid playable Channel");
    const auto file = folder.getChildFile("glued-linked-pattern.vibra");
    require(saveProject(c.project, file).wasOk(), "Glue project save");
    Project reopened;
    require(loadProject(file, reopened).wasOk() && reopened.state().isEquivalentTo(selectedGlued),
            "Glue exact project round trip with linked placements");

    // Decimal chop/glue round trip tests numerical tolerance without changing
    // the project's beat resolution or filling a genuine small gap.
    Controller phrase;
    auto &part = phrase.project.patterns[0];
    const auto owner = phrase.project.channels[0].id;
    part.notes = {{phrase.project.allocate(), owner, .1, .7, 60, .6f, false}};
    const auto originalId = part.notes[0].id;
    CommandArgs chop;
    chop.pattern = part.id;
    chop.channel = owner;
    chop.length = .1;
    require(phrase.execute("notes.chop", chop).wasOk() && phrase.project.patterns[0].notes.size() == 7,
            "Decimal phrase quick chop");
    require(phrase.execute("notes.glue", chop).wasOk() && phrase.project.patterns[0].notes.size() == 1 &&
                phrase.project.patterns[0].notes[0].id == originalId &&
                std::abs(phrase.project.patterns[0].notes[0].length - .7) < 1.e-14,
            "Glue rejoins decimal chopped tails without timing drift");

    // A sampled occupancy oracle is independent of the implementation's sweep:
    // same-key covered cells must be exactly conserved for touching/overlap unions.
    juce::Random random(47003);
    for (int trial = 0; trial < 30; ++trial)
    {
        auto project = Project::empty();
        auto &patternData = project.patterns[0];
        const auto ch = project.channels[0].id;
        std::array<std::array<bool, 160>, 3> before{}, after{};
        for (int i = 0; i < 40; ++i)
        {
            const int start = random.nextInt(120), length = 1 + random.nextInt(20), pitch = random.nextInt(3);
            patternData.notes.push_back(
                {project.allocate(), ch, start / 16., length / 16., 60 + pitch, .5f, false});
            for (int tick = start; tick < start + length; ++tick)
                before[size_t(pitch)][size_t(tick)] = true;
        }
        c.replace(project);
        a = {};
        a.pattern = patternData.id;
        a.channel = ch;
        require(c.execute("notes.glue", a).wasOk(), "Random Glue union");
        for (const auto &note : c.project.patterns[0].notes)
            for (int tick = int(note.beat * 16); tick < int((note.beat + note.length) * 16); ++tick)
                after[size_t(note.pitch - 60)][size_t(tick)] = true;
        require(before == after && c.project.patterns[0].notes.size() <= 40,
                "Glue preserves independently sampled pitch/time coverage");
    }

    // Macro context must discard removed IDs before following document commands.
    Customization settings = Customization::familiarDefaults();
    Controller macro;
    auto &macroPart = macro.project.patterns[0];
    const auto macroChannel = macro.project.channels[0].id;
    macroPart.notes = {{macro.project.allocate(), macroChannel, 0, .25, 60, .4f, false},
                       {macro.project.allocate(), macroChannel, .25, .5, 60, .8f, false}};
    MacroContext context{macroPart.id, macroChannel, 0, {macroPart.notes[0].id, macroPart.notes[1].id}, {}};
    CommandArgs velocity;
    velocity.value = .5;
    require(settings
                .addMacro({"macro.glue",
                           "Join and level",
                           {{"notes.glue", {}, true}, {"notes.velocity", velocity, true}}})
                .wasOk(),
            "Glue macro admitted");
    const auto macroBefore = macro.project.state();
    require(settings.runMacro("macro.glue", macro, context).wasOk() && context.notes.size() == 1 &&
                macro.project.patterns[0].notes[0].velocity == .5f,
            "Glue macro follows surviving note ID");
    macro.undoEdit();
    require(macro.project.state().isEquivalentTo(macroBefore), "Glue macro one exact Undo");
    require(settings.resolve(juce::KeyPress('G', juce::ModifierKeys::ctrlModifier, 0), Scope::piano) ==
                "notes.glue",
            "Documented Piano Ctrl+G default");
}
