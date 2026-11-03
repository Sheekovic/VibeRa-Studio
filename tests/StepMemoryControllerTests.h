// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Commands.h"
#include "Customization.h"
#include "PatternChannelLoops.h"

// Real Controller/history integration draft. Not covered by the standalone
// structural probe; register/run with the existing acceptance harness later.
static void stepMemoryControllerTests()
{
    using namespace vibera;
    auto project = Project::empty();
    auto &part = project.patterns.front();
    part.autoLength = false;
    part.length = 4;
    const auto channel = project.channels[0].id, other = project.channels[1].id, pattern = part.id;
    part.notes = {{project.allocate(), channel, 0, .5, 48, .3f, true},
                  {project.allocate(), channel, .25, .75, 55, .4f, true},
                  {project.allocate(), channel, .5, .25, 67, .5f, true, true},
                  {project.allocate(), channel, .5, 1, 72, 0, true},
                  {project.allocate(), other, 0, 2, 40, .7f, false},
                  {project.allocate(), channel, 5, .25, 90, .8f, true}};
    const auto baseline = project.state();
    Controller c;
    c.replace(project);
    CommandArgs set;
    set.pattern = pattern;
    set.channel = channel;
    set.beat = .5;
    set.enabled = false;
    set.retainSteps = true;
    require(c.execute("steps.set", set).wasOk(), "Canonical step deactivation commits retained chord");
    auto expected = project;
    expected.pattern(pattern)->notes[3].muted = true;
    require(c.project.state().isEquivalentTo(expected.state()),
            "Only active chord member mute changes, preserving mixed chord IDs/durations/zero velocity");
    const auto retained = c.project.state();
    const auto revision = c.revision;
    const auto undo = c.undo.getUndoDescriptions();
    require(c.execute("steps.set", set).wasOk() && c.project.state().isEquivalentTo(retained) &&
                c.revision == revision && c.undo.getUndoDescriptions() == undo,
            "Repeated retained erasure is exact canonical no-op");
    require(c.undoEditChecked().wasOk() && c.project.state().isEquivalentTo(baseline) && !c.undo.canUndo() &&
                c.redoEditChecked().wasOk() && c.project.state().isEquivalentTo(retained) &&
                !c.undo.canRedo(),
            "Retained chord operation has one exact Undo/Redo");
    set.enabled = true;
    expected.pattern(pattern)->notes[2].muted = false;
    expected.pattern(pattern)->notes[3].muted = false;
    require(c.execute("steps.set", set).wasOk() && c.project.state().isEquivalentTo(expected.state()),
            "Enabling restores every same-cell member without allocator consumption or velocity replacement");

    c.replace(project);
    set.enabled = false;
    require(c.beginGesture("retained-step-test") && c.execute("steps.set", set, "retained-step-test").wasOk(),
            "Actual owned gesture previews retained activation");
    require(c.cancelGestureChecked("retained-step-test").wasOk() &&
                c.project.state().isEquivalentTo(baseline) && !c.undo.canUndo(),
            "Gesture cancel restores complete source/allocator and leaves no history");
    CommandArgs wrong = set;
    wrong.pitch = 80;
    require(c.execute("notes.add", wrong).failed() && c.project.state().isEquivalentTo(baseline) &&
                !c.undo.canUndo(),
            "Unrelated command refuses present retention flag before musical mutation");

    // Exact source addressing is independent of retention policy. A repeated
    // display position must never erase the earlier chord it happens to echo.
    for (const bool retention : {false, true})
    {
        auto fixed = project;
        fixed.pattern(pattern)->channelLoops = {{channel, ChannelLoopMode::fixed, 1}};
        c.replace(fixed);
        auto raw = set;
        raw.retainSteps = retention;
        raw.beat = 2.5;
        raw.enabled = false;
        const auto fixedRevision = c.revision;
        require(c.execute("steps.set", raw).wasOk() && c.project.state().isEquivalentTo(fixed.state()) &&
                    c.revision == fixedRevision && !c.undo.canUndo(),
                "Empty ghost-source erase never changes an earlier chord or consumes history");
        raw.enabled = true;
        require(c.execute("steps.set", raw).wasOk() &&
                    c.project.pattern(pattern)->notes.size() == fixed.pattern(pattern)->notes.size() + 1 &&
                    c.project.pattern(pattern)->notes.back().beat == 2.5 &&
                    c.project.pattern(pattern)->notes.back().id == fixed.nextId &&
                    channelLoopLength(c.project, *c.project.pattern(pattern), channel) == 1 &&
                    c.undoEditChecked().wasOk() && c.project.state().isEquivalentTo(fixed.state()),
                "Both activation policies create the explicit later source cell and undo exactly");
    }
    auto automatic = Project::empty();
    const auto autoPattern = automatic.patterns.front().id;
    const auto autoChannel = automatic.channels.front().id;
    automatic.patterns.front().autoLength = false;
    automatic.patterns.front().length = 4;
    automatic.patterns.front().notes = {{automatic.allocate(), autoChannel, .5, .5, 48, .3f, true},
                                        {automatic.allocate(), autoChannel, .5, .25, 72, 0, true}};
    automatic.patterns.front().channelLoops = {{autoChannel, ChannelLoopMode::step, 1}};
    c.replace(automatic);
    CommandArgs autoStep;
    autoStep.pattern = autoPattern;
    autoStep.channel = autoChannel;
    autoStep.beat = .5;
    autoStep.enabled = false;
    autoStep.retainSteps = true;
    require(c.execute("steps.set", autoStep).wasOk() && c.project.nextId == automatic.nextId &&
                channelLoopLength(c.project, *c.project.pattern(autoPattern), autoChannel) == .25,
            "Automatic shrink retains the complete chord without consuming source identities");
    const auto shrunk = c.project.state();
    autoStep.enabled = true;
    require(c.execute("steps.set", autoStep).wasOk() && c.project.state().isEquivalentTo(automatic.state()) &&
                channelLoopLength(c.project, *c.project.pattern(autoPattern), autoChannel) == 1 &&
                c.undoEditChecked().wasOk() && c.project.state().isEquivalentTo(shrunk) &&
                c.redoEditChecked().wasOk() && c.project.state().isEquivalentTo(automatic.state()),
            "Original chord recall after loop shrink and its Undo/Redo preserve every property and period");

    // Keep the existing non-looped semantic admission: off-grid and dormant
    // future source edits were legal before this feature. No new mode narrows
    // the default path merely to satisfy an easier structural helper.
    for (bool explicitFalse : {false, true})
    {
        c.replace(project);
        CommandArgs future;
        future.pattern = pattern;
        future.channel = channel;
        future.beat = 5.125;
        if (explicitFalse)
            future.retainSteps = false;
        require(c.execute("steps.set", future).wasOk(),
                "Legacy/default future off-grid step remains admitted");
        const auto &note = c.project.pattern(pattern)->notes.back();
        require(note.beat == 5.125 && note.step && !note.muted && c.project.pattern(pattern)->length == 4,
                "Manual extent retains future source dormant rather than reject or resize the window");
        require(c.undoEditChecked().wasOk() && c.project.state().isEquivalentTo(baseline),
                "Legacy future source step has exact history");
    }

    c.replace(project);
    CommandArgs fill;
    fill.pattern = pattern;
    fill.channel = channel;
    fill.interval = 2;
    fill.retainSteps = true;
    require(c.execute("steps.fill", fill).wasOk(),
            "Canonical retained fill covers full Pattern source window");
    const auto &filled = c.project.pattern(pattern)->notes;
    require(
        filled.size() == 12 && c.project.nextId == project.nextId + 6 && filled[0].length == .5 &&
            filled[0].pitch == 48 && !filled[0].muted && filled[1].muted &&
            filled[1].id == part.notes[1].id && filled[1].length == .75 && !filled[2].muted &&
            !filled[3].muted && filled[3].velocity == 0 && filled[4].channel == other &&
            filled[4].length == 2 && filled[5].beat == 5,
        "Fill retains off-mask chord properties and dormant/foreign notes; allocates only six missing cells");
    const auto filledState = c.project.state();
    const auto fillRevision = c.revision;
    require(c.execute("steps.fill", fill).wasOk() && c.project.state().isEquivalentTo(filledState) &&
                c.revision == fillRevision,
            "Repeated retained fill does not allocate or create history");
    require(c.undoEditChecked().wasOk() && c.project.state().isEquivalentTo(baseline) && !c.undo.canUndo() &&
                c.redoEditChecked().wasOk() && c.project.state().isEquivalentTo(filledState),
            "Whole retained fill is one exact history action");

    c.replace(project);
    StepComposeRecipe recipe;
    recipe.channel = channel;
    recipe.sequenceSteps = 16;
    recipe.every = 2;
    recipe.loop = false;
    CommandArgs compose;
    compose.pattern = pattern;
    compose.retainSteps = true;
    compose.composition = {recipe};
    require(
        c.execute("steps.compose", compose).wasOk() && c.project.state().isEquivalentTo(filledState),
        "Explicit retained composition matches literal equivalent two-step fill, including exact note IDs");

    // Omitted macro parameters use the application preference; explicit false
    // overrides it. Run through the actual shared command/history implementation.
    for (bool overrideDeletion : {false, true})
    {
        c.replace(project);
        Customization settings;
        settings.muteRemovedSteps = true;
        auto remove = set;
        remove.enabled = false;
        remove.retainSteps.reset();
        if (overrideDeletion)
            remove.retainSteps = false;
        Macro macro{"macro.step-memory", "Remove chord", {{"steps.set", remove, true}}};
        require(settings.addMacro(macro).wasOk(), "Step-memory macro passes canonical action admission");
        MacroContext context;
        context.pattern = pattern;
        context.channel = channel;
        require(settings.runMacro(macro.id, c, context).wasOk(),
                "Actual macro executes its resolved retention policy");
        require(c.project.pattern(pattern)->notes.size() == (overrideDeletion ? 4 : 6),
                "Omitted and explicit false policy remain distinct in actual macro results");
        require(c.undoEditChecked().wasOk() && c.project.state().isEquivalentTo(baseline) &&
                    !c.undo.canUndo(),
                "Macro policy result remains one exact musical Undo");
    }
}
