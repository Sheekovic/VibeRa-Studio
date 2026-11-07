// SPDX-License-Identifier: AGPL-3.0-or-later
// Included inside namespace vibera by StudioMixer.cpp; sole CONTROL owner.
void Studio::reconcileMixerSelection()
{
    if (!mixerInsert(controller.project, selectedInsert)) selectedInsert = 0;
    if (mixerSelection.project != controller.projectIdentity)
        mixerSelection = {controller.projectIdentity, {selectedInsert}, selectedInsert, selectedInsert};
    std::erase_if(mixerSelection.ids, [&](int id) { return !mixerInsert(controller.project, id); });
    if (!mixerSelection.contains(selectedInsert)) mixerSelection.ids.push_back(selectedInsert);
    std::vector<int> ordered;
    for (const auto &insert : controller.project.mixer)
        if (mixerSelection.contains(insert.index)) ordered.push_back(insert.index);
    mixerSelection.ids.swap(ordered);
    mixerSelection.active = selectedInsert;
    if (!mixerInsert(controller.project, mixerSelection.anchor)) mixerSelection.anchor = selectedInsert;
}
juce::Result Studio::selectMixerMany(const std::vector<int> &ids, const juce::String &mode,
                                     std::optional<int> active)
{
    if (controller.hasGesture() || noteSlicePending() || noteStretchPending())
        return juce::Result::fail("mixer.selection.gestureOwned: Finish/cancel the active edit before selecting");
    MixerSelection prepared;
    try
    {
        if (auto result = MixerSelection::prepare(controller.project, controller.projectIdentity,
                                                   mixerSelection, ids, mode, active, prepared); result.failed())
            return result;
    }
    catch (...)
    { return juce::Result::fail("mixer.selection.preparationFailed: Selection unchanged"); }
    const int previous = selectedInsert;
    mixerSelection = std::move(prepared);
    selectedInsert = mixerSelection.active;
    if (previous != selectedInsert)
        static_cast<Editor *>(windows[3]->getContentComponent())->resetMixerEffectView();
    focus = Scope::mixer;
    showEditor(3);
    status = "Selected " + juce::String(int(mixerSelection.ids.size())) + " Mixer insert(s)";
    refresh();
    return juce::Result::ok();
}
juce::Result Studio::selectMixerGroup(const juce::String &id)
{
    const auto *group = mixerGroup(controller.project, id);
    if (!group) return juce::Result::fail("mixer.group.unavailable: Display group no longer exists");
    if (group->members.empty()) return juce::Result::fail("mixer.group.empty: Display group has no members");
    const bool retainSource = std::find(group->members.begin(), group->members.end(), selectedInsert) != group->members.end();
    std::vector<int> ordered;
    for (const auto &insert : controller.project.mixer)
        if (std::find(group->members.begin(), group->members.end(), insert.index) != group->members.end())
            ordered.push_back(insert.index);
    return selectMixerMany(ordered, "replace", retainSource ? selectedInsert : ordered.front());
}
juce::var Studio::mixerOrganisationState() const
{
    auto *state = new juce::DynamicObject;
    juce::Array<juce::var> selected, groups;
    for (int id : mixerSelection.ids) selected.add(id);
    state->setProperty("selected", selected);
    state->setProperty("active", selectedInsert);
    state->setProperty("anchor", mixerSelection.anchor);
    state->setProperty("activeGroup", mixerGroupFor(controller.project, selectedInsert));
    for (const auto &group : controller.project.mixerGroups)
    {
        auto *row = new juce::DynamicObject;
        juce::Array<juce::var> members;
        for (const auto &insert : controller.project.mixer)
            if (std::find(group.members.begin(), group.members.end(), insert.index) != group.members.end())
                members.add(insert.index);
        row->setProperty("id", group.id); row->setProperty("name", group.name);
        row->setProperty("colour", juce::int64(group.colour)); row->setProperty("members", members);
        row->setProperty("solo", controller.project.mixerSolo && controller.project.mixerSolo->group == group.id);
        groups.add(juce::var(row));
    }
    state->setProperty("groups", groups);
    state->setProperty("projectIdentity", controller.projectIdentity);
    state->setProperty("revision", juce::int64(controller.revision));
    state->setProperty("selectionUndoable", false);
    state->setProperty("selectionPersisted", false);
    state->setProperty("groupMeaning", "Display/selection membership, never an implicit bus; route explicitly");
    state->setProperty("insertCreationAvailable",
                       controller.project.mixer.size() < MixerNativeIdentity::capacity &&
                           !controller.project.mixerSolo);
    state->setProperty("insertDeletion", mixerDeletionReferences(controller.project, selectedInsert));
    return juce::var(state);
}
void Studio::mixerOrganisationDialog(const juce::String &operation, const juce::String &groupId)
{
    juce::String code;
    if (auto r = admitMixerInsertOrder(code); r.failed()) { status = r.getErrorMessage(); return; }
    const auto *group = mixerGroup(controller.project, groupId);
    if (operation == "mixer.group.rename" && !group) { status = "mixer.group.unavailable: Select a group first"; return; }
    auto *dialog = new juce::AlertWindow(operation == "mixer.group.create" ? "Group selected Mixer inserts"
        : operation == "mixer.selection.rename" ? "Rename selected Mixer inserts" : "Rename Mixer group",
        operation == "mixer.selection.rename" ? "Prefix +1-based number in display order. Routing identities stay unchanged."
        : "Groups organise selection only. They do not create a bus or change audio routing.",
        juce::MessageBoxIconType::NoIcon, this);
    dialog->setLookAndFeel(&look);
    dialog->addTextEditor("name", group ? group->name : operation == "mixer.group.create" ? "Drums" : "Track", "Name");
    dialog->getTextEditor("name")->setInputRestrictions(operation == "mixer.selection.rename" ? 55 : 64);
    dialog->addButton("Apply", 1, juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    const auto identity = controller.projectIdentity;
    const auto revision = controller.revision;
    const auto targets = mixerSelection.ids;
    const auto source = selectedInsert;
    dialog->enterModalState(true, juce::ModalCallbackFunction::create(
        [safe = juce::Component::SafePointer<Studio>(this), d = juce::Component::SafePointer<juce::AlertWindow>(dialog),
         identity, revision, targets, source, operation, groupId](int choice)
        {
            if (!safe || !d || choice != 1) return;
            if (safe->controller.projectIdentity != identity || safe->controller.revision != revision ||
                safe->mixerSelection.ids != targets || safe->selectedInsert != source)
            { safe->status = "mixer.group.staleIntent: Mixer selection/project changed; reopen the dialog"; return; }
            CommandArgs args;
            args.text = d->getTextEditorContents("name").trim();
            if (operation == "mixer.group.rename") args.mixerGroup = groupId;
            else args.ids.assign(targets.begin(), targets.end());
            safe->command(operation, args);
        }), true);
    bringWindowToFront(*dialog); dialog->getTextEditor("name")->grabKeyboardFocus(); dialog->getTextEditor("name")->selectAll();
}
void Studio::mixerOrganisationMenu(int index)
{
    if (!mixerInsert(controller.project, index)) return;
    const auto identity = controller.projectIdentity;
    const auto revision = controller.revision;
    const auto targets = mixerSelection.ids;
    const auto source = selectedInsert;
    const auto groupId = mixerGroupFor(controller.project, index);
    const auto *group = mixerGroup(controller.project, groupId);
    const bool canGroup = !targets.empty() && std::find(targets.begin(), targets.end(), 0) == targets.end();
    const bool membershipAvailable = !(controller.project.mixerSolo && controller.project.mixerSolo->group.isNotEmpty());
    juce::PopupMenu menu, assign, select, colour, selectedColours;
    menu.addSectionHeader(juce::String(int(targets.size())) + " selected | active source " + juce::String(source));
    menu.addItem(1, "Select all inserts (Ctrl+A)");
    menu.addItem(4, "Rename selected... (F2)", !targets.empty());
    selectedColours.addItem(500, "Follow theme");
    colour.addItem(100, "Follow theme");
    for (int i = 0; i < 8; ++i)
    {
        selectedColours.addColouredItem(501 + i, "Colour " + juce::String(i + 1), channelColour(i));
        colour.addColouredItem(101 + i, "Colour " + juce::String(i + 1), channelColour(i));
    }
    menu.addSubMenu("Colour selected inserts", selectedColours);
    menu.addSeparator();
    menu.addItem(3, "New group from selected... (Alt+G)", canGroup && membershipAvailable);
    menu.addItem(5, "Ungroup selected", canGroup && membershipAvailable);
    if (!membershipAvailable) menu.addItem(98,"Restore group solo before editing membership",false);
    std::vector<juce::String> groupIds;
    for (const auto &entry : controller.project.mixerGroups)
    {
        const int offset = int(groupIds.size()); groupIds.push_back(entry.id);
        assign.addItem(1000 + offset, entry.name);
        select.addItem(2000 + offset, entry.name, !entry.members.empty());
    }
    menu.addSubMenu("Assign selected to group", assign, canGroup && membershipAvailable && !groupIds.empty());
    menu.addSubMenu("Select group", select, !groupIds.empty());
    if (group)
    {
        menu.addSectionHeader(group->name);
        menu.addItem(2, "Select this group", !group->members.empty());
        menu.addItem(6, "Rename group..."); menu.addSubMenu("Group marker colour", colour);
        menu.addItem(13, mixerGroupColourActionLabel(mixerInsert(controller.project, index)->colour));
        menu.addItem(8, controller.project.mixerSolo ? "Mute group (ends solo restore)" : "Mute group");
        menu.addItem(9, controller.project.mixerSolo ? "Unmute group (ends solo restore)" : "Unmute group");
        menu.addItem(10, "Solo group", !group->members.empty());
        menu.addItem(11, "Solo group with connected inputs", !group->members.empty());
        menu.addItem(12, "Restore group solo", controller.project.mixerSolo && controller.project.mixerSolo->group == groupId);
        menu.addItem(7, "Dissolve display group", !(controller.project.mixerSolo && controller.project.mixerSolo->group == groupId));
    }
    menu.showMenuAsync({}, [safe = juce::Component::SafePointer<Studio>(this), identity, revision, targets, source, index, groupId, groupIds](int choice)
    {
        if (!safe || choice == 0) return;
        if (safe->controller.projectIdentity != identity || safe->controller.revision != revision ||
            safe->mixerSelection.ids != targets || safe->selectedInsert != source)
        { safe->status = "mixer.group.staleIntent: Reopen the Mixer group menu"; return; }
        if (choice == 1)
        { safe->performMixerControlAction("mixer.selectAll"); return; }
        if (choice == 2 || choice >= 2000)
        {
            const int offset = choice - 2000;
            if (choice != 2 && (offset < 0 || offset >= int(groupIds.size()))) return;
            auto result = safe->selectMixerGroup(choice == 2 ? groupId : groupIds[size_t(offset)]);
            if (result.failed()) safe->status = result.getErrorMessage(); return;
        }
        if (choice == 3 || choice == 4 || choice == 6)
        { safe->mixerOrganisationDialog(choice == 3 ? "mixer.group.create" : choice == 4 ? "mixer.selection.rename" : "mixer.group.rename", groupId); return; }
        CommandArgs args;
        if (choice >= 1000 || choice == 5)
        {
            const int offset = choice - 1000;
            if (choice != 5 && (offset < 0 || offset >= int(groupIds.size()))) return;
            args.mixerGroup = choice == 5 ? juce::String{} : groupIds[size_t(offset)];
            args.ids.assign(targets.begin(), targets.end()); safe->command("mixer.group.assign", args); return;
        }
        if (choice >= 500)
        {
            args.ids.assign(targets.begin(), targets.end());
            args.value = choice == 500 ? 0 : double(channelColour(choice - 501).getARGB());
            safe->command("mixer.selection.colour", args); return;
        }
        args.mixerGroup = groupId;
        if (choice == 13)
        {
            args.mixer = index; // Original clicked member, never later selection/row.
            safe->command("mixer.group.colourMembers", args); return;
        }
        if (choice >= 100)
        {
            args.value = choice == 100 ? 0 : double(channelColour(choice - 101).getARGB());
            safe->command("mixer.group.colour", args); return;
        }
        args.enabled = choice != 9;
        safe->command(choice == 7 ? "mixer.group.delete" : choice == 8 || choice == 9 ? "mixer.group.mute"
                      : choice == 10 ? "mixer.group.solo" : choice == 11 ? "mixer.group.soloConnected" : "mixer.group.unsolo", args);
    });
}
