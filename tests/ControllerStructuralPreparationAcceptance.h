// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <limits>
// SOURCE-ONLY original proposal. Include after CallbackAllocationProbe and
// ControllerPreparedHistoryAcceptance helpers (snapshot/measuredCommit/Foreign).
namespace controller_structural_preparation_acceptance
{
using namespace vibera;
using controller_prepared_acceptance::Foreign;
using controller_prepared_acceptance::measuredCommit;
using controller_prepared_acceptance::snapshot;
inline CommandArgs args(const Controller &c, const juce::String &id)
{
    CommandArgs a;
    a.channel = c.project.channels.front().id;
    a.mixer = id == "channel.mixer" ? 2 : 1;
    a.target = id == "mixer.send" ? 0 : 2;
    a.value = .375;
    return a;
}
inline void tests(const juce::File &folder)
{
    require(folder.createDirectory().wasOk(), "owned canonical routing preparation output");
    unsigned cases = 0;
    for (const juce::String id : {"channel.mixer", "mixer.route", "mixer.routeOnly", "mixer.send"})
    {
        Controller c;
        CommandArgs tempo;
        tempo.value = 121;
        require(c.execute("project.tempo", tempo).wasOk() && c.undoEditChecked().wasOk() && c.undo.canRedo(),
                "real redo cursor exists before routing preparation");
        int notifications = 0;
        c.changed = [&] { ++notifications; };
        const auto before = snapshot(c);
        const auto a = args(c, id);
        auto expected = c.project;
        if (id == "channel.mixer")
            expected.channels.front().mixer = 2;
        if (id == "mixer.route")
            expected.mixer[1].routes = {{0, 1.f}, {2, 1.f}};
        if (id == "mixer.routeOnly")
            expected.mixer[1].routes = {{2, 1.f}};
        if (id == "mixer.send")
            expected.mixer[1].routes = {{0, .375f}};
        require(expected.validate().wasOk(), "independent original routing destination is genuine");
        std::unique_ptr<PreparedControllerChange> prepared;
        require(c.prepareStructuralCommand(id, a, prepared).wasOk() && prepared && !prepared->isNoOp() &&
                    !prepared->wasCommitted() && prepared->destinationBeforeCommit() &&
                    expected.state().isEquivalentTo(prepared->destinationBeforeCommit()->state()) &&
                    snapshot(c) == before && notifications == 0,
                "actual canonical mutation is prepared with exact target while real "
                "model/history/notifications remain untouched");
        require(!measuredCommit(c, *prepared) && snapshot(c) == before && notifications == 0,
                "returned preparation is not validated publication authority");
        Controller normal;
        require(normal.execute(id, a).wasOk() &&
                    normal.project.state().isEquivalentTo(prepared->destinationBeforeCommit()->state()),
                "normal execute and new preparation use one canonical destination, with independent routing "
                "oracle");
        require(c.validatePreparedChange(*prepared).wasOk() && measuredCommit(c, *prepared) &&
                    prepared->wasCommitted() && !prepared->destinationBeforeCommit() &&
                    c.revision == before.revision + 1 && c.dirty && notifications == 0 &&
                    c.hasPendingControllerNotification() && c.hasPendingUndoNotification() &&
                    expected.state().isEquivalentTo(c.project.state()) && c.undo.canUndo() &&
                    !c.undo.canRedo(),
                "one actual canonical/history commit, no observer or allocation/destruction in publication");
        require(c.deliverPendingNotifications() && notifications == 1 && !measuredCommit(c, *prepared),
                "post-commit delivery is separate and token cannot commit twice");
        require(c.undoEditChecked().wasOk() && c.project.state().toXmlString() == before.model &&
                    !c.undo.canUndo(),
                "one real Undo restores exact source and consumes only the new routing transaction");
        require(c.redoEditChecked().wasOk() && expected.state().isEquivalentTo(c.project.state()),
                "one real Redo restores independent destination");

        const auto noOpBefore = snapshot(c);
        const auto channels = c.project.channels.data();
        const auto inserts = c.project.mixer.data();
        const auto noticeBefore = notifications;
        require(c.prepareStructuralCommand(id, a, prepared).wasOk() && prepared && prepared->isNoOp() &&
                    snapshot(c) == noOpBefore && notifications == noticeBefore &&
                    c.validatePreparedChange(*prepared).wasOk() && measuredCommit(c, *prepared) &&
                    snapshot(c) == noOpBefore && notifications == noticeBefore &&
                    c.project.channels.data() == channels && c.project.mixer.data() == inserts,
                "explicit real no-op ticket changes no model owners, dirtiness, revision, history or "
                "notifications");
        ++cases;
    }
    {
        Controller c;
        int notifications = 0;
        c.changed = [&] { ++notifications; };
        std::unique_ptr<PreparedControllerChange> ticket;
        const auto a = args(c, "mixer.route");
        require(c.prepareStructuralCommand("mixer.route", a, ticket).wasOk() &&
                    c.validatePreparedChange(*ticket).wasOk() && measuredCommit(c, *ticket) &&
                    c.hasPendingControllerNotification() && c.hasPendingUndoNotification() &&
                    notifications == 0,
                "genuine previous committed notification pending before preparation");
        const auto before = snapshot(c);
        require(
            c.prepareStructuralCommand("mixer.route", a, ticket).wasOk() && ticket->isNoOp() &&
                snapshot(c) == before && notifications == 0 && c.validatePreparedChange(*ticket).wasOk() &&
                measuredCommit(c, *ticket) && snapshot(c) == before && notifications == 0,
            "no-op preparation/commit cannot deliver or clear a prior transaction's pending notifications");
        require(c.deliverPendingNotifications() && notifications == 1, "only prior real edit delivers once");
        ++cases;
    }
    {
        Controller c;
        int notifications = 0;
        c.changed = [&] { ++notifications; };
        auto a = args(c, "mixer.route");
        std::unique_ptr<PreparedControllerChange> ticket;
        require(c.prepareStructuralCommand("mixer.route", a, ticket).wasOk(),
                "unchanged-output refusal fixture");
        const auto *kept = ticket.get();
        const auto before = snapshot(c);
        require(c.prepareStructuralCommand("project.tempo", a, ticket).failed() && ticket.get() == kept,
                "restricted API cannot prepare unrelated commands");
        a.channel = c.project.channels.front().id;
        a.mixer = 9999;
        require(c.prepareStructuralCommand("channel.mixer", a, ticket).failed() && ticket.get() == kept,
                "actual Project validation refuses missing destination atomically");
        a.mixer = 1;
        a.target = 1;
        require(c.prepareStructuralCommand("mixer.route", a, ticket).failed(),
                "actual routing helper refuses feedback/self route");
        a.target = Id(std::numeric_limits<int>::max()) + 1;
        require(c.prepareStructuralCommand("mixer.routeOnly", a, ticket).failed(),
                "existing integer destination bounds preserved");
        a.target = 0;
        a.value = std::numeric_limits<double>::quiet_NaN();
        require(c.prepareStructuralCommand("mixer.send", a, ticket).failed() && ticket.get() == kept &&
                    snapshot(c) == before && notifications == 0,
                "actual finite send-gain validation and all refusals leave source/output/history unchanged");
        ++cases;
    }
    {
        Controller c;
        c.project.mixer[2].routes = {{1, 1.f}};
        require(c.project.validate().wasOk(), "genuine acyclic existing route2 to1");
        const auto before = snapshot(c);
        std::unique_ptr<PreparedControllerChange> ticket;
        require(c.prepareStructuralCommand("mixer.route", args(c, "mixer.route"), ticket).failed() &&
                    !ticket && snapshot(c) == before,
                "existing graph-cycle validation reused without canonical mutation");
        ++cases;
    }
    for (const int mode : {0, 1, 2})
    {
        Controller c;
        const auto baseline = c.project.state().toXmlString();
        const juce::String actualOwner(mode == 2 ? "" : "gesture-owner");
        const juce::String requestedOwner(mode == 1 ? juce::String("foreign-owner") : actualOwner);
        std::unique_ptr<PreparedControllerChange> ticket;
        require(c.prepareStructuralCommand("mixer.route", args(c, "mixer.route"), ticket).wasOk(),
                "gesture old output fixture");
        const auto kept = ticket.get();
        require(c.beginGesture(actualOwner), "real owned/local gesture starts");
        CommandArgs gain;
        gain.channel = c.project.channels.front().id;
        gain.value = .43;
        require(c.execute("channel.gain", gain, actualOwner).wasOk(),
                "real musical preview in active gesture");
        const auto before = snapshot(c);
        int notifications = 0;
        c.changed = [&] { ++notifications; };
        const auto refused =
            c.prepareStructuralCommand("mixer.route", args(c, "mixer.route"), ticket, requestedOwner);
        require(refused.failed() &&
                    refused.getErrorMessage().startsWith(mode == 1 ? "command.foreignGesture:"
                                                                   : "command.activeGesture:") &&
                    snapshot(c) == before && ticket.get() == kept && notifications == 0,
                "owned/foreign/local gesture preparation explicitly refuses, retaining actual "
                "preview/owner/serial/history");
        require(c.cancelGestureChecked(actualOwner).wasOk() && c.project.state().toXmlString() == baseline,
                "original owner can still Escape through canonical checked cancellation");
        ++cases;
    }
    {
        Controller c;
        int calls = 0, notifications = 0;
        c.changed = [&] { ++notifications; };
        require(c.undo.perform(new Foreign(calls)) && calls == 1, "genuine foreign actual-manager action");
        const auto before = snapshot(c);
        std::unique_ptr<PreparedControllerChange> ticket;
        require(c.prepareStructuralCommand("mixer.route", args(c, "mixer.route"), ticket).failed() &&
                    !ticket && snapshot(c) == before && calls == 1 && notifications == 0,
                "real history preparation refuses unsupported action without running it or changing "
                "cursor/source");
        ++cases;
    }
    for (const int mode : {0, 1, 2, 3})
    {
        Controller c;
        std::unique_ptr<PreparedControllerChange> ticket;
        require(c.prepareStructuralCommand("mixer.route", args(c, "mixer.route"), ticket).wasOk(),
                "stale actual routing preparation");
        if (mode == 0)
        {
            CommandArgs tempo;
            tempo.value = 123;
            require(c.execute("project.tempo", tempo).wasOk(), "intervening canonical edit");
        }
        if (mode == 1)
            c.project.name = "Unnotified direct source change";
        if (mode == 2)
        {
            require(c.validatePreparedChange(*ticket).wasOk(),
                    "pre-history-metadata change exact validation");
            c.undo.setCurrentTransactionName("Changed actual manager metadata");
        }
        if (mode == 3)
            c.projectIdentity = juce::Uuid().toString();
        const auto before = snapshot(c);
        if (mode != 2)
            require(c.validatePreparedChange(*ticket).failed(),
                    "actual source/session/revision validation refuses stale ticket");
        require(!measuredCommit(c, *ticket) && snapshot(c) == before,
                "stale real ticket cannot publish over intervening model/session/history state");
        ++cases;
    }
    {
        Controller c;
        const juce::String id("mixer.route");
        const auto a = args(c, id);
        std::unique_ptr<PreparedControllerChange> ticket;
        require(c.prepareStructuralCommand(id, a, ticket).wasOk(), "prewarmed actual preparation output");
        const auto kept = ticket.get();
        const auto before = snapshot(c);
        preparationAllocationFailureHit = false;
        preparationAllocationFailureAfter =
            0; // Genuine next C++ allocation inside API, no fixture strings created armed.
        auto result = juce::Result::ok();
        bool escaped = false;
        try
        {
            result = c.prepareStructuralCommand(id, a, ticket);
        }
        catch (...)
        {
            escaped = true;
        }
        preparationAllocationFailureAfter = -1;
        require(
            preparationAllocationFailureHit && !escaped && result.failed() && ticket.get() == kept &&
                snapshot(c) == before,
            "genuine preparation allocation refusal preserves existing output, source and actual history");
        ++cases;
    }
    require(folder.getChildFile("structural-command-preparation.txt")
                .replaceWithText("PASS " + juce::String(cases) +
                                 " actual Controller/JUCE history scenarios: canonical routing destination, "
                                 "nonmutating preparation, explicit no-op/gesture refusal, "
                                 "invalid/cycle/history/allocation failures, stale guards, "
                                 "single allocation-free commit and exact Undo/Redo. No audio graph "
                                 "publication, native desktop, worker or listening qualification.\n"),
            "retain actual structural preparation receipt");
}
} // namespace controller_structural_preparation_acceptance
