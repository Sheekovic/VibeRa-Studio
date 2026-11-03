// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Studio.h"
#include "Editors.h"
#include "MixerRouting.h"
#include "MixerInsertOrder.h"
#include "MixerMacroInput.h"
#include "Settings.h"
#include "../tests/SyntheticDevice.h"
namespace vibera
{
juce::Result Studio::selectMixerInsert(int index)
{
    return selectMixerMany({index}, "replace", index);
}
void Studio::mixerRouteMenu(int source, int target)
{
    const auto available = canMixerRoute(controller.project, source, target);
    const auto *route = mixerRoute(controller.project, source, target);
    juce::PopupMenu menu;
    menu.addSectionHeader("From " + (mixerInsert(controller.project, source)
                                         ? mixerInsert(controller.project, source)->name
                                         : juce::String("Master")));
    menu.addItem(1, "Route to this track", available.wasOk(), route != nullptr);
    menu.addItem(2, "Route to this track only", available.wasOk());
    menu.addItem(3, "Disable send", route != nullptr);
    menu.addItem(4, "Set send level...", route != nullptr);
    menu.addItem(5, "Reset send to100%", route != nullptr);
    if (available.failed())
        menu.addItem(99, available.getErrorMessage(), false);
    const auto identity = controller.projectIdentity;
    menu.showMenuAsync(
        {},
        [safe = juce::Component::SafePointer<Studio>(this), identity, source, target](int action)
        {
            if (!safe || !action)
                return;
            if (safe->controller.projectIdentity != identity)
            {
                safe->status = "Project changed; routing menu cancelled";
                return;
            }
            if (action == 4)
            {
                safe->promptMixerSend(source, target);
                return;
            }
            CommandArgs a;
            a.mixer = source;
            a.target = target;
            a.enabled = action != 3;
            a.value = 1;
            safe->command(action == 2 ? "mixer.routeOnly" : action == 5 ? "mixer.send" : "mixer.route", a);
        });
}
void Studio::promptMixerSend(int source, int target)
{
    const auto *route = mixerRoute(controller.project, source, target);
    if (!route)
    {
        status = "Enable the route before setting its level";
        return;
    }
    auto *dialog = new juce::AlertWindow("Mixer send level", "Linear send percentage (0..200;100=unity)",
                                         juce::MessageBoxIconType::NoIcon);
    dialog->setLookAndFeel(&look);
    dialog->addTextEditor("amount", juce::String(route->gain * 100, 2), "Percent");
    dialog->addButton("Apply", 1, juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    const auto identity = controller.projectIdentity;
    const auto revision = controller.revision;
    auto safeDialog = juce::Component::SafePointer<juce::AlertWindow>(dialog);
    dialog->enterModalState(
        true,
        juce::ModalCallbackFunction::create(
            [safe = juce::Component::SafePointer<Studio>(this), safeDialog, identity, revision, source,
             target](int result)
            {
                if (!safe || !safeDialog || result != 1)
                    return;
                if (safe->controller.projectIdentity != identity || safe->controller.revision != revision)
                {
                    safe->status = "Mixer source changed; no level edit applied";
                    return;
                }
                CommandArgs a;
                a.mixer = source;
                const auto input = juce::String(target) + ":" + safeDialog->getTextEditorContents("amount");
                if (auto r = parseMixerMacro("mixer.send", input, a); r.failed())
                {
                    safe->status = r.getErrorMessage();
                    return;
                }
                safe->command("mixer.send", a);
            }),
        true);
}
bool Studio::performMixerRoutingAction(const juce::String &action)
{
    if (action != "mixer.route" && action != "mixer.routeOnly" && action != "mixer.send")
        return false;
    const int source = selectedInsert;
    if (source == 0)
    {
        status = "Select a source insert; Master is destination only";
        return true;
    }
    juce::PopupMenu menu;
    std::vector<int> targets;
    for (const auto &m : controller.project.mixer)
    {
        const auto *route = mixerRoute(controller.project, source, m.index);
        targets.push_back(m.index);
        menu.addItem(int(targets.size()), m.name,
                     action == "mixer.send" ? route != nullptr
                                            : canMixerRoute(controller.project, source, m.index).wasOk(),
                     route != nullptr);
    }
    const auto identity = controller.projectIdentity;
    menu.showMenuAsync(
        {},
        [safe = juce::Component::SafePointer<Studio>(this), identity, action, source, targets](int choice)
        {
            if (!safe || choice < 1 || choice > int(targets.size()))
                return;
            if (safe->controller.projectIdentity != identity)
            {
                safe->status = "Project changed; routing action cancelled";
                return;
            }
            const auto target = targets[size_t(choice - 1)];
            if (action == "mixer.send")
            {
                safe->promptMixerSend(source, target);
                return;
            }
            CommandArgs a;
            a.mixer = source;
            a.target = target;
            a.enabled = action == "mixer.routeOnly" || !mixerRoute(safe->controller.project, source, target);
            safe->command(action, a);
        });
    return true;
}
juce::var Studio::mixerRoutingState() const
{
    auto *state = new juce::DynamicObject;
    state->setProperty("selectedInsert", selectedInsert);
    state->setProperty("organisation", mixerOrganisationState());
    state->setProperty("order", mixerInsertOrderState(controller.project, selectedInsert));
    state->setProperty("projectIdentity", controller.projectIdentity);
    state->setProperty("revision", juce::int64(controller.revision));
    state->setProperty("routes", mixerRoutesState(controller.project, selectedInsert, true));
    state->setProperty("gainUnits", "linear factor0..2;1=100%;UI displays percent");
    state->setProperty("tap", "post-effects/fader/pan");
    if (controller.project.mixerSolo)
    {
        auto *solo = new juce::DynamicObject;
        solo->setProperty("insert", controller.project.mixerSolo->insert);
        solo->setProperty("connected", controller.project.mixerSolo->connected);
        solo->setProperty("group", controller.project.mixerSolo->group);
        juce::Array<juce::var> baseline;
        for (auto index : controller.project.mixerSolo->mutedBefore)
            baseline.add(index);
        solo->setProperty("mutedBefore", baseline);
        state->setProperty("solo", juce::var(solo));
    }
    else
        state->setProperty("solo", juce::var());
    juce::Array<juce::var> controls;
    auto *editor = static_cast<Editor *>(windows[3]->getContentComponent());
    const auto bounds = getLocalArea(editor, editor->getLocalBounds());
    state->setProperty("contentBounds", juce::Array<juce::var>{bounds.getX(), bounds.getY(),
                                                               bounds.getWidth(), bounds.getHeight()});
    for (const auto &m : controller.project.mixer)
    {
        auto *row = new juce::DynamicObject;
        row->setProperty("index", m.index);
        row->setProperty("name", m.name);
        row->setProperty("selected", mixerSelection.contains(m.index));
        row->setProperty("group", mixerGroupFor(controller.project, m.index));
        row->setProperty("pan", m.pan);
        row->setProperty("muted", m.muted);
        row->setProperty("gain", m.gain);
        row->setProperty("colour", juce::int64(m.colour));
        row->setProperty("meter", mixerMeterState(m.index));
        auto rect = [](juce::Rectangle<int> r)
        { return juce::var(juce::Array<juce::var>{r.getX(), r.getY(), r.getWidth(), r.getHeight()}); };
        row->setProperty("nameBounds", rect(editor->mixerNameBounds(m.index)));
        row->setProperty("muteBounds", rect(editor->mixerMuteBounds(m.index)));
        row->setProperty("panBounds", rect(editor->mixerPanBounds(m.index)));
        row->setProperty("faderBounds", rect(editor->mixerFaderBounds(m.index)));
        row->setProperty("meterBounds", rect(editor->mixerMeterBounds(m.index)));
        const auto button = editor->mixerRouteButtonBounds(m.index),
                   level = editor->mixerSendLevelBounds(m.index);
        row->setProperty("button", juce::Array<juce::var>{button.getX(), button.getY(), button.getWidth(),
                                                          button.getHeight()});
        row->setProperty(
            "level", juce::Array<juce::var>{level.getX(), level.getY(), level.getWidth(), level.getHeight()});
        controls.add(juce::var(row));
    }
    state->setProperty("controls", controls);
    return juce::var(state);
}
juce::Result Studio::verifyMixerRouting(const juce::File &output)
{
    if (auto r = output.createDirectory(); r.failed())
        return r;
    auto fail = [&](const juce::String &message)
    {
        output.getChildFile("failure.txt").replaceWithText(message);
        return juce::Result::fail(message);
    };
    stopTimer();
    controller.replace(Project::demo());
    controller.project.name = "Route drums to a shared bus";
    auto *editor = static_cast<Editor *>(windows[3]->getContentComponent());
    setSize(1920, 1044);
    refresh();
    if (selectMixerInsert(1).failed())
        return fail("Shared Mixer source selection");
    auto point = editor->mixerRouteButtonBounds(3).getCentre().toFloat();
    juce::Point<float> origin = point;
    auto event = [&](juce::Point<float> p, int mods = juce::ModifierKeys::leftButtonModifier)
    {
        auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), p,
                                juce::ModifierKeys(mods), 1, 0, 0, 0, 0, editor, editor, now, origin, now, 1,
                                true);
    };
    editor->mouseDown(event(point));
    editor->mouseUp(event(point));
    if (selectedInsert != 1 || !mixerRoute(controller.project, 1, 3) || !mixerRoute(controller.project, 1, 0))
        return fail("Destination arrow lost source selection or default Master route");
    const auto baseline = controller.project.state();
    origin = editor->mixerSendLevelBounds(3).getCentre().toFloat();
    editor->mouseDown(event(origin));
    editor->mouseDrag(event(origin + juce::Point<float>{0, 50}));
    editor->mouseUp(event(origin + juce::Point<float>{0, 50}));
    if (std::abs(mixerRoute(controller.project, 1, 3)->gain - .5f) > 1.e-5 || controller.hasGesture())
        return fail("Actual send drag did not commit the expected level");
    const auto changed = controller.project.state();
    controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(baseline))
        return fail("Send stroke is not one exact Undo");
    controller.redoEdit();
    if (!controller.project.state().isEquivalentTo(changed))
        return fail("Send stroke Redo");
    selectedNotes = {controller.project.patterns.front().notes.front().id};
    selectedClips = {controller.project.placements.front().id};
    const auto noteSelection = selectedNotes, clipSelection = selectedClips;
    editor->mouseDown(event(origin));
    editor->mouseDrag(event(origin + juce::Point<float>{0, 20}));
    if (!performAction("selection.cancel") || !controller.project.state().isEquivalentTo(changed))
        return fail("Escape did not restore the exact level baseline");
    if (selectedNotes != noteSelection || selectedClips != clipSelection)
        return fail("Mixer Escape changed another editor's selection");
    editor->mouseUp(event(origin));
    editor->mouseDoubleClick(event(origin));
    if (mixerRoute(controller.project, 1, 3)->gain != 1)
        return fail("Double-click send unity reset");
    juce::MouseWheelDetails wheel{};
    wheel.deltaY = .5f;
    editor->mouseWheelMove(event(origin), wheel);
    if (std::abs(mixerRoute(controller.project, 1, 3)->gain - 1.1f) > 1.e-5)
        return fail("Send wheel uses the canonical level command");
    controller.undoEdit();
    editor->mouseDown(event(origin));
    editor->mouseDrag(event(origin + juce::Point<float>{0, 50},
                            juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier));
    editor->mouseUp(event(origin));
    if (std::abs(mixerRoute(controller.project, 1, 3)->gain - .95f) > 1.e-5)
        return fail("Shift fine send drag");
    controller.undoEdit();
    MacroPage macro(*this);
    if (auto r = macro.verifyMixerBuilder(); r.failed())
        return fail(r.getErrorMessage());
    auto dialog = [&](const juce::String &text, int result)
    {
        promptMixerSend(1, 3);
        auto *window = dynamic_cast<juce::AlertWindow *>(juce::Component::getCurrentlyModalComponent());
        if (!window || !window->getTextEditor("amount"))
            return false;
        window->getTextEditor("amount")->setText(text);
        window->exitModalState(result);
        dispatchMessages(30);
        return true;
    };
    if (!dialog("50", 1) || mixerRoute(controller.project, 1, 3)->gain != .5f)
        return fail("Functional percent-entry dialog");
    const auto precise = controller.project.state();
    if (!dialog("50junk", 1) || !controller.project.state().isEquivalentTo(precise))
        return fail("Malformed send percent was accepted");
    if (!dialog("100", 0) || !controller.project.state().isEquivalentTo(precise))
        return fail("Send dialog cancel edited source");
    promptMixerSend(1, 3);
    auto *stale = dynamic_cast<juce::AlertWindow *>(juce::Component::getCurrentlyModalComponent());
    if (!stale)
        return fail("Open stale-source dialog");
    stale->getTextEditor("amount")->setText("125");
    CommandArgs intervening;
    intervening.mixer = 1;
    intervening.target = 3;
    intervening.value = .75;
    command("mixer.send", intervening);
    stale->exitModalState(1);
    dispatchMessages(30);
    if (mixerRoute(controller.project, 1, 3)->gain != .75f)
        return fail("Stale percent dialog overwrote newer source state");
    controller.undoEdit();
    // A clipped strip is painted behind the FX pane. Its raw knob coordinates
    // must never receive wheel/unity edits from that different visible surface.
    const auto wideWidth = getWidth(), wideHeight = getHeight();
    setSize(1280, 1044);
    refresh();
    if (editor->getWidth() < 700)
        return fail("Narrow Mixer fixture must include the FX pane");
    int hiddenDestination = -1;
    for (const auto &insert : controller.project.mixer)
    {
        const auto centre = editor->mixerSendLevelBounds(insert.index).getCentre();
        if (insert.index != 0 && insert.index != 1 && centre.x > editor->getWidth() - 200 &&
            centre.x < editor->getWidth() - 20)
        {
            hiddenDestination = insert.index;
            break;
        }
    }
    if (hiddenDestination < 0)
        return fail("Expected an offscreen send below the visible FX pane");
    const auto beforeHidden = controller.project.state();
    CommandArgs hidden;
    hidden.mixer = 1;
    hidden.target = hiddenDestination;
    hidden.enabled = true;
    command("mixer.route", hidden);
    hidden.value = .5;
    command("mixer.send", hidden);
    origin = editor->mixerSendLevelBounds(hiddenDestination).getCentre().toFloat();
    const auto hiddenBaseline = controller.project.state();
    const auto hiddenRevision = controller.revision;
    editor->mouseWheelMove(event(origin), wheel);
    editor->mouseDoubleClick(event(origin));
    if (!controller.project.state().isEquivalentTo(hiddenBaseline) || controller.revision != hiddenRevision)
        return fail("FX-pane wheel/double-click changed a clipped hidden send");
    controller.undoEdit();
    controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(beforeHidden))
        return fail("Narrow Mixer fixture cleanup lost source state");
    setSize(wideWidth, wideHeight);
    refresh();
    CommandArgs only;
    only.mixer = 2;
    only.target = 3;
    command("mixer.routeOnly", only);
    if (mixerRoute(controller.project, 2, 0) || !mixerRoute(controller.project, 2, 3))
        return fail("Bus-only model after UI edits");
    auto screenshot = [&](juce::String name, float scale)
    {
        auto image = createComponentSnapshot(getLocalBounds(), true, scale);
        auto stream = output.getChildFile(name).createOutputStream();
        if (!stream)
            return false;
        stream->setPosition(0);
        stream->truncate();
        return juce::PNGImageFormat().writeImageToStream(image, *stream);
    };
    refresh();
    if (!screenshot("mixer-routes-workspace-1080p.png", 1))
        return fail("Workspace capture");
    maximiseEditor(3);
    refresh();
    if (!screenshot("mixer-routes-1080p.png", 1))
        return fail("Mixer capture");
    setSize(2560, 1392);
    refresh();
    if (!screenshot("mixer-routes-4k-150.png", 1.5f))
        return fail("4K render");
    if (saveProject(controller.project, output.getChildFile("shared-bus-showcase.vibra")).failed())
        return fail("Save actual Mixer project");
    output.getChildFile("mixer-routing-gui.txt")
        .replaceWithText(
            "PASS actual JUCE source selection/destination "
            "toggle/drag/fine/wheel/history/Escape/unity, clipped-send FX-pane isolation, strict percent "
            "dialog/cancel/stale-source and macro builder callbacks, project and populated "
            "workspace/1080p/4K150 component "
            "renders. Context menus, native OS input, live PCM and listening separate.\n");
    return juce::Result::ok();
}
#include "StudioMixerOrganisation.inc"
#include "../tests/MixerOrganisationUiAcceptance.inc"
} // namespace vibera
