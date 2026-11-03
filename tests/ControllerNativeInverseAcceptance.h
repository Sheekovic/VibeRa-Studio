// SPDX-License-Identifier: AGPL-3.0-or-later
// Original actual Controller/history tests with SYNTHETIC opaque metadata.
// No native provider/capture/graph/OS interaction is qualified by this test.
#pragma once
#include "ControllerRoutingCaptureAcceptance.h"
#include "PreparedHistoryInverseAcceptance.h"
namespace controller_native_inverse_acceptance
{
using namespace vibera;
using controller_routing_capture_acceptance::opaque;
using controller_routing_capture_acceptance::originalSpec;
inline void commit(Controller &c, PreparedControllerChange &change)
{
    require(c.validatePreparedChange(change).wasOk(), "Validate actual Controller prepared history");
    const auto allocations = callbackAllocations.load(), deletions = callbackDestructions.load();
    const bool previous = callbackAllocationProbe;
    callbackAllocationProbe = true;
    const bool committed = c.commitPreparedChange(change);
    callbackAllocationProbe = previous;
    require(committed && callbackAllocations.load() == allocations &&
                callbackDestructions.load() == deletions,
            "Canonical inverse commit uses actual manager and no instrumented C++ allocation/destruction");
}
inline PluginSpec append(Controller &c)
{
    auto before = c.project, next = before;
    auto spec = originalSpec(next, "Original inverse metadata fixture", false);
    next.mixer[1].effects.push_back(spec);
    std::unique_ptr<PreparedControllerChange> ticket;
    require(c.prepareSnapshotChange(next, before, "Append original chain", true, false, true, ticket).wasOk(),
            "Actual canonical append transaction prepares");
    commit(c, *ticket);
    return spec;
}
inline void positiveTests()
{
    Controller c;
    const auto effect = append(c);
    const auto cursor = c.undo.getUndoDescription();
    const auto time = c.undo.getTimeOfUndoTransaction();
    auto captured = c.project;
    captured.plugin(effect.id)->state = opaque("current real-capture-shaped B, different byte count");
    const auto before = c.project.state().toXmlString();
    const auto revision = c.revision;
    std::unique_ptr<PreparedControllerChange> inverse;
    require(c.prepareStoppedEffectHistory(HistoryDirection::undo, 1, captured, inverse).wasOk() && inverse &&
                c.project.state().toXmlString() == before && c.revision == revision,
            "Dirty-native-shaped inverse preparation leaves canonical source/revision untouched");
    auto witness = inverse->nativeEffectHistoryBeforeCommit();
    require(witness && witness->capturedSource->state().isEquivalentTo(captured.state()) &&
                witness->destination == inverse->destinationBeforeCommit() && witness->insert == 1 &&
                witness->direction == HistoryDirection::undo && !witness->destination->plugin(effect.id),
            "Typed precommit witness identifies exact captured source/destination for SAME ticket admission");
    commit(c, *inverse);
    require(!inverse->nativeEffectHistoryBeforeCommit() && !inverse->destinationBeforeCommit() &&
                !c.project.plugin(effect.id) && c.undo.getRedoDescription() == cursor &&
                c.undo.getTimeOfRedoTransaction() == time,
            "Committed witness cannot expose swapped-out source as destination");
    HistoryDestination redo;
    require(c.previewHistoryDestination(HistoryDirection::redo, redo).wasOk() &&
                redo.destination().plugin(effect.id)->state == captured.plugin(effect.id)->state,
            "Real redo transaction owns exact captured inverse rather than stale append state");
    std::unique_ptr<PreparedControllerChange> redoTicket;
    require(c.prepareStoppedEffectHistory(HistoryDirection::redo, 1, c.project, redoTicket).wasOk(),
            "Same internal primitive admits absent-plugin Redo without inventing fresh state");
    commit(c, *redoTicket);
    require(c.project.plugin(effect.id)->state == captured.plugin(effect.id)->state &&
                c.undo.getUndoDescription() == cursor && c.undo.getTimeOfUndoTransaction() == time,
            "Redone chain retains ID/selection/current captured state and original history metadata");
    // Later native capture changes metadata outside musical history. The next
    // pure inverse must carry C into its actual redo baseline exactly once.
    auto newer = c.project;
    newer.plugin(effect.id)->state = opaque("later captured C");
    std::unique_ptr<PreparedControllerChange> refresh;
    require(c.prepareNativeStateRefresh(newer, refresh).wasOk(), "Prepare existing no-file native refresh");
    commit(c, *refresh);
    require(c.prepareStoppedEffectHistory(HistoryDirection::undo, 1, c.project, inverse).wasOk(),
            "Earlier chain history admits independently refreshed native capture");
    commit(c, *inverse);
    require(c.redoEditChecked().wasOk() &&
                c.project.plugin(effect.id)->state == newer.plugin(effect.id)->state,
            "Ordinary actual Redo sees replaced immutable inverse baseline C");
}
inline void negativeTests()
{
    for (int kind = 0; kind < 6; ++kind)
    {
        Controller c;
        const auto effect = append(c);
        auto captured = c.project;
        captured.plugin(effect.id)->state = opaque("fresh capture");
        if (kind == 0)
            captured.tempo += 1;
        if (kind == 1)
            captured.plugin(effect.id)->descriptor += "changed";
        if (kind == 2)
        {
            captured.plugin(effect.id)->id = captured.allocate();
        }
        if (kind == 3)
            c.project.channels.front().gain = .234375f;
        if (kind == 4)
        {
            require(c.beginGesture(), "Original gesture setup");
        }
        if (kind == 5)
        {
            c.undo.beginNewTransaction("foreign");
            c.undo.perform(new vibera::draft::prepared_history::ForeignAction());
        }
        const auto before = c.project.state().toXmlString();
        const auto units = c.undo.getNumberOfUnitsTakenUpByStoredCommands();
        std::unique_ptr<PreparedControllerChange> refused;
        require(c.prepareStoppedEffectHistory(HistoryDirection::undo, 1, captured, refused).failed() &&
                    !refused && c.project.state().toXmlString() == before &&
                    c.undo.getNumberOfUnitsTakenUpByStoredCommands() == units,
                "Musical/descriptor/ID/stale/active gesture/foreign history refuses without model/history "
                "mutation");
    }
    {
        Controller c;
        auto before = c.project, next = before;
        auto spec = originalSpec(next, "Mixed action", false);
        next.mixer[1].effects.push_back(spec);
        next.tempo += 1;
        std::unique_ptr<PreparedControllerChange> transaction;
        require(c.prepareSnapshotChange(next, before, "Mixed action", true, false, true, transaction).wasOk(),
                "Prepare actual mixed append action");
        commit(c, *transaction);
        std::unique_ptr<PreparedControllerChange> refused;
        require(c.prepareStoppedEffectHistory(HistoryDirection::undo, 1, c.project, refused).failed() &&
                    !refused,
                "Reconciliation cannot exempt unrelated changes or mixed musical/native transactions");
    }
    {
        Controller c;
        const auto effect = append(c);
        auto capture = c.project;
        capture.plugin(effect.id)->state = opaque("capture B");
        std::unique_ptr<PreparedControllerChange> stale;
        require(c.prepareStoppedEffectHistory(HistoryDirection::undo, 1, capture, stale).wasOk(),
                "Prepare stale inverse");
        c.project.plugin(effect.id)->state = opaque("newer capture C without revision");
        const auto before = c.project.state().toXmlString();
        require(c.validatePreparedChange(*stale).failed() && !c.commitPreparedChange(*stale) &&
                    c.project.state().toXmlString() == before,
                "Exact source XML rejects newer opaque capture even at same revision");
    }
}
inline void faultTests()
{
    int faults = 0, successes = 0;
    // Bounded sampled Controller sites. The manager test separately sweeps its
    // own C++ allocation sites to success; this is NOT an exhaustive XML/CRT OOM test.
    for (int fault : {0, 1, 2, 4, 8, 16, 32, 64, 128, -1})
    {
        Controller c;
        const auto effect = append(c);
        auto capture = c.project;
        capture.plugin(effect.id)->state = opaque("captured fault-oracle B");
        std::unique_ptr<PreparedControllerChange> sentinel;
        require(c.prepareSnapshotChange(c.project, c.project, {}, false, false, c.dirty, sentinel).wasOk(),
                "Preowned output sentinel");
        const auto *sentinelAddress = sentinel.get();
        const auto before = c.project.state().toXmlString();
        const auto revision = c.revision;
        const auto units = c.undo.getNumberOfUnitsTakenUpByStoredCommands();
        juce::Array<const juce::UndoableAction *> actions;
        c.undo.getActionsInUndoTransaction(actions);
        const auto *original = actions[0];
        preparationAllocationFailureHit = false;
        preparationAllocationFailureAfter = fault;
        const auto result = c.prepareStoppedEffectHistory(HistoryDirection::undo, 1, capture, sentinel);
        preparationAllocationFailureAfter = -1;
        juce::Array<const juce::UndoableAction *> after;
        c.undo.getActionsInUndoTransaction(after);
        require(c.project.state().toXmlString() == before && c.revision == revision &&
                    c.undo.getNumberOfUnitsTakenUpByStoredCommands() == units && after.size() == 1 &&
                    after[0] == original,
                "Each actual C++ preparation failure retains source, actual owner/cursor/cost and revision");
        if (preparationAllocationFailureHit)
        {
            ++faults;
            require(result.failed() && sentinel.get() == sentinelAddress,
                    "Failed inverse preserves caller's preowned output sentinel");
        }
        else
        {
            require(result.wasOk() && sentinel && sentinel->nativeEffectHistoryBeforeCommit(),
                    "Non-injected Controller preparation yields a complete real-manager inverse ticket");
            ++successes;
        }
    }
    require(successes > 0 && faults > 0,
            "Sampled actual C++ faults and unarmed successful inverse preparation are covered");
}
inline void tests()
{
    positiveTests();
    negativeTests();
    faultTests();
}
} // namespace controller_native_inverse_acceptance
