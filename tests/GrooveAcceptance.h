// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Groove.h"
static void grooveTests(const juce::File &folder)
{
    for (const auto &groove : factoryGrooves())
    {
        require(validateGroove(groove).wasOk(), "Original presets are valid independent data");
        GrooveTemplate parsed;
        require(parseGroove(grooveState(groove), parsed).wasOk() &&
                    juce::JSON::toString(grooveState(parsed)) == juce::JSON::toString(grooveState(groove)),
                "Strict groove round trip");
        const auto file = folder.getChildFile(groove.name + ".vgroove");
        require(saveGroove(groove, file, true).wasOk() && loadGroove(file, parsed).wasOk(),
                "Actual reusable template save/load");
        require(saveGroove(groove, file, false).failed(), "Existing groove requires explicit overwrite");
    }
    auto invalid = grooveState(factoryGrooves()[0]);
    invalid.getDynamicObject()->setProperty("version", 2);
    auto retained = factoryGrooves()[1];
    const auto retainedState = juce::JSON::toString(grooveState(retained));
    require(parseGroove(invalid, retained).failed() &&
                juce::JSON::toString(grooveState(retained)) == retainedState,
            "Unsupported template is refused without changing destination");
    invalid = grooveState(factoryGrooves()[0]);
    invalid["points"][0].getDynamicObject()->setProperty("velocity", true);
    require(parseGroove(invalid, retained).failed(), "Boolean is not a numeric groove level");
    invalid = grooveState(factoryGrooves()[0]);
    invalid.getDynamicObject()->setProperty("unexpected", 1);
    require(parseGroove(invalid, retained).failed(), "Unknown template fields fail closed");
    const auto blocked = folder.getChildFile("blocked.vgroove");
    blocked.createDirectory();
    require(saveGroove(retained, blocked, true).failed(), "Never replace a directory with a template");
    Controller c;
    const auto channel = c.project.channels[0].id, other = c.project.channels[1].id,
               pattern = c.project.patterns[0].id;
    auto &score = c.project.patterns[0];
    score.length = 4;
    for (int i = 0; i < 8; ++i)
        score.notes.push_back({c.project.allocate(), channel, i * .25, .25, 60, .8f, false});
    score.notes.push_back({c.project.allocate(), other, .25, .5, 67, .4f, false});
    const auto before = c.project;
    GrooveTemplate captured;
    require(captureGroove(before, pattern, channel, {}, 4, "My timing", captured).wasOk() &&
                captured.points.size() == 8,
            "Capture active multi-instrument part only");
    auto chord = before;
    chord.patterns[0].notes.push_back({chord.allocate(), channel, 0, .75, 72, .2f, false});
    require(captureGroove(chord, pattern, channel, {}, 4, "Chord", captured).wasOk() &&
                captured.points[0].length == .25 &&
                captured.points[0].velocity == double(before.patterns[0].notes[0].velocity),
            "Chord capture ignores pitch and keeps lowest stable ID per onset");
    require(captureGroove(chord, pattern, channel, {99999}, 4, "Stale", captured).failed(),
            "Stale capture selection rejected");
    CommandArgs args;
    args.pattern = pattern;
    args.channel = channel;
    args.text = "duration";
    args.groove =
        GrooveTemplate{"Test groove", 1, {{0, .2, .9}, {.3125, .1, .5}, {.5, .2, .9}, {.8125, .1, .5}}};
    args.velocityStrength = .5;
    require(c.beginGesture("groove") && c.execute("notes.quantize", args, "groove").wasOk(),
            "Reusable groove previews in canonical owned transaction");
    for (size_t i = 0; i < 8; ++i)
    {
        const auto &note = c.project.patterns[0].notes[i];
        require(std::abs(note.beat - (i * .25 + (i % 2 ? .0625 : 0))) < 1.e-9 &&
                    note.id == before.patterns[0].notes[i].id && note.pitch == 60,
                "Repeated groove phase and source identities");
        require(std::abs(note.length - (i % 2 ? .1 : .2)) < 1.e-9 &&
                    std::abs(note.velocity - (i % 2 ? .65 : .85)) < 1.e-6,
                "Independent groove lengths and velocity blend");
    }
    args.groove->points[1].velocity = 0;
    require(c.execute("notes.quantize", args, "groove").wasOk() && c.project.patterns[0].notes.size() == 7,
            "Zero-velocity rests remove two repeated source notes");
    args.zeroVelocityDeletes = false;
    require(c.execute("notes.quantize", args, "groove").wasOk() && c.project.patterns[0].notes.size() == 9 &&
                c.project.patterns[0].notes[1].id == before.patterns[0].notes[1].id,
            "Turning rests off restores deleted baseline notes and vector order");
    args.groove.reset();
    args.velocityStrength = 0;
    require(c.execute("notes.quantize", args, "groove").wasOk() &&
                c.project.state().isEquivalentTo(before.state()),
            "Reset to uniform defaults restores eligible original grid score");
    c.cancelGesture("groove");
    require(!c.undo.canUndo() && !c.hasQuantizerPreview(),
            "Cancelled groove creates no Undo and drops preview cache");
    args.groove = factoryGrooves()[1];
    require(c.beginGesture("groove") && c.execute("notes.quantize", args, "groove").wasOk(),
            "Accepted groove preview");
    c.endGesture("Groove quantize", "groove");
    const auto after = c.project.state();
    c.undoEdit();
    require(c.project.state().isEquivalentTo(before.state()) && !c.undo.canUndo(), "Groove one exact Undo");
    c.redoEdit();
    require(c.project.state().isEquivalentTo(after), "Groove exact Redo");
    const auto guides = grooveGuidePositions(*args.groove, 3.8, 4.8);
    require(guides.size() == 4 && guides[0] == 3.8125 && guides[1] == 4 && guides[2] == 4.3125,
            "Guide lines share repeated template timing across loop boundary");
    require(grooveGuidePositions(*args.groove, 0, 65536, 2048).size() <= 2048,
            "Dense guide display is bounded/decimated without inventing positions");
    Customization profile;
    Macro macro{"macro.groove", "Apply groove", {{"notes.quantize", args, true}}};
    require(profile.addMacro(macro).wasOk(), "Canonical groove macro");
    Customization imported;
    require(imported.importState(profile.state()).wasOk() &&
                imported.macros[0].steps[0].parameters.groove.has_value(),
            "Groove preset survives macro profile round trip");
    c.replace(before);
    MacroContext context;
    context.pattern = pattern;
    context.channel = channel;
    require(imported.runMacro("macro.groove", c, context).wasOk(),
            "Groove macro supports active part without explicit selection");
    c.undoEdit();
    require(c.project.state().isEquivalentTo(before.state()), "Groove macro one exact Undo");
    require(int(profile.state()["version"]) == 2, "Groove profiles refuse unsafe legacy decoding");
    auto legacy = profile.state();
    legacy.getDynamicObject()->setProperty("version", 1);
    require(imported.importState(legacy).failed(), "Version1 cannot silently drop a groove macro");
    require(int(Customization::familiarDefaults().state()["version"]) == 1,
            "Ordinary profiles retain legacy compatibility");
    auto restArgs = args;
    restArgs.groove->points[0].velocity = 0;
    restArgs.zeroVelocityDeletes = true;
    CommandArgs move;
    move.beat = .25;
    move.length = 0;
    move.pitch = 0;
    Macro composite{
        "macro.restMove", "Rest then move", {{"notes.quantize", restArgs, true}, {"notes.move", move, true}}};
    Customization combined;
    require(combined.addMacro(composite).wasOk(), "Composite rest/move macro");
    c.replace(before);
    context.notes.clear();
    for (size_t i = 0; i < 8; ++i)
        context.notes.push_back(before.patterns[0].notes[i].id);
    require(combined.runMacro("macro.restMove", c, context).wasOk() && context.notes.size() == 7,
            "Composite macro reconciles deleted IDs before next command");
    c.undoEdit();
    require(c.project.state().isEquivalentTo(before.state()), "Composite rest/move macro exact Undo");
    const auto bom = folder.getChildFile("bom.vgroove");
    {
        auto stream = bom.createOutputStream();
        require(stream != nullptr, "BOM fixture output");
        stream->setPosition(0);
        stream->truncate();
        const unsigned char prefix[]{0xef, 0xbb, 0xbf};
        stream->write(prefix, 3);
        const auto text = juce::JSON::toString(grooveState(factoryGrooves()[0]));
        stream->write(text.toRawUTF8(), text.getNumBytesAsUTF8());
    }
    require(loadGroove(bom, retained).wasOk(), "Windows UTF8-BOM templates load through bounded decoder");
    require(quantizerEditAdmission(false, false, false, false, false).wasOk(), "Idle Quantizer admission");
    {
        Controller growth;
        auto &part = growth.project.patterns.front();
        part.length = 1;
        const auto target = growth.project.channels.front().id;
        const auto untouched = growth.project.channels[1].id;
        part.notes = {{growth.project.allocate(), target, 0, 1, 60, .7f, false},
                      {growth.project.allocate(), untouched, 0, 1, 48, .7f, false}};
        const auto original = growth.project.state();
        const auto unaffected = part.notes.back();
        CommandArgs end;
        end.pattern = part.id;
        end.channel = target;
        end.text = "end";
        end.groove = factoryGrooves()[1];
        require(growth.execute("notes.quantize", end).wasOk() &&
                    growth.project.patterns.front().length == 4 &&
                    growth.project.patterns.front().notes.front().length > 1 &&
                    growth.project.patterns.front().notes.back().id == unaffected.id &&
                    growth.project.patterns.front().notes.back().length == 1,
                "Groove end extension grows Pattern without inventing a longer untouched note");
        const auto extended = growth.project.state();
        growth.undoEdit();
        require(growth.project.state().isEquivalentTo(original), "Groove Pattern-growth exact Undo");
        growth.redoEdit();
        require(growth.project.state().isEquivalentTo(extended), "Groove Pattern-growth exact Redo");
    }
    require(quantizerEditAdmission(true, false, false, false, false).failed() &&
                quantizerEditAdmission(false, true, false, false, false).failed() &&
                quantizerEditAdmission(false, false, true, false, false).failed() &&
                quantizerEditAdmission(false, false, false, true, false).failed() &&
                quantizerEditAdmission(false, false, false, false, true).failed(),
            "UI/command/macro share recording and I/O admission");
}
