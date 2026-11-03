// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Commands.h"
#include "Customization.h"
#include "McpContracts.h"
#include <array>
#include <limits>

// Original model/schema/history/profile tests. No sampler/native audio claim.
static void samplerNoteModesModelTests(const juce::File &folder)
{
    using namespace vibera;
    const Note neutral;
    const Note oldAggregate{1, 2, 0, .5, 60, .8f, true};
    require(!neutral.slide && !neutral.portamento && neutral.colourGroup == 0 && !oldAggregate.slide &&
                !oldAggregate.portamento && oldAggregate.colourGroup == 0,
            "Appended Note modes preserve source-compatible neutral aggregates");
    auto source = Project::empty();
    source.channels.resize(2);
    auto &part = source.patterns.front();
    part.autoLength = false;
    part.length = 4;
    const auto pattern = part.id, channel = source.channels[0].id, other = source.channels[1].id;
    part.notes = {{source.allocate(), channel, 0, .5, 60, .8f, true, false, -.25, 25, 50, 0, .75, 0, 1},
                  {source.allocate(), channel, 0, .5, 64, 0.f, true, true},
                  {source.allocate(), other, 1, .25, 67, .5f, false}};
    source.placements = {{source.allocate(), pattern, 0, 8, 4, 0}};
    require(source.validate().wasOk(), "Original independent source chord/inactive/other Channel fixture");
    const auto baseline = source.state();
    const auto first = part.notes[0].id, second = part.notes[1].id, foreign = part.notes[2].id;
    Controller controller;
    CommandArgs args;
    args.pattern = pattern;
    args.channel = channel;
    args.ids = {first, second};
    args.enabled = true;
    args.value = 15;
    const std::array<const char *, 3> commands{"notes.slide", "notes.portamento", "notes.colourGroup"};
    for (int index = 0; index < 3; ++index)
    {
        controller.replace(source);
        auto expected = source;
        for (auto &note : expected.pattern(pattern)->notes)
            if (note.channel == channel)
            {
                if (index == 0)
                    note.slide = true;
                if (index == 1)
                    note.portamento = true;
                if (index == 2)
                    note.colourGroup = 15;
            }
        require(controller.execute(commands[size_t(index)], args).wasOk() &&
                    controller.project.state().isEquivalentTo(expected.state()),
                "Bulk canonical setter changes only its exact selected field, including inactive chord");
        const auto revision = controller.revision;
        require(controller.execute(commands[size_t(index)], args).wasOk() && controller.revision == revision,
                "Exact Note-mode no-op adds no revision/history");
        require(controller.undoEditChecked().wasOk() && controller.project.state().isEquivalentTo(baseline) &&
                    !controller.undo.canUndo() && controller.redoEditChecked().wasOk() &&
                    controller.project.state().isEquivalentTo(expected.state()),
                "Each setter is one exact Undo/Redo preserving source IDs and every unrelated field");
    }
    controller.replace(source);
    const juce::String owner = "original-note-modes";
    require(controller.beginGesture(owner), "Own real selection edit baseline");
    for (auto command : commands)
        require(controller.execute(command, args, owner).wasOk(), "Canonical modes share one owner");
    require(controller.cancelGestureChecked(owner).wasOk() &&
                controller.project.state().isEquivalentTo(baseline) && !controller.dirty &&
                !controller.undo.canUndo(),
            "Cancel restores source modes, allocator, dirty and history exactly");
    const auto refuse = [&](const juce::String &command, const CommandArgs &invalid)
    {
        const auto before = controller.project.state();
        const auto revision = controller.revision;
        const auto undo = controller.undo.canUndo(), redo = controller.undo.canRedo();
        const auto dirty = controller.dirty;
        require(controller.execute(command, invalid).failed() && controller.revision == revision &&
                    controller.project.state().isEquivalentTo(before) && controller.dirty == dirty &&
                    controller.undo.canUndo() == undo && controller.undo.canRedo() == redo,
                "Invalid Note mode refuses atomically with exact history/source/allocator");
    };
    for (auto command : commands)
    {
        auto invalid = args;
        invalid.ids.clear();
        refuse(command, invalid);
        invalid.ids = {first, first};
        refuse(command, invalid);
        invalid.ids = {first, foreign};
        refuse(command, invalid);
        invalid.ids = {first, 999999999};
        refuse(command, invalid);
        invalid = args;
        invalid.channel = other;
        refuse(command, invalid);
        invalid = args;
        invalid.pattern = 999999999;
        refuse(command, invalid);
    }
    for (double invalid :
         {-1., 15.5, 16., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
    {
        auto bad = args;
        bad.value = invalid;
        refuse("notes.colourGroup", bad);
    }
    auto repeated = source;
    repeated.pattern(pattern)->notes[1].repeatMode = 10;
    controller.replace(repeated);
    refuse("notes.slide", args); // The earlier selected Note cannot change first.
    auto slide = source;
    slide.pattern(pattern)->notes[0].slide = true;
    controller.replace(slide);
    auto repeat = args;
    repeat.ids = {first};
    repeat.text = "set";
    repeat.value = 10;
    refuse("steps.repeat", repeat); // Never clear Slide to fit Repeat or discard Repeat to fit Slide.
    controller.replace(source);
    auto create = args;
    create.ids.clear();
    create.beat = 2;
    create.length = .75;
    create.pitch = 72;
    create.value = .6;
    create.slide = create.portamento = true;
    create.colourGroup = 7;
    const auto allocation = source.nextId;
    const auto revision = controller.revision;
    require(controller.execute("notes.add", create).wasOk() && controller.revision == revision + 1,
            "Creation admits both independent flags atomically in one document transaction");
    const auto &created = controller.project.pattern(pattern)->notes.back();
    require(created.id == allocation && created.slide && created.portamento && created.colourGroup == 7 &&
                created.beat == 2 && created.length == .75 && created.pitch == 72,
            "No transient ordinary source Note exists before silent-Slide metadata");
    require(controller.undoEditChecked().wasOk() && controller.project.state().isEquivalentTo(baseline),
            "Atomic Note creation has exact Undo");
    auto foreignField = args;
    foreignField.slide = true;
    refuse("notes.velocity", foreignField);
    juce::PluginDescription native;
    native.name = "Original absent note-mode instrument";
    native.pluginFormatName = "VST3";
    native.fileOrIdentifier = "original-note-mode-model.vst3";
    native.isInstrument = true;
    native.numOutputChannels = 2;
    auto hosted = source;
    hosted.channel(channel)->instrument =
        PluginSpec{hosted.allocate(), native.name, native.createXml()->toString(), {}, true};
    controller.replace(hosted);
    refuse("notes.slide", args);
    refuse("notes.portamento", args);
    refuse("notes.add", create);
    require(controller.execute("notes.colourGroup", args).wasOk(),
            "Native MIDI instrument source notes retain editable sixteen colour groups");
    auto dormant = slide;
    dormant.channel(channel)->instrument = hosted.channel(channel)->instrument;
    dormant.channel(channel)->instrument->id = dormant.allocate();
    require(dormant.validate().wasOk() && dormant.pattern(pattern)->notes[0].slide,
            "Later hosted role preserves stored sampler flags as dormant metadata");

    auto configured = source;
    configured.pattern(pattern)->notes[0].slide = configured.pattern(pattern)->notes[0].portamento = true;
    configured.pattern(pattern)->notes[0].colourGroup = 15;
    configured.pattern(pattern)->notes[1].colourGroup = 4;
    const auto state = configured.state();
    Project loaded;
    require(Project::parse(state, loaded).wasOk() && loaded.state().isEquivalentTo(state),
            "Sparse schema41 preserves independent modes and all source properties");
    const auto file = folder.getChildFile("sampler-note-modes.vibra");
    require(saveProject(configured, file).wasOk() && loadProject(file, loaded).wasOk() &&
                loaded.state().isEquivalentTo(state),
            "Actual project file roundtrip retains source identity");
    const auto firstTree = [](juce::ValueTree tree)
    { return tree.getChildWithName("PATTERNS").getChild(0).getChildWithName("NOTE"); };
    auto legacy = source.state();
    legacy.setProperty("version", 40, nullptr);
    require(!firstTree(legacy).hasProperty("slide") && !firstTree(legacy).hasProperty("portamento") &&
                !firstTree(legacy).hasProperty("colourGroup") && Project::parse(legacy, loaded).wasOk() &&
                loaded.state().isEquivalentTo(baseline),
            "Genuine schema40 absence migrates exact neutral modes");
    const auto destination = loaded.state();
    for (const auto *field : {"slide", "portamento", "colourGroup"})
    {
        auto smuggled = legacy.createCopy();
        firstTree(smuggled).setProperty(
            field, juce::String(field) == "colourGroup" ? juce::var(0) : juce::var(false), nullptr);
        require(Project::parse(smuggled, loaded).failed() && loaded.state().isEquivalentTo(destination),
                "Old schema rejects even neutral new-field presence without changing destination");
        const bool group = juce::String(field) == "colourGroup";
        const std::array<juce::var, 4> invalid =
            group ? std::array<juce::var, 4>{juce::var(true), juce::var(-1), juce::var(16), juce::var(1.5)}
                  : std::array<juce::var, 4>{juce::var(1), juce::var("true"), juce::var("false"),
                                             juce::var(.5)};
        for (const auto &value : invalid)
        {
            auto malformed = state.createCopy();
            firstTree(malformed).setProperty(field, value, nullptr);
            require(Project::parse(malformed, loaded).failed() && loaded.state().isEquivalentTo(destination),
                    "Exact mode types/ranges reject coercion atomically");
        }
    }
    auto conflict = state.createCopy();
    firstTree(conflict).setProperty("repeatMode", 10, nullptr);
    require(Project::parse(conflict, loaded).failed() && loaded.state().isEquivalentTo(destination),
            "Persisted Slide/Repeat conflict refuses without rewriting source state");
    auto duplicate = state.createCopy();
    duplicate.getChildWithName("PATTERNS")
        .getChild(0)
        .appendChild(firstTree(duplicate).createCopy(), nullptr);
    require(Project::parse(duplicate, loaded).failed() && loaded.state().isEquivalentTo(destination),
            "Duplicate source-note identities remain forbidden");

    controller.replace(configured);
    auto copy = args;
    copy.ids = {first};
    copy.beat = 1;
    require(controller.execute("notes.duplicate", copy).wasOk(), "Real source duplication");
    auto survivor = controller.project.pattern(pattern)->notes.back();
    require(survivor.id != first && survivor.slide && survivor.portamento && survivor.colourGroup == 15 &&
                survivor.modX == 0 && survivor.modY == 1 && survivor.releaseVelocity == .75,
            "Duplicate retains modes and every pre-existing expression field with fresh identity");
    controller.replace(configured);
    copy.beat = 0;
    require(controller.execute("pattern.clone", copy).wasOk() &&
                controller.project.patterns[1].notes.front().slide &&
                controller.project.patterns[1].notes.front().portamento &&
                controller.project.patterns[1].notes.front().colourGroup == 15,
            "Pattern clone retains modes while assigning fresh Pattern/Note IDs");
    auto glue = source;
    glue.pattern(pattern)->notes = {part.notes[0], part.notes[0]};
    glue.pattern(pattern)->notes[0].shiftPercent = 0;
    glue.pattern(pattern)->notes[1] = glue.pattern(pattern)->notes[0];
    glue.pattern(pattern)->notes[1].id = glue.allocate();
    glue.pattern(pattern)->notes[1].beat = .5;
    for (int field = 0; field < 3; ++field)
    {
        auto different = glue;
        auto &right = different.pattern(pattern)->notes[1];
        if (field == 0)
            right.slide = true;
        if (field == 1)
            right.portamento = true;
        if (field == 2)
            right.colourGroup = 3;
        controller.replace(different);
        auto join = args;
        join.ids = {different.pattern(pattern)->notes[0].id, right.id};
        const auto unchanged = different.state();
        require(controller.execute("notes.glue", join).wasOk() &&
                    controller.project.state().isEquivalentTo(unchanged) && !controller.undo.canUndo(),
                "Glue never silently merges mismatched mode/group metadata");
    }

    controller.replace(source);
    auto properties = args;
    NotePropertiesPatch patch;
    patch.slide = patch.portamento = true;
    patch.colourGroup = 6;
    patch.velocity = .25;
    patch.pan = .5;
    patch.fineCents = -50;
    patch.modX = .75;
    patch.modY = .25;
    patch.releaseVelocity = .5;
    patch.muted = false;
    properties.noteProperties = patch;
    require(controller.execute("notes.properties", properties).wasOk(),
            "One typed Properties edit handles melodic/step source levels and modes together");
    auto expectedProperties = source;
    for (auto &note : expectedProperties.pattern(pattern)->notes)
        if (note.channel == channel)
        {
            note.slide = note.portamento = true;
            note.colourGroup = 6;
            note.velocity = .25f;
            note.pan = .5;
            note.fineCents = -50;
            note.modX = .75;
            note.modY = .25;
            note.releaseVelocity = .5;
            note.muted = false;
        }
    const auto appliedProperties = controller.project.state();
    const auto propertiesRevision = controller.revision;
    require(appliedProperties.isEquivalentTo(expectedProperties.state()) &&
                controller.execute("notes.properties", properties).wasOk() &&
                controller.revision == propertiesRevision && controller.undoEditChecked().wasOk() &&
                controller.project.state().isEquivalentTo(baseline) && !controller.undo.canUndo() &&
                controller.redoEditChecked().wasOk() &&
                controller.project.state().isEquivalentTo(appliedProperties),
            "Properties is one exact transaction/no-op/Undo/Redo, no unrelated source metadata lost");
    controller.replace(source);
    auto invalidProperties = properties;
    invalidProperties.noteProperties->beat = 0;
    refuse("notes.properties", invalidProperties); // Multi timing is forbidden even when equal.
    invalidProperties = properties;
    invalidProperties.noteProperties = NotePropertiesPatch{};
    refuse("notes.properties", invalidProperties);
    invalidProperties = args;
    refuse("notes.properties", invalidProperties);
    invalidProperties = properties;
    invalidProperties.ids = {first, foreign};
    refuse("notes.properties", invalidProperties);
    invalidProperties = properties;
    refuse("notes.velocity", invalidProperties);
    auto single = properties;
    single.ids = {first};
    single.noteProperties = NotePropertiesPatch{};
    single.noteProperties->beat = 6;
    single.noteProperties->length = .75;
    single.noteProperties->pitch = 72;
    require(controller.execute("notes.properties", single).wasOk(),
            "Manual-window dormant timing stays legal");
    const auto &timed = controller.project.pattern(pattern)->notes.front();
    require(timed.id == first && timed.beat == 6 && timed.length == .75 && timed.pitch == 72 &&
                timed.shiftPercent == 50 && controller.project.pattern(pattern)->length == 4 &&
                controller.undoEditChecked().wasOk() && controller.project.state().isEquivalentTo(baseline),
            "Absolute timing preserves raw Shift, fixed Pattern window and every other field");
    require(controller.beginGesture(owner) &&
                controller.execute("notes.properties", properties, owner).wasOk() &&
                controller.cancelGestureChecked(owner).wasOk() &&
                controller.project.state().isEquivalentTo(baseline) && !controller.undo.canUndo(),
            "Properties preview Cancel uses the actual canonical baseline");
    controller.replace(hosted);
    refuse("notes.properties", properties);
    auto hostedCommon = properties;
    hostedCommon.noteProperties = NotePropertiesPatch{};
    hostedCommon.noteProperties->colourGroup = 12;
    hostedCommon.noteProperties->releaseVelocity = .75;
    hostedCommon.noteProperties->velocity = .5;
    require(controller.execute("notes.properties", hostedCommon).wasOk(),
            "Hosted notes accept ordinary MIDI/colour Properties without sampler parameter promises");
    const auto wirePatch = notePropertiesState(patch);
    NotePropertiesPatch decoded;
    require(decodeNotePropertiesPatch(wirePatch, decoded).wasOk() &&
                juce::JSON::toString(notePropertiesState(decoded)) == juce::JSON::toString(wirePatch),
            "Shared strict decoder roundtrips sparse typed Properties exactly");
    const auto decodedBefore = juce::JSON::toString(notePropertiesState(decoded));
    for (const auto &badField : std::array<std::pair<const char *, juce::var>, 8>{
             {{"slide", juce::var(1)},
              {"colourGroup", juce::var(1.5)},
              {"velocity", juce::var(true)},
              {"pan", juce::var("0")},
              {"fineCents", juce::var(101)},
              {"length", juce::var(0)},
              {"modX", juce::var(std::numeric_limits<double>::infinity())},
              {"unknown", juce::var(0)}}})
    {
        auto malformed = juce::JSON::parse(juce::JSON::toString(wirePatch));
        malformed.getDynamicObject()->setProperty(badField.first, badField.second);
        require(decodeNotePropertiesPatch(malformed, decoded).failed() &&
                    juce::JSON::toString(notePropertiesState(decoded)) == decodedBefore,
                "Malformed Properties type/range/unknown field leaves prior decoded output untouched");
    }

    const auto clone = [](const juce::var &v) { return juce::JSON::parse(juce::JSON::toString(v)); };
    const auto same = [](const juce::var &a, const juce::var &b)
    { return juce::JSON::toString(a) == juce::JSON::toString(b); };
    auto settings = Customization::familiarDefaults();
    require(settings.resolve(juce::KeyPress('S'), Scope::piano) == "piano.insertSlide" &&
                settings.resolve(juce::KeyPress('O'), Scope::piano) == "piano.insertPortamento",
            "Familiar contextual insertion commands are authoritative bindings");
    Macro macro{"macro.original-note-modes", "Create exact source modes", {{"notes.add", create, false}}};
    require(settings.addMacro(macro).wasOk() && int(settings.state()["version"]) == 18,
            "Nonneutral creation fields require truthful profile18");
    Customization restored;
    const auto profile = folder.getChildFile("sampler-note-modes-profile.json");
    require(settings.save(profile).wasOk() && restored.load(profile).wasOk() &&
                same(settings.state(), restored.state()),
            "Actual creation macro file preserves all three fields");
    controller.replace(source);
    MacroContext context;
    require(restored.runMacro(macro.id, controller, context).wasOk() &&
                controller.project.pattern(pattern)->notes.back().slide &&
                controller.project.pattern(pattern)->notes.back().portamento &&
                controller.project.pattern(pattern)->notes.back().colourGroup == 7 &&
                controller.undoEditChecked().wasOk() && controller.project.state().isEquivalentTo(baseline),
            "Real macro runner creates flags atomically and owns one exact Undo");
    const auto creation = settings.state();
    for (int version = 1; version <= 17; ++version)
    {
        auto old = clone(creation);
        old.getDynamicObject()->setProperty("version", version);
        const auto previous = restored.state();
        require(restored.importState(old).failed() && same(previous, restored.state()),
                "All genuine prior versions refuse new creation field presence atomically");
    }
    settings = Customization::familiarDefaults();
    Macro propertyMacro{
        "macro.original-note-properties", "Properties one edit", {{"notes.properties", properties, false}}};
    require(settings.addMacro(propertyMacro).wasOk() && int(settings.state()["version"]) == 18 &&
                settings.save(profile).wasOk() && restored.load(profile).wasOk(),
            "Typed sparse aggregate Properties requires18 and survives actual file roundtrip");
    controller.replace(source);
    require(restored.runMacro(propertyMacro.id, controller, context).wasOk() &&
                controller.project.state().isEquivalentTo(appliedProperties) &&
                controller.undoEditChecked().wasOk() && controller.project.state().isEquivalentTo(baseline) &&
                !controller.undo.canUndo(),
            "Real Properties macro applies the same atomic candidate and owns one exact Undo");
    auto oldProperties = clone(settings.state());
    oldProperties.getDynamicObject()->setProperty("version", 17);
    const auto beforeOldProperties = restored.state();
    require(restored.importState(oldProperties).failed() && same(beforeOldProperties, restored.state()),
            "Older17 cannot silently discard a new sparse Properties payload");
    CommandArgs imported;
    imported.pattern = pattern;
    imported.channel = channel;
    imported.recordedNotes = {configured.pattern(pattern)->notes.front(),
                              configured.pattern(pattern)->notes.front()};
    imported.recordedNotes[1].colourGroup = 5; // Same inert metadata ID, distinct fresh runtime IDs.
    macro = {"macro.original-note-mode-import", "Import source modes", {{"notes.import", imported, false}}};
    settings = Customization::familiarDefaults();
    require(settings.addMacro(macro).wasOk() && settings.save(profile).wasOk() &&
                restored.load(profile).wasOk(),
            "Full imported note payload preserves both flags, colour and earlier expression through file");
    const auto encoded = settings.state();
    controller.replace(source);
    const auto nextID = controller.project.nextId;
    require(restored.runMacro(macro.id, controller, context).wasOk(), "Actual full imported mode macro runs");
    const auto &notes = controller.project.pattern(pattern)->notes;
    require(notes[3].id == nextID && notes[4].id == nextID + 1 && notes[3].slide && notes[3].portamento &&
                notes[3].colourGroup == 15 && notes[4].colourGroup == 5 && notes[3].channel == channel &&
                !notes[3].step && notes[3].modX == 0 && notes[3].modY == 1 && notes[3].releaseVelocity == .75,
            "Import preserves all payload modes/expression and fresh-ID/target-Channel semantics");
    const auto importedState = controller.project.state();
    require(controller.undoEditChecked().wasOk() && controller.project.state().isEquivalentTo(baseline) &&
                controller.redoEditChecked().wasOk() &&
                controller.project.state().isEquivalentTo(importedState),
            "Full payload import one exact Undo/Redo");
    const auto payload = [](juce::var profile)
    {
        return profile["macros"].getArray()->getReference(0)["steps"].getArray()->getReference(
            0)["parameters"];
    };
    for (const auto *field : {"slide", "portamento", "colourGroup"})
    {
        auto old = clone(encoded);
        old.getDynamicObject()->setProperty("version", 17);
        auto rows = payload(old)["recordedNotes"];
        for (auto &row : *rows.getArray())
        {
            row.getDynamicObject()->removeProperty("slide");
            row.getDynamicObject()->removeProperty("portamento");
            row.getDynamicObject()->removeProperty("colourGroup");
        }
        rows.getArray()->getReference(0).getDynamicObject()->setProperty(
            field, juce::String(field) == "colourGroup" ? juce::var(0) : juce::var(false));
        const auto beforeImport = restored.state();
        require(restored.importState(old).failed() && same(beforeImport, restored.state()),
                "Prior profile17 refuses even neutral new imported field presence");
    }
    auto oldNeutral = clone(encoded);
    oldNeutral.getDynamicObject()->setProperty("version", 17);
    for (auto &row : *payload(oldNeutral)["recordedNotes"].getArray())
    {
        row.getDynamicObject()->removeProperty("slide");
        row.getDynamicObject()->removeProperty("portamento");
        row.getDynamicObject()->removeProperty("colourGroup");
    }
    require(restored.importState(oldNeutral).wasOk() &&
                !restored.macros.front().steps.front().parameters.recordedNotes.front().slide &&
                !restored.macros.front().steps.front().parameters.recordedNotes.front().portamento &&
                restored.macros.front().steps.front().parameters.recordedNotes.front().colourGroup == 0,
            "Genuine profile17 absent fields migrate neutral without discarding earlier payload");
    auto future = clone(encoded);
    future.getDynamicObject()->setProperty("version", 19);
    const auto beforeFuture = restored.state();
    require(restored.importState(future).failed() && same(beforeFuture, restored.state()),
            "Declared profile18 authority explicitly refuses future19");
    auto unsafe = macro;
    unsafe.steps.front().parameters.recordedNotes.front().repeatMode = 10;
    require(restored.addMacro(unsafe).failed(), "Slide/Repeat malformed payload refuses at macro add");
    restored.macros = {unsafe};
    controller.replace(source);
    juce::MemoryBlock saved;
    require(profile.loadFileAsData(saved) && restored.runMacro(unsafe.id, controller, context).failed() &&
                controller.project.state().isEquivalentTo(baseline) && !controller.undo.canUndo() &&
                restored.save(profile).failed(),
            "Unsafe public macro storage cannot run or overwrite saved profile");
    juce::MemoryBlock after;
    require(profile.loadFileAsData(after) && saved == after,
            "Failed save retains exact previous profile bytes");
    for (const auto *command : commands)
    {
        const auto schema = commandArgumentSchema(command);
        require(!bool(schema["additionalProperties"]) && schema["required"].size() == 4 &&
                    int(schema["properties"]["ids"]["minItems"]) == 1,
                "Strict canonical wire schema requires explicit stable target set and typed edit");
    }
}
