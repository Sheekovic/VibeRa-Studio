// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "StepComposeContracts.h"
#include <algorithm>

// Original Controller/history/profile tests; no worker, UI, callback or hardware evidence.
static void stepComposeCanonicalTests()
{
    const auto seed = []
    {
        auto project = Project::empty();
        auto &pattern = project.patterns.front();
        pattern.length = 8;
        pattern.autoLength = false;
        pattern.notes = {{project.allocate(), project.channels[0].id, 0, .25, 60, .6f, true},
                         {project.allocate(), project.channels[0].id, 9, .5, 72, .5f, false},
                         {project.allocate(), project.channels[2].id, .5, .5, 48, .4f, false}};
        return project;
    };
    Controller c;
    c.replace(seed());
    const auto pattern = c.project.patterns.front().id;
    const auto first = c.project.channels[0].id, second = c.project.channels[1].id;
    StepComposeRecipe every;
    every.channel = first;
    every.sequenceSteps = 8;
    every.every = 2;
    every.rotation = 1;
    StepComposeRecipe euclidean;
    euclidean.channel = second;
    euclidean.sequenceSteps = 8;
    euclidean.mode = StepComposeMode::euclidean;
    euclidean.pulses = 3;
    euclidean.rotation = -1;
    euclidean.seed = 0xffffffffu;
    euclidean.loop = false;
    CommandArgs compose;
    compose.pattern = pattern;
    compose.composition = {every, euclidean};
    const auto baseline = c.project.state();
    const auto beforeAllocation = c.project.nextId;
    auto positions = [](const Project &project, Id patternId, Id channel)
    {
        std::vector<double> beats;
        for (const auto &note : project.pattern(patternId)->notes)
            if (note.channel == channel && note.beat < 2)
                beats.push_back(note.beat);
        std::sort(beats.begin(), beats.end());
        return beats;
    };
    require(c.execute("steps.compose", compose).wasOk() &&
                positions(c.project, pattern, first) == std::vector<double>{.25, .75, 1.25, 1.75} &&
                positions(c.project, pattern, second) == std::vector<double>{.25, 1, 1.75} &&
                c.project.nextId == beforeAllocation + 7,
            "Canonical multi-recipe compose has literal onsets and exact fresh-note allocation");
    require(channelLoopLength(c.project, *c.project.pattern(pattern), first) == 2 &&
                channelLoopLength(c.project, *c.project.pattern(pattern), second) == 0,
            "Canonical recipes apply only their explicit loop policies");
    const auto composed = c.project.state();
    require(c.undoEditChecked().wasOk() && c.project.state().isEquivalentTo(baseline) && !c.undo.canUndo() &&
                c.redoEditChecked().wasOk() && c.project.state().isEquivalentTo(composed) &&
                !c.undo.canRedo(),
            "Ordinary canonical composition is one exact Undo/Redo with identical generated IDs");
    const auto revision = c.revision;
    const auto undo = c.undo.getUndoDescriptions(), redo = c.undo.getRedoDescriptions();
    const auto units = c.undo.getNumberOfUnitsTakenUpByStoredCommands();
    require(c.execute("steps.compose", compose).wasOk() && c.project.state().isEquivalentTo(composed) &&
                c.revision == revision && c.undo.getUndoDescriptions() == undo &&
                c.undo.getRedoDescriptions() == redo &&
                c.undo.getNumberOfUnitsTakenUpByStoredCommands() == units,
            "Reapplying exact recipes retains note IDs, allocator, revision and real history as a no-op");
    auto refused = [&](const CommandArgs &bad, const juce::String &owner = {})
    {
        const auto state = c.project.state();
        const auto rev = c.revision;
        const auto dirty = c.dirty;
        const auto undoBefore = c.undo.getUndoDescriptions(), redoBefore = c.undo.getRedoDescriptions();
        const auto unitsBefore = c.undo.getNumberOfUnitsTakenUpByStoredCommands();
        require(c.execute("steps.compose", bad, owner).failed() && c.project.state().isEquivalentTo(state) &&
                    c.revision == rev && c.dirty == dirty && c.undo.getUndoDescriptions() == undoBefore &&
                    c.undo.getRedoDescriptions() == redoBefore &&
                    c.undo.getNumberOfUnitsTakenUpByStoredCommands() == unitsBefore,
                "Invalid batch/owner restores no state: document, allocator and both history branches remain "
                "exact");
    };
    auto bad = compose;
    bad.composition.back().pulses = 9;
    refused(bad);
    bad = compose;
    bad.composition.back().channel = first;
    refused(bad);
    bad = compose;
    bad.composition.back().channel = 999999999;
    refused(bad);
    bad = compose;
    bad.composition.clear();
    refused(bad);
    bad = compose;
    bad.enabled = false;
    refused(bad);
    bad = compose;
    bad.pattern = 999999999;
    refused(bad);

    c.replace(seed());
    const auto previewBaseline = c.project.state();
    const auto previewAllocation = c.project.nextId;
    const auto previewDirty = c.dirty;
    const juce::String owner = "step-compose:original-canonical-acceptance";
    require(c.beginGesture(owner), "Acquire actual owned Composer preview gesture");
    const auto gestureSerial = c.gestureSerial();
    require(c.execute("steps.compose", compose, owner).wasOk() &&
                c.project.state().isEquivalentTo(composed) && !c.undo.canUndo() && !c.undo.canRedo(),
            "Owned preview uses canonical mutation without admitting a committed history action");
    require(c.execute("steps.compose", compose, owner).wasOk() &&
                c.project.state().isEquivalentTo(composed) && c.project.nextId == previewAllocation + 7,
            "Repeated preview reuses exact opening allocator and is never cumulative");
    CommandArgs changed = compose;
    changed.composition.resize(1);
    changed.composition.front().every = 4;
    require(c.execute("steps.compose", changed, owner).wasOk() &&
                positions(c.project, pattern, first) == std::vector<double>{.25, 1.25} &&
                positions(c.project, pattern, second).empty() && c.project.nextId == previewAllocation + 2,
            "Each owned preview starts at original baseline, removing prior preview's second recipe and IDs");
    const auto retainedPreview = c.project.state();
    refused(compose, "step-compose:foreign-owner");
    require(c.gestureOwner() == owner && c.gestureSerial() == gestureSerial &&
                c.project.state().isEquivalentTo(retainedPreview),
            "Foreign token cannot steal or finalize the real preview owner");
    CommandArgs restore = compose;
    restore.enabled = false;
    restore.composition.clear();
    require(c.execute("steps.compose", restore, owner).wasOk() &&
                c.project.state().isEquivalentTo(previewBaseline) && c.project.nextId == previewAllocation &&
                !c.undo.canUndo() && !c.undo.canRedo(),
            "Owned enabled=false restores exact baseline and allocator without a fake empty-recipe edit");
    require(c.execute("steps.compose", compose, owner).wasOk() && c.cancelGestureChecked(owner).wasOk() &&
                c.project.state().isEquivalentTo(previewBaseline) && c.dirty == previewDirty &&
                !c.undo.canUndo() && !c.undo.canRedo() && !c.hasGesture(),
            "Cancelling actual preview restores opening source, dirty state, allocator and history");
    require(c.beginGesture(owner) && c.execute("steps.compose", compose, owner).wasOk() &&
                c.endGestureChecked("Original Advanced Fill", owner).wasOk(),
            "Accept actual preview through the real owned gesture history");
    const auto accepted = c.project.state();
    require(c.undoEditChecked().wasOk() && c.project.state().isEquivalentTo(previewBaseline) &&
                !c.undo.canUndo() && c.redoEditChecked().wasOk() &&
                c.project.state().isEquivalentTo(accepted),
            "Accepted preview is exactly one canonical history action");

    // Audition is private, baseline-relative preview state, never part of the
    // accepted musical edit. Retain a genuine pre-existing solo and its mute baseline.
    auto auditionSource = seed();
    const auto third = auditionSource.channels[2].id, locked = auditionSource.channels[3].id;
    auditionSource.channel(third)->muted = true;
    auditionSource.channel(locked)->muteLocked = true;
    c.replace(auditionSource);
    CommandArgs originalSolo;
    originalSolo.channel = third;
    require(c.execute("channel.solo", originalSolo).wasOk() && c.project.solo &&
                c.project.solo->channel == third && c.project.solo->mutedBefore == std::vector<Id>{third},
            "Audition fixture owns a real opening solo with a pre-existing mute baseline");
    auditionSource = c.project;
    c.replace(auditionSource);
    const auto auditionBaseline = c.project.state();
    const auto auditionDirty = c.dirty;
    const auto auditionAllocation = c.project.nextId;
    Controller musicalReference;
    musicalReference.replace(auditionSource);
    require(musicalReference.execute("steps.compose", compose).wasOk(),
            "Ordinary composition supplies the exact musical-only history destination");
    const auto musicalOnly = musicalReference.project.state();
    CommandArgs audition = restore;
    audition.compositionSolo = first;
    refused(audition);
    bad = compose;
    bad.compositionSolo = first;
    refused(bad);
    require(
        c.beginGesture(owner) && c.execute("steps.compose", audition, owner).wasOk() && c.project.solo &&
            c.project.solo->channel == first && !c.project.solo->groupScoped &&
            !c.project.channel(first)->muted && c.project.channel(second)->muted &&
            c.project.channel(third)->muted && !c.project.channel(locked)->muted &&
            c.project.channel(locked)->muteLocked && c.project.nextId == auditionAllocation &&
            positions(c.project, pattern, first) == std::vector<double>{0} && !c.undo.canUndo() &&
            !c.undo.canRedo(),
        "Disabled empty composition auditions the first Channel without composing or changing a locked peer");
    const auto firstAudition = c.project.state();
    audition.compositionSolo = second;
    require(c.execute("steps.compose", audition, owner).wasOk() && c.project.solo &&
                c.project.solo->channel == second && c.project.channel(first)->muted &&
                !c.project.channel(second)->muted && c.project.channel(third)->muted &&
                !c.project.channel(locked)->muted && c.project.channel(locked)->muteLocked &&
                c.project.solo->mutedBefore == std::vector<Id>{third} &&
                c.project.nextId == auditionAllocation,
            "Switching audition uses the opening mute baseline instead of accumulating the first solo");
    bad = compose;
    bad.compositionSolo = locked;
    refused(bad, owner);
    require(c.gestureOwner() == owner && c.project.solo->channel == second,
            "Locked audition target refuses the whole recipe batch without stealing the preview owner");
    audition.compositionSolo = first;
    require(c.execute("steps.compose", audition, owner).wasOk() &&
                c.project.state().isEquivalentTo(firstAudition) && c.cancelGestureChecked(owner).wasOk() &&
                c.project.state().isEquivalentTo(auditionBaseline) && c.dirty == auditionDirty &&
                !c.hasGesture() && !c.undo.canUndo() && !c.undo.canRedo(),
            "Repeated audition and cancel restore the exact opening solo, mutes, locks, score and allocator");
    auto composeAndSolo = compose;
    composeAndSolo.compositionSolo = second;
    require(c.beginGesture(owner) && c.execute("steps.compose", composeAndSolo, owner).wasOk() &&
                positions(c.project, pattern, first) == std::vector<double>{.25, .75, 1.25, 1.75} &&
                positions(c.project, pattern, second) == std::vector<double>{.25, 1, 1.75} &&
                c.project.solo && c.project.solo->channel == second && !c.project.channel(locked)->muted &&
                c.project.channel(locked)->muteLocked && !c.undo.canUndo(),
            "Owned preview atomically composes both literal recipes and auditions their chosen Channel");
    require(c.execute("steps.compose", compose, owner).wasOk() &&
                c.project.state().isEquivalentTo(musicalOnly) && c.project.solo &&
                c.project.solo->channel == third && !c.project.channel(third)->muted &&
                c.project.channel(first)->muted && c.project.channel(second)->muted &&
                !c.project.channel(locked)->muted && c.project.channel(locked)->muteLocked &&
                c.endGestureChecked("Compose without audition", owner).wasOk(),
            "Final apply without private solo restores opening mute ownership before committing only music");
    require(c.undoEditChecked().wasOk() && c.project.state().isEquivalentTo(auditionBaseline) &&
                !c.undo.canUndo() && c.redoEditChecked().wasOk() &&
                c.project.state().isEquivalentTo(musicalOnly) && !c.undo.canRedo(),
            "Audition never enters the single exact musical Undo/Redo transaction");

    // A generic document/macro gesture must compose onto its current candidate,
    // preserving earlier commands rather than borrowing Composer's baseline policy.
    c.replace(seed());
    require(c.beginGesture("generic-original-owner"), "Acquire ordinary grouped document gesture");
    CommandArgs tempo;
    tempo.value = 137;
    require(c.execute("project.tempo", tempo, "generic-original-owner").wasOk() &&
                c.execute("steps.compose", compose, "generic-original-owner").wasOk() &&
                c.project.tempo == 137,
            "Generic gesture composition retains its earlier unrelated tempo edit");
    refused(restore, "generic-original-owner");
    refused(composeAndSolo, "generic-original-owner");
    require(c.cancelGestureChecked("generic-original-owner").wasOk(),
            "Ordinary grouped gesture cancels exactly");

    Customization profile = Customization::familiarDefaults();
    CommandArgs onlyFirst = compose, onlySecond = compose;
    onlyFirst.composition = {every};
    onlySecond.composition = {euclidean};
    Macro macro{"macro.original-compose-batch",
                "Original compose batch",
                {{"project.tempo", tempo, false},
                 {"steps.compose", onlyFirst, false},
                 {"steps.compose", onlySecond, false}}};
    require(profile.addMacro(macro).wasOk(), "Grouped macro accepts actual typed recipes");
    const auto encoded = profile.state();
    require(int(encoded["version"]) == 9,
            "Recipes require explicit profile9 without changing old profile contracts");
    Customization reloaded;
    require(reloaded.importState(juce::JSON::parse(juce::JSON::toString(encoded))).wasOk() &&
                juce::JSON::toString(reloaded.state()) == juce::JSON::toString(encoded),
            "Profile9 JSON serialization/reload preserves every typed recipe and binding");
    c.replace(seed());
    MacroContext context;
    context.pattern = pattern;
    context.channel = first;
    const auto beforeMacro = c.project.state();
    require(
        reloaded.runMacro(macro.id, c, context).wasOk() && !reloaded.running && c.project.tempo == 137 &&
            positions(c.project, pattern, first) == std::vector<double>{.25, .75, 1.25, 1.75} &&
            positions(c.project, pattern, second) == std::vector<double>{.25, 1, 1.75},
        "Real macro groups multiple recipe commands without resetting earlier recipes or unrelated edits");
    const auto afterMacro = c.project.state();
    require(c.undoEditChecked().wasOk() && c.project.state().isEquivalentTo(beforeMacro) &&
                !c.undo.canUndo() && c.redoEditChecked().wasOk() &&
                c.project.state().isEquivalentTo(afterMacro),
            "Whole grouped macro uses one exact Undo/Redo including generated IDs and tempo");

    auto macroParameters = [&](juce::var &state) -> juce::DynamicObject *
    {
        for (auto &entry : *state["macros"].getArray())
            if (entry["id"].toString() == macro.id)
                return entry["steps"].getArray()->getReference(1)["parameters"].getDynamicObject();
        require(false, "Find actual authored profile9 macro before corrupting fixture");
        return nullptr;
    };
    const auto refuseProfile = [&](const juce::var &invalid)
    {
        const auto before = juce::JSON::toString(reloaded.state());
        require(reloaded.importState(invalid).failed() && juce::JSON::toString(reloaded.state()) == before,
                "Malformed/old profile refuses atomically without replacing current macros or shortcuts");
    };
    auto invalid = encoded.clone();
    invalid.getDynamicObject()->setProperty("version", 8);
    refuseProfile(invalid);
    for (int fault = 0; fault < 5; ++fault)
    {
        invalid = encoded.clone();
        auto *parameters = macroParameters(invalid);
        auto &row = parameters->getProperty("recipes").getArray()->getReference(0);
        if (fault == 0)
            row.getDynamicObject()->setProperty("unexpected", true);
        if (fault == 1)
            row.getDynamicObject()->setProperty("sequenceSteps", 8.5);
        if (fault == 2)
            row.getDynamicObject()->setProperty("seed", -1);
        if (fault == 3)
            row.getDynamicObject()->removeProperty("channel");
        if (fault == 4)
            row.getDynamicObject()->setProperty("mode", "invented");
        refuseProfile(invalid);
    }
    auto recipes = juce::Array<juce::var>{stepComposeRecipeState(every)};
    std::vector<StepComposeRecipe> parsed;
    require(parseStepComposeRecipes(recipes, parsed).wasOk() && parsed.size() == 1,
            "Public strict recipe parser accepts canonical serialized recipe");
    recipes.getReference(0).getDynamicObject()->setProperty("readbackOnlyMask", juce::Array<juce::var>{1});
    require(parseStepComposeRecipes(recipes, parsed).failed() && parsed.size() == 1 &&
                parsed.front().channel == every.channel,
            "Public parser rejects extra readback fields and retains prior parsed recipe output");
    auto request = juce::JSON::parse(R"({"pattern":1,"recipes":[]})");
    request.getDynamicObject()->setProperty("pattern", juce::int64(pattern));
    request.getDynamicObject()->setProperty("recipes", juce::Array<juce::var>{stepComposeRecipeState(every)});
    bool acceptedSchema = true;
    try
    {
        validateMcpSchema(stepComposeArguments(), request);
    }
    catch (const McpFailure &)
    {
        acceptedSchema = false;
    }
    require(acceptedSchema, "Public canonical schema accepts explicit stable Pattern and typed recipe batch");
    request.getDynamicObject()->setProperty("enabled", false);
    bool refusedSchema = false;
    try
    {
        validateMcpSchema(stepComposeArguments(), request);
    }
    catch (const McpFailure &failure)
    {
        refusedSchema = failure.code == "invalid_arguments";
    }
    require(refusedSchema,
            "Internal preview restore flag cannot be supplied through the public command schema");
    request.getDynamicObject()->removeProperty("enabled");
    request.getDynamicObject()->setProperty("compositionSolo", juce::int64(first));
    refusedSchema = false;
    try
    {
        validateMcpSchema(stepComposeArguments(), request);
    }
    catch (const McpFailure &failure)
    {
        refusedSchema = failure.code == "invalid_arguments";
    }
    require(refusedSchema, "Private audition target cannot be supplied through the public command schema");
}
