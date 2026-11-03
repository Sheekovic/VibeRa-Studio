// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Customization.h"
#include <limits>

// Original actual settings/Controller/file tests, not UI or audio qualification.
static void importedNoteMacroTests(const juce::File &folder)
{
    using namespace vibera;
    auto project = Project::empty();
    auto &pattern = project.patterns.front();
    pattern.autoLength = false;
    pattern.length = 4;
    const auto target = project.channels[0].id, other = project.channels[1].id, patternId = pattern.id;
    pattern.notes = {{project.allocate(), target, 0, .25, 48, .5f},
                     {project.allocate(), other, .5, .5, 55, .7f}};
    CommandArgs importArgs;
    importArgs.pattern = patternId;
    importArgs.channel = target;
    importArgs.recordedNotes = {{123456789, other, .125, .75, 65, .31f, true, true, -.375, 53.272943230144},
                                {0, target, 1.5, .5, 72, 0, false, false, .625, -53.272943230144}};
    Macro macro{
        "macro.original-note-import", "Original imported score", {{"notes.import", importArgs, false}}};
    auto settings = Customization::familiarDefaults();
    require(settings.addMacro(macro).wasOk(),
            "Actual notes.import macro accepts typed source payload without selected IDs");
    const auto encoded = settings.state();
    require(int(encoded["version"]) == 12 && !macro.steps.front().parameters.recordedNotes.empty() &&
                macro.steps.front().parameters.ids.empty(),
            "Only a nonempty imported payload requires profile12, independently of Piano selection");
    const auto same = [](const juce::var &a, const juce::var &b)
    { return juce::JSON::toString(a) == juce::JSON::toString(b); };
    const auto clone = [](const juce::var &v) { return juce::JSON::parse(juce::JSON::toString(v)); };
    const auto parameters = [&](juce::var &profile) -> juce::DynamicObject *
    {
        for (auto &row : *profile["macros"].getArray())
            if (row["id"].toString() == macro.id)
                return row["steps"].getArray()->getReference(0)["parameters"].getDynamicObject();
        require(false, "Locate exact authored imported-note macro");
        return nullptr;
    };
    auto inspected = clone(encoded);
    auto *notes = parameters(inspected)->getProperty("recordedNotes").getArray();
    require(
        notes && notes->size() == 2 &&
            notes->getReference(0).getDynamicObject()->getProperties().size() == 10 &&
            juce::int64(notes->getReference(0)["id"]) == 123456789 &&
            juce::int64(notes->getReference(1)["id"]) == 0 && bool(notes->getReference(0)["muted"]),
        "Profile serialization retains all10 note fields, original identities and muted/zero source data");
    Customization reloaded;
    require(
        reloaded.importState(inspected).wasOk() && same(reloaded.state(), encoded),
        "Full typed imported-note JSON round trip is exact, including float velocities and pan/fine doubles");
    const auto file = folder.getChildFile("imported-note-macro-profile.json");
    require(settings.save(file).wasOk() && reloaded.load(file).wasOk() && same(reloaded.state(), encoded),
            "Real profile file save/load preserves complete imported note payload");
    juce::MemoryBlock savedBytes;
    require(file.loadFileAsData(savedBytes), "Read actual saved profile bytes before refusal fixtures");
    const auto savedUnchanged = [&]
    {
        juce::MemoryBlock actual;
        return file.loadFileAsData(actual) && actual == savedBytes;
    };
    Controller controller;
    controller.replace(project);
    MacroContext context;
    context.pattern = patternId;
    context.channel = target;
    const auto before = controller.project.state();
    const auto allocation = controller.project.nextId;
    require(reloaded.runMacro(macro.id, controller, context).wasOk() && !controller.hasGesture() &&
                !reloaded.running,
            "Real imported-note macro runs with empty selected Note IDs");
    auto expected = project;
    for (auto note : importArgs.recordedNotes)
    {
        note.id = expected.allocate();
        note.channel = target;
        note.step = false;
        expected.pattern(patternId)->notes.push_back(note);
    }
    require(controller.project.state().isEquivalentTo(expected.state()) &&
                controller.project.nextId == allocation + 2 &&
                controller.project.pattern(patternId)->notes[2].id == allocation &&
                controller.project.pattern(patternId)->notes[3].id == allocation + 1 &&
                same(reloaded.state(), encoded),
            "Canonical import preserves source properties but allocates fresh runtime IDs/target Channel and "
            "clears step");
    const auto after = controller.project.state();
    require(controller.undoEditChecked().wasOk() && controller.project.state().isEquivalentTo(before) &&
                !controller.undo.canUndo() && controller.redoEditChecked().wasOk() &&
                controller.project.state().isEquivalentTo(after) && !controller.undo.canRedo(),
            "Actual import macro has exactly one source/allocator-exact Undo/Redo");
    auto repeatedMetadata = clone(encoded);
    auto *repeatedRows = parameters(repeatedMetadata)->getProperty("recordedNotes").getArray();
    repeatedRows->getReference(1).getDynamicObject()->setProperty("id", juce::int64(123456789));
    Customization repeatedSettings;
    require(repeatedSettings.importState(repeatedMetadata).wasOk() &&
                same(repeatedSettings.state(), repeatedMetadata),
            "Repeated source identities remain inert metadata through exact profile round trip");
    controller.replace(project);
    require(repeatedSettings.runMacro(macro.id, controller, context).wasOk() &&
                controller.project.state().isEquivalentTo(expected.state()) &&
                controller.project.pattern(patternId)->notes[2].id == allocation &&
                controller.project.pattern(patternId)->notes[3].id == allocation + 1 &&
                same(repeatedSettings.state(), repeatedMetadata) && controller.undoEditChecked().wasOk() &&
                controller.project.state().isEquivalentTo(before) && !controller.undo.canUndo(),
            "Repeated metadata IDs create distinct fresh runtime IDs without changing recipe or exact Undo");
    const auto refuseProfile = [&](const juce::var &bad)
    {
        const auto existing = reloaded.state();
        require(reloaded.importState(bad).failed() && same(reloaded.state(), existing),
                "Malformed/unsupported imported-note profile never replaces current settings");
    };
    for (int version = 1; version <= 11; ++version)
    {
        auto old = clone(encoded);
        old.getDynamicObject()->setProperty("version", version);
        refuseProfile(old);
        auto validOld = Customization::familiarDefaults().state();
        validOld.getDynamicObject()->setProperty("version", version);
        Customization migrated;
        require(migrated.importState(validOld).wasOk(),
                "Previous supported profile versions without payload remain readable");
    }
    for (int fault = 0; fault < 24; ++fault)
    {
        auto bad = clone(encoded);
        auto *args = parameters(bad);
        auto *rows = args->getProperty("recordedNotes").getArray();
        auto *row = rows->getReference(0).getDynamicObject();
        if (fault == 0)
            row->removeProperty("channel");
        if (fault == 1)
            row->setProperty("unknown", 1);
        if (fault == 2)
            row->setProperty("id", -1);
        if (fault == 3)
            row->setProperty("id", juce::int64(1000000001));
        if (fault == 4)
            row->setProperty("channel", 0);
        if (fault == 5)
            row->setProperty("channel", "2");
        if (fault == 6)
            row->setProperty("beat", -.25);
        if (fault == 7)
            row->setProperty("beat", true);
        if (fault == 8)
            row->setProperty("length", 0);
        if (fault == 9)
            row->setProperty("pitch", 60.5);
        if (fault == 10)
            row->setProperty("velocity", 1.01);
        if (fault == 11)
            row->setProperty("velocity", true);
        if (fault == 12)
            row->setProperty("step", 1);
        if (fault == 13)
            row->setProperty("muted", "false");
        if (fault == 14)
            row->setProperty("pan", 1.1);
        if (fault == 15)
            row->setProperty("fineCents", 100.01);
        if (fault == 16)
            row->setProperty("fineCents", "0");
        if (fault == 17)
            row->setProperty("pan", std::numeric_limits<double>::quiet_NaN());
        if (fault == 18)
            args->setProperty("recordedNotes", juce::Array<juce::var>{});
        if (fault == 19)
            args->setProperty("recordedNotes", "not an array");
        if (fault == 20)
            args->removeProperty("recordedNotes");
        if (fault == 21)
            row->setProperty("channel", juce::int64(1000000001));
        if (fault == 22)
            row->setProperty("id", .5);
        if (fault == 23)
            for (auto &entry : *bad["macros"].getArray())
                if (entry["id"].toString() == macro.id)
                    entry["steps"].getArray()->getReference(0).getDynamicObject()->setProperty(
                        "action", "notes.record");
        refuseProfile(bad);
    }
    auto sparse = clone(encoded);
    auto *sparseRows = parameters(sparse)->getProperty("recordedNotes").getArray();
    for (auto &row : *sparseRows)
        for (const auto *optional : {"id", "step", "muted", "pan", "fineCents"})
            row.getDynamicObject()->removeProperty(optional);
    Customization sparseSettings;
    require(sparseSettings.importState(sparse).wasOk(),
            "Canonical sparse note input uses the same explicit defaults in profile12");
    auto sparseSaved = sparseSettings.state();
    auto *sparseSavedRows = parameters(sparseSaved)->getProperty("recordedNotes").getArray();
    require(sparseSavedRows->getReference(0).getDynamicObject()->getProperties().size() == 10 &&
                juce::int64(sparseSavedRows->getReference(0)["id"]) == 0 &&
                !bool(sparseSavedRows->getReference(0)["step"]) &&
                !bool(sparseSavedRows->getReference(0)["muted"]) &&
                double(sparseSavedRows->getReference(0)["pan"]) == 0 &&
                double(sparseSavedRows->getReference(0)["fineCents"]) == 0,
            "Sparse canonical input is durably expanded to the complete neutral typed source record");
    auto unsafe = reloaded;
    auto badMacro = macro;
    badMacro.id = "macro.invalid-import-payload";
    badMacro.steps.front().action = "notes.add";
    const auto profileBefore = unsafe.state();
    require(unsafe.addMacro(badMacro).failed() && same(unsafe.state(), profileBefore),
            "Unrelated macro command cannot accept an imported payload at admission");
    badMacro.steps.insert(badMacro.steps.begin(), {"project.tempo", {}, false});
    badMacro.steps.front().parameters.value = 137;
    unsafe.macros = {badMacro}; // Explicitly corrupted public storage, bypassing normal add/import.
    controller.replace(project);
    const auto unsafeBefore = controller.project.state();
    require(unsafe.runMacro(badMacro.id, controller, context).failed() &&
                controller.project.state().isEquivalentTo(unsafeBefore) && !controller.hasGesture() &&
                !controller.undo.canUndo() && !unsafe.running && unsafe.save(file).failed() &&
                savedUnchanged(),
            "Run/save preflight refuses unsafe public storage before earlier edits, ownership or persisted "
            "bytes change");
    badMacro = macro;
    badMacro.steps.front().parameters.recordedNotes.assign(100001, importArgs.recordedNotes.front());
    require(unsafe.addMacro(badMacro).failed(),
            "Payload cardinality above100000 refuses before macro publication");
    auto large = macro;
    large.steps.front().parameters.recordedNotes.assign(6000, importArgs.recordedNotes.front());
    for (auto &note : large.steps.front().parameters.recordedNotes)
        note.id = 0;
    Customization oversized;
    require(oversized.addMacro(large).wasOk() &&
                juce::JSON::toString(oversized.state()).getNumBytesAsUTF8() > 1024 * 1024 &&
                oversized.save(file).failed() && savedUnchanged(),
            "Valid model payload cannot overwrite a profile with bytes beyond the actual1MiB load limit");
    refuseProfile(oversized.state());
}
