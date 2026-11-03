// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "StepSequence.h"
#include <limits>
static void stepFillTests(const juce::File &folder)
{
    for (int interval : {2, 4, 8})
    {
        Controller c;
        CommandArgs a;
        a.pattern = c.project.patterns[0].id;
        a.channel = c.project.channels[0].id;
        const auto before = c.project.state();
        require(c.execute("steps.fill" + juce::String(interval), a).wasOk(), "Fill named interval");
        const auto &notes = c.project.pattern(a.pattern)->notes;
        require(notes.size() == size_t(16 / interval), "One-bar sixteenth-note fill count");
        for (size_t i = 0; i < notes.size(); ++i)
            require(notes[i].beat == double(i * interval) * .25 && notes[i].step && notes[i].length == .25 &&
                        notes[i].pitch == 60 && notes[i].velocity == .8f,
                    "Exact fill hits and normal Rack defaults");
        const auto filled = c.project.state();
        const auto revision = c.revision;
        require(c.execute("steps.fill" + juce::String(interval), a).wasOk() && c.revision == revision,
                "Repeated same fill is idempotent with no extra history");
        c.undoEdit();
        require(c.project.state().isEquivalentTo(before), "Fill exact one Undo including allocator");
        c.redoEdit();
        require(c.project.state().isEquivalentTo(filled), "Fill exact Redo");
    }
    Controller c;
    auto &part = c.project.patterns[0];
    part.length = 24;
    part.autoLength = false; // Outside-loop notes stay dormant beyond this 96-step window.
    const auto pattern = part.id, channel = c.project.channels[0].id, ghost = c.project.channels[1].id;
    const auto kept = c.project.allocate(), removed = c.project.allocate(), chord = c.project.allocate(),
               outside = c.project.allocate(), other = c.project.allocate();
    part.notes = {{kept, channel, .125, 2, 67, .45f, false},
                  {removed, channel, .25, .25, 62, .2f, true},
                  {chord, channel, .05, 1, 72, .6f, false},
                  {outside, channel, 24.5, 2, 50, .7f, false},
                  {other, ghost, .25, 2, 65, .3f, false}};
    c.project.placements = {{c.project.allocate(), pattern, 0, 0, 24, 0},
                            {c.project.allocate(), pattern, 1, 24, 24, 0}};
    require(stepSequenceIsMelodic(part, channel), "In-window melodic notes select Piano Roll strip");
    const auto before = c.project.state();
    CommandArgs a;
    a.pattern = pattern;
    a.channel = channel;
    a.interval = 4;
    require(c.execute("steps.fill", a).wasOk(), "Generic fill converts melodic part");
    const auto filled = c.project.state();
    const auto &notes = c.project.pattern(pattern)->notes;
    require(stepSequenceSize(*c.project.pattern(pattern)) == 96 && notes.size() == 27,
            "Fill spans beyond64 steps without truncating explicit Pattern");
    require(
        notes[0].id == kept && notes[0].beat == 0 && notes[0].length == .25 && notes[0].pitch == 67 &&
            notes[0].velocity == .45f && notes[0].step && notes[1].id == chord && notes[1].pitch == 72 &&
            notes[1].velocity == .6f && notes[1].step,
        "Surviving event IDs/chord/pitch/velocity retained; conversion floor-aligns and sets step duration");
    require(notes[2].id == outside && notes[2].beat == 24.5 && notes[2].length == 2 && !notes[2].step &&
                notes[3].id == other && notes[3].channel == ghost && notes[3].length == 2 && !notes[3].step,
            "Outside-loop and other Channel notes untouched");
    require(!stepSequenceIsMelodic(*c.project.pattern(pattern), channel) &&
                stepSequenceIsMelodic(*c.project.pattern(pattern), ghost),
            "Outside-loop melodic notes retain data without hiding the filled step grid");
    require(notes.back().beat == 23 && c.project.placements[0].pattern == pattern &&
                c.project.placements[1].pattern == pattern &&
                sequence(c.project, false, pattern).size() == (notes.size() - 1) * 2,
            "Last fill hits and linked arrangement render events update without Make Unique");
    a.beat = 0;
    a.enabled = false;
    require(c.execute("steps.set", a).wasOk() && c.project.pattern(pattern)->notes.size() == 25,
            "Deactivating a filled chord step removes all its events, not one hidden survivor");
    c.undoEdit();
    require(c.project.state().isEquivalentTo(filled), "Chord step off exact Undo");
    Project loaded;
    const auto file = folder.getChildFile("filled-linked-pattern.vibra");
    require(saveProject(c.project, file).wasOk() && loadProject(file, loaded).wasOk() &&
                loaded.state().isEquivalentTo(filled),
            "Filled score exact persistence including links and outside notes");
    for (int interval : {0, 1, 3, 16})
    {
        a.interval = interval;
        require(c.execute("steps.fill", a).failed() && c.project.state().isEquivalentTo(filled),
                "Invalid interval fails atomically");
    }
    a.interval = 2;
    a.ids = {channel, 999999};
    require(c.execute("steps.fill", a).failed() && c.project.state().isEquivalentTo(filled),
            "Stale bulk fill refuses all Channels");
    a.ids = {channel, channel};
    require(c.execute("steps.fill", a).failed(), "Duplicate bulk Channel IDs rejected");
    Customization custom = Customization::familiarDefaults();
    require(custom.addMacro({"macro.fill", "Fill drums", {{"steps.fill8", {}, true}}}).wasOk(),
            "Fill macro registration");
    MacroContext context;
    context.pattern = pattern;
    context.channel = channel;
    context.channels = {channel, ghost};
    context.notes = {kept, removed};
    require(custom.runMacro("macro.fill", c, context).wasOk() && context.notes == std::vector<Id>{kept},
            "Fill macro uses shared bulk selection and prunes dead note targets");
    require(std::count_if(c.project.pattern(pattern)->notes.begin(), c.project.pattern(pattern)->notes.end(),
                          [&](const auto &n) { return n.channel == ghost; }) == 12,
            "Bulk macro fills hidden/current other Channel too");
    c.undoEdit();
    require(c.project.state().isEquivalentTo(filled), "Bulk fill macro exact one Undo");
    c.undoEdit();
    require(c.project.state().isEquivalentTo(before), "Original conversion exact Undo");
    Customization profiles;
    require(profiles.addMacro({"macro.generic-fill", "Default fill", {{"steps.fill", {}, true}}}).wasOk(),
            "Default generic fill macro");
    const auto profileFile = folder.getChildFile("fill-shortcuts.json");
    require(profiles.save(profileFile).wasOk(), "Fill profile saved");
    Customization reopened;
    require(reopened.load(profileFile).wasOk() && reopened.macros[0].steps[0].parameters.interval == 4,
            "Typed fill interval survives macro profile round trip");
    auto legacy = profiles.state();
    legacy["macros"]
        .getArray()
        ->getReference(0)["steps"]
        .getArray()
        ->getReference(0)["parameters"]
        .getDynamicObject()
        ->removeProperty("interval");
    require(reopened.importState(legacy).wasOk() && reopened.macros[0].steps[0].parameters.interval == 4,
            "Legacy omitted interval migrates to the same canonical default4");
    auto invalid = profiles.state();
    invalid["macros"]
        .getArray()
        ->getReference(0)["steps"]
        .getArray()
        ->getReference(0)["parameters"]
        .getDynamicObject()
        ->setProperty("interval", 2.5);
    const auto validProfile = juce::JSON::toString(reopened.state());
    require(reopened.importState(invalid).failed() && juce::JSON::toString(reopened.state()) == validProfile,
            "Fractional macro interval refused without changing preferences");
    Controller defaultFill;
    MacroContext defaultContext;
    defaultContext.pattern = defaultFill.project.patterns[0].id;
    defaultContext.channel = defaultFill.project.channels[0].id;
    require(reopened.runMacro("macro.generic-fill", defaultFill, defaultContext).wasOk() &&
                defaultFill.project.patterns[0].notes.size() == 4,
            "Generic macro shares default4 behavior with UI/MCP");
    auto huge = c.project;
    huge.pattern(pattern)->length = 1000000;
    c.replace(huge);
    a.ids = {channel};
    a.interval = 8;
    const auto limited = c.project.state();
    require(c.execute("steps.fill", a).failed() && c.project.state().isEquivalentTo(limited),
            "Large fill refuses before exceeding note budget, no silent64-step cap");
    auto budget = Project::empty();
    budget.patterns[0].length = 200000;
    budget.patterns[0].autoLength = false; // Deliberate fixed fill-capacity boundary.
    budget.patterns[0].notes = {{budget.allocate(), budget.channels[1].id, 0, .25, 60, .5f, false}};
    c.replace(budget);
    a.pattern = c.project.patterns[0].id;
    a.channel = c.project.channels[0].id;
    a.ids.clear();
    const auto budgetBefore = c.project.state();
    require(c.execute("steps.fill", a).failed() && c.project.state().isEquivalentTo(budgetBefore),
            "Aggregate budget includes unrelated notes and refuses before allocating event IDs");
    auto clipped = Project::empty();
    Channel clip;
    clip.id = clipped.allocate();
    clip.name = "Missing owned clip fixture";
    clip.source = "E:/VibeRa-owned-missing-test.wav";
    clip.audioClip = true;
    clip.audioDuration = 1;
    clipped.channels.push_back(clip);
    c.replace(clipped);
    a.pattern = c.project.patterns[0].id;
    a.ids = {c.project.channels[0].id, clip.id};
    const auto clipBefore = c.project.state();
    require(c.execute("steps.fill", a).failed() && c.project.state().isEquivalentTo(clipBefore),
            "Mixed playable/Audio Clip bulk fill fails atomically instead of skipping a target");
    auto partial = Project::empty();
    partial.patterns[0].length = 1.125;
    partial.patterns[0].autoLength = false; // Last partial step must not extend the source window.
    c.replace(partial);
    a = {};
    a.pattern = c.project.patterns[0].id;
    a.channel = c.project.channels[0].id;
    a.interval = 2;
    require(c.execute("steps.fill", a).wasOk() && c.project.pattern(a.pattern)->notes.size() == 3 &&
                c.project.pattern(a.pattern)->notes.back().beat == 1 &&
                c.project.pattern(a.pattern)->length == 1.125,
            "Partial last step included without extending explicit Pattern length");
}
