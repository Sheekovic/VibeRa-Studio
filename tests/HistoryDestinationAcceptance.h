// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../src/Commands.h"
#include "../src/Customization.h"
#include <tuple>
#include <cstdint>

namespace history_destination_acceptance
{
class ScalarAction final : public juce::UndoableAction
{
    int &value, &calls;
    int before, after;
    bool refuseUndo;
  public:
    ScalarAction(int &v, int &c, int b, int a, bool refuse = false)
        : value(v), calls(c), before(b), after(a), refuseUndo(refuse) {}
    bool perform() override { ++calls; value = after; return true; }
    bool undo() override { ++calls; if (refuseUndo) return false; value = before; return true; }
};
struct Readback
{
    juce::String state, identity, undoNames, redoNames, owner;
    std::uint64_t revision;
    bool dirty, gesture;
    int units;
    std::vector<std::uintptr_t> undoActions, redoActions;
    bool operator==(const Readback &) const = default;
};
inline Readback readback(const vibera::Controller &controller)
{
    Readback result{controller.project.state().toXmlString(), controller.projectIdentity,
                    controller.undo.getUndoDescriptions().joinIntoString("|"),
                    controller.undo.getRedoDescriptions().joinIntoString("|"),
                    controller.gestureOwner(), controller.revision, controller.dirty,
                    controller.hasGesture(), controller.undo.getNumberOfUnitsTakenUpByStoredCommands(),
                    {}, {}};
    juce::Array<const juce::UndoableAction *> actions;
    controller.undo.getActionsInUndoTransaction(actions);
    for (const auto *action : actions)
        result.undoActions.push_back(reinterpret_cast<std::uintptr_t>(action));
    actions.clear();
    controller.undo.getActionsInRedoTransaction(actions);
    for (const auto *action : actions)
        result.redoActions.push_back(reinterpret_cast<std::uintptr_t>(action));
    return result;
}
}
static void historyDestinationTests(const juce::File &folder)
{
    using namespace vibera;
    using namespace history_destination_acceptance;
    // Independent JUCE cursor/order oracle; no VibeRa model mutations involved.
    {
        juce::UndoManager manager;
        int value = 0, calls = 0;
        auto *first = new ScalarAction(value, calls, 0, 1);
        auto *last = new ScalarAction(value, calls, 1, 2);
        manager.beginNewTransaction("Original paired transaction");
        require(manager.perform(first) && manager.perform(last), "History fixture actions perform");
        manager.beginNewTransaction("Empty boundary");
        require(manager.getNumActionsInCurrentTransaction() == 0, "Original open-transaction semantics");
        juce::Array<const juce::UndoableAction *> actions;
        manager.getActionsInUndoTransaction(actions);
        require(actions.size() == 2 && actions[0] == first && actions[1] == last && value == 2 && calls == 2,
                "Read-only Undo transaction includes earliest-to-latest actions across empty boundary");
        manager.getActionsInRedoTransaction(actions);
        require(actions.size() == 2 && calls == 2, "Empty Redo accessor appends nothing");
        require(manager.undo() && value == 0 && calls == 4, "Actual Undo reverses complete pair");
        actions.clear();
        manager.getActionsInRedoTransaction(actions);
        require(actions.size() == 2 && actions[0] == first && actions[1] == last && value == 0 && calls == 4,
                "Read-only Redo includes actual forward pair without performing it");
        require(manager.redo() && value == 2 && calls == 6, "Actual Redo retains original transaction");
        manager.clearUndoHistory();
        actions.clear(); manager.getActionsInUndoTransaction(actions); manager.getActionsInRedoTransaction(actions);
        require(actions.isEmpty(), "Cleared history returns no dangling actions");
    }
    {
        // Retain the motivating JUCE failure contract as an independent assertion.
        juce::UndoManager manager;
        int value = 0, calls = 0;
        require(manager.perform(new ScalarAction(value, calls, 0, 1, true)), "Refusing Undo fixture prepared");
        juce::Array<const juce::UndoableAction *> actions;
        manager.getActionsInUndoTransaction(actions);
        require(actions.size() == 1 && calls == 1 && manager.canUndo(), "Preview does not invoke failing Undo");
        manager.undo();
        require(calls == 2 && !manager.canUndo() && !manager.canRedo(),
                "Pinned JUCE clears history when an action refuses Undo; no rollback strategy assumes otherwise");
    }
    Controller c;
    int notifications = 0;
    c.changed = [&] { ++notifications; };
    HistoryDestination candidate;
    const auto empty = readback(c);
    require(c.previewHistoryDestination(HistoryDirection::undo, candidate).failed() &&
                c.previewHistoryDestination(HistoryDirection::redo, candidate).failed() && empty == readback(c),
            "Empty directions refuse atomically without cursor/model/history changes");
    require(c.previewHistoryDestination(static_cast<HistoryDirection>(123), candidate).failed() &&
                c.validateHistoryDestination(candidate).failed() && empty == readback(c),
            "Invalid direction/default candidate refuse without mutation");
    CommandArgs a;
    a.channel = c.project.channels.front().id;
    a.value = .37;
    const auto original = c.project;
    require(c.execute("channel.gain", a).wasOk(), "History gain operation");
    const auto gained = c.project;
    const auto afterGain = readback(c);
    const int afterGainNotifications = notifications;
    require(c.previewHistoryDestination(HistoryDirection::undo, candidate).wasOk() &&
                candidate.destination().state().isEquivalentTo(original.state()) &&
                candidate.transactionName() == c.undo.getUndoDescription() &&
                c.validateHistoryDestination(candidate).wasOk() && afterGain == readback(c) &&
                notifications == afterGainNotifications,
            "Undo destination is exact and preview/validation do not notify or consume history");
    const auto candidateBeforeRefusal = candidate.destination().state();
    require(c.previewHistoryDestination(HistoryDirection::redo, candidate).failed() &&
                candidateBeforeRefusal.isEquivalentTo(candidate.destination().state()),
            "Refused preview preserves previous candidate output");
    c.undo.beginNewTransaction("No action yet");
    require(c.previewHistoryDestination(HistoryDirection::undo, candidate).wasOk(),
            "BeginNewTransaction does not hide the real Undo destination");
    c.undoEdit();
    require(original.state().isEquivalentTo(c.project.state()) &&
                c.validateHistoryDestination(candidate).failed(), "Actual Undo matches preview and invalidates token");
    require(c.previewHistoryDestination(HistoryDirection::redo, candidate).wasOk() &&
                candidate.destination().state().isEquivalentTo(gained.state()), "Redo destination is exact");
    c.redoEdit();
    require(gained.state().isEquivalentTo(c.project.state()), "Actual Redo matches preview");
    a.value = .39;
    require(c.execute("channel.gain", a).wasOk(), "Open transaction for metadata stale check");
    require(c.previewHistoryDestination(HistoryDirection::undo, candidate).wasOk(), "Prepare stale checks");
    c.undo.setCurrentTransactionName("Renamed during prepare");
    require(c.validateHistoryDestination(candidate).failed(), "History metadata change invalidates prepared destination");
    require(c.previewHistoryDestination(HistoryDirection::undo, candidate).wasOk(), "Refresh after rename");
    const auto oldName = c.project.name;
    c.project.name = "Direct model edit without notify";
    require(c.validateHistoryDestination(candidate).failed(), "Direct model changes cannot evade revision guard");
    c.project.name = oldName;
    Controller other;
    require(other.validateHistoryDestination(candidate).failed(), "Candidates cannot cross controllers");
    require(c.beginGesture("remote-history-test"), "Active gesture fixture");
    const auto active = readback(c);
    require(c.previewHistoryDestination(HistoryDirection::undo, candidate).failed() && active == readback(c),
            "Preview refuses active gesture without cancel/finish or owner changes");
    c.cancelGesture("remote-history-test");
    // Exact source identities and allocator state are snapshots, never remapped.
    a.pattern = c.project.patterns.front().id; a.beat = .5; a.length = .25; a.pitch = 64;
    const auto beforeNote = c.project;
    require(c.execute("notes.add", a).wasOk(), "Allocating score edit");
    const auto withNote = c.project;
    require(c.previewHistoryDestination(HistoryDirection::undo, candidate).wasOk() &&
                candidate.destination().state().isEquivalentTo(beforeNote.state()), "Preview preserves exact pre-note ID allocator");
    c.undoEdit();
    require(c.previewHistoryDestination(HistoryDirection::redo, candidate).wasOk() &&
                candidate.destination().state().isEquivalentTo(withNote.state()), "Redo candidate preserves allocated note/source identities");
    c.redoEdit();
    // Existing document macros produce a single snapshot transaction.
    Customization customization;
    Macro macro{"macro.history-destination", "Original paired mix gesture", {}};
    MacroStep gain; gain.action = "channel.gain"; gain.parameters.value = .52;
    MacroStep pan; pan.action = "channel.pan"; pan.parameters.value = -.25;
    macro.steps = {gain, pan};
    require(customization.addMacro(macro).wasOk(), "History macro fixture accepted");
    MacroContext context; context.channel = a.channel; context.pattern = a.pattern;
    const auto beforeMacro = c.project;
    require(customization.runMacro(macro.id, c, context).wasOk(), "Actual shared macro performs");
    const auto afterMacro = c.project;
    require(c.previewHistoryDestination(HistoryDirection::undo, candidate).wasOk() &&
                candidate.destination().state().isEquivalentTo(beforeMacro.state()) &&
                candidate.transactionName() == macro.name, "Macro remains one exact previewable transaction");
    c.undoEdit();
    require(beforeMacro.state().isEquivalentTo(c.project.state()), "One actual Undo reverses whole macro");
    require(c.previewHistoryDestination(HistoryDirection::redo, candidate).wasOk() &&
                candidate.destination().state().isEquivalentTo(afterMacro.state()), "Macro Redo preview exact");
    c.redoEdit();
    // Unsupported additions must refuse the complete transaction, not silently skip them.
    a.value = .61;
    require(c.execute("channel.gain", a).wasOk(), "Open mixed snapshot transaction");
    int foreignValue = 0, foreignCalls = 0;
    require(c.undo.perform(new ScalarAction(foreignValue, foreignCalls, 0, 1)), "Foreign mixed-action fixture");
    const auto mixed = readback(c);
    require(c.previewHistoryDestination(HistoryDirection::undo, candidate).failed() && mixed == readback(c) &&
                foreignValue == 1 && foreignCalls == 1, "Mixed transaction refuses without invoking foreign actions");
    c.undoEdit();
    const auto redoForeign = readback(c);
    require(c.previewHistoryDestination(HistoryDirection::redo, candidate).failed() && redoForeign == readback(c),
            "Foreign Redo transaction also refuses atomically");
    c.replace(Project::demo());
    require(c.validateHistoryDestination(candidate).failed() && !c.undo.canUndo() && !c.undo.canRedo(),
            "Project replacement invalidates token and preserves normal empty history policy");
    // Model-only portable descriptor: deliberately unavailable, never hosted/scanned.
    Controller native;
    juce::PluginDescription descriptor;
    descriptor.name = "Original History Metadata Fixture"; descriptor.pluginFormatName = "VST3";
    descriptor.fileOrIdentifier = "original-unavailable-history-fixture.vst3"; descriptor.isInstrument = true;
    PluginSpec spec; spec.id = native.project.allocate(); spec.name = descriptor.name;
    spec.descriptor = descriptor.createXml()->toString(); spec.instrument = true;
    spec.state = juce::MemoryBlock("old", 3).toBase64Encoding();
    native.project.channels.front().instrument = spec;
    auto captured = spec; captured.state = juce::MemoryBlock("fresh", 5).toBase64Encoding();
    auto expectedBeforeDelete = native.project; expectedBeforeDelete.plugin(spec.id)->state = captured.state;
    CommandArgs remove; remove.channel = native.project.channels.front().id; remove.capturedInstruments = {captured};
    require(native.execute("channel.delete", remove).wasOk(), "Captured baseline delete fixture");
    const auto deleted = native.project;
    require(native.previewHistoryDestination(HistoryDirection::undo, candidate).wasOk() &&
                candidate.destination().state().isEquivalentTo(expectedBeforeDelete.state()),
            "Undo preview uses stored fresh opaque native state, not a stale reconstructed baseline");
    native.undoEdit();
    require(expectedBeforeDelete.state().isEquivalentTo(native.project.state()), "Actual Undo retains captured state/source IDs");
    require(native.previewHistoryDestination(HistoryDirection::redo, candidate).wasOk() &&
                candidate.destination().state().isEquivalentTo(deleted.state()), "Captured delete Redo exact");
    native.redoEdit();
    require(deleted.state().isEquivalentTo(native.project.state()), "Actual Redo retains exact destination");
    require(folder.getChildFile("history-destination-verification.txt").replaceWithText(
                "PASS read-only actual JUCE transaction discovery, snapshot destination preview, guards, "
                "negative admission, exact Undo/Redo, macro and captured opaque-state semantics. "
                "Model/control tests only; no private graph publication, hosting, native input or listening qualification.\n"),
            "History destination evidence");
}
