// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "LegacySamplerVoices.h"
#include "StepExpression.h"
#include "Customization.h"
#include <limits>

// Original model/profile tests only; root owns actual audio/UI/MCP qualification.
static void samplerModulationModelTests(const juce::File &folder)
{
    using namespace vibera;
    auto source = Project::empty();
    auto &part = source.patterns.front();
    part.autoLength = false;
    part.length = 4;
    const auto pattern = part.id, channel = source.channels.front().id;
    const double beats[] = {0, .25, .25, .5};
    const double originalValues[] = {0, .25, .75, 1};
    for (size_t i = 0; i < 4; ++i)
    {
        part.notes.push_back(
            {source.allocate(), channel, beats[i], .5, 60 + int(i), i == 2 ? 0.f : .8f, true, i == 1});
        part.notes.back().modX = originalValues[i];
        part.notes.back().modY = 1 - originalValues[i];
    }
    part.notes.push_back({source.allocate(), channel, 4, .25, 72, .5f, true});   // Dormant.
    part.notes.push_back({source.allocate(), channel, .5, .25, 74, .5f, false}); // Melodic.
    part.channelLoops = {{channel, ChannelLoopMode::fixed, 1}};
    require(source.validate().wasOk(), "Independent Mod X/Y source with chords/inactive data validates");
    const Note oldNote{1, channel, 0, .25, 60, .8f, true};
    const ScheduledNote oldEvent{channel, 0, .25, 60, .8f};
    require(oldNote.modX == .5 && oldNote.modY == .5 && oldEvent.modX == .5 && oldEvent.modY == .5,
            "Existing aggregate initializers default Mod X/Y to neutral .5");
    const auto before = source.state();
    for (const auto property : {StepExpression::modX, StepExpression::modY})
    {
        const auto *info = stepExpressionInfo(property);
        require(info && info->minimum == 0 && info->maximum == 1 && info->defaultValue == .5 &&
                    juce::String(info->units) == "normalized",
                "Authoritative Mod X/Y metadata distinguishes nonnegative range and centered default");
        auto model = source;
        for (size_t i = 0; i < 4; ++i)
            require(setStepExpressionValue(model.pattern(pattern)->notes[i], property, originalValues[i]),
                    "Typed expression setter accepts literal original values");
        const auto opening = model.state();
        const auto literal = [&](const Project &actual, std::initializer_list<double> values)
        {
            size_t index = 0;
            for (auto expected : values)
                require(stepExpressionValue(actual.pattern(pattern)->notes[index++], property) == expected,
                        "Modulation edit matches literal independent chord values");
            auto restored = actual;
            for (size_t i = 0; i < 4; ++i)
                require(
                    setStepExpressionValue(restored.pattern(pattern)->notes[i], property, originalValues[i]),
                    "Restore only tested property for preservation comparison");
            require(restored.state().isEquivalentTo(opening),
                    "Graph edits retain IDs/all other fields/dormant melodic notes/loops/allocator");
        };
        Controller controller;
        controller.replace(model);
        CommandArgs args;
        args.pattern = pattern;
        args.channel = channel;
        args.value = .5;
        require(controller.execute(info->command, args).wasOk(),
                "Canonical Mod X/Y set is an actual Controller edit");
        literal(controller.project, {.5, .5, .5, .5});
        const auto set = controller.project.state();
        const auto revision = controller.revision;
        require(controller.execute(info->command, args).wasOk() && controller.revision == revision,
                "Identical modulation set is a real no-op");
        require(controller.undoEditChecked().wasOk() && controller.project.state().isEquivalentTo(opening) &&
                    !controller.undo.canUndo() && controller.redoEditChecked().wasOk() &&
                    controller.project.state().isEquivalentTo(set) && !controller.undo.canRedo(),
                "One modulation edit has exact one-action Undo/Redo");
        controller.replace(model);
        const juce::String owner = "step-modulation:original-model";
        require(controller.beginGesture(owner), "Acquire immutable source baseline for actual Graph preview");
        args.text = "scale";
        args.target = model.pattern(pattern)->notes.front().id;
        args.value = .5;
        require(controller.execute(info->command, args, owner).wasOk(),
                "Mod X/Y scale translates from baseline anchor");
        literal(controller.project, {.5, .75, 1, 1});
        args.value = 0;
        require(controller.execute(info->command, args, owner).wasOk(),
                "Repeated scale restores unclamped original baseline");
        literal(controller.project, {0, .25, .75, 1});
        args.text = "ramp";
        args.beat = .5;
        args.length = -.5;
        args.value = 1;
        args.offset = 0;
        require(controller.execute(info->command, args, owner).wasOk(),
                "Reverse modulation ramp spans complete chord columns");
        literal(controller.project, {0, .5, .5, 1});
        args.beat = .25;
        args.length = 0;
        args.offset = .125;
        require(controller.execute(info->command, args, owner).wasOk(),
                "Zero-span ramp edits chord from original baseline");
        literal(controller.project, {0, .125, .125, 1});
        require(controller.cancelGestureChecked(owner).wasOk() &&
                    controller.project.state().isEquivalentTo(opening) && !controller.undo.canUndo(),
                "Cancel restores exact musical score and no history");
        args.text = "reset";
        args.value = std::numeric_limits<double>::quiet_NaN();
        require(controller.execute(info->command, args).wasOk(), "Centered reset ignores unused value");
        literal(controller.project, {.5, .5, .5, .5});
        for (double invalid :
             {-.01, 1.01, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()})
        {
            auto candidate = model;
            args.text = "set";
            args.value = invalid;
            require(applyStepExpression(candidate, model, args, property).failed() &&
                        candidate.state().isEquivalentTo(opening),
                    "Invalid direct edit is atomic");
            auto note = candidate.pattern(pattern)->notes.front();
            require(!setStepExpressionValue(note, property, invalid) &&
                        stepExpressionValue(note, property) == originalValues[0],
                    "Typed setter refuses invalid values atomically");
        }
        args.value = .5;
        auto invalidTargets = model;
        args.ids = {model.pattern(pattern)->notes.front().id, 999999999};
        require(applyStepExpression(invalidTargets, model, args, property).failed() &&
                    invalidTargets.state().isEquivalentTo(opening),
                "A foreign target refuses the entire batch before changing a valid earlier source");
        args.ids.clear();
        auto unsupported = model;
        unsupported.channel(channel)->audioClip = true;
        auto candidate = unsupported;
        require(applyStepExpression(candidate, unsupported, args, property).failed() &&
                    candidate.state().isEquivalentTo(unsupported.state()),
                "Audio Clip cannot receive sampler modulation");
        unsupported = model;
        unsupported.channel(channel)->instrument = PluginSpec{};
        candidate = unsupported;
        require(applyStepExpression(candidate, unsupported, args, property).failed() &&
                    candidate.state().isEquivalentTo(unsupported.state()),
                "Any instrument role including native Synth refuses sampler modulation");
    }
    Project loaded;
    require(Project::parse(before, loaded).wasOk() && loaded.state().isEquivalentTo(before),
            "Schema36 nonneutral modulation values round trip exactly");
    require(saveProject(source, folder.getChildFile("step-modulation.vibra")).wasOk() &&
                loadProject(folder.getChildFile("step-modulation.vibra"), loaded).wasOk() &&
                loaded.state().isEquivalentTo(before),
            "Real project file preserves exact binary64 modulation values");
    const auto firstNote = [](juce::ValueTree state)
    {
        for (auto note : state.getChildWithName("PATTERNS").getChild(0))
            if (note.hasType("NOTE"))
                return note;
        return juce::ValueTree{};
    };
    auto neutral = source;
    for (auto &note : neutral.pattern(pattern)->notes)
        note.modX = note.modY = .5;
    auto legacy = neutral.state();
    for (auto note : legacy.getChildWithName("PATTERNS").getChild(0))
        if (note.hasType("NOTE"))
            require(!note.hasProperty("modX") && !note.hasProperty("modY"),
                    "Neutral expression is sparse, not explicit new fields");
    legacy.setProperty("version", 35, nullptr);
    stripSamplerModulationForLegacy(legacy);
    require(Project::parse(legacy, loaded).wasOk() && loaded.state().isEquivalentTo(neutral.state()),
            "Genuine schema35 without modulation migrates to neutral .5");
    const auto destination = loaded.state();
    for (const char *field : {"modX", "modY"})
    {
        auto smuggled = legacy.createCopy();
        firstNote(smuggled).setProperty(field, .5, nullptr);
        require(Project::parse(smuggled, loaded).failed() && loaded.state().isEquivalentTo(destination),
                "Even neutral Mod X/Y presence under old schema is rejected atomically");
        for (const juce::var invalid : {juce::var(true), juce::var("invalid"), juce::var(-.01),
                                        juce::var(1.01), juce::var(std::numeric_limits<double>::infinity())})
        {
            auto malformed = before.createCopy();
            firstNote(malformed).setProperty(field, invalid, nullptr);
            require(
                Project::parse(malformed, loaded).failed() && loaded.state().isEquivalentTo(destination),
                "Typed/range/finite malformed persisted modulation refuses without replacing destination");
        }
    }
    for (const auto &event : sequence(source, true, pattern))
    {
        const auto found = std::find_if(part.notes.begin(), part.notes.end(),
                                        [&](const auto &note) { return note.id == event.note; });
        require(found != part.notes.end() && event.modX == found->modX && event.modY == found->modY,
                "Prepared loop occurrences retain source modulation values and IDs");
    }
    Controller transform;
    transform.replace(source);
    CommandArgs copy;
    copy.pattern = pattern;
    copy.channel = channel;
    copy.ids = {part.notes.front().id};
    copy.beat = 1;
    require(transform.execute("notes.duplicate", copy).wasOk() &&
                transform.project.pattern(pattern)->notes.back().modX == 0 &&
                transform.project.pattern(pattern)->notes.back().modY == 1,
            "Actual duplication retains source modulation while allocating a fresh Note ID");
    transform.replace(source);
    const auto scheduledBeforeBurn = sequence(source, true, pattern);
    require(transform.execute("pattern.burnChannelLoop", copy).wasOk(),
            "Burn materializes actual raw loop occurrences");
    const auto scheduledAfterBurn = sequence(transform.project, true, pattern);
    require(scheduledBeforeBurn.size() == scheduledAfterBurn.size(),
            "Burn retains the exact sounding occurrence count");
    for (size_t i = 0; i < scheduledBeforeBurn.size(); ++i)
        require(scheduledBeforeBurn[i].beat == scheduledAfterBurn[i].beat &&
                    scheduledBeforeBurn[i].modX == scheduledAfterBurn[i].modX &&
                    scheduledBeforeBurn[i].modY == scheduledAfterBurn[i].modY,
                "Burn preserves literal per-occurrence modulation and timing without double application");
    Controller glue;
    auto adjacent = neutral;
    auto &notes = adjacent.pattern(pattern)->notes;
    notes.resize(2);
    notes[0].beat = 0;
    notes[0].length = .25;
    notes[0].pitch = 60;
    notes[0].muted = false;
    notes[1] = notes[0];
    notes[1].id = adjacent.allocate();
    notes[1].beat = .25;
    notes[1].modY = .75;
    glue.replace(adjacent);
    CommandArgs join;
    join.pattern = pattern;
    join.channel = channel;
    join.ids = {notes[0].id, notes[1].id};
    require(glue.execute("notes.glue", join).wasOk() && glue.project.pattern(pattern)->notes.size() == 2,
            "Glue preserves unequal Mod X/Y rather than discarding the second note's filter value");
    CommandArgs imported;
    imported.pattern = pattern;
    imported.channel = channel;
    imported.recordedNotes = {part.notes.front(), part.notes[1]};
    imported.recordedNotes[1].id = imported.recordedNotes[0].id; // Inert source metadata may repeat.
    Macro macro{"macro.original-modulation", "Original Mod X/Y payload", {{"notes.import", imported, false}}};
    auto settings = Customization::familiarDefaults();
    require(settings.addMacro(macro).wasOk() && int(settings.state()["version"]) == 16,
            "Only nonneutral imported modulation requires macro profile16");
    auto neutralMacro = macro;
    for (auto &note : neutralMacro.steps.front().parameters.recordedNotes)
        note.modX = note.modY = .5;
    auto neutralSettings = Customization::familiarDefaults();
    require(neutralSettings.addMacro(neutralMacro).wasOk() && int(neutralSettings.state()["version"]) == 12,
            "Neutral Mod X/Y omission preserves the previous ten-field import profile12");
    const auto sameProfile = [](const juce::var &a, const juce::var &b)
    { return juce::JSON::toString(a) == juce::JSON::toString(b); };
    const auto cloneProfile = [](const juce::var &v) { return juce::JSON::parse(juce::JSON::toString(v)); };
    Customization restored;
    require(restored.importState(settings.state()).wasOk() &&
                sameProfile(restored.state(), settings.state()) &&
                settings.save(folder.getChildFile("step-modulation-profile.json")).wasOk() &&
                restored.load(folder.getChildFile("step-modulation-profile.json")).wasOk() &&
                sameProfile(restored.state(), settings.state()),
            "Typed modulation macro round trip and real file are exact");
    Controller importer;
    importer.replace(neutral);
    MacroContext context;
    context.pattern = pattern;
    context.channel = channel;
    const auto allocation = neutral.nextId;
    require(restored.runMacro(macro.id, importer, context).wasOk(),
            "Actual imported modulation macro runs without selected Note IDs");
    const auto &importedNotes = importer.project.pattern(pattern)->notes;
    require(importedNotes[6].id == allocation && importedNotes[7].id == allocation + 1 &&
                importedNotes[6].modX == 0 && importedNotes[6].modY == 1 && importedNotes[7].modX == .25 &&
                importedNotes[7].modY == .75 && importer.undoEditChecked().wasOk() &&
                importer.project.state().isEquivalentTo(neutral.state()),
            "Repeated metadata IDs become distinct fresh IDs with exact modulation and one Undo");
    const auto payloadRow = [](juce::var &profile) -> juce::DynamicObject *
    {
        return profile["macros"]
            .getArray()
            ->getReference(0)["steps"]
            .getArray()
            ->getReference(0)["parameters"]["recordedNotes"]
            .getArray()
            ->getReference(0)
            .getDynamicObject();
    };
    for (const char *field : {"modX", "modY"})
    {
        auto oldProfile = cloneProfile(settings.state());
        oldProfile.getDynamicObject()->setProperty("version", 15);
        payloadRow(oldProfile)->removeProperty("modX");
        payloadRow(oldProfile)->removeProperty("modY");
        // The other source row must be neutral too, so only this field proves refusal.
        for (auto &row : *oldProfile["macros"]
                              .getArray()
                              ->getReference(0)["steps"]
                              .getArray()
                              ->getReference(0)["parameters"]["recordedNotes"]
                              .getArray())
        {
            row.getDynamicObject()->removeProperty("modX");
            row.getDynamicObject()->removeProperty("modY");
        }
        payloadRow(oldProfile)->setProperty(field, .5);
        const auto originalSettings = restored.state();
        require(restored.importState(oldProfile).failed() && sameProfile(restored.state(), originalSettings),
                "Any modulation field presence under profile16 refuses, including neutral .5");
        auto malformed = cloneProfile(settings.state());
        payloadRow(malformed)->setProperty(field, true);
        require(restored.importState(malformed).failed() && sameProfile(restored.state(), originalSettings),
                "Boolean imported modulation cannot silently coerce or partially replace settings");
    }
    settings.macros.front().steps.front().parameters.recordedNotes.front().modX =
        std::numeric_limits<double>::quiet_NaN();
    const auto beforeRun = importer.project.state();
    require(settings.runMacro(macro.id, importer, context).failed() &&
                importer.project.state().isEquivalentTo(beforeRun) &&
                settings.save(folder.getChildFile("step-modulation-profile.json")).failed(),
            "Corrupted in-memory imported modulation refuses run/save before any mutation");
}
