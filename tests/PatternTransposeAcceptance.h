// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

static void patternTransposeTests(const juce::File &folder)
{
    Controller controller;
    auto project = Project::empty();
    const auto pattern = project.patterns.front().id;
    const auto first = project.channels.front().id, second = project.channels.back().id;
    project.patterns.front().notes = {{project.allocate(), first, 0, .5, 48, .7f, false},
                                      {project.allocate(), second, 1, .25, 60, .6f, true}};
    project.placements = {{project.allocate(), pattern, 0, 0, 4, 0},
                          {project.allocate(), pattern, 2, 4, 2, 1, true}};
    controller.replace(project);
    const auto original = controller.project.state();
    CommandArgs args;
    args.pattern = pattern;
    args.channel = first;
    args.value = 12;
    require(controller.execute("pattern.transpose", args).wasOk(), "Transpose selected Pattern Channel");
    auto expected = project;
    expected.patterns.front().notes.front().pitch = 60;
    const auto shifted = controller.project.state();
    require(expected.state().isEquivalentTo(shifted), "Transpose changes only targeted pitch, retaining note "
                                                      "IDs/timing/steps, linked windows and source state");
    controller.undoEdit();
    require(original.isEquivalentTo(controller.project.state()), "Transpose is exactly one Undo");
    controller.redoEdit();
    require(shifted.isEquivalentTo(controller.project.state()), "Transpose exact Redo");
    controller.undoEdit();
    args.channel = 0;
    args.value = -12;
    require(controller.execute("pattern.transpose", args).wasOk(), "Transpose all Pattern Channels");
    require(controller.project.pattern(pattern)->notes[0].pitch == 36 &&
                controller.project.pattern(pattern)->notes[1].pitch == 48 &&
                controller.project.pattern(pattern)->notes[1].step,
            "Negative transpose includes steps without converting note kinds");
    const auto file = folder.getChildFile("transposed-pattern.vibra");
    Project reopened;
    require(saveProject(controller.project, file).wasOk() && loadProject(file, reopened).wasOk() &&
                reopened.state().isEquivalentTo(controller.project.state()),
            "Transposed linked Pattern round trip");
    controller.undoEdit();
    const auto revision = controller.revision;
    for (auto value : {128., -128., 1.5, 68., -61., std::numeric_limits<double>::infinity(),
                       std::numeric_limits<double>::quiet_NaN()})
    {
        args.value = value;
        require(controller.execute("pattern.transpose", args).failed() &&
                    original.isEquivalentTo(controller.project.state()) && controller.revision == revision,
                "Invalid or partially overflowing transpose preserves every note and history revision");
    }
    args.value = 0;
    require(controller.execute("pattern.transpose", args).wasOk() && controller.revision == revision,
            "Zero transpose is inert and creates no Undo transaction");
    args.pattern = -1;
    args.value = 12;
    require(controller.execute("pattern.transpose", args).failed() && controller.revision == revision,
            "Missing Pattern cannot transpose another source");
    args.pattern = pattern;
    args.channel = -1;
    require(controller.execute("pattern.transpose", args).failed(), "Missing Channel refused");
    auto empty = project;
    empty.patterns.front().notes.clear();
    controller.replace(empty);
    args.channel = first;
    require(controller.execute("pattern.transpose", args).failed() && !controller.undo.canUndo(),
            "Empty transpose refuses without history");
    controller.replace(project);
    Customization customization = Customization::familiarDefaults();
    CommandArgs semitones;
    semitones.value = 12;
    Macro macro{"macro.transpose-test", "Octave selected Channel", {{"pattern.transpose", semitones, true}}};
    require(customization.addMacro(macro).wasOk(), "Transpose supports parameterized user macros");
    MacroContext context;
    context.pattern = pattern;
    context.channel = first;
    require(customization.runMacro(macro.id, controller, context).wasOk() &&
                expected.state().isEquivalentTo(controller.project.state()),
            "Transpose macro uses current Pattern/Channel and stored semitones");
    controller.undoEdit();
    require(original.isEquivalentTo(controller.project.state()), "Transpose macro exact Undo");
    folder.getChildFile("pattern-transpose-verification.txt")
        .replaceWithText(
            "PASS: selected/all Channels, step pitches, linked placements and untouched state, "
            "atomic range/nonfinite/target refusals, zero no-op, round trip, exact Undo/Redo and "
            "parameterized macro. Automated model evidence, not reference execution/listening.\n");
}
