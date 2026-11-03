// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "StepVelocity.h"
static void stepVelocityTests(const juce::File &folder)
{
    Controller c;
    const auto channel = c.project.channels[0].id, other = c.project.channels[1].id;
    const auto pattern = c.project.patterns[0].id;
    auto &part = c.project.patterns[0];
    part.autoLength = false; // Graph targets exclude the explicitly retained dormant note below.
    for (int i = 0; i < 4; ++i)
        part.notes.push_back({c.project.allocate(), channel, i * .25, .25, 60, (i + 1) * .2f, true});
    const auto first = part.notes[0].id;
    part.notes.push_back({c.project.allocate(), channel, 0, .5, 65, .31f, false});
    const auto melody = part.notes.back().id;
    part.notes.push_back({c.project.allocate(), other, 0, .25, 60, .27f, true});
    const auto wrong = part.notes.back().id;
    part.notes.push_back({c.project.allocate(), channel, part.length, .25, 60, .67f, true});
    const auto dormant = part.notes.back().id;
    auto second = part;
    second.id = c.project.allocate();
    for (auto &note : second.notes)
        note.id = c.project.allocate();
    c.project.patterns.push_back(second);
    c.project.placements = {{c.project.allocate(), pattern, 0, 0, 4, 0},
                            {c.project.allocate(), pattern, 1, 4, 4, 0}};
    const auto initial = c.project.state();
    CommandArgs a;
    a.pattern = pattern;
    a.channel = channel;
    a.value = .5;
    require(c.execute("steps.velocity", a).wasOk(), "Graph canonical set all active steps");
    for (int i = 0; i < 4; ++i)
        require(c.project.pattern(pattern)->notes[size_t(i)].velocity == .5f, "Set normalized velocity");
    require(c.project.pattern(pattern)->notes[4].velocity == .31f &&
                c.project.pattern(pattern)->notes[5].velocity == .27f &&
                c.project.pattern(pattern)->notes[6].velocity == .67f &&
                c.project.state()
                    .getChildWithName("PATTERNS")
                    .getChild(1)
                    .isEquivalentTo(initial.getChildWithName("PATTERNS").getChild(1)),
            "Graph excludes melody, other Channels, dormant notes and other Patterns");
    const auto changed = c.project.state();
    c.undoEdit();
    require(c.project.state().isEquivalentTo(initial), "Graph exact Undo");
    c.redoEdit();
    require(c.project.state().isEquivalentTo(changed), "Graph exact Redo");
    c.undoEdit();
    auto velocity = [&](int index) { return c.project.pattern(pattern)->notes[size_t(index)].velocity; };
    require(c.beginGesture(), "Scale gesture begins");
    const auto serial = c.gestureSerial();
    a.text = "scale";
    a.target = first;
    a.value = .4;
    require(c.execute("steps.velocity", a).wasOk() && std::abs(velocity(1) - .8f) < 1.e-6 && velocity(3) == 1,
            "Scale uses anchor ratio and clips normalized range");
    a.value = .1;
    require(c.execute("steps.velocity", a).wasOk() && std::abs(velocity(3) - .4f) < 1.e-6 &&
                c.gestureSerial() == serial,
            "Repeated scaling uses gesture baseline without compounding");
    c.cancelGesture();
    require(c.project.state().isEquivalentTo(initial), "Cancel restores exact graph baseline");
    c.beginGesture();
    a.text = "ramp";
    a.beat = 0;
    a.length = .75;
    a.value = .1;
    a.offset = .7;
    require(c.execute("steps.velocity", a).wasOk() && std::abs(velocity(1) - .3f) < 1.e-6,
            "Forward ramp interpolates by musical columns");
    a.length = .25;
    require(c.execute("steps.velocity", a).wasOk() && velocity(2) == .6f && velocity(3) == .8f,
            "Shrinking ramp restores out-of-range baseline values");
    c.endGesture("Graph ramp");
    const auto ramped = c.project.state();
    c.undoEdit();
    require(c.project.state().isEquivalentTo(initial), "One Undo per ramp drag");
    c.redoEdit();
    require(c.project.state().isEquivalentTo(ramped), "Ramp redo exact");
    c.undoEdit();
    a.beat = .75;
    a.length = -.75;
    a.value = .2;
    a.offset = .8;
    require(c.execute("steps.velocity", a).wasOk() && std::abs(velocity(0) - .8f) < 1.e-6 &&
                std::abs(velocity(3) - .2f) < 1.e-6,
            "Reverse ramp endpoints");
    c.undoEdit();
    for (auto ids : {std::vector<Id>{first, first}, std::vector<Id>{wrong}, std::vector<Id>{melody},
                     std::vector<Id>{dormant}, std::vector<Id>{999999}})
    {
        a.text = "set";
        a.ids = ids;
        require(c.execute("steps.velocity", a).failed() && c.project.state().isEquivalentTo(initial),
                "Invalid graph targets atomic");
    }
    a.ids.clear();
    for (double value : {-1., 1.01, std::numeric_limits<double>::quiet_NaN()})
    {
        a.value = value;
        require(c.execute("steps.velocity", a).failed() && c.project.state().isEquivalentTo(initial),
                "Invalid velocity rejected atomically");
    }
    a.text = "ramp";
    a.value = .5;
    a.offset = .5;
    a.beat = .1;
    a.length = .25;
    require(c.execute("steps.velocity", a).failed(), "Off-grid ramp endpoint refused");
    a.beat = 0;
    a.length = c.project.pattern(pattern)->length;
    require(c.execute("steps.velocity", a).failed(), "Dormant endpoint refused");
    a.text = "set";
    a.value = 0;
    a.ids = {first};
    require(c.execute("steps.velocity", a).wasOk() && velocity(0) == 0 &&
                c.project.pattern(pattern)->notes[0].step,
            "Zero velocity retains active step identity");
    a.text = "scale";
    a.target = first;
    a.value = .1;
    a.ids.clear();
    require(c.execute("steps.velocity", a).wasOk() && std::abs(velocity(1) - .5f) < 1.e-6,
            "Zero anchor has defined additive lift");
    a.text = "reset";
    require(c.execute("steps.velocity", a).wasOk(), "Reset graph velocities");
    for (int i = 0; i < 4; ++i)
        require(velocity(i) == .8f, "Reset uses actual step creation default");
    const auto file = folder.getChildFile("velocity-roundtrip.vibra");
    Project reopened;
    require(saveProject(c.project, file).wasOk() && loadProject(file, reopened).wasOk() &&
                reopened.state().isEquivalentTo(c.project.state()),
            "Graph stable identities and values round trip");
    auto custom = Customization::familiarDefaults();
    require(custom.resolve(juce::KeyPress('K', juce::ModifierKeys::ctrlModifier, 0), Scope::rack) ==
                "rack.graph",
            "Contextual Ctrl+K");
    Macro macro{"macro.step-velocity", "Quiet steps", {{"steps.velocity", {}, true}}};
    macro.steps[0].parameters.value = .25;
    require(custom.addMacro(macro).wasOk(), "Velocity command supports parameterized document macro");
    MacroContext context{pattern, channel, 0, {wrong}, {}, {}};
    const auto macroBefore = c.project.state();
    require(custom.runMacro(macro.id, c, context).wasOk() && velocity(0) == .25f &&
                c.project.pattern(pattern)->notes[5].velocity == .27f,
            "Macro targets current step Channel, ignores unrelated Piano selection");
    c.undoEdit();
    require(c.project.state().isEquivalentTo(macroBefore), "Graph macro one Undo");
}
