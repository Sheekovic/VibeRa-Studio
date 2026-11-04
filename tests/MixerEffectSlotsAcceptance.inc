// SPDX-License-Identifier: AGPL-3.0-or-later
#include "../src/NoInline.h"
// Include once at the end of StudioMixerControls.cpp, outside its namespace.
// Component handlers and semantic calls only; no native OS input or audio claim.
#include "MixerEffectSlotsAcceptance.h"
#include "MixerReadabilityAcceptance.h"
#include "MixerWorkspaceReadabilityAcceptance.h"
namespace vibera
{
VIBERA_NOINLINE juce::Result Studio::verifyMixerChainControlsUi()
{
    auto fail = [](const char *message) { return juce::Result::fail(message); };
    auto *editor = static_cast<Editor *>(windows[3]->getContentComponent());
    const auto source = controller.project;
    const auto baseline = source.state();
    const auto *insert = mixerInsert(source, selectedInsert);
    if (!insert || insert->effects.size() != 16 || insert->effects[0].wet != 1)
        return fail("Original16-slot wet fixture");
    const auto first = insert->effects[0].id, second = insert->effects[1].id;
    const auto layout = editor->effectSlotsLayout();
    const auto knob = layout.wet(0).getCentre().toFloat();
    auto event =
        [&](juce::Point<float> point, int flags = juce::ModifierKeys::leftButtonModifier, int clicks = 1)
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), point,
                                juce::ModifierKeys(flags), 1, 0, 0, 0, 0, editor, editor, now, knob, now,
                                clicks, true);
    };
    const auto click = [&](juce::Rectangle<int> target)
    {
        editor->mouseDown(event(target.getCentre().toFloat()));
        editor->mouseUp(event(target.getCentre().toFloat()));
    };
    click(layout.chainBypass);
    auto chain = source;
    chain.mixer[1].effectsBypassed = true;
    if (!controller.project.state().isEquivalentTo(chain.state()) || controller.undoEditChecked().failed() ||
        !controller.project.state().isEquivalentTo(baseline) || controller.redoEditChecked().failed() ||
        !controller.project.state().isEquivalentTo(chain.state()) || controller.undoEditChecked().failed())
        return fail("Actual chain header exact target/Undo/Redo");
    const auto noOpRevision = controller.revision;
    const auto noOpDirty = controller.dirty;
    const auto noOpUndo = controller.undo.getUndoDescription(),
               noOpRedo = controller.undo.getRedoDescription();
    click(layout.wet(0));
    if (controller.revision != noOpRevision || controller.dirty != noOpDirty ||
        controller.undo.getUndoDescription() != noOpUndo ||
        controller.undo.getRedoDescription() != noOpRedo ||
        !controller.project.state().isEquivalentTo(baseline))
        return fail("Stationary wet click preserves exact history/no-op state");
    juce::MouseWheelDetails wheel{};
    wheel.deltaY = -1;
    editor->mouseWheelMove(event(knob), wheel);
    if (std::abs(controller.project.plugin(first)->wet - .9f) > 1.e-6 ||
        controller.project.plugin(second)->wet != 1 || controller.undoEditChecked().failed() ||
        !controller.project.state().isEquivalentTo(baseline))
        return fail("Wet wheel exact stable slot/one Undo");
    editor->mouseWheelMove(
        event(knob, juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier), wheel);
    if (std::abs(controller.project.plugin(first)->wet - .99f) > 1.e-6 ||
        controller.undoEditChecked().failed())
        return fail("Shift fine wet wheel");
    const auto beforePreviewUndo = controller.undo.getUndoDescription();
    editor->mouseDown(event(knob));
    editor->mouseDrag(event(knob.translated(0, 60)));
    editor->mouseDrag(event(knob.translated(0, 72),
                            juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier));
    auto pending = editor->effectSlotsState()["wetGesture"];
    if (pending["completionState"].toString() != "pending" || juce::int64(pending["plugin"]) != first ||
        !bool(pending["valid"]) || std::abs(controller.project.plugin(first)->wet - .49f) > 1.e-6 ||
        controller.undo.getUndoDescription() != beforePreviewUndo)
        return fail("Owned drag/fine typed pending preview, no premature Undo");
    editor->mouseUp(event(knob.translated(0, 72)));
    auto mixed = source;
    mixed.mixer[1].effects[0].wet = .49f;
    if (controller.hasGesture() || !editor->effectSlotsState()["wetGesture"].isVoid() ||
        !controller.project.state().isEquivalentTo(mixed.state()) || controller.undoEditChecked().failed() ||
        !controller.project.state().isEquivalentTo(baseline) || controller.redoEditChecked().failed() ||
        !controller.project.state().isEquivalentTo(mixed.state()) || controller.undoEditChecked().failed())
        return fail("Whole wet drag is one exact checked Undo/Redo");
    editor->mouseDown(event(knob));
    editor->mouseDrag(event(knob.translated(0, 60)));
    if (!performAction("selection.cancel") || controller.hasGesture() ||
        !editor->effectSlotsState()["wetGesture"].isVoid() ||
        !controller.project.state().isEquivalentTo(baseline))
        return fail("Escape action cancels exact wet preview");
    CommandArgs mix;
    mix.target = first;
    mix.value = .25;
    command("plugin.effect.mix", mix);
    editor->mouseDoubleClick(event(knob, juce::ModifierKeys::leftButtonModifier, 2));
    if (controller.project.plugin(first)->wet != 1 || controller.undoEditChecked().failed() ||
        controller.project.plugin(first)->wet != .25f || controller.undoEditChecked().failed() ||
        !controller.project.state().isEquivalentTo(baseline))
        return fail("Actual wet double-click reset is canonical/one Undo");
    if (editor->setEffectSlotsOffset(3).failed())
        return fail("Scroll wet target");
    editor->mouseWheelMove(event(editor->effectSlotsLayout().wet(0).getCentre().toFloat()), wheel);
    if (std::abs(controller.project.plugin(insert->effects[3].id)->wet - .9f) > 1.e-6 ||
        controller.project.plugin(first)->wet != 1 || controller.undoEditChecked().failed())
        return fail("Scrolled wet control resolves actual fourth stable ID");
    editor->setEffectSlotsOffset(0);
    if (!controller.beginGesture("foreign-wet-owner"))
        return fail("Independent Controller owner fixture");
    editor->mouseDown(event(knob));
    editor->mouseWheelMove(event(knob), wheel);
    editor->mouseDoubleClick(event(knob));
    click(layout.chainBypass);
    if (controller.gestureOwner() != "foreign-wet-owner" ||
        !controller.project.state().isEquivalentTo(baseline) ||
        !editor->effectSlotsState()["wetGesture"].isVoid() ||
        controller.cancelGestureChecked("foreign-wet-owner").failed())
        return fail("Foreign owner refuses every wet/header UI entry without takeover");
    editor->mouseDown(event(knob));
    editor->mouseDrag(event(knob.translated(0, 60)));
    controller.replace(source);
    if (!controller.beginGesture("new-project-owner"))
        return fail("New project owner fixture");
    editor->mouseUp(event(knob.translated(0, 60)));
    if (controller.gestureOwner() != "new-project-owner" ||
        !controller.project.state().isEquivalentTo(baseline) ||
        controller.cancelGestureChecked("new-project-owner").failed())
        return fail("Stale release preserves replacement project and foreign owner");
    if (!performAction("transport.record") || !recordArmed)
        return fail("Canonical recording arm for control admission");
    editor->mouseDown(event(knob));
    editor->mouseDrag(event(knob.translated(0, 60)));
    editor->mouseUp(event(knob.translated(0, 60)));
    if (!recordArmed || controller.project.plugin(first)->wet != .5f || controller.undoEditChecked().failed())
        return fail("Actual UI wet gesture preserves armed recording");
    auto args = juce::JSON::parse(R"({"action":"begin","domain":"mixerEffects","sourceInsert":1})");
    auto lease = mcpCall("vibera_gesture", args)["structuredContent"];
    if (lease["completionState"].toString() != "pending")
        return fail("Semantic recording-safe Mixer-effects lease");
    auto update =
        juce::JSON::parse(R"({"action":"update","command":"plugin.effect.mix","args":{"value":0.75}})");
    update.getDynamicObject()->setProperty("operation", lease["operation"]);
    update["args"].getDynamicObject()->setProperty("target", juce::int64(first));
    mcpCall("vibera_gesture", update);
    const auto validPreview = controller.project.state();
    auto wrong = juce::JSON::parse(
        R"({"action":"update","command":"mixer.effects.bypass","args":{"mixer":2,"enabled":true}})");
    wrong.getDynamicObject()->setProperty("operation", lease["operation"]);
    bool refused = false;
    try
    {
        mcpCall("vibera_gesture", wrong);
    }
    catch (const McpFailure &error)
    {
        refused = error.code == "gesture_domain";
    }
    if (!refused || !controller.project.state().isEquivalentTo(validPreview) || !recordArmed)
        return fail("Semantic lease cannot escape captured chain or disrupt recording");
    auto commit = juce::JSON::parse(R"({"action":"commit"})");
    commit.getDynamicObject()->setProperty("operation", lease["operation"]);
    if (mcpCall("vibera_gesture", commit)["structuredContent"]["completionState"].toString() != "completed" ||
        !performAction("transport.record") || recordArmed || controller.undoEditChecked().failed() ||
        !controller.project.state().isEquivalentTo(baseline))
        return fail("Semantic armed mix one Undo and canonical disarm");
    return juce::Result::ok();
}
juce::Result Studio::verifyMixerEffectSlots(const juce::File &output)
{
    auto fail = [](const juce::String &message) { return juce::Result::fail(message); };
    try
    {
        acceptance::effectSlots::geometry();
        acceptance::effectSlots::canonicalOrder(output);
    }
    catch (const std::exception &error)
    {
        return fail(error.what());
    }
    if (auto result = output.createDirectory(); result.failed())
        return result;
    stopTimer();
    audio.stop();
    auto project = Project::empty();
    std::vector<Id> ids;
    juce::PluginDescription missing;
    missing.pluginFormatName = "VST3";
    missing.fileOrIdentifier = output.getChildFile("original-missing-fixture.vst3").getFullPathName();
    missing.numInputChannels = missing.numOutputChannels = 2;
    for (int slot = 0; slot < 16; ++slot)
    {
        missing.name = "Unavailable fixture " + juce::String(slot + 1);
        ids.push_back(project.allocate());
        project.mixer[1].effects.push_back({ids.back(), missing.name, missing.createXml()->toString(),
                                            slot % 2 ? "1.A." : "1...", false, false});
    }
    controller.replace(project);
    selectedPattern = project.patterns.front().id;
    selectedChannel = project.channels.front().id;
    setSize(1920, 1044);
    refresh();
    if (auto result = selectMixerInsert(1); result.failed())
        return result;
    maximiseEditor(3);
    auto *editor = static_cast<Editor *>(windows[3]->getContentComponent());
    try
    {
        acceptance::mixerReadability::geometry(*editor, 0, 1);
        acceptance::mixerWorkspaceReadability::geometry(*editor, 0, 1, 17);
    }
    catch (const std::exception &error)
    {
        return juce::Result::fail(error.what());
    }
    auto click = [&](juce::Rectangle<int> bounds)
    {
        const auto point = bounds.getCentre().toFloat();
        const auto now = juce::Time::getCurrentTime();
        const juce::MouseEvent event(juce::Desktop::getInstance().getMainMouseSource(), point,
                                     juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1, 0, 0, 0,
                                     0, editor, editor, now, point, now, 1, false);
        editor->mouseDown(event);
        editor->mouseUp(event);
    };
    if (project.validate().failed())
        return fail("Original missing fixtures must form a valid project");
    const auto fullEditorBounds = editor->getBounds();
    editor->setSize(editor->getWidth(), 320); // Actual resizable compact pane,16-slot model limit unchanged.
    const auto baseline = controller.project.state();
    auto layout = editor->effectSlotsLayout();
    if (auto result = verifyMixerChainControlsUi(); result.failed())
        return result;
    auto intent = MixerEffectOrderIntent::capture(controller.project, controller.projectIdentity,
                                                  controller.revision, 1, ids[0]);
    if (!intent || editor->executeEffectOrder(*intent, 1).failed())
        return fail("Actual menu dispatch orders one effect down");
    auto ordered = project;
    std::swap(ordered.mixer[1].effects[0], ordered.mixer[1].effects[1]);
    const auto awaitOrderHistory = [&](bool undo)
    {
        const auto admitted = undo ? controller.undoEditChecked() : controller.redoEditChecked();
        if (admitted.failed() || lastMixerChainHistoryOperation.isEmpty())
            return admitted;
        const auto operation = lastMixerChainHistoryOperation;
        const auto started = juce::Time::getMillisecondCounter();
        auto current = mixerChainReadback(operation);
        while (current["completionState"].toString() == "pending" &&
               juce::Time::getMillisecondCounter() - started < 15000)
        {
            timerCallback();
            dispatchMessages(5);
            current = mixerChainReadback(operation);
        }
        if (!output.getChildFile("latest-order-history.json").replaceWithText(juce::JSON::toString(current)))
            return juce::Result::fail("Retain exact actual order-history terminal receipt");
        if (current["completionState"].toString() != "completed" || !bool(current["documentChanged"]) ||
            !bool(current["undoable"]) || bool(current["nativeRecoveryRequired"]))
            return juce::Result::fail("Actual order history did not complete: " +
                                      juce::JSON::toString(current));
        return juce::Result::ok();
    };
    if (!controller.project.state().isEquivalentTo(ordered.state()))
        return fail("Menu reorder retains distinct complete state");
    const auto menuUndo = awaitOrderHistory(true);
    if (menuUndo.failed())
        return fail("Menu order Undo: " + menuUndo.getErrorMessage());
    if (!controller.project.state().isEquivalentTo(baseline))
    {
        output.getChildFile("order-expected.xml").replaceWithText(baseline.toXmlString());
        output.getChildFile("order-actual.xml").replaceWithText(controller.project.state().toXmlString());
        return fail("Menu order Undo exact canonical state differs; retained expected/actual XML");
    }
    if (awaitOrderHistory(false).failed() || !controller.project.state().isEquivalentTo(ordered.state()))
        return fail("Menu order Redo");
    if (awaitOrderHistory(true).failed())
        return fail("Restore menu order baseline");
    if (editor->executeEffectOrder(*intent, 1).wasOk() ||
        !controller.project.state().isEquivalentTo(baseline))
        return fail("Stale menu revision refuses without changing order");
    auto drag = [&](int from, juce::Point<float> destination, bool release)
    {
        const auto origin = layout.grip(from).getCentre().toFloat();
        auto event = [&](juce::Point<float> point)
        {
            const auto now = juce::Time::getCurrentTime();
            return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), point,
                                    juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0,
                                    editor, editor, now, origin, now, 1, true);
        };
        editor->mouseDown(event(origin));
        editor->mouseDrag(event(destination));
        if (release)
            editor->mouseUp(event(destination));
    };
    drag(0, layout.row(1).getCentre().toFloat(), false);
    if (!controller.project.state().isEquivalentTo(baseline) ||
        editor->effectSlotsState()["reorder"]["completionState"].toString() != "pending")
        return fail("Grip preview owns typed pending intent without model/audio/history mutation");
    editor->cancelPointerGesture();
    if (!controller.project.state().isEquivalentTo(baseline) ||
        !editor->effectSlotsState()["reorder"].isVoid())
        return fail("Grip cancellation only drops its own transient intent");
    drag(0, layout.row(1).getCentre().toFloat(), true);
    if (!controller.project.state().isEquivalentTo(ordered.state()))
        return fail("Actual grip drop uses same canonical ordering");
    if (awaitOrderHistory(true).failed() || !controller.project.state().isEquivalentTo(baseline))
        return fail("Drag order exact one Undo");
    if (awaitOrderHistory(false).failed() || !controller.project.state().isEquivalentTo(ordered.state()))
        return fail("Drag order exact Redo");
    if (awaitOrderHistory(true).failed())
        return fail("Restore drag order baseline");
    drag(0, layout.row(1).getCentre().toFloat(), false);
    CommandArgs unrelated;
    unrelated.mixer = 2;
    unrelated.value = .25;
    if (controller.execute("mixer.pan", unrelated).failed())
        return fail("Independent edit during grip intent");
    const auto unrelatedState = controller.project.state();
    const auto stalePoint = layout.row(1).getCentre().toFloat();
    auto release = [&](juce::Point<float> point)
    {
        const auto now = juce::Time::getCurrentTime();
        const auto origin = layout.grip(0).getCentre().toFloat();
        const juce::MouseEvent event(juce::Desktop::getInstance().getMainMouseSource(), point,
                                     juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1, 0, 0, 0,
                                     0, editor, editor, now, origin, now, 1, true);
        editor->mouseUp(event);
    };
    release(stalePoint);
    if (!controller.project.state().isEquivalentTo(unrelatedState) ||
        !editor->effectSlotsState()["reorder"].isVoid())
        return fail("Late stale drop preserves the independent committed edit and exact order");
    if (awaitOrderHistory(true).failed() || !controller.project.state().isEquivalentTo(baseline))
        return fail("Restore independent edit baseline");
    drag(0, layout.row(1).getCentre().toFloat(), false);
    if (editor->setEffectSlotsOffset(editor->maximumEffectSlotsOffset()).failed())
        return fail("Scroll while holding reorder");
    const int lastVisible = editor->effectSlotsLayout().visibleRows() - 1;
    release(editor->effectSlotsLayout().row(lastVisible).getCentre().toFloat()); // Final Add row => existing
                                                                                 // last slot.
    auto last = project;
    std::rotate(last.mixer[1].effects.begin(), last.mixer[1].effects.begin() + 1,
                last.mixer[1].effects.end());
    if (!controller.project.state().isEquivalentTo(last.state()))
        return fail("Scrolled drag reaches exact final slot and preserves every distinct state");
    if (awaitOrderHistory(true).failed() || !controller.project.state().isEquivalentTo(baseline))
        return fail("Long drag one exact Undo");
    editor->setEffectSlotsOffset(0);
    drag(0, layout.row(1).getCentre().toFloat(), false);
    if (!controller.beginGesture("another-owner"))
        return fail("Independent actual Controller owner");
    if (editor->beginEffectOrder(ids[1]).wasOk() || editor->executeEffectOrder(*intent, 1).wasOk())
        return fail("Existing edit owner refuses reordering without takeover");
    CommandArgs busyMove;
    busyMove.target = ids[0];
    busyMove.lane = 1;
    command("plugin.effect.move", busyMove);
    Macro orderMacro{"macro.effect-order-fixture", "Original effect order fixture", {}};
    orderMacro.steps.push_back({"plugin.effect.move", busyMove, false});
    if (customization.addMacro(orderMacro).failed())
        return fail("Install original effect move macro fixture");
    runMacro(orderMacro.id);
    customization.removeMacro(orderMacro.id);
    bool semanticBusy = false;
    try
    {
        auto args = juce::JSON::parse(R"({"command":"plugin.effect.move","args":{"lane":1}})");
        args["args"].getDynamicObject()->setProperty("target", juce::int64(ids[0]));
        mcpCall("vibera_command", args);
    }
    catch (const McpFailure &error)
    {
        semanticBusy = error.code == "gesture_owned";
    }
    if (!semanticBusy || !controller.hasGesture() || controller.gestureOwner() != "another-owner" ||
        !controller.project.state().isEquivalentTo(baseline))
        return fail("Direct UI, macro and semantic final moves share exact active-owner refusal");
    editor->cancelPointerGesture();
    if (!controller.hasGesture() || controller.gestureOwner() != "another-owner" ||
        !controller.project.state().isEquivalentTo(baseline))
        return fail("Cancelling local reorder must not cancel another Controller gesture");
    if (controller.cancelGestureChecked("another-owner").failed())
        return fail("Cleanup owned test gesture");
    const auto noOpRevision = controller.revision;
    const auto noOpUndo = controller.undo.getUndoDescription(),
               noOpRedo = controller.undo.getRedoDescription();
    drag(0, layout.row(0).getCentre().toFloat(), true);
    drag(0, juce::Point<float>{1, 1}, true);
    if (controller.revision != noOpRevision || controller.undo.getUndoDescription() != noOpUndo ||
        controller.undo.getRedoDescription() != noOpRedo ||
        !controller.project.state().isEquivalentTo(baseline))
        return fail("Same-row and outside drop create no document/history mutation");
    click(layout.bypass(0));
    if (!controller.project.plugin(ids[0])->bypass || controller.project.plugin(ids[1])->bypass)
        return fail("Actual first-row power target bypasses only the stable first effect");
    if (awaitOrderHistory(true).failed() || !controller.project.state().isEquivalentTo(baseline))
        return fail("Power edit has one exact Undo");
    if (awaitOrderHistory(false).failed() || !controller.project.plugin(ids[0])->bypass)
        return fail("Power edit Redo");
    if (awaitOrderHistory(true).failed())
        return fail("Restore power baseline");
    const auto revision = controller.revision;
    const auto dirty = controller.dirty;
    const auto undo = controller.undo.getUndoDescription(), redo = controller.undo.getRedoDescription();
    click(layout.name(0));
    if (status != "Plugin worker is unavailable; saved state is retained" ||
        controller.revision != revision || controller.dirty != dirty ||
        controller.undo.getUndoDescription() != undo || controller.undo.getRedoDescription() != redo ||
        !controller.project.state().isEquivalentTo(baseline))
        return fail("Name click reaches actual native service and truthful unavailable refusal without "
                    "document change");
    auto args = juce::JSON::parse(R"({"action":"scrollEffects","offset":3})");
    auto response = mcpCall("vibera_mixer", args)["structuredContent"];
    if (response["completionState"].toString() != "completed" || int(response["effectsPane"]["offset"]) != 3)
        return fail("Semantic effect scrolling shares component state");
    layout = editor->effectSlotsLayout();
    click(layout.bypass(0));
    if (!controller.project.plugin(ids[3])->bypass || controller.project.plugin(ids[0])->bypass)
        return fail("Scrolled power click resolves current stable fourth ID");
    if (awaitOrderHistory(true).failed())
        return fail("Scrolled power Undo");
    const auto now = juce::Time::getCurrentTime();
    const auto point = layout.rows.getCentre().toFloat();
    const juce::MouseEvent event(juce::Desktop::getInstance().getMainMouseSource(), point, {}, 1, 0, 0, 0, 0,
                                 editor, editor, now, point, now, 1, false);
    juce::MouseWheelDetails wheel{};
    wheel.deltaY = -1;
    editor->mouseWheelMove(event, wheel);
    if (editor->effectSlotsOffset() != 4)
        return fail("Actual pane wheel scrolls effect rows");
    bool invalidOffset = false;
    try
    {
        mcpCall("vibera_mixer", juce::JSON::parse(R"({"action":"scrollEffects","offset":2147483647})"));
    }
    catch (const McpFailure &error)
    {
        invalidOffset = error.code == "invalid_arguments";
    }
    if (!invalidOffset || editor->effectSlotsOffset() != 4)
        return fail("Invalid semantic offset has no partial navigation");
    click(layout.add);
    auto browser = mixerEffectBrowserState();
    if (!bool(browser["open"]) || int(browser["insert"]) != 1 || !bool(browser["targetCurrent"]))
        return fail("Header Add opens actual Settings page at selected insert");
    response = mcpCall("vibera_mixer",
                       juce::JSON::parse(R"({"action":"browseEffects","insert":2})"))["structuredContent"];
    if (response["completionState"].toString() != "completed" ||
        int(response["effectsBrowser"]["insert"]) != 2)
        return fail("Semantic Add explicitly retargets an already-open Settings page");
    juce::String code;
    if (browseMixerEffects(1, code).failed() || int(mixerEffectBrowserState()["insert"]) != 1)
        return fail("Repeated UI Add cannot keep the previous insert target");
    if (browseMixerEffects(999, code).wasOk() || code != "unavailable_target" ||
        int(mixerEffectBrowserState()["insert"]) != 1)
        return fail("Unknown insert refuses without changing existing browser target");
    if (!controller.project.state().isEquivalentTo(baseline))
        return fail("Browser and scroll do not edit project");
    if (selectMixerInsert(2).failed())
        return fail("Select an actual empty insert");
    click(editor->effectSlotsLayout().row(0));
    if (int(mixerEffectBrowserState()["insert"]) != 2)
        return fail("Actual empty-row Add opens the selected empty insert");
    if (selectMixerInsert(1).failed())
        return fail("Restore populated insert");
    closeSettings();
    editor->setEffectSlotsOffset(0);
    editor->setBounds(fullEditorBounds);
    CommandArgs showMix;
    showMix.target = ids[0];
    showMix.value = .25;
    command("plugin.effect.mix", showMix);
    showMix.target = ids[1];
    showMix.value = .75;
    command("plugin.effect.mix", showMix);
    if (status != "Ready")
        return fail("Prepare actual populated wet controls before component capture");
    auto capture = [&](const char *name, int width, int height, float scale)
    {
        setSize(width, height);
        refresh();
        auto image = createComponentSnapshot(getLocalBounds(), true, scale);
        auto stream = output.getChildFile(name).createOutputStream();
        if (!stream)
            return false;
        stream->setPosition(0);
        stream->truncate();
        return juce::PNGImageFormat{}.writeImageToStream(image, *stream);
    };
    if (!capture("mixer-effects-1080p.png", 1920, 1044, 1) ||
        !capture("mixer-effects-4k-150.png", 2560, 1392, 1.5f))
        return fail("Effect pane component captures");
    CommandArgs showChain;
    showChain.mixer = 1;
    showChain.enabled = true;
    command("mixer.effects.bypass", showChain);
    if (status != "Ready" || !capture("mixer-effects-chain-bypassed-1080p.png", 1920, 1044, 1))
        return fail("Actual retained slot mixes under chain bypass capture");
    if (browseMixerEffects(1, code).failed())
        return fail("Reopen browser for stale-project proof");
    auto *tabs =
        settingsWindow ? dynamic_cast<SettingsContent *>(settingsWindow->getContentComponent()) : nullptr;
    auto *page = tabs ? tabs->pluginPage() : nullptr;
    if (!page)
        return fail("Actual Settings page for stale-project proof");
    const auto oldProject = controller.projectIdentity;
    controller.replace(Project::empty());
    if (page->selectMixerInsert(2, oldProject).wasOk() || bool(mixerEffectBrowserState()["targetCurrent"]))
        return fail("Stale Add intent refuses before replacing newer-project dropdown");
    if (browseMixerEffects(2, code).failed() || int(mixerEffectBrowserState()["insert"]) != 2 ||
        mixerEffectBrowserState()["projectIdentity"].toString() != controller.projectIdentity)
        return fail("Explicit new-project Add binds current project and target");
    closeSettings();
    output.getChildFile("verification.txt")
        .replaceWithText(
            "PASS original slot/grip geometry; canonical unequal-state ordering/no-op/invalid/Undo/Redo; "
            "component menu/grip/drop/cancel/stale/scrolled ordering; shared UI/macro/MCP ownership refusal; "
            "actual wet drag/fine/wheel/reset/cancel/late-owner/armed-MCP equivalence and chain header "
            "Undo/Redo; "
            "actual component bypass/current stable IDs/Undo/Redo; "
            "native editor unavailable refusal (missing original fixtures only); shared MCP scroll/invalid "
            "ranges; "
            "actual Settings Add/reused-page retarget/readback; component1080p/4K150 captures. "
            "No actual plugin success, native OS input, audio playback, hardware or listening claim.\n");
    return juce::Result::ok();
}
} // namespace vibera
