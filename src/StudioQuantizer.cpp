// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Studio.h"
#include "Quantizer.h"
#include "../tests/SyntheticDevice.h"
namespace vibera
{
namespace
{
class QuantizerPanel final : public juce::Component, private juce::Timer, private juce::KeyListener
{
    juce::Component::SafePointer<Studio> studio;
    CommandArgs args;
    juce::String identity, owner = "quantizer-" + juce::Uuid().toString();
    std::uint64_t serial = 0;
    bool finished = false;
    double initialGrid;
    juce::Label heading, hint, gridLabel, modeLabel, startLabel, sensitivityLabel, durationLabel, grooveLabel,
        velocityLabel;
    juce::ComboBox gridBox, modeBox, grooveBox;
    std::optional<GrooveTemplate> customGroove;
    std::unique_ptr<juce::FileChooser> chooser;
    bool filePending = false;
    juce::ToggleButton rests{"Apply zero-velocity rests (deletes matching notes)"};
    juce::TextButton capture{"Capture"}, load{"Load..."}, save{"Save..."};
    juce::Slider start, sensitivity, duration, velocity;
    juce::TextButton reset{"Reset"}, accept{"Accept"}, cancel{"Cancel"};

    class Guide final : public juce::Component
    {
        QuantizerPanel &owner;

