// SPDX-License-Identifier: AGPL-3.0-or-later
#include "../src/NoInline.h"
// Include once after StudioMixerControls.cpp namespace closes. Actual component
// handlers/shared Studio services; no OS pointer input, DSP or hardware claim.
namespace vibera
{
VIBERA_NOINLINE juce::Result Studio::verifyMixerInsertOrder(const juce::File &folder)
{
    juce::Component::SafePointer<juce::Component> ownedMenu;
    juce::StringArray cases;
    const auto record = [&](bool passed, const juce::String &error)
    {
        auto *result = new juce::DynamicObject;
        result->setProperty("passed", passed);
        result->setProperty("failure", error);
        result->setProperty("cases", cases);
        result->setProperty("nativeOsInput", false);
        result->setProperty("listening", false);
        result->setProperty("hardware", false);
        return folder.getChildFile("insert-order-ui.json")
            .replaceWithText(juce::JSON::toString(juce::var(result), true));
    };
    const auto check = [](bool ok, const char *text)
    {
        if (!ok)
            throw std::runtime_error(text);
    };
    try
    {
        check(controllerTestMode && folder.createDirectory().wasOk() &&
                  !juce::Component::getCurrentlyModalComponent() && !controller.hasGesture() && !midiTaking &&
                  !hasPendingMidiTake() && controller.project.mixer.size() == 18,
              "Owned isolated idle default18 component fixture");
        auto *editor = static_cast<Editor *>(windows[3]->getContentComponent());
        auto builder = std::make_unique<MacroPage>(*this);
        check(builder->verifyMixerInsertOrderBuilder().wasOk(),
              "Actual macro builder uses typed absolute/no relative amounts");
        builder.reset();
        setSize(1920, 1044);
        showEditor(3);
        if (maximisedEditor != 3)
            maximiseEditor(3);
        check(selectMixerInsert(1).wasOk(), "Actual source selection");
        focus = Scope::mixer;
        const auto before = controller.project.state();
        const int source = selectedInsert;
        const auto channel = selectedChannel, pattern = selectedPattern;
        const auto contents = [&]
        {
            auto copy = controller.project;
            std::sort(copy.mixer.begin(), copy.mixer.end(),
                      [](const auto &a, const auto &b) { return a.index < b.index; });
            return copy.state();
        }();
        const auto context = [&]
        { return selectedInsert == source && selectedChannel == channel && selectedPattern == pattern; };
        check(keyPressed(juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys::altModifier, 0)) &&
                  mixerInsertPosition(controller.project, 1) == 2 && context(),
              "Actual Mixer Alt+Right retains selected/source IDs");
        check(keyPressed(juce::KeyPress('Z', juce::ModifierKeys::ctrlModifier, 0)) &&
                  before.isEquivalentTo(controller.project.state()),
              "Actual Ctrl+Z restores exact reorder");
        check(keyPressed(juce::KeyPress(
                  'Z', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier, 0)) &&
                  mixerInsertPosition(controller.project, 1) == 2 && context(),
              "Actual Ctrl+Shift+Z restores exact destination");
        check(controller.undoEditChecked().wasOk(), "Restore source before wheel");
        cases.add("actual-default-keyboard-history");
        auto event = [&](juce::Point<float> point, int flags)
        {
            const auto now = juce::Time::getCurrentTime();
            return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), point,
                                    juce::ModifierKeys(flags), 1, 0, 0, 0, 0, editor, editor, now, point, now,
                                    1, false);
        };
        juce::MouseWheelDetails wheel{};
        wheel.deltaY = -1;
        const auto hovered = editor->mixerNameBounds(2).getCentre().toFloat();
        const auto recorded = recordedSteps.size();
        recordingNotes = true;
        editor->mouseWheelMove(event(hovered, juce::ModifierKeys::shiftModifier), wheel);
        recordingNotes = false;
        check(mixerInsertPosition(controller.project, 2) == 3 && context(),
              "Actual Shift-wheel names target hovered stable ID, not selection/send destination");
        check(recordedSteps.size() == recorded + 1 && recordedSteps.back().parameters.mixer == 2 &&
                  !recordedSteps.back().useCurrentContext,
              "Actual action recorder retains hovered stable source instead of replaying on selected send "
              "source");
        check(controller.undoEditChecked().wasOk() && before.isEquivalentTo(controller.project.state()),
              "One wheel event one exact Undo");
        cases.add("actual-hovered-name-wheel-and-history");
        const auto open = [&]
        {
            check(!juce::Component::getCurrentlyModalComponent(), "No unrelated modal may be dismissed");
            const auto at = editor->mixerNameBounds(1).getCentre().toFloat();
            editor->mouseDown(event(at, juce::ModifierKeys::rightButtonModifier));
            editor->mouseUp(event(at, juce::ModifierKeys::rightButtonModifier));
            ownedMenu = juce::Component::getCurrentlyModalComponent();
            check(bool(ownedMenu), "Actual strip-name popup opens");
        };
        const auto choose = [&](int item)
        {
            check(ownedMenu && ownedMenu.getComponent() == juce::Component::getCurrentlyModalComponent(),
                  "Only fixture-owned popup is completed");
            ownedMenu->exitModalState(item);
            const auto deadline = juce::Time::getMillisecondCounter() + 1000;
            while (ownedMenu && juce::Time::getMillisecondCounter() < deadline)
                dispatchMessages(1);
            check(!ownedMenu, "Actual popup callback completes and owned menu retires");
        };
        open();
        choose(10);
        check(mixerInsertPosition(controller.project, 1) == 2 && selectedInsert == 1 &&
                  controller.undoEditChecked().wasOk() && before.isEquivalentTo(controller.project.state()),
              "Actual Move right context callback and one Undo");
        open();
        const auto cancelled = controller.project.state();
        const auto cancelRevision = controller.revision;
        choose(0);
        check(cancelled.isEquivalentTo(controller.project.state()) && cancelRevision == controller.revision,
              "Context cancellation creates no edit");
        open();
        CommandArgs colour;
        colour.mixer = 1;
        colour.value = double(0xff36a890);
        command("mixer.colour", colour);
        const auto stale = controller.project.state();
        const auto staleRevision = controller.revision;
        choose(10);
        check(stale.isEquivalentTo(controller.project.state()) && staleRevision == controller.revision &&
                  status.startsWith("mixer.staleOrder:"),
              "Actual delayed order choice refuses changed revision without retargeting");
        check(controller.undoEditChecked().wasOk(), "Restore after independent colour mutation");
        cases.add("actual-popup-move-cancel-stale");
        check(controller.beginGesture("original-other-owner"), "Actual foreign gesture acquired");
        const auto busy = controller.project.state();
        const auto busyRevision = controller.revision;
        keyPressed(juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys::altModifier, 0));
        check(controller.project.state().isEquivalentTo(busy) && busyRevision == controller.revision &&
                  controller.gestureOwner() == "original-other-owner" &&
                  status.startsWith("mixer.orderGestureOwned:"),
              "Keyboard cannot steal active gesture");
        check(controller.cancelGestureChecked("original-other-owner").wasOk(),
              "Explicit fixture owner cancellation");
        recordArmed = true;
        CommandArgs order;
        order.mixer = 1;
        command("mixer.moveRight", order);
        check(recordArmed && !midiTaking && !hasPendingMidiTake() &&
                  mixerInsertPosition(controller.project, 1) == 2 && context(),
              "Armed-but-not-capturing reorder retains actual arm and source context");
        check(controller.undoEditChecked().wasOk() && recordArmed, "One exact order Undo leaves arm alone");
        recordArmed = false;
        cases.add("shared-foreign-gesture-and-armed-admission");
        auto response =
            mcpCall("vibera_command",
                    juce::JSON::parse(R"({"command":"mixer.move","args":{"mixer":1,"mixerPosition":4}})"));
        check(!bool(response["isError"]) &&
                  response["structuredContent"]["completionState"].toString() == "completed" &&
                  response["structuredContent"]["completionScope"].toString() == "canonicalProjectOrder" &&
                  mixerInsertPosition(controller.project, 1) == 4 && context(),
              "Actual semantic operation shares canonical model and completion scope");
        auto sorted = controller.project;
        std::sort(sorted.mixer.begin(), sorted.mixer.end(),
                  [](const auto &a, const auto &b) { return a.index < b.index; });
        check(sorted.state().isEquivalentTo(contents),
              "All original source/send/effect/automation state retained by ID");
        check(controller.undoEditChecked().wasOk() && before.isEquivalentTo(controller.project.state()),
              "Actual semantic reorder one Undo");
        cases.add("actual-semantic-readback-and-history");
        Macro macro;
        macro.id = "macro.original-component-order";
        macro.name = "Original component reorder";
        CommandArgs absolute;
        absolute.mixerPosition = 3;
        macro.steps = {{"mixer.move", absolute, true}, {"mixer.moveRight", CommandArgs{}, true}};
        check(customization.addMacro(macro).wasOk(), "Actual app macro uses typed canonical commands");
        runMacro(macro.id);
        check(status == "Macro complete" && mixerInsertPosition(controller.project, 1) == 4 && context() &&
                  controller.undoEditChecked().wasOk() && before.isEquivalentTo(controller.project.state()),
              "Actual Studio macro admission/context and one grouped Undo");
        cases.add("actual-macro-builder-and-execution");
        check(record(true, {}), "Retain component acceptance receipt");
        return juce::Result::ok();
    }
    catch (const std::exception &error)
    {
        if (ownedMenu && ownedMenu.getComponent() == juce::Component::getCurrentlyModalComponent())
            ownedMenu->exitModalState(0);
        record(false, error.what());
        return juce::Result::fail(error.what());
    }
}
} // namespace vibera
