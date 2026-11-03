// SPDX-License-Identifier: AGPL-3.0-or-later
// Include inside verifySamplerGestures immediately after the voice-mode UI block.
// Existing actual selected-recipient shortcuts/dialog callbacks; no OS input.
{
    juce::Component::SafePointer<juce::AlertWindow> ownedDialog;
    const auto shortcutFailure = [&](const juce::String &message)
    {
        if (ownedDialog)
        {
            ownedDialog->exitModalState(0);
            dispatchMessages(20);
            ownedDialog = nullptr;
        }
        selectedChannel = second;
        window->refreshFromProject();
        return fail(message);
    };
    if (juce::Component::getCurrentlyModalComponent() ||
        !controller.project.state().isEquivalentTo(original) || controller.hasGesture())
        return shortcutFailure("Voice shortcut fixture did not begin at restored baseline/no modal");
    selectedChannel = second; // The open instrument window still belongs to first.
    const std::array<juce::String, 3> flags{"mono", "portamento", "fullPortamento"};
    for (size_t index = 0; index < flags.size(); ++index)
    {
        const auto action = "channel.sampler." + flags[index];
        if (!performAction(action))
            return shortcutFailure("Actual sampler flag shortcut was not admitted: " + action);
        const auto &target = controller.project.channel(second)->sampler;
        const auto &untouched = controller.project.channel(first)->sampler;
        const auto on = controller.project.state();
        if (target.mono != (index == 0) || target.portamento != (index == 1) ||
            target.fullPortamento != (index == 2) || untouched.mono || untouched.portamento ||
            untouched.fullPortamento || selectedChannel != second)
            return shortcutFailure("Flag shortcut followed open settings instead of selected recipient");
        if (!performAction(action) || !controller.project.state().isEquivalentTo(original))
            return shortcutFailure("Flag shortcut did not toggle the selected flag back independently");
        controller.undoEdit();
        if (!controller.project.state().isEquivalentTo(on))
            return shortcutFailure("Flag double-toggle second Undo did not restore enabled state");
        controller.undoEdit();
        if (!controller.project.state().isEquivalentTo(original))
            return shortcutFailure("Flag double-toggle first Undo did not restore baseline");
        controller.redoEdit();
        if (!controller.project.state().isEquivalentTo(on))
            return shortcutFailure("Flag shortcut exact enabled Redo failed");
        controller.redoEdit();
        if (!controller.project.state().isEquivalentTo(original))
            return shortcutFailure("Flag shortcut exact disabled Redo failed");
    }
    const auto openAmount = [&](const juce::String &action)
    {
        if (juce::Component::getCurrentlyModalComponent() || !performAction(action))
            return false;
        ownedDialog = dynamic_cast<juce::AlertWindow *>(juce::Component::getCurrentlyModalComponent());
        return ownedDialog && ownedDialog->getTextEditor("amount") != nullptr;
    };
    const auto finishAmount = [&](int result)
    {
        if (!ownedDialog)
            return false;
        ownedDialog->exitModalState(result);
        dispatchMessages(20); // Execute the real retained modal callback and deletion.
        const bool closed = !ownedDialog && juce::Component::getCurrentlyModalComponent() == nullptr;
        return closed;
    };
    for (const bool slide : {true, false})
    {
        selectedChannel = second;
        const juce::String action = slide ? "channel.sampler.slideTime" : "channel.sampler.gateBeats";
        if (!openAmount(action) || !ownedDialog->getName().contains(slide ? "seconds" : "quarter-note beats"))
            return shortcutFailure("Actual selected sampler numeric shortcut dialog/physical units missing");
        const auto initial = ownedDialog->getTextEditorContents("amount").getDoubleValue();
        if (initial != (slide ? .06 : 0.))
            return shortcutFailure("Sampler shortcut dialog did not project current selected value");
        ownedDialog->getTextEditor("amount")->setText(slide ? "0.375" : "1.25");
        selectedChannel = first; // Selection changes must not retarget an already-owned dialog.
        if (!finishAmount(1))
            return shortcutFailure("Sampler shortcut Apply left its real modal owner pending");
        auto expected = controller.project;
        if (slide)
            expected.channel(second)->sampler.slideTime = .06;
        else
            expected.channel(second)->sampler.gateBeats = 0;
        if (!expected.state().isEquivalentTo(original) ||
            (slide ? controller.project.channel(second)->sampler.slideTime != .375
                   : controller.project.channel(second)->sampler.gateBeats != 1.25) ||
            selectedChannel != first)
            return shortcutFailure(
                "Numeric shortcut lost fixed recipient, physical units or unrelated state");
        const auto changed = controller.project.state();
        controller.undoEdit();
        if (!controller.project.state().isEquivalentTo(original))
            return shortcutFailure("Numeric shortcut Apply was not one exact Undo");
        controller.redoEdit();
        if (!controller.project.state().isEquivalentTo(changed))
            return shortcutFailure("Numeric shortcut exact Redo failed");
        controller.undoEdit();
        selectedChannel = second;
        const auto revision = controller.revision;
        const auto undo = controller.undo.getUndoDescription(), redo = controller.undo.getRedoDescription();
        const bool dirty = controller.dirty;
        if (!openAmount(action))
            return shortcutFailure("Numeric shortcut Cancel dialog failed to open");
        ownedDialog->getTextEditor("amount")->setText(slide ? "0.9" : "2.5");
        if (!finishAmount(0) || !controller.project.state().isEquivalentTo(original) ||
            controller.revision != revision || controller.dirty != dirty ||
            controller.undo.getUndoDescription() != undo || controller.undo.getRedoDescription() != redo)
            return shortcutFailure("Numeric shortcut Cancel changed source/revision/history/dirty state");
        for (const auto &invalid :
             {juce::String(), juce::String("true"), juce::String("nan"), juce::String("0.5junk"),
              juce::String("0,1"), juce::String("-0.1"), juce::String(slide ? "10.01" : "64.01")})
        {
            if (!openAmount(action))
                return shortcutFailure("Numeric refusal dialog failed to open");
            ownedDialog->getTextEditor("amount")->setText(invalid);
            if (!finishAmount(1) || !status.contains("samplerVoice.invalidValue") ||
                !controller.project.state().isEquivalentTo(original) || controller.revision != revision ||
                controller.dirty != dirty || controller.undo.getUndoDescription() != undo ||
                controller.undo.getRedoDescription() != redo)
                return shortcutFailure("Numeric shortcut invalid/range input changed canonical history");
        }
    }
    selectedChannel = second;
    if (!openAmount("channel.sampler.gateBeats"))
        return shortcutFailure("Stale source shortcut dialog did not open");
    ownedDialog->getTextEditor("amount")->setText("3");
    CommandArgs newer;
    newer.channel = second;
    newer.value = .75;              // Literal binary fraction matches Channel's stored float domain exactly.
    command("channel.gain", newer); // A real newer canonical edit, not a fabricated revision.
    const auto newerState = controller.project.state();
    const auto newerRevision = controller.revision;
    const auto newerUndo = controller.undo.getUndoDescription(),
               newerRedo = controller.undo.getRedoDescription();
    const bool newerDirty = controller.dirty;
    if (controller.project.channel(second)->gain != .75f || newerState.isEquivalentTo(original))
        return shortcutFailure("Stale numeric dialog fixture failed to publish its newer real edit");
    if (!finishAmount(1) || !status.startsWith("samplerVoice.staleDialog") ||
        !controller.project.state().isEquivalentTo(newerState) || controller.revision != newerRevision ||
        controller.dirty != newerDirty || controller.undo.getUndoDescription() != newerUndo ||
        controller.undo.getRedoDescription() != newerRedo)
        return shortcutFailure("Stale numeric shortcut overwrote or absorbed a newer canonical change");
    controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(original))
        return shortcutFailure("Newer edit lost independent Undo after stale dialog refusal");
    controller.redoEdit();
    if (!controller.project.state().isEquivalentTo(newerState))
        return shortcutFailure("Newer edit lost exact Redo after stale dialog refusal");
    controller.undoEdit();
    selectedChannel = second;
    window->refreshFromProject();
    if (controller.hasGesture() || juce::Component::getCurrentlyModalComponent() ||
        !controller.project.state().isEquivalentTo(original))
        return shortcutFailure("Sampler shortcut fixture leaked a dialog/gesture or changed original state");
    if (!folder.getChildFile("sampler-voice-mode-shortcuts.txt")
             .replaceWithText(
                 "PASS actual performAction selected-recipient flags/double-toggle/history, numeric physical "
                 "units/fixed-dialog target/Apply/Cancel/strict invalid/range refusal and newer-revision "
                 "preservation. Component/modal callbacks only; no OS input, physical MIDI, listening or "
                 "audible voice-mode/FL timing proof.\n"))
        return shortcutFailure("Sampler shortcut case receipt could not be retained");
}
