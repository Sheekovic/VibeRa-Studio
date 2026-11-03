// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../src/StoppedEffectChangePlan.h"
#include "../src/Commands.h"
#include <stdexcept>

namespace vibera::acceptance::stoppedEffects
{
// Model/history admission only. This does NOT process or qualify a native graph.
__declspec(noinline) inline juce::Result verifyPlan()
{
    try
    {
        const auto check = [](bool value, const char *message)
        {
            if (!value)
                throw std::runtime_error(message);
        };
        auto source = Project::empty();
        juce::PluginDescription type;
        type.name = "Original absent effect state fixture";
        type.pluginFormatName = "VST3";
        type.fileOrIdentifier = "original-absent-effect.vst3";
        type.numInputChannels = type.numOutputChannels = 2;
        auto destination = source;
        auto &chain = destination.mixer[1];
        chain.effects.push_back(
            {destination.allocate(), type.name, type.createXml()->toString(), "1...", false, true});
        chain.effects.front().wet = .25f;
        chain.effects.front().automationBindings.push_back({17, 1, 0, .625f, "Original", "unit"});
        chain.effects.push_back({destination.allocate(), "Independent second slot",
                                 type.createXml()->toString(), "1.A.", false, false});
        chain.effectsBypassed = true;
        const auto target = chain.index;
        check(source.validate().wasOk() && destination.validate().wasOk(),
              "Genuine missing-plugin model fixtures, not provider compatibility evidence");
        const auto original = source.state(), next = destination.state();
        StoppedEffectChangePlan plan;
        check(planStoppedEffectChange(source, destination, target, plan).wasOk() && plan.appending &&
                  plan.insert == target && plan.added == chain.effects && plan.removed.empty(),
              "Append preserves exact ordered IDs/state/bindings/wet/bypass");
        check(source.state().isEquivalentTo(original) && destination.state().isEquivalentTo(next),
              "Read-only planning never mutates either document");
        StoppedEffectChangePlan inverse;
        check(planStoppedEffectChange(destination, source, target, inverse).wasOk() && !inverse.appending &&
                  inverse.removed == chain.effects && inverse.added.empty(),
              "Whole-chain removal is explicit inverse admission only");
        const auto saved = plan;
        const auto refusal = [&](const Project &candidate, int insert)
        {
            check(planStoppedEffectChange(source, candidate, insert, plan).failed() &&
                      plan.insert == saved.insert && plan.appending == saved.appending &&
                      plan.added == saved.added && plan.removed == saved.removed,
                  "Refusal preserves caller-owned prior output");
        };
        refusal(source, target);      // No change.
        refusal(destination, 987654); // Missing stable Mixer ID.
        auto changed = destination;
        changed.channels.front().mixer = 2;
        refusal(changed, target);
        changed = destination;
        changed.mixer[2].gain = .37f;
        refusal(changed, target);
        changed = destination;
        changed.tempo += 1;
        refusal(changed, target);
        changed = destination;
        changed.patterns.front().length += 1;
        refusal(changed, target);
        changed = destination;
        changed.mixer[1].effects.front().id = source.channels.front().id;
        refusal(changed, target); // Genuine duplicate global ID is invalid.
        auto occupied = source;
        occupied.mixer[1].effects.push_back(destination.mixer[1].effects.front());
        occupied.nextId = destination.nextId;
        check(occupied.validate().wasOk() &&
                  planStoppedEffectChange(occupied, destination, target, plan).failed(),
              "Occupied-chain replacement is explicitly outside this admission");
        auto reordered = source, reorderedNext = destination;
        std::swap(reordered.mixer[0], reordered.mixer[2]);
        std::swap(reorderedNext.mixer[0], reorderedNext.mixer[2]);
        check(planStoppedEffectChange(reordered, reorderedNext, target, plan).wasOk(),
              "Stable Mixer ID, never vector ordinal or assumed leading Master");

        // Independent native-metadata/history contract: source carries fresh bytes
        // from another existing slot; canonical model still has older opaque bytes.
        auto canonical = source;
        auto existing = destination.mixer[1].effects.back();
        canonical.mixer[2].effects.push_back(existing);
        canonical.nextId = destination.nextId;
        auto captured = canonical;
        captured.plugin(existing.id)->state = "1...";
        auto pasted = captured;
        auto copied = destination.mixer[1].effects.front();
        copied.id = pasted.allocate();
        pasted.mixer[1].effects.push_back(copied);
        auto controller = std::make_unique<Controller>();
        controller->replace(canonical);
        const auto before = controller->project.state();
        std::unique_ptr<PreparedControllerChange> prepared;
        check(controller
                  ->prepareSnapshotChange(pasted, captured, "Original stopped-chain snapshot", true, false,
                                          true, prepared)
                  .wasOk(),
              "Fresh native baseline prepared as one genuine history transaction");
        check(controller->project.state().isEquivalentTo(before) && !controller->undo.canUndo(),
              "Preparing history does not publish or consume canonical history");
        check(controller->validatePreparedChange(*prepared).wasOk() &&
                  controller->commitPreparedChange(*prepared) &&
                  controller->project.state().isEquivalentTo(pasted.state()),
              "Canonical commit uses existing prepared Controller primitive");
        check(controller->undoEditChecked().wasOk() &&
                  controller->project.state().isEquivalentTo(captured.state()) &&
                  !controller->undo.canUndo() && controller->redoEditChecked().wasOk() &&
                  controller->project.state().isEquivalentTo(pasted.state()),
              "One Undo/Redo preserves acknowledged native baseline and exact copied order/state");
        return juce::Result::ok();
    }
    catch (const std::exception &e)
    {
        return juce::Result::fail(e.what());
    }
}
} // namespace vibera::acceptance::stoppedEffects
