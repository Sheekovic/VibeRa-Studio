// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../src/Commands.h"
#include "../src/StoppedEffectChangePlan.h"
#include <stdexcept>

// Original missing-provider MODEL/history proof only. No scanning, worker,
// native graph, capture freshness, audio adoption or listening claim.
namespace vibera::acceptance::stoppedEffectCommand
{
__declspec(noinline) inline juce::Result verify()
{
    try
    {
        const auto check = [](bool ok, const char *message)
        {
            if (!ok)
                throw std::runtime_error(message);
        };
        juce::PluginDescription effect;
        effect.name = "Original absent stopped-effect command fixture";
        effect.pluginFormatName = "VST3";
        effect.fileOrIdentifier = "original-missing-command-effect.vst3";
        effect.numInputChannels = effect.numOutputChannels = 2;
        auto instrument = effect;
        instrument.name = "Original absent capture instrument";
        instrument.isInstrument = true;
        auto p = Project::empty();
        PluginSpec background{p.allocate(),
                              effect.name,
                              effect.createXml()->toString(),
                              juce::MemoryBlock("old-effect", 10).toBase64Encoding(),
                              false,
                              false};
        PluginSpec synth{p.allocate(),
                         instrument.name,
                         instrument.createXml()->toString(),
                         juce::MemoryBlock("old-synth", 9).toBase64Encoding(),
                         true,
                         false};
        p.mixer[2].effects = {background};
        p.channels.front().instrument = synth;
        std::swap(p.mixer.front(), p.mixer[3]); // Master is not assumed vector0.
        auto owner = std::make_unique<Controller>();
        owner->replace(p);
        owner->deliverPendingNotifications();
        int applicationCalls = 0, notifications = 0;
        owner->applicationCommand = [&](const juce::String &, const CommandArgs &,
                                        const juce::String &) -> std::optional<juce::Result>
        {
            ++applicationCalls;
            return juce::Result::fail("Original application hook must not run during preparation");
        };
        owner->changed = [&] { ++notifications; };
        const auto snapshot = [&]
        {
            return owner->project.state().toXmlString() + "/" + owner->projectIdentity + "/" +
                   juce::String(juce::int64(owner->revision)) + "/" + juce::String(int(owner->dirty)) + "/" +
                   owner->undo.getUndoDescriptions().joinIntoString("|") + "/" +
                   owner->undo.getRedoDescriptions().joinIntoString("|") + "/" +
                   juce::String(owner->undo.getNumberOfUnitsTakenUpByStoredCommands());
        };
        auto captured = owner->project;
        captured.plugin(background.id)->state = juce::MemoryBlock("new-effect", 10).toBase64Encoding();
        captured.plugin(synth.id)->state = juce::MemoryBlock("new-synth", 9).toBase64Encoding();
        CommandArgs insert;
        insert.mixer = 1;
        insert.text = effect.createXml()->toString();
        std::unique_ptr<PreparedControllerChange> prepared;
        const auto original = snapshot();
        check(
            owner->prepareStoppedEffectCommand("plugin.effect.insert", insert, captured, prepared).wasOk() &&
                prepared && !prepared->isNoOp() && prepared->destinationBeforeCommit(),
            "Real canonical effect mutator prepares with all fresh opaque states");
        const auto *target = mixerInsert(*prepared->destinationBeforeCommit(), 1);
        check(target && target->effects.size() == 1 && target->effects.front().descriptor == insert.text &&
                  target->effects.front().id >= captured.nextId &&
                  prepared->destinationBeforeCommit()->plugin(background.id)->state ==
                      captured.plugin(background.id)->state &&
                  prepared->destinationBeforeCommit()->plugin(synth.id)->state ==
                      captured.plugin(synth.id)->state &&
                  snapshot() == original && applicationCalls == 0 && notifications == 0 &&
                  !owner->undo.canUndo(),
              "Preparation leaves actual model/history/observers intact; destination retains all captured "
              "states");
        const auto ownedBefore = prepared->nativeEffectHistoryBeforeCommit();
        check(
            ownedBefore && ownedBefore->capturedSource && !ownedBefore->isHistoryTraversal &&
                ownedBefore->capturedSource != &captured && ownedBefore->capturedSource != &owner->project &&
                ownedBefore->destination == prepared->destinationBeforeCommit() && ownedBefore->insert == 1 &&
                ownedBefore->capturedSource->state().isEquivalentTo(captured.state()),
            "Positive command witness points into actual owned immutable history, never incoming source "
            "alias");
        const auto savedCapturedState = captured.plugin(background.id)->state;
        captured.plugin(background.id)->state = juce::MemoryBlock("caller-changed", 14).toBase64Encoding();
        check(ownedBefore->capturedSource->plugin(background.id)->state == savedCapturedState,
              "History baseline remains immutable when incoming capture object later changes");
        captured.plugin(background.id)->state = savedCapturedState;
        const auto addedId = target->effects.front().id;
        const auto destination = *prepared->destinationBeforeCommit();
        auto *const prior = prepared.get();
        const auto refused = [&](const juce::String &id, const CommandArgs &args, const Project &capture)
        {
            const auto before = snapshot();
            check(owner->prepareStoppedEffectCommand(id, args, capture, prepared).failed() &&
                      prepared.get() == prior && snapshot() == before && applicationCalls == 0 &&
                      notifications == 0,
                  "Refusal preserves prior output, source, dirty/revision and actual history without "
                  "callbacks");
        };
        auto bad = captured;
        bad.tempo += 1;
        refused("plugin.effect.insert", insert, bad);
        bad = captured;
        bad.plugin(background.id)->name += " changed";
        refused("plugin.effect.insert", insert, bad);
        bad = captured;
        bad.plugin(background.id)->descriptor = instrument.createXml()->toString();
        refused("plugin.effect.insert", insert, bad);
        bad = captured;
        bad.plugin(background.id)->wet = .25f;
        refused("plugin.effect.insert", insert, bad);
        bad = captured;
        bad.channels.front().instrument.reset();
        refused("plugin.effect.insert", insert, bad);
        auto args = insert;
        args.text = "malformed descriptor";
        refused("plugin.effect.insert", args, captured);
        args = insert;
        args.mixer = 765432;
        refused("plugin.effect.insert", args, captured);
        refused("plugin.bypass", insert, captured);
        args = {};
        args.target = synth.id;
        refused("plugin.remove", args, captured);
        args = insert;
        args.capturedInstruments = {synth};
        refused("plugin.effect.insert", args, captured);
        check(!owner->commitPreparedChange(*prepared) && snapshot() == original,
              "Unvalidated prepared command cannot publish");
        check(owner->validatePreparedChange(*prepared).wasOk() && owner->commitPreparedChange(*prepared) &&
                  owner->project.state().isEquivalentTo(destination.state()) && notifications == 0 &&
                  owner->undo.getUndoDescriptions().size() == 1,
              "One validated no-observer commit uses the canonical history manager");
        check(!prepared->nativeEffectHistoryBeforeCommit(),
              "Committed command never exposes retired ticket witness");
        owner->deliverPendingNotifications();
        check(notifications == 1 && owner->undoEditChecked().wasOk() &&
                  owner->project.state().isEquivalentTo(captured.state()) && !owner->undo.canUndo() &&
                  owner->redoEditChecked().wasOk() &&
                  owner->project.state().isEquivalentTo(destination.state()),
              "One Undo restores exact captured opaque baseline for every plugin; one Redo restores "
              "destination");
        // A new real capture before removal contains newer bytes for the removed
        // effect too. Its inverse must restore THESE bytes, not insertion-time state.
        auto removalCapture = owner->project;
        removalCapture.plugin(addedId)->state =
            juce::MemoryBlock("removed-live-state", 18).toBase64Encoding();
        removalCapture.plugin(background.id)->state =
            juce::MemoryBlock("background-newer", 16).toBase64Encoding();
        CommandArgs remove;
        remove.target = addedId;
        remove.mixer = 999999; // Remove uses actual stable effect owner, not unrelated argument/ordinal.
        std::unique_ptr<PreparedControllerChange> removal;
        check(owner->prepareStoppedEffectCommand("plugin.remove", remove, removalCapture, removal).wasOk(),
              "Canonical removal prepared before examining actual owned history witness");
        const auto removalBefore = removal->nativeEffectHistoryBeforeCommit();
        check(removalBefore && !removalBefore->isHistoryTraversal && removalBefore->insert == 1 &&
                  removalBefore->capturedSource != &removalCapture &&
                  removalBefore->capturedSource->plugin(addedId)->state ==
                      removalCapture.plugin(addedId)->state &&
                  removalBefore->destination == removal->destinationBeforeCommit(),
              "Remove witness retains actual nonempty captured native state for its real history action");
        check(
            mixerInsert(*removal->destinationBeforeCommit(), 1)->effects.empty() &&
                owner->validatePreparedChange(*removal).wasOk() && owner->commitPreparedChange(*removal) &&
                !removal->nativeEffectHistoryBeforeCommit(),
            "Actual Mixer-target remove prepares a pure whole-chain inverse through shared mutator/planner");
        check(
            owner->undoEditChecked().wasOk() &&
                owner->project.state().isEquivalentTo(removalCapture.state()) &&
                owner->project.plugin(addedId)->state == removalCapture.plugin(addedId)->state,
            "Remove Undo restores freshly captured removed opaque state, exact ID/order and unrelated state");
        // Occupied append and partial chain removal cannot masquerade as a pure
        // stopped change. Preserve even an already committed output on refusal.
        const auto occupied = owner->project;
        auto *const saved = prepared.get();
        const auto beforeOccupied = snapshot();
        check(
            owner->prepareStoppedEffectCommand("plugin.effect.insert", insert, occupied, prepared).failed() &&
                prepared.get() == saved && snapshot() == beforeOccupied,
            "Occupied chain append refused atomically");
        auto multi = occupied;
        auto second = *multi.plugin(addedId);
        second.id = multi.allocate();
        for (auto &row : multi.mixer)
            if (row.index == 1)
                row.effects.push_back(second);
        auto other = std::make_unique<Controller>();
        other->replace(multi);
        std::unique_ptr<PreparedControllerChange> nothing;
        check(other->prepareStoppedEffectCommand("plugin.remove", remove, multi, nothing).failed() &&
                  !nothing && other->project.state().isEquivalentTo(multi.state()) && !other->undo.canUndo(),
              "Single-slot delete from populated chain is not a whole-chain inverse");
        // A newer opaque capture without a musical revision bump still invalidates the ticket.
        args = insert;
        args.mixer = 4;
        std::unique_ptr<PreparedControllerChange> stale;
        check(owner->prepareStoppedEffectCommand("plugin.effect.insert", args, owner->project, stale).wasOk(),
              "Prepare independent empty stable insert for stale-source proof");
        owner->project.plugin(background.id)->state = juce::MemoryBlock("late-state", 10).toBase64Encoding();
        const auto newer = snapshot();
        check(
            owner->validatePreparedChange(*stale).failed() && !owner->commitPreparedChange(*stale) &&
                snapshot() == newer,
            "Actual canonical source is freshness witness, even though history.before held captured states");
        check(owner->beginGesture("original-owned-gesture"), "Actual owned gesture starts");
        const auto gestureBefore = snapshot();
        check(owner->prepareStoppedEffectCommand("plugin.effect.insert", args, owner->project, stale)
                      .failed() &&
                  owner->hasGesture() && owner->gestureOwner() == "original-owned-gesture" &&
                  snapshot() == gestureBefore,
              "Preparation never cancels, commits or takes over an active gesture");
        check(
            owner->cancelGestureChecked("original-owned-gesture").wasOk() && applicationCalls == 0,
            "Ordinary checked owner can cancel its gesture; preparation never called application admission");
        return juce::Result::ok();
    }
    catch (const std::exception &error)
    {
        return juce::Result::fail(error.what());
    }
}
} // namespace vibera::acceptance::stoppedEffectCommand
