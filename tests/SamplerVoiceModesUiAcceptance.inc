// SPDX-License-Identifier: AGPL-3.0-or-later
// Add inside Studio::verifySamplerGestures after sampler-voices-gui.txt and
// BEFORE SamplerFilterUiAcceptance.inc. Reuses its real owned sample/window,
// controls, first/second IDs, original canonical state, folder and fail().
// Original component callbacks, not OS input or an audible timing/parity claim.
{
    auto *mono = dynamic_cast<juce::ToggleButton *>(controls->findChildWithID("instrument.sampler.mono"));
    auto *porta =
        dynamic_cast<juce::ToggleButton *>(controls->findChildWithID("instrument.sampler.portamento"));
    auto *full =
        dynamic_cast<juce::ToggleButton *>(controls->findChildWithID("instrument.sampler.fullPortamento"));
    auto *slide = dynamic_cast<juce::Slider *>(controls->findChildWithID("instrument.sampler.slideTime"));
    auto *gate = dynamic_cast<juce::Slider *>(controls->findChildWithID("instrument.sampler.gateBeats"));
    auto *heading =
        dynamic_cast<juce::Label *>(controls->findChildWithID("instrument.sampler.voiceModeHeading"));
    auto *hint = dynamic_cast<juce::Label *>(controls->findChildWithID("instrument.sampler.voiceModeHint"));
    if (!mono || !porta || !full || !slide || !gate || !heading || !hint || !mono->isEnabled() ||
        !porta->isEnabled() || !full->isEnabled() || !slide->isEnabled() || !gate->isEnabled() ||
        mono->getToggleState() || porta->getToggleState() || full->getToggleState() ||
        slide->getValue() != 60. || gate->getValue() != 0 || gate->getTextFromValue(0) != "Off" ||
        !slide->getTextFromValue(60).contains("ms") || !gate->getTextFromValue(.75).contains("beats") ||
        !slide->onDragStart || !slide->onDragEnd || !gate->onDragStart || !gate->onDragEnd)
        return fail("Sampler voice-mode actual controls/default units/owned callbacks unavailable");
    if (!slide->getTooltip().contains("not FL STEPS:TICKS") ||
        !full->getTooltip().contains("does not turn Porta or Mono on") ||
        !maxPoly->getTooltip().contains("Mono ignores") ||
        !hint->getText().contains("Preview source stays dry"))
        return fail("Sampler voice-mode original-policy/units/independent-flag hints are inaccurate");
    const std::array<juce::ToggleButton *, 3> flags{mono, porta, full};
    for (size_t i = 0; i < flags.size(); ++i)
    {
        flags[i]->setToggleState(true, juce::dontSendNotification);
        flags[i]->onClick();
        const auto &owned = controller.project.channel(first)->sampler;
        const auto &other = controller.project.channel(second)->sampler;
        const auto changed = controller.project.state();
        if (owned.mono != (i == 0) || owned.portamento != (i == 1) || owned.fullPortamento != (i == 2) ||
            other.mono || other.portamento || other.fullPortamento || selectedChannel != second)
            return fail("Sampler voice-mode toggle changed another flag/Channel or MIDI recipient");
        controller.undoEdit();
        if (!controller.project.state().isEquivalentTo(original))
            return fail("Voice-mode toggle is not one exact Undo");
        controller.redoEdit();
        if (!controller.project.state().isEquivalentTo(changed))
            return fail("Voice-mode toggle exact Redo failed");
        controller.undoEdit();
        window->refreshFromProject();
    }
    // Foreign canonical gesture cannot be joined by a value-box callback.
    if (!controller.beginGesture())
        return fail("Voice-mode foreign gesture fixture unavailable");
    slide->setValue(250, juce::sendNotificationSync);
    if (!controller.project.state().isEquivalentTo(original) ||
        !status.startsWith("Finish or cancel the current edit"))
        return fail("Voice-mode slider joined a foreign history owner");
    if (auto result = controller.cancelGestureChecked(); result.failed())
        return result;
    window->refreshFromProject();
    slide->onDragStart();
    slide->setValue(200, juce::sendNotificationSync);
    slide->setValue(270, juce::dontSendNotification);
    window->refreshFromProject();
    if (slide->getValue() != 270 || controller.project.channel(first)->sampler.slideTime != .2)
        return fail(
            "Voice-mode projection overwrote owned drag or milliseconds were not converted to seconds");
    slide->setValue(400, juce::sendNotificationSync);
    gate->setValue(.75, juce::sendNotificationSync);
    slide->onDragEnd();
    const auto modeDrag = controller.project.state();
    const auto &owned = controller.project.channel(first)->sampler;
    const auto &other = controller.project.channel(second)->sampler;
    if (controller.hasGesture() || owned.slideTime != .4 || owned.gateBeats != .75 ||
        other.slideTime != .06 || other.gateBeats != 0 || selectedChannel != second)
        return fail("Voice-mode grouped drag lost exact units/owner/history boundary");
    auto *viewport = controls->findParentComponentOfClass<juce::Viewport>();
    if (!viewport)
        return fail("Voice-mode controls lost their scrollable owner");
    const auto previousBounds = window->getBounds();
    const auto previousView = viewport->getViewPosition();
    for (const int width : {360, 620, 980})
    {
        window->setSize(width, 680);
        window->refreshFromProject();
        for (auto *component :
             std::array<juce::Component *, 7>{mono, porta, full, slide, gate, heading, hint})
            if (component->getWidth() < 24 || component->getHeight() < 20 ||
                !controls->getLocalBounds().contains(component->getBounds()))
                return fail("Voice-mode control is clipped or too small in minimum/resized content");
        if (mono->getBounds().intersects(porta->getBounds()) ||
            porta->getBounds().intersects(full->getBounds()) ||
            slide->getBounds().intersects(gate->getBounds()) || gate->getBottom() > hint->getY() ||
            hint->getBottom() > controls->findChildWithID("instrument.sampler.reverse")->getY() ||
            keyboard->getBottom() > controls->getHeight())
            return fail("Voice-mode section overlaps retained controls or clips keyboard content");
        viewport->setViewPosition(100000, heading->getY() - 8);
        if (width == 360 && viewport->getViewPositionX() <= 0)
            return fail("Minimum-width sampler cannot scroll to all original voice controls");
    }
    window->setSize(620, 680);
    window->refreshFromProject();
    viewport->setViewPosition(0, heading->getY() - 8);
    {
        auto stream = folder.getChildFile("sampler-voice-modes.png").createOutputStream();
        if (!stream || !stream->setPosition(0) || stream->truncate().failed() ||
            !juce::PNGImageFormat{}.writeImageToStream(
                window->createComponentSnapshot(window->getLocalBounds()), *stream))
            return fail("Actual populated voice-mode component PNG failed");
    }
    window->setBounds(previousBounds);
    viewport->setViewPosition(previousView);
    controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(original))
        return fail("Voice-mode drag is not one exact Undo");
    controller.redoEdit();
    if (!controller.project.state().isEquivalentTo(modeDrag))
        return fail("Voice-mode drag exact Redo failed");
    controller.undoEdit();
    window->refreshFromProject();
    const auto undoBeforeCancel = controller.undo.getUndoDescription(),
               redoBeforeCancel = controller.undo.getRedoDescription();
    const bool dirtyBeforeCancel = controller.dirty;
    gate->onDragStart();
    gate->setValue(1.5, juce::sendNotificationSync);
    if (!controls->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)) || controller.hasGesture() ||
        !controller.project.state().isEquivalentTo(original))
        return fail("Voice-mode Escape did not restore baseline");
    gate->setValue(2., juce::sendNotificationSync);
    gate->onDragEnd();
    if (!controller.project.state().isEquivalentTo(original) || controller.dirty != dirtyBeforeCancel ||
        controller.undo.getUndoDescription() != undoBeforeCancel ||
        controller.undo.getRedoDescription() != redoBeforeCancel || gate->getValue() != 0)
        return fail("Continued stroke/release after Escape reapplied a cancelled voice-mode draft");
    if (!slide->valueFromTextFunction || !gate->valueFromTextFunction ||
        slide->valueFromTextFunction("125 ms") != 125 || gate->valueFromTextFunction("0.75 beats") != .75 ||
        gate->valueFromTextFunction("Off") != 0)
        return fail("Voice-mode value boxes lost explicit millisecond/beat/Off units");
    slide->setValue(.123456789, juce::dontSendNotification);
    gate->setValue(.00001, juce::dontSendNotification);
    if (slide->valueFromTextFunction(slide->getTextFromValue(slide->getValue())) != slide->getValue() ||
        gate->getValue() != .00001 || gate->getTextFromValue(gate->getValue()).containsIgnoreCase("Off") ||
        gate->valueFromTextFunction(gate->getTextFromValue(gate->getValue())) != gate->getValue())
        return fail("Voice-mode rounded text changed precision or a positive Gate falsely displayed Off");
    window->refreshFromProject();
    for (const auto &text : {"", "nan", "1x", "-1", "10001 ms"})
        if (slide->valueFromTextFunction(text) != slide->getValue())
            return fail("Invalid Slide text was coerced into an edit");
    for (const auto &text : {"", "true", "inf", "0.5junk", "-0.1", "64.1 beats"})
        if (gate->valueFromTextFunction(text) != gate->getValue())
            return fail("Invalid Gate text was coerced into an edit");
    if (!controller.project.state().isEquivalentTo(original))
        return fail("Voice-mode numeric refusal changed canonical state");
    // Existing canonical MCP dispatcher, same fixed owner, no new semantic tool.
    try
    {
        const auto invoke = [&](const char *id, juce::var value, bool flag)
        {
            auto args = juce::var(new juce::DynamicObject);
            args.getDynamicObject()->setProperty("channel", juce::int64(first));
            args.getDynamicObject()->setProperty(flag ? "enabled" : "value", value);
            auto request = juce::var(new juce::DynamicObject);
            request.getDynamicObject()->setProperty("command", id);
            request.getDynamicObject()->setProperty("args", args);
            mcpCall("vibera_command", request);
            window->refreshFromProject();
        };
        invoke("channel.sampler.fullPortamento", true, true);
        if (!full->getToggleState() || porta->getToggleState() || mono->getToggleState())
            return fail("MCP Full porta coerced independent flags");
        controller.undoEdit();
        invoke("channel.sampler.slideTime", .125, false);
        if (slide->getValue() != 125 || controller.project.channel(first)->sampler.slideTime != .125)
            return fail("MCP canonical seconds differ from actual Slide display");
        controller.undoEdit();
        invoke("channel.sampler.gateBeats", .5, false);
        if (gate->getValue() != .5 || controller.project.channel(second)->sampler.gateBeats != 0)
            return fail("MCP Gate unit or fixed Channel ownership differs from UI");
        controller.undoEdit();
        window->refreshFromProject();
    }
    catch (const std::exception &error)
    {
        return fail("Voice-mode shared MCP: " + juce::String(error.what()));
    }
    if (!controller.project.state().isEquivalentTo(original) || selectedChannel != second ||
        controller.hasGesture())
        return fail("Voice-mode component acceptance failed to restore original fixture/recipient");
    folder.getChildFile("sampler-voice-modes-ui.txt")
        .replaceWithText(
            "PASS actual independent Mono/Porta/Full porta callbacks, Slide ms->seconds, Gate beats/Off, "
            "fixed-owner "
            "shared UI/MCP, one-drag Undo/Redo, foreign-owner refusal, continuing-stroke Escape, strict "
            "value boxes "
            "and reachable resized/scrolled component PNG. No OS input, physical MIDI, DSP timing/FL parity, "
            "provider, hardware or listening claim. Per-note Porta, silent Slide and color groups remain "
            "separate RPS-I work.\n");
}