      public:
        explicit Guide(QuantizerPanel &p) : owner(p)
        {
            setComponentID("quantizer.guide");
        }
        void paint(juce::Graphics &g) override
        {
            g.setColour(bg);
            g.fillRoundedRectangle(getLocalBounds().toFloat(), 4);
            g.setColour(muted);
            const auto &groove = owner.args.groove;
            g.drawText(groove ? groove->name + " | " + juce::String(int(groove->points.size())) +
                                    " points / " + juce::String(groove->cycle, 3) + " beats"
                              : "Uniform grid - no velocity template",
                       8, 2, getWidth() - 16, 18, juce::Justification::centredLeft);
            g.setColour(orange.withAlpha(.8f));
            if (groove)
                for (auto beat : grooveGuidePositions(*groove, 0, groove->cycle, 256))
                    g.drawVerticalLine(int(8 + beat / groove->cycle * (getWidth() - 16)), 24.f,
                                       float(getHeight() - 6));
        }
    } guide{*this};
    void useCustom(GrooveTemplate groove)
    {
        customGroove = std::move(groove);
        const auto title = customGroove->name;
        if (grooveBox.indexOfItemId(100) < 0)
            grooveBox.addItem(title, 100);
        else
            grooveBox.changeItemText(100, title);
        grooveBox.setSelectedId(100, juce::dontSendNotification);
        update();
    }
    void captureScore()
    {
        if (!valid())
            return;
        const auto *baseline = studio->controller.quantizerBaseline();
        const auto *pattern = baseline ? baseline->pattern(args.pattern) : nullptr;
        const auto *channel = baseline ? baseline->channel(args.channel) : nullptr;
        if (!pattern)
            return;
        GrooveTemplate groove;
        const auto name = ((channel ? channel->name : pattern->name) + " groove").substring(0, 96).trim();
        const auto result =
            captureGroove(*baseline, args.pattern, args.channel, args.ids, pattern->length, name, groove);
        if (result.failed())
            hint.setText(result.getErrorMessage(), juce::dontSendNotification);
        else
            useCustom(std::move(groove));
    }
    void chooseFile(bool writing)
    {
        if (!valid() || filePending || (writing && !args.groove))
            return;
        filePending = true;
        state("pending");
        chooser = std::make_unique<juce::FileChooser>(writing ? "Save VibeRa groove" : "Load VibeRa groove",
                                                      juce::File{}, "*.vgroove");
        auto safe = juce::Component::SafePointer<QuantizerPanel>(this);
        chooser->launchAsync(
            writing ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                          juce::FileBrowserComponent::warnAboutOverwriting
                    : juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safe, writing](const juce::FileChooser &choice)
            {
                if (!safe)
                    return;
                safe->filePending = false;
                if (!safe->valid())
                    return;
                const auto file = choice.getResult();
                if (file == juce::File{})
                {
                    safe->state("pending");
                    return;
                }
                const auto result = writing ? safe->exportTemplate(file, true) : safe->importTemplate(file);
                if (result.failed())
                    safe->hint.setText(result.getErrorMessage(), juce::dontSendNotification);
                else if (writing)
                    safe->hint.setText("Groove saved: " + file.getFileName(), juce::dontSendNotification);
                safe->state("pending");
            });
    }
    bool valid() const
    {
        return studio && studio->controller.projectIdentity == identity && studio->controller.hasGesture() &&
               studio->controller.gestureOwner() == owner && studio->controller.gestureSerial() == serial &&
               studio->selectedPattern == args.pattern && studio->selectedChannel == args.channel;
    }
    void state(const char *completion)
    {
        if (!studio)
            return;
        auto value = juce::var(new juce::DynamicObject());
        auto *p = value.getDynamicObject();
        p->setProperty("completionState", completion);
        p->setProperty("pattern", juce::int64(args.pattern));
        p->setProperty("channel", juce::int64(args.channel));
        p->setProperty("gridBeats", args.length);
        p->setProperty("mode", args.text);
        p->setProperty("strength", args.strength);
        p->setProperty("sensitivity", args.sensitivity);
        p->setProperty("durationStrength", args.durationStrength);
        p->setProperty("velocityStrength", args.velocityStrength);
        p->setProperty("zeroVelocityDeletes", args.zeroVelocityDeletes);
        p->setProperty("fileDialogPending", filePending);
        p->setProperty("grooveName", args.groove ? args.groove->name : "Uniform grid");
        p->setProperty("groovePoints", args.groove ? int(args.groove->points.size()) : 0);
        juce::Array<juce::var> ids;
        for (auto id : args.ids)
            ids.add(juce::int64(id));
        p->setProperty("noteIds", ids);
        studio->quantizerPreviewState = value;
    }
    void update()
    {
        if (!valid())
            return;
        static constexpr double grids[]{.03125, .0625, .125, .25, .5, 1, 2, 4};
        args.length = gridBox.getSelectedId() == 9 ? initialGrid : grids[gridBox.getSelectedId() - 1];
        args.text =
            juce::StringArray{"duration", "end", "leaveDuration", "leaveEnd"}[modeBox.getSelectedId() - 1];
        if (grooveBox.getSelectedId() == 1)
            args.groove.reset();
        else if (grooveBox.getSelectedId() == 100)
            args.groove = customGroove;
        else
            args.groove = factoryGrooves()[size_t(grooveBox.getSelectedId() - 2)];
        args.velocityStrength = velocity.getValue() / 100;
        args.zeroVelocityDeletes = rests.getToggleState();
        args.strength = start.getValue() / 100;
        args.sensitivity = sensitivity.getValue() / 100;
        args.durationStrength = duration.getValue() / 100;
        studio->command("notes.quantize", args, owner);
        hint.setText(studio->status == "Ready"
                         ? "Live preview - Accept keeps one Undo; Cancel restores every note."
                         : studio->status,
                     juce::dontSendNotification);
        duration.setEnabled(modeBox.getSelectedId() <= 2);
        gridBox.setEnabled(!args.groove.has_value());
        velocity.setEnabled(args.groove.has_value());
        rests.setEnabled(args.groove.has_value());
        save.setEnabled(args.groove.has_value());
        guide.repaint();
        state("pending");
    }
    void finish(bool keep)
    {
        if (finished)
            return;
        const bool owned = valid();
        auto result = juce::Result::ok();
        if (owned)
            result = keep ? studio->controller.endGestureChecked("Quantize notes", owner)
                          : studio->controller.cancelGestureChecked(owner);
        else if (studio && studio->controller.gestureOwner() == owner)
            result = studio->controller.cancelGestureChecked(owner);
        if (result.failed())
        {
            studio->status = result.getErrorMessage();
            hint.setText(result.getErrorMessage() + " Retry Accept or Cancel to restore the preview.",
                         juce::dontSendNotification);
            state("pending");
            auto error = juce::var(new juce::DynamicObject());
            error.getDynamicObject()->setProperty("code", "history_refused");
            error.getDynamicObject()->setProperty("message", result.getErrorMessage());
            studio->quantizerPreviewState.getDynamicObject()->setProperty("error", error);
            studio->repaint();
            return; // Keep the modal window, timer, owner and baseline alive.
        }
        finished = true;
        filePending = false;
        stopTimer();
        state(keep && owned ? "completed" : "cancelled");
        if (auto *window = findParentComponentOfClass<juce::DialogWindow>())
            window->exitModalState(keep && owned ? 1 : 0);
    }
    bool keyPressed(const juce::KeyPress &key, juce::Component *) override
    {
        if (key.isKeyCode(juce::KeyPress::returnKey))
        {
            finish(true);
            return true;
        }
        if (key.isKeyCode(juce::KeyPress::escapeKey))
        {
            finish(false);
            return true;
        }
        return false;
    }
    void timerCallback() override
    {
        if (!valid())
            finish(false);
    }

