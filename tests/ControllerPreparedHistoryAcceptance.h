// SPDX-License-Identifier: AGPL-3.0-or-later
// Original source-only draft. Include after existing require + allocation probe.
#pragma once
#include "../src/Commands.h"
#include <stdexcept>
#include <tuple>

namespace controller_prepared_acceptance
{
using namespace vibera;
struct Snapshot
{
    juce::String model, identity, undoNames, redoNames, owner;
    std::uint64_t revision, serial;
    bool dirty, gesture, quantizer, pendingController, pendingUndo;
    int units;
    std::vector<std::uintptr_t> undoActions, redoActions;
    bool operator==(const Snapshot &) const = default;
};
inline Snapshot snapshot(const Controller &c)
{
    Snapshot result{c.project.state().toXmlString(),
                    c.projectIdentity,
                    c.undo.getUndoDescriptions().joinIntoString("|"),
                    c.undo.getRedoDescriptions().joinIntoString("|"),
                    c.gestureOwner(),
                    c.revision,
                    c.gestureSerial(),
                    c.dirty,
                    c.hasGesture(),
                    c.hasQuantizerPreview(),
                    c.hasPendingControllerNotification(),
                    c.hasPendingUndoNotification(),
                    c.undo.getNumberOfUnitsTakenUpByStoredCommands(),
                    {},
                    {}};
    juce::Array<const juce::UndoableAction *> actions;
    c.undo.getActionsInUndoTransaction(actions);
    for (auto *a : actions)
        result.undoActions.push_back(reinterpret_cast<std::uintptr_t>(a));
    actions.clear();
    c.undo.getActionsInRedoTransaction(actions);
    for (auto *a : actions)
        result.redoActions.push_back(reinterpret_cast<std::uintptr_t>(a));
    return result;
}
inline CommandArgs gain(const Controller &c, double value)
{
    CommandArgs a;
    a.channel = c.project.channels.front().id;
    a.value = value;
    return a;
}
struct Foreign final : juce::UndoableAction
{
    int &calls;
    explicit Foreign(int &c) : calls(c) {}
    bool perform() override
    {
        ++calls;
        return true;
    }
    bool undo() override
    {
        ++calls;
        return false;
    }
    int getSizeInUnits() override
    {
        return 1;
    }
};
// Probe only the publication window, never preparation, retirement or observers.
inline bool measuredCommit(Controller &c, PreparedControllerChange &prepared)
{
    callbackAllocations = 0;
    callbackDestructions = 0;
    callbackAllocationProbe = true;
    const bool committed = c.commitPreparedChange(prepared);
    callbackAllocationProbe = false;
    require(callbackAllocations.load() == 0 && callbackDestructions.load() == 0,
            "Prepared canonical+actual history commit allocates/deletes no C++ objects");
    return committed;
}
inline int faultScenario = -1, faultBudget = -1;
inline void tests(const juce::File &folder)
{
    // Preserve the exact fatal site if upstream noexcept code terminates during
    // fault injection. Diagnostics run only after disarming; never in DSP.
    struct TerminateDiagnostic
    {
        std::terminate_handler previous = std::set_terminate([]
        {
            preparationAllocationFailureAfter = -1;
            std::cerr << "Prepared Controller terminate: scenario=" << faultScenario
                      << " budget=" << faultBudget << " assertions=" << assertions << "\n"
                      << juce::SystemStats::getStackBacktrace() << std::flush;
            std::abort();
        });
        ~TerminateDiagnostic() { std::set_terminate(previous); }
    } terminateDiagnostic;
    {
        Controller c;
        const auto original = c.project.state();
        int notifications = 0;
        c.changed = [&] { ++notifications; };
        require(c.execute("channel.gain", gain(c, .31)).wasOk(), "Prepared canonical gain");
        const auto gained = c.project.state();
        require(notifications == 1 && c.undo.canUndo() && !c.hasPendingControllerNotification(),
                "Canonical result delivered once after actual history exists");
        require(c.undoEditChecked().wasOk() && original.isEquivalentTo(c.project.state()),
                "Checked actual-manager Undo uses immutable exact snapshot");
        require(c.redoEditChecked().wasOk() && gained.isEquivalentTo(c.project.state()),
                "Checked actual-manager Redo uses immutable exact snapshot");
        juce::Array<const juce::UndoableAction *> actions;
        c.undo.getActionsInUndoTransaction(actions);
        callbackAllocations = 0;
        callbackDestructions = 0;
        callbackAllocationProbe = true;
        const auto cached = const_cast<juce::UndoableAction *>(actions.getFirst())->getSizeInUnits();
        callbackAllocationProbe = false;
        require(cached >= 0 && callbackAllocations.load() == 0 && callbackDestructions.load() == 0,
                "StateAction unit getter uses immutable construction-time checked cache");
        std::unique_ptr<PreparedControllerChange> ticket;
        const auto before = snapshot(c);
        require(c.prepareHistoryChange(HistoryDirection::undo, ticket).wasOk() && before == snapshot(c),
                "History preparation is nonmutating");
        require(ticket->destinationBeforeCommit() &&
                    original.isEquivalentTo(ticket->destinationBeforeCommit()->state()),
                "Prepared destination is available only before consuming publication");
        require(c.validatePreparedChange(*ticket).wasOk(), "Exact source admitted before commit");
        require(measuredCommit(c, *ticket) && ticket->wasCommitted() && !ticket->destinationBeforeCommit() &&
                    original.isEquivalentTo(c.project.state()) && notifications == 3 &&
                    c.hasPendingControllerNotification() && c.hasPendingUndoNotification(),
                "Commit publishes real model/history and pending outcomes without callbacks");
        require(!measuredCommit(c, *ticket), "Same actual-manager ticket cannot apply twice");
        require(c.deliverPendingNotifications() && notifications == 4, "Separate observer request succeeds");
    }
    {
        Controller c;
        require(c.execute("channel.gain", gain(c, .32)).wasOk(), "Stale fixture edit");
        std::unique_ptr<PreparedControllerChange> ticket;
        require(c.prepareHistoryChange(HistoryDirection::undo, ticket).wasOk(), "Stale fixture prepare");
        c.project.name = "Direct edit without notify";
        const auto direct = snapshot(c);
        require(c.validatePreparedChange(*ticket).failed() && !measuredCommit(c, *ticket) &&
                    direct == snapshot(c),
                "Exact source guard catches unnotified model mutation before publication");
        c.project.name = "Untitled";
        require(c.prepareHistoryChange(HistoryDirection::undo, ticket).wasOk() &&
                    c.validatePreparedChange(*ticket).wasOk(),
                "Metadata stale fixture prepared");
        c.undo.setCurrentTransactionName("Renamed after exact validation");
        const auto renamed = snapshot(c);
        require(!measuredCommit(c, *ticket) && renamed == snapshot(c),
                "Cheap real-manager generation guard catches history metadata mutation");
        Controller other;
        require(!measuredCommit(other, *ticket), "Prepared changes cannot cross Controllers");
    }
    {
        Controller c;
        const auto original = c.project.state();
        require(c.beginGesture("original-owner"), "Owned gesture begins");
        require(c.execute("channel.gain", gain(c, .42), "original-owner").wasOk() &&
                    c.execute("channel.pan", gain(c, -.2), "original-owner").wasOk(),
                "Multi-edit gesture");
        const auto after = c.project.state();
        require(c.endGestureChecked("Original combined gesture", "original-owner").wasOk() &&
                    !c.hasGesture() && c.undo.getUndoDescription() == "Original combined gesture",
                "Whole gesture completes as one real transaction");
        require(c.undoEditChecked().wasOk() && original.isEquivalentTo(c.project.state()) &&
                    !c.undo.canUndo(),
                "One Undo reverts every gesture edit and exact source IDs");
        require(c.redoEditChecked().wasOk() && after.isEquivalentTo(c.project.state()),
                "One Redo restores every gesture edit");
        require(c.beginGesture("cancel-owner") &&
                    c.execute("channel.gain", gain(c, .65), "cancel-owner").wasOk(),
                "Cancel gesture fixture");
        const auto historyBefore = c.undo.getUndoDescriptions().joinIntoString("|");
        std::unique_ptr<PreparedControllerChange> cancel;
        require(c.prepareSnapshotChange(c.project, c.project, {}, false, false, true, cancel).wasOk(),
                "Independent preparation can be abandoned");
        cancel.reset();
        require(c.cancelGestureChecked("cancel-owner").wasOk() && after.isEquivalentTo(c.project.state()) &&
                    historyBefore == c.undo.getUndoDescriptions().joinIntoString("|"),
                "Cancel preserves exact history");
    }
    {
        Controller c;
        const auto original = c.project.state();
        auto expected = c.project;
        expected.channels.front().gain = .44f;
        require(c.beginGesture("takeover-edit") &&
                    c.execute("channel.gain", gain(c, .68), "takeover-edit").wasOk(),
                "Remote takeover edit fixture");
        const auto serial = c.gestureSerial(), revision = c.revision;
        bool armed = true, sawLocal = false, edited = false, competingRefused = false;
        c.changed = [&]
        {
            if (!armed)
                return;
            armed = false;
            sawLocal = c.hasGesture() && c.gestureOwner().isEmpty() && !c.dirty &&
                       c.gestureSerial() == serial + 1 && original.isEquivalentTo(c.project.state());
            edited = c.execute("channel.gain", gain(c, .44)).wasOk();
            competingRefused = !c.beginGesture("observer-competing-owner");
        };
        require(c.beginGesture() && !armed && sawLocal && edited && competingRefused && c.hasGesture() &&
                    c.gestureOwner().isEmpty() && c.gestureSerial() == serial + 1 &&
                    c.revision == revision + 2 && expected.state().isEquivalentTo(c.project.state()) &&
                    c.dirty && !c.undo.canUndo(),
                "Takeover observer sees installed local owner and edits its exact baseline without ownership "
                "overwrite");
        require(c.endGestureChecked("Observer joins local gesture").wasOk() && !c.hasGesture() &&
                    c.undo.getUndoDescription() == "Observer joins local gesture" &&
                    c.undoEditChecked().wasOk() && original.isEquivalentTo(c.project.state()) &&
                    c.redoEditChecked().wasOk() && expected.state().isEquivalentTo(c.project.state()),
                "Reentrant local edit completes as one real exact Undo/Redo transaction");
    }
    {
        Controller c;
        const auto original = c.project.state();
        auto committed = c.project;
        committed.channels.front().gain = .43f;
        auto preview = committed;
        preview.channels.front().gain = .59f;
        require(c.beginGesture("takeover-owner") &&
                    c.execute("channel.gain", gain(c, .67), "takeover-owner").wasOk(),
                "Remote competing-owner fixture");
        const auto serial = c.gestureSerial();
        bool armed = true, sawLocal = false, edited = false, completed = false, acquired = false,
             previewed = false;
        c.changed = [&]
        {
            if (!armed)
                return;
            armed = false;
            sawLocal = c.hasGesture() && c.gestureOwner().isEmpty();
            edited = c.execute("channel.gain", gain(c, .43)).wasOk();
            completed = c.endGestureChecked("Observer committed edit").wasOk();
            acquired = c.beginGesture("observer-new-owner");
            previewed = c.execute("channel.gain", gain(c, .59), "observer-new-owner").wasOk();
        };
        require(c.beginGesture() && !armed && sawLocal && edited && completed && acquired && previewed &&
                    c.hasGesture() && c.gestureOwner() == "observer-new-owner" &&
                    c.gestureSerial() == serial + 2 && preview.state().isEquivalentTo(c.project.state()) &&
                    c.undo.getUndoDescription() == "Observer committed edit",
                "Outer takeover never overwrites a gesture acquired after the observer explicitly completes "
                "local ownership");
        require(c.cancelGestureChecked("observer-new-owner").wasOk() && !c.hasGesture() && c.dirty &&
                    committed.state().isEquivalentTo(c.project.state()) && c.undoEditChecked().wasOk() &&
                    original.isEquivalentTo(c.project.state()) && c.redoEditChecked().wasOk() &&
                    committed.state().isEquivalentTo(c.project.state()),
                "Callback-acquired cancellation preserves its preceding committed edit and exact history");
    }
    {
        Controller c;
        const auto original = c.project.state();
        require(c.beginGesture("takeover-oom") &&
                    c.execute("channel.gain", gain(c, .69), "takeover-oom").wasOk(),
                "Remote failing takeover fixture");
        int observers = 0;
        c.changed = [&]
        {
            ++observers;
            throw 37;
        };
        const auto before = snapshot(c);
        bool threw = false;
        preparationAllocationFailureHit = false;
        preparationAllocationFailureAfter = 0;
        try
        {
            (void)c.beginGesture();
        }
        catch (const std::bad_alloc &)
        {
            threw = true;
        }
        preparationAllocationFailureAfter = -1;
        require(threw && preparationAllocationFailureHit && before == snapshot(c) && observers == 0,
                "Takeover allocation failure retains exact remote ownership before intrinsic mutation");
        const auto serial = c.gestureSerial();
        require(c.beginGesture() && c.hasGesture() && c.gestureOwner().isEmpty() && !c.dirty &&
                    c.gestureSerial() == serial + 1 && original.isEquivalentTo(c.project.state()) &&
                    observers == 1 && c.hasPendingControllerNotification(),
                "Throwing takeover observer cannot undo installed local baseline/owner and remains pending");
        c.changed = {};
        require(c.deliverPendingNotifications() && c.cancelGestureChecked().wasOk() &&
                    original.isEquivalentTo(c.project.state()) && !c.dirty && !c.hasGesture(),
                "Throwing takeover notification retries independently and cancellation remains exact");
    }
    {
        Controller c;
        const auto baseline = c.project.state();
        const juce::String cancelOwner("oom-cancel"); // Prepare fixture arguments before arming.
        require(c.beginGesture(cancelOwner) && c.execute("channel.gain", gain(c, .63), cancelOwner).wasOk(),
                "OOM cancel fixture");
        c.changed = [] { throw 37; }; // No heap in the deliberately failing observer.
        preparationAllocationFailureHit = false;
        preparationAllocationFailureAfter = 0;
        const bool cancelled = c.cancelGestureChecked(cancelOwner).wasOk();
        preparationAllocationFailureAfter = -1;
        require(cancelled && !preparationAllocationFailureHit && !c.hasGesture() && !c.dirty &&
                    baseline.isEquivalentTo(c.project.state()) && c.hasPendingControllerNotification(),
                "Already owned cancellation survives next allocation fault and throwing observer");
        c.changed = {};
        require(c.deliverPendingNotifications(), "OOM cancellation observer retry succeeds");
    }
    {
        Controller c;
        const auto pristine = c.project.state();
        require(c.beginGesture() && c.execute("channel.gain", gain(c, .51)).wasOk(),
                "No-history active preview");
        require(c.undoEditChecked().wasOk() && pristine.isEquivalentTo(c.project.state()) && !c.dirty &&
                    !c.hasGesture() && !c.undo.canRedo(),
                "Empty-history Ctrl-Z cancels preview and restores saved dirtiness");
        require(c.execute("channel.gain", gain(c, .36)).wasOk(), "Pre-gesture history fixture");
        require(c.beginGesture() && c.execute("channel.gain", gain(c, .61)).wasOk(),
                "Active preview after history");
        std::unique_ptr<PreparedControllerChange> combined;
        require(c.prepareHistoryChange(HistoryDirection::undo, combined).wasOk() &&
                    c.validatePreparedChange(*combined).wasOk() && measuredCommit(c, *combined) &&
                    pristine.isEquivalentTo(c.project.state()) && c.dirty && !c.hasGesture() &&
                    c.undo.canRedo(),
                "Ctrl-Z atomically cancels preview plus previous Undo, dirty true");
        require(c.deliverPendingNotifications(), "Combined cancellation/history notification is postcommit");
    }
    {
        // Original portable metadata, deliberately unavailable: no hosting claim.
        Controller c;
        juce::PluginDescription descriptor;
        descriptor.name = "Original Prepared Controller State Fixture";
        descriptor.pluginFormatName = "VST3";
        descriptor.fileOrIdentifier = "original-prepared-history-unavailable.vst3";
        descriptor.isInstrument = true;
        PluginSpec spec;
        spec.id = c.project.allocate();
        spec.name = descriptor.name;
        spec.descriptor = descriptor.createXml()->toString();
        spec.instrument = true;
        spec.state = juce::MemoryBlock("old", 3).toBase64Encoding();
        c.project.channels.front().instrument = spec;
        auto captured = spec;
        captured.state = juce::MemoryBlock("original captured native state", 30).toBase64Encoding();
        auto expected = c.project;
        expected.plugin(spec.id)->state = captured.state;
        CommandArgs remove;
        remove.channel = c.project.channels.front().id;
        remove.capturedInstruments = {captured};
        require(c.execute("channel.delete", remove).wasOk(), "Captured baseline snapshot action");
        const auto removed = c.project.state();
        require(c.undoEditChecked().wasOk() && expected.state().isEquivalentTo(c.project.state()),
                "Prepared Undo retains exact freshly captured opaque state and allocator/source identities");
        require(c.redoEditChecked().wasOk() && removed.isEquivalentTo(c.project.state()),
                "Prepared Redo restores exact removed destination without rebuilding old baseline");
    }
    {
        Controller c;
        require(c.execute("channel.gain", gain(c, .33)).wasOk(), "Foreign transaction fixture");
        int calls = 0;
        require(c.undo.perform(new Foreign(calls)) && calls == 1, "Raw foreign fixture performs once");
        require(c.beginGesture("foreign-owner") &&
                    c.execute("channel.gain", gain(c, .62), "foreign-owner").wasOk(),
                "Foreign active gesture fixture");
        const auto before = snapshot(c);
        require(
            c.undoEditChecked().failed() && before == snapshot(c) && calls == 1,
            "Unsupported mixed history refuses without invoking, clearing or moving foreign action/gesture");
        require(c.endGestureChecked("Must refuse", "foreign-owner").failed() && before == snapshot(c),
                "New snapshot refuses foreign history and retains exact gesture baseline/ownership");
    }
    {
        Controller c;
        int attempts = 0;
        c.changed = [&]
        {
            ++attempts;
            throw std::runtime_error("Original observer failure");
        };
        require(c.execute("channel.gain", gain(c, .27)).wasOk() && c.undo.canUndo() && attempts == 1 &&
                    c.hasPendingControllerNotification(),
                "Observer exception cannot falsely fail or undo committed edit");
        const auto committed = c.project.state();
        require(!c.deliverPendingNotifications() && attempts == 2 &&
                    committed.isEquivalentTo(c.project.state()),
                "Throwing observer stays explicitly pending and retries without extra history");
        c.changed = [&] { ++attempts; };
        require(c.deliverPendingNotifications() && attempts == 3 && !c.hasPendingControllerNotification(),
                "Recovered observer clears only delivered notification");
        c.changed = [&]
        {
            ++attempts;
            c.notify();
        };
        require(c.undoEditChecked().wasOk() && c.hasPendingControllerNotification(),
                "Observer reentry leaves newly requested notification pending without recursive delivery");
        c.changed = {};
        require(c.deliverPendingNotifications(), "Reentrant notification can finish later");
    }
    {
        Controller c;
        Customization profile;
        MacroContext context{c.project.patterns.front().id, c.project.channels.front().id};
        const auto original = c.project.state();
        const auto commandArgs = gain(c, .46);
        require(profile.addMacro({"macro.history-refusal", "Refused history macro",
                                  {{"channel.gain", commandArgs, true}}}).wasOk(),
                "Actual macro fixture uses supported canonical edit");
        int calls = 0;
        require(c.undo.perform(new Foreign(calls)), "Actual foreign history admission fixture");
        const auto history = snapshot(c);
        require(profile.runMacro("macro.history-refusal", c, context).failed() && calls == 1 &&
                    original.isEquivalentTo(c.project.state()) && !c.hasGesture() && !c.dirty &&
                    c.undo.getUndoDescriptions().joinIntoString("|") == history.undoNames &&
                    !c.undo.canRedo(),
                "Macro must fail and restore baseline when real final history admission refuses");
    }
    {
        Controller c;
        Customization profile;
        MacroContext context{c.project.patterns.front().id, c.project.channels.front().id};
        const auto original = c.project.state();
        auto expected = c.project;
        expected.channels.front().pan = .2f;
        CommandArgs invalid;
        invalid.pitch = 200;
        require(profile.addMacro({"macro.rollback-observer", "Rollback observer",
                    {{"channel.gain", gain(c, .41), true}, {"notes.add", invalid, true}}}).wasOk(),
                "Supported macro defers musical validation to canonical command");
        bool rollbackObserved = false, observerEdited = false;
        c.changed = [&]
        {
            if (c.hasGesture() || rollbackObserved) return;
            rollbackObserved = true;
            observerEdited = c.execute("channel.pan", gain(c, .2)).wasOk();
        };
        require(profile.runMacro("macro.rollback-observer", c, context).failed() &&
                    rollbackObserved && observerEdited && c.dirty && !c.hasGesture() &&
                    expected.state().isEquivalentTo(c.project.state()),
                "Macro rollback cannot overwrite dirtiness or model of a subsequent canonical observer edit");
        require(c.undoEditChecked().wasOk() && !c.undo.canUndo() &&
                    original.isEquivalentTo(c.project.state()),
                "Only the observer edit has one exact real Undo after failed macro rollback");
    }
    // C++ new fault sweep; CRT allocation and hardware are deliberately not claimed.
    // Rebuild each fixture unarmed, inject only the named operation, then disarm
    // BEFORE readback/assertions. Require the first genuinely nonfaulted success.
    for (int scenario = 0; scenario != 5; ++scenario)
    {
        bool reachedSuccess = false;
        int failures = 0;
        for (int budget = 0; budget < 4096 && !reachedSuccess; ++budget)
        {
            Controller c;
            if (scenario != 0)
                require(c.execute("channel.gain", gain(c, .34)).wasOk(), "Fault fixture canonical baseline");
            if (scenario == 2)
                require(c.beginGesture("fault-owner") &&
                            c.execute("channel.gain", gain(c, .64), "fault-owner").wasOk(),
                        "Fault fixture owned preview");
            if (scenario == 3)
                require(c.undoEditChecked().wasOk(), "Redo fault fixture actual Undo");
            std::unique_ptr<PreparedControllerChange> validation;
            if (scenario == 4)
                require(c.prepareHistoryChange(HistoryDirection::undo, validation).wasOk(),
                        "Validation fault fixture prepares actual history ticket");
            const auto before = snapshot(c);
            const auto args = gain(c, .44);
            const juce::String commandName("channel.gain"), gestureLabel("Fault-bounded gesture"),
                               gestureOwner("fault-owner");
            bool returnedSuccess = false, escaped = false, typedFailure = false;
            faultScenario = scenario;
            faultBudget = budget;
            preparationAllocationFailureHit = false;
            preparationAllocationFailureAfter = budget;
            try
            {
                auto result = scenario == 0   ? c.execute(commandName, args)
                              : scenario == 1 ? c.undoEditChecked()
                              : scenario == 2 ? c.endGestureChecked(gestureLabel, gestureOwner)
                              : scenario == 3 ? c.redoEditChecked()
                                              : c.validatePreparedChange(*validation);
                returnedSuccess = result.wasOk();
                typedFailure =
                    result.failed() && result.getErrorMessage().startsWith("history.preparationFailed:");
            }
            catch (const std::bad_alloc &)
            {
                escaped = true;
            }
            catch (...)
            {
                preparationAllocationFailureAfter = -1;
                throw;
            }
            preparationAllocationFailureAfter = -1;
            if (preparationAllocationFailureHit)
            {
                ++failures;
                require(!returnedSuccess && before == snapshot(c),
                        "Every injected precommit allocation fault preserves exact "
                        "model/history/ownership/dirty/revision");
                if (scenario != 0)
                    require(!escaped && typedFailure, "Checked end/Undo/Redo/validation returns structured "
                                                      "preparation failure instead of throwing");
                if (scenario == 4)
                    require(!measuredCommit(c, *validation),
                            "Failed deep validation invalidates publication admission");
            }
            else
            {
                require(returnedSuccess, "First nonfaulted canonical operation must actually succeed");
                reachedSuccess = true;
            }
        }
        require(reachedSuccess && failures > 0,
                "Bounded fault sweep reaches genuine success after negative admissions");
    }
    require(folder.getChildFile("controller-prepared-history-verification.txt")
                .replaceWithText(
                    "PASS actual-manager canonical snapshots/Undo/Redo, exact source/stale/foreign guards, "
                    "gesture grouping/cancel/dirtiness, no C++ new/delete commit, cached costs, observer "
                    "pending/retry/reentry "
                    "and bounded C++ allocation faults. No CRT OOM, graph adoption, real plugins, audio, "
                    "desktop, hardware "
                    "or listening qualification. Existing HistoryDestinationAcceptance remains separately "
                    "required.\n"),
            "Prepared Controller evidence");
}
} // namespace controller_prepared_acceptance
