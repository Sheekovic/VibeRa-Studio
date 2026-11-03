// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../src/PluginWrapperView.h"
#include <memory>

// Include after the acceptance harness require(). These are GUI-component
// ownership/intent tests, not native OS input or third-party-editor evidence.
namespace plugin_wrapper_view_acceptance
{
using namespace vibera;

template <typename T> static T *visibleChild(PluginWrapperViewComponent &view)
{
    T *result = nullptr;
    for (int i = 0; i < view.getNumChildComponents(); ++i)
        if (auto *candidate = dynamic_cast<T *>(view.getChildComponent(i));
            candidate && candidate->isVisible())
        {
            require(result == nullptr, "Wrapper fixture has one visible control of requested type");
            result = candidate;
        }
    require(result != nullptr, "Wrapper fixture exposes the requested control");
    return result;
}

static juce::TextButton *button(PluginWrapperViewComponent &view, const juce::String &caption)
{
    for (int i = 0; i < view.getNumChildComponents(); ++i)
        if (auto *candidate = dynamic_cast<juce::TextButton *>(view.getChildComponent(i));
            candidate && candidate->getButtonText() == caption)
            return candidate;
    require(false, "Wrapper fixture exposes the named button");
    return nullptr;
}

static juce::ToggleButton *toggle(PluginWrapperViewComponent &view, const juce::String &caption)
{
    for (int i = 0; i < view.getNumChildComponents(); ++i)
        if (auto *candidate = dynamic_cast<juce::ToggleButton *>(view.getChildComponent(i));
            candidate && candidate->getButtonText() == caption)
            return candidate;
    require(false, "Wrapper fixture exposes the named toggle");
    return nullptr;
}

static juce::ComboBox *combo(PluginWrapperViewComponent &view, const juce::String &name)
{
    for (int i = 0; i < view.getNumChildComponents(); ++i)
        if (auto *candidate = dynamic_cast<juce::ComboBox *>(view.getChildComponent(i));
            candidate && candidate->getName() == name)
            return candidate;
    require(false, "Wrapper fixture exposes the exact named native-I/O selector");
    return nullptr;
}

static PluginBusCatalogue buses()
{
    PluginBusCatalogue catalogue;
    catalogue.inputCount = 2;
    catalogue.outputCount = 3;
    catalogue.entries[0] = {0, true, 2, 2, "Main input"};
    catalogue.entries[1] = {1, true, 1, 0, "Alternate mono input"};
    catalogue.entries[2] = {0, false, 2, 2, "Main output"};
    catalogue.entries[3] = {1, false, 2, 0, "Alternate output"};
    catalogue.entries[4] = {2, false, 6, 0, "Surround default output"};
    require(catalogue.valid(),
            "Original I/O catalogue fixture is actual-format metadata, not negotiation proof");
    return catalogue;
}

struct NativeProbe final : juce::Component
{
    explicit NativeProbe(int &count) : destroyed(count)
    {
        setSize(620, 300);
    }
    ~NativeProbe() override
    {
        ++destroyed;
    }
    int &destroyed;
};

static std::vector<PluginWrapperParameterView> catalogue()
{
    return {{0, 7, "Original gain", "linear", .5f, .25f, true, true},
            {19, 7, "Meter", "read only", .875f, 0.f, false, false}};
}

static void ownershipAndSizing()
{
    int destroyed = 0, changes = 0;
    auto native = std::make_unique<NativeProbe>(destroyed);
    juce::Component::SafePointer<NativeProbe> alive(native.get());
    {
        PluginWrapperViewComponent view(native.get(), [](const auto &) { return false; });
        view.onNativeSizeChanged = [&] { ++changes; };
        require(native->getParentComponent() == &view && native->isVisible() && destroyed == 0,
                "Native editor is borrowed into exactly the wrapper's component tree");
        const auto natural = native->getLocalBounds();
        require(native->getY() > 0 && native->getBottom() < view.getHeight(),
                "Native view reserves separate toolbar and feedback space");
        view.setSize(900, 700); // User-owned wrapper enlargement, not native resize.
        require(native->getLocalBounds() == natural && native->getX() >= 0 &&
                    native->getRight() <= view.getWidth() && native->getBottom() < view.getHeight(),
                "User wrapper enlargement retains fixed native dimensions and visible content");
        view.setView(PluginWrapperView::settings);
        require(!native->isVisible() && native->getParentComponent() == &view &&
                    native->getLocalBounds() == natural && view.getWidth() >= 560 &&
                    view.getHeight() >= 340,
                "Settings hides rather than reparents or destroys the native editor");
        native->setSize(720, 480); // A genuine native natural-size request while hidden.
        view.setView(PluginWrapperView::plugin);
        require(native->isVisible() && native->getWidth() == 720 && native->getHeight() == 480 &&
                    view.getWidth() >= 720 && native->getBottom() < view.getHeight(),
                "Returning to Plugin adopts current native natural size without shrinking it");
        const auto previousChanges = changes;
        native->setSize(800, 520);
        require(changes > previousChanges && native->getWidth() == 800 && native->getHeight() == 520 &&
                    view.getWidth() >= 800 && native->getBottom() < view.getHeight(),
                "Visible native resize propagates a wrapper size notification and remains contained");
        view.setView(PluginWrapperView::parameters);
        require(!native->isVisible() && view.getWidth() >= 560 && view.getHeight() >= 340,
                "Parameters supplies the controls minimum independently of native dimensions");
        const auto selectedView = view.selectedView();
        view.setView(static_cast<PluginWrapperView>(-1));
        require(view.selectedView() == selectedView, "Unknown view request preserves the selected tab");
    }
    require(alive && destroyed == 0 && native->getParentComponent() == nullptr,
            "Destroying wrapper detaches its borrowed editor without deleting it");
    native->setSize(640, 360); // Removed listener must not call a dead wrapper.
    native.reset();
    require(!alive && destroyed == 1, "Only original editor owner performs native destruction");
}

static void selectionAndIntents()
{
    std::vector<PluginWrapperIntent> intents;
    bool admit = true;
    PluginWrapperViewComponent view(nullptr,
                                    [&](const auto &intent)
                                    {
                                        intents.push_back(intent);
                                        return admit;
                                    });
    auto parameters = catalogue();
    PluginWrapperState state;
    state.ready = true;
    view.update(state, parameters, "Authoritative ready");
    view.setView(PluginWrapperView::parameters);
    auto *selector = visibleChild<juce::ComboBox>(view);
    auto *slider = visibleChild<juce::Slider>(view);
    auto *reset = button(view, "Reset");
    require(view.selectedParameterIdentity() == std::pair<std::uint32_t, std::uint32_t>{0, 7} &&
                slider->isEnabled() && reset->isEnabled(),
            "Native parameter ID zero is a real writable selection");
    const auto beforeSelection = view.selectedParameterIdentity();
    require(!view.selectParameter(0, 8) && !view.selectParameter(999, 7) &&
                view.selectedParameterIdentity() == beforeSelection && intents.empty(),
            "Stale generation and absent ID reject atomically without emitting commands");
    selector->setSelectedId(2, juce::sendNotificationSync);
    require(view.selectedParameterIdentity() == std::pair<std::uint32_t, std::uint32_t>{19, 7} &&
                !slider->isEnabled() && !reset->isEnabled(),
            "ComboBox uses exact shared selection path and respects read-only metadata");
    require(view.selectParameter(0, 7), "Semantic selection uses the same current native identity");
    slider->onDragStart();
    slider->setValue(.625, juce::sendNotificationSync);
    require(intents.size() == 2 && intents.front().action == PluginWrapperAction::parameter &&
                intents.front().parameter == 0 && intents.front().generation == 7 &&
                intents.front().gestureOpen && intents.back().value == .625f && intents.back().gestureOpen,
            "Parameter drag emits pinned native identity and actual normalized values");
    state.pending = true;
    parameters.front().value = .125f;
    view.update(state, parameters, "Pending actual result");
    require(slider->getValue() == .625 && slider->isEnabled() && !selector->isEnabled() &&
                !reset->isEnabled(),
            "Pending owned drag remains editable without snapping to asynchronous current value");
    const auto openCount = intents.size();
    require(!view.selectParameter(0, 99) && intents.size() == openCount &&
                view.selectedParameterIdentity() == beforeSelection,
            "Stale semantic selection cannot close or retarget an owned gesture");
    require(view.selectParameter(19, 7) && intents.size() == openCount + 1 && !intents.back().gestureOpen &&
                intents.back().parameter == 0 && intents.back().generation == 7 &&
                intents.back().value == .625f,
            "Valid selection closes the old actual target once before switching");
    view.finishGesture();
    slider->onDragEnd();
    require(intents.size() == openCount + 1, "Repeated gesture finish does not reopen or duplicate closure");
    state.pending = false;
    view.update(state, parameters, "Actual completed");
    require(view.selectParameter(0, 7), "Restore writable selection after completion");
    reset->onClick();
    require(intents.back().parameter == 0 && intents.back().generation == 7 && intents.back().value == .25f &&
                !intents.back().gestureOpen,
            "Reset submits the supplied native default through the shared application proxy");
    parameters.front().value = .25f;
    view.update(state, parameters, "Reset completion consumed");
    slider->onDragStart();
    const auto beforeTab = intents.size();
    button(view, "Settings")->onClick();
    require(view.selectedView() == PluginWrapperView::settings && intents.size() == beforeTab + 1 &&
                !intents.back().gestureOpen,
            "Tab action finishes the owned parameter gesture before hiding its controls");
    view.finishGesture();
    require(intents.size() == beforeTab + 1, "Hidden gesture remains closed");
    view.setView(PluginWrapperView::parameters);
    view.update(state, parameters, "Tab gesture completion consumed");
    admit = false;
    const auto beforeRefusal = intents.size();
    slider->onDragStart();
    slider->setValue(.75, juce::sendNotificationSync);
    slider->onDragEnd();
    require(intents.size() == beforeRefusal + 1,
            "Rejected gesture admission is not retried or reopened by later drag values");
}

static void pendingAndUnavailable()
{
    int submits = 0;
    PluginWrapperViewComponent view(nullptr,
                                    [&](const auto &)
                                    {
                                        ++submits;
                                        return true;
                                    });
    auto parameters = catalogue();
    PluginWrapperState state;
    view.update(state, parameters, "Not prepared");
    view.setView(PluginWrapperView::settings);
    auto *bypass = toggle(view, "Bypass");
    auto *mix = visibleChild<juce::Slider>(view);
    require(!bypass->isEnabled() && !mix->isEnabled(),
            "Unavailable host never advertises usable processing controls");
    state.ready = true;
    state.pending = true;
    view.update(state, parameters, "Native request pending");
    require(!bypass->isEnabled() && !mix->isEnabled(), "Pending operation disables independent host writes");
    bypass->onClick();
    mix->setValue(.375, juce::sendNotificationSync);
    require(submits == 0, "Even retained control callbacks cannot mutate during pending state");
    view.setView(PluginWrapperView::parameters);
    require(!visibleChild<juce::ComboBox>(view)->isEnabled() &&
                !visibleChild<juce::Slider>(view)->isEnabled() && !button(view, "Reset")->isEnabled(),
            "Pending idle parameter controls are disabled rather than claiming completion");
    state.pending = false;
    state.instrument = true;
    view.update(state, parameters, "Current instrument");
    view.setView(PluginWrapperView::settings);
    require(bypass->isEnabled() && !mix->isEnabled(),
            "Instrument bypass remains available but effect mix does not");
    state.instrument = false;
    state.bypassed = true;
    state.mix = .875f;
    view.update(state, parameters, "Current effect");
    require(bypass->getToggleState() && mix->isEnabled() && mix->getValue() == .875,
            "Host control readback comes from authoritative state rather than accepted intent");
    bypass->onClick();
    require(submits == 1 && bypass->getToggleState(),
            "Accepted bypass intent does not fabricate an already-applied checkbox value");
    view.setView(PluginWrapperView::parameters);
    view.update(state, {}, "Catalogue unavailable");
    require(!view.selectedParameterIdentity() && !visibleChild<juce::Slider>(view)->isEnabled(),
            "Empty catalogue exposes no fabricated parameter target");
}

static void independentControlAcknowledgement()
{
    PluginWrapperMailbox mailbox;
    PluginWrapperViewComponent view(nullptr, [&](const auto &intent) { return mailbox.submit(intent); });
    PluginWrapperState state;
    state.ready = true;
    view.update(state, catalogue(), "Ready");
    view.setView(PluginWrapperView::settings);
    auto *bypass = toggle(view, "Bypass");
    auto *mix = visibleChild<juce::Slider>(view);
    bypass->onClick();
    const auto firstSerial = mailbox.serial;
    require(firstSerial != 0 && mailbox.action == PluginWrapperAction::bypass && !bypass->isEnabled() &&
                !mix->isEnabled(),
            "Bypass admission immediately disables independent controls before asynchronous publication");
    mix->setValue(.37, juce::sendNotificationSync);
    require(mailbox.serial == firstSerial && mailbox.action == PluginWrapperAction::bypass,
            "Retained wet callback cannot consume a second action while bypass owns the mailbox");
    mailbox.phase.store(PluginWrapperPhase::terminal, std::memory_order_release);
    bypass->onClick();
    require(mailbox.serial == firstSerial && !mix->isEnabled(),
            "Application terminal ACK alone does not advertise another write before GUI consumption");
    // Same order as PluginEditorWindow::updateWrapper: consume terminal first,
    // then publish current authoritative state into the component.
    mailbox.phase.store(PluginWrapperPhase::idle, std::memory_order_release);
    state.bypassed = true;
    view.update(state, catalogue(), "Applied");
    require(mix->isEnabled() && bypass->isEnabled() && mix->getValue() == state.mix,
            "Consumed completion re-enables controls with authoritative values");
    mix->setValue(.37, juce::sendNotificationSync);
    require(mailbox.serial != firstSerial && mailbox.action == PluginWrapperAction::mix &&
                PluginWrapperMailbox::value(mailbox.desired.load()) == .37f && !bypass->isEnabled(),
            "Next wet edit is actually admitted once, then independently gated until its own ACK");
    mailbox.phase.store(PluginWrapperPhase::idle, std::memory_order_release);
    view.update(state, catalogue(), "Mix completion consumed");
    view.setView(PluginWrapperView::parameters);
    auto *parameter = visibleChild<juce::Slider>(view);
    parameter->onDragStart();
    parameter->setValue(.62, juce::sendNotificationSync);
    const auto parameterSerial = mailbox.serial;
    require(mailbox.action == PluginWrapperAction::parameter &&
                PluginWrapperMailbox::gestureOpen(mailbox.desired.load()) && parameter->isEnabled(),
            "Open native gesture remains editable and coalesces on its owned serial");
    parameter->onDragEnd();
    view.setView(PluginWrapperView::settings);
    bypass->onClick();
    require(mailbox.serial == parameterSerial && mailbox.action == PluginWrapperAction::parameter &&
                !PluginWrapperMailbox::gestureOpen(mailbox.desired.load()) && !bypass->isEnabled(),
            "Final native gesture gates subsequent independent writes until completion is consumed");
}

static void automationNotificationsAndLastTweaked()
{
    std::vector<PluginWrapperIntent> intents;
    bool accepted = true;
    PluginWrapperViewComponent view(nullptr,
                                    [&](const auto &intent)
                                    {
                                        intents.push_back(intent);
                                        return accepted;
                                    });
    auto parameters = catalogue();
    parameters.front().automatable = true;
    parameters[1].automatable = true; // Read-only still excludes mutation.
    parameters.push_back({23, 7, "Manual only", "linear", .375f, 0, true, false, false});
    PluginWrapperState state;
    state.ready = true;
    view.update(state, parameters, "Actual ready catalogue");
    view.setView(PluginWrapperView::parameters);
    auto *create = button(view, "Create automation clip");
    auto *initialize = button(view, "Initialize");
    auto *remove = button(view, "Remove init");
    auto *refresh = button(view, "Refresh parameters");
    auto *last = button(view, "Last tweaked");
    auto *notifications = toggle(view, "Native parameter notifications");
    require(create->isEnabled() && initialize->isEnabled() && remove->isEnabled() && !last->isEnabled(),
            "Writable automatable ID0 supports explicit actions; absent last tweak is unavailable");
    const auto exactAction = [&](PluginWrapperAction expected)
    {
        require(!intents.empty() && intents.back().action == expected && intents.back().parameter == 0 &&
                    intents.back().generation == 7 && intents.back().value == .5f &&
                    !intents.back().gestureOpen && pluginWrapperIntentValid(intents.back()),
                "Automation action carries exact native ID/generation/value without a scalar gesture");
    };
    create->onClick();
    exactAction(PluginWrapperAction::automationCreate);
    require(!create->isEnabled() && !initialize->isEnabled() && !remove->isEnabled() &&
                !refresh->isEnabled() && !visibleChild<juce::Slider>(view)->isEnabled(),
            "Accepted automation disables independent controls immediately without claiming completion");
    const auto pendingCount = intents.size();
    initialize->onClick();
    refresh->onClick();
    require(intents.size() == pendingCount, "Retained callbacks cannot bypass locally pending admission");
    state.pending = true;
    view.update(state, parameters, "Actual automation request pending");
    require(!create->isEnabled() && !refresh->isEnabled(),
            "Published pending state retains control ownership");
    state.pending = false;
    view.update(state, parameters, "Actual automation result");
    initialize->onClick();
    exactAction(PluginWrapperAction::initialize);
    view.update(state, parameters, "Actual initialization result");
    remove->onClick();
    exactAction(PluginWrapperAction::removeInitialization);
    view.update(state, parameters, "Actual remove result");
    require(view.selectParameter(23, 7) && visibleChild<juce::Slider>(view)->isEnabled() &&
                !create->isEnabled() && !initialize->isEnabled() && !remove->isEnabled(),
            "Writable manual-only parameter stays editable but cannot advertise automation");
    const auto beforeUnavailable = intents.size();
    create->onClick();
    initialize->onClick();
    remove->onClick();
    require(intents.size() == beforeUnavailable, "Nonautomatable retained callbacks refuse atomically");
    require(view.selectParameter(19, 7) && !create->isEnabled() && !initialize->isEnabled() &&
                !remove->isEnabled() && !visibleChild<juce::Slider>(view)->isEnabled(),
            "Automatable read-only metadata cannot enable native writes or initialization");
    state.lastTweakedSelection = (std::uint64_t(8) << 32);
    view.update(state, parameters, "Stale last tweak");
    const auto selectedBefore = view.selectedParameterIdentity();
    last->onClick();
    require(!last->isEnabled() && view.selectedParameterIdentity() == selectedBefore &&
                intents.size() == beforeUnavailable,
            "Stale last-tweak generation cannot retarget selection or emit a command");
    state.lastTweakedSelection = (std::uint64_t(7) << 32) | 999;
    view.update(state, parameters, "Absent last tweak");
    require(!last->isEnabled(), "Last tweak requires the actual native ID in this catalogue");
    state.lastTweakedSelection = (std::uint64_t(7) << 32); // Native ID0 is not the empty sentinel.
    view.update(state, parameters, "Actual zero-ID last tweak");
    require(last->isEnabled(), "Published generation with native ID0 enables Last tweaked");
    last->onClick();
    require(view.selectedParameterIdentity() == std::pair<std::uint32_t, std::uint32_t>{0, 7} &&
                view.selectedView() == PluginWrapperView::parameters && intents.size() == beforeUnavailable,
            "Last tweaked uses exact local selection in Parameters without mutating native state/history");
    parameters.front().generation = 0;
    view.update(state, parameters, "No valid catalogue generation");
    require(view.selectParameter(0, 0) && !create->isEnabled() && !initialize->isEnabled() &&
                !remove->isEnabled() && !last->isEnabled(),
            "Zero/obsolete catalogue generations never advertise automation or last-tweak targets");
    view.update(state, {}, "Empty catalogue");
    require(refresh->isEnabled() && !create->isEnabled(),
            "Refresh remains available for an empty current catalogue");
    refresh->onClick();
    require(intents.back().action == PluginWrapperAction::refresh && intents.back().parameter == 0 &&
                intents.back().generation == 0 && !intents.back().gestureOpen && !refresh->isEnabled() &&
                pluginWrapperIntentValid(intents.back()),
            "Refresh is a distinct pending metadata action without a fabricated native parameter");
    view.update(state, parameters, "Actual refresh failed; prior catalogue retained");
    view.setView(PluginWrapperView::settings);
    require(notifications->isEnabled() && notifications->getToggleState(),
            "Notifications initially reflects actual authoritative enabled state");
    notifications->setToggleState(false, juce::dontSendNotification);
    notifications->onClick();
    require(
        intents.back().action == PluginWrapperAction::notifications && intents.back().value == 0 &&
            !intents.back().gestureOpen && notifications->getToggleState() && !notifications->isEnabled() &&
            !visibleChild<juce::Slider>(view)->isEnabled(),
        "Notification intent rolls checkbox back and waits for actual state instead of optimistic success");
    view.update(state, parameters, "Actual notification request failed");
    require(notifications->getToggleState() && notifications->isEnabled(),
            "Failed notification readback preserves actual enabled state and permits another request");
    accepted = false;
    notifications->onClick();
    require(notifications->getToggleState() && notifications->isEnabled(),
            "Rejected notification admission does not change state or invent a pending owner");
    accepted = true;
    state.notifications = false;
    view.update(state, parameters, "Actual notifications disabled");
    require(!notifications->getToggleState(), "Only acknowledged readback changes the notification checkbox");
    notifications->onClick();
    require(intents.back().action == PluginWrapperAction::notifications && intents.back().value == 1 &&
                !notifications->getToggleState(),
            "Re-enable request remains a 0/1 intent until acknowledged");
    state.ready = false;
    view.update(state, parameters, "Worker unavailable");
    const auto unavailableCount = intents.size();
    notifications->onClick();
    refresh->onClick();
    create->onClick();
    last->onClick();
    require(intents.size() == unavailableCount && !notifications->isEnabled() && !refresh->isEnabled(),
            "Unavailable worker disables every new mutation and cannot emit retained actions");
}

static void editorlessNavigation()
{
    int intents = 0;
    PluginWrapperViewComponent view(nullptr,
                                    [&](const auto &)
                                    {
                                        ++intents;
                                        return true;
                                    });
    PluginWrapperState state;
    state.ready = true;
    view.update(state, catalogue(), "Actual editorless host controls");
    auto *nativeTab = button(view, "Plugin");
    require(!nativeTab->isEnabled(), "Editorless wrapper cannot advertise a usable native Plugin tab");
    bool explanation = false;
    for (int i = 0; i < view.getNumChildComponents(); ++i)
        if (const auto *label = dynamic_cast<juce::Label *>(view.getChildComponent(i));
            label && label->isVisible() && label->getText().contains("No native editor is available"))
            explanation = true;
    require(explanation, "Editorless initial view retains its truthful native-interface explanation");
    button(view, "Settings")->onClick();
    require(view.selectedView() == PluginWrapperView::settings &&
                toggle(view, "Native parameter notifications")->isVisible(),
            "Editorless Settings remains a real host control page");
    nativeTab->onClick();
    require(view.selectedView() == PluginWrapperView::settings,
            "Retained disabled Plugin callback cannot imply a native attachment");
    button(view, "Parameters")->onClick();
    require(view.selectedView() == PluginWrapperView::parameters &&
                visibleChild<juce::Slider>(view)->isEnabled() && intents == 0,
            "Editorless Parameters exposes actual catalogue controls without pretending native attachment");
}

static void nativeIoDraftAndAdmission()
{
    std::vector<PluginWrapperIntent> intents;
    bool accept = true;
    PluginWrapperViewComponent view(nullptr,
                                    [&](const auto &intent)
                                    {
                                        intents.push_back(intent);
                                        return accept;
                                    });
    PluginWrapperState state;
    state.ready = true;
    auto parameters = catalogue();
    auto routing = buses();
    const PluginMainIo original{0, 0, 2, 0};
    view.update(state, parameters, "Actual ready source");
    view.updateRouting(original, routing);
    view.setView(PluginWrapperView::settings);
    auto *input = combo(view, "Native input bus");
    auto *output = combo(view, "Native output bus");
    auto *mapping = combo(view, "Native input mapping");
    auto *apply = button(view, "Apply audio I/O");
    require(view.getWidth() >= 560 && view.getHeight() >= 480 && input->getNumItems() == 2 &&
                output->getNumItems() == 3 && input->getItemText(1).contains("1: Alternate mono input") &&
                output->getItemText(2).contains("2: Surround default output") &&
                input->getSelectedId() == 1 && output->getSelectedId() == 1 &&
                mapping->getSelectedId() == 1 && !apply->isEnabled(),
            "Settings shows real indexed bus names/default metadata, with no unchanged Apply or negotiation "
            "promise");
    input->setSelectedId(2, juce::sendNotificationSync);
    output->setSelectedId(2, juce::sendNotificationSync);
    mapping->setSelectedId(3, juce::sendNotificationSync);
    require(apply->isEnabled(), "Explicit changed valid native bus/mono-right draft enables Apply");
    view.updateRouting(original, routing);
    require(input->getSelectedId() == 2 && output->getSelectedId() == 2 && mapping->getSelectedId() == 3 &&
                apply->isEnabled() && intents.empty(),
            "Identical prepared readback preserves an unsubmitted draft and never submits implicitly");
    apply->onClick();
    const PluginMainIo monoRight{1, 1, 1, 2};
    require(intents.size() == 1 && intents.back().action == PluginWrapperAction::audioIo &&
                intents.back().io == monoRight && !intents.back().gestureOpen &&
                pluginWrapperIntentValid(intents.back()) && !apply->isEnabled() && !input->isEnabled() &&
                !output->isEnabled() && !mapping->isEnabled(),
            "Apply emits exact typed main-I/O intent, disables pending controls and never claims canonical "
            "completion");
    const auto submitted = intents.size();
    apply->onClick();
    output->setSelectedId(1, juce::sendNotificationSync);
    require(intents.size() == submitted && output->getSelectedId() == 2,
            "Pending retained Apply/selector callbacks cannot mutate or resubmit the owned I/O draft");
    view.updateRouting(original, routing);
    view.update(state, parameters, "Actual preparation refused; source still active");
    require(
        input->getSelectedId() == 2 && output->getSelectedId() == 2 && mapping->getSelectedId() == 3 &&
            apply->isEnabled(),
        "Failed unchanged authoritative I/O leaves the user's draft intact for explicit correction/retry");
    accept = false;
    apply->onClick();
    require(apply->isEnabled() && intents.back().io == monoRight,
            "Rejected Apply admission retains draft without creating an optimistic pending owner");
    accept = true;
    view.updateRouting(monoRight, routing);
    require(!apply->isEnabled() && input->getSelectedId() == 2 && mapping->getSelectedId() == 3,
            "Only actual authoritative I/O change resets the draft baseline and makes Apply unchanged");
    output->setSelectedId(1, juce::sendNotificationSync);
    auto renamed = routing;
    juce::String("Renamed alternate").copyToUTF8(renamed.entries[3].name, sizeof(renamed.entries[3].name));
    view.updateRouting(monoRight, renamed);
    require(output->getSelectedId() == 1 && output->getItemText(1).contains("Renamed alternate") &&
                apply->isEnabled(),
            "Catalogue-only refresh updates actual labels while preserving the same-I/O draft");
    view.updateRouting(original, routing);
    require(input->getSelectedId() == 1 && output->getSelectedId() == 1 && mapping->getSelectedId() == 1 &&
                !apply->isEnabled(),
            "Changed prepared I/O resets all selectors to its authoritative selection");
    const auto beforeMissing = intents.size();
    output->setSelectedId(0, juce::sendNotificationSync);
    apply->onClick();
    require(!apply->isEnabled() && intents.size() == beforeMissing,
            "Missing output selection refuses without inventing or clamping a bus");
    output->setSelectedId(2, juce::sendNotificationSync);
    mapping->setSelectedId(0, juce::sendNotificationSync);
    apply->onClick();
    require(!apply->isEnabled() && intents.size() == beforeMissing,
            "Missing input mapping cannot dispatch a partial I/O request");
    mapping->setSelectedId(2, juce::sendNotificationSync);
    apply->onClick();
    require(intents.back().io == PluginMainIo{0, 1, 1, 1},
            "Mono left dispatches the literal typed left mapping");
    view.update(state, parameters, "Actual left request drained");
    mapping->setSelectedId(4, juce::sendNotificationSync);
    apply->onClick();
    require(intents.back().io == PluginMainIo{0, 1, 1, 3},
            "Mono average uses the literal typed average mapping, independently of stereo and right");
    view.update(state, parameters, "Actual request drained");
    view.updateRouting(original, routing);
    view.setView(PluginWrapperView::parameters);
    auto *scalar = visibleChild<juce::Slider>(view);
    scalar->onDragStart();
    const auto duringGesture = intents.size();
    apply->onClick();
    require(intents.size() == duringGesture && !apply->isEnabled(),
            "An active scalar gesture cannot dispatch even a retained structural I/O callback");
    view.finishGesture();
    view.update(state, parameters, "Actual scalar closure");
    view.setView(PluginWrapperView::settings);
    state.pending = true;
    view.update(state, parameters, "Another actual request pending");
    const auto beforePending = intents.size();
    apply->onClick();
    require(!input->isEnabled() && !output->isEnabled() && !mapping->isEnabled() && !apply->isEnabled() &&
                intents.size() == beforePending,
            "Published pending state disables all structural I/O edits");
    state.pending = false;
    view.update(state, parameters, "Source available");
    auto invalidCatalogue = routing;
    invalidCatalogue.outputCount = 0;
    view.updateRouting(original, invalidCatalogue);
    apply->onClick();
    require(!input->isEnabled() && !output->isEnabled() && !apply->isEnabled() &&
                intents.size() == beforePending,
            "Invalid catalogue disables I/O instead of fabricating native choices");
    view.updateRouting({0, 99, 2, 0}, routing);
    apply->onClick();
    require(!apply->isEnabled() && intents.size() == beforePending,
            "Out-of-range authoritative layout never becomes a clamped valid Apply target");
    view.updateRouting(original, routing);
    view.setView(PluginWrapperView::parameters);
    require(view.getHeight() == 340, "Main-I/O Settings growth does not change Parameters preferred size");
}

static void noInputInstrumentRouting()
{
    std::vector<PluginWrapperIntent> intents;
    PluginWrapperViewComponent view(nullptr,
                                    [&](const auto &intent)
                                    {
                                        intents.push_back(intent);
                                        return true;
                                    });
    PluginWrapperState state;
    state.ready = true;
    state.instrument = true;
    PluginBusCatalogue routing;
    routing.outputCount = 2;
    routing.entries[0] = {0, false, 2, 2, "Instrument stereo"};
    routing.entries[1] = {1, false, 1, 0, "Instrument mono"};
    require(routing.valid(), "Original no-input instrument catalogue fixture is valid");
    view.update(state, catalogue(), "Actual no-input instrument");
    view.updateRouting({}, routing);
    view.setView(PluginWrapperView::settings);
    auto *input = combo(view, "Native input bus");
    auto *output = combo(view, "Native output bus");
    auto *mapping = combo(view, "Native input mapping");
    auto *apply = button(view, "Apply audio I/O");
    require(!input->isEnabled() && !mapping->isEnabled() && input->getNumItems() == 0 &&
                output->isEnabled() && !apply->isEnabled(),
            "No-input instrument exposes output buses without pretending an input or mapping exists");
    input->setSelectedId(2, juce::sendNotificationSync);
    mapping->setSelectedId(4, juce::sendNotificationSync);
    output->setSelectedId(2, juce::sendNotificationSync);
    apply->onClick();
    require(intents.size() == 1 && intents.back().action == PluginWrapperAction::audioIo &&
                intents.back().io == PluginMainIo{0, 1, 2, 0},
            "Output-only instrument Apply preserves original no-input contract fields exactly");
}

static void containingChainActions()
{
    struct ActionCase
    {
        const char *caption;
        PluginWrapperAction action;
    };
    const ActionCase cases[] = {{"Copy chain", PluginWrapperAction::chainCopy},
                                {"Save chain", PluginWrapperAction::chainSave},
                                {"Paste chain", PluginWrapperAction::chainPaste},
                                {"Load chain", PluginWrapperAction::chainLoad},
                                {"Chain directory", PluginWrapperAction::chainDirectory},
                                {"Detector routing", PluginWrapperAction::sidechain}};
    std::vector<PluginWrapperIntent> intents;
    bool accepted = true;
    PluginWrapperViewComponent view(nullptr,
                                    [&](const auto &intent)
                                    {
                                        intents.push_back(intent);
                                        return accepted;
                                    });
    PluginWrapperState state;
    state.ready = true;
    auto parameters = catalogue();
    view.update(state, parameters, "Actual effect source");
    view.updateRouting({}, buses());
    view.setView(PluginWrapperView::settings);
    auto *output = combo(view, "Native output bus");
    output->setSelectedId(2, juce::sendNotificationSync);
    require(view.getWidth() >= 560 && view.getHeight() >= 640 && button(view, "Apply audio I/O")->isEnabled(),
            "Containing-chain Settings reserves640px and starts with a genuine unsubmitted I/O draft");
    bool heading = false;
    for (int i = 0; i < view.getNumChildComponents(); ++i)
        if (const auto *label = dynamic_cast<juce::Label *>(view.getChildComponent(i));
            label && label->isVisible() && label->getText() == "Containing Mixer chain")
            heading = true;
    require(heading, "Settings names the containing Mixer chain instead of an unrelated selected insert");
    for (int index = 0; index < 6; ++index)
    {
        const auto &action = cases[index];
        auto *control = button(view, action.caption);
        require(control->isVisible() && control->isEnabled() && control->getHeight() >= 32 &&
                    control->getWidth() >= 140 && int(action.action) == 10 + index,
                "Each containing-chain action has its actual stable intent and practical visible target");
        const auto before = intents.size();
        control->onClick();
        require(intents.size() == before + 1 && intents.back().action == action.action &&
                    intents.back().parameter == 0 && intents.back().generation == 0 &&
                    intents.back().value == 0 && !intents.back().gestureOpen &&
                    intents.back().io == PluginMainIo{} && pluginWrapperIntentValid(intents.back()),
                "Containing chain/detector button submits exactly one typed intent without stale parameter "
                "or I/O payload");
        for (const auto &other : cases)
        {
            require(!button(view, other.caption)->isEnabled(),
                    "Accepted containing-chain work disables every independent chain/detector action");
            button(view, other.caption)->onClick();
        }
        require(intents.size() == before + 1 && !button(view, "Apply audio I/O")->isEnabled() &&
                    !output->isEnabled() && !toggle(view, "Native parameter notifications")->isEnabled(),
                "Retained callbacks cannot bypass pending ownership or optimistically mutate other controls");
        state.pending = true;
        view.update(state, parameters, "Actual application chain request pending");
        view.updateRouting({}, buses());
        require(!control->isEnabled(), "Published pending readback retains the actual chain operation lease");
        state.pending = false;
        view.update(state, parameters, "Actual application terminal result or refusal");
        require(control->isEnabled() && output->getSelectedId() == 2 &&
                    button(view, "Apply audio I/O")->isEnabled() && !toggle(view, "Bypass")->getToggleState(),
                "Actual terminal readback releases controls and preserves unrelated I/O draft and canonical "
                "bypass");
    }
    accepted = false;
    button(view, "Copy chain")->onClick();
    require(button(view, "Copy chain")->isEnabled() && output->getSelectedId() == 2,
            "Rejected containing-chain admission neither invents pending ownership nor resets I/O draft");
    accepted = true;
    state.instrument = true;
    view.update(state, parameters, "Actual instrument source has no containing Mixer effect chain");
    const auto instrumentCount = intents.size();
    for (const auto &action : cases)
    {
        require(!button(view, action.caption)->isEnabled(),
                "Every containing-effect-chain action is disabled for instruments");
        button(view, action.caption)->onClick();
    }
    require(intents.size() == instrumentCount,
            "Instrument retained callbacks never retarget a selected Mixer insert");
    state.instrument = false;
    state.ready = false;
    view.update(state, parameters, "Actual worker unavailable");
    for (const auto &action : cases)
    {
        require(!button(view, action.caption)->isEnabled(),
                "Unavailable effect source cannot advertise chain actions");
        button(view, action.caption)->onClick();
    }
    require(intents.size() == instrumentCount,
            "Unavailable-source callbacks produce no chain/detector intents");
    state.ready = true;
    view.update(state, parameters, "Actual effect ready");
    view.setView(PluginWrapperView::parameters);
    require(view.getHeight() == 340, "Containing-chain Settings growth leaves Parameters sizing unchanged");
    visibleChild<juce::Slider>(view)->onDragStart();
    const auto gestureCount = intents.size();
    for (const auto &action : cases)
    {
        require(!button(view, action.caption)->isEnabled(),
                "Active scalar gesture excludes independent chain actions");
        button(view, action.caption)->onClick();
    }
    require(intents.size() == gestureCount,
            "No structural or dialog intent can steal the owned scalar gesture");
    view.finishGesture();
    require(intents.size() == gestureCount + 1 && intents.back().action == PluginWrapperAction::parameter &&
                !intents.back().gestureOpen,
            "Original scalar gesture still closes exactly once after refused chain callbacks");
    view.update(state, parameters, "Actual scalar completion");
    view.setView(PluginWrapperView::settings);
    require(output->getSelectedId() == 2 && view.getHeight() == 640,
            "Tab/gesture actions retain local I/O draft and the complete Settings minimum");
}

static void controlsLayout()
{
    PluginWrapperViewComponent view(nullptr, [](const auto &) { return false; });
    PluginWrapperState state;
    state.ready = true;
    auto parameters = catalogue();
    parameters.front().automatable = true;
    view.update(state, parameters, "Actual component layout");
    for (const auto size : {juce::Point<int>{560, 340}, juce::Point<int>{720, 480}})
        for (const auto page : {PluginWrapperView::settings, PluginWrapperView::parameters})
        {
            view.setView(page);
            view.setSize(size.x, page == PluginWrapperView::settings ? size.y + 300 : size.y);
            for (int i = 0; i < view.getNumChildComponents(); ++i)
            {
                const auto *a = view.getChildComponent(i);
                if (!a->isVisible())
                    continue;
                require(a->getWidth() > 0 && a->getHeight() > 0 &&
                            view.getLocalBounds().contains(a->getBounds()),
                        "Every visible wrapper control fits the minimum and enlarged host page");
                for (int j = i + 1; j < view.getNumChildComponents(); ++j)
                {
                    const auto *b = view.getChildComponent(j);
                    if (b->isVisible())
                        require(!a->getBounds().intersects(b->getBounds()),
                                "Toolbar, feedback, settings and automation click targets never overlap");
                }
            }
            if (page == PluginWrapperView::settings)
            {
                for (const auto *name : {"Native input bus", "Native output bus", "Native input mapping"})
                    require(combo(view, name)->isVisible() && combo(view, name)->getHeight() >= 30 &&
                                combo(view, name)->getWidth() >= 200,
                            "Native-I/O choices retain practical visible click targets in Settings");
                require(button(view, "Apply audio I/O")->getHeight() >= 32,
                        "Explicit I/O Apply keeps a practical action target");
                for (const auto *name : {"Copy chain", "Paste chain", "Save chain", "Load chain",
                                         "Chain directory", "Detector routing"})
                    require(button(view, name)->isVisible() && button(view, name)->getHeight() >= 32 &&
                                button(view, name)->getWidth() >= 140,
                            "Containing-chain controls remain fully visible with practical click targets");
            }
            if (page == PluginWrapperView::parameters)
                for (const auto *name : {"Create automation clip", "Initialize", "Remove init",
                                         "Refresh parameters", "Last tweaked"})
                    require(button(view, name)->isVisible() && button(view, name)->getHeight() >= 32 &&
                                button(view, name)->getWidth() >= 100,
                            "New parameter actions retain practical visible click targets");
        }
}

} // namespace plugin_wrapper_view_acceptance

