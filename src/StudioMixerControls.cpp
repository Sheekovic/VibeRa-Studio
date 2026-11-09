// SPDX-License-Identifier: AGPL-3.0-or-later
#include "NumberText.h"
#include "Studio.h"
#include "Editors.h"
#include "MixerControls.h"
#include "MixerInsertOrder.h"
#include "McpSchema.h"
#include "Settings.h"
#include "../tests/SyntheticDevice.h"
#include <charconv>
#include <algorithm>

namespace vibera
{
namespace
{
const char *const modulatorShapeLabels[] = {"Sine", "Triangle", "Saw up", "Saw down", "Square", "Random (steps)",
                                           "Random (smooth)"};
const double modulatorPeriods[] = {.25, .5, 1, 2, 4, 8, 16};
const char *const modulatorPeriodLabels[] = {"1/16 note", "1/8 note", "1 beat", "Half bar", "1 bar", "2 bars",
                                             "4 bars"};
const double modulatorDepths[] = {.1, .25, .5, .75, 1};
} // namespace
namespace
{
// Macro knobs window: a rotary per macro (release commits automation.macro.set), its targets
// listed below, plus Add. Rebuilt when the project revision changes.
class MacroKnobs final : public juce::Component, private juce::Timer
{
  public:
    explicit MacroKnobs(Studio &owner) : studio(&owner)
    {
        add.onClick = [this]
        {
            if (studio)
                studio->command("automation.macro.add", {});
        };
        addAndMakeVisible(add);
        addAndMakeVisible(hint);
        hint.setText("Link Mixer controls with right-click > Macro knob. Turn a knob to move all its targets.",
                     juce::dontSendNotification);
        rebuild();
        startTimerHz(10);
        setSize(640, 300);
    }