  public:
    juce::Result importTemplate(const juce::File &file)
    {
        if (!valid())
            return juce::Result::fail("Quantizer preview expired");
        GrooveTemplate groove;
        if (auto result = loadGroove(file, groove); result.failed())
            return result;
        useCustom(std::move(groove));
        return juce::Result::ok();
    }
    juce::Result exportTemplate(const juce::File &file, bool overwrite)
    {
        if (!valid() || !args.groove)
            return juce::Result::fail("Select a groove template before saving");
        return saveGroove(*args.groove, file, overwrite);
    }
    QuantizerPanel(Studio &s, CommandArgs target)
        : studio(&s), args(std::move(target)), identity(s.controller.projectIdentity),
          initialGrid(args.length)
    {
        s.controller.beginGesture(owner);
        serial = s.controller.gestureSerial();
        setComponentID("piano.quantizer");
        setSize(640, 550);
        auto label = [&](juce::Label &l, const char *text)
        {
            l.setText(text, juce::dontSendNotification);
            l.setColour(juce::Label::textColourId, ink);
            addAndMakeVisible(l);
        };
        label(heading, "PIANO ROLL / QUANTIZER");
        label(hint, "Reusable timing/velocity grooves. Preview changes are reversible.");
        label(grooveLabel, "Template");
        label(velocityLabel, "Velocity");
        label(gridLabel, "Grid");
        label(modeLabel, "Mode");
        label(startLabel, "Start time");
        label(sensitivityLabel, "Sensitivity");
        label(durationLabel, "Duration / end");
        gridBox.addItemList({"1/128 note", "1/64 note", "1/32 note", "1/16 note", "1/8 note", "1/4 note",
                             "1/2 note", "Whole note (4 beats)"},
                            1);
        gridBox.addItem("Current snap (" + juce::String(initialGrid, 5) + " beats)", 9);
        modeBox.addItemList({"Quantize duration", "Quantize end time", "Leave duration", "Leave end time"},
                            1);
        gridBox.setSelectedId(9, juce::dontSendNotification);
        modeBox.setSelectedId(1, juce::dontSendNotification);
        grooveBox.addItem("Uniform grid", 1);
        int presetId = 2;
        for (const auto &groove : factoryGrooves())
            grooveBox.addItem(groove.name, presetId++);
        grooveBox.setSelectedId(1, juce::dontSendNotification);
        rests.setToggleState(true, juce::dontSendNotification);
        for (auto *control : {&start, &sensitivity, &duration, &velocity})
        {
            control->setRange(0, 100, .1);
            control->setSliderStyle(juce::Slider::LinearHorizontal);
            control->setTextBoxStyle(juce::Slider::TextBoxRight, false, 72, 24);
            control->setTextValueSuffix(" %");
            control->setValue(100, juce::dontSendNotification);
            control->onValueChange = [this] { update(); };
        }
        velocity.setValue(0, juce::dontSendNotification);
        for (auto *c : std::initializer_list<juce::Component *>{
                 &gridBox, &modeBox, &start, &sensitivity, &duration, &velocity, &grooveBox, &rests, &capture,
                 &load, &save, &guide, &reset, &accept, &cancel})
            addAndMakeVisible(c);
        gridBox.setComponentID("quantizer.grid");
        modeBox.setComponentID("quantizer.mode");
        start.setComponentID("quantizer.start");
        sensitivity.setComponentID("quantizer.sensitivity");
        duration.setComponentID("quantizer.duration");
        reset.setComponentID("quantizer.reset");
        accept.setComponentID("quantizer.accept");
        cancel.setComponentID("quantizer.cancel");
        grooveBox.setComponentID("quantizer.groove");
        velocity.setComponentID("quantizer.velocity");
        rests.setComponentID("quantizer.rests");
        capture.setComponentID("quantizer.capture");
        load.setComponentID("quantizer.load");
        save.setComponentID("quantizer.save");
        capture.setTooltip("Capture original selected notes/current Channel part before this preview; "
                           "Pattern-zero phase, pitch ignored.");
        capture.onClick = [this] { captureScore(); };
        load.onClick = [this] { chooseFile(false); };
        save.onClick = [this] { chooseFile(true); };
        rests.onClick = [this] { update(); };
        grooveBox.addKeyListener(this);
        grooveBox.onChange = [this] { update(); };
        gridBox.addKeyListener(this);
        modeBox.addKeyListener(this);
        gridBox.onChange = modeBox.onChange = [this] { update(); };
        reset.onClick = [this]
        {
            gridBox.setSelectedId(9, juce::dontSendNotification);
            modeBox.setSelectedId(1, juce::dontSendNotification);
            for (auto *slider : {&start, &sensitivity, &duration})
                slider->setValue(100, juce::dontSendNotification);
            velocity.setValue(0, juce::dontSendNotification);
            grooveBox.setSelectedId(1, juce::dontSendNotification);
            rests.setToggleState(true, juce::dontSendNotification);
            update();
        };
        accept.onClick = [this] { finish(true); };
        cancel.onClick = [this] { finish(false); };
        accept.addShortcut(juce::KeyPress(juce::KeyPress::returnKey));
        cancel.addShortcut(juce::KeyPress(juce::KeyPress::escapeKey));
        update();
        startTimerHz(20);
    }
    ~QuantizerPanel() override
    {
        if (!finished && studio && studio->controller.gestureOwner() == owner)
        {
            filePending = false;
            if (auto result = studio->controller.cancelGestureChecked(owner); result.failed())
            {
                studio->status = result.getErrorMessage();
                state("pending");
                auto error = juce::var(new juce::DynamicObject());
                error.getDynamicObject()->setProperty("code", "history_refused");
                error.getDynamicObject()->setProperty("message", result.getErrorMessage());
                studio->quantizerPreviewState.getDynamicObject()->setProperty("error", error);
            }
            else
                state("cancelled");
        }
    }
    void paint(juce::Graphics &g) override
    {
        g.fillAll(panel);
    }
    void resized() override
    {
        heading.setBounds(18, 10, 604, 28);
        grooveLabel.setBounds(18, 48, 76, 28);
        grooveBox.setBounds(98, 48, 264, 28);
        capture.setBounds(372, 48, 78, 28);
        load.setBounds(460, 48, 72, 28);
        save.setBounds(542, 48, 80, 28);
        gridLabel.setBounds(18, 90, 76, 28);
        gridBox.setBounds(98, 90, 524, 28);
        modeLabel.setBounds(18, 128, 76, 28);
        modeBox.setBounds(98, 128, 524, 28);
        auto row = [&](juce::Label &l, juce::Slider &s, int y)
        {
            l.setBounds(18, y, 128, 32);
            s.setBounds(154, y, 468, 32);
        };
        row(startLabel, start, 174);
        row(sensitivityLabel, sensitivity, 218);
        row(durationLabel, duration, 262);
        row(velocityLabel, velocity, 306);
        rests.setBounds(18, 350, 604, 28);
        guide.setBounds(18, 388, 604, 66);
        hint.setBounds(18, 460, 604, 40);
        reset.setBounds(18, 514, 92, 28);
        cancel.setBounds(420, 514, 92, 28);
        accept.setBounds(530, 514, 92, 28);
    }
};
} // namespace
void Studio::openQuantizer()
{
    if (quantizerWindow)
    {
        quantizerWindow->toFront(true);
        return;
    }
    if (finishRackVelocity().failed() || finishRackSwing().failed())
        return;
    if (auto admitted = quantizerEditAdmission(recordArmed, midiTaking || hasPendingMidiTake(),
                                               bool(afterPluginState) || bool(pendingExport),
                                               audio.rendering(), midiFileBusy);
        admitted.failed())
    {
        status = admitted.getErrorMessage();
        return;
    }
    if (customization.running || controller.hasGesture())
    {
        status = "Finish the current edit before opening Quantizer";
        return;
    }
    CommandArgs args;
    args.pattern = selectedPattern;
    args.channel = selectedChannel;
    args.ids = selectedNotes;
    args.length = snap > 0 ? snap : .25;
    args.text = "duration";
    auto checked = controller.project;
    if (auto result = quantizeNotes(checked, controller.project, args, "notes.quantize"); result.failed())
    {
        status = result.getErrorMessage();
        return;
    }
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(new QuantizerPanel(*this, args));
    options.dialogTitle = "Quantizer";
    options.dialogBackgroundColour = panel;
    options.componentToCentreAround = this;
    options.useNativeTitleBar = false;
    options.resizable = false;
    options.escapeKeyTriggersCloseButton = true;
    quantizerWindow = options.launchAsync();
    if (auto *window = dynamic_cast<juce::DialogWindow *>(quantizerWindow.getComponent()))
        if (auto *button = window->getContentComponent()->findChildWithID("quantizer.accept"))
            button->grabKeyboardFocus();
}
juce::Result Studio::verifyQuantizer(const juce::File &folder)
{
    auto fail = [](const juce::String &message) { return juce::Result::fail("Quantizer UI: " + message); };
    audio.stop();
    if (auto result = finishMidiTake(); result.failed())
        return result;
    finishRackVelocity();
    finishRackSwing();
    auto project = controller.project;
    if (project.channels.size() < 2 || project.patterns.empty())
        return fail("fixture targets");
    selectedPattern = project.patterns[0].id;
    selectedChannel = project.channels[0].id;
    project.patterns[0].notes = {{project.allocate(), selectedChannel, .08, .34, 60, .7f, false},
                                 {project.allocate(), project.channels[1].id, .13, .28, 67, .6f, false}};
    controller.replace(project);
    selectedNotes.clear();
    focus = Scope::piano;
    const auto before = controller.project.state();
    auto pump = [] { dispatchMessages(100); };
    auto content = [&]() -> juce::Component *
    {
        if (auto *window = dynamic_cast<juce::DialogWindow *>(quantizerWindow.getComponent()))
            return window->getContentComponent();
        return nullptr;
    };
    performAction("notes.quantize");
    auto *view = content();
    if (!view || quantizerPreviewState["completionState"].toString() != "pending")
        return fail("Alt+Q action did not open live preview");
    auto *start = dynamic_cast<juce::Slider *>(view->findChildWithID("quantizer.start"));
    auto *duration = dynamic_cast<juce::Slider *>(view->findChildWithID("quantizer.duration"));
    auto *mode = dynamic_cast<juce::ComboBox *>(view->findChildWithID("quantizer.mode"));
    if (!start || !duration || !mode)
        return fail("functional controls missing");
    start->setValue(0, juce::sendNotificationSync);
    duration->setValue(0, juce::sendNotificationSync);
    if (!controller.project.state().isEquivalentTo(before))
        return fail("baseline restoration through actual Slider handlers");
    auto click = [&](const char *id)
    {
        auto *button = dynamic_cast<juce::TextButton *>(content()->findChildWithID(id));
        if (!button || !button->onClick)
            return false;
        button->onClick();
        return true;
    };
    if (!click("quantizer.reset") || start->getValue() != 100 || duration->getValue() != 100)
        return fail("Reset defaults");
    mode->setSelectedId(2, juce::sendNotificationSync);
    if (std::abs(controller.project.patterns[0].notes[0].length - .5) > 1.e-9)
        return fail("actual end mode");
    mode->setSelectedId(4, juce::sendNotificationSync);
    if (duration->isEnabled() || std::abs(controller.project.patterns[0].notes[0].length - .42) > 1.e-9)
        return fail("actual leave-end mode and disabled duration");
    auto stream = folder.getChildFile("quantizer-dialog.png").createOutputStream();
    if (!stream)
        return fail("dialog evidence output");
    stream->setPosition(0);
    stream->truncate();
    juce::PNGImageFormat().writeImageToStream(
        quantizerWindow->createComponentSnapshot(quantizerWindow->getLocalBounds()), *stream);
    stream.reset();
    if (!click("quantizer.cancel"))
        return fail("Cancel button");
    pump();
    if (!controller.project.state().isEquivalentTo(before) || controller.hasGesture())
        return fail("exact Cancel");
    openQuantizer();
    const auto after = controller.project.state();
    if (!click("quantizer.accept"))
        return fail("Accept button");
    pump();
    if (controller.hasGesture() || !controller.project.state().isEquivalentTo(after))
        return fail("terminal Accept");
    controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(before))
        return fail("one Undo");
    controller.redoEdit();
    if (!controller.project.state().isEquivalentTo(after))
        return fail("exact Redo");
    selectedNotes = {project.patterns[0].notes[0].id};
    const auto originalSelection = selectedNotes;
    controller.replace(project);
    openQuantizer();
    auto *groovePanel = dynamic_cast<QuantizerPanel *>(content());
    auto *templates = dynamic_cast<juce::ComboBox *>(content()->findChildWithID("quantizer.groove"));
    auto *levels = dynamic_cast<juce::Slider *>(content()->findChildWithID("quantizer.velocity"));
    auto *rest = dynamic_cast<juce::ToggleButton *>(content()->findChildWithID("quantizer.rests"));
    if (!groovePanel || !templates || !levels || !rest || !click("quantizer.capture"))
        return fail("Groove controls/capture");
    if (quantizerPreviewState["groovePoints"] != juce::var(1) ||
        std::abs(controller.project.patterns[0].notes[0].beat - .08) > 1.e-9)
        return fail("Capture must use original pre-preview score");
    templates->setSelectedId(3, juce::sendNotificationSync);
    levels->setValue(100, juce::sendNotificationSync);
    if (!levels->isEnabled() || !quantizerGuide || quantizerGuide->points.size() != 16 ||
        std::abs(controller.project.patterns[0].notes[0].velocity - .9) > 1.e-6)
        return fail("Preset/velocity handlers and actual guide model");
    const auto exported = folder.getChildFile("ui-groove.vgroove");
    if (groovePanel->exportTemplate(exported, true).failed())
        return fail("UI save adapter");
    GrooveTemplate reopened;
    if (loadGroove(exported, reopened).failed() || reopened.points.size() != 16)
        return fail("Saved UI groove readback");
    auto imageStream = folder.getChildFile("groove-dialog.png").createOutputStream();
    if (!imageStream)
        return fail("Groove dialog evidence output");
    imageStream->setPosition(0);
    imageStream->truncate();
    juce::PNGImageFormat().writeImageToStream(
        quantizerWindow->createComponentSnapshot(quantizerWindow->getLocalBounds()), *imageStream);
    imageStream.reset();
    reopened.points[0].velocity = 0;
    const auto restsFile = folder.getChildFile("ui-rests.vgroove");
    if (saveGroove(reopened, restsFile, true).failed() || groovePanel->importTemplate(restsFile).failed())
        return fail("Actual UI import adapter");
    if (controller.project.patterns[0].notes.size() != 1 || selectedNotes != originalSelection)
        return fail("Rest preview source selection ownership");
    rest->setToggleState(false, juce::dontSendNotification);
    rest->onClick();
    levels->setValue(0, juce::sendNotificationSync);
    if (controller.project.patterns[0].notes.size() != 2 ||
        controller.project.patterns[0].notes[0].id != originalSelection[0])
        return fail("Rest toggle restores original identities");
    if (!click("quantizer.reset") || templates->getSelectedId() != 1 || levels->getValue() != 0 ||
        quantizerGuide)
        return fail("Reset clears template/guides and restores defaults");
    if (!click("quantizer.cancel"))
        return fail("Groove Cancel");
    pump();
    if (!controller.project.state().isEquivalentTo(before) || selectedNotes != originalSelection)
        return fail("Groove exact Cancel/selection");
    folder.getChildFile("groove-gui.txt")
        .replaceWithText("PASS actual capture/preset/velocity/rest/reset/cancel callbacks, baseline source "
                         "selection, Piano Roll guide model and shared UI file adapters with real save/load. "
                         "File-picker OS interaction is separate.\n");
    const auto path = folder.getChildFile("quantizer-workspace.vibra");
    if (saveProject(project, path).failed())
        return fail("native fixture");
    folder.getChildFile("quantizer-gui.txt")
        .replaceWithText(
            "PASS real component Slider/ComboBox/Reset/Cancel/Accept, baseline/selection/history and dialog "
            "snapshot. Not OS input, listening or hardware.\n");
    return juce::Result::ok();
}
} // namespace vibera
