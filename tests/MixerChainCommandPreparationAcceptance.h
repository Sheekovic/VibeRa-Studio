// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../src/Commands.h"
#include "../src/EffectChain.h"
#include "../src/MixerRouting.h"
#include <cstring>
#include <stdexcept>

// Original absent-provider MODEL/history proof. No native capture, worker,
// graph publication, playback continuity or provider compatibility claim.
namespace vibera::acceptance::mixerChainCommand
{
__declspec(noinline) inline juce::Result verify()
{
    try
    {
        const auto check = [](bool value, const char *message)
        {
            if (!value)
                throw std::runtime_error(message);
        };
        const auto state = [](const char *s)
        { return juce::MemoryBlock(s, std::strlen(s)).toBase64Encoding(); };
        juce::PluginDescription type;
        type.name = "Original absent general chain fixture";
        type.pluginFormatName = "VST3";
        type.fileOrIdentifier = "original-absent-general-chain.vst3";
        type.numInputChannels = type.numOutputChannels = 2;
        const auto descriptor = type.createXml()->toString();
        auto project = Project::empty();
        const auto oldA = project.allocate(), oldB = project.allocate(), background = project.allocate();
        project.mixer[1].effects = {{oldA, "Old A", descriptor, state("old-A"), false, true},
                                    {oldB, "Old B", descriptor, state("old-B"), false, false}};
        project.mixer[1].effects[0].wet = .375f;
        project.mixer[2].effects = {
            {background, "Background", descriptor, state("background"), false, false}};
        std::swap(project.mixer.front(), project.mixer[4]); // Master need not be display row0.
        auto owner = std::make_unique<Controller>();
        owner->replace(project);
        owner->deliverPendingNotifications();
        int applicationCalls = 0, notifications = 0;
        owner->applicationCommand = [&](const juce::String &, const CommandArgs &,
                                        const juce::String &) -> std::optional<juce::Result>
        {
            ++applicationCalls;
            return juce::Result::fail("unexpected hook");
        };
        owner->applicationHistory = [&](HistoryDirection) -> std::optional<juce::Result>
        {
            ++applicationCalls;
            return juce::Result::fail("unexpected history hook");
        };
        owner->changed = [&] { ++notifications; };
        const auto snapshot = [&]
        {
            return owner->project.state().toXmlString() + owner->projectIdentity +
                   juce::String(juce::int64(owner->revision)) + juce::String(int(owner->dirty)) +
                   owner->undo.getUndoDescriptions().joinIntoString("|") +
                   owner->undo.getRedoDescriptions().joinIntoString("|") +
                   juce::String(owner->undo.getNumberOfUnitsTakenUpByStoredCommands());
        };
        const auto commit = [&](std::unique_ptr<PreparedControllerChange> &ticket)
        {
            check(owner->validatePreparedChange(*ticket).wasOk() && owner->commitPreparedChange(*ticket),
                  "Actual prepared snapshot and actual manager commit");
        };
        auto capture = owner->project;
        capture.plugin(oldA)->state = state("fresh-old-A");
        capture.plugin(background)->state = state("fresh-background");
        const effect_chain::Chain chain{"Portable chain",
                                        true,
                                        {{"New A", descriptor, state("new-A"), false, .25f},
                                         {"New B", descriptor, state("new-B"), true, .75f}}};
        const auto original = snapshot();
        std::unique_ptr<PreparedControllerChange> discarded;
        check(owner->prepareMixerChainReplacement(capture, 1, chain, discarded).wasOk(),
              "Prepare occupied chain");
        const auto abandoned = mixerInsert(*discarded->destinationBeforeCommit(), 1)->effects.front().id;
        discarded.reset();
        std::unique_ptr<PreparedControllerChange> prepared;
        check(owner->prepareMixerChainReplacement(capture, 1, chain, prepared).wasOk(),
              "Retry after discard");
        const auto destination = *prepared->destinationBeforeCommit();
        const auto *insert = mixerInsert(destination, 1);
        const auto newA = insert->effects[0].id, newB = insert->effects[1].id;
        auto expected = capture;
        auto *expectedInsert = const_cast<MixerInsert *>(mixerInsert(expected, 1));
        expectedInsert->effects = {{newA, "New A", descriptor, state("new-A"), false, false},
                                   {newB, "New B", descriptor, state("new-B"), false, true}};
        expectedInsert->effects[0].wet = .25f;
        expectedInsert->effects[1].wet = .75f;
        expectedInsert->effectsBypassed = true;
        expected.nextId = newB + 1;
        check(newA == abandoned + 2 && newB == newA + 1 &&
                  destination.state().isEquivalentTo(expected.state()) && snapshot() == original &&
                  !owner->undo.canUndo() && !prepared->nativeEffectHistoryBeforeCommit() &&
                  !applicationCalls && !notifications,
              "Discard never recycles chain IDs; literal ordered state/flags/IDs and all other project "
              "values preserved");
        const auto *oldOutput = prepared.get();
        auto invalid = capture;
        invalid.tempo += 1;
        check(owner->prepareMixerChainReplacement(invalid, 1, chain, prepared).failed() &&
                  prepared.get() == oldOutput && snapshot() == original,
              "Mixed capture fails without changing output/history/model");
        commit(prepared);
        check(owner->undo.getUndoDescriptions().size() == 1 && !notifications && !applicationCalls,
              "One real history action; no observer/application callback during prepare/commit");
        // Current runtime edits must populate BOTH actual replacement history endpoints.
        auto undoCapture = owner->project;
        undoCapture.plugin(newA)->state = state("new-A-edited-live");
        undoCapture.plugin(background)->state = state("background-at-Undo");
        std::unique_ptr<PreparedControllerChange> undo;
        check(owner->prepareMixerEffectHistory(HistoryDirection::undo, undoCapture, undo).wasOk(),
              "Prepare actual occupied-chain Undo with fresh current state");
        check(undo->destinationBeforeCommit()->plugin(oldA)->state == capture.plugin(oldA)->state &&
                  undo->destinationBeforeCommit()->plugin(background)->state == state("background-at-Undo"),
              "Undo restores captured removed state and retains current unrelated native state");
        commit(undo);
        auto redoCapture = owner->project;
        redoCapture.plugin(oldA)->state = state("old-A-edited-after-Undo");
        redoCapture.plugin(background)->state = state("background-at-Redo");
        std::unique_ptr<PreparedControllerChange> redo;
        check(owner->prepareMixerEffectHistory(HistoryDirection::redo, redoCapture, redo).wasOk() &&
                  redo->destinationBeforeCommit()->plugin(newA)->state == state("new-A-edited-live") &&
                  redo->destinationBeforeCommit()->plugin(background)->state == state("background-at-Redo"),
              "Redo restores fresh removed new-worker state, exact same IDs and latest unrelated bytes");
        commit(redo);
        std::unique_ptr<PreparedControllerChange> again;
        check(owner->prepareMixerEffectHistory(HistoryDirection::undo, owner->project, again).wasOk() &&
                  again->destinationBeforeCommit()->plugin(oldA)->state == state("old-A-edited-after-Undo"),
              "Opposite edge really owns fresh history state, not an incoming capture alias");
        commit(again);
        std::unique_ptr<PreparedControllerChange> branch;
        check(owner->prepareMixerChainReplacement(owner->project, 1, chain, branch).wasOk() &&
                  mixerInsert(*branch->destinationBeforeCommit(), 1)->effects.front().id > newB,
              "Abandoning the Redo branch cannot recycle published chain IDs");
        auto newer = owner->project;
        newer.plugin(background)->state = state("late-canonical-change");
        owner->project = newer;
        check(owner->validatePreparedChange(*branch).failed() && !owner->commitPreparedChange(*branch),
              "Actual current canonical bytes invalidate prepared publication, even without revision change");
        auto other = std::make_unique<Controller>();
        other->replace(project);
        auto mixed = project;
        mixed.tempo += 1;
        for (auto &row : mixed.mixer)
            if (row.index == 1)
                std::swap(row.effects[0], row.effects[1]);
        std::unique_ptr<PreparedControllerChange> mixedTicket;
        check(other->prepareSnapshotChange(mixed, project, "Mixed original history", true, false, true,
                                           mixedTicket)
                      .wasOk() &&
                  other->validatePreparedChange(*mixedTicket).wasOk() &&
                  other->commitPreparedChange(*mixedTicket),
              "Build real mixed snapshot history for negative admission");
        check(other->prepareMixerEffectHistory(HistoryDirection::undo, other->project, mixedTicket).failed(),
              "Effect reorder plus tempo cannot masquerade as pure structural effect history");
        other->replace(project);
        CommandArgs move;
        move.target = oldA;
        move.lane = 1;
        check(other->execute("plugin.effect.move", move).wasOk(), "Actual canonical reorder action");
        auto reorderCapture = other->project;
        reorderCapture.plugin(oldA)->state = state("reorder-fresh");
        check(other->prepareMixerEffectHistory(HistoryDirection::undo, reorderCapture, mixedTicket).wasOk() &&
                  mixerInsert(*mixedTicket->destinationBeforeCommit(), 1)->effects.front().id == oldA &&
                  mixedTicket->destinationBeforeCommit()->plugin(oldA)->state == state("reorder-fresh"),
              "Reorder history preserves identity/order and freshly captured current opaque state");
        other->replace(project);
        const auto emptyBefore = other->project.state();
        const auto emptyRevision = other->revision;
        const effect_chain::Chain emptyChain{"Portable name must not rename insert", false, {}};
        check(other->prepareMixerChainReplacement(other->project, 3, emptyChain, mixedTicket).wasOk() &&
                  mixedTicket->isNoOp() && other->validatePreparedChange(*mixedTicket).wasOk() &&
                  other->commitPreparedChange(*mixedTicket) &&
                  other->project.state().isEquivalentTo(emptyBefore) && other->revision == emptyRevision &&
                  !other->dirty && !other->undo.canUndo(),
              "Empty identical Paste has no model/history/revision/dirty effect and never renames insert");
        auto bypassOnly = emptyChain;
        bypassOnly.effectsBypassed = true;
        check(other->prepareMixerChainReplacement(other->project, 3, bypassOnly, mixedTicket).wasOk() &&
                  !mixedTicket->isNoOp() && other->validatePreparedChange(*mixedTicket).wasOk() &&
                  other->commitPreparedChange(*mixedTicket) &&
                  mixerInsert(other->project, 3)->effectsBypassed &&
                  other->undo.getUndoDescriptions().size() == 1 && other->undoEditChecked().wasOk() &&
                  other->project.state().isEquivalentTo(emptyBefore),
              "Empty-chain bypass difference is a real ordinary one-Undo model change");
        auto automated = project;
        automated.plugin(oldA)->automationBindings.push_back({17, 1, 0, .25f, "Original", "unit"});
        automated.automation.initialValues.push_back(
            {{AutomationParameter::pluginNormalized, oldA, 17}, .25f});
        other->replace(automated);
        check(other->prepareMixerChainReplacement(automated, 1, chain, mixedTicket).failed() &&
                  other->project.state().isEquivalentTo(automated.state()) && !other->undo.canUndo(),
              "Actual referenced target refuses rather than pruning automation declarations/initialization");
        check(other->beginGesture("owned"), "Real gesture begins");
        check(other->prepareMixerChainReplacement(other->project, 1, chain, mixedTicket).failed() &&
                  other->prepareMixerEffectHistory(HistoryDirection::undo, other->project, mixedTicket)
                      .failed() &&
                  other->hasGesture() && other->gestureOwner() == "owned",
              "Neither preparation path takes over an owned gesture");
        check(!applicationCalls && !notifications,
              "No application hook or observer was delivered implicitly");
        return juce::Result::ok();
    }
    catch (const std::exception &error)
    {
        return juce::Result::fail(error.what());
    }
}
} // namespace vibera::acceptance::mixerChainCommand