  private:
    juce::Component::SafePointer<Studio> studio;
    juce::TextButton add{"Add macro"};
    juce::Label hint;
    juce::OwnedArray<juce::Slider> knobs;
    juce::OwnedArray<juce::Label> names;
    std::vector<Id> ids;
    std::uint64_t revision = ~std::uint64_t(0);
    void rebuild()
    {
        if (!studio)
            return;
        revision = studio->controller.revision;
        knobs.clear();
        names.clear();
        ids.clear();
        for (const auto &macro : studio->controller.project.automation.macros)
        {
            auto *knob = knobs.add(new juce::Slider(juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow));
            knob->setRange(0, 1, .001);
            knob->setValue(macro.value, juce::dontSendNotification);
            knob->setName(juce::String::fromUTF8(macro.name.c_str()));
            const Id id = macro.id;
            knob->onDragEnd = [this, knob, id]
            {
                if (!studio)
                    return;
                CommandArgs a;
                a.target = id;
                a.value = knob->getValue();
                studio->command("automation.macro.set", a);
            };
            addAndMakeVisible(knob);
            auto *name = names.add(new juce::Label({}, juce::String::fromUTF8(macro.name.c_str()) + "\n" +
                                                         juce::String(int(macro.targets.size())) + " targets"));
            name->setJustificationType(juce::Justification::centred);
            addAndMakeVisible(name);
            ids.push_back(id);
        }
        resized();
    }
    void timerCallback() override
    {
        if (studio && studio->controller.revision != revision && !isMouseButtonDown(true))
            rebuild();
    }
    void resized() override
    {
        auto area = getLocalBounds().reduced(12);
        auto top = area.removeFromTop(28);
        add.setBounds(top.removeFromLeft(110));
        top.removeFromLeft(10);
        hint.setBounds(top);
        area.removeFromTop(10);
        for (int i = 0; i < knobs.size(); ++i)
        {
            auto cell = juce::Rectangle<int>(area.getX() + (i % 6) * 100, area.getY() + (i / 6) * 150, 96, 146);
            names[i]->setBounds(cell.removeFromBottom(36));
            knobs[i]->setBounds(cell);
        }
    }
};
} // namespace
void Studio::openMacroKnobs()
{
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(new MacroKnobs(*this));
    options.dialogTitle = "Macro knobs";
    options.dialogBackgroundColour = juce::Colour(0xff10191f);
    options.resizable = true;
    options.useNativeTitleBar = true;
    options.launchAsync();
}
juce::PopupMenu Studio::modulatorMenu(const AutomationModulator *m)
{
    juce::PopupMenu menu, rate, depth;
    menu.addSectionHeader(m ? "Shape" : "Add a modulator");
    for (int i = 0; i < 7; ++i)
        menu.addItem(modulatorMenuFirst + i, modulatorShapeLabels[i], true, m && int(m->shape) == i);
    for (int i = 0; i < 7; ++i)
        rate.addItem(modulatorMenuFirst + 20 + i, modulatorPeriodLabels[i], m != nullptr,
                     m && m->period == modulatorPeriods[i]);
    for (int i = 0; i < 5; ++i)
        depth.addItem(modulatorMenuFirst + 40 + i, juce::String(int(modulatorDepths[i] * 100)) + " %", m != nullptr,
                      m && m->depth == float(modulatorDepths[i]));
    menu.addSeparator();
    menu.addSubMenu("Rate (one cycle)", rate, m != nullptr);
    menu.addSubMenu("Depth", depth, m != nullptr);
    menu.addItem(modulatorMenuFirst + 60, "Enabled", m != nullptr, m && m->enabled);
    menu.addItem(modulatorMenuFirst + 61, "Remove modulator", m != nullptr);
    return menu;
}
void Studio::applyModulatorChoice(int choice, Id id, const juce::String &addCommand, CommandArgs a)
{
    const int item = choice - modulatorMenuFirst;
    const auto &list = controller.project.automation.modulators;
    const auto found = std::find_if(list.begin(), list.end(), [&](const auto &m) { return m.id == id; });
    if (id == 0 || found == list.end())
    {
        if (item < 0 || item >= 7)
            return;
        a.text = modulatorShapeName(ModulatorShape(item));
        a.value = 4;
        a.strength = .5;
        a.offset = 0;
        command(addCommand, a);
        return;
    }
    CommandArgs edit;
    edit.target = id;
    if (item == 60 || item == 61)
    {
        edit.enabled = !found->enabled;
        command(item == 60 ? "automation.modulator.enable" : "automation.modulator.delete", edit);
        return;
    }
    edit.text = modulatorShapeName(found->shape);
    edit.value = found->period;
    edit.strength = found->depth;
    edit.offset = found->phase;
    if (item >= 0 && item < 7)
        edit.text = modulatorShapeName(ModulatorShape(item));
    else if (item >= 20 && item < 27)
        edit.value = modulatorPeriods[item - 20];
    else if (item >= 40 && item < 45)
        edit.strength = modulatorDepths[item - 40];
    else
        return;
    command("automation.modulator.set", edit);
}
juce::Result Studio::admitMixerInsertOrder(juce::String &errorCode, const juce::String &owner)
{
    errorCode.clear();
    const bool ownRemote = mcpGestureDispatch && mcpGesture && mcpGesture->state == "pending" &&
        mcpGesture->domain == "document" && controller.gestureOwner() == mcpGesture->id;
    const bool ownToken = owner.isNotEmpty() && controller.gestureOwner() == owner;
    const bool ownMacro = customization.running && controller.hasGesture() && controller.gestureOwner().isEmpty();
    if ((controller.hasGesture() && !ownRemote && !ownToken && !ownMacro) || noteSlicePending() || noteStretchPending())
    {
        errorCode = "mixer.orderGestureOwned";
        return juce::Result::fail(errorCode + ": Finish/cancel the active edit before reordering inserts");
    }
    if (devicePreparationBusy() || routingApplicationPreparing || routingCoordinator.mutationBlocked() ||
        afterPluginState || pendingExport || audio.rendering() || midiFileBusy || pluginRecovery ||
        mcpExportOperation.isNotEmpty() || !audioImports.idle() || audioImportAfterTake || pollingAudioImports)
    {
        errorCode = "mixer.orderBusy";
        return juce::Result::fail(errorCode + ": Finish active device/routing/project work before reordering inserts");
    }
    // No source, placement or MIDI clock change. Do not finish/retry/cancel an
    // armed, active or retained take. Macro/remote owners remain explicit.
    return juce::Result::ok();
}
juce::Result Studio::admitMixerEffectMove(juce::String &errorCode) const
{
    errorCode.clear();
    auto refuse = [&](const char *code, const char *message)
    {
        errorCode = code;
        return juce::Result::fail(message);
    };
    const bool ownedDocumentGesture = mcpGestureDispatch && mcpGesture && mcpGesture->state == "pending" &&
                                      mcpGesture->domain == "document" &&
                                      controller.gestureOwner() == mcpGesture->id;
    if ((controller.hasGesture() && !ownedDocumentGesture) || noteSlicePending() || noteStretchPending())
        return refuse("gesture_owned", "Finish the active edit before reordering effects");
    if (midiTaking || hasPendingMidiTake())
        return refuse("take_pending",
                      "Finish, retry or cancel the captured MIDI take before reordering effects");
    if (recordArmed)
        return refuse("busy", "Disarm recording before reordering effects");
    if (afterPluginState || pendingExport || audio.rendering() || midiFileBusy || pluginRecovery ||
        mcpExportOperation.isNotEmpty())
        return refuse("busy",
                      "Finish the active file operation or plugin recovery before reordering effects");
    return juce::Result::ok();
}
juce::Result Studio::admitMixerChainControl(juce::String &errorCode, const juce::String &owner) const
{
    errorCode.clear();
    const bool ownedRemote = mcpGestureDispatch && mcpGesture && mcpGesture->state == "pending" &&
                             (mcpGesture->domain == "document" || mcpGesture->domain == "mixerEffects" ||
                              mcpGesture->domain == "mixerControl") &&
                             controller.gestureOwner() == mcpGesture->id;
    const bool ownedLocal = owner.isNotEmpty() && controller.gestureOwner() == owner;
    if ((controller.hasGesture() && !ownedLocal && !ownedRemote) || noteSlicePending() ||
        noteStretchPending())
    {
        errorCode = "gesture_owned";
        return juce::Result::fail("Finish the active edit before changing Mixer controls");
    }
    if (afterPluginState || pendingExport || audio.rendering() || midiFileBusy || pluginRecovery ||
        mcpExportOperation.isNotEmpty() ||
        std::any_of(pluginOperations.begin(), pluginOperations.end(), [](const auto &job)
                    { return job.state == "pending" && job.kind != "refresh" && job.kind != "editor"; }))
    {
        errorCode = "busy";
        return juce::Result::fail(
            "Finish active file, export or plugin control work before changing Mixer controls");
    }
    // Effect mix, chain override and stereo controls change no MIDI source or
    // placement. Keep armed/active/retained takes intact for UI and exact semantic leases.
    return juce::Result::ok();
}
juce::Result Studio::admitMixerAutomation(const juce::String &owner)
{
    const bool ownToken = owner.isNotEmpty() && controller.gestureOwner() == owner;
    const bool ownRemote = mcpGestureDispatch && mcpGesture && mcpGesture->state == "pending" &&
        mcpGesture->domain == "document" && controller.gestureOwner() == mcpGesture->id;
    const bool ownMacro = customization.running && controller.gestureOwner().isEmpty();
    if ((controller.hasGesture() && !ownToken && !ownRemote && !ownMacro) || noteSlicePending() || noteStretchPending())
        return juce::Result::fail("automation.gestureOwned: Finish/cancel the active edit first");
    if (devicePreparationBusy() || routingApplicationPreparing || routingCoordinator.mutationBlocked() ||
        afterPluginState || pendingExport || audio.rendering() || midiFileBusy || pluginRecovery ||
        mcpExportOperation.isNotEmpty() || !audioImports.idle() || audioImportAfterTake || pollingAudioImports)
        return juce::Result::fail("automation.busy: Finish active device/routing/file work first");
    // No source/clock change: armed, active and retained MIDI takes stay intact.
    return juce::Result::ok();
}
juce::Result Studio::selectAutomationSource(Id source)
{
    if (!controller.project.automationSource(source))
        return juce::Result::fail("automation.missingSource: Automation source is unavailable");
    if (auto result = admitMixerAutomation(); result.failed()) return result;
    auto *editor = static_cast<Editor *>(windows[2]->getContentComponent());
    auto *picker = editor ? editor->clipPicker() : nullptr;
    if (!picker) return juce::Result::fail("automation.unavailable: Playlist Picker is unavailable");
    editor->setPickerShown(true);
    picker->setKind(2); picker->setFilter({}); picker->setGroupFilter({});
    picker->selectIds({source}); // Existing canonical Picker navigation, also used by vibera_picker.
    showEditor(2); focus = Scope::playlist; refresh();
    return juce::Result::ok();
}
juce::Result Studio::performMixerAutomationIntent(const MixerAutomationUiIntent &intent,
                                                 MixerAutomationUiAction action, Id source)
{
    if (action != MixerAutomationUiAction::create && action != MixerAutomationUiAction::initialize &&
        action != MixerAutomationUiAction::removeInitialization && action != MixerAutomationUiAction::link &&
        action != MixerAutomationUiAction::select)
        return juce::Result::fail("automation.unsupportedAction: Unknown UI intent");
    MixerAutomationTarget target;
    if (auto valid = intent.validate(controller.project, controller.projectIdentity, controller.revision, target); valid.failed()) return valid;
    if (auto finished = finishRackEditsChecked(); finished.failed()) return finished;
    // Finishing another owned edit may notify/change the revision: validate again.
    if (auto valid = intent.validate(controller.project, controller.projectIdentity, controller.revision, target); valid.failed()) return valid;
    if (auto admitted = admitMixerAutomation(); admitted.failed()) return admitted;
    if (action == MixerAutomationUiAction::select)
    {
        const auto *item = controller.project.automationSource(source);
        if (!item || std::none_of(item->links.begin(), item->links.end(),
                                  [&](const auto &link) { return link.key == target.key; }))
            return juce::Result::fail("automation.missingLink: Source is not linked to this exact control");
        return selectAutomationSource(source);
    }
    CommandArgs args;
    args.mixer = intent.insert; args.automationControl = intent.control;
    args.automationSendTarget = intent.sendTarget;
    args.value = target.manual; args.target = source;
    args.length = std::max(4., controller.project.endBeat());
    for (const auto &clip : controller.project.placements) args.lane = std::max(args.lane, clip.lane + 1);
    for (const auto &clip : controller.project.audioPlacements) args.lane = std::max(args.lane, clip.lane + 1);
    for (const auto &clip : controller.project.automation.placements) args.lane = std::max(args.lane, clip.lane + 1);
    args.lane = std::min(args.lane, 127);
    const juce::String commandId = action == MixerAutomationUiAction::create ? "automation.create"
        : action == MixerAutomationUiAction::initialize ? "automation.init"
        : action == MixerAutomationUiAction::removeInitialization ? "automation.init.remove" : "automation.link";
    // Same Controller admission/validation/history as MCP, never duplicate mutations.
    const auto result = controller.execute(commandId, args);
    if (result.wasOk() && action == MixerAutomationUiAction::create &&
        controller.projectIdentity == intent.project && controller.revision == intent.revision + 1 &&
        !controller.project.automation.placements.empty())
    {
        selectedClips = {controller.project.automation.placements.back().id};
        selectAutomationSource(controller.project.automation.placements.back().source);
    }
    // Completion here is canonical model application, never audio-consumption acknowledgement.
    refresh();
    return result;
}
void Studio::mixerAutomationMenu(int insert, MixerAutomationControl control, std::optional<int> sendTarget)
{
    MixerAutomationTarget target;
    if (auto result = resolveMixerAutomationTarget(controller.project, insert, control, target, sendTarget);
        result.failed())
    { status = result.getErrorMessage(); return; }
    if (!automationRecordingActive())
        if (auto result = admitMixerAutomation(); result.failed())
        {
            status = result.getErrorMessage();
            return;
        }
    const MixerAutomationUiIntent intent{controller.projectIdentity, controller.revision, insert, control,
                                         sendTarget};
    juce::PopupMenu menu, linked, sources;
    menu.addSectionHeader(target.name);
    menu.addItem(4, "Edit value...");
    const auto recordingToken = automationTouch ? automationTouch->token : juce::String{};
    menu.addItem(5,
                 automationTouch ? "Keep MIDI automation take (Song)"
                                 : "Record MIDI automation for this insert (Song)",
                 !automationTouch || automationTouch->insert == insert, bool(automationTouch));
    if (automationTouch)
        menu.addItem(6, "Cancel MIDI automation take");
    menu.addSeparator();
    menu.addItem(1, "Create automation clip");
    menu.addItem(2, "Init song with this position");
    const bool initialized = std::any_of(controller.project.automation.initialValues.begin(),
        controller.project.automation.initialValues.end(), [&](const auto &item) { return item.key == target.key; });
    menu.addItem(3, "Delete initialized control", initialized);
    std::vector<Id> sourceIds;
    for (const auto &source : controller.project.automation.sources)
    {
        const auto offset = int(sourceIds.size()); sourceIds.push_back(source.id);
        const auto label = juce::String::fromUTF8(source.name.c_str()) + " [" + juce::String(source.id) + "]";
        sources.addItem(1000 + offset, label);
        if (std::any_of(source.links.begin(), source.links.end(), [&](const auto &link) { return link.key == target.key; }))
            linked.addItem(3000 + offset, label);
    }
    menu.addSubMenu("Link to existing source", sources, !sourceIds.empty());
    menu.addSubMenu("Select linked source in Playlist", linked, linked.getNumItems() > 0);
    const auto modulated = std::find_if(controller.project.automation.modulators.begin(),
                                        controller.project.automation.modulators.end(),
                                        [&](const auto &m) { return m.link.key == target.key; });
    const AutomationModulator *modulator =
        modulated == controller.project.automation.modulators.end() ? nullptr : &*modulated;
    menu.addSubMenu("Modulator (LFO)", modulatorMenu(modulator), !std::any_of(
        controller.project.automation.sources.begin(), controller.project.automation.sources.end(),
        [&](const auto &s) { return std::any_of(s.links.begin(), s.links.end(),
                                                [&](const auto &l) { return l.key == target.key; }); }));
    const Id modulatorId = modulator ? modulator->id : 0;
    {
        juce::PopupMenu macroMenu;
        const auto &macros = controller.project.automation.macros;
        for (size_t k = 0; k < macros.size() && k < 90; ++k)
            macroMenu.addItem(7200 + int(k), "Link to " + juce::String::fromUTF8(macros[k].name.c_str()));
        macroMenu.addItem(7299, "New macro knob and link");
        menu.addSubMenu("Macro knob", macroMenu);
    }
    menu.showMenuAsync(
        {},
        [safe = juce::Component::SafePointer<Studio>(this), intent, sourceIds, recordingToken, modulatorId](int choice)
        {
            if (!safe || choice == 0)
                return;
            if (choice >= 7200 && choice <= 7299)
            {
                CommandArgs a;
                a.mixer = intent.insert;
                a.automationControl = intent.control;
                a.automationSendTarget = intent.sendTarget;
                auto &macros = safe->controller.project.automation.macros;
                if (choice == 7299)
                {
                    safe->command("automation.macro.add", {});
                    if (macros.empty())
                        return;
                    a.ids = {macros.back().id};
                }
                else if (size_t(choice - 7200) < macros.size())
                    a.ids = {macros[size_t(choice - 7200)].id};
                else
                    return;
                safe->command("automation.macro.link", a);
                return;
            }
            if (choice >= modulatorMenuFirst && choice < modulatorMenuLast)
            {
                CommandArgs a;
                a.mixer = intent.insert;
                a.automationControl = intent.control;
                a.automationSendTarget = intent.sendTarget;
                safe->applyModulatorChoice(choice, modulatorId, "automation.modulator", a);
                return;
            }
            if ((choice == 5 || choice == 6) && recordingToken.isNotEmpty())
            {
                if (!safe->automationTouch || safe->automationTouch->token != recordingToken)
                {
                    safe->status = "automationRecording.staleToken: Menu belongs to another take";
                    return;
                }
                const auto result = safe->finishAutomationRecording(choice == 6);
                safe->status = result.wasOk() ? "Automation take resolved or sealing; inspect recording state"
                                              : result.getErrorMessage();
                safe->repaint();
                return;
            }
            MixerAutomationTarget current;
            if (auto valid = intent.validate(safe->controller.project, safe->controller.projectIdentity,
                                             safe->controller.revision, current);
                valid.failed())
            {
                safe->status = valid.getErrorMessage();
                return;
            }
            if (choice == 5)
            {
                const auto result = safe->startAutomationRecording(intent.insert, 0);
                safe->status = result.wasOk() ? "Song MIDI automation armed; play and move mapped Mixer "
                                                "controls, Stop to keep, Escape to cancel"
                                              : result.getErrorMessage();
                safe->repaint();
                return;
            }
            if (choice == 4)
            {
                safe->mixerValueDialog(intent.insert, intent.control, intent.sendTarget);
                return;
            }
            Id source = 0;
            auto action = choice == 1   ? MixerAutomationUiAction::create
                          : choice == 2 ? MixerAutomationUiAction::initialize
                                        : MixerAutomationUiAction::removeInitialization;
            if (choice >= 1000)
            {
                const bool select = choice >= 3000;
                const auto offset = choice - (select ? 3000 : 1000);
                if (offset < 0 || offset >= int(sourceIds.size()))
                    return;
                source = sourceIds[size_t(offset)];
                action = select ? MixerAutomationUiAction::select : MixerAutomationUiAction::link;
            }
            const auto result = safe->performMixerAutomationIntent(intent, action, source);
            safe->status = result.wasOk() ? "Ready" : result.getErrorMessage();
            safe->repaint();
        });
}
void Studio::automationSourceDialog(Id sourceId)
{
    const auto *source = controller.project.automationSource(sourceId);
    if (!source)
        return;
    if (auto admitted = admitMixerAutomation(); admitted.failed())
    {
        status = admitted.getErrorMessage();
        return;
    }
    struct SourceTargetChoice
    {
        AutomationKey key;
        juce::String name, units;
        double minimum = 0, maximum = 0;
        bool linked = false, available = true;
    };
    std::vector<SourceTargetChoice> choices;
    for (const auto &choice : mixerAutomationUiChoices(controller.project, *source))
        choices.push_back({choice.target.key, choice.target.name, choice.target.units, choice.minimum,
                           choice.maximum, choice.linked, true});
    // Existing sampler links only: retain their full key, including dormant roles.
    // Never reinterpret a Channel owner as a Mixer row or enumerate88*allChannels.
    for (const auto &link : source->links)
        if (link.key.kind == AutomationParameter::samplerModulation)
        {
            SamplerAutomationTarget target;
            if (auto resolved = resolveSamplerAutomationTarget(controller.project, link.key.owner,
                                                               link.key.parameter, target, true);
                resolved.failed())
            {
                status = resolved.getErrorMessage();
                return;
            }
            double minimum = 0, maximum = 0;
            if (!samplerAutomationFloatToPhysical(target.parameter, link.minimum, minimum) ||
                !samplerAutomationFloatToPhysical(target.parameter, link.maximum, maximum) ||
                !samplerAutomationValueValid(target.parameter, minimum) ||
                !samplerAutomationValueValid(target.parameter, maximum))
            {
                status = "automation.invalidRange: Existing sampler range is invalid";
                return;
            }
            choices.push_back({target.key, target.name,
                               juce::String(target.parameter.units.data(), target.parameter.units.size()),
                               minimum, maximum, true, target.available});
        }
    if (choices.empty())
        return;
    int selected = 0;
    for (size_t i = 0; i < choices.size(); ++i)
        if (!source->links.empty() && source->links.front().key == choices[i].key)
            selected = int(i);
    auto *dialog = new juce::AlertWindow("Automation source / targets",
                                         "Linked clips share points. Outputs use the selected target's "
                                         "physical units; reverse ranges are allowed. "
                                         "Unavailable sampler links allow name-only Apply or Remove link.",
                                         juce::MessageBoxIconType::NoIcon, this);
    dialog->setLookAndFeel(&look);
    dialog->addTextEditor("name", juce::String::fromUTF8(source->name.c_str()), "Source name");
    juce::StringArray labels;
    for (const auto &choice : choices)
        labels.add(choice.name + (choice.linked ? " (linked)" : "") +
                   (choice.available ? "" : " (sampler unavailable)"));
    dialog->addComboBox("target", labels, "Automation target");
    auto *combo = dialog->getComboBoxComponent("target");
    combo->setSelectedItemIndex(-1, juce::sendNotificationSync);
    combo->setSelectedItemIndex(selected, juce::dontSendNotification);
    dialog->addTextEditor("minimum", juce::String(choices[size_t(selected)].minimum, 6), "Output at source0");
    dialog->addTextEditor("maximum", juce::String(choices[size_t(selected)].maximum, 6), "Output at source1");
    dialog->addButton("Apply name only", 1);
    dialog->addButton("Remove link", 2);
    dialog->addButton("Cancel", 0);
    combo->onChange = [view = juce::Component::SafePointer<juce::AlertWindow>(dialog), choices]
    {
        if (!view)
            return;
        const auto index = view->getComboBoxComponent("target")->getSelectedItemIndex();
        if (index < 0 || index >= int(choices.size()))
            return;
        const auto &choice = choices[size_t(index)];
        auto *minimum = view->getTextEditor("minimum"), *maximum = view->getTextEditor("maximum");
        minimum->setText(juce::String(choice.minimum, 6));
        maximum->setText(juce::String(choice.maximum, 6));
        minimum->setTooltip(choice.units);
        maximum->setTooltip(choice.units);
        minimum->setEnabled(choice.available);
        maximum->setEnabled(choice.available);
        view->getButton(0)->setButtonText(choice.available ? "Apply / link" : "Apply name only");
    };
    combo->onChange(); // Initial dormant choice must already be read-only before display.
    const auto identity = controller.projectIdentity;
    const auto revision = controller.revision;
    dialog->enterModalState(
        true,
        juce::ModalCallbackFunction::create(
            [safe = juce::Component::SafePointer<Studio>(this),
             view = juce::Component::SafePointer<juce::AlertWindow>(dialog), choices, sourceId, identity,
             revision](int action)
            {
                if (!safe || !view || action == 0)
                    return;
                if (safe->controller.projectIdentity != identity || safe->controller.revision != revision ||
                    !safe->controller.project.automationSource(sourceId))
                {
                    safe->status = "automation.staleIntent: Reopen automation properties";
                    return;
                }
                const auto selected = view->getComboBoxComponent("target")->getSelectedItemIndex();
                if (selected < 0 || selected >= int(choices.size()))
                    return;
                const auto &choice = choices[size_t(selected)];
                const bool sampler = choice.key.kind == AutomationParameter::samplerModulation;
                CommandArgs target;
                target.target = sourceId;
                if (sampler)
                {
                    SamplerAutomationTarget current;
                    if (auto resolved = resolveSamplerAutomationTarget(
                            safe->controller.project, choice.key.owner, choice.key.parameter, current, true);
                        resolved.failed())
                    {
                        safe->status = resolved.getErrorMessage();
                        return;
                    }
                    if (current.available != choice.available)
                    {
                        safe->status = "automation.staleIntent: Sampler role changed; reopen properties";
                        return;
                    }
                    target.channel = choice.key.owner;
                    target.samplerAutomationParameter = choice.key.parameter;
                }
                else
                {
                    target.mixer = int(choice.key.owner);
                    target.automationControl = static_cast<MixerAutomationControl>(unsigned(choice.key.kind));
                    if (choice.key.kind == AutomationParameter::mixerSend)
                        target.automationSendTarget = int(choice.key.parameter);
                }
                CommandArgs rename;
                rename.target = sourceId;
                rename.text = view->getTextEditorContents("name");
                auto parse = [](juce::String text, double &value)
                {
                    auto bytes = text.trim().toStdString();
                    if (!bytes.empty() && bytes.front() == '+')
                        bytes.erase(bytes.begin());
                    const auto result = vibera::number_text::fromChars(bytes.data(), bytes.data() + bytes.size(), value);
                    return result.ec == std::errc{} && result.ptr == bytes.data() + bytes.size() &&
                           std::isfinite(value);
                };
                // Six-decimal display is not the saved value. Preserve the captured
                // exact endpoint independently for each unchanged text field.
                const auto minimumText = view->getTextEditorContents("minimum");
                const auto maximumText = view->getTextEditorContents("maximum");
                const bool minimumUnchanged = minimumText.trim() == juce::String(choice.minimum, 6);
                const bool maximumUnchanged = maximumText.trim() == juce::String(choice.maximum, 6);
                target.beat = choice.minimum;
                target.length = choice.maximum;
                const bool editTarget = action == 1 && choice.available &&
                                        (!choice.linked || !minimumUnchanged || !maximumUnchanged);
                if (editTarget && ((!minimumUnchanged && !parse(minimumText, target.beat)) ||
                                   (!maximumUnchanged && !parse(maximumText, target.length))))
                {
                    safe->status = "automation.invalidRange: Enter complete finite target outputs";
                    return;
                }
                if (auto admitted = safe->admitMixerAutomation(); admitted.failed())
                {
                    safe->status = admitted.getErrorMessage();
                    return;
                }
                const auto owner = "ui.automation.properties." + juce::Uuid().toString();
                auto result = juce::Result::ok();
                const juce::String prefix = sampler ? "automation.sampler." : "automation.";
                try
                {
                    if (!safe->controller.beginGesture(owner))
                    {
                        safe->status = "automation.gestureOwned: Cannot own properties transaction";
                        return;
                    }
                    result = action == 2
                                 ? safe->controller.execute(prefix + "unlink", target, owner)
                                 : safe->controller.execute("automation.source.rename", rename, owner);
                    if (editTarget && result.wasOk())
                        result = safe->controller.execute(prefix + "link", target, owner);
                    if (editTarget && result.wasOk())
                        result = safe->controller.execute(prefix + "range", target, owner);
                    if (result.wasOk())
                        result = safe->controller.endGestureChecked("Edit automation source / target", owner);
                }
                catch (...)
                {
                    result =
                        juce::Result::fail("automation.preparationFailed: Properties could not be prepared");
                }
                if (result.failed() && safe->controller.hasGesture() &&
                    safe->controller.gestureOwner() == owner)
                {
                    const auto cancelled = safe->controller.cancelGestureChecked(owner);
                    if (cancelled.failed())
                        result =
                            juce::Result::fail(result.getErrorMessage() + "; " + cancelled.getErrorMessage());
                }
                safe->status = result.wasOk() ? "Ready" : result.getErrorMessage();
                safe->refresh();
            }),
        true);
}

void Studio::mixerValueDialog(int index, MixerAutomationControl control, std::optional<int> sendTarget)
{
    if (control == MixerAutomationControl::send)
    {
        MixerAutomationTarget target;
        if (auto valid = resolveMixerAutomationTarget(controller.project, index, control, target, sendTarget);
            valid.failed())
        {
            status = valid.getErrorMessage();
            return;
        }
        if (auto admitted = admitMixerAutomation(); admitted.failed())
        {
            status = admitted.getErrorMessage();
            return;
        }
        // Existing numeric service captures this exact pair + project/revision,
        // shows percent0..200 and commits through canonical mixer.send.
        promptMixerSend(index, *sendTarget);
        return;
    }
    if (sendTarget)
    {
        status = "automation.invalidArguments: Send destination belongs only to send control";
        return;
    }
    if (control == MixerAutomationControl::stereoSeparation) { mixerStereoDialog(index); return; }
    const juce::String action = control == MixerAutomationControl::pan ? "mixer.pan" : "mixer.gain";
        const auto *insert = mixerInsert(controller.project, index);
        if (!insert)
            return;
        auto *dialog = new juce::AlertWindow(
            action == "mixer.pan" ? "Mixer pan" : "Mixer volume",
            action == "mixer.pan" ? "Stereo balance: -1 left,0 centre,+1 right" : "Linear gain:0..2;1=unity",
            juce::MessageBoxIconType::NoIcon, this);
        dialog->setLookAndFeel(&look);
        dialog->addTextEditor("amount", juce::String(action == "mixer.pan" ? insert->pan : insert->gain, 4),
                              "Value");
        dialog->addButton("Apply", 1, juce::KeyPress(juce::KeyPress::returnKey));
        dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        const auto identity = controller.projectIdentity;
        const auto revision = controller.revision;
        dialog->enterModalState(
            true,
            juce::ModalCallbackFunction::create(
                [safe = juce::Component::SafePointer<Studio>(this),
                 d = juce::Component::SafePointer<juce::AlertWindow>(dialog), action, identity, revision,
                 index](int result)
                {
                    if (!safe || !d || result != 1)
                        return;
                    if (safe->controller.projectIdentity != identity || safe->controller.revision != revision)
                    {
                        safe->status = "Mixer changed; value edit cancelled";
                        return;
                    }
                    const auto text = d->getTextEditorContents("amount").trim().toStdString();
                    double value = 0;
                    auto parsed = vibera::number_text::fromChars(text.data(), text.data() + text.size(), value);
                    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
                        !std::isfinite(value))
                    {
                        safe->status = "Enter a complete finite number";
                        return;
                    }
                    CommandArgs a;
                    a.mixer = index;
                    a.value = value;
                    safe->command(action, a);
                }),
            true);
        bringWindowToFront(*dialog);
        dialog->getTextEditor("amount")->grabKeyboardFocus();
        dialog->getTextEditor("amount")->selectAll();
}
void Studio::mixerNameDialog(int index)
{
    if (mixerSelection.ids.size() > 1 && mixerSelection.contains(index))
    { mixerOrganisationDialog("mixer.selection.rename"); return; }
    const auto *insert = mixerInsert(controller.project, index);
    if (!insert || controller.hasGesture())
    {
        status = "Finish the active edit before renaming";
        return;
    }
    auto *dialog =
        new juce::AlertWindow("Rename Mixer insert", "Names do not change routing or instrument assignments.",
                              juce::MessageBoxIconType::NoIcon, this);
    dialog->setLookAndFeel(&look);
    dialog->addTextEditor("name", insert->name, "Name");
    dialog->getTextEditor("name")->setInputRestrictions(64);
    dialog->addButton("Apply", 1, juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    const auto identity = controller.projectIdentity;
    const auto revision = controller.revision;
    dialog->enterModalState(true,
                            juce::ModalCallbackFunction::create(
                                [safe = juce::Component::SafePointer<Studio>(this),
                                 d = juce::Component::SafePointer<juce::AlertWindow>(dialog), identity,
                                 revision, index](int choice)
                                {
                                    if (!safe || !d || choice != 1)
                                        return;
                                    if (safe->controller.projectIdentity != identity ||
                                        safe->controller.revision != revision)
                                    {
                                        safe->status = "Mixer changed; rename cancelled";
                                        return;
                                    }
                                    CommandArgs a;
                                    a.mixer = index;
                                    a.text = d->getTextEditorContents("name");
                                    safe->command("mixer.rename", a);
                                }),
                            true);
    // F2 must be ready for typing, regardless of button traversal order.
    bringWindowToFront(*dialog);
    dialog->getTextEditor("name")->grabKeyboardFocus();
    dialog->getTextEditor("name")->selectAll();
}
void Studio::mixerStereoDialog(int index)
{
    juce::String code;
    if (auto result = admitMixerChainControl(code); result.failed())
    { status = result.getErrorMessage(); return; }
    const auto *insert = mixerInsert(controller.project, index);
    if (!insert) { status = "Mixer insert is unavailable"; return; }
    auto *dialog = new juce::AlertWindow("Mixer stereo separation",
        "-1 subtract opposite channels; 0 original stereo; +1 mono.",
        juce::MessageBoxIconType::NoIcon, this);
    dialog->setLookAndFeel(&look);
    dialog->addTextEditor("amount", juce::String(insert->stereoSeparation, 4), "Separation [-1..1]");
    dialog->addButton("Apply", 1, juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    const auto identity = controller.projectIdentity;
    const auto revision = controller.revision;
    dialog->enterModalState(true, juce::ModalCallbackFunction::create(
        [safe = juce::Component::SafePointer<Studio>(this),
         d = juce::Component::SafePointer<juce::AlertWindow>(dialog), identity, revision, index](int choice)
        {
            if (!safe || !d || choice != 1) return;
            if (safe->controller.projectIdentity != identity || safe->controller.revision != revision ||
                !mixerInsert(safe->controller.project, index))
            { safe->status = "Mixer changed; stereo value edit cancelled"; return; }
            auto text = d->getTextEditorContents("amount").trim().toStdString();
            if (!text.empty() && text.front() == '+') text.erase(text.begin());
            double value = 0;
            const auto parsed = vibera::number_text::fromChars(text.data(), text.data() + text.size(), value);
            if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !std::isfinite(value))
            { safe->status = "Enter a complete finite stereo separation number"; return; }
            CommandArgs args; args.mixer = index; args.value = value;
            safe->command("mixer.stereoSeparation", args); // Canonical range/admission/history, never clamp typed input.
        }), true);
    bringWindowToFront(*dialog);
    dialog->getTextEditor("amount")->grabKeyboardFocus();
    dialog->getTextEditor("amount")->selectAll();
}

void Studio::mixerControlMenu(int index)
{
    const auto *insert = mixerInsert(controller.project, index);
    if (!insert)
        return;
    juce::PopupMenu menu, colours;
    menu.addSectionHeader(insert->name);
    menu.addItem(1, "Rename... (F2 / middle-click)");
    menu.addItem(30, "Selection / display groups...");
    menu.addItem(2, "Mute", true, insert->muted);
    menu.addItem(3, "Solo (Ctrl-click enable light)", index != 0);
    menu.addItem(4, "Solo with connected inputs (Alt-click)", index != 0);
    menu.addItem(5, "Restore previous mute state", controller.project.mixerSolo.has_value());
    menu.addItem(15, "Unmute all tracks (Ctrl-right-click enable light)");
    colours.addItem(100, "Follow theme", true, insert->colour == 0);
    for (int i = 0; i < 8; ++i)
        colours.addColouredItem(101 + i, "Colour " + juce::String(i + 1), channelColour(i), true,
                                insert->colour == channelColour(i).getARGB());
    menu.addSubMenu("Colour", colours);
    menu.addSeparator();
    menu.addItem(6, "Centre pan");
    menu.addItem(7, "Unity volume (0 dB)");
    menu.addItem(8, "Clear sample overload indication");
    menu.addSeparator();
    menu.addItem(11, "Stereo separation...");
    juce::PopupMenu automation;
    automation.addItem(20, "Volume automation...");
    automation.addItem(21, "Pan automation...");
    automation.addItem(22, "Stereo separation automation...");
    menu.addSubMenu("Automation", automation);
    menu.addItem(12, "Original stereo (0%)");
    menu.addItem(13, "Invert polarity (both channels)", true, insert->phaseInverted);
    menu.addItem(14, "Swap left/right", true, insert->stereoSwapped);
    CommandArgs orderArgs; orderArgs.mixer = index;
    MixerInsertOrderPlan left, right;
    const bool canLeft = planMixerInsertOrder(controller.project, "mixer.moveLeft", orderArgs, left).wasOk() && !left.noOp();
    const bool canRight = planMixerInsertOrder(controller.project, "mixer.moveRight", orderArgs, right).wasOk() && !right.noOp();
    menu.addSeparator();
    menu.addItem(9, "Move left (Alt+Left / Shift-wheel name)", canLeft);
    menu.addItem(10, "Move right (Alt+Right / Shift-wheel name)", canRight);
    const auto orderIntent = MixerInsertOrderIntent::capture(controller.project, controller.projectIdentity, controller.revision, index);
    const auto identity = controller.projectIdentity;
    const auto revision = controller.revision;
    menu.showMenuAsync({},
                       [safe = juce::Component::SafePointer<Studio>(this), identity, revision, index, orderIntent](int choice)
                       {
                           if (!safe || !choice)
                               return;
                           if (safe->controller.projectIdentity != identity)
                           {
                               safe->status = "Project changed; Mixer menu cancelled";
                               return;
                           }
                           if (choice == 30)
                           {
                               if (safe->controller.revision != revision)
                               { safe->status = "mixer.group.staleIntent: Reopen Mixer menu"; return; }
                               safe->mixerOrganisationMenu(index); return;
                           }
                           if (choice >= 20 && choice <= 22)
                           {
                               if (safe->controller.revision != revision)
                               { safe->status = "automation.staleIntent: Reopen Mixer menu"; return; }
                               safe->mixerAutomationMenu(index, choice == 20 ? MixerAutomationControl::gain
                                   : choice == 21 ? MixerAutomationControl::pan : MixerAutomationControl::stereoSeparation);
                               return;
                           }
                           if (choice == 11) { safe->mixerStereoDialog(index); return; }
                           if (choice >= 12 && choice <= 14)
                           {
                               const auto *target = mixerInsert(safe->controller.project, index);
                               if (!target) { safe->status = "Mixer target is unavailable"; return; }
                               CommandArgs args; args.mixer = index; args.value = 0;
                               args.enabled = choice == 13 ? !target->phaseInverted : !target->stereoSwapped;
                               safe->command(choice == 12 ? "mixer.stereoSeparation"
                                             : choice == 13 ? "mixer.phaseInvert" : "mixer.stereoSwap", args);
                               return;
                           }
                           if (choice == 9 || choice == 10)
                           {
                               if (auto valid = orderIntent.validate(safe->controller.project, safe->controller.projectIdentity, safe->controller.revision); valid.failed())
                               { safe->status = valid.getErrorMessage(); return; }
                               CommandArgs args; args.mixer = index;
                               safe->command(choice == 9 ? "mixer.moveLeft" : "mixer.moveRight", args);
                               return;
                           }
                           if (choice == 1)
                           {
                               safe->mixerNameDialog(index);
                               return;
                           }
                           if (choice == 8)
                           {
                               std::uint64_t fence = 0;
                               safe->resetMixerOverload(index, fence);
                               return;
                           }
                           const auto *m = mixerInsert(safe->controller.project, index);
                           if (!m)
                               return;
                           CommandArgs a;
                           a.mixer = index;
                           a.enabled =
                               choice == 2
                                   ? !m->muted
                                   : !(safe->controller.project.mixerSolo &&
                                       safe->controller.project.mixerSolo->insert == index &&
                                       safe->controller.project.mixerSolo->connected == (choice == 4));
                           if (choice >= 100)
                           {
                               a.value = choice == 100 ? 0 : double(channelColour(choice - 101).getARGB());
                               safe->command("mixer.colour", a);
                               return;
                           }
                           a.value = choice == 7 ? 1 : 0;
                           safe->command(choice == 2    ? "mixer.mute"
                                         : choice == 3  ? "mixer.solo"
                                         : choice == 4  ? "mixer.soloConnected"
                                         : choice == 5  ? "mixer.unsolo"
                                         : choice == 15 ? "mixer.unmuteAll"
                                         : choice == 6  ? "mixer.pan"
                                                        : "mixer.gain",
                                         a);
                       });
}
bool Studio::performMixerControlAction(const juce::String &action)
{
    if (action == "mixer.unmuteAll")
    {
        command(action, {});
        return true;
    }
    if (action == "mixer.selectAll")
    {
        std::vector<int> ids;
        for (const auto &insert : controller.project.mixer) ids.push_back(insert.index);
        auto r = selectMixerMany(ids, "replace", selectedInsert);
        if (r.failed()) status = r.getErrorMessage(); return true;
    }
    if (action == "mixer.organisation.menu") { mixerOrganisationMenu(selectedInsert); return true; }
    if (action.startsWith("mixer.group."))
    {
        const auto group = mixerGroupFor(controller.project, selectedInsert);
        if (action == "mixer.group.create" || action == "mixer.group.rename")
        { mixerOrganisationDialog(action, group); return true; }
        if (action == "mixer.group.assign" || action == "mixer.group.colour")
        { mixerOrganisationMenu(selectedInsert); return true; }
        CommandArgs args; args.mixerGroup = group;
        if (action == "mixer.group.colourMembers")
            args.mixer = selectedInsert;
        args.enabled = !(controller.project.mixerSolo && controller.project.mixerSolo->group == group &&
            controller.project.mixerSolo->connected == (action == "mixer.group.soloConnected"));
        if (action == "mixer.group.mute")
        {
            const auto *g = mixerGroup(controller.project, group);
            args.enabled = g && !std::all_of(g->members.begin(), g->members.end(), [&](int id)
                { return mixerInsert(controller.project, id)->muted; });
        }
        command(action, args); return true;
    }
    if (action == "mixer.selection.rename") { mixerOrganisationDialog(action); return true; }
    if (action == "mixer.selection.colour") { mixerOrganisationMenu(selectedInsert); return true; }
    if (action == "mixer.moveLeft" || action == "mixer.moveRight")
    {
        CommandArgs args; args.mixer = selectedInsert;
        command(action, args);
        return true;
    }
    if (action == "mixer.effects.browse")
    {
        juce::String code;
        browseMixerEffects(selectedInsert, code);
        return true;
    }
    if (action == "mixer.meterReset")
    {
        std::uint64_t fence = 0;
        resetMixerOverload(selectedInsert, fence);
        return true;
    }
    if (action == "mixer.stereoSeparation")
    { mixerStereoDialog(selectedInsert); return true; }
    if (action == "mixer.phaseInvert" || action == "mixer.stereoSwap")
    {
        const auto *insert = mixerInsert(controller.project, selectedInsert);
        if (!insert) return true;
        CommandArgs args; args.mixer = selectedInsert;
        args.enabled = action == "mixer.phaseInvert" ? !insert->phaseInverted : !insert->stereoSwapped;
        command(action, args); return true;
    }
    if (action == "mixer.pan" || action == "mixer.gain")
    {
        mixerValueDialog(selectedInsert, action == "mixer.pan" ? MixerAutomationControl::pan : MixerAutomationControl::gain);
        return true;
    }
    if (action == "mixer.rename")
    {
        mixerNameDialog(selectedInsert);
        return true;
    }
    if (action == "mixer.colour")
    {
        mixerControlMenu(selectedInsert);
        return true;
    }
    if (action == "mixer.mute" || action == "mixer.solo" || action == "mixer.soloConnected" ||
        action == "mixer.unsolo")
    {
        const auto *m = mixerInsert(controller.project, selectedInsert);
        if (!m)
            return true;
        CommandArgs a;
        a.mixer = selectedInsert;
        a.enabled =
            action == "mixer.mute"
                ? !m->muted
                : !(controller.project.mixerSolo && controller.project.mixerSolo->insert == selectedInsert &&
                    controller.project.mixerSolo->connected == (action == "mixer.soloConnected"));
        command(action, a);
        return true;
    }
    return false;
}
juce::Result Studio::resetMixerOverload(int index, std::uint64_t &clearedThroughBlock)
{
    const auto result = mixerInsert(controller.project, index)
                            ? audio.clearMixerOverload(index, clearedThroughBlock)
                            : juce::Result::fail("Mixer insert is unavailable");
    status = result.wasOk() ? "Sample overload indication cleared; new overloads can re-latch"
                            : result.getErrorMessage();
    refresh(); // Transient meter state only: no history, dirty flag or audio graph update.
    return result;
}
juce::var Studio::mixerMeterState(int index) const
{
    const auto m = audio.mixerMeter(index);
    auto *o = new juce::DynamicObject;
    o->setProperty("available", m.available);
    o->setProperty("stale", m.stale);
    o->setProperty("ageMs", int(m.ageMs));
    o->setProperty("blocks", juce::int64(m.blocks));
    o->setProperty("clearedThroughBlock", juce::int64(m.clearedThroughBlock));
    o->setProperty("overloadPolicy",
                   "finite sample peak>=1 latches per side until clear or meter lifecycle reset; not true "
                   "peak or hardware clipping; clear excludes in-flight/new blocks");
    o->setProperty("tap", "post-effects/pan/fader/mute, before outgoing sends; Master after its fader");
    o->setProperty("units",
                   "linear amplitude; dBFS=20*log10(amplitude); display peak has300ms exponential release");
    for (int ch = 0; ch < 2; ++ch)
    {
        const auto prefix = ch == 0 ? juce::String("left") : juce::String("right");
        o->setProperty(juce::Identifier(prefix + "Peak"), m.peak[ch]);
        o->setProperty(juce::Identifier(prefix + "Rms"), m.rms[ch]);
        o->setProperty(juce::Identifier(prefix + "Display"), m.display[ch]);
        o->setProperty(juce::Identifier(prefix + "Overload"), m.overload[ch]);
        o->setProperty(juce::Identifier(prefix + "LastClippedBlock"), juce::int64(m.lastClippedBlock[ch]));
    }
    return juce::var(o);
}
juce::Result Studio::verifyMixerControls(const juce::File &output)
{
    if (auto r = output.createDirectory(); r.failed())
        return r;
    auto fail = [&](const juce::String &s)
    {
        output.getChildFile("failure.txt").replaceWithText(s);
        return juce::Result::fail(s);
    };
    stopTimer();
    controller.replace(Project::demo());
    setSize(1920, 1044);
    refresh();
    selectMixerInsert(1);
    MacroPage macro(*this);
    if (auto result = macro.verifyMixerControlBuilder(); result.failed())
        return fail(result.getErrorMessage());
    auto *editor = static_cast<Editor *>(windows[3]->getContentComponent());
    auto origin = editor->mixerPanBounds(1).getCentre().toFloat();
    auto event = [&](juce::Point<float> p, int flags = juce::ModifierKeys::leftButtonModifier)
    {
        auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), p,
                                juce::ModifierKeys(flags), 1, 0, 0, 0, 0, editor, editor, now, origin, now, 1,
                                true);
    };
    const auto baseline = controller.project.state();
    editor->mouseDown(event(origin));
    editor->mouseDrag(event(origin.translated(25, 0)));
    editor->mouseUp(event(origin.translated(25, 0)));
    if (std::abs(mixerInsert(controller.project, 1)->pan - .25f) > .0001)
        return fail("Actual pan drag");
    controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(baseline))
        return fail("One pan Undo");
    controller.redoEdit();
    editor->mouseDoubleClick(event(origin));
    if (mixerInsert(controller.project, 1)->pan != 0)
        return fail("Pan centre reset");
    editor->mouseDown(event(origin));
    editor->mouseDrag(event(origin.translated(50, 0),
                            juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier));
    if (std::abs(mixerInsert(controller.project, 1)->pan - .05f) > .0001)
        return fail("Fine pan drag");
    editor->cancelPointerGesture();
    if (mixerInsert(controller.project, 1)->pan != 0)
        return fail("Pan cancellation");
    juce::MouseWheelDetails wheel{};
    wheel.deltaY = .5f;
    editor->mouseWheelMove(event(origin), wheel);
    if (std::abs(mixerInsert(controller.project, 1)->pan - .1f) > .0001)
        return fail("Pan wheel");
    auto precise = [&](const juce::String &text, int choice)
    {
        performAction("mixer.pan");
        auto *d = dynamic_cast<juce::AlertWindow *>(juce::Component::getCurrentlyModalComponent());
        if (!d || !d->getTextEditor("amount"))
            return false;
        d->getTextEditor("amount")->setText(text);
        d->exitModalState(choice);
        dispatchMessages(20);
        return true;
    };
    const auto beforePrecise = controller.project.state();
    if (!precise(".5junk", 1) || !controller.project.state().isEquivalentTo(beforePrecise))
        return fail("Strict precise pan entry");
    if (!precise("-.75", 0) || !controller.project.state().isEquivalentTo(beforePrecise))
        return fail("Precise pan cancel");
    if (!precise("-.5", 1) || mixerInsert(controller.project, 1)->pan != -.5f)
        return fail("Functional precise pan entry");
    controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(beforePrecise))
        return fail("Precise pan Undo");
    origin = editor->mixerMuteBounds(1).getCentre().toFloat();
    editor->mouseDown(event(origin));
    if (!mixerInsert(controller.project, 1)->muted)
        return fail("Enable light mute");
    editor->mouseDown(event(origin));
    editor->mouseDown(
        event(origin, juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::ctrlModifier));
    if (!controller.project.mixerSolo || controller.project.mixerSolo->connected)
        return fail("Ctrl solo");
    editor->mouseDown(
        event(origin, juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier));
    if (!controller.project.mixerSolo || !controller.project.mixerSolo->connected)
        return fail("Alt connected solo");
    CommandArgs a;
    a.mixer = 1;
    command("mixer.unsolo", a);
    const auto beforeGlobal = controller.project.state();
    const auto selectedBeforeGlobal = selectedInsert;
    const auto selectionBeforeGlobal = mixerSelection;
    a.enabled = true;
    for (const int index : {0, 2, 7})
    {
        a.mixer = index;
        command("mixer.mute", a);
    }
    const auto mutedBeforeGlobal = controller.project.state();
    origin = editor->mixerMuteBounds(2).getCentre().toFloat();
    editor->mouseDown(
        event(origin, juce::ModifierKeys::rightButtonModifier | juce::ModifierKeys::ctrlModifier));
    if (controller.project.mixerSolo || selectedInsert != selectedBeforeGlobal ||
        mixerSelection.project != selectionBeforeGlobal.project ||
        mixerSelection.ids != selectionBeforeGlobal.ids ||
        mixerSelection.active != selectionBeforeGlobal.active ||
        mixerSelection.anchor != selectionBeforeGlobal.anchor ||
        std::any_of(controller.project.mixer.begin(), controller.project.mixer.end(),
                    [](const auto &insert) { return insert.muted; }))
        return fail("Ctrl-right enable light globally unmutes without changing selection");
    controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(mutedBeforeGlobal))
        return fail("Ctrl-right Unmute All exact Undo");
    controller.redoEdit();
    if (!controller.project.state().isEquivalentTo(beforeGlobal))
        return fail("Ctrl-right Unmute All exact Redo");
    a.mixer = 1;
    origin = editor->mixerNameBounds(1).getCentre().toFloat();
    editor->mouseDown(event(origin, juce::ModifierKeys::middleButtonModifier));
    auto *dialog = dynamic_cast<juce::AlertWindow *>(juce::Component::getCurrentlyModalComponent());
    if (!dialog || !dialog->getTextEditor("name"))
        return fail("Middle-click rename dialog");
    dialog->getTextEditor("name")->setText("DRUMS");
    dialog->exitModalState(1);
    dispatchMessages(20);
    if (mixerInsert(controller.project, 1)->name != "DRUMS")
        return fail("Functional rename");
    a.text = "Drum bus";
    a.mixer = 5;
    command("mixer.rename", a);
    a.value = double(channelColour(4).getARGB());
    command("mixer.colour", a);
    for (int source : {1, 2, 3})
    {
        a.mixer = source;
        a.target = 5;
        command("mixer.routeOnly", a);
    }
    a.mixer = 4;
    a.text = "Bass";
    command("mixer.rename", a);
    controller.project.name = "Named buses / pan / mute / stereo meters";
    refresh();
    auto &manager = audio.engine.getDeviceManager();
    manager.deviceManager.addAudioDeviceType(std::make_unique<SyntheticDeviceType>());
    juce::XmlElement deviceSetup("DEVICESETUP");
    deviceSetup.setAttribute("deviceType", "VibeRa synthetic");
    deviceSetup.setAttribute("audioOutputDeviceName", "In-memory output");
    deviceSetup.setAttribute("audioDeviceRate", 48000.);
    deviceSetup.setAttribute("audioDeviceBufferSize", 256);
    audio.engine.getPropertyStorage().setXmlProperty(te::SettingID::audio_device_setup, deviceSetup);
    manager.initialise(0, 2);
    dispatchMessages(20);
    auto *device = dynamic_cast<SyntheticDevice *>(manager.deviceManager.getCurrentAudioDevice());
    if (!device || audio.rebuild(controller.project, true, selectedPattern).failed())
        return fail("Prepare actual virtual audio for rendered meters");
    needsRebuild = false;
    engineRevision = controller.revision;
    audio.setLooping(true);
    audio.playStop();
    dispatchMessages(20);
    juce::AudioBuffer<float> block(2, 256);
    for (int i = 0; i < 64; ++i)
    {
        device->process(block);
        dispatchMessages(1);
    }
    if (!audio.mixerMeter(0).available || audio.mixerMeter(0).display[0] <= 0)
        return fail("Actual graph meter before capture");
    auto capture = [&](const juce::String &name, float scale)
    {
        for (int i = 0; i < 8; ++i)
        {
            device->process(block);
            dispatchMessages(1);
        }
        auto image = createComponentSnapshot(getLocalBounds(), true, scale);
        auto stream = output.getChildFile(name).createOutputStream();
        if (!stream)
            return false;
        stream->setPosition(0);
        stream->truncate();
        return juce::PNGImageFormat().writeImageToStream(image, *stream);
    };
    const auto beforeOverload = controller.project.state();
    a.channel = controller.project.channels.front().id;
    a.value = 2;
    command("channel.gain", a);
    a.mixer = 1;
    command("mixer.gain", a);
    a.mixer = 0;
    command("mixer.gain", a);
    auto publishAndTick = [&]
    {
        if (audio.rebuild(controller.project, true, selectedPattern).failed())
            return false;
        needsRebuild = false;
        engineRevision = controller.revision;
        for (int i = 0; i < 128; ++i)
        {
            device->process(block);
            dispatchMessages(1);
        }
        return true;
    };
    if (!publishAndTick() || !audio.mixerMeter(1).overload[0])
        return fail("Actual overloaded drum PCM latches before semantic clear");
    if (!capture("named-mixer-overload-1080p.png", 1))
        return fail("Actual held overload capture");
    const auto resetBaseline = controller.project.state();
    const auto resetRevision = controller.revision;
    const bool resetDirty = controller.dirty;
    const auto undoName = controller.undo.getUndoDescription(),
               redoName = controller.undo.getRedoDescription();
    auto args = juce::JSON::parse(R"({"action":"resetOverload","insert":1})");
    const auto reset = mcpCall("vibera_mixer", args);
    const auto data = reset["structuredContent"];
    if (bool(reset["isError"]) || data["completionState"].toString() != "completed" ||
        bool(data["undoable"]) || juce::int64(data["clearedThroughBlock"]) <= 0 ||
        bool(data["meter"]["leftOverload"]) || bool(data["meter"]["rightOverload"]))
        return fail("Semantic clear acknowledges actual completed-block fence and result");
    if (!publishAndTick() || !audio.mixerMeter(1).overload[0])
        return fail("New actual overload re-latches after semantic clear");
    origin = editor->mixerMeterBounds(1).getCentre().toFloat();
    editor->mouseDown(event(origin));
    editor->mouseUp(event(origin));
    if (audio.mixerMeter(1).overload[0] || controller.revision != resetRevision ||
        controller.dirty != resetDirty || controller.undo.getUndoDescription() != undoName ||
        controller.undo.getRedoDescription() != redoName ||
        !controller.project.state().isEquivalentTo(resetBaseline))
        return fail("Actual meter click shares clear behavior without gain/history/project changes");
    args.getDynamicObject()->setProperty("insert", 999);
    bool refusedMissing = false;
    try
    {
        mcpCall("vibera_mixer", args);
    }
    catch (const McpFailure &error)
    {
        refusedMissing = error.code == "unavailable_target";
    }
    if (!refusedMissing || controller.revision != resetRevision)
        return fail("Invalid semantic overload target refuses without history change");
    controller.undoEdit();
    controller.undoEdit();
    controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(beforeOverload) || !publishAndTick())
        return fail("Only original gain transactions enter history; overload is transient");
    for (const auto &insert : controller.project.mixer)
    {
        std::uint64_t fence = 0;
        if (resetMixerOverload(insert.index, fence).failed())
            return fail("Restore showcase meter indications after explicit overload tests");
    }
    // Shared geometry is checked at compact, maximised1080p and4K150 logical layouts.
    auto verifyFader = [&](bool capped)
    {
        // Revised readable strip design uses a280px ceiling; retain independent
        // endpoint/history/cancel/isolation checks, not an implementation-derived expectation.
        constexpr int expectedMaximumTravel = 280;
        const auto fader = editor->mixerFaderBounds(1);
        if (fader.getHeight() != editor->mixerFaderTravel() || fader.getHeight() > expectedMaximumTravel ||
            (capped && fader.getHeight() != expectedMaximumTravel) || std::abs(editor->mixerMeterY(0) - 100.f) > .001f ||
            std::abs(editor->mixerMeterY(-60) - float(fader.getBottom())) > .001f ||
            std::abs(editor->mixerGainY(0) - float(fader.getBottom())) > .001f ||
            std::abs(editor->mixerGainY(2) - 100.f) > .001f ||
            std::abs(editor->mixerGainAtY(editor->mixerGainY(1)) - 1.) > .0001)
            return false;
        const auto before = controller.project.state();
        origin = {float(fader.getCentreX()), editor->mixerGainY(.5f)};
        editor->mouseDown(event(origin));
        editor->mouseUp(event(origin));
        if (std::abs(mixerInsert(controller.project, 1)->gain - .5f) > .0001)
            return false;
        controller.undoEdit();
        if (!controller.project.state().isEquivalentTo(before))
            return false;
        origin.y = editor->mixerGainY(1.5f);
        editor->mouseDown(event(origin));
        editor->cancelPointerGesture();
        if (!controller.project.state().isEquivalentTo(before))
            return false;
        // Readouts/unused space below a capped fader must not change gain or start a gesture.
        if (capped)
        {
            origin.y = float(fader.getBottom() + 30);
            editor->mouseDown(event(origin));
            editor->mouseUp(event(origin));
            if (controller.hasGesture() || !controller.project.state().isEquivalentTo(before))
                return false;
        }
        return true;
    };
    if (!verifyFader(false))
        return fail("Compact fader mapping/history/cancel and actual signal scale");
    if (!capture("named-mixer-workspace-1080p.png", 1))
        return fail("1080p capture");
    maximiseEditor(3);
    refresh();
    if (!verifyFader(true))
        return fail("Maximised1080p fader cap, shared mapping/history/cancel and readout isolation");
    if (!capture("named-mixer-1080p.png", 1))
        return fail("Mixer capture");
    setSize(2560, 1392);
    refresh();
    if (!verifyFader(true))
        return fail("4K150 logical fader cap, shared mapping/history/cancel and readout isolation");
    if (!capture("named-mixer-4k-150.png", 1.5f))
        return fail("4K150 capture");
    if (saveProject(controller.project, output.getChildFile("named-buses-showcase.vibra")).failed())
        return fail("Save showcase");
    output.getChildFile("mixer-controls-gui.txt")
        .replaceWithText("PASS actual component pan/fine/wheel/cancel/Undo/Redo/centre, enable light/solo "
                         "modifiers, functional middle-click rename and populated1080p/4K150 renders. Not "
                         "native input, live PCM or listening.\n");
    return juce::Result::ok();
}
} // namespace vibera

#include "../tests/MixerEffectSlotsAcceptance.inc"
#include "../tests/MixerInsertOrderUiAcceptance.inc"

#include "../tests/MixerStereoUiAcceptance.inc"

#include "../tests/MixerAutomationUiAcceptance.inc"
