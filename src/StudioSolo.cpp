// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Studio.h"
#include "Editors.h"
#include "RackPage.h"
#include "MacroConfiguration.h"
#include "ChannelColourPicker.h"
#include "../tests/SyntheticDevice.h"
namespace vibera
{
juce::Result Studio::setStepMemoryPreference(bool enabled)
{
    if (customization.running || controller.hasGesture())
        return juce::Result::fail(
            "stepMemory.busy: Finish the active macro or document gesture before changing step retention");
    try
    {
        const auto file = profileFile();
        MacroConfigurationSnapshot before;
        if (auto result = readMacroConfiguration(customization, file, before); result.failed())
            return result;
        if (before.exists && !before.matches)
            return juce::Result::fail("stepMemory.profileChanged: Saved shortcuts/macros changed; reconcile "
                                      "them in Settings before saving");
        if (customization.muteRemovedSteps == enabled && (before.exists || !enabled))
            return juce::Result::ok();
        auto next = customization;
        next.muteRemovedSteps = enabled;
        if (juce::JSON::toString(next.state()).getNumBytesAsUTF8() > 1024 * 1024)
            return juce::Result::fail(
                "stepMemory.profileLimit: Saved settings must remain loadable within1MiB");
        MacroConfigurationSnapshot immediatelyBeforeWrite;
        if (auto result = readMacroConfiguration(customization, file, immediatelyBeforeWrite);
            result.failed())
            return result;
        if (immediatelyBeforeWrite.generation != before.generation)
            return juce::Result::fail("stepMemory.profileChanged: Saved settings changed during preparation");
        if (const auto saved = next.save(file); saved.failed())
            return saved;
        // The intrinsic memory commit is one non-throwing bool assignment after
        // atomic persistence. No Project, allocator, history or native state edit.
        customization.muteRemovedSteps = enabled;
    }
    catch (...)
    {
        return juce::Result::fail("stepMemory.preparationFailed: Preference preparation did not complete");
    }
    refresh();
    return juce::Result::ok();
}
juce::Result Studio::setSoloRestorePreference(bool enabled)
{
    if (customization.running)
        return juce::Result::fail("Finish the active macro before changing workflow preferences");
    auto next = customization;
    next.restoreMuteAfterSolo = enabled;
    const auto saved = next.save(profileFile());
    if (saved.failed())
        return saved;
    customization.restoreMuteAfterSolo = enabled;
    refresh();
    return juce::Result::ok();
}
void Studio::addChannelMuteItems(juce::PopupMenu &menu, Id channel)
{
    const auto *target = controller.project.channel(channel);
    if (!target)
        return;
    const bool soloed = channelIsSoloed(controller.project, channel);
    const auto identity = controller.projectIdentity;
    const auto revision = controller.revision;
    auto item = [&](int id, juce::String text, juce::String command, bool enabled, juce::String scope = {},
                    bool available = true)
    {
        juce::PopupMenu::Item entry(text);
        entry.itemID = id;
        entry.isEnabled = available;
        entry.action = [safe = juce::Component::SafePointer<Studio>(this), identity, revision, channel,
                        command, enabled, scope]
        {
            if (!safe || safe->controller.projectIdentity != identity)
                return;
            if (safe->controller.revision != revision)
            {
                safe->status = "Channel menu cancelled: project changed";
                safe->repaint();
                return;
            }
            CommandArgs args;
            args.channel = channel;
            args.enabled = enabled;
            if (scope.isNotEmpty())
                args.soloScope = scope;
            safe->command(command, args);
        };
        menu.addItem(std::move(entry));
    };
    item(601, target->muted ? "Unmute" : "Mute", "channel.mute", !target->muted, {}, !target->muteLocked);
    item(602, soloed ? "Unsolo" : "Solo    Ctrl+click", "channel.solo", !soloed, "global",
         !target->muteLocked);
    const bool groupSoloed = channelIsSoloedInScope(controller.project, channel, true);
    item(603, groupSoloed ? "Unsolo filter group" : "Solo within filter group    Alt+click", "channel.solo",
         !groupSoloed, "group", !target->muteLocked);
    item(604, target->muteLocked ? "Unlock mute state    Shift+click" : "Lock mute state    Shift+click",
         "channel.muteLock", !target->muteLocked);
}
juce::Result Studio::Editor::verifyChannelSolo(const juce::File &folder)
{
    auto fail = [](const char *text) { return juce::Result::fail(juce::String("Solo UI: ") + text); };
    const auto kick = s.controller.project.channels[0].id, hat = s.controller.project.channels[2].id;
    const auto active = s.selectedChannel;
    const auto initial = s.controller.project.state();
    const auto originalKickColour = s.controller.project.channel(kick)->colour;
    s.channelPropertiesDialog({kick, hat});
    auto *properties = dynamic_cast<juce::AlertWindow *>(juce::Component::getCurrentlyModalComponent());
    auto *colours = properties && properties->getNumCustomComponents() == 1
                        ? dynamic_cast<ChannelColourPicker *>(properties->getCustomComponent(0))
                        : nullptr;
    auto *keep =
        colours ? dynamic_cast<juce::TextButton *>(colours->findChildWithID("channelColourKeep")) : nullptr;
    auto *preset = colours
                       ? dynamic_cast<juce::TextButton *>(colours->findChildWithID("channelColourPreset.5"))
                       : nullptr;
    if (!keep || !preset || !keep->isVisible())
        return fail("Bulk colour chooser controls missing");
    preset->onClick();
    keep->onClick();
    properties->getComboBoxComponent("icon")->setSelectedItemIndex(4);
    dispatchMessages(5);
    if (properties->getTextEditorContents("colour").isNotEmpty() ||
        !s.controller.project.state().isEquivalentTo(initial))
        return fail("Bulk keep/preview changed existing Channel colours");
    properties->exitModalState(0);
    dispatchMessages(5);
    if (!s.controller.project.state().isEquivalentTo(initial) || s.channelPropertiesWindow)
        return fail("Colour Cancel mutated project or retained its window");
    s.channelPropertiesDialog({kick, hat});
    properties = dynamic_cast<juce::AlertWindow *>(juce::Component::getCurrentlyModalComponent());
    if (!properties)
        return fail("Bulk chooser did not reopen after Cancel");
    properties->getComboBoxComponent("icon")->setSelectedItemIndex(4);
    dispatchMessages(5);
    properties->exitModalState(1);
    dispatchMessages(5);
    if (s.controller.project.channel(kick)->icon != "keys" ||
        s.controller.project.channel(hat)->icon != "keys" ||
        s.controller.project.channel(kick)->colour != originalKickColour || s.selectedChannel != active)
        return fail("Bulk Keep did not apply icon only with preserved colour/recipient");
    const auto appliedProperties = s.controller.project.state();
    s.controller.undoEdit();
    if (!s.controller.project.state().isEquivalentTo(initial))
        return fail("Bulk colour properties lacked one exact Undo");
    s.controller.redoEdit();
    if (!s.controller.project.state().isEquivalentTo(appliedProperties))
        return fail("Bulk colour properties lacked exact Redo");
    s.controller.undoEdit();
    auto click = [&](int row, bool control, int modifiers = 0)
    {
        const juce::Point<float> point{25.f, float(44 + row * rackRowHeight() + 16)};
        juce::MouseEvent event(juce::Desktop::getInstance().getMainMouseSource(), point,
                               juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier |
                                                  (control ? juce::ModifierKeys::ctrlModifier : 0) |
                                                  modifiers),
                               1.f, 0.f, 0.f, 0.f, 0.f, this, this, juce::Time::getCurrentTime(), point,
                               juce::Time::getCurrentTime(), 1, false);
        mouseDown(event);
        mouseUp(event);
    };
    click(0, false, juce::ModifierKeys::altModifier);
    if (!s.controller.project.solo || !s.controller.project.solo->groupScoped ||
        s.controller.project.solo->affected != std::vector<Id>{kick, hat} ||
        s.controller.project.channel(active)->muted || !s.controller.project.channel(hat)->muted ||
        s.selectedChannel != active)
        return fail("Alt solo did not restrict changes to the actual filter group");
    click(0, true, juce::ModifierKeys::altModifier);
    if (!s.controller.project.state().isEquivalentTo(initial))
        return fail("Alt takes precedence over Ctrl and toggles the same group back to baseline");
    click(1, false, juce::ModifierKeys::shiftModifier);
    if (!s.controller.project.channel(hat)->muteLocked || s.selectedChannel != active)
        return fail("Shift mute lock changed the recipient or audible mute");
    const auto locked = s.controller.project.state();
    click(0, false, juce::ModifierKeys::altModifier);
    if (!s.controller.project.solo || s.controller.project.channel(hat)->muted ||
        s.controller.project.solo->affected != std::vector<Id>{kick})
        return fail("Group solo changed or claimed a locked audible peer");
    click(0, false, juce::ModifierKeys::altModifier);
    if (!s.controller.project.state().isEquivalentTo(locked))
        return fail("Group restore did not retain exact lock state");
    click(1, true, juce::ModifierKeys::shiftModifier | juce::ModifierKeys::altModifier);
    if (!s.controller.project.state().isEquivalentTo(initial))
        return fail("Shift lock precedence over combined Alt/Ctrl changed score state");
    s.controller.undoEdit();
    if (!s.controller.project.state().isEquivalentTo(locked))
        return fail("Mute lock one-step Undo was not exact");
    s.controller.redoEdit();
    click(0, true);
    if (!s.controller.project.solo || s.controller.project.solo->channel != kick ||
        s.selectedChannel != active || s.controller.project.solo->mutedBefore.size() != 1)
        return fail("Ctrl mute click did not solo stable filtered target without redirecting MIDI");
    juce::PopupMenu menu;
    s.addChannelMuteItems(menu, hat);
    std::function<void()> switchSolo;
    for (juce::PopupMenu::MenuItemIterator it(menu); it.next();)
        if (it.getItem().itemID == 602)
            switchSolo = it.getItem().action;
    if (!switchSolo)
        return fail("missing real mute-light Solo context action");
    switchSolo();
    if (!s.controller.project.solo || s.controller.project.solo->channel != hat ||
        s.selectedChannel != active || s.controller.project.solo->mutedBefore.size() != 1)
        return fail("context switch lost original baseline or MIDI recipient");
    const auto solo = s.controller.project.state();
    click(1, true);
    if (!s.controller.project.state().isEquivalentTo(initial))
        return fail("second Ctrl-click did not restore exact original mute state");
    s.controller.undoEdit();
    if (!s.controller.project.state().isEquivalentTo(solo))
        return fail("one Unsolo Undo did not restore active solo and baseline");
    s.controller.redoEdit();
    click(0, false);
    if (!s.controller.project.channel(kick)->muted || s.selectedChannel != active)
        return fail("ordinary mute did not retain active MIDI target");
    s.controller.undoEdit();
    s.selectRackChannels({kick, hat});
    s.performAction("rack.muteSelected");
    if (!s.controller.project.channel(kick)->muted || !s.controller.project.channel(hat)->muted ||
        s.selectedChannel != active)
        return fail("bulk mute did not preserve target identities");
    s.controller.undoEdit();
    RackPage page(s);
    auto *restore = dynamic_cast<juce::ToggleButton *>(page.findChildWithID("rack-solo-restore"));
    if (!restore || !s.workflowTestSettings)
        return fail("missing real Workflow preference or isolated settings file");
    restore->setToggleState(false, juce::dontSendNotification);
    restore->onClick();
    Customization loaded;
    if (s.customization.restoreMuteAfterSolo || loaded.load(s.profileFile()).failed() ||
        loaded.restoreMuteAfterSolo || !s.controller.project.state().isEquivalentTo(initial))
        return fail("Workflow UI did not persist preference independently of musical history");
    auto *memory = dynamic_cast<juce::ToggleButton *>(page.findChildWithID("rack-mute-removed-steps"));
    if (!memory || memory->getWidth() < 400 || memory->getHeight() < 32 ||
        memory->getBounds().intersects(restore->getBounds()) || memory->getBottom() > page.getHeight())
        return fail("missing or overlapping readable retained-step Workflow control");
    const auto memoryRevision = s.controller.revision;
    const auto memoryHistory = s.controller.undo.getNumberOfUnitsTakenUpByStoredCommands();
    memory->setToggleState(true, juce::dontSendNotification);
    memory->onClick();
    if (!s.customization.muteRemovedSteps || !memory->getToggleState() ||
        loaded.load(s.profileFile()).failed() || !loaded.muteRemovedSteps ||
        s.controller.revision != memoryRevision || !s.controller.project.state().isEquivalentTo(initial))
        return fail("retained-step Workflow click did not persist independently of musical history");
    const juce::String memoryOwner("workflow-step-memory-test");
    if (!s.controller.beginGesture(memoryOwner))
        return fail("retained-step Workflow owned gesture fixture");
    memory->setToggleState(false, juce::dontSendNotification);
    memory->onClick();
    const bool gestureRefused = s.customization.muteRemovedSteps && memory->getToggleState() &&
                                loaded.load(s.profileFile()).wasOk() && loaded.muteRemovedSteps;
    if (s.controller.cancelGestureChecked(memoryOwner).failed() || !gestureRefused ||
        !s.controller.project.state().isEquivalentTo(initial) ||
        s.controller.revision != memoryRevision + 1 ||
        s.controller.undo.getNumberOfUnitsTakenUpByStoredCommands() != memoryHistory)
        return fail("retained-step Workflow changed policy during an owned gesture");
    // Canonical Cancel advances revision to expire stale clients, without
    // creating musical history. Compare later preference writes to this fence.
    const auto memoryAfterCancelRevision = s.controller.revision;
    const auto profile = s.profileFile();
    if (!profile.setReadOnly(true))
        return fail("owned read-only profile failure fixture setup");
    memory->setToggleState(false, juce::dontSendNotification);
    memory->onClick();
    const bool memoryWriteRefused = s.customization.muteRemovedSteps && memory->getToggleState() &&
                                    loaded.load(profile).wasOk() && loaded.muteRemovedSteps;
    const auto refused = s.setSoloRestorePreference(true);
    const bool unchanged =
        !s.customization.restoreMuteAfterSolo && loaded.load(profile).wasOk() && !loaded.restoreMuteAfterSolo;
    profile.setReadOnly(false);
    if (refused.wasOk() || !unchanged || !memoryWriteRefused)
        return fail("failed preference persistence changed current or saved settings");
    memory->setToggleState(false, juce::dontSendNotification);
    memory->onClick();
    if (s.customization.muteRemovedSteps || loaded.load(profile).failed() || loaded.muteRemovedSteps ||
        s.controller.revision != memoryAfterCancelRevision ||
        s.controller.undo.getNumberOfUnitsTakenUpByStoredCommands() != memoryHistory ||
        !s.controller.project.state().isEquivalentTo(initial))
        return fail("retained-step Workflow could not restore the legacy deletion preference");
    restore->setToggleState(true, juce::dontSendNotification);
    restore->onClick();
    if (!s.performAction("rack.soloRestore") || s.customization.restoreMuteAfterSolo ||
        !s.performAction("rack.soloRestore") || !s.customization.restoreMuteAfterSolo)
        return fail("registered Workflow shortcut did not use the shared persistent preference");
    click(1, true);
    s.command("channel.unsolo", {});
    CommandArgs lockedPeer;
    lockedPeer.channel = s.controller.project.channels[1].id;
    s.command("channel.muteLock", lockedPeer);
    click(0, false, juce::ModifierKeys::altModifier);
    s.status = "Channel Solo | Ctrl: global / Alt: group / Shift: lock | Live MIDI remains playable";
    s.setSize(1920, 1040);
    s.resized();
    juce::PNGImageFormat png;
    auto snapshot = [&](const juce::String &name, juce::Component &component, float scale)
    {
        auto stream = folder.getChildFile(name).createOutputStream();
        if (!stream)
            return false;
        stream->setPosition(0);
        stream->truncate();
        return png.writeImageToStream(
            component.createComponentSnapshot(component.getLocalBounds(), true, scale), *stream);
    };
    if (!snapshot("channel-solo-workspace.png", s, 1.f) || !snapshot("channel-solo-settings.png", page, 1.f))
        return fail("actual populated/Workflow renders");
    s.setSize(2560, 1392);
    s.resized();
    if (!snapshot("channel-solo-4k.png", s, 1.5f))
        return fail("actual4K render");
    s.setSize(1920, 1040);
    s.resized();
    const auto replacement = Project::empty();
    s.controller.replace(replacement);
    switchSolo();
    if (!s.controller.project.state().isEquivalentTo(replacement.state()))
        return fail("stale popup affected replacement project");
    folder.getChildFile("channel-solo-gui.txt")
        .replaceWithText(
            "PASS actual filtered Ctrl/global, Alt/group, Shift/lock and combined precedence; "
            "locked-peer exclusion; exact lock Undo/Redo; mute/solo/restore; context switch; exact "
            "Undo/Redo; ordinary/bulk mute "
            "retain active MIDI target; Workflow checkbox persists isolated preference; stale "
            "menu; populated1080p/4K "
            "and settings component PNGs. No OS input, physical MIDI or listening.\n");
    return juce::Result::ok();
}
juce::Result Studio::verifyChannelSolo(const juce::File &folder)
{
    stopTimer();
    audio.stop();
    releaseTypingNotes();
    customization = Customization::familiarDefaults();
    auto project = Project::empty();
    project.patterns[0].length = 8;
    project.channels[1].muted = true;
    for (const auto &channel : project.channels)
        project.patterns[0].notes.push_back({project.allocate(), channel.id, .5, 1, 60, .7f, false});
    selectedPattern = project.patterns[0].id;
    selectedChannel = project.channels[3].id;
    const auto group = project.allocate();
    project.channelGroups.push_back({group, "Drums"});
    project.channels[0].group = project.channels[2].group = group;
    project.placements = {{project.allocate(), selectedPattern, 0, 0, 8, 0},
                          {project.allocate(), selectedPattern, 0, 8, 8, 0}};
    controller.replace(project);
    refresh();
    setRackFilter("group", group);
    return static_cast<Editor *>(windows[0]->getContentComponent())->verifyChannelSolo(folder);
}
} // namespace vibera
