// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "StepPitch.h"
#include "Customization.h"
#include <limits>

// Original pure model tests; command/history/UI/worker integration is separate.
static void stepPitchTests()
{
    auto baseline = Project::empty();
    const auto pattern = baseline.patterns.front().id;
    const auto channel = baseline.channels[0].id, other = baseline.channels[1].id;
    auto &part = baseline.patterns.front();
    part.autoLength = false;
    part.length = 4;
    const double beats[] = {0, .25, .25, .5, .75, 1};
    const int pitches[] = {60, 64, 67, 72, 0, 127};
    for (int i = 0; i < 6; ++i)
        part.notes.push_back(
            {baseline.allocate(), channel, beats[i], .5 + i * .25, pitches[i], float(i) / 5, true, i == 2});
    const auto anchor = part.notes[0].id;
    part.notes.push_back({baseline.allocate(), channel, 0, 2, 41, .31f, false});
    const auto melodic = part.notes.back().id;
    part.notes.push_back({baseline.allocate(), other, .25, .5, 42, .4f, true});
    const auto foreign = part.notes.back().id;
    part.notes.push_back({baseline.allocate(), channel, 4, .25, 43, .5f, true});
    const auto dormant = part.notes.back().id;
    part.notes.push_back({baseline.allocate(), channel, .1, .25, 44, .6f, true});
    const auto offGrid = part.notes.back().id;
    part.notes.push_back({baseline.allocate(), channel, -.25, .25, 45, .7f, true});
    const auto negative = part.notes.back().id;
    auto extraPattern = part;
    extraPattern.id = baseline.allocate();
    for (auto &note : extraPattern.notes)
        note.id = baseline.allocate();
    baseline.patterns.push_back(extraPattern);
    baseline.placements = {{baseline.allocate(), pattern, 0, 3, 4, 0}};
    const auto opening = baseline.state();
    const auto pitchVector = [&](const Project &project)
    {
        std::vector<int> result;
        for (size_t i = 0; i < 6; ++i)
            result.push_back(project.pattern(pattern)->notes[i].pitch);
        return result;
    };
    const auto onlyPitchChanged = [&](const Project &project)
    {
        auto restored = project;
        auto *notes = &restored.pattern(pattern)->notes;
        for (size_t i = 0; i < 6; ++i)
            (*notes)[i].pitch = pitches[i];
        require(
            restored.state().isEquivalentTo(opening),
            "Pitch graph preserves every other property, unrelated note, Pattern, placement and allocator");
    };
    CommandArgs args;
    args.pattern = pattern;
    args.channel = channel;
    args.pitch = 73;
    auto next = baseline;
    require(applyStepPitch(next, baseline, args).wasOk() &&
                pitchVector(next) == std::vector<int>{73, 73, 73, 73, 73, 73},
            "Default set assigns integral pitch to aligned active steps including chords and "
            "muted/zero-velocity events");
    onlyPitchChanged(next);
    auto defaultPitch = args;
    defaultPitch.pitch = CommandArgs{}.pitch;
    defaultPitch.value = 127;
    require(applyStepPitch(next, baseline, defaultPitch).wasOk() &&
                pitchVector(next) == std::vector<int>{60, 60, 60, 60, 60, 60},
            "Omitted pitch keeps creation default60 and never borrows the velocity value field");
    args.text = "reset";
    args.pitch = -1;
    args.value = std::numeric_limits<double>::quiet_NaN();
    args.offset = std::numeric_limits<double>::infinity();
    require(applyStepPitch(next, baseline, args).wasOk() &&
                pitchVector(next) == std::vector<int>{60, 60, 60, 60, 60, 60},
            "Reset uses creation pitch60 and ignores unused numeric fields");
    onlyPitchChanged(next);
    args.text = "scale";
    args.target = anchor;
    args.pitch = 65;
    require(applyStepPitch(next, baseline, args).wasOk() &&
                pitchVector(next) == std::vector<int>{65, 69, 72, 77, 5, 127},
            "Pitch scale transposes literal chord intervals with upper clamp, rather than multiplying MIDI "
            "pitches");
    args.pitch = 0;
    require(applyStepPitch(next, baseline, args).wasOk() &&
                pitchVector(next) == std::vector<int>{0, 4, 7, 12, 0, 67},
            "Repeated transposition uses immutable baseline with lower clamp and no cumulative clipping");
    onlyPitchChanged(next);
    args.pitch = 127;
    next = baseline;
    require(applyStepPitch(next, next, args).wasOk() &&
                pitchVector(next) == std::vector<int>{127, 127, 127, 127, 67, 127},
            "Direct aliased baseline call snapshots the original anchor before writing any pitch");
    onlyPitchChanged(next);
    args.text = "ramp";
    args.beat = 0;
    args.length = 1;
    args.pitch = 60;
    args.offset = 65;
    require(applyStepPitch(next, baseline, args).wasOk() &&
                pitchVector(next) == std::vector<int>{60, 61, 61, 63, 64, 65},
            "Forward pitch ramp rounds nearest integer, including half pitches and coincident chord events");
    args.length = .25;
    args.pitch = 70;
    args.offset = 74;
    require(applyStepPitch(next, baseline, args).wasOk() &&
                pitchVector(next) == std::vector<int>{70, 74, 74, 72, 0, 127},
            "Shorter ramp restores targeted notes outside its span from the opening baseline");
    args.beat = 1;
    args.length = -1;
    args.pitch = 65;
    args.offset = 60;
    require(applyStepPitch(next, baseline, args).wasOk() &&
                pitchVector(next) == std::vector<int>{60, 61, 61, 63, 64, 65},
            "Reverse musical ramp retains exact endpoint direction and nearest-integer pitches");
    args.beat = .25;
    args.length = 0;
    args.pitch = 2;
    args.offset = 91;
    require(applyStepPitch(next, baseline, args).wasOk() &&
                pitchVector(next) == std::vector<int>{60, 91, 91, 72, 0, 127},
            "Zero-span ramp assigns its final endpoint to the whole column and restores every other target");
    onlyPitchChanged(next);
    args = {};
    args.pattern = pattern;
    args.channel = channel;
    args.pitch = 12;
    args.ids = {baseline.pattern(pattern)->notes[2].id};
    next = baseline;
    require(applyStepPitch(next, baseline, args).wasOk() &&
                pitchVector(next) == std::vector<int>{60, 64, 12, 72, 0, 127},
            "Explicit stable-ID selection edits one chord voice without changing its coincident neighbour");
    onlyPitchChanged(next);
    const auto refuse = [&](const CommandArgs &bad, const Project &source, Project candidate)
    {
        const auto before = candidate.state();
        require(applyStepPitch(candidate, source, bad).failed() && candidate.state().isEquivalentTo(before),
                "Invalid pitch graph input refuses atomically even after valid earlier targets");
    };
    for (auto ids :
         {std::vector<Id>{anchor, anchor}, std::vector<Id>{anchor, foreign}, std::vector<Id>{anchor, melodic},
          std::vector<Id>{anchor, dormant}, std::vector<Id>{anchor, offGrid},
          std::vector<Id>{anchor, negative}, std::vector<Id>{anchor, 999999999}})
    {
        auto bad = args;
        bad.ids = ids;
        refuse(bad, baseline, next);
    }
    args.ids.clear();
    for (int value : {-1, 128, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()})
    {
        auto bad = args;
        bad.pitch = value;
        refuse(bad, baseline, next);
    }
    for (double value :
         {-1., 128., 60.5, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()})
    {
        auto bad = args;
        bad.text = "ramp";
        bad.offset = value;
        refuse(bad, baseline, next);
    }
    for (int fault = 0; fault < 15; ++fault)
    {
        auto bad = args;
        auto source = baseline, candidate = next;
        if (fault == 0)
            bad.text = "unknown";
        if (fault == 1)
            bad.pattern = 999999999;
        if (fault == 2)
            bad.channel = 999999999;
        if (fault == 3)
            candidate.channel(channel)->audioClip = true;
        if (fault == 4)
            source.patterns.clear();
        if (fault == 5)
            candidate.pattern(pattern)->notes.clear();
        if (fault >= 6)
            bad.text = "ramp";
        if (fault == 6)
            bad.beat = .1;
        if (fault == 7)
            bad.length = .1;
        if (fault == 8)
            bad.length = 4;
        if (fault == 9)
            bad.length = -.25;
        if (fault == 10)
            bad.beat = std::numeric_limits<double>::quiet_NaN();
        if (fault == 11)
            source.pattern(pattern)->notes[0].beat = .25;
        if (fault == 12)
            source.pattern(pattern)->notes.erase(source.pattern(pattern)->notes.begin());
        if (fault == 13)
            bad.length = std::numeric_limits<double>::infinity();
        if (fault == 14)
            bad.beat = 4;
        refuse(bad, source, candidate);
    }
    auto bad = args;
    bad.text = "scale";
    bad.target = melodic;
    refuse(bad, baseline, next);
    bad.target = anchor;
    bad.ids = {baseline.pattern(pattern)->notes[1].id};
    refuse(bad, baseline, next);
    bad = args;
    bad.ids.assign(100001, anchor);
    refuse(bad, baseline, next);

    auto musical = Project::empty();
    musical.patterns.front().autoLength = false;
    musical.patterns.front().length = 4;
    const auto musicalPattern = musical.patterns.front().id;
    const auto musicalChannel = musical.channels.front().id;
    for (int value : {60, 64, 67, 72})
        musical.patterns.front().notes.push_back({musical.allocate(), musicalChannel,
                                                  musical.patterns.front().notes.size() * .25, .25, value,
                                                  .5f, true});
    require(musical.validate().wasOk(), "Pitch macro fixture is a genuine valid project");
    Controller controller;
    controller.replace(musical);
    CommandArgs transpose;
    transpose.pattern = musicalPattern;
    transpose.channel = musicalChannel;
    transpose.text = "scale";
    transpose.target = musical.patterns.front().notes.front().id;
    transpose.pitch = 65;
    Customization authored = Customization::familiarDefaults(), restored;
    Macro macro{"macro.step-pitch", "Transpose step chord", {{"steps.pitch", transpose, false}}};
    require(authored.addMacro(macro).wasOk() && restored.importState(authored.state()).wasOk(),
            "Real pitch macro creation and profile round trip retain typed existing arguments");
    MacroContext context;
    require(restored.runMacro(macro.id, controller, context).wasOk() && !restored.running,
            "Restored pitch macro completes through the canonical application command");
    const int expected[] = {65, 69, 72, 77};
    for (size_t i = 0; i < 4; ++i)
        require(controller.project.pattern(musicalPattern)->notes[i].pitch == expected[i] &&
                    controller.project.pattern(musicalPattern)->notes[i].step,
                "Pitch macro transposes literal intervals and preserves step identity");
    const auto macroResult = controller.project.state();
    require(controller.undoEditChecked().wasOk() &&
                controller.project.state().isEquivalentTo(musical.state()) && !controller.undo.canUndo() &&
                controller.redoEditChecked().wasOk() &&
                controller.project.state().isEquivalentTo(macroResult),
            "Whole pitch macro has one exact canonical Undo/Redo");
}
