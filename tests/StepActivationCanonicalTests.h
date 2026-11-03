// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "StepActivationCommand.h"
#include "PatternChannelLoops.h"

// Integration-test draft for the actual application's acceptance harness after
// source freeze. NOT registered, compiled or executed by the standalone probe.
static void stepActivationCanonicalTests(const juce::File &folder)
{
    using namespace vibera;
    using step_memory::Retention;
    auto project = Project::empty();
    const auto pattern = project.patterns.front().id, channel = project.channels.front().id;
    project.patterns.front().autoLength = false;
    project.patterns.front().length = 4;
    const auto first = project.allocate(), second = project.allocate();
    project.patterns.front().notes = {{first, channel, .5, .5, 48, .3f, true},
                                      {second, channel, .5, .25, 72, 0, true}};
    const auto before = project.state();
    const auto nextId = project.nextId;
    require(applyStepActivation(project, pattern, channel, .5, false, Retention::mute).wasOk(),
            "Actual Project retains deactivated chord");
    require(project.nextId == nextId && project.pattern(pattern)->notes.size() == 2 &&
                project.pattern(pattern)->notes[0].muted && project.pattern(pattern)->notes[1].muted,
            "Deactivation preserves real IDs and allocator, with no replacement notes");
    const auto retained = project.state();
    require(applyStepActivation(project, pattern, channel, .5, false, Retention::mute).wasOk() &&
                project.state().isEquivalentTo(retained),
            "Actual Project repeated deactivation is exact no-op");
    require(applyStepActivation(project, pattern, channel, .5, true, Retention::mute).wasOk() &&
                project.state().isEquivalentTo(before),
            "Actual Project reactivation restores exact zero velocity/chord properties and IDs");
    require(applyStepActivation(project, pattern, channel, 2.5, false, Retention::discard).wasOk() &&
                project.state().isEquivalentTo(before),
            "Erasing empty cell leaves real allocator/project untouched");
    project.pattern(pattern)->channelLoops = {{channel, ChannelLoopMode::fixed, 1}};
    const auto fixed = project.state();
    require(applyStepActivation(project, pattern, channel, 2.5, false, Retention::mute).wasOk() &&
                project.state().isEquivalentTo(fixed),
            "Erasing a source-empty ghost position never erases its earlier repeated chord");
    auto laterSource = project;
    require(applyStepActivation(laterSource, pattern, channel, 2.5, true, Retention::mute).wasOk() &&
                laterSource.pattern(pattern)->notes.size() == 3 &&
                laterSource.pattern(pattern)->notes.back().beat == 2.5 &&
                laterSource.pattern(pattern)->notes.back().id == nextId &&
                !laterSource.pattern(pattern)->notes[0].muted &&
                channelLoopLength(laterSource, *laterSource.pattern(pattern), channel) == 1,
            "A later click creates its own raw source cell; fixed playback extent remains explicit");

    project.pattern(pattern)->channelLoops = {{channel, ChannelLoopMode::step, 1}};
    const auto automatic = project.state();
    require(applyStepActivation(project, pattern, channel, .5, false, Retention::mute).wasOk() &&
                project.pattern(pattern)->notes[0].muted && project.pattern(pattern)->notes[1].muted &&
                project.nextId == nextId &&
                channelLoopLength(project, *project.pattern(pattern), channel) == .25,
            "Muting the final source chord permits automatic shrink without discarding its identities");
    require(applyStepActivation(project, pattern, channel, .5, true, Retention::mute).wasOk() &&
                project.state().isEquivalentTo(automatic) &&
                channelLoopLength(project, *project.pattern(pattern), channel) == 1,
            "Reactivation recalls the same source chord after automatic shrink and restores its period");
    for (const bool muted : {false, true})
    {
        auto zero = project;
        zero.pattern(pattern)->notes.erase(zero.pattern(pattern)->notes.begin());
        zero.pattern(pattern)->notes.front().muted = muted;
        require(channelLoopLength(zero, *zero.pattern(pattern), channel) == .25,
                "Actual current automatic-loop helper ignores zero-velocity/muted source");
        const auto originalZero = zero.state();
        auto expectedZero = zero;
        expectedZero.pattern(pattern)->notes.front().muted = false;
        require(applyStepActivation(zero, pattern, channel, .5, true, Retention::mute).wasOk() &&
                    zero.state().isEquivalentTo(expectedZero.state()) && zero.nextId == project.nextId &&
                    zero.pattern(pattern)->notes.front().velocity == 0 &&
                    (muted || zero.state().isEquivalentTo(originalZero)),
                "Muted OR active zero-velocity source is recalled exactly, never an audible default "
                "in an aliased repetition");
    }
    auto restored = Project::empty();
    const auto path = folder.getChildFile("retained-step-score.vibra");
    auto saved = project;
    saved.pattern(pattern)->channelLoops.clear();
    require(applyStepActivation(saved, pattern, channel, .5, false, Retention::mute).wasOk() &&
                saveProject(saved, path).wasOk() && loadProject(path, restored).wasOk() &&
                restored.state().isEquivalentTo(saved.state()),
            "Existing project note mute schema stores exact retained step properties/IDs");
}
