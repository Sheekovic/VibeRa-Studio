// SPDX-License-Identifier: AGPL-3.0-or-later
// Original real-JUCE manager extension tests. Exact execution evidence is retained separately.
#pragma once
#include "PreparedHistoryMutationAcceptance.h"

namespace vibera::draft::prepared_history_inverse
{
using namespace prepared_history;
inline const FrozenAction *edge(juce::UndoManager &manager, bool undoing)
{
    juce::Array<const juce::UndoableAction *> actions;
    if (undoing)
        manager.getActionsInUndoTransaction(actions);
    else
        manager.getActionsInRedoTransaction(actions);
    return actions.size() == 1 ? dynamic_cast<const FrozenAction *>(actions[0]) : nullptr;
}
struct MutatingReader
{
    juce::UndoManager &manager;
    int calls = 0;
    static bool read(const juce::UndoableAction &action, int &cost, void *context) noexcept
    {
        auto &self = *static_cast<MutatingReader *>(context);
        if (++self.calls == 1)
            self.manager.beginNewTransaction(); // Empty metadata, no fallible String allocation.
        return cachedUnits(action, cost, nullptr);
    }
};
inline void replacementAndRetirementTests()
{
    int value = 0;
    Counters calls;
    juce::UndoManager manager;
    ordinary(manager, value, 1, calls, "first");
    ordinary(manager, value, 2, calls, "captured chain");
    const auto before = snapshot(manager, value);
    const auto stamp = manager.getTimeOfUndoTransaction();
    const auto *original = edge(manager, true);
    const auto destroys = calls.destroys, performs = calls.performs, undoes = calls.undoes;
    auto ticket = manager.prepareHistoryReplacement(
        true, std::make_unique<FrozenAction>(value, 1, 200, calls, 7), 7, &cachedUnits, nullptr);
    require(ticket && snapshot(manager, value) == before && edge(manager, true) == original,
            "Inverse replacement prepares without changing real action, cursor, metadata or model");
    Apply inverse{value, 1};
    noAllocationCommit(manager, *ticket, inverse);
    require(value == 1 && edge(manager, false) && edge(manager, false)->after == 200 &&
                edge(manager, false)->before == 1 && manager.getRedoDescription() == "captured chain" &&
                manager.getTimeOfRedoTransaction() == stamp &&
                manager.getNumberOfUnitsTakenUpByStoredCommands() == 8 && calls.destroys == destroys &&
                calls.performs == performs && calls.undoes == undoes,
            "Actual cursor transaction now owns exact captured inverse, cost/name/time; old owner remains "
            "pinned");
    ticket.reset();
    require(calls.destroys == destroys + 1, "Only CONTROL ticket retirement deletes replaced owner");
    auto redo = manager.prepareHistoryReplacement(
        false, std::make_unique<FrozenAction>(value, 10, 201, calls, 9), 9, &cachedUnits, nullptr);
    require(bool(redo), "Real Redo side admits its own captured baseline replacement");
    Apply forward{value, 201};
    noAllocationCommit(manager, *redo, forward);
    require(
        edge(manager, true) && edge(manager, true)->before == 10 && edge(manager, true)->after == 201 &&
            manager.getNumberOfUnitsTakenUpByStoredCommands() == 10 &&
            manager.getTimeOfUndoTransaction() == stamp && manager.getUndoDescription() == "captured chain",
        "Redo moves actual cursor and replaces both immutable edge snapshots without renaming transaction");
    // Exercise the unchanged generic API after explicit replacement; it must see
    // the actual real-manager snapshot, not an app-owned shadow cursor.
    require(manager.undo() && value == 10 && manager.redo() && value == 201,
            "Ordinary JUCE Undo/Redo executes the actual replaced action");
}
inline void stashPruneAndAdmissionTests()
{
    {
        int value = 0;
        Counters calls;
        juce::UndoManager manager;
        ordinary(manager, value, 1, calls, "first");
        ordinary(manager, value, 2, calls, "stashed future");
        require(manager.undo(), "Prepare actual future for branch");
        ordinary(manager, value, 10, calls, "branch");
        auto ticket = manager.prepareHistoryReplacement(
            true, std::make_unique<FrozenAction>(value, 1, 100, calls), 1, &cachedUnits, nullptr);
        require(bool(ticket), "Inverse replacement admits actual branch and its pre-existing stash");
        Apply back{value, 1};
        noAllocationCommit(manager, *ticket, back);
        require(manager.redo() && value == 100,
                "Redo real replacement before checking preserved legacy stash");
        ordinary(manager, value, 101, calls, "probe preserved stash");
        require(manager.undoCurrentTransactionOnly() && value == 100 &&
                    manager.getRedoDescription() == "stashed future" && manager.redo() && value == 2,
                "Replaced transaction preserves legacy future-stash restoration and real owner identities");
    }
    {
        int value = 0;
        Counters calls;
        juce::UndoManager manager;
        ordinary(manager, value, 1, calls, "pruned first");
        ordinary(manager, value, 2, calls, "retained second");
        ordinary(manager, value, 3, calls, "selected");
        manager.setMaxNumberOfStoredUnits(3, 1);
        const auto destroys = calls.destroys;
        auto ticket = manager.prepareHistoryReplacement(
            true, std::make_unique<FrozenAction>(value, 2, 300, calls, 2), 2, &cachedUnits, nullptr);
        require(bool(ticket), "Replacement prepares only earlier budget pruning");
        Apply back{value, 2};
        noAllocationCommit(manager, *ticket, back);
        require(manager.getUndoDescriptions().size() == 1 &&
                    manager.getUndoDescription() == "retained second" &&
                    manager.getRedoDescription() == "selected" &&
                    manager.getNumberOfUnitsTakenUpByStoredCommands() == 3 && calls.destroys == destroys,
                "Prune retains selected/future while removed owners stay pinned");
        ticket.reset();
        require(calls.destroys == destroys + 2, "Retirement deletes pruned and replaced owners exactly once");
    }
    for (int kind = 0; kind < 6; ++kind)
    {
        int value = 0;
        Counters calls;
        juce::UndoManager manager;
        ordinary(manager, value, 1, calls, "selected");
        if (kind == 0)
            manager.perform(new FrozenAction(value, 1, 2, calls)); // Same actual multi-action set.
        if (kind == 1)
        {
            manager.beginNewTransaction("foreign");
            manager.perform(new ForeignAction());
        }
        if (kind == 2)
        {
            ordinary(manager, value, 2, calls, "protected future");
            manager.undo();
            manager.setMaxNumberOfStoredUnits(1, 1);
        }
        const auto before = snapshot(manager, value);
        const auto *actual = edge(manager, true);
        std::unique_ptr<juce::UndoableAction> action;
        if (kind == 3)
            action = std::make_unique<ForeignAction>();
        else
            action = std::make_unique<FrozenAction>(value, 0, 100, calls, kind == 2 ? 2 : kind == 4 ? -1 : 1);
        auto ticket = manager.prepareHistoryReplacement(true, std::move(action),
                                                        kind == 5   ? 2
                                                        : kind == 2 ? 2
                                                        : kind == 4 ? -1
                                                                    : 1,
                                                        &cachedUnits, nullptr);
        require(!ticket && snapshot(manager, value) == before && edge(manager, true) == actual,
                "Multi-action/foreign/over-budget/negative/wrong-cost replacement refuses without cursor "
                "mutation");
    }
    {
        int value = 0;
        Counters calls;
        juce::UndoManager manager;
        ordinary(manager, value, 1, calls, "old");
        ordinary(manager, value, 2, calls, "selected");
        auto ticket = manager.prepareHistoryReplacement(
            true, std::make_unique<FrozenAction>(value, 1, 2, calls), 1, &cachedUnits, nullptr);
        require(bool(ticket), "Prepare explicit freshness ticket");
        manager.setCurrentTransactionName("renamed");
        const auto before = snapshot(manager, value);
        Apply apply{value, -1};
        require(!manager.commitPreparedMutation(*ticket, &Apply::swapCanonical, &apply) && apply.calls == 0 &&
                    snapshot(manager, value) == before,
                "Metadata mutation invalidates replacement before canonical apply");
        juce::UndoManager foreign;
        require(!foreign.isPreparedMutationCurrent(*ticket), "Owner cannot cross managers");
    }
    {
        int value = 0;
        Counters calls;
        juce::UndoManager manager;
        ordinary(manager, value, 1, calls, "selected");
        ordinary(manager, value, 2, calls, "future");
        manager.undo();
        manager.setMaxNumberOfStoredUnits(std::numeric_limits<int>::max(), 1);
        const auto before = snapshot(manager, value);
        require(!manager.prepareHistoryReplacement(
                    true, std::make_unique<FrozenAction>(value, 0, 1, calls, std::numeric_limits<int>::max()),
                    std::numeric_limits<int>::max(), &cachedUnits, nullptr) &&
                    snapshot(manager, value) == before,
                "Replacement cannot overflow units or prune the selected target/future to fit");
    }
    {
        int value = 0;
        Counters calls;
        juce::UndoManager manager;
        ordinary(manager, value, 1, calls, "minimum retained");
        manager.setMaxNumberOfStoredUnits(1, 1);
        auto ticket = manager.prepareHistoryReplacement(
            true, std::make_unique<FrozenAction>(value, 0, 1, calls, 7), 7, &cachedUnits, nullptr);
        require(bool(ticket),
                "Existing minimum-transactions policy permits a sole action above the soft unit budget");
        Apply back{value, 0};
        noAllocationCommit(manager, *ticket, back);
        require(manager.getNumberOfUnitsTakenUpByStoredCommands() == 7 && manager.canRedo(),
                "Soft budget compatibility does not delete the minimum retained inverse");
    }
    {
        int value = 0;
        Counters calls;
        juce::UndoManager manager;
        ordinary(manager, value, 1, calls, "owned");
        const auto *original = edge(manager, true);
        MutatingReader reader{manager};
        auto ticket = manager.prepareHistoryReplacement(
            true, std::make_unique<FrozenAction>(value, 0, 7, calls), 1, &MutatingReader::read, &reader);
        require(!ticket && value == 1 && edge(manager, true) == original && reader.calls == 1 &&
                    manager.getNumActionsInCurrentTransaction() == 0,
                "Reader-triggered metadata mutation invalidates generation before stale borrowed owner "
                "use/apply");
    }
    {
        int value = 0;
        Counters calls;
        std::unique_ptr<juce::UndoManager::PreparedHistoryMutation> abandoned;
        {
            juce::UndoManager manager;
            ordinary(manager, value, 1, calls, "one");
            ordinary(manager, value, 2, calls, "two");
            abandoned = manager.prepareHistoryReplacement(
                true, std::make_unique<FrozenAction>(value, 1, 9, calls), 1, &cachedUnits, nullptr);
            require(bool(abandoned), "Prepared replacement can outlive its originating manager on CONTROL");
        }
        require(calls.destroys == 2,
                "Manager destruction retires its actual owners while stale ticket only borrows them");
        abandoned.reset();
        require(calls.destroys == 3, "Stale ticket retirement deletes its sole private owner exactly once");
    }
}
inline void preparationFailureTests()
{
    bool completed = false;
    int rejected = 0;
    for (int fault = 0; fault < 128 && !completed; ++fault)
    {
        int value = 0;
        Counters calls;
        juce::UndoManager manager;
        ordinary(manager, value, 1, calls, "one");
        ordinary(manager, value, 2, calls, "two");
        const auto before = snapshot(manager, value);
        const auto *original = edge(manager, true);
        const int destroys = calls.destroys;
        auto action = std::make_unique<FrozenAction>(value, 1, 200, calls, 5);
        std::unique_ptr<juce::UndoManager::PreparedHistoryMutation> ticket;
        preparationAllocationFailureHit = false;
        preparationAllocationFailureAfter = fault;
        try
        {
            ticket = manager.prepareHistoryReplacement(true, std::move(action), 5, &cachedUnits, nullptr);
        }
        catch (const std::bad_alloc &)
        {
        }
        preparationAllocationFailureAfter = -1;
        const bool injected = preparationAllocationFailureHit;
        require(snapshot(manager, value) == before && edge(manager, true) == original,
                "Every real C++ preparation fault leaves actual action/cursor/cost/model unchanged");
        if (injected)
        {
            ++rejected;
            require(!ticket && calls.destroys == destroys + 1,
                    "Fault retirement destroys only private replacement, never borrowed real actions");
        }
        else
        {
            require(bool(ticket),
                    "First no-fault actual replacement preparation succeeds within bounded sites");
            ticket.reset();
            require(calls.destroys == destroys + 1,
                    "Abandoned valid ticket retires only private replacement");
            completed = true;
        }
    }
    require(completed && rejected > 0,
            "Bounded real allocation-site sweep reaches valid preparation after tested faults");
}
inline void tests()
{
    replacementAndRetirementTests();
    stashPruneAndAdmissionTests();
    preparationFailureTests();
}
} // namespace vibera::draft::prepared_history_inverse
