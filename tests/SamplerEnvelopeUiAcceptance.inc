// SPDX-License-Identifier: AGPL-3.0-or-later
// Included in the existing real sampler-window component workflow.
{
    auto *panel =
        dynamic_cast<SamplerModulationPanel *>(controls->findChildWithID("instrument.sampler.modulation"));
    if (!panel)
        return fail("Actual sampler modulation panel missing");
    auto *destination =
        dynamic_cast<juce::ComboBox *>(panel->findChildWithID("instrument.sampler.modulation.target"));
    auto *shape =
        dynamic_cast<juce::ComboBox *>(panel->findChildWithID("instrument.sampler.modulation.lfoShape"));
    auto *enabled =
        dynamic_cast<juce::ToggleButton *>(panel->findChildWithID("instrument.sampler.modulation.enabled"));
    auto *attack =
        dynamic_cast<juce::Slider *>(panel->findChildWithID("instrument.sampler.modulation.attack"));
    auto *amount =
        dynamic_cast<juce::Slider *>(panel->findChildWithID("instrument.sampler.modulation.amount"));
    if (!destination || !shape || !enabled || !attack || !amount || destination->getNumItems() != 5)
        return fail("Actual sampler destination/shape/envelope controls unavailable");
    const auto starting = controller.project.state();
    destination->setSelectedId(1, juce::sendNotification);
    window->refreshFromProject();
    if (destination->getSelectedId() != 1)
        return fail("Sampler destination projection erased queued choice");
    dispatchMessages(20);
    if (window->currentModulationTarget() != 0 || selectedChannel != second ||
        !controller.project.state().isEquivalentTo(starting))
        return fail("Sampler destination navigation changed document or active MIDI recipient");
    enabled->setToggleState(true, juce::dontSendNotification);
    enabled->onClick();
    amount->setValue(1, juce::sendNotificationSync);
    attack->onDragStart();
    attack->setValue(.125, juce::sendNotificationSync);
    attack->setValue(.25, juce::sendNotificationSync);
    attack->onDragEnd();
    const auto edited = controller.project.state();
    const auto &m = controller.project.channel(first)->sampler.modulation[0];
    if (!m.enabled || m.amount != 1 || m.attack != .25 ||
        controller.project.channel(second)->sampler.modulation[0].enabled || selectedChannel != second)
        return fail("Sampler envelope components lost fixed owner or canonical values");
    for (int i = 0; i < 3; ++i)
        controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(starting))
        return fail("Sampler envelope three actions did not Undo exactly");
    for (int i = 0; i < 3; ++i)
        controller.redoEdit();
    if (!controller.project.state().isEquivalentTo(edited))
        return fail("Sampler envelope exact Redo failed");
    for (int i = 0; i < 3; ++i)
        controller.undoEdit();
    window->refreshFromProject();
    attack->onDragStart();
    attack->setValue(.4, juce::sendNotificationSync);
    controls->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    attack->setValue(.6, juce::sendNotificationSync);
    attack->onDragEnd();
    if (!controller.project.state().isEquivalentTo(starting) || controller.hasGesture())
        return fail("Continuing physical stroke reapplied a cancelled envelope edit");
    attack->onDragStart();
    attack->setValue(.3, juce::sendNotificationSync);
    destination->setSelectedId(5, juce::sendNotificationSync);
    attack->setValue(.7, juce::sendNotificationSync);
    attack->onDragEnd();
    if (controller.project.channel(first)->sampler.modulation[0].attack != .3 ||
        controller.project.channel(first)->sampler.modulation[4].attack != .01 || selectedChannel != second)
        return fail("Continuing old envelope stroke edited a newly selected destination");
    controller.undoEdit();
    window->refreshFromProject();
    shape->setSelectedId(2, juce::sendNotification);
    window->refreshFromProject();
    if (shape->getSelectedId() != 2)
        return fail("Sampler LFO shape projection erased queued choice");
    dispatchMessages(20);
    if (controller.project.channel(first)->sampler.modulation[4].lfoShape != 1)
        return fail("Sampler LFO shape callback did not edit its target");
    controller.undoEdit();
    destination->setSelectedId(2, juce::sendNotificationSync);
    if (enabled->isEnabled() || attack->isEnabled() || !shape->isEnabled())
        return fail("Pan incorrectly exposes an envelope or hides its supported LFO");
    destination->setSelectedId(3, juce::sendNotificationSync);
    const auto originalBounds = window->getBounds();
    for (const auto width : {600, 980})
    {
        window->setSize(width, 800);
        window->refreshFromProject();
        const auto image = panel->createComponentSnapshot(panel->getLocalBounds());
        auto stream =
            folder.getChildFile("sampler-envelope-" + juce::String(width) + ".png").createOutputStream();
        if (!stream || !stream->setPosition(0) || stream->truncate().failed() ||
            !juce::PNGImageFormat().writeImageToStream(image, *stream))
            return fail("Fresh sampler envelope component capture failed");
    }
    window->setBounds(originalBounds);
    window->refreshFromProject();
    if (!controller.project.state().isEquivalentTo(starting))
        return fail("Sampler modulation component test failed to restore the original fixture");

    {
        const auto beforeAutomation = controller.project.state();
        const auto revisionBeforePopup = controller.revision;
        const auto recipientBeforePopup = selectedChannel;
        for (auto *control : std::array<juce::Component *, 3>{attack, enabled, shape})
        {
            const auto point = control->getLocalBounds().getCentre().toFloat();
            control->mouseDown(event(*control, point, juce::ModifierKeys::rightButtonModifier));
            if (!juce::PopupMenu::dismissAllActiveMenus())
                return fail("Actual sampler scalar/toggle/shape popup did not open");
            control->mouseDrag(
                event(*control, point.translated(20, 0), juce::ModifierKeys::rightButtonModifier));
            control->mouseUp(event(*control, point, 0));
            control->mouseDoubleClick(event(*control, point, juce::ModifierKeys::rightButtonModifier));
            dispatchMessages(20);
            if (!controller.project.state().isEquivalentTo(beforeAutomation) ||
                controller.revision != revisionBeforePopup || controller.hasGesture() ||
                selectedChannel != recipientBeforePopup)
                return fail("Sampler popup input changed a value/history/gesture or MIDI recipient");
        }
        const auto code0 = samplerAutomationParameterCode("volume", "enabled");
        const auto attackCode = samplerAutomationParameterCode("cutoff", "attack");
        if (!code0.has_value() || *code0 != 0 || !attackCode.has_value())
            return fail("Actual sampler automation resolver lost valid code0 or explicit field");
        const auto capture = [&](std::uint32_t parameter)
        {
            return SamplerAutomationUiIntent{controller.projectIdentity, controller.revision, first,
                                             parameter};
        };
        const auto run = [&](std::uint32_t parameter, SamplerAutomationUiAction action, Id source = 0)
        { return performSamplerAutomationIntent(capture(parameter), action, source); };
        const AutomationKey enabledKey{AutomationParameter::samplerModulation, first, *code0};
        const AutomationKey attackKey{AutomationParameter::samplerModulation, first, *attackCode};
        if (run(*code0, SamplerAutomationUiAction::initialize).failed() ||
            std::none_of(controller.project.automation.initialValues.begin(),
                         controller.project.automation.initialValues.end(),
                         [&](const auto &item) { return item.key == enabledKey && item.value == 0; }) ||
            controller.undoEditChecked().failed() ||
            !controller.project.state().isEquivalentTo(beforeAutomation) ||
            controller.redoEditChecked().failed() ||
            run(*code0, SamplerAutomationUiAction::removeInitialization).failed() ||
            !controller.project.state().isEquivalentTo(beforeAutomation) ||
            controller.undoEditChecked().failed() || controller.undoEditChecked().failed() ||
            !controller.project.state().isEquivalentTo(beforeAutomation))
            return fail("Actual sampler code0 initialization/removal did not preserve exact history");
        const auto sourceCount = controller.project.automation.sources.size();
        const auto placementCount = controller.project.automation.placements.size();
        if (run(*code0, SamplerAutomationUiAction::create).failed() ||
            controller.project.automation.sources.size() != sourceCount + 1 ||
            controller.project.automation.placements.size() != placementCount + 1)
            return fail("Actual sampler Create automation clip did not create one source/placement");
        const auto automationSource = controller.project.automation.sources.back().id;
        const auto automationPlacement = controller.project.automation.placements.back().id;
        const auto createdState = controller.project.state();
        if (controller.project.automationSource(automationSource)->links.front().key != enabledKey ||
            selectedClips != std::vector<Id>{automationPlacement} ||
            selectedChannel != recipientBeforePopup || controller.undoEditChecked().failed() ||
            !controller.project.state().isEquivalentTo(beforeAutomation) ||
            controller.redoEditChecked().failed() || !controller.project.state().isEquivalentTo(createdState))
            return fail("Sampler clip creation lost exact key/selection or one Undo/Redo");
        const auto playlistVisible = [&]()
        {
            const auto state =
                mcpCall("vibera_state", juce::var(new juce::DynamicObject))["structuredContent"];
            if (const auto *editors = state["editors"].getArray())
                for (const auto &editor : *editors)
                    if (int(editor["index"]) == 2)
                        return bool(editor["visible"]);
            return false;
        };
        if (run(*attackCode, SamplerAutomationUiAction::link, automationSource).failed() ||
            run(*attackCode, SamplerAutomationUiAction::select, automationSource).failed() ||
            !playlistVisible() || focus != Scope::playlist || selectedChannel != recipientBeforePopup)
            return fail("Actual sampler link/Playlist navigation lost source or changed MIDI recipient");
        const auto linkedState = controller.project.state();
        const auto openRange = [&]() -> juce::AlertWindow *
        {
            samplerAutomationRangeDialog(capture(*attackCode), automationSource);
            return dynamic_cast<juce::AlertWindow *>(juce::Component::getCurrentlyModalComponent());
        };
        const auto closeRange = [](juce::AlertWindow *dialog, int action)
        {
            dialog->exitModalState(action);
            dispatchMessages(30);
        };
        auto *dialog = openRange();
        if (!dialog)
            return fail("Actual sampler output range dialog did not open");
        {
            auto stream = folder.getChildFile("sampler-automation-range.png").createOutputStream();
            if (!stream || !stream->setPosition(0) || stream->truncate().failed() ||
                !juce::PNGImageFormat().writeImageToStream(
                    dialog->createComponentSnapshot(dialog->getLocalBounds()), *stream))
                return fail("Actual sampler automation range screenshot failed");
        }
        dialog->getTextEditor("minimum")->setText("+8");
        dialog->getTextEditor("maximum")->setText("0.25");
        closeRange(dialog, 1);
        const auto rangedState = controller.project.state();
        const auto *ranged = controller.project.automationSource(automationSource);
        const auto rangeMatches =
            ranged &&
            std::any_of(ranged->links.begin(), ranged->links.end(), [&](const auto &link)
                        { return link.key == attackKey && link.minimum == 8 && link.maximum == .25f; });
        if (!rangeMatches || controller.undoEditChecked().failed() ||
            !controller.project.state().isEquivalentTo(linkedState) ||
            controller.redoEditChecked().failed() ||
            !controller.project.state().isEquivalentTo(rangedState) || controller.undoEditChecked().failed())
            return fail("Actual sampler inverted physical range did not preserve one exact Undo/Redo");
        dialog = openRange();
        if (!dialog)
            return fail("Actual sampler range Cancel dialog missing");
        dialog->getTextEditor("minimum")->setText("9");
        closeRange(dialog, 0);
        if (!controller.project.state().isEquivalentTo(linkedState))
            return fail("Sampler range Cancel changed source/link/manual parameters");
        dialog = openRange();
        if (!dialog)
            return fail("Actual sampler malformed range dialog missing");
        dialog->getTextEditor("minimum")->setText("nan");
        closeRange(dialog, 1);
        if (!controller.project.state().isEquivalentTo(linkedState))
            return fail("Malformed sampler range partially changed the project");
        const auto staleIntent = capture(*attackCode);
        CommandArgs newerName;
        newerName.channel = second;
        newerName.appearance = ChannelAppearancePatch{};
        newerName.appearance->name = "Newer sampler menu fixture";
        if (controller.execute("channel.properties", newerName).failed())
            return fail("Sampler stale-intent setup failed");
        const auto newerState = controller.project.state();
        if (performSamplerAutomationIntent(staleIntent, SamplerAutomationUiAction::initialize).wasOk() ||
            !controller.project.state().isEquivalentTo(newerState) || controller.undoEditChecked().failed() ||
            !controller.project.state().isEquivalentTo(linkedState))
            return fail("Stale sampler automation menu overwrote newer work");
        if (!controller.beginGesture("sampler.automation.foreign"))
            return fail("Sampler foreign gesture fixture unavailable");
        const auto refused = run(*attackCode, SamplerAutomationUiAction::initialize);
        if (refused.wasOk() || controller.gestureOwner() != "sampler.automation.foreign" ||
            !controller.project.state().isEquivalentTo(linkedState))
        {
            controller.cancelGestureChecked("sampler.automation.foreign");
            return fail("Sampler automation UI stole a foreign gesture");
        }
        if (controller.cancelGestureChecked("sampler.automation.foreign").failed() ||
            run(*attackCode, SamplerAutomationUiAction::unlink, automationSource).failed() ||
            controller.project.automationSource(automationSource)->links.size() != 1 ||
            controller.undoEditChecked().failed() ||
            !controller.project.state().isEquivalentTo(linkedState) ||
            controller.undoEditChecked().failed() ||
            !controller.project.state().isEquivalentTo(createdState) ||
            controller.undoEditChecked().failed() ||
            !controller.project.state().isEquivalentTo(beforeAutomation))
            return fail("Actual sampler unlink or final exact fixture restoration failed");
        window->refreshFromProject();
        folder.getChildFile("sampler-automation-controls.txt")
            .replaceWithText("PASS component popup isolation, code0 create/init/remove, exact "
                             "key/link/Playlist selection, "
                             "physical range Apply/Cancel/invalid refusal, stale intent/foreign ownership "
                             "and exact history. "
                             "Component handlers only; no native OS input or DSP acknowledgement claim.\n");
    }
    folder.getChildFile("sampler-envelope-controls.txt")
        .replaceWithText(
            "PASS actual target/shape choices, fixed Channel/MIDI recipient, envelope controls, exact "
            "history, "
            "cancelled/retargeted continuing strokes, Pan LFO-only controls and two fresh component PNGs. "
            "Component callbacks, not native OS input, listening or hardware qualification.\n");
}
