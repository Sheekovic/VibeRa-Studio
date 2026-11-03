// SPDX-License-Identifier: AGPL-3.0-or-later
// Include inside namespace vibera in StudioController.cpp, after StudioSamplerController.inc.
// Root declares the member and invokes it at the end of the existing verifyControllers.
// Original component callbacks + actual shared MCP + synthetic MIDI ingress, not OS input.
juce::Result Studio::verifySamplerControllerWorkflow(const juce::File &freshOwnedFolder)
{
    if (midiController.bindingChangesPersisted())
        return juce::Result::fail(
            "Sampler controller acceptance requires the existing hardware-disabled Studio fixture");
    if (freshOwnedFolder.getChildFile("sampler-controller-ui.json").exists())
        return juce::Result::fail("Sampler controller fixture refuses to overwrite prior evidence");
    juce::Array<juce::var> cases, measurements;
    juce::String failure;
    Id openedChannel = 0;
    const auto object = [](std::initializer_list<std::pair<const char *, juce::var>> fields)
    {
        auto result = juce::var(new juce::DynamicObject);
        for (const auto &field : fields)
            result.getDynamicObject()->setProperty(field.first, field.second);
        return result;
    };
    const auto expect = [](bool ok, const juce::String &why)
    {
        if (!ok)
            throw std::runtime_error(why.toStdString());
    };
    const auto call = [&](const char *tool, const juce::var &args)
    {
        const auto envelope = mcpCall(tool, args);
        expect(envelope["structuredContent"].isObject(), "Actual MCP structuredContent required");
        return envelope["structuredContent"];
    };
    const auto learnCall = [&](const char *action, const juce::String &token = juce::String{})
    { return call("vibera_controller", object({{"action", action}, {"token", token}})); };
    const auto commandCall = [&](const char *name, const juce::var &args = juce::var{})
    {
        auto request = object({{"command", name}});
        if (!args.isVoid())
            request.getDynamicObject()->setProperty("args", args);
        return call("vibera_command", request);
    };
    const auto saveImage = [&](juce::Component &component, const char *name)
    {
        auto stream = freshOwnedFolder.getChildFile(name).createOutputStream();
        expect(stream && stream->setPosition(0) && stream->truncate().wasOk(),
               "Fresh truncating component PNG stream");
        expect(juce::PNGImageFormat{}.writeImageToStream(
                   component.createComponentSnapshot(component.getLocalBounds()), *stream),
               "Actual populated component PNG");
    };
    const auto waitClosed = [&](juce::Component::SafePointer<juce::AlertWindow> window)
    {
        const auto deadline = juce::Time::getMillisecondCounter() + 2000;
        while (window && juce::Time::getMillisecondCounter() < deadline)
            dispatchMessages(2);
        expect(!window, "Owned modal callback and automatic deletion must actually drain");
    };
    std::function<juce::Component *(juce::Component &, const juce::String &)> find;
    find = [&](juce::Component &parent, const juce::String &id) -> juce::Component *
    {
        if (parent.getComponentID() == id)
            return &parent;
        for (auto *child : parent.getChildren())
            if (auto *result = find(*child, id))
                return result;
        return nullptr;
    };
    // Scope traversal to the popup opened by this owned component. Pinned JUCE
    // exposes real menu-item titles and a press action that triggers its normal
    // result callback; no private MenuWindow casts, guessed row IDs or OS input.
    const auto pressPopupItem = [&](const juce::String &title)
    {
        juce::Component::SafePointer<juce::Component> item;
        std::function<juce::Component *(juce::Component &)> locate;
        locate = [&](juce::Component &parent) -> juce::Component *
        {
            if (auto *handler = parent.getAccessibilityHandler();
                handler && handler->getRole() == juce::AccessibilityRole::menuItem &&
                handler->getTitle() == title)
                return &parent;
            for (auto *child : parent.getChildren())
                if (auto *found = locate(*child))
                    return found;
            return nullptr;
        };
        const auto deadline = juce::Time::getMillisecondCounter() + 2000;
        do
        {
            if (auto *menu = juce::Component::getCurrentlyModalComponent())
                if (auto *handler = menu->getAccessibilityHandler();
                    handler && handler->getRole() == juce::AccessibilityRole::popupMenu)
                    item = locate(*menu);
            if (item)
                break;
            dispatchMessages(2);
        } while (juce::Time::getMillisecondCounter() < deadline);
        if (!item)
        {
            auto diagnostic = object(
                {{"expected", title}, {"modalCount", juce::Component::getNumCurrentlyModalComponents()}});
            juce::Array<juce::var> entries;
            std::function<void(juce::Component &)> capture;
            capture = [&](juce::Component &component)
            {
                if (auto *handler = component.getAccessibilityHandler())
                    entries.add(object({{"name", component.getName()},
                                        {"id", component.getComponentID()},
                                        {"role", int(handler->getRole())},
                                        {"title", handler->getTitle()}}));
                for (auto *child : component.getChildren())
                    capture(*child);
            };
            for (int index = 0; index < juce::Component::getNumCurrentlyModalComponents(); ++index)
                if (auto *modal = juce::Component::getCurrentlyModalComponent(index))
                    capture(*modal);
            diagnostic.getDynamicObject()->setProperty("entries", entries);
            freshOwnedFolder.getChildFile("popup-failure.json")
                .replaceWithText(juce::JSON::toString(diagnostic, true) + "\n");
        }
        expect(item, "Actual currently-modal popup item required: " + title);
        auto *handler = item->getAccessibilityHandler();
        expect(handler && handler->getRole() == juce::AccessibilityRole::menuItem &&
                   handler->getTitle() == title,
               "Exact real popup title/role retained before activation");
        auto actions = handler->getActions(); // CONTROL copy survives synchronous popup dismissal.
        expect(actions.contains(juce::AccessibilityActionType::press) &&
                   actions.invoke(juce::AccessibilityActionType::press),
               "Actual menu-item press callback: " + title);
    };
    const auto dismiss = [&](juce::Component::SafePointer<juce::AlertWindow> window, int action)
    {
        if (window)
        {
            window->exitModalState(action);
            waitClosed(window);
        }
    };
    const auto cleanup = [&]
    {
        juce::PopupMenu::dismissAllActiveMenus();
        dispatchMessages(2);
        if (samplerControllerLearn && samplerControllerLearn->state == "pending")
            (void)cancelSamplerControllerLearn(samplerControllerLearn->token);
        dismiss(samplerControllerLearnWindow, 0);
        dismiss(samplerControllerPropertiesWindow, 0);
        audio.midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::controllerEvent(1, 64, 0));
        audio.midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 48));
        audio.midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 67));
        audio.stop();
        if (openedChannel)
            instrumentWindows.erase(openedChannel);
    };
    try
    {
        expect(freshOwnedFolder.createDirectory().wasOk(), "Owned application controller output");
        expect(!freshOwnedFolder.getChildFile("sampler-controller-ui.json").exists(),
               "Fresh receipt, never overwrite prior run");
        for (const auto &invalid : {"", "true", "null", "21x", "1]ignored", "1,2", "nan", "1e999", "1 2"})
            expect(samplerControllerNumericText(invalid).isVoid(),
                   "GUI scalar parser refuses invalid input: " + juce::String(invalid));
        expect(samplerControllerNumericText("21").isInt() && int(samplerControllerNumericText("21")) == 21 &&
                   double(samplerControllerNumericText(" -0.12345678901234567 ")) == -.12345678901234567 &&
                   double(samplerControllerNumericText("1e-3")) == .001,
               "GUI scalar parser accepts exact integer, signed precision and exponent input");
        stopTimer();
        audio.stop();
        releaseTypingNotes();
        expect(!midiTaking && !recordArmed && !automationRecordingActive(),
               "Isolated fixture begins without a retained recording owner");
        auto &manager = audio.engine.getDeviceManager();
        auto *device = dynamic_cast<SyntheticDevice *>(manager.deviceManager.getCurrentAudioDevice());
        expect(device && device->getCurrentSampleRate() == 48000. &&
                   device->getCurrentBufferSizeSamples() == 256,
               "Reuse actual controller fixture SyntheticDevice256/48k without changing its configuration");
        auto seed = Project::empty();
        seed.channels.resize(2);
        seed.tempo = 120;
        const auto first = seed.channels.front().id, second = seed.channels.back().id;
        seed.channels[0].name = "Fixed sampler owner";
        seed.channels[1].name = "Independent MIDI recipient";
        controller.replace(seed);
        selectedPattern = seed.patterns.front().id;
        selectedChannel = second;
        selectedChannels = {second};
        selectedNotes.clear();
        selectedClips.clear();
        refresh();
        patternMode = true;
        expect(audio.rebuild(controller.project, true, selectedPattern).wasOk(),
               "Actual builtin sampler-window projection prepared");
        needsRebuild = false;
        ControllerBinding global;
        global.action = "mixer.gain";
        global.mode = 1;
        global.port = 1;
        global.channel = 3;
        global.number = 21;
        global.mixer = 1;
        global.pickup = false;
        midiController.bindings = {global};
        midiController.publish();
        syncSamplerControllerBindings();
        const auto preferences = juce::JSON::toString(midiController.state());
        const auto starting = controller.project.state();
        const auto startingRevision = controller.revision;
        const auto startingUndo = controller.undo.getUndoDescription(),
                   startingRedo = controller.undo.getRedoDescription();
        const bool startingDirty = controller.dirty;
        constexpr std::uint32_t cutoffAmount = 82, pitchAmount = 146;
        expect(lookupSamplerAutomationParameter(cutoffAmount).has_value() &&
                   lookupSamplerAutomationParameter(pitchAmount).has_value(),
               "Explicit original cutoff/pitch LFO amount codes");
        openInstrument(first);
        openedChannel = first;
        auto *instrument = instrumentWindows[first].get();
        expect(instrument && instrument->selectModulationTarget(4),
               "Actual sampler window and destination navigation");
        selectedChannel = second;
        audio.setMidiTarget(second);
        refresh();
        expect(juce::Component::getCurrentlyModalComponent() == nullptr,
               "Owned sampler entry starts without another modal owner");
        expect(instrument->selectModulationTarget(2), "Actual Cutoff destination before popup entry");
        auto *amountControl =
            find(*instrument->controlsComponent(), "instrument.sampler.modulation.lfoAmount");
        expect(amountControl && amountControl->isEnabled() && !amountControl->getLocalBounds().isEmpty(),
               "Real enabled sampler LFO amount component");
        const auto point = amountControl->getLocalBounds().getCentre().toFloat();
        const auto now = juce::Time::getCurrentTime();
        amountControl->mouseDown(juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), point,
                                                  juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier),
                                                  1, 0, 0, 0, 0, amountControl, amountControl, now, point,
                                                  now, 1, false));
        amountControl->mouseUp(juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), point,
                                                juce::ModifierKeys{}, 1, 0, 0, 0, 0, amountControl,
                                                amountControl, now, point, now, 1, false));
        expect(controller.project.state().isEquivalentTo(starting) &&
                   controller.revision == startingRevision && !controller.hasGesture(),
               "Actual sampler context opening does not edit its scalar/history");
        pressPopupItem("Link to controller...");
        const auto learnDeadline = juce::Time::getMillisecondCounter() + 2000;
        while (!samplerControllerLearnWindow && juce::Time::getMillisecondCounter() < learnDeadline)
            dispatchMessages(2);
        auto oldA = samplerControllerLearnWindow;
        expect(oldA && samplerControllerLearn && samplerControllerLearn->state == "pending",
               "Actual GUI Learn A opens pending");
        expect(samplerControllerLearn->candidate.channel == first &&
                   samplerControllerLearn->candidate.parameter == cutoffAmount && selectedChannel == second,
               "Actual context callback resolves its fixed Channel/destination rather than selection");
        cases.add("Actual sampler LFO context popup -> Link to controller fixed target");
        const auto tokenA = samplerControllerLearn->token;
        audio.midiInput.controls.observe(juce::MidiMessage::controllerEvent(3, 21, 90), 1);
        midiController.drain();
        syncSamplerControllerBindings();
        expect(samplerControllerLearn->captured && controller.project.state().isEquivalentTo(starting),
               "Real GUI capture identifies A without applying old global mapping or document value");
        saveImage(*oldA, "sampler-controller-learn-A.png");
        expect(learnCall("samplerLearnCancel", tokenA)["completionState"].toString() == "cancelled",
               "Actual shared MCP cancels A while its old dialog remains visible");
        const auto startedB = call("vibera_controller", object({{"action", "samplerLearnStart"},
                                                                {"targetChannel", juce::int64(first)},
                                                                {"samplerParameter", int(cutoffAmount)}}));
        const auto tokenB = startedB["operation"].toString();
        expect(startedB["completionState"].toString() == "pending" && tokenB.isNotEmpty() && tokenB != tokenA,
               "B is a distinct real pending operation");
        expect(instrument->selectModulationTarget(0) && instrument->selectModulationTarget(4),
               "Destination page changes while fixed-target Learn waits");
        selectedChannel = second;
        refresh();
        audio.midiInput.controls.observe(juce::MidiMessage::controllerEvent(3, 21, 88), 1);
        midiController.drain();
        syncSamplerControllerBindings();
        auto stateB = learnCall("samplerLearnState", tokenB);
        expect(bool(stateB["captured"]) && juce::int64(stateB["candidate"]["channel"]) == first &&
                   int(stateB["candidate"]["parameter"]) == int(cutoffAmount) && selectedChannel == second &&
                   instrument->currentModulationTarget() == 4,
               "Page/Channel selection cannot retarget B");
        auto *disabled = oldA->getButton("Link");
        expect(disabled && !disabled->isEnabled(), "Old GUI A disables Link when newer B owns capture");
        const auto statusBeforeOldA = status;
        disabled->triggerClick();
        dispatchMessages(5);
        expect(oldA && samplerControllerLearn->token == tokenB &&
                   samplerControllerLearn->state == "pending" && samplerControllerLearn->captured,
               "Disabled old Link cannot accept/cancel newer B");
        saveImage(*oldA, "sampler-controller-old-A-disabled.png");
        expect(oldA->getButton("Cancel"), "Actual old A Cancel button");
        oldA->getButton("Cancel")->triggerClick();
        waitClosed(oldA);
        expect(samplerControllerLearn->token == tokenB && samplerControllerLearn->state == "pending" &&
                   samplerControllerLearn->captured && status == statusBeforeOldA,
               "Old A dismissal must leave B candidate, capture lease AND status unchanged");
        expect(controller.project.state().isEquivalentTo(starting) &&
                   controller.revision == startingRevision && controller.dirty == startingDirty &&
                   controller.undo.getUndoDescription() == startingUndo &&
                   controller.undo.getRedoDescription() == startingRedo,
               "All identification/cancel/navigation leaves document/history exact");
        const auto completedB = call("vibera_controller", object({{"action", "samplerLearnAccept"},
                                                                  {"token", tokenB},
                                                                  {"minimum", -.1},
                                                                  {"maximum", .1},
                                                                  {"pickup", false}}));
        expect(completedB["completionState"].toString() == "completed" &&
                   bool(completedB["effectiveApplied"]) && completedB["conflictingProjectLinks"].isArray() &&
                   completedB["conflictingProjectLinks"].size() == 0 &&
                   controller.project.samplerControllerLinks.size() == 1,
               "Completed B has actual effective application and empty conflicts");
        const auto originalLink = controller.project.samplerControllerLinks.front();
        expect(originalLink.channel == first && originalLink.parameter == cutoffAmount &&
                   originalLink.cc == 21 && controller.revision == startingRevision + 1 && !startingDirty &&
                   controller.dirty && juce::JSON::toString(midiController.state()) == preferences,
               "Accept one canonical document edit on fixed owner without global preference mutation");
        const auto completedBState = controller.project.state();
        expect(performAction("edit.undo") && controller.project.state().isEquivalentTo(starting),
               "First Learn Accept is one exact UI Undo");
        commandCall("edit.redo");
        expect(controller.project.state().isEquivalentTo(completedBState),
               "First Learn Accept exact MCP Redo");
        cases.add("GUI A / MCP cancel / B capture / disabled old Link / old dismiss owner isolation");

        // Actual scrollable conflict evidence, explicit replacement and exact shared history.
        const auto beforeReplacement = controller.project.state();
        constexpr double preciseMinimum = -.12345678901234567, preciseMaximum = .8765432109876543;
        showSamplerControllerLearn(second, pitchAmount);
        auto conflictWindow = samplerControllerLearnWindow;
        expect(conflictWindow, "Actual conflict Learn dialog");
        audio.midiInput.controls.observe(juce::MidiMessage::controllerEvent(3, 21, 70), 1);
        midiController.drain();
        syncSamplerControllerBindings();
        const auto conflictState = samplerControllerLearnState();
        const auto conflicts = conflictState["conflictingProjectLinks"];
        auto *conflictText =
            dynamic_cast<juce::TextEditor *>(find(*conflictWindow, "sampler.controller.conflicts"));
        auto *replace =
            dynamic_cast<juce::ToggleButton *>(find(*conflictWindow, "sampler.controller.secondary"));
        auto *pickup = dynamic_cast<juce::ToggleButton *>(find(*conflictWindow, "sampler.controller.pickup"));
        expect(conflicts.isArray() && conflicts.size() == 1 && juce::int64(conflicts[0]) == originalLink.id &&
                   conflictText && conflictText->isReadOnly() &&
                   conflictText->getText().contains(originalLink.label) &&
                   conflictText->getText().contains("#" + juce::String(originalLink.id)) && replace && pickup,
               "Real conflict IDs and displaced fixed-target label appear in actual read-only UI");
        conflictWindow->getTextEditor("minimum")->setText("-0.12345678901234567");
        conflictWindow->getTextEditor("maximum")->setText("0.8765432109876543");
        saveImage(*conflictWindow, "sampler-controller-conflict.png");
        replace->setToggleState(true, juce::dontSendNotification);
        pickup->setToggleState(false, juce::dontSendNotification);
        conflictWindow->getButton("Link")->triggerClick();
        waitClosed(conflictWindow);
        const auto replaced = samplerControllerLearnState();
        freshOwnedFolder.getChildFile("replacement-outcome.json")
            .replaceWithText(juce::JSON::toString(replaced, true) + "\n");
        expect(replaced["completionState"].toString() == "completed" &&
                   replaced["conflictingProjectLinks"].isArray() &&
                   replaced["conflictingProjectLinks"].size() == 0 &&
                   replaced["replacedProjectLinks"].size() == 1 &&
                   juce::int64(replaced["replacedProjectLinks"][0]) == originalLink.id &&
                   controller.project.samplerControllerLinks.size() == 1,
               "GUI replacement commits exact displaced IDs and reports no self-conflict after completion");
        const auto replacement = controller.project.samplerControllerLinks.front();
        expect(replacement.id != originalLink.id && replacement.channel == second &&
                   replacement.parameter == pitchAmount && replacement.minimum == preciseMinimum &&
                   replacement.maximum == preciseMaximum,
               "Conflict replacement allocates one different link on its originally captured target");
        const auto replacementState = controller.project.state();
        expect(performAction("edit.undo") && controller.project.state().isEquivalentTo(beforeReplacement),
               "GUI Undo restores exact replaced link and canonical state in one action");
        commandCall("edit.redo");
        expect(controller.project.state().isEquivalentTo(replacementState),
               "MCP Redo restores exact replacement");

        const auto beforeProperties = controller.project.state();
        const auto propertyRevision = controller.revision;
        const auto propertyUndo = controller.undo.getUndoDescription();
        const auto propertyRedo = controller.undo.getRedoDescription();
        const bool propertyDirty = controller.dirty;
        {
            expect(juce::Component::getCurrentlyModalComponent() == nullptr,
                   "Owned Settings entry starts without another modal owner");
            SettingsContent controllerSettings(*this);
            controllerSettings.setSize(840, 920);
            const int controllersTab = controllerSettings.getTabNames().indexOf("Controllers");
            expect(controllersTab >= 0, "Actual Settings Controllers tab");
            controllerSettings.setCurrentTabIndex(controllersTab);
            auto *linksButton =
                dynamic_cast<juce::Button *>(find(controllerSettings, "settings.controller.projectLinks"));
            expect(controllerSettings.getCurrentTabName() == "Controllers" && linksButton &&
                       linksButton->isEnabled() && linksButton->isVisible() &&
                       linksButton->getButtonText() == "Project sampler links",
                   "Actual Settings Controllers Project sampler links button");
            saveImage(controllerSettings, "sampler-controller-settings-entry.png");
            linksButton->triggerClick();
            SamplerAutomationTarget target;
            expect(resolveSamplerAutomationTarget(controller.project, replacement.channel,
                                                  replacement.parameter, target, true)
                       .wasOk(),
                   "Actual fixed link target label");
            pressPopupItem(target.name + " / CC " + juce::String(replacement.cc) + " [" +
                           juce::String(replacement.id) + "]");
            const auto propertiesDeadline = juce::Time::getMillisecondCounter() + 2000;
            while (!samplerControllerPropertiesWindow &&
                   juce::Time::getMillisecondCounter() < propertiesDeadline)
                dispatchMessages(2);
            expect(samplerControllerPropertiesWindow &&
                       controller.project.state().isEquivalentTo(beforeProperties) &&
                       controller.revision == propertyRevision &&
                       controller.undo.getUndoDescription() == propertyUndo &&
                       controller.undo.getRedoDescription() == propertyRedo &&
                       controller.dirty == propertyDirty,
                   "Settings button/menu traversal opens actual fixed properties without a document/history "
                   "edit");
            expect(samplerControllerPropertiesWindow->getTextEditorContents("label") == replacement.label &&
                       samplerControllerPropertiesWindow->getTextEditorContents("cc") ==
                           juce::String(replacement.cc),
                   "Stable menu link ID opens that actual row's properties");
            cases.add("Actual Settings Controllers Project sampler links button -> stable-link popup -> "
                      "properties");
        }
        auto properties = samplerControllerPropertiesWindow;
        expect(properties, "Actual fixed-target property dialog");
        properties->getTextEditor("label")->setText("Cancelled draft only");
        properties->getTextEditor("cc")->setText("31");
        properties->getButton("Cancel")->triggerClick();
        waitClosed(properties);
        expect(controller.project.state().isEquivalentTo(beforeProperties) &&
                   controller.revision == propertyRevision && controller.dirty == propertyDirty &&
                   controller.undo.getUndoDescription() == propertyUndo &&
                   controller.undo.getRedoDescription() == propertyRedo,
               "Properties Cancel leaves model/history/revision/dirty exact");
        samplerControllerLinkDialog(replacement.id);
        properties = samplerControllerPropertiesWindow;
        expect(properties, "Actual unchanged properties reopen");
        properties->getButton("Apply")->triggerClick();
        waitClosed(properties);
        expect(controller.project.state().isEquivalentTo(beforeProperties) &&
                   controller.revision == propertyRevision && controller.dirty == propertyDirty &&
                   controller.undo.getUndoDescription() == propertyUndo &&
                   controller.undo.getRedoDescription() == propertyRedo &&
                   controller.project.samplerControllerLinks.front().minimum == preciseMinimum &&
                   controller.project.samplerControllerLinks.front().maximum == preciseMaximum,
               "Unchanged rounded display text preserves original full-precision endpoints and is a true "
               "history no-op");
        samplerControllerLinkDialog(replacement.id);
        properties = samplerControllerPropertiesWindow;
        expect(properties, "Actual name-only properties reopen");
        selectedChannel = first;
        refresh();
        properties->getTextEditor("label")->setText("Edited fixed sampler target");
        saveImage(*properties, "sampler-controller-properties.png");
        properties->getButton("Apply")->triggerClick();
        waitClosed(properties);
        expect(controller.project.samplerControllerLinks.front().channel == second &&
                   controller.project.samplerControllerLinks.front().id == replacement.id &&
                   controller.project.samplerControllerLinks.front().cc == 21 &&
                   controller.project.samplerControllerLinks.front().label == "Edited fixed sampler target" &&
                   controller.project.samplerControllerLinks.front().minimum == preciseMinimum &&
                   controller.project.samplerControllerLinks.front().maximum == preciseMaximum &&
                   selectedChannel == first,
               "Actual name-only Apply retains precise endpoints and fixed owner independent of selected "
               "Channel");
        const auto editedProperties = controller.project.state();
        expect(performAction("edit.undo") && controller.project.state().isEquivalentTo(beforeProperties),
               "Property edit is one exact UI Undo");
        commandCall("edit.redo");
        expect(controller.project.state().isEquivalentTo(editedProperties), "Property edit exact MCP Redo");
        samplerControllerLinkDialog(replacement.id);
        properties = samplerControllerPropertiesWindow;
        expect(properties, "Actual source-selector properties");
        properties->getComboBoxComponent("port")->setSelectedId(1);
        properties->getComboBoxComponent("channel")->setSelectedId(8);
        properties->getComboBoxComponent("layer")->setSelectedId(3);
        properties->getTextEditor("cc")->setText("31");
        auto *sourceView = dynamic_cast<SamplerControllerAlert *>(properties.getComponent());
        expect(sourceView, "Actual source-selector controls");
        sourceView->pickup.setToggleState(true, juce::dontSendNotification);
        sourceView->secondary.setToggleState(false, juce::dontSendNotification);
        saveImage(*properties, "sampler-controller-source-properties.png");
        properties->getButton("Apply")->triggerClick();
        waitClosed(properties);
        const auto sourceEdited = controller.project.samplerControllerLinks.front();
        expect(sourceEdited.id == replacement.id && sourceEdited.channel == second &&
                   sourceEdited.parameter == replacement.parameter && sourceEdited.port == 0 &&
                   sourceEdited.midiChannel == 7 && sourceEdited.cc == 31 && sourceEdited.layer == 2 &&
                   sourceEdited.pickup && !sourceEdited.enabled && sourceEdited.minimum == preciseMinimum &&
                   sourceEdited.maximum == preciseMaximum,
               "Actual port/channel/CC/layer/pickup/enabled controls apply exact fixed-row fields");
        const auto sourceState = controller.project.state();
        expect(performAction("edit.undo") && controller.project.state().isEquivalentTo(editedProperties),
               "Source-selector Apply is one exact UI Undo");
        commandCall("edit.redo");
        expect(controller.project.state().isEquivalentTo(sourceState), "Source-selector exact MCP Redo");
        expect(performAction("edit.undo") && controller.project.state().isEquivalentTo(editedProperties),
               "Restore exact source selectors before independent failure case");
        const auto invalidRevision = controller.revision;
        const auto invalidUndo = controller.undo.getUndoDescription();
        const auto invalidRedo = controller.undo.getRedoDescription();
        samplerControllerLinkDialog(replacement.id);
        properties = samplerControllerPropertiesWindow;
        expect(properties, "Actual invalid source properties");
        properties->getTextEditor("cc")->setText("128");
        properties->getButton("Apply")->triggerClick();
        waitClosed(properties);
        expect(controller.project.state().isEquivalentTo(editedProperties) &&
                   controller.revision == invalidRevision &&
                   controller.undo.getUndoDescription() == invalidUndo &&
                   controller.undo.getRedoDescription() == invalidRedo &&
                   status != "Project controller link updated",
               "Invalid actual GUI source refuses atomically without a false completion");
        cases.add("Actual source port/MIDI-channel/CC/layer/pickup/enabled Apply/Undo/Redo and invalid GUI "
                  "source refusal");
        samplerControllerLinkDialog(replacement.id);
        properties = samplerControllerPropertiesWindow;
        expect(properties, "Actual remove dialog");
        properties->getButton("Remove link")->triggerClick();
        waitClosed(properties);
        expect(controller.project.samplerControllerLinks.empty(), "Actual Remove removes exact link");
        expect(performAction("edit.undo") && controller.project.state().isEquivalentTo(editedProperties),
               "Remove exact UI Undo");
        commandCall("edit.redo");
        expect(controller.project.samplerControllerLinks.empty(), "Remove actual MCP Redo");
        expect(juce::JSON::toString(midiController.state()) == preferences,
               "All project dialogs/history retain global preferences");
        cases.add("Conflict labels/IDs; GUI explicit replacement; precise endpoint no-op/name edit; "
                  "properties Cancel/Apply/Remove; UI "
                  "Undo/MCP Redo");
        instrumentWindows.erase(first);
        openedChannel = 0;

        // New isolated builtin-only production projection for ordinary live edits.
        // No native provider is loaded and no physical device or MIDI driver is opened.
        for (const bool pattern : {true, false})
        {
            audio.stop();
            auto live = Project::empty();
            live.channels.resize(2);
            live.tempo = 120;
            const auto local = live.channels[0].id, recipient = live.channels[1].id;
            for (auto &channel : live.channels)
            {
                channel.source = "builtin:bass";
                channel.sampler.envelope = true;
                channel.sampler.loop = true;
                channel.sampler.root = 48;
            }
            live.channels[0].mixer = 1;
            live.channels[1].mixer = 2;
            live.channels[0].pan = -1; // Independently observe score on left, pedal-held input on right.
            live.channels[1].pan = 1;
            live.channels[0].sampler.filterCutoff = .6;
            live.channels[0].sampler.modulation[2].lfoEnabled = true;
            live.channels[0].sampler.modulation[2].lfoPeriod = .5;
            auto &part = live.patterns.front();
            part.autoLength = false;
            part.length = 8;
            part.notes = {{live.allocate(), local, 0., 8., 48, .5f, false}};
            live.placements = {{live.allocate(), part.id, 0, 0., 8., 0.}};
            controller.replace(live);
            selectedPattern = part.id;
            selectedChannel = recipient;
            selectedChannels = {recipient};
            selectedNotes.clear();
            selectedClips.clear();
            patternMode = pattern;
            refresh();
            expect(audio.rebuild(controller.project, pattern, selectedPattern).wasOk(),
                   "Actual builtin live projection");
            needsRebuild = false;
            audio.setMidiTarget(recipient);
            audio.setCountInBars(0);
            audio.setLooping(false);
            audio.seekBeat(0);
            audio.playStop();
            dispatchMessages(20);
            juce::AudioBuffer<float> block(2, 256);
            const auto pump = [&](int count)
            {
                for (int i = 0; i < count; ++i)
                {
                    timerCallback();
                    device->process(block);
                    dispatchMessages(1);
                }
            };
            pump(12);
            audio.midiInput.handleIncomingMidiMessage(nullptr,
                                                      juce::MidiMessage::noteOn(1, 48, juce::uint8(100)));
            audio.midiInput.handleIncomingMidiMessage(nullptr,
                                                      juce::MidiMessage::controllerEvent(1, 64, 127));
            pump(4);
            audio.midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 48));
            pump(4);
            const auto lane = audio.midiInput.retainRecordingLane();
            expect(bool(lane), "Real retained input lane");
            const auto overflow = lane->overflows.load(), panic = lane->panicEpoch.load();
            const auto before = audio.projectionDiagnostics();
            const auto beforeGains = audio.mixerGraphInitialisations();
            const auto position = audio.positionBeat();
            const int starts = device->starts, stops = device->stops;
            auto diagnostic = [](const auto &projection, Id id)
            {
                for (const auto &row : projection.channels)
                    if (row.channel == id)
                        return row;
                return AudioEngine::ChannelDiagnostic{};
            };
            expect(diagnostic(before, recipient).heldInputKeys > 0 &&
                       diagnostic(before, local).renderCalls > 0,
                   "Actual pedal-held independent input plus processed score source before edits");
            int callbacks = 0, overruns = 0, nonfinite = 0, silent = 0, invalid = 0;
            double maximum = 0;
            SyntheticCallbackSpan worst;
            const auto measurement =
                object({{"mode", pattern ? "Pattern" : "Song"},
                        {"frames", 256},
                        {"sampleRate", 48000.},
                        {"budgetMs", 256000. / 48000.},
                        {"callbackAllocations", juce::var{}},
                        {"callbackDestructions", juce::var{}},
                        {"heapObservation",
                         "Unavailable in application target; separate test-executable probe only"},
                        {"scope", "Outer SyntheticDevice callback; actual score plus pedal-held MIDI; no "
                                  "physical/hardware/listening claim"}});
            measurements.add(measurement);
            const auto retainMeasurement = [&]
            {
                auto *out = measurement.getDynamicObject();
                out->setProperty("callbacks", callbacks);
                out->setProperty("overruns", overruns);
                out->setProperty("maximumMs", maximum);
                out->setProperty("nonfiniteBlocks", nonfinite);
                out->setProperty("silentBlocks", silent);
                out->setProperty("invalidSpans", invalid);
                out->setProperty("deviceStarts", device->starts - starts);
                out->setProperty("deviceStops", device->stops - stops);
                out->setProperty("worstSpan", worst.timing.describe(worst.began, worst.ended));
            };
            const auto tick = [&](int count)
            {
                for (int i = 0; i < count; ++i)
                {
                    timerCallback();
                    SyntheticCallbackSpan span;
                    span.began = juce::Time::getHighResolutionTicks();
                    device->process(block);
                    span.ended = juce::Time::getHighResolutionTicks();
                    span.timing = device->lastTiming();
                    const auto elapsed = SyntheticCallbackTiming::milliseconds(span.began, span.ended);
                    ++callbacks;
                    overruns += elapsed >= 256000. / 48000.;
                    invalid += !span.timing.containedBy(span.began, span.ended);
                    if (callbacks == 1 || elapsed > maximum)
                    {
                        maximum = elapsed;
                        worst = span;
                    }
                    bool bad = false;
                    std::array<float, 2> peak{};
                    for (int channel = 0; channel < 2; ++channel)
                        for (int frame = 0; frame < 256; ++frame)
                        {
                            const auto value = block.getSample(channel, frame);
                            bad |= !std::isfinite(value);
                            if (std::isfinite(value))
                                peak[size_t(channel)] = std::max(peak[size_t(channel)], std::abs(value));
                        }
                    nonfinite += bad;
                    silent += peak[0] <= .000001f || peak[1] <= .000001f;
                    retainMeasurement();
                    dispatchMessages(1);
                }
            };
            const auto liveStart =
                call("vibera_controller", object({{"action", "samplerLearnStart"},
                                                  {"targetChannel", juce::int64(local)},
                                                  {"samplerParameter", int(cutoffAmount)}}));
            const auto liveToken = liveStart["operation"].toString();
            const auto beforeCapture = controller.project.state();
            audio.midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::controllerEvent(1, 25, 77));
            midiController.drain();
            tick(4);
            expect(samplerControllerLearn->captured &&
                       controller.project.state().isEquivalentTo(beforeCapture),
                   "Live first CC captures without changing manual value or source");
            const auto accepted = call("vibera_controller", object({{"action", "samplerLearnAccept"},
                                                                    {"token", liveToken},
                                                                    {"minimum", -.05},
                                                                    {"maximum", .05},
                                                                    {"pickup", false}}));
            expect(accepted["completionState"].toString() == "completed" &&
                       bool(accepted["effectiveApplied"]),
                   "Live canonical link actually effective");
            const auto liveID = controller.project.samplerControllerLinks.front().id;
            tick(4);
            juce::ValueTree beforeLast;
            for (const int value : {0, 64, 127})
            {
                beforeLast = controller.project.state();
                audio.midiInput.handleIncomingMidiMessage(nullptr,
                                                          juce::MidiMessage::controllerEvent(1, 25, value));
                midiController.drain();
                tick(4);
                const double expected = value == 0     ? -.05
                                        : value == 127 ? .05
                                                       : -.05 + double(value) * .1 / 127.;
                expect(std::abs(controller.project.channel(local)->sampler.modulation[2].lfoAmount -
                                expected) < 1e-12 &&
                           controller.project.channel(recipient)->sampler.modulation[2].lfoAmount == 0 &&
                           selectedChannel == recipient,
                       "Actual ingress CC applies exact physical scalar to bound owner while other held "
                       "recipient stays selected");
            }
            const auto afterLast = controller.project.state();
            expect(performAction("edit.undo"), "Live scalar UI Undo admitted");
            tick(4);
            expect(controller.project.state().isEquivalentTo(beforeLast),
                   "Live scalar Undo exact source state");
            commandCall("edit.redo");
            tick(4);
            expect(controller.project.state().isEquivalentTo(afterLast), "Live scalar MCP Redo exact");
            const auto beforeConfig = controller.project.state();
            samplerControllerLinkDialog(liveID);
            auto liveProperties = samplerControllerPropertiesWindow;
            expect(liveProperties, "Actual properties during held ordinary playback");
            liveProperties->getTextEditor("cc")->setText("26");
            liveProperties->getButton("Apply")->triggerClick();
            waitClosed(liveProperties);
            tick(4);
            const auto afterConfig = controller.project.state();
            expect(controller.project.samplerControllerLinks.front().cc == 26,
                   "Live source-config update canonical");
            expect(performAction("edit.undo"), "Live link config Undo admitted");
            tick(4);
            expect(controller.project.state().isEquivalentTo(beforeConfig), "Live link config Undo exact");
            commandCall("edit.redo");
            tick(4);
            expect(controller.project.state().isEquivalentTo(afterConfig), "Live link config Redo exact");
            const auto cancelStart =
                call("vibera_controller", object({{"action", "samplerLearnStart"},
                                                  {"targetChannel", juce::int64(recipient)},
                                                  {"samplerParameter", int(pitchAmount)}}));
            const auto cancelToken = cancelStart["operation"].toString();
            const auto cancelBefore = controller.project.state();
            audio.midiInput.handleIncomingMidiMessage(nullptr,
                                                      juce::MidiMessage::noteOn(1, 67, juce::uint8(80)));
            audio.midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::controllerEvent(1, 27, 99));
            midiController.drain();
            tick(4);
            audio.midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 67));
            expect(learnCall("samplerLearnCancel", cancelToken)["completionState"].toString() == "cancelled",
                   "Live cancel is actual terminal");
            tick(4);
            expect(controller.project.state().isEquivalentTo(cancelBefore),
                   "Live Learn Cancel exact; no captured CC replay");
            const auto after = audio.projectionDiagnostics();
            auto normalized = controller.project;
            normalized.samplerControllerLinks.clear();
            normalized.nextId = live.nextId;
            normalized.channel(local)->sampler.modulation[2].lfoAmount = 0;
            expect(normalized.state().isEquivalentTo(live.state()),
                   "Live workflow preserves all raw score IDs/placements/source/settings except owned "
                   "link/scalar");
            // The transport's UI-facing position is timer-cached and need not advance
            // during a short virtual run. Assert exact rendered frame advance on the
            // actual node playhead, while also refusing any UI-position regression.
            measurement.getDynamicObject()->setProperty(
                "invariants",
                object({{"playing", audio.playing()},
                        {"positionBefore", position},
                        {"positionAfter", audio.positionBeat()},
                        {"processingHeadBeforeSeconds", before.headSeconds},
                        {"processingHeadAfterSeconds", after.headSeconds},
                        {"expectedAdvanceFrames", callbacks * 256},
                        {"actualAdvanceFrames", juce::int64(std::llround(after.headSeconds * 48000.) -
                                                            std::llround(before.headSeconds * 48000.))},
                        {"sameEdit", before.edit == after.edit},
                        {"sameContext", before.playbackContext == after.playbackContext},
                        {"replacementsBefore", juce::int64(before.replacements)},
                        {"replacementsAfter", juce::int64(after.replacements)},
                        {"samplerCreationsBefore", juce::int64(before.samplerCreations)},
                        {"samplerCreationsAfter", juce::int64(after.samplerCreations)},
                        {"sameMixerGraphs", beforeGains == audio.mixerGraphInitialisations()},
                        {"needsRebuild", needsRebuild},
                        {"projectionError", projectionError},
                        {"sameLane", lane == audio.midiInput.retainRecordingLane()},
                        {"overflowsBefore", juce::int64(overflow)},
                        {"overflowsAfter", juce::int64(lane->overflows.load())},
                        {"panicBefore", juce::int64(panic)},
                        {"panicAfter", juce::int64(lane->panicEpoch.load())},
                        {"heldBefore", diagnostic(before, recipient).heldInputKeys},
                        {"heldAfter", diagnostic(after, recipient).heldInputKeys}}));
            expect(
                audio.playing() && audio.positionBeat() >= position && before.headPlaying &&
                    after.headPlaying &&
                    std::llround(after.headSeconds * 48000.) - std::llround(before.headSeconds * 48000.) ==
                        callbacks * 256 &&
                    starts == device->starts && stops == device->stops && before.edit == after.edit &&
                    before.playbackContext == after.playbackContext &&
                    before.replacements == after.replacements &&
                    before.samplerCreations == after.samplerCreations &&
                    beforeGains == audio.mixerGraphInitialisations() && !needsRebuild &&
                    projectionError.isEmpty() && lane == audio.midiInput.retainRecordingLane() &&
                    lane->overflows.load() == overflow && lane->panicEpoch.load() == panic &&
                    diagnostic(after, recipient).heldInputKeys > 0,
                "Actual live playhead/device/Edit/context/graphs/input lane/held ownership remain retained");
            for (const auto id : {local, recipient})
            {
                const auto old = diagnostic(before, id), now = diagnostic(after, id);
                expect(old.track == now.track && old.sampler == now.sampler && old.sample == now.sample &&
                           old.graphInitialisations == now.graphInitialisations &&
                           now.renderCalls > old.renderCalls && old.timelineMisses == now.timelineMisses &&
                           old.samplerAutomationFailures == now.samplerAutomationFailures,
                       "Real processed source and voice bank retained without timeline/automation failures");
            }
            retainMeasurement();
            expect(callbacks > 0 && !overruns && !nonfinite && !silent && !invalid,
                   "Every warm256/48k callback retains exact deadline and finite non-silent independent "
                   "score/held-input output");
            audio.midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::controllerEvent(1, 64, 0));
            pump(4);
            expect(diagnostic(audio.projectionDiagnostics(), recipient).heldInputKeys == 0,
                   "Natural pedal release closes retained keys without panic");
            audio.stop();
            expect(juce::JSON::toString(midiController.state()) == preferences,
                   "Live project mapping/history never rewrites global profile");
        }
        cases.add("Pattern/Song actual score+pedal-held MIDI, CC capture/config/scalar/history/cancel, "
                  "graph/lane/voice ownership and warm callbacks");
        cleanup();
    }
    catch (const std::exception &error)
    {
        failure = error.what();
        try
        {
            cleanup();
        }
        catch (...)
        {
            failure += "; fixture cleanup failed";
        }
    }
    const auto report =
        object({{"passed", failure.isEmpty()},
                {"error", failure},
                {"cases", cases},
                {"callbacks", measurements},
                {"nativeOsInput", false},
                {"physicalMidi", false},
                {"hardware", false},
                {"listening", false},
                {"evidence", "Actual component callbacks/shared MCP/synthetic callback and MIDI ingress; not "
                             "native OS input or an exact modulation-generation ACK"}});
    if (!freshOwnedFolder.getChildFile("sampler-controller-ui.json")
             .replaceWithText(juce::JSON::toString(report, true) + "\n"))
        return juce::Result::fail("Sampler controller acceptance could not retain its actual outcome: " +
                                  failure);
    return failure.isEmpty() ? juce::Result::ok() : juce::Result::fail("Sampler controller UI: " + failure);
}
