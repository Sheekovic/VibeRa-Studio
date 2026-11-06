// SPDX-License-Identifier: AGPL-3.0-or-later
#include "NumberText.h"
#include "Studio.h"
#include "Editors.h"
#include "InstrumentWindow.h"
#include "McpSchema.h"
#include "RackContracts.h"
#include "NativeWindowStyle.h"
#include "MidiScore.h"
#include "ChannelAppearanceCommands.h"
#include "ChannelColourPicker.h"
#include "../tests/SyntheticDevice.h"
namespace vibera
{
void Studio::patternMeterDialog(Id pattern)
{
    if (patternMeterWindow && patternMeterWindow->isCurrentlyModal())
    {
        patternMeterWindow->toFront(true);
        return;
    }
    const auto *part = controller.project.pattern(pattern);
    if (!part)
        return;
    const auto identity = controller.projectIdentity;
    const auto revision = controller.revision;
    auto *dialog =
        new juce::AlertWindow("Pattern time signature",
                              "Changes this Pattern's bars and grid. Song playback uses the project meter. "
                              "Notes and Playlist windows stay in quarter-note beats.",
                              juce::MessageBoxIconType::NoIcon);
    patternMeterWindow = dialog;
    dialog->addComboBox("mode", {"Inherit project meter", "Custom Pattern meter"}, "Meter source");
    dialog->getComboBoxComponent("mode")->setSelectedItemIndex(part->numerator == 0 ? 0 : 1);
    juce::StringArray numerators;
    for (int value = 1; value <= 32; ++value)
        numerators.add(juce::String(value));
    dialog->addComboBox("numerator", numerators, "Beats per bar");
    dialog->getComboBoxComponent("numerator")
        ->setSelectedItemIndex(patternNumerator(controller.project, *part) - 1);
    dialog->addComboBox("denominator", {"2", "4", "8", "16"}, "Beat unit");
    dialog->getComboBoxComponent("denominator")
        ->setSelectedItemIndex(patternDenominator(controller.project, *part) == 2   ? 0
                               : patternDenominator(controller.project, *part) == 4 ? 1
                               : patternDenominator(controller.project, *part) == 8 ? 2
                                                                                    : 3);
    dialog->addButton("Apply", 1, juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    dialog->enterModalState(
        true,
        juce::ModalCallbackFunction::create(
            [safe = juce::Component::SafePointer<Studio>(this),
             window = juce::Component::SafePointer<juce::AlertWindow>(dialog), identity, revision,
             pattern](int result)
            {
                if (!safe || !window || result != 1)
                    return;
                if (safe->controller.projectIdentity != identity || safe->controller.revision != revision)
                {
                    safe->status = "Pattern meter cancelled: source changed";
                    return;
                }
                CommandArgs args;
                args.pattern = pattern;
                args.enabled = window->getComboBoxComponent("mode")->getSelectedItemIndex() == 1;
                args.numerator = window->getComboBoxComponent("numerator")->getSelectedItemIndex() + 1;
                args.denominator = 2 << window->getComboBoxComponent("denominator")->getSelectedItemIndex();
                safe->command("pattern.meter", args);
            }),
        true);
}
void Studio::channelLoopLengthDialog(Id channel, Id pattern)
{
    const auto *part = controller.project.pattern(pattern);
    if (!part || !controller.project.channel(channel))
        return;
    const auto identity = controller.projectIdentity;
    const auto revision = controller.revision;
    auto *dialog = new juce::AlertWindow(
        "Channel loop length",
        "Fixed length in quarter-note beats (multiples of0.25). Source notes remain unchanged.",
        juce::MessageBoxIconType::NoIcon);
    const double current = channelLoopLength(controller.project, *part, channel);
    dialog->addTextEditor("length", juce::String(current > 0 ? current : 1), "Beats");
    dialog->addButton("Apply", 1, juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    dialog->enterModalState(
        true,
        juce::ModalCallbackFunction::create(
            [safe = juce::Component::SafePointer<Studio>(this),
             window = juce::Component::SafePointer<juce::AlertWindow>(dialog), identity, revision, channel,
             pattern](int result)
            {
                if (!safe || !window || result != 1)
                    return;
                if (safe->controller.projectIdentity != identity || safe->controller.revision != revision)
                {
                    safe->status = "Channel loop cancelled: source changed";
                    return;
                }
                CommandArgs args;
                args.channel = channel;
                args.pattern = pattern;
                args.text = "fixed";
                const auto text = window->getTextEditorContents("length").trim();
                const auto utf8 = text.toStdString();
                const auto parsed = vibera::number_text::fromChars(utf8.data(), utf8.data() + utf8.size(), args.length);
                if (text.isEmpty() || parsed.ec != std::errc{} || parsed.ptr != utf8.data() + utf8.size())
                {
                    safe->status = "channelLoop.invalidLength: Enter a positive beat length";
                    return;
                }
                safe->command("pattern.channelLoop", args);
            }),
        true);
}
void Studio::addChannelLoopItems(juce::PopupMenu &menu, Id channel, Id pattern)
{
    const auto *part = controller.project.pattern(pattern);
    const auto *source = controller.project.channel(channel);
    if (!part || !source || source->audioClip)
        return;
    const auto identity = controller.projectIdentity;
    const auto revision = controller.revision;
    const auto *current = channelLoop(*part, channel);
    juce::PopupMenu loops;
    int itemId = 920;
    for (const auto &[mode, title] : std::array<std::pair<const char *, const char *>, 6>{
             {{"off", "Off"},
              {"step", "Auto: next Step"},
              {"beat", "Auto: next Beat"},
              {"bar", "Auto: next Bar"},
              {"fixed", "Fixed length..."},
              {"burn", "Burn repetitions to score"}}})
    {
        if (juce::String(mode).isEmpty())
            break;
        juce::PopupMenu::Item item(title);
        item.itemID = itemId++;
        item.isTicked = juce::String(mode) == (current ? channelLoopModeName(current->mode) : "off");
        item.isEnabled = juce::String(mode) != "burn" || (current && !audio.playing() && !recordArmed &&
                                                          !midiTaking && !hasPendingMidiTake());
        item.action = [safe = juce::Component::SafePointer<Studio>(this), identity, revision, channel,
                       pattern, mode = juce::String(mode)]
        {
            if (!safe || safe->controller.projectIdentity != identity ||
                safe->controller.revision != revision)
                return;
            if (mode == "fixed")
            {
                safe->channelLoopLengthDialog(channel, pattern);
                return;
            }
            CommandArgs args;
            args.channel = channel;
            args.pattern = pattern;
            args.text = mode;
            safe->command(mode == "burn" ? "pattern.burnChannelLoop" : "pattern.channelLoop", args);
        };
        loops.addItem(item);
    }
    menu.addSubMenu("Channel loop (this Pattern)", loops);
}
namespace
{
class ChannelPropertiesAlert final : public juce::AlertWindow
{
  public:
    ChannelColourPicker colours;
    ChannelPropertiesAlert(bool single, juce::Colour inherited)
        : juce::AlertWindow(single ? "Channel properties" : "Selected Channel properties",
                            "Display settings preserve instruments, scores and routing. "
                            "Blank bulk fields leave values unchanged.",
                            juce::MessageBoxIconType::NoIcon),
          colours(!single, inherited)
    {
    }
    ~ChannelPropertiesAlert() override
    {
        if (getNumCustomComponents() > 0)
            removeCustomComponent(0);
    }
};
} // namespace
void Studio::channelPropertiesDialog(const std::vector<Id> &ids)
{
    if (channelPropertiesWindow)
    {
        channelPropertiesWindow->toFront(true);
        return;
    }
    if (ids.empty() || ids.size() > 128)
        return;
    std::vector<Channel> originals;
    std::set<Id> unique;
    for (const auto id : ids)
    {
        const auto *channel = controller.project.channel(id);
        if (!channel || !unique.insert(id).second)
            return;
        originals.push_back(*channel);
    }
    const bool single = ids.size() == 1;
    auto dialog = std::make_unique<ChannelPropertiesAlert>(single, channelColour(int(ids.front() - 1)));
    dialog->setLookAndFeel(&look);
    if (single)
        dialog->addTextEditor("name", originals.front().name, "Name");
    dialog->addTextEditor("colour",
                          single
                              ? (originals.front().colour
                                     ? "#" + juce::String::toHexString(originals.front().colour).substring(2)
                                     : "theme")
                              : "",
                          "Colour: #RRGGBB or theme");
    juce::StringArray icons;
    icons.add("Keep current");
    for (const auto *icon : channelAppearanceIconIds)
        icons.add(icon);
    dialog->addComboBox("icon", icons, "Original icon");
    if (single)
        dialog->getComboBoxComponent("icon")->setSelectedItemIndex(icons.indexOf(originals.front().icon));
    dialog->addCustomComponent(&dialog->colours);
    dialog->addButton("Apply", 1, juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    const auto identity = controller.projectIdentity;
    auto safe = juce::Component::SafePointer<Studio>(this);
    auto pointer = juce::Component::SafePointer<juce::AlertWindow>(dialog.get());
    channelPropertiesWindow = dialog.get();
    const auto read = [pointer, single, ids](CommandArgs &args) -> juce::Result
    {
        if (!pointer)
            return juce::Result::fail("Dialog closed");
        ChannelAppearancePatch patch;
        if (single)
            patch.name = pointer->getTextEditorContents("name");
        if (auto result = readChannelColourDraft(pointer->getTextEditorContents("colour"), patch.colour);
            result.failed())
            return result;
        const auto icon = pointer->getComboBoxComponent("icon")->getSelectedItemIndex();
        if (icon > 0)
            patch.icon = channelAppearanceIconIds[size_t(icon - 1)];
        args.channel = ids.front();
        args.ids = ids;
        args.appearance = std::move(patch);
        return juce::Result::ok();
    };
    const auto validate = [safe, pointer, read, single]
    {
        if (!safe || !pointer)
            return;
        CommandArgs args;
        auto result = read(args);
        if (result.wasOk())
        {
            // Appearance validation needs Channels only, never a copy of every
            // Pattern/clip/curve on each spectrum drag. The canonical handler
            // still validates the exact current IDs and metadata.
            Project project;
            project.channels = safe->controller.project.channels;
            result = applyChannelAppearance(project, single ? "channel.properties" : "rack.properties", args);
        }
        pointer->getButton("Apply")->setEnabled(result.wasOk());
        pointer->setMessage(result.wasOk() ? "Display settings preserve instruments, scores and routing."
                                           : result.getErrorMessage());
    };
    if (single)
        dialog->getTextEditor("name")->onTextChange = validate;
    const auto syncColour = [pointer, validate]
    {
        if (!pointer)
            return;
        if (auto *custom = dynamic_cast<ChannelColourPicker *>(pointer->getCustomComponent(0)))
            custom->setDraft(pointer->getTextEditorContents("colour"));
        validate();
    };
    dialog->getTextEditor("colour")->onTextChange = syncColour;
    dialog->getComboBoxComponent("icon")->onChange = validate;
    dialog->colours.onChoose = [pointer, validate](const juce::String &value)
    {
        if (!pointer)
            return;
        pointer->getTextEditor("colour")->setText(value, false);
        validate();
    };
    syncColour();
    dialog->enterModalState(true,
                            juce::ModalCallbackFunction::create(
                                [safe, pointer, identity, originals, read, single](int accepted)
                                {
                                    if (!safe || !pointer || accepted != 1)
                                        return;
                                    bool current = safe->controller.projectIdentity == identity;
                                    for (const auto &original : originals)
                                    {
                                        const auto *channel = safe->controller.project.channel(original.id);
                                        current = current && channel && channel->name == original.name &&
                                                  channel->colour == original.colour &&
                                                  channel->icon == original.icon &&
                                                  channel->source == original.source &&
                                                  (channel->instrument ? channel->instrument->id : 0) ==
                                                      (original.instrument ? original.instrument->id : 0);
                                    }
                                    if (!current)
                                    {
                                        safe->status = "Channel properties cancelled: target changed";
                                        safe->repaint();
                                        return;
                                    }
                                    CommandArgs args;
                                    if (auto result = read(args); result.failed())
                                    {
                                        safe->status = result.getErrorMessage();
                                        safe->repaint();
                                        return;
                                    }
                                    safe->command(single ? "channel.properties" : "rack.properties", args);
                                }),
                            true);
    roundNativeWindow(*dialog);
    dialog.release();
}
std::vector<Id> Studio::visibleRackChannels() const
{
    std::vector<Id> result;
    for (const auto &channel : controller.project.channels)
        if (controller.project.warpOriginalOnly(channel.id))
            continue;
        else if (rackFilter == "all" || (rackFilter == "unsorted" && channel.group == 0) ||
            (rackFilter == "group" && channel.group == rackGroup))
            result.push_back(channel.id);
    return result;
}
Channel *Studio::rackChannelAt(int row)
{
    const auto visible = visibleRackChannels();
    return row < 0 || size_t(row) >= visible.size() ? nullptr
                                                    : controller.project.channel(visible[size_t(row)]);
}
void Studio::reconcileRack()
{
    if (rackProjectIdentity != controller.projectIdentity)
    {
        rackProjectIdentity = controller.projectIdentity;
        rackFilter = "all";
        rackGroup = 0;
        selectedChannels = controller.project.channel(selectedChannel) ? std::vector<Id>{selectedChannel}
                                                                       : std::vector<Id>{};
        rackObservedActive = selectedChannel;
    }
    if (rackFilter == "group" && !controller.project.channelGroup(rackGroup))
    {
        rackFilter = "all";
        rackGroup = 0;
    }
    if (rackObservedActive != selectedChannel)
    {
        selectedChannels = {selectedChannel};
        rackObservedActive = selectedChannel;
    }
    std::erase_if(selectedChannels, [&](Id id) { return !controller.project.channel(id); });
}
juce::Result Studio::selectRackChannels(const std::vector<Id> &ids, const juce::String &mode, Id active)
{
    if (mode != "replace" && mode != "add" && mode != "remove" && mode != "toggle")
        return juce::Result::fail("Unknown Channel selection mode");
    if (ids.size() > 128 || (active && !controller.project.channel(active)))
        return juce::Result::fail("Channel selection/active target is unavailable");
    for (auto id : ids)
        if (!controller.project.channel(id))
            return juce::Result::fail("Channel selection target is unavailable");
    auto next = mode == "replace" ? std::vector<Id>{} : selectedChannels;
    for (auto id : ids)
    {
        auto at = std::find(next.begin(), next.end(), id);
        if (mode == "remove" || (mode == "toggle" && at != next.end()))
        {
            if (at != next.end())
                next.erase(at);
        }
        else if (at == next.end())
            next.push_back(id);
    }
    if (active && active != selectedChannel)
    {
        if (auto result = finishMidiTake(); result.failed())
            return result;
        releaseTypingNotes();
        selectedChannel = active;
    }
    rackObservedActive = selectedChannel;
    selectedChannels = std::move(next);
    refresh();
    return juce::Result::ok();
}
juce::Result Studio::setRackFilter(const juce::String &kind, Id group)
{
    if ((kind != "all" && kind != "unsorted" && kind != "group") ||
        (kind == "group" ? !controller.project.channelGroup(group) : group != 0))
        return juce::Result::fail("Channel display filter is unavailable");
    if (windows[0])
        static_cast<Editor *>(windows[0]->getContentComponent())->finishRackGesture();
    rackFilter = kind;
    rackGroup = group;
    refresh();
    return juce::Result::ok();
}
void Studio::rackGroupDialog(const juce::String &operation)
{
    if (operation != "create" && operation != "rename" && operation != "assign")
        return;
    if (rackGroupWindow && rackGroupWindow->isCurrentlyModal())
    {
        rackGroupWindow->toFront(true);
        return;
    }
    if (operation == "assign" && selectedChannels.empty())
    {
        status = "Select one or more Channels first";
        repaint();
        return;
    }
    const auto *group = rackFilter == "group" ? controller.project.channelGroup(rackGroup) : nullptr;
    if (operation == "rename" && !group)
        return;
    auto dialog = std::make_unique<juce::AlertWindow>(operation == "create"   ? "Add filter group"
                                                      : operation == "rename" ? "Rename filter group"
                                                                              : "Group selected Channels",
                                                      "Enter a group name", juce::MessageBoxIconType::NoIcon);
    dialog->addTextEditor("name", group ? group->name : juce::String{}, "Name");
    dialog->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    const auto identity = controller.projectIdentity;
    const auto revision = controller.revision;
    const auto groupId = group ? group->id : 0;
    const auto channels = selectedChannels;
    auto safe = juce::Component::SafePointer<Studio>(this);
    auto pointer = juce::Component::SafePointer<juce::AlertWindow>(dialog.get());
    rackGroupWindow = dialog.get();
    auto validate = [safe, pointer, operation, groupId]
    {
        if (!safe || !pointer)
            return;
        const auto result =
            validateRackGroupName(safe->controller.project, pointer->getTextEditorContents("name"),
                                  operation == "rename" ? groupId : 0, operation == "assign");
        pointer->getButton("OK")->setEnabled(result.wasOk());
        pointer->setMessage(result.wasOk() ? "Group names do not change routing or pattern ownership"
                                           : result.getErrorMessage());
    };
    dialog->getTextEditor("name")->onTextChange = validate;
    validate();
    dialog->enterModalState(
        true,
        juce::ModalCallbackFunction::create(
            [safe, pointer, operation, identity, revision, groupId, channels](int accepted)
            {
                if (!safe || !pointer || accepted != 1)
                    return;
                if (safe->controller.projectIdentity != identity || safe->controller.revision != revision)
                {
                    safe->status = "Group edit cancelled: project changed";
                    safe->repaint();
                    return;
                }
                CommandArgs args;
                args.text = pointer->getTextEditorContents("name");
                args.target = operation == "rename" ? groupId : 0;
                args.ids = channels;
                safe->command("rack.group." + operation, args);
            }),
        true);
    roundNativeWindow(*dialog);
    dialog.release();
}
void Studio::rackGroupMenu(juce::Component *anchor)
{
    juce::PopupMenu menu;
    menu.addItem(1, "Group selected...    Alt+G", !selectedChannels.empty());
    menu.addItem(2, "Add filter group...");
    menu.addItem(3, "Rename filter group...", rackFilter == "group");
    menu.addItem(4, "Delete filter group", rackFilter == "group");
    menu.addItem(5, "Move selected to Unsorted", !selectedChannels.empty());
    menu.addSeparator();
    menu.addItem(6, "Select all visible Channels");
    menu.addItem(28, "Select unused Channels");
    menu.addItem(33, "Transpose selected Channels...", !selectedChannels.empty());
    menu.addItem(34, "Assign selected to free Mixer inserts", !selectedChannels.empty());
    menu.addItem(31, "Zip selected Channels    Alt+Z", !selectedChannels.empty());
    menu.addItem(32, "Unzip all    Alt+U", !zippedChannels.empty());
    menu.addItem(7, "Deselect Channels", !selectedChannels.empty());
    juce::PopupMenu fill;
    addStepFillItems(fill, selectedChannels, selectedPattern);
    menu.addSubMenu("Fill selected Channels", fill, !selectedChannels.empty());
    menu.addSeparator();
    menu.addItem(8, "Mute selected Channels", !selectedChannels.empty());
    menu.addItem(9, "Unmute selected Channels", !selectedChannels.empty());
    menu.addItem(10, "Unsolo Channels", bool(controller.project.solo));
    menu.addItem(26, "Lock selected mute states", !selectedChannels.empty());
    menu.addItem(27, "Unlock selected mute states", !selectedChannels.empty());
    menu.addSeparator();
    addChannelManagementItems(menu, selectedChannels);
    menu.addSeparator();
    menu.addItem(11, "Global Swing...");
    menu.addSeparator();
    menu.addItem(20, "Colour / icon selected Channels...", !selectedChannels.empty());
    menu.addItem(29, "Random colours for selected Channels", !selectedChannels.empty());
    menu.addItem(30, "Gradient colours for selected Channels", selectedChannels.size() > 1);
    juce::PopupMenu sort;
    sort.addItem(21, "Name A-Z");
    sort.addItem(22, "Assigned colour (theme first)");
    sort.addItem(23, "Mixer assignment");
    sort.addItem(24, "Instrument / sample / Audio Clip");
    sort.addItem(25, "Name Z-A");
    menu.addSubMenu("Sort selected Channels", sort, !selectedChannels.empty());
    addSwingItems(menu, selectedChannels);
    {
        juce::PopupMenu width;
        width.addItem(40, "Narrow", true, rackButtonWidth == 0);
        width.addItem(41, "Normal", true, rackButtonWidth == 1);
        width.addItem(42, "Wide", true, rackButtonWidth == 2);
        menu.addSubMenu("Channel button width", width);
    }
    menu.addItem(43, "Focus selected channel", true, rackFocusSelected);
    menu.addItem(44, "Scroll to the selected channel");
    const auto identity = controller.projectIdentity;
    const auto group = rackGroup;
    const auto channels = selectedChannels;
    auto safe = juce::Component::SafePointer<Studio>(this);
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(anchor),
        [safe, identity, group, channels](int action)
        {
            if (!safe || safe->controller.projectIdentity != identity)
                return;
            safe->focus = Scope::rack;
            if (action == 43 || action == 44)
            {
                safe->performAction(action == 43 ? "view.rackFocusSelected" : "view.rackScrollToSelected");
                return;
            }
            if (action >= 40 && action <= 42)
            {
                safe->performAction(action == 40 ? "view.rackButtonsNarrow"
                                    : action == 41 ? "view.rackButtonsNormal"
                                                   : "view.rackButtonsWide");
                return;
            }
            if (action == 26 || action == 27)
            {
                CommandArgs args;
                args.ids = channels;
                args.enabled = action == 26;
                safe->command("rack.muteLock", args);
            }
            if (action == 20)
                safe->channelPropertiesDialog(channels);
            if (action == 31 || action == 32)
                safe->performAction(action == 31 ? "rack.zip" : "rack.unzipAll");
            if (action == 29 || action == 30)
            {
                CommandArgs args;
                args.ids = channels;
                args.text = action == 29 ? "random" : "gradient";
                args.value = double(juce::Random::getSystemRandom().nextInt(1000000));
                safe->command("rack.colourSpread", args);
            }
            if (action == 28)
            {
                // Channels with no notes, steps or loops in any Pattern and no Playlist audio clip.
                const auto &p = safe->controller.project;
                std::vector<Id> unused;
                for (const auto &channel : p.channels)
                {
                    bool used = std::any_of(p.audioPlacements.begin(), p.audioPlacements.end(),
                                            [&](const auto &c) { return c.channel == channel.id; });
                    for (const auto &pattern : p.patterns)
                        used = used ||
                               std::any_of(pattern.notes.begin(), pattern.notes.end(),
                                           [&](const auto &n) { return n.channel == channel.id; }) ||
                               std::any_of(pattern.channelLoops.begin(), pattern.channelLoops.end(),
                                           [&](const auto &l) { return l.channel == channel.id; });
                    if (!used)
                        unused.push_back(channel.id);
                }
                safe->selectRackChannels(unused);
                safe->status = juce::String(int(unused.size())) + " unused Channel(s) selected";
            }
            if (action == 34)
            {
                safe->focus = Scope::rack;
                safe->performAction("rack.assignFreeInserts");
                return;
            }
            if (action == 33)
            {
                auto *box = new juce::AlertWindow("Transpose selected Channels",
                                                  "Semitones (-127..127; 12 = one octave up). Moves their notes in "
                                                  "every Pattern.",
                                                  juce::MessageBoxIconType::NoIcon);
                box->addTextEditor("semitones", "12");
                box->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
                box->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
                box->enterModalState(true, juce::ModalCallbackFunction::create(
                                               [safe, box, channels](int ok)
                                               {
                                                   if (!safe || ok != 1)
                                                       return;
                                                   CommandArgs args;
                                                   args.ids = channels;
                                                   args.value = box->getTextEditorContents("semitones")
                                                                    .trim().getIntValue();
                                                   safe->command("rack.transpose", args);
                                               }),
                                     true);
                return;
            }
            if (action >= 21 && action <= 25)
            {
                CommandArgs args;
                args.ids = channels;
                args.channelSort = ChannelSortRequest{action == 22   ? ChannelSortRequest::By::colour
                                                      : action == 23 ? ChannelSortRequest::By::mixer
                                                      : action == 24 ? ChannelSortRequest::By::type
                                                                     : ChannelSortRequest::By::name,
                                                      action == 25};
                safe->command("rack.sort", args);
            }
            if (action == 11)
                safe->swingDialog("project.swing");
            if (action >= 1 && action <= 3)
                safe->rackGroupDialog(action == 1 ? "assign" : action == 2 ? "create" : "rename");
            else if (action == 4)
            {
                CommandArgs args;
                args.target = group;
                safe->command("rack.group.delete", args);
            }
            else if (action == 5)
            {
                CommandArgs args;
                args.ids = safe->selectedChannels;
                safe->command("rack.group.assign", args);
            }
            else if (action == 6)
                safe->selectRackChannels(safe->visibleRackChannels());
            else if (action == 7)
                safe->selectRackChannels({});
            else if (action >= 8 && action <= 10)
                safe->performAction(action == 8   ? "rack.muteSelected"
                                    : action == 9 ? "rack.unmuteSelected"
                                                  : "channel.unsolo");
        });
}
juce::var Studio::rackState() const
{
    auto object = [](std::initializer_list<std::pair<const char *, juce::var>> values)
    {
        auto *item = new juce::DynamicObject;
        for (const auto &value : values)
            item->setProperty(value.first, value.second);
        return juce::var(item);
    };
    const auto visible = visibleRackChannels();
    const auto *pattern = controller.project.pattern(selectedPattern);
    juce::Array<juce::var> groups, rows, visibleIds, selection;
    for (const auto &group : controller.project.channelGroups)
    {
        int count = 0;
        for (const auto &channel : controller.project.channels)
            count += channel.group == group.id;
        groups.add(object({{"id", juce::int64(group.id)}, {"name", group.name}, {"channelCount", count}}));
    }
    for (auto id : visible)
    {
        const auto *channel = controller.project.channel(id);
        visibleIds.add(juce::int64(id));
        rows.add(
            object({{"id", juce::int64(id)},
                    {"name", channel->name},
                    {"colour", juce::int64(channel->colour)},
                    {"icon", channel->icon},
                    {"group", juce::int64(channel->group)},
                    {"selected", selected(selectedChannels, id)},
                    {"active", id == selectedChannel},
                    {"audioClip", channel->audioClip},
                    {"canFillSteps", !channel->audioClip},
                    {"committedSequenceMode", channel->audioClip                               ? "audioClip"
                                              : pattern && stepSequenceIsMelodic(*pattern, id) ? "pianoRoll"
                                                                                               : "steps"},
                    {"muted", channel->muted},
                    {"muteLocked", channel->muteLocked},
                    {"soloed", channelIsSoloed(controller.project, id)},
                    {"mixer", channel->mixer},
                    {"swingMix", channel->swingMix},
                    {"truncateSwing", channel->truncateSwing}}));
        rows.getReference(rows.size() - 1)
            .getDynamicObject()
            ->setProperty("loopMode", pattern && channelLoop(*pattern, id)
                                          ? channelLoopModeName(channelLoop(*pattern, id)->mode)
                                          : "off");
        rows.getReference(rows.size() - 1)
            .getDynamicObject()
            ->setProperty("loopLength", pattern ? channelLoopLength(controller.project, *pattern, id) : 0);
    }
    for (auto id : selectedChannels)
        selection.add(juce::int64(id));
    juce::Array<juce::var> restoreMuted;
    juce::Array<juce::var> soloAffected;
    if (controller.project.solo)
    {
        for (auto id : controller.project.solo->mutedBefore)
            restoreMuted.add(juce::int64(id));
        for (auto id : controller.project.solo->affected)
            soloAffected.add(juce::int64(id));
    }
    const auto *group = controller.project.channelGroup(rackGroup);
    juce::Array<juce::var> operations;
    for (const auto &job : channelEditJobs)
        operations.add(object({{"operation", job.id},
                               {"command", job.command},
                               {"completionState", job.state},
                               {"projectIdentity", job.project},
                               {"sourceRevision", juce::int64(job.revision)}}));
    juce::Array<juce::var> colourPalette;
    const auto colourChoices = channelAppearancePalette();
    for (size_t i = 0; i < colourChoices.size(); ++i)
        colourPalette.add(
            object({{"presetIndex", int(i)}, {"colour", juce::int64(colourChoices[i].getARGB())}}));
    juce::var swingControlState;
    if (windows[0])
        if (const auto *slider = static_cast<Editor *>(windows[0]->getContentComponent())->rackSwingControl())
        {
            const auto bounds = getLocalArea(slider, slider->getLocalBounds());
            swingControlState = object({{"visible", slider->isShowing()},
                                        {"x", bounds.getX()},
                                        {"y", bounds.getY()},
                                        {"width", bounds.getWidth()},
                                        {"height", bounds.getHeight()},
                                        {"uiValuePercent", controller.project.swing * 100}});
        }
    return object(
        {{"completionState", "completed"},
         {"projectIdentity", controller.projectIdentity},
         {"revision", juce::int64(controller.revision)},
         {"filter", object({{"kind", rackFilter},
                            {"group", juce::int64(rackGroup)},
                            {"name", group                 ? group->name
                                     : rackFilter == "all" ? "All"
                                                           : "Unsorted"}})},
         {"groups", groups},
         {"rows", rows},
         {"visibleChannelIds", visibleIds},
         {"selectedChannels", selection},
         {"activeChannel", juce::int64(selectedChannel)},
         {"activeChannelVisible", selected(visible, selectedChannel)},
         {"viewport", windows[0] ? static_cast<Editor *>(windows[0]->getContentComponent())->rackViewport()
                                 : juce::var{}},
         {"graph", rackGraphState()},
         {"colourPicker", object({{"palette", colourPalette},
                                  {"themeValue", 0},
                                  {"units", "Opaque 0xFFRRGGBB integer;0 follows the theme"},
                                  {"previewChangesProject", false},
                                  {"bulkUnchanged", "Omit appearance.colour"},
                                  {"singleCommand", "channel.properties"},
                                  {"bulkCommand", "rack.properties"}})},
         {"swing", controller.project.swing},
         {"swingControl", swingControlState},
         {"swingUnits", "normalized 0-1; UI percent"},
         {"swingTiming", "Continuous monotonic sixteenth-pair warp; odd-sixteenth maximum delay 0.125 "
                         "quarter-note beats times global and Channel Mix; source notes retained"},
         {"swingAffectsLiveInput", false},
         {"swingAffectsAudioClips", false},
         {"selectionLayersMidi", false},
         {"soloChannel", juce::int64(controller.project.solo ? controller.project.solo->channel : 0)},
         {"hasSoloSnapshot", bool(controller.project.solo)},
         {"soloRestoreMuted", restoreMuted},
         {"soloScope", controller.project.solo
                           ? juce::var(controller.project.solo->groupScoped ? "group" : "global")
                           : juce::var{}},
         {"soloGroup", controller.project.solo && controller.project.solo->groupScoped
                           ? juce::var(juce::int64(controller.project.solo->group))
                           : juce::var{}},
         {"soloAffected", soloAffected},
         {"audibleSoloChannel", juce::int64(audibleSoloChannel(controller.project))},
         {"restoreMuteAfterSolo", customization.restoreMuteAfterSolo},
         {"muteRemovedSteps", customization.muteRemovedSteps},
         {"scoreMuteKeepsLiveMidi", true},
         {"cloneIncludesScore", false},
         {"channelStateCaptureBusy", afterPluginState != nullptr},
         {"channelOperations", operations},
         {"channelEditCompletion", "document commit after state capture; read audio for preparation/health"},
         {"fillIntervals", juce::Array<juce::var>{2, 4, 8}},
         {"filterIsUndoable", false},
         {"groupEditsUndoable", true},
         {"filterPersistedWithProject", false}});
}
juce::var Studio::mcpRackOperation(const juce::var &args)
{
    validateMcpSchema(rackToolDefinition()["inputSchema"], args);
    const auto action = args["action"].toString();
    if (args.hasProperty("restoreMuteAfterSolo") && action != "configure")
        throw McpFailure("invalid_arguments", "Use configure to change the solo restore preference");
    if (args.hasProperty("muteRemovedSteps") && action != "configure")
        throw McpFailure("invalid_arguments",
                         "Use configure to change the removed-step retention preference");
    if (args.hasProperty("muteRemovedSteps") && !args["muteRemovedSteps"].isBool())
        throw McpFailure("invalid_arguments", "muteRemovedSteps must be a boolean");
    if (args.hasProperty("graphVisible") && action != "configure")
        throw McpFailure("invalid_arguments", "Use configure to show or hide the velocity Graph Editor");
    if (action == "job" || action == "cancel")
    {
        const auto operation = args["operation"].toString();
        if (action == "cancel")
        {
            const auto cancelled = cancelChannelEdit(operation);
            if (cancelled.failed())
                throw McpFailure("unavailable_operation", cancelled.getErrorMessage());
        }
        return channelEditState(operation);
    }
    if (args.hasProperty("operation"))
        throw McpFailure("invalid_arguments", "operation is only valid for Channel job/cancel");
    if (args.hasProperty("revision") && double(args["revision"]) != double(controller.revision))
        throw McpFailure("stale_revision", "Read current Rack state before changing targets");
    juce::Result result = juce::Result::ok();
    if (action == "clone" || action == "delete")
    {
        auto ids = selectedChannels;
        if (args.hasProperty("ids"))
        {
            ids.clear();
            for (const auto &id : *args["ids"].getArray())
                ids.push_back(Id(juce::int64(id)));
        }
        juce::String operation;
        const auto started = beginChannelEdit("channel." + action, ids, operation);
        if (started.failed())
            throw McpFailure("validation_failed", started.getErrorMessage());
        return channelEditState(operation);
    }
    if (action == "configure")
    {
        const bool soloPreference = args.hasProperty("restoreMuteAfterSolo");
        const bool stepPreference = args.hasProperty("muteRemovedSteps");
        if (!soloPreference && !stepPreference && !args.hasProperty("graphVisible") &&
            !args.hasProperty("graphProperty"))
            throw McpFailure("invalid_arguments",
                             "configure requires a supported preference or graphVisible");
        if (soloPreference || stepPreference)
        {
            if (soloPreference && stepPreference)
                throw McpFailure("invalid_arguments",
                                 "Change persistent Rack preferences in separate requests");
            // Persisting one preference and changing another property must not
            // produce a partial outcome if the later service refuses.
            for (const auto &property : args.getDynamicObject()->getProperties())
            {
                const auto key = property.name.toString();
                if (key != "action" && key != "revision" && key != "projectIdentity" &&
                    key != (stepPreference ? "muteRemovedSteps" : "restoreMuteAfterSolo"))
                    throw McpFailure(
                        "invalid_arguments",
                        "Change persistent Rack preferences and other properties in separate requests");
            }
        }
        if (stepPreference)
        {
            result = setStepMemoryPreference(bool(args["muteRemovedSteps"]));
            if (result.failed())
                throw McpFailure(customization.running || controller.hasGesture() ? "busy"
                                                                                  : "persistence_failed",
                                 result.getErrorMessage());
        }
        if (args.hasProperty("graphProperty"))
        {
            if (args.hasProperty("restoreMuteAfterSolo"))
                throw McpFailure("invalid_arguments",
                                 "Change graph view and persistent solo preferences in separate requests");
            result = setRackGraphProperty(args["graphProperty"].toString());
            if (result.failed())
                throw McpFailure("validation_failed", result.getErrorMessage());
        }
        if (args.hasProperty("restoreMuteAfterSolo"))
        {
            result = setSoloRestorePreference(bool(args["restoreMuteAfterSolo"]));
            if (result.failed())
                throw McpFailure("persistence_failed", result.getErrorMessage());
        }
        if (args.hasProperty("graphVisible"))
            setRackGraphVisible(bool(args["graphVisible"]));
    }
    else if (action == "scroll")
    {
        if (!windows[0])
            throw McpFailure("unavailable_target", "Channel Rack viewport is unavailable");
        if (!args.hasProperty("firstRow") && !args.hasProperty("firstStep"))
            throw McpFailure("invalid_arguments", "scroll requires firstRow or firstStep");
        auto *editor = static_cast<Editor *>(windows[0]->getContentComponent());
        const auto state = editor->rackViewport();
        editor->setRackViewport(int(args.getProperty("firstRow", state["firstRow"])),
                                int(args.getProperty("firstStep", state["firstStep"])));
    }
    else if (action == "filter")
    {
        const auto kind = args["kind"].toString();
        result = setRackFilter(kind, args.hasProperty("group") ? Id(juce::int64(args["group"])) : 0);
    }
    else if (action == "select" || action == "all")
    {
        std::vector<Id> ids;
        if (action == "all")
        {
            if (bool(args["allChannels"]))
                for (const auto &channel : controller.project.channels)
                    ids.push_back(channel.id);
            else
                ids = visibleRackChannels();
        }
        else
        {
            if (!args["ids"].isArray())
                throw McpFailure("invalid_arguments", "Channel selection needs an ids array");
            for (const auto &id : *args["ids"].getArray())
                ids.push_back(Id(juce::int64(id)));
        }
        result = selectRackChannels(ids, args.getProperty("mode", "replace").toString(),
                                    args.hasProperty("activeChannel") ? Id(juce::int64(args["activeChannel"]))
                                                                      : 0);
    }
    else if (action == "group")
    {
        CommandArgs values;
        values.ids = selectedChannels;
        if (args.hasProperty("ids"))
        {
            values.ids.clear();
            for (const auto &id : *args["ids"].getArray())
                values.ids.push_back(Id(juce::int64(id)));
        }
        values.text = args["name"].toString();
        if (values.text.isEmpty())
            throw McpFailure("invalid_arguments", "Group Selected needs a name");
        command("rack.group.assign", values);
        if (status != "Ready")
            throw McpFailure("validation_failed", status);
    }
    if (result.failed())
        throw McpFailure("unavailable_target", result.getErrorMessage());
    return rackState();
}
juce::Result Studio::Editor::verifyRackGroups(const juce::File &folder)
{
    auto fail = [](const juce::String &message) { return juce::Result::fail("Rack groups: " + message); };
    const auto all = s.visibleRackChannels();
    const auto active = s.selectedChannel;
    const auto group = s.controller.project.channelGroups.front().id;
    const std::vector<Id> members{all[4], all[6], all[8]};
    rackFilter.setSelectedId(int(group) + 3, juce::sendNotificationSync);
    if (s.visibleRackChannels() != members || s.selectedChannel != active)
        return fail("filter redirects active target");
    auto event = [&](float x, int row, bool right)
    {
        const juce::Point<float> point{x, float(44 + row * rackRowHeight() + 16)};
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), point,
                                juce::ModifierKeys(right ? juce::ModifierKeys::rightButtonModifier
                                                         : juce::ModifierKeys::leftButtonModifier),
                                1.f, 0.f, 0.f, 0.f, 0.f, this, this, juce::Time::getCurrentTime(), point,
                                juce::Time::getCurrentTime(), 1, false);
    };
    s.selectRackChannels({});
    mouseDown(event(10, 0, true));
    mouseDrag(event(10, 1, true));
    mouseUp(event(10, 1, true));
    if (s.selectedChannels != std::vector<Id>{members[0], members[1]} || s.selectedChannel != active)
        return fail("right selector drag / independent active target");
    s.selectRackChannels({});
    mouseDown(event(10, 0, true));
    mouseDrag(event(10, 2, true));
    mouseUp(event(10, 2, true));
    if (s.selectedChannels != members)
        return fail("rapid selector drag skips middle row");
    mouseDoubleClick(event(10, 1, false));
    if (s.selectedChannels != members || s.selectedChannel != active)
        return fail("double selector selects visible only");
    mouseDown(event(10, 1, false));
    mouseUp(event(10, 1, false));
    if (s.selectedChannels != std::vector<Id>{members[1]} || s.selectedChannel != members[1])
        return fail("single selector stable visible target");
    mouseDown(event(float(stepStart() + stepWidth() / 2), 0, false));
    mouseUp(event(float(stepStart() + stepWidth() / 2), 0, false));
    const auto *part = s.controller.project.pattern(s.selectedPattern);
    if (part->notes.size() != 1 || part->notes[0].channel != members[0] || !part->notes[0].step)
        return fail("filtered step row targets hidden full-list position");
    mouseDown(event(25, 1, false));
    mouseUp(event(25, 1, false));
    if (!s.controller.project.channel(members[1])->muted || s.controller.project.channel(all[1])->muted)
        return fail("filtered mute identity");
    mouseDown(event(65, 0, false));
    mouseUp(event(65, 0, false));
    if (!s.instrumentWindows.contains(members[0]) || s.instrumentWindows.contains(all[0]))
        return fail("filtered name opens wrong sampler");
    s.instrumentWindows.clear();
    mouseDown(event(float(rackLabelWidth() - 55), 1, false));
    mouseUp(event(float(rackLabelWidth() - 55), 1, false));
    if (s.focus != Scope::piano || s.selectedChannel != members[1])
        return fail("separate Piano Roll target");
    const auto beforeDrop = s.controller.project.state();
    const auto wav = folder.getChildFile("rack-drop.wav");
    {
        auto stream = wav.createOutputStream();
        if (!stream)
            return fail("sample drop fixture stream");
        stream->setPosition(0);
        stream->truncate();
        std::unique_ptr<juce::AudioFormatWriter> writer(
            juce::WavAudioFormat{}.createWriterFor(stream.release(), 48000., 1, 16, {}, 0));
        juce::AudioBuffer<float> pcm(1, 4800);
        for (int i = 0; i < 4800; ++i)
            pcm.setSample(0, i, .1f * std::sin(float(i) * .02f));
        if (!writer || !writer->writeFromAudioSampleBuffer(pcm, 0, 4800))
            return fail("sample drop fixture PCM");
    }
    filesDroppedWithModifiers({wav.getFullPathName()}, 65, 60, {});
    if (s.controller.project.channel(members[0])->source != wav.getFullPathName() ||
        s.controller.project.channel(all[0])->source !=
            beforeDrop.getChildWithName("CHANNELS").getChild(0)["source"].toString())
        return fail("filtered sample replacement target");
    s.controller.undoEdit();
    if (!s.controller.project.state().isEquivalentTo(beforeDrop))
        return fail("sample replacement exact Undo");
    auto phrase = *s.controller.project.pattern(s.selectedPattern);
    phrase.notes = {{1, members[1], .5, .5, 72, .7f, false}};
    juce::MemoryBlock midi;
    const auto mid = folder.getChildFile("rack-drop.mid");
    if (writeMidiScore(phrase, members[1], 120, 4, 4, midi).failed() ||
        !mid.replaceWithData(midi.getData(), midi.getSize()))
        return fail("MIDI drop fixture");
    filesDroppedWithModifiers({mid.getFullPathName()}, 65, 44 + rackRowHeight() + 16,
                              juce::ModifierKeys::shiftModifier);
    for (int i = 0; i < 2000 && s.midiFileBusy; ++i)
        dispatchMessages(2);
    const auto &imported = s.controller.project.pattern(s.selectedPattern)->notes;
    if (s.midiFileBusy ||
        std::none_of(imported.begin(), imported.end(),
                     [&](const auto &n) { return n.channel == members[1] && n.pitch == 72; }) ||
        std::any_of(imported.begin(), imported.end(), [&](const auto &n) { return n.channel == all[1]; }))
        return fail("filtered MIDI drop stable recipient");
    s.controller.undoEdit();
    if (!s.controller.project.state().isEquivalentTo(beforeDrop))
        return fail("MIDI drop exact Undo");
    s.focus = Scope::rack;
    if (!s.keyPressed(juce::KeyPress(juce::KeyPress::pageUpKey)) || s.rackFilter != "unsorted" ||
        !s.keyPressed(juce::KeyPress(juce::KeyPress::pageDownKey)) || s.rackGroup != group)
        return fail("PgUp/PgDn filter navigation");
    s.selectRackChannels(members);
    const auto before = s.controller.project.state();
    if (!s.keyPressed(juce::KeyPress('G', juce::ModifierKeys::altModifier, 0)) || !s.rackGroupWindow)
        return fail("Alt+G dialog");
    auto dialog = s.rackGroupWindow;
    dialog->getTextEditor("name")->setText("All", true);
    dispatchMessages(10);
    if (dialog->getButton("OK")->isEnabled())
        return fail("reserved group live validation");
    dialog->exitModalState(0);
    dispatchMessages(10);
    if (!s.controller.project.state().isEquivalentTo(before))
        return fail("cancel mutates project");
    s.rackGroupDialog("assign");
    dialog = s.rackGroupWindow;
    dialog->getTextEditor("name")->setText("Percussion", true);
    dispatchMessages(10);
    if (!dialog->getButton("OK")->isEnabled())
        return fail("valid group disabled");
    dialog->exitModalState(1);
    dispatchMessages(10);
    if (s.controller.project.channelGroups.size() != 2 || s.visibleRackChannels() != members)
        return fail("dialog confirmation creates and filters membership");
    s.controller.undoEdit();
    if (!s.controller.project.state().isEquivalentTo(before))
        return fail("dialog one Undo");
    s.setRackFilter("all");
    rackOffset = 0;
    for (int i = 0; i < 18; ++i)
        mouseWheelMove(event(300, 0, false), juce::MouseWheelDetails{0, -1, false, false, false});
    if (rackOffset <= 0 || rackOffset > int(all.size()) - rackRows())
        return fail("mouse-only vertical Rack scroll");
    rackFilter.setSelectedId(int(group) + 3, juce::sendNotificationSync);
    if (rackOffset != 0 || s.rackChannelAt(0)->id != members[0])
        return fail("filter resets scroll to stable first member");
    s.selectRackChannels(members);
    for (size_t i = 0; i < members.size(); ++i)
    {
        CommandArgs value;
        value.channel = members[i];
        value.enabled = false;
        s.command("channel.mute", value);
        value.pattern = s.selectedPattern;
        value.enabled = true;
        for (int step = 0; step < 16; ++step)
            if ((i == 0 && step % 4 == 0) || (i == 1 && step % 8 == 4) || (i == 2 && step % 2 == 0))
            {
                value.beat = step * .25;
                s.command("steps.set", value);
            }
    }
    CommandArgs placement;
    placement.pattern = s.selectedPattern;
    placement.length = 4;
    for (int bar = 0; bar < 3; ++bar)
    {
        placement.beat = bar * 4.;
        s.command("clip.add", placement);
    }
    s.status = "Channel Rack: right-drag selectors | Alt+G groups | PgUp/PgDn filters";
    juce::PNGImageFormat png;
    auto snapshot = [&](const char *name, float scale)
    {
        auto stream = folder.getChildFile(name).createOutputStream();
        if (!stream)
            return false;
        stream->setPosition(0);
        stream->truncate();
        return png.writeImageToStream(s.createComponentSnapshot(s.getLocalBounds(), true, scale), *stream);
    };
    if (!snapshot("rack-groups-workspace.png", 1.f))
        return fail("workspace snapshot");
    s.setSize(2560, 1392);
    s.resized();
    if (!snapshot("rack-groups-4k.png", 1.5f))
        return fail("4K at150% snapshot");
    if (!folder.getChildFile("rack-groups-gui.txt")
             .replaceWithText(
                 "PASS actual component callbacks: ComboBox filters; selector right drag, double/all and "
                 "left active; "
                 "filtered step/mute/name/sampler/PR identity; contextual PgUp/PgDn/Alt+G; name "
                 "validation/cancel/confirm/exact Undo; "
                 "mouse-only scrolling/filter reset;1080 logical and4K at150% component snapshots. "
                 "No native OS input, physical MIDI or listening.\n"))
        return fail("evidence write");
    return juce::Result::ok();
}
juce::Result Studio::verifyRackGroups(const juce::File &folder)
{
    stopTimer();
    audio.stop();
    releaseTypingNotes();
    customization = Customization::familiarDefaults();
    auto project = Project::empty();
    const auto group = project.allocate();
    project.channelGroups.push_back({group, "Drums"});
    for (int i = 4; i < 24; ++i)
        project.channels.push_back(
            {project.allocate(), "Percussion " + juce::String(i), "builtin:kick", 1, .7f, 0, false});
    for (int i : {4, 6, 8})
        project.channels[size_t(i)].group = group;
    project.channels[4].name = "Kick";
    project.channels[6].name = "Snare";
    project.channels[6].source = "builtin:snare";
    project.channels[8].name = "Hat";
    project.channels[8].source = "builtin:hat";
    controller.replace(std::move(project));
    selectedPattern = controller.project.patterns[0].id;
    selectedChannel = controller.project.channels[0].id;
    selectedNotes.clear();
    selectedClips.clear();
    focus = Scope::rack;
    setSize(1920, 1040);
    maximisedEditor = -1;
    refresh();
    resized();
    auto result = static_cast<Editor *>(windows[0]->getContentComponent())->verifyRackGroups(folder);
    if (result.failed())
        return result;
    auto *device = dynamic_cast<SyntheticDevice *>(
        audio.engine.getDeviceManager().deviceManager.getCurrentAudioDevice());
    if (!device)
        return juce::Result::fail("Rack held-take verification requires synthetic device");
    juce::AudioBuffer<float> block(2, 256);
    for (bool pattern : {true, false})
    {
        audio.stop();
        patternMode = pattern;
        selectedChannel = controller.project.channels[0].id;
        if (!pattern && controller.project.placements.empty())
            controller.project.placements.push_back(
                {controller.project.allocate(), selectedPattern, 0, 0, 4, 0});
        audio.setMidiTarget(selectedChannel);
        auto prepared = audio.rebuild(controller.project, patternMode, selectedPattern);
        for (int i = 0; i < 1000 && AudioEngine::preparationPending(prepared); ++i)
        {
            dispatchMessages(2);
            prepared = audio.rebuild(controller.project, patternMode, selectedPattern);
        }
        if (prepared.failed())
            return prepared;
        needsRebuild = false;
        audio.seekBeat(0);
        for (int i = 0; i < 8; ++i)
        {
            device->process(block);
            dispatchMessages(1);
        }
        recordArmed = false;
        audio.setCountInBars(0);
        record.onClick();
        play.onClick();
        if (!midiTaking)
            return juce::Result::fail("Rack held-take fixture did not begin");
        for (int i = 0; i < 8; ++i)
        {
            device->process(block);
            dispatchMessages(1);
            drainMidiTake();
        }
        audio.midiInput.handleIncomingMidiMessage(nullptr,
                                                  juce::MidiMessage::noteOn(1, 60, juce::uint8(100)));
        for (int i = 0; i < 8; ++i)
        {
            device->process(block);
            dispatchMessages(1);
            drainMidiTake();
        }
        const auto active = selectedChannel;
        const auto take = takePattern;
        CommandArgs args;
        args.ids = {controller.project.channels[4].id, controller.project.channels[6].id};
        args.text = pattern ? "Pattern live group" : "Song live group";
        command("rack.group.assign", args);
        selectRackChannels(args.ids);
        setRackFilter("unsorted");
        Macro macro{"macro.rack-held", "Held take group", {{"rack.group.assign", {}, true}}};
        macro.steps[0].parameters.text = "Macro live";
        if (customization.addMacro(macro).failed())
            return juce::Result::fail("Rack held macro fixture");
        runMacro(macro.id);
        CommandArgs fill;
        fill.pattern = selectedPattern;
        fill.ids = args.ids;
        command("steps.fill2", fill);
        Macro fillMacro{"macro.fill-held", "Held take fill", {{"steps.fill4", {}, true}}};
        if (customization.addMacro(fillMacro).failed())
            return juce::Result::fail("Held fill macro fixture");
        runMacro(fillMacro.id);
        CommandArgs solo;
        solo.channel = args.ids.front();
        command("channel.solo", solo);
        command("channel.unsolo", {});
        solo.soloScope = "group";
        command("channel.solo", solo);
        if (status != "Ready" || !controller.project.solo || !controller.project.solo->groupScoped)
            return juce::Result::fail("Group solo failed during a held take");
        command("channel.unsolo", {});
        CommandArgs lock;
        lock.channel = args.ids.front();
        command("channel.muteLock", lock);
        if (status != "Ready" || !controller.project.channel(lock.channel)->muteLocked)
            return juce::Result::fail("Mute lock failed during a held take");
        lock.enabled = false;
        command("channel.muteLock", lock);
        CommandArgs reorder;
        reorder.ids = args.ids;
        command("channel.moveDown", reorder);
        command("channel.moveUp", reorder);
        Macro reorderMacro{"macro.reorder-held",
                           "Reorder held take",
                           {{"channel.moveDown", {}, true}, {"channel.moveUp", {}, true}}};
        if (customization.addMacro(reorderMacro).failed())
            return juce::Result::fail("Held reorder macro fixture");
        runMacro(reorderMacro.id);
        juce::String unsafeClone;
        if (beginChannelEdit("channel.clone", {active}, unsafeClone).wasOk() || !midiTaking)
            return juce::Result::fail("Structural clone committed or interrupted an active recording take");
        if (!midiTaking || selectedChannel != active || takePattern != take || !audio.playing())
            return juce::Result::fail(
                "Rack metadata/filter/bulk macro interrupted active take or MIDI target");
        for (int i = 0; i < 8; ++i)
        {
            device->process(block);
            dispatchMessages(1);
            drainMidiTake();
        }
        audio.midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 60));
        for (int i = 0; i < 4; ++i)
        {
            device->process(block);
            dispatchMessages(1);
            drainMidiTake();
        }
        const auto count = controller.project.pattern(selectedPattern)->notes.size();
        stop.onClick();
        const auto &notes = controller.project.pattern(selectedPattern)->notes;
        if (midiTaking || notes.size() != count + 1 || notes.back().channel != active)
        {
            juce::String detail;
            for (size_t i = count; i < notes.size(); ++i)
                detail += " [beat=" + juce::String(notes[i].beat, 16) +
                          " length=" + juce::String(notes[i].length, 16) +
                          " pitch=" + juce::String(notes[i].pitch) + "]";
            return juce::Result::fail("Rack held take: mode=" + juce::String(pattern ? "Pattern" : "Song") +
                                      " before=" + juce::String(int(count)) + " after=" +
                                      juce::String(int(notes.size())) + " active=" + juce::String(active) +
                                      " last=" + juce::String(notes.empty() ? 0 : notes.back().channel) +
                                      " status=" + status + detail);
        }
    }
    recordArmed = false;
    folder.getChildFile("rack-groups-held-take.txt")
        .replaceWithText("PASS Pattern and Song: real UI Rec/Play/Stop handlers, injected held MIDI, shared "
                         "group/filter/bulk selection/group macro preserve take and original single "
                         "recipient; one recorded note committed. No physical MIDI/listening.\n");
    folder.getChildFile("step-fill-held-take.txt")
        .replaceWithText("PASS Pattern/Song committed-part fill2 and current-selection fill4 macro during "
                         "held injected MIDI: take and original recipient survive, one recorded note "
                         "committed. No physical MIDI/listening.\n");
    folder.getChildFile("channel-solo-held-take.txt")
        .replaceWithText("PASS Pattern/Song solo/restore during held injected MIDI: take, recipient and "
                         "transport retained; "
                         "one recorded note committed. No physical MIDI/listening.\n");
    folder.getChildFile("channel-management-held-take.txt")
        .replaceWithText("PASS Pattern/Song Channel reordering and current-selection reorder macro retain "
                         "held take/recipient/transport; structural clone refuses without committing take; "
                         "one recorded note. No physical MIDI/listening.\n");
    setSize(1920, 1040);
    resized();
    return result;
}
} // namespace vibera
