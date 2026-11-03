// SPDX-License-Identifier: AGPL-3.0-or-later
// Original source-only acceptance. Include after existing require/allocation probe.
#pragma once
#include "../src/Commands.h"
#include <cstring>
#include <tuple>
namespace controller_routing_capture_acceptance
{
using namespace vibera;
inline juce::String opaque(const char* value)
{
    return juce::MemoryBlock(value, std::strlen(value)).toBase64Encoding();
}
inline PluginSpec originalSpec(Project& p, const char* name, bool instrument)
{
    juce::PluginDescription description;
    description.name = name; description.pluginFormatName = "VST3";
    description.fileOrIdentifier = "original-unit-fixture-not-an-installed-provider.vst3";
    description.isInstrument = instrument;
    PluginSpec spec; spec.id = p.allocate(); spec.name = name;
    spec.descriptor = description.createXml()->toString(); spec.instrument = instrument;
    spec.state = opaque("original-state-A"); return spec;
}
inline void tests(const juce::File& folder)
{
    {
        Controller c;
        auto spec = originalSpec(c.project, "Original no-file opaque refresh", true);
        c.project.channels.front().instrument = spec;
        c.project.channels.front().mixer = 1;
        CommandArgs route; route.channel = c.project.channels.front().id; route.mixer = 2;
        require(c.execute("channel.mixer", route).wasOk(), "Original route before no-file native refresh");
        const auto revision = c.revision;
        const auto units = c.undo.getNumberOfUnitsTakenUpByStoredCommands();
        auto next = c.project; next.plugin(spec.id)->state = opaque("actual-captured-metadata-B");
        std::unique_ptr<PreparedControllerChange> ticket;
        require(c.prepareNativeStateRefresh(next, ticket).wasOk() && ticket &&
                    c.project.plugin(spec.id)->state == spec.state &&
                    c.validatePreparedChange(*ticket).wasOk(), "Opaque refresh prepares without applying or saving");
        const auto allocations = callbackAllocations.load(), destructions = callbackDestructions.load();
        callbackAllocationProbe = true;
        const auto committed = c.commitPreparedChange(*ticket);
        callbackAllocationProbe = false;
        require(committed && callbackAllocations.load() == allocations &&
                    callbackDestructions.load() == destructions && c.project.state().isEquivalentTo(next.state()) &&
                    c.revision == revision && c.dirty && c.undo.getNumberOfUnitsTakenUpByStoredCommands() == units,
                "No-file opaque commit is allocation/destruction free, dirty, and outside musical revision/history");
        require(c.undoEditChecked().wasOk() && c.project.channels.front().mixer == 1 &&
                    c.project.plugin(spec.id)->state == next.plugin(spec.id)->state &&
                    c.redoEditChecked().wasOk() && c.project.channels.front().mixer == 2 &&
                    c.project.plugin(spec.id)->state == next.plugin(spec.id)->state,
                "EARLIER actual route Undo/Redo preserves no-file captured opaque refresh");
        for (int kind : {0, 1, 2})
        {
            auto invalid = c.project;
            if (kind == 0) invalid.tempo += 1;
            if (kind == 1) invalid.channels.front().gain = .125f;
            if (kind == 2) invalid.plugin(spec.id)->descriptor += "different";
            const auto before = c.project.state().toXmlString();
            std::unique_ptr<PreparedControllerChange> refused;
            require(c.prepareNativeStateRefresh(std::move(invalid), refused).failed() && !refused &&
                        c.project.state().toXmlString() == before,
                    "Opaque refresh rejects musical/descriptor changes without mutation");
        }
        auto captured = c.project; captured.plugin(spec.id)->state = opaque("later-native-capture-C");
        std::unique_ptr<PreparedControllerChange> stale;
        require(c.prepareNativeStateRefresh(std::move(captured), stale).wasOk(), "Prepare original stale refresh");
        c.project.plugin(spec.id)->state = opaque("newer-state-without-revision");
        require(c.validatePreparedChange(*stale).failed(), "Exact opaque source freshness is stronger than revision equality");
    }
    {
        Controller c;
        auto instrument = originalSpec(c.project, "Original source-only instrument", true);
        auto effect = originalSpec(c.project, "Original source-only effect", false);
        const auto channel = c.project.channels.front().id;
        c.project.channels.front().instrument = instrument;
        c.project.channels.front().mixer = 1;
        c.project.mixer[1].effects.push_back(effect);
        CommandArgs route; route.channel = channel; route.mixer = 2;
        require(c.execute("channel.mixer", route).wasOk(), "Original actual route history action");
        const auto units = c.undo.getNumberOfUnitsTakenUpByStoredCommands();
        juce::Array<const juce::UndoableAction*> actions;
        c.undo.getActionsInUndoTransaction(actions);
        require(actions.size() == 1, "One actual immutable route StateAction");
        const auto* action = actions[0];
        // Models the EXISTING saveTo native-capture mirror contract. This is a
        // source-model oracle, not a real plugin/save qualification; GUI branch
        // separately exercises the real original worker and actual shared Save.
        c.project.plugin(instrument.id)->state = opaque("current-capture-instrument-B");
        c.project.plugin(effect.id)->state = opaque("current-capture-effect-B");
        auto expected = c.project; expected.channel(channel)->mixer = 1;
        HistoryDestination preview;
        require(c.previewHistoryDestination(HistoryDirection::undo, preview).wasOk() &&
                    preview.destination().state().isEquivalentTo(expected.state()) &&
                    c.validateHistoryDestination(preview).wasOk(),
                "Preview preserves exact current native capture across earlier pure route action");
        juce::Array<const juce::UndoableAction*> unchanged;
        c.undo.getActionsInUndoTransaction(unchanged);
        require(unchanged.size() == 1 && unchanged[0] == action &&
                    c.undo.getNumberOfUnitsTakenUpByStoredCommands() == units,
                "Preview does not rewrite immutable actions/history metadata");
        std::unique_ptr<PreparedControllerChange> prepared;
        require(c.prepareHistoryChange(HistoryDirection::undo, prepared).wasOk() &&
                    prepared->destinationBeforeCommit()->state().isEquivalentTo(expected.state()) &&
                    c.validatePreparedChange(*prepared).wasOk(),
                "Real history preparation uses the same canonical destination as preview");
        const auto allocations = callbackAllocations.load(), destructions = callbackDestructions.load();
        callbackAllocationProbe = true;
        const auto committed = c.commitPreparedChange(*prepared);
        callbackAllocationProbe = false;
        require(committed && callbackAllocations.load() == allocations &&
                    callbackDestructions.load() == destructions &&
                    c.project.state().isEquivalentTo(expected.state()),
                "Rebased actual history commit still has no C++ new/delete");
        c.deliverPendingNotifications();
        require(c.redoEditChecked().wasOk() && c.project.channel(channel)->mixer == 2 &&
                    c.project.plugin(instrument.id)->state == opaque("current-capture-instrument-B") &&
                    c.project.plugin(effect.id)->state == opaque("current-capture-effect-B"),
                "Actual Redo moves same cursor and retains both current opaque captures");
        // Native raw UndoManager semantics intentionally remain unchanged. This
        // directly proves snapshots were NOT edited behind its real ownership.
        require(c.undo.undo() && c.project.channel(channel)->mixer == 1 &&
                    c.project.plugin(instrument.id)->state == instrument.state &&
                    c.project.plugin(effect.id)->state == effect.state,
                "Immutable source snapshots and raw generic JUCE action semantics remain original");
    }
    for (const auto kind : {0, 1, 2})
    {
        Controller c;
        auto spec = originalSpec(c.project, "Original negative history fixture", true);
        const auto channel = c.project.channels.front().id;
        c.project.channels.front().instrument = spec;
        c.project.channels.front().mixer = 1;
        const auto before = c.project;
        auto next = before;
        next.channel(channel)->mixer = 2;
        if (kind == 0) next.channel(channel)->gain = .234375f; // Mixed musical delta.
        if (kind == 1) next.plugin(spec.id)->state = opaque("intentional-opaque-action-B");
        if (kind == 2)
        {
            juce::PluginDescription changed;
            changed.name = "Different descriptor"; changed.pluginFormatName = "VST3";
            changed.fileOrIdentifier = "different-original-unit-fixture.vst3"; changed.isInstrument = true;
            next.plugin(spec.id)->descriptor = changed.createXml()->toString();
        }
        std::unique_ptr<PreparedControllerChange> transaction;
        require(c.prepareSnapshotChange(next, before, "Original mixed/state/descriptor transaction", true,
                    false, true, transaction).wasOk() &&
                    c.validatePreparedChange(*transaction).wasOk() && c.commitPreparedChange(*transaction),
                "Actual original non-pure-route transaction");
        c.deliverPendingNotifications();
        c.project.plugin(spec.id)->state = opaque("later-native-capture-C");
        HistoryDestination destination;
        require(c.previewHistoryDestination(HistoryDirection::undo, destination).wasOk() &&
                    destination.destination().state().isEquivalentTo(before.state()) &&
                    c.undoEditChecked().wasOk() && c.project.state().isEquivalentTo(before.state()),
                "Mixed musical, opaque-state and descriptor-changing actions get NO rebasing exception");
    }
    {
        Controller c;
        auto spec = originalSpec(c.project, "Original source freshness fixture", true);
        c.project.channels.front().instrument = spec;
        CommandArgs route; route.channel = c.project.channels.front().id; route.mixer = 2;
        require(c.execute("channel.mixer", route).wasOk(), "Actual freshness route fixture");
        HistoryDestination preview;
        require(c.previewHistoryDestination(HistoryDirection::undo, preview).wasOk(), "Actual preview source");
        // Save can update opaque state without incrementing revision. Full source
        // XML comparison MUST still make this prepared preview stale.
        c.project.plugin(spec.id)->state = opaque("changed-after-preview");
        require(c.validateHistoryDestination(preview).failed(),
                "No-revision native capture invalidates stale history preview before mutation");
    }
    // Existing test-only C++ allocation probe, unarmed during fixture setup and
    // every assertion/readback. No CRT/hardware/provider claim is made.
    bool reachedSuccess = false;
    int failedSites = 0;
    for (int budget = 0; budget < 4096 && !reachedSuccess; ++budget)
    {
        Controller c;
        auto spec = originalSpec(c.project, "Original routing capture fault fixture", true);
        c.project.channels.front().instrument = spec;
        CommandArgs route; route.channel = c.project.channels.front().id; route.mixer = 2;
        require(c.execute("channel.mixer", route).wasOk(), "Fault fixture actual route action");
        c.project.plugin(spec.id)->state = opaque("latest-capture-during-fault-fixture");
        auto source = [&]
        {
            juce::Array<const juce::UndoableAction*> undo, redo;
            c.undo.getActionsInUndoTransaction(undo); c.undo.getActionsInRedoTransaction(redo);
            return std::make_tuple(c.project.state().toXmlString(), c.revision, c.dirty,
                c.undo.getUndoDescriptions().joinIntoString("|"), c.undo.getRedoDescriptions().joinIntoString("|"),
                c.undo.getNumberOfUnitsTakenUpByStoredCommands(), undo.size(), redo.size(),
                reinterpret_cast<std::uintptr_t>(undo.isEmpty() ? nullptr : undo[0]),
                reinterpret_cast<std::uintptr_t>(redo.isEmpty() ? nullptr : redo[0]));
        };
        const auto before = source();
        std::unique_ptr<PreparedControllerChange> ticket;
        bool ok = false, escaped = false;
        preparationAllocationFailureHit = false;
        preparationAllocationFailureAfter = budget;
        try { ok = c.prepareHistoryChange(HistoryDirection::undo, ticket).wasOk(); }
        catch (...) { escaped = true; }
        preparationAllocationFailureAfter = -1;
        if (preparationAllocationFailureHit)
        {
            ++failedSites;
            require(!escaped && !ok && before == source() && !ticket,
                    "Every actual rebasing allocation failure preserves source, real cursor and immutable action");
        }
        else
        {
            require(!escaped && ok && ticket && before == source() &&
                        ticket->destinationBeforeCommit()->plugin(spec.id)->state ==
                            opaque("latest-capture-during-fault-fixture"),
                    "First genuinely nonfaulted shared preparation reaches the exact native-capture destination");
            reachedSuccess = true;
        }
    }
    require(reachedSuccess && failedSites > 0,
            "Bounded original fault sweep covers real rebasing preparation and reaches nonfaulted success");
    require(folder.getChildFile("controller-routing-capture.txt").replaceWithText(
        "Original actual Controller/JUCE history: earlier pure routing actions preserve current native "
        "opaque captures; strict mixed/state/descriptor negatives, immutable raw actions, no-allocation "
        "commit and stale source guard. Model fixtures only; real worker GUI branch is separate.\n"),
        "Preserve Controller routing capture evidence");
}
} // namespace controller_routing_capture_acceptance
