// SPDX-License-Identifier: AGPL-3.0-or-later
// Include inside Studio::verifySamplerGestures, after sampler-voices-gui.txt,
// before the existing `auto &keys = keyboard->keyComponent();`.
// Uses the retained actual sample/window/controls, first/second Channel IDs,
// original Project state, fail(), and existing callback/dispatch helpers.
{
    auto *filterType =
        dynamic_cast<juce::ComboBox *>(controls->findChildWithID("instrument.sampler.filterType"));
    auto *filterCutoff =
        dynamic_cast<juce::Slider *>(controls->findChildWithID("instrument.sampler.filterCutoff"));
    auto *filterResonance =
        dynamic_cast<juce::Slider *>(controls->findChildWithID("instrument.sampler.filterResonance"));
    auto *filterHint =
        dynamic_cast<juce::Label *>(controls->findChildWithID("instrument.sampler.filterHint"));
    if (!filterType || !filterCutoff || !filterResonance || !filterHint || !filterType->isEnabled() ||
        !filterCutoff->isEnabled() || !filterResonance->isEnabled() || filterType->getNumItems() != 6 ||
        filterType->getSelectedId() != 2 || filterCutoff->getValue() != 100 ||
        filterResonance->getValue() != 0 || !filterCutoff->onDragStart || !filterCutoff->onDragEnd ||
        !filterResonance->onDragStart || !filterResonance->onDragEnd)
        return fail("Sampler filter controls/defaults or owned gesture callbacks unavailable");
    for (int type = 0; type < 6; ++type)
        if (filterType->getItemText(type) != samplerFilterTypeName(type))
            return fail("Sampler filter picker does not expose the actual six model types");
    // A queued actual ComboBox notification must survive a model projection refresh.
    filterType->setSelectedId(3, juce::sendNotification);
    window->refreshFromProject();
    if (filterType->getSelectedId() != 3 || controller.project.channel(first)->sampler.filterType != 1)
        return fail("Sampler filter projection erased a queued choice or committed it early");
    dispatchMessages(20);
    if (controller.project.channel(first)->sampler.filterType != 2 ||
        controller.project.channel(second)->sampler.filterType != 1 || selectedChannel != second)
        return fail("Sampler filter picker changed the selected Rack Channel instead of its owner");
    const auto typeState = controller.project.state();
    controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(original))
        return fail("Sampler filter type is not one exact Undo");
    controller.redoEdit();
    if (!controller.project.state().isEquivalentTo(typeState))
        return fail("Sampler filter type Redo changed unrelated source state");
    controller.undoEdit();
    window->refreshFromProject();
    if (!controller.beginGesture())
        return fail("Sampler filter foreign gesture fixture unavailable");
    filterCutoff->setValue(40, juce::sendNotificationSync);
    if (!controller.project.state().isEquivalentTo(original) ||
        !status.startsWith("Finish or cancel the current edit"))
        return fail("Sampler filter joined another control's history owner");
    if (auto result = controller.cancelGestureChecked(); result.failed())
        return result;
    filterCutoff->onDragStart();
    filterCutoff->setValue(75, juce::sendNotificationSync);
    // This is a component projection test, not a native mouse/OS-input claim.
    filterCutoff->setValue(83, juce::dontSendNotification);
    window->refreshFromProject();
    if (filterCutoff->getValue() != 83 || controller.project.channel(first)->sampler.filterCutoff != .75)
        return fail("Sampler filter refresh overwrote an owned active drag");
    filterCutoff->setValue(25, juce::sendNotificationSync);
    filterResonance->setValue(50, juce::sendNotificationSync);
    filterCutoff->onDragEnd();
    const auto filterState = controller.project.state();
    if (controller.hasGesture() || controller.project.channel(first)->sampler.filterCutoff != .25 ||
        controller.project.channel(first)->sampler.filterResonance != .5 ||
        controller.project.channel(second)->sampler.filterCutoff != 1 ||
        controller.project.channel(second)->sampler.filterResonance != 0 || selectedChannel != second ||
        !filterHint->getText().contains("112.5 Hz") || !filterHint->getText().contains("Q 6.35") ||
        !filterType->getTooltip().contains("Preview source bypasses Channel processing"))
        return fail("Sampler filter drag/grouped values/Hz-Q hints or exact Channel target incorrect");
    controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(original))
        return fail("Sampler filter drag is not one exact Undo");
    controller.redoEdit();
    if (!controller.project.state().isEquivalentTo(filterState))
        return fail("Sampler filter drag exact Redo failed");
    controller.undoEdit();
    window->refreshFromProject();
    filterResonance->onDragStart();
    filterResonance->setValue(60, juce::sendNotificationSync);
    if (!controls->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)) || controller.hasGesture() ||
        !controller.project.state().isEquivalentTo(original))
        return fail("Sampler filter Escape failed to restore the owned baseline");
    filterResonance->onDragEnd();
    if (!controller.project.state().isEquivalentTo(original))
        return fail("Sampler filter release after Escape re-committed a cancelled draft");
    filterType->setSelectedId(4, juce::sendNotificationSync);
    filterCutoff->setValue(25, juce::sendNotificationSync);
    filterResonance->setValue(50, juce::sendNotificationSync);
    if (auto *viewport = controls->findParentComponentOfClass<juce::Viewport>())
    {
        const auto previous = viewport->getViewPosition();
        window->showPage(1); // The filter lives on the Envelope / instrument page.
        viewport->setViewPosition(0, 0);
        auto stream = folder.getChildFile("sampler-filter-controls.png").createOutputStream();
        if (!stream)
            return fail("Sampler filter component screenshot stream unavailable");
        stream->setPosition(0);
        stream->truncate();
        if (!juce::PNGImageFormat().writeImageToStream(
                window->createComponentSnapshot(window->getLocalBounds()), *stream))
            return fail("Sampler filter component screenshot failed");
        viewport->setViewPosition(previous);
    }
    else
        return fail("Sampler filter controls lost their scrollable owner");
    for (int edit = 0; edit < 3; ++edit)
        controller.undoEdit();
    window->refreshFromProject();
    if (!controller.project.state().isEquivalentTo(original))
        return fail("Sampler filter component evidence failed to restore the original fixture");
    folder.getChildFile("sampler-filter-controls.txt")
        .replaceWithText("PASS actual sampler filter ComboBox/slider callbacks, queued choice, owned drag "
                         "projection, fixed Channel target, exact Undo/Redo/Escape and fresh component PNG. "
                         "No native OS input, DSP response, provider, hardware or listening claim.\n");
}