static void pluginWrapperViewTests()
{
    using Rectangle = juce::Rectangle<int>;
    require(vibera::pluginWrapperBoundsWithinWorkArea({300, 300, 562, 672}, {0, 0, 1280, 680}) ==
                Rectangle(300, 8, 562, 672),
            "Expanded Settings stays within the current1080p/150 logical work area");
    require(vibera::pluginWrapperBoundsWithinWorkArea({-1600, -50, 562, 672}, {-1920, -200, 1280, 880}) ==
                Rectangle(-1600, -50, 562, 672),
            "Visible wrapper preserves placement on left/above monitors");
    require(vibera::pluginWrapperBoundsWithinWorkArea({-2100, 100, 562, 672}, {-1920, -200, 1280, 880}) ==
                Rectangle(-1920, 8, 562, 672),
            "Off-edge host controls recover in negative monitor coordinates without changing size");
    require(vibera::pluginWrapperBoundsWithinWorkArea({400, 200, 2000, 1400}, {0, 0, 1280, 680}) ==
                Rectangle(0, 0, 2000, 1400),
            "Oversized vendor editor is positioned without silently stretching or scaling it");
    require(vibera::pluginWrapperBoundsWithinWorkArea({30, 40, 562, 672}, {}) == Rectangle(30, 40, 562, 672),
            "Unavailable display metadata never invents a monitor or changes wrapper bounds");
    plugin_wrapper_view_acceptance::ownershipAndSizing();
    {
        juce::Component native;
        native.setSize(620, 300);
        juce::ComponentBoundsConstrainer bounds;
        bounds.setSizeLimits(320, 200, 900, 700);
        vibera::PluginWrapperViewComponent view(&native, [](const auto &) { return false; }, &bounds, true);
        view.setSize(800, 600);
        require(native.getWidth() == 800 && native.getHeight() == 516 && native.getBottom() <= view.getHeight() - 40,
                "Supported host resize changes native bounds with toolbar/footer reserved");
        view.setSize(560, 284);
        require(native.getWidth() == 560 && native.getHeight() == 200,
                "Declared minimum host size respects native constraint rather than clipping");
        view.setView(vibera::PluginWrapperView::settings);
        view.setSize(700, 660); // Settings now reserves its complete chain/I/O controls height.
        require(native.getWidth() == 560 && native.getHeight() == 200,
                "Resizing host controls does not resize hidden native editor");
    }
    plugin_wrapper_view_acceptance::selectionAndIntents();
    plugin_wrapper_view_acceptance::pendingAndUnavailable();
    plugin_wrapper_view_acceptance::independentControlAcknowledgement();
    plugin_wrapper_view_acceptance::automationNotificationsAndLastTweaked();
    plugin_wrapper_view_acceptance::controlsLayout();
    plugin_wrapper_view_acceptance::editorlessNavigation();
    plugin_wrapper_view_acceptance::nativeIoDraftAndAdmission();
    plugin_wrapper_view_acceptance::noInputInstrumentRouting();
    plugin_wrapper_view_acceptance::containingChainActions();
}
