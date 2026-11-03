// SPDX-License-Identifier: AGPL-3.0-or-later
// Included by StudioMixer.cpp, inside namespace vibera. Owned component test only.
juce::Result Studio::verifyMixerOrganisation(const juce::File &output)
{
    if (auto r = output.createDirectory(); r.failed())
        return r;
    auto fail = [&](const juce::String &message)
    {
        output.getChildFile("failure.txt").replaceWithText(message);
        return juce::Result::fail(message);
    };
    if (audio.playing() || controller.hasGesture() || routingPending() ||
        audio.channelRoutingReadback().overlayActive)
        return fail("Fixture requires its fresh stopped app; never replace active/user projection");
    stopTimer();
    controller.replace(Project::demo());
    refresh();
    auto *editor = static_cast<Editor *>(windows[3]->getContentComponent());
    showEditor(3);
    maximiseEditor(3);
    refresh();
    auto clickName = [&](int id, int flags)
    {
        const auto point = editor->mixerNameBounds(id).getCentre().toFloat();
        const auto now = juce::Time::getCurrentTime();
        const auto event = juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), point,
                                            juce::ModifierKeys(flags), 1, 0, 0, 0, 0, editor, editor, now,
                                            point, now, 1, true);
        editor->mouseDown(event);
        editor->mouseUp(event);
    };
    if (selectMixerInsert(1).failed())
        return fail("Initial actual source selection");
    const auto baseline = controller.project.state();
    clickName(3, juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::ctrlModifier);
    if (mixerSelection.ids != std::vector<int>({1, 3}) || selectedInsert != 3 ||
        !controller.project.state().isEquivalentTo(baseline))
        return fail("Actual name Ctrl-click selects two stable inserts without document mutation");
    clickName(5, juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier);
    if (mixerSelection.ids != std::vector<int>({3, 4, 5}) || selectedInsert != 5)
        return fail("Actual name Shift-click range and active source");
    if (selectMixerMany({1, 3}, "replace", 1).failed())
        return fail("Prepare multi-name dialog selection");
    performMixerControlAction("mixer.rename");
    auto *dialog = dynamic_cast<juce::AlertWindow *>(juce::Component::getCurrentlyModalComponent());
    if (!dialog || !dialog->getTextEditor("name"))
        return fail("Actual F2 multi-name interface opens");
    dialog->getTextEditor("name")->setText("Percussion");
    dialog->exitModalState(0);
    dispatchMessages(20);
    if (!controller.project.state().isEquivalentTo(baseline))
        return fail("Actual multi-name Cancel preserves model/history");
    performMixerControlAction("mixer.group.create");
    dialog = dynamic_cast<juce::AlertWindow *>(juce::Component::getCurrentlyModalComponent());
    if (!dialog || !dialog->getTextEditor("name"))
        return fail("Actual group create dialog");
    dialog->getTextEditor("name")->setText("Drums");
    dialog->exitModalState(1);
    dispatchMessages(20);
    if (controller.project.mixerGroups.size() != 1 ||
        controller.project.mixerGroups.front().members != std::vector<int>({1, 3}))
        return fail("Actual UI groups original stable selection through canonical Controller");
    const auto id = controller.project.mixerGroups.front().id;
    if (controller.undoEditChecked().failed() || !controller.project.state().isEquivalentTo(baseline) ||
        controller.redoEditChecked().failed() || controller.project.mixerGroups.front().id != id)
        return fail("Actual UI one Undo/Redo preserves exact group UUID");
    if (selectMixerGroup(id).failed() || mixerSelection.ids != std::vector<int>({1, 3}) ||
        selectedInsert != 1)
        return fail("Shared group select retains existing active source");
    mixerOrganisationDialog("mixer.group.rename", id);
    dialog = dynamic_cast<juce::AlertWindow *>(juce::Component::getCurrentlyModalComponent());
    if (!dialog)
        return fail("Actual group rename dialog opens");
    dialog->getTextEditor("name")->setText("Must not apply after selection changes");
    if (selectMixerInsert(2).failed())
        return fail("Independent selection while model revision remains unchanged");
    dialog->exitModalState(1);
    dispatchMessages(20);
    if (mixerGroup(controller.project, id)->name != "Drums" || !status.startsWith("mixer.group.staleIntent"))
        return fail("Stale selection-bound group dialog refuses without retargeting");
    auto args = juce::JSON::parse(R"({"action":"selectMany","ids":[1,3],"active":1})");
    const auto semantic = mcpCall("vibera_mixer", args);
    if (bool(semantic["isError"]) || mixerSelection.ids != std::vector<int>({1, 3}) || selectedInsert != 1)
        return fail("Actual semantic selection reuses same shared state");
    CommandArgs memberColour;
    memberColour.mixer = 1;
    memberColour.value = double(0xff735bd8u);
    command("mixer.colour", memberColour);
    memberColour.mixer = 3;
    memberColour.value = double(0xff5ac8d8u);
    command("mixer.colour", memberColour);
    const auto beforeMemberColour = controller.project;
    const auto colourSelection = mixerSelection.ids;
    auto expectedMemberColour = beforeMemberColour;
    expectedMemberColour.mixer[3].colour = 0xff735bd8u;
    if (!performMixerControlAction("mixer.group.colourMembers") || status != "Ready" ||
        !controller.project.state().isEquivalentTo(expectedMemberColour.state()) ||
        mixerSelection.ids != colourSelection || selectedInsert != 1)
        return fail(
            "Actual shared UI group colour action preserves selection/source and every non-colour field");
    if (controller.undoEditChecked().failed() ||
        !controller.project.state().isEquivalentTo(beforeMemberColour.state()) ||
        controller.redoEditChecked().failed() ||
        !controller.project.state().isEquivalentTo(expectedMemberColour.state()))
        return fail("Actual UI sampled group colour has one exact Undo/Redo");
    const juce::String foreignColourOwner = "original.ui.groupColour.foreign";
    if (!controller.beginGesture(foreignColourOwner))
        return fail("Own foreign group-colour test gesture");
    const auto foreignBaseline = controller.project.state();
    performMixerControlAction("mixer.group.colourMembers");
    if (status == "Ready" || !controller.project.state().isEquivalentTo(foreignBaseline) ||
        controller.gestureOwner() != foreignColourOwner ||
        controller.cancelGestureChecked(foreignColourOwner).failed())
        return fail("Actual UI cannot steal a foreign edit lease for group colours");
    memberColour.mixer = 1;
    memberColour.value = 0;
    command("mixer.colour", memberColour);
    auto colourArgs = juce::JSON::parse(R"({"command":"mixer.group.colourMembers","args":{"mixer":1}})");
    colourArgs["args"].getDynamicObject()->setProperty("mixerGroup", id);
    const auto semanticColours = mcpCall("vibera_command", colourArgs);
    if (bool(semanticColours["isError"]) || controller.project.mixer[1].colour != 0 ||
        controller.project.mixer[3].colour != 0 || mixerSelection.ids != colourSelection ||
        selectedInsert != 1)
        return fail(
            "Actual semantic command uses the same stored theme sentinel and leaves source/selection stable");
    if (editor->mixerNameBounds(1).intersects(editor->mixerRouteButtonBounds(1)) ||
        editor->mixerNameBounds(1).intersects(editor->mixerMuteBounds(1)))
        return fail("Selection/name target remains separate from sends and enable controls");
    if (!output.getChildFile("component-verification.json")
             .replaceWithText(juce::JSON::toString(mixerOrganisationState())))
        return fail("Owned component readback evidence");
    const auto capture = [&](const char *name, float scale)
    {
        const auto image = createComponentSnapshot(getLocalBounds(), true, scale);
        const auto file = output.getChildFile(name);
        if (!image.isValid() || file.exists())
            return false;
        auto stream = file.createOutputStream();
        if (!stream || !stream->setPosition(0) || stream->truncate().failed() ||
            !juce::PNGImageFormat{}.writeImageToStream(image, *stream))
            return false;
        stream->flush();
        return stream->getStatus().wasOk();
    };
    const auto meterTextSeparated = [&]
    {
        for (const auto &insert : controller.project.mixer)
        {
            const auto header = editor->mixerMeterHeaderBounds(insert.index);
            const auto reset = editor->mixerMeterBounds(insert.index);
            const auto gain = editor->mixerGainReadoutBounds(insert.index);
            const auto zero = editor->mixerMeterTickLabelBounds(insert.index, 0);
            if (header.isEmpty() || zero.isEmpty() || !reset.contains(header) ||
                zero.getY() - header.getBottom() < 3 || header.intersects(zero))
                return false;
            std::vector<juce::Rectangle<int>> labels;
            for (int db : {0, -12, -24, -36, -48, -60})
            {
                const auto bounds = editor->mixerMeterTickLabelBounds(insert.index, db);
                if (bounds.isEmpty() || bounds.intersects(header) || bounds.intersects(gain))
                    return false;
                for (const auto &earlier : labels)
                    if (bounds.intersects(earlier))
                        return false;
                labels.push_back(bounds);
            }
        }
        return true;
    };
    setSize(1920, 1044);
    if (maximisedEditor != 3)
        maximiseEditor(3);
    refresh();
    if (!meterTextSeparated())
        return fail("1080p actual header/zero tick gap and all signal labels remain distinct from "
                    "header/each other/gain readout");
    if (!capture("mixer-groups-1080p.png", 1))
        return fail("Actual populated group1080p component capture");
    setSize(2560, 1392);
    refresh();
    if (!meterTextSeparated())
        return fail("4K150 actual logical header/zero tick gap and all signal labels remain distinct");
    if (!capture("mixer-groups-4k-150.png", 1.5f))
        return fail("Actual group4K150 component render, not physical DPI");
    if (saveProject(controller.project, output.getChildFile("mixer-groups-showcase.vibra")).failed())
        return fail("Actual populated group project persistence");
    return juce::Result::ok();
}
