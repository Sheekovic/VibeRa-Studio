// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Quantizer.h"
static void quantizerTests(const juce::File &folder)
{
    Controller c;
    const auto pattern = c.project.patterns[0].id;
    const auto channel = c.project.channels[0].id, ghost = c.project.channels[1].id;
    c.project.patterns[0].notes = {{c.project.allocate(), channel, .08, .34, 60, .7f, false},
                                   {c.project.allocate(), channel, .63, .4, 64, .3f, false},
                                   {c.project.allocate(), ghost, .13, .28, 67, .9f, false}};
    c.project.placements = {{c.project.allocate(), pattern, 0, 0, 4, 0},
                            {c.project.allocate(), pattern, 1, 4, 4, 0}};
    c.project.name = "Quantizer acceptance";
    const auto before = c.project;
    const auto source = before.patterns[0].notes;
    CommandArgs a;
    a.pattern = pattern;
    a.channel = channel;
    a.text = "duration";
    auto close = [](double x, double y) { return std::abs(x - y) < 1.e-9; };
    require(c.beginGesture("preview"), "Quantizer owns a baseline transaction");
    a.strength = .5;
    require(c.execute("notes.quantize", a, "preview").wasOk(), "Half-strength preview");
    require(close(c.project.patterns[0].notes[0].beat, .04) &&
                close(c.project.patterns[0].notes[0].length, .25),
            "Start and duration are independent");
    a.strength = 0;
    a.durationStrength = 0;
    require(c.execute("notes.quantize", a, "preview").wasOk() &&
                c.project.state().isEquivalentTo(before.state()),
            "Zero strengths restore original exact baseline");
    a.strength = 1;
    a.durationStrength = 1;
    require(c.execute("notes.quantize", a, "preview").wasOk(), "Full grid preview");
    const auto full = c.project;
    require(full.patterns[0].notes[2].id == source[2].id && full.patterns[0].notes[2].beat == source[2].beat,
            "Empty selection affects current part, not ghosts");
    const auto rendered = sequence(full, false, pattern);
    require(rendered.size() == 6 && close(rendered[3].beat, 4),
            "Linked Pattern instances both follow source score");
    a.sensitivity = .1;
    require(c.execute("notes.quantize", a, "preview").wasOk() &&
                c.project.state().isEquivalentTo(before.state()),
            "Low sensitivity excludes distant starts and durations");
    a.sensitivity = 1;
    a.text = "end";
    require(c.execute("notes.quantize", a, "preview").wasOk() &&
                close(c.project.patterns[0].notes[0].length, .5),
            "End-time mode snaps absolute end");
    a.text = "leaveDuration";
    require(c.execute("notes.quantize", a, "preview").wasOk() &&
                close(c.project.patterns[0].notes[0].length, .34),
            "Leave-duration mode retains length");
    a.text = "leaveEnd";
    require(c.execute("notes.quantize", a, "preview").wasOk() &&
                close(c.project.patterns[0].notes[0].length, .42),
            "Leave-end mode retains original end");
    const auto latest = c.project.state();
    for (auto value : {-1., 1.1, std::numeric_limits<double>::quiet_NaN()})
    {
        a.strength = value;
        require(c.execute("notes.quantize", a, "preview").failed() &&
                    c.project.state().isEquivalentTo(latest),
                "Invalid strength is atomic and retains last good preview");
    }
    a.strength = 1;
    c.cancelGesture("preview");
    require(c.project.state().isEquivalentTo(before.state()) && !c.undo.canUndo(),
            "Cancel restores source and creates no history");
    a.ids = {source[0].id, source[2].id};
    a.text = "duration";
    require(c.beginGesture("preview") && c.execute("notes.quantize", a, "preview").wasOk(),
            "Explicit ghost selection supported");
    c.endGesture("Quantize notes", "preview");
    const auto committed = c.project.state();
    require(c.project.patterns[0].notes[1].beat == source[1].beat &&
                close(c.project.patterns[0].notes[2].beat, .25),
            "Only explicit notes change");
    c.undoEdit();
    require(c.project.state().isEquivalentTo(before.state()) && !c.undo.canUndo(),
            "Preview commit is exactly one Undo");
    c.redoEdit();
    require(c.project.state().isEquivalentTo(committed), "Exact Quantizer Redo");
    Project loaded;
    const auto file = folder.getChildFile("quantizer-roundtrip.vibra");
    require(saveProject(c.project, file).wasOk() && loadProject(file, loaded).wasOk() &&
                loaded.state().isEquivalentTo(committed),
            "Quantized project round trip");
    for (auto mode : {"duration", "end", "leaveDuration", "leaveEnd"})
        for (double grid : {.03125, .125, .25, 1., 64.})
            for (double strength : {0., .5, 1.})
            {
                auto transformed = before;
                a.ids.clear();
                a.length = grid;
                a.text = mode;
                a.strength = strength;
                require(quantizeNotes(transformed, before, a, "notes.quantize").wasOk() &&
                            transformed.validate().wasOk(),
                        "Grid/mode/strength matrix keeps valid identities and durations");
                for (size_t i = 0; i < source.size(); ++i)
                    require(transformed.patterns[0].notes[i].id == source[i].id &&
                                transformed.patterns[0].notes[i].pitch == source[i].pitch &&
                                transformed.patterns[0].notes[i].velocity == source[i].velocity,
                            "Quantizer retains identities/pitch/velocity");
            }
    const auto defaults = Customization::familiarDefaults();
    require(defaults.resolve(juce::KeyPress('Q', juce::ModifierKeys::altModifier, 0), Scope::piano) ==
                "notes.quantize",
            "Alt+Q opens advanced workflow");
    require(defaults.resolve(juce::KeyPress('Q', juce::ModifierKeys::ctrlModifier, 0), Scope::piano) ==
                "notes.quickQuantize",
            "Ctrl+Q quick timing");
    require(defaults.resolve(juce::KeyPress('Q', juce::ModifierKeys::shiftModifier, 0), Scope::piano) ==
                "notes.quantizeStarts",
            "Shift+Q onset only");
    c.replace(before);
    a = {};
    a.pattern = pattern;
    a.channel = channel;
    require(c.execute("notes.quantizeStarts", a).wasOk() &&
                c.project.patterns[0].notes[0].length == source[0].length,
            "Quick start action preserves durations");
    c.replace(before);
    require(c.execute("notes.quickQuantize", a).wasOk() && close(c.project.patterns[0].notes[0].length, .25),
            "Quick quantize includes durations");
    c.replace(before);
    Customization profile;
    CommandArgs move;
    move.beat = .2;
    move.length = 0;
    move.pitch = 0;
    CommandArgs quant;
    quant.text = "duration";
    quant.strength = .5;
    Macro macro{"macro.quantizer",
                "Move then quantize",
                {{"notes.move", move, true}, {"notes.quantize", quant, true}}};
    require(profile.addMacro(macro).wasOk(), "Quantizer macro registration");
    Customization imported;
    require(imported.importState(profile.state()).wasOk() &&
                imported.macros[0].steps[1].parameters.strength == .5,
            "Strength/mode survive profile round trip");
    MacroContext context;
    context.pattern = pattern;
    context.channel = channel;
    context.notes = {source[0].id};
    require(imported.runMacro("macro.quantizer", c, context).wasOk() &&
                close(c.project.patterns[0].notes[0].beat, .265),
            "Macro quantization baselines after prior move, not before macro");
    c.undoEdit();
    require(c.project.state().isEquivalentTo(before.state()), "Composite macro one exact Undo");
    auto large = before;
    large.patterns[0].notes.clear();
    quant = {};
    quant.pattern = pattern;
    quant.channel = channel;
    for (int i = 0; i < 10000; ++i)
    {
        const auto id = large.allocate();
        large.patterns[0].notes.push_back({id, channel, i * .25 + .02, .2, 60, .8f, false});
        quant.ids.push_back(id);
    }
    const auto largeBefore = large;
    require(quantizeNotes(large, largeBefore, quant, "notes.quantize").wasOk() && large.validate().wasOk() &&
                large.patterns[0].notes.back().beat == 2499.75,
            "Large explicit selection indexed without quadratic target searches");
}
