// SPDX-License-Identifier: AGPL-3.0-or-later
// Actual component callbacks/shared semantic state, not native OS input or listening.
namespace vibera
{
juce::Result Studio::Editor::verifyNoteModes(const juce::File &folder)
{
    juce::Array<juce::var> cases;
    auto receipt = juce::var(new juce::DynamicObject);
    const auto expect = [&](bool okay, const char *name)
    {
        if (!okay)
            throw std::runtime_error(name);
        cases.add(name);
    };
    const auto find = [](juce::Component &root, const juce::String &id) -> juce::Component *
    {
        const auto visit = [&](auto &&self, juce::Component &component) -> juce::Component *
        {
            if (component.getComponentID() == id)
                return &component;
            for (int index = 0; index < component.getNumChildComponents(); ++index)
                if (auto *match = self(self, *component.getChildComponent(index)))
                    return match;
            return nullptr;
        };
        return visit(visit, root);
    };
    const auto request = [&](const char *action, const juce::String &operation = {})
    {
        auto value = noteModeArgs(action);
        if (operation.isNotEmpty())
            value.getDynamicObject()->setProperty("operation", operation);
        return value;
    };
    const auto capture = [&](juce::Component &component, const char *name)
    {
        auto stream = folder.getChildFile(name).createOutputStream();
        if (!stream)
            throw std::runtime_error("Cannot create owned PNG output");
        stream->setPosition(0);
        stream->truncate();
        expect(juce::PNGImageFormat{}.writeImageToStream(
                   component.createComponentSnapshot(component.getLocalBounds()), *stream),
               "Fresh component PNG encodes into truncated stream");
    };
    try
    {
        expect(folder.createDirectory().wasOk(), "Owned output folder exists");
        auto project = Project::empty();
        const auto channel = project.channels.front().id;
        const auto pattern = project.patterns.front().id;
        Note first{project.allocate(), channel, .25, 2, 60, .5f, false};
        Note second{project.allocate(), channel, .5, 1, 67, .7f, false};
        second.colourGroup = 2;
        project.patterns.front().notes = {first, second};
        s.controller.replace(project);
        s.selectedPattern = pattern;
        s.selectedChannel = channel;
        s.focus = Scope::piano;
        s.typingKeys.enabled = false;
        s.selectedNotes = {first.id, second.id};
        setSize(1080, 600);
        resized();
        expect(pianoColour.getNumItems() == 16, "Actual Piano toolbar offers all16 color groups");
        pianoColour.setSelectedId(16, juce::dontSendNotification);
        pianoColour.onChange();
        expect(insertColour == 15, "Actual color selector stores stable0..15 rather than display1..16");
        pianoInsertSlide.onClick();
        pianoInsertPorta.onClick();
        expect(insertSlide && insertPortamento,
               "Actual Slide/Porta toolbar callbacks retain independent insertion flags");
        CommandArgs added;
        added.pattern = pattern;
        added.channel = channel;
        added.beat = 1;
        added.length = 1;
        added.pitch = 72;
        added.value = .8;
        fillNoteInsertion(added);
        s.command("notes.add", added);
        const auto drawn = s.controller.project.pattern(pattern)->notes.back();
        expect(drawn.slide && drawn.portamento && drawn.colourGroup == 15,
               "Insertion payload reaches one atomic real canonical note creation");
        s.selectedNotes = {first.id, second.id};
        pianoRecolour.onClick();
        expect(s.controller.project.pattern(pattern)->notes[0].colourGroup == 15 &&
                   s.controller.project.pattern(pattern)->notes[1].colourGroup == 15,
               "Actual Recolor uses canonical selected source edits");
        expect(s.controller.undoEditChecked().wasOk() &&
                   s.controller.project.pattern(pattern)->notes[0].colourGroup == 0 &&
                   s.controller.project.pattern(pattern)->notes[1].colourGroup == 2,
               "Recolor one Undo restores distinct mixed colors");
        pianoColour.setSelectedId(3, juce::dontSendNotification);
        pianoColour.onChange();
        pianoSelectColour.onClick();
        expect(s.selectedNotes == std::vector<Id>{second.id},
               "Actual By-color selects stable original sources");
        pianoProperties.onClick();
        auto operation = notePropertiesSession->operation;
        expect(notePropertiesWindow && notePropertiesWindow->isVisible() &&
                   notePropertiesSession->state == "pending",
               "Actual Properties button opens the functional captured-target window");
        auto *velocity =
            dynamic_cast<juce::TextEditor *>(find(*notePropertiesWindow, "piano.noteProperties.velocity"));
        auto *colour =
            dynamic_cast<juce::ComboBox *>(find(*notePropertiesWindow, "piano.noteProperties.colourGroup"));
        auto *porta = dynamic_cast<juce::ToggleButton *>(
            find(*notePropertiesWindow, "piano.noteProperties.portamento"));
        auto *apply =
            dynamic_cast<juce::TextButton *>(find(*notePropertiesWindow, "piano.noteProperties.apply"));
        auto *reset =
            dynamic_cast<juce::TextButton *>(find(*notePropertiesWindow, "piano.noteProperties.reset"));
        auto *cancel =
            dynamic_cast<juce::TextButton *>(find(*notePropertiesWindow, "piano.noteProperties.cancel"));
        expect(velocity && colour && porta && apply && reset && cancel,
               "Actual properties controls have discoverable component IDs");
        const auto baseline = s.controller.project.state();
        velocity->setText("0.375", false);
        velocity->onTextChange();
        colour->setSelectedId(8, juce::dontSendNotification);
        colour->onChange();
        porta->setToggleState(true, juce::dontSendNotification);
        porta->onClick();
        expect(s.controller.project.state().isEquivalentTo(baseline) &&
                   double(notePropertiesSession->draft["velocity"]) == .375 &&
                   int(notePropertiesSession->draft["colourGroup"]) == 7 &&
                   bool(notePropertiesSession->draft["portamento"]),
               "Actual controls stage semantic state without mutating the score");
        reset->onClick();
        expect(notePropertiesSession->draft.getDynamicObject()->getProperties().isEmpty() &&
                   s.controller.project.state().isEquivalentTo(baseline),
               "Actual Reset restores dialog baseline without score/history changes");
        velocity->setText("0.375", false);
        velocity->onTextChange();
        colour->setSelectedId(8, juce::dontSendNotification);
        colour->onChange();
        porta->setToggleState(true, juce::dontSendNotification);
        porta->onClick();
        capture(*notePropertiesWindow, "note-properties-pending.png");
        apply->onClick();
        const auto &edited = s.controller.project.pattern(pattern)->notes[1];
        expect(notePropertiesSession->state == "completed" && !notePropertiesWindow->isVisible() &&
                   edited.velocity == .375f && edited.colourGroup == 7 && edited.portamento,
               "Actual Apply atomically commits all staged fields and closes its own window");
        expect(s.controller.undoEditChecked().wasOk() &&
                   s.controller.project.state().isEquivalentTo(baseline),
               "Properties group has one exact Undo");
        expect(s.controller.redoEditChecked().wasOk(), "Properties exact Redo remains available");
        s.pianoNoteModes(request("propertiesOpen"));
        operation = notePropertiesSession->operation;
        auto update = request("propertiesUpdate", operation);
        update.getDynamicObject()->setProperty("noteProperties",
                                               juce::JSON::parse("{\"pan\":0.25,\"modX\":0.75}"));
        expect(s.pianoNoteModes(update)["completionState"].toString() == "pending",
               "Semantic stage updates the same actual window");
        const auto beforeCancel = s.controller.project.state();
        notePropertiesWindow->closeButtonPressed();
        expect(notePropertiesSession->state == "cancelled" &&
                   s.controller.project.state().isEquivalentTo(beforeCancel),
               "Window Close cancels only its captured operation without score loss");
        s.pianoNoteModes(request("propertiesOpen"));
        const auto newer = notePropertiesSession->operation;
        const auto status = s.status;
        expect(s.pianoNoteModes(request("propertiesCancel", operation))["completionState"].toString() ==
                       "failed" &&
                   notePropertiesSession->operation == newer && notePropertiesSession->state == "pending" &&
                   s.status == status,
               "Old token cannot cancel or change the newer window/status");
        velocity =
            dynamic_cast<juce::TextEditor *>(find(*notePropertiesWindow, "piano.noteProperties.velocity"));
        apply = dynamic_cast<juce::TextButton *>(find(*notePropertiesWindow, "piano.noteProperties.apply"));
        velocity->setText("invalid", false);
        velocity->onTextChange();
        apply->onClick();
        expect(notePropertiesSession->state == "pending" &&
                   s.controller.project.state().isEquivalentTo(beforeCancel),
               "Invalid actual numeric text cannot Apply an older staged value");
        s.pianoNoteModes(request("propertiesCancel", newer));
        s.pianoNoteModes(request("propertiesOpen"));
        const auto beforeButtons = s.controller.project.state();
        auto *slide =
            dynamic_cast<juce::ToggleButton *>(find(*notePropertiesWindow, "piano.noteProperties.slide"));
        auto *muted =
            dynamic_cast<juce::ToggleButton *>(find(*notePropertiesWindow, "piano.noteProperties.muted"));
        cancel = dynamic_cast<juce::TextButton *>(find(*notePropertiesWindow, "piano.noteProperties.cancel"));
        expect(slide && muted && cancel, "Actual Slide/Mute/Cancel controls remain available on reopen");
        slide->setToggleState(true, juce::dontSendNotification);
        slide->onClick();
        muted->setToggleState(true, juce::dontSendNotification);
        muted->onClick();
        expect(bool(notePropertiesSession->draft["slide"]) && bool(notePropertiesSession->draft["muted"]) &&
                   s.controller.project.state().isEquivalentTo(beforeButtons),
               "Actual Properties Slide/Mute callbacks stage independent flags");
        cancel->onClick();
        expect(notePropertiesSession->state == "cancelled" && !notePropertiesWindow->isVisible() &&
                   s.controller.project.state().isEquivalentTo(beforeButtons),
               "Actual Properties Cancel button cancels without score/history changes");
        s.pianoNoteModes(request("propertiesOpen"));
        velocity =
            dynamic_cast<juce::TextEditor *>(find(*notePropertiesWindow, "piano.noteProperties.velocity"));
        expect(velocity != nullptr, "Actual numeric field remains available on reopen");
        velocity->setText("0.625", false);
        velocity->onTextChange();
        velocity->onReturnKey();
        expect(notePropertiesSession->state == "completed" && !notePropertiesWindow->isVisible() &&
                   s.controller.project.pattern(pattern)->notes[1].velocity == .625f &&
                   s.controller.undoEditChecked().wasOk() &&
                   s.controller.project.state().isEquivalentTo(beforeButtons),
               "Actual Properties Return commits the same staged operation");
        s.pianoNoteModes(request("propertiesOpen"));
        velocity =
            dynamic_cast<juce::TextEditor *>(find(*notePropertiesWindow, "piano.noteProperties.velocity"));
        expect(velocity != nullptr, "Actual numeric field remains available after Return");
        velocity->setText("0.125", false);
        velocity->onTextChange();
        velocity->onEscapeKey();
        expect(notePropertiesSession->state == "cancelled" && !notePropertiesWindow->isVisible() &&
                   s.controller.project.state().isEquivalentTo(beforeButtons),
               "Actual Properties text Escape cancels the captured operation");
        capture(*this, "piano-note-modes-1080.png");
        setSize(320, 400);
        resized();
        const std::array<juce::Component *, 6> controls{&pianoColour,      &pianoInsertSlide,
                                                        &pianoInsertPorta, &pianoProperties,
                                                        &pianoRecolour,    &pianoSelectColour};
        bool fits = true;
        for (const auto *control : controls)
            fits = fits && control->isVisible() && control->getRight() <= getWidth() - rightInset() &&
                   control->getBottom() < gridTop();
        expect(fits && noteModeRows() >= 2,
               "Small Piano window actually wraps every mode control above its musical grid");
        expect(pianoColour.findColour(juce::ComboBox::textColourId) == ink,
               "Every color selector retains readable theme ink rather than a dark hue label");
        capture(*this, "piano-note-modes-small.png");
        setSize(1440, 900);
        resized();
        capture(*this, "piano-note-modes-large.png");
        receipt.getDynamicObject()->setProperty("passed", true);
    }
    catch (const std::exception &error)
    {
        receipt.getDynamicObject()->setProperty("passed", false);
        receipt.getDynamicObject()->setProperty("error", juce::String(error.what()));
    }
    receipt.getDynamicObject()->setProperty("cases", cases);
    receipt.getDynamicObject()->setProperty("nativeOsInput", false);
    receipt.getDynamicObject()->setProperty("listening", false);
    receipt.getDynamicObject()->setProperty("hardware", false);
    receipt.getDynamicObject()->setProperty("screenshotsInspected", false);
    folder.getChildFile("verification.json").replaceWithText(juce::JSON::toString(receipt, true));
    return bool(receipt["passed"]) ? juce::Result::ok() : juce::Result::fail(receipt["error"].toString());
}
} // namespace vibera
