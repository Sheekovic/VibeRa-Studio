// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Studio.h"
#include "Editors.h"
#include "Swing.h"
#include "AudioLogger.h"
#include <cstdlib>

namespace vibera
{
juce::Result Studio::finishRackSwing()
{
    if (windows[0])
        if (auto *slider = static_cast<Editor *>(windows[0]->getContentComponent())->rackSwingControl())
            return slider->finish();
    return juce::Result::ok();
}
juce::Result Studio::setAudioLogger(bool enabled, double minutes)
{
    auto &logger = AudioLogger::instance();
    if (!enabled)
    {
        logger.disable();
        return juce::Result::ok();
    }
    auto *device = audio.engine.getDeviceManager().deviceManager.getCurrentAudioDevice();
    if (!device)
        return juce::Result::fail("audioLog.noDevice: The audio logger needs an audio device");
    return logger.enable(minutes * 60, device->getCurrentSampleRate());
}
void Studio::saveAudioLogDialog()
{
    audioLogChooser = std::make_unique<juce::FileChooser>(
        "Save the logged audio", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                     .getChildFile("Audio log " + juce::Time::getCurrentTime().formatted("%Y-%m-%d %H-%M") + ".wav"),
        "*.wav");
    auto safe = juce::Component::SafePointer<Studio>(this);
    audioLogChooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                                 [safe](const juce::FileChooser &chooser)
                                 {
                                     if (!safe || chooser.getResult() == juce::File{})
                                         return;
                                     double saved = 0;
                                     const auto file = chooser.getResult().withFileExtension("wav");
                                     const auto r = AudioLogger::instance().save(file, 0, saved);
                                     safe->status = r.wasOk() ? "Saved " + juce::String(saved, 1) + " s of audio to " + file.getFileName()
                                                              : r.getErrorMessage().fromFirstOccurrenceOf(": ", false, false);
                                     safe->repaint();
                                 });
}
void Studio::analyzeAudioIntoAutomation(std::int64_t source)
{
    automationAudioChooser = std::make_unique<juce::FileChooser>("Analyze audio file into automation", juce::File{},
                                                                 "*.wav;*.flac;*.aif;*.aiff;*.ogg;*.mp3");
    auto safe = juce::Component::SafePointer<Studio>(this);
    automationAudioChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                        [safe, source](const juce::FileChooser &chooser)
                                        {
                                            if (!safe || !chooser.getResult().existsAsFile())
                                                return;
                                            CommandArgs a;
                                            a.target = source;
                                            a.text = chooser.getResult().getFullPathName();
                                            safe->command("automation.source.fromAudio", a);
                                        });
}
juce::var Studio::audioLoggerState() const
{
    auto *o = new juce::DynamicObject;
    const auto &logger = AudioLogger::instance();
    o->setProperty("enabled", logger.enabled());
    o->setProperty("minutes", logger.capacitySeconds() / 60);
    o->setProperty("recordedSeconds", logger.recordedSeconds());
    return juce::var(o);
}
void Studio::armWaitForInput(bool on)
{
    waitingForInput = on;
    for (auto *capture : {&audio.midiInput.capture, &audio.typingInput.capture})
    {
        capture->waitTriggered.store(false);
        capture->waitArmed.store(on);
    }
}
void Studio::pollWaitForInput()
{
    if (!waitingForInput)
        return;
    if (!midiTaking)
        return armWaitForInput(false); // The take ended elsewhere (Escape, project change).
    auto &hardware = audio.midiInput.capture, &typing = audio.typingInput.capture;
    if (!hardware.waitTriggered.load() && !typing.waitTriggered.load())
        return;
    hardware.waitArmed.store(false);
    typing.waitArmed.store(false);
    if (!audio.playing())
        audio.playStop(0);
    if (audio.playing())
    {
        waitingForInput = false;
        hardware.waitTriggered.store(false);
        typing.waitTriggered.store(false);
        status = "Recording MIDI notes | Stop to keep take; Esc cancels";
        repaint();
    }
}
void Studio::masterPitchDialog()
{
    auto *dialog = new juce::AlertWindow("Master pitch",
                                         "Cents (-1200..1200) for every tunable instrument: the sampler, MPE plugins "
                                         "and MTS-ESP clients. 0 = A440.",
                                         juce::MessageBoxIconType::NoIcon);
    dialog->addTextEditor("cents", juce::String(controller.project.masterPitch, 1), "Cents");
    dialog->addButton("Apply", 1);
    dialog->addButton("Cancel", 0);
    const auto identity = controller.projectIdentity;
    auto safe = juce::Component::SafePointer<Studio>(this);
    auto alert = juce::Component::SafePointer<juce::AlertWindow>(dialog);
    dialog->enterModalState(true,
                            juce::ModalCallbackFunction::create(
                                [safe, alert, identity](int result)
                                {
                                    if (!safe || !alert || result != 1 || safe->controller.projectIdentity != identity)
                                        return;
                                    CommandArgs a;
                                    a.value = alert->getTextEditorContents("cents").getDoubleValue();
                                    safe->command("project.masterPitch", a);
                                }),
                            true);
}
void Studio::swingDialog(const juce::String &action, const std::vector<Id> &ids)
{
    if (finishRackVelocity().failed() || finishRackSwing().failed())
        return;
    const bool global = action == "project.swing";
    double initial = controller.project.swing;
    if (!global)
    {
        const auto *channel = ids.empty() ? nullptr : controller.project.channel(ids.front());
        if (!channel || channel->audioClip)
            return;
        initial = channel->swingMix;
    }
    auto *dialog = new juce::AlertWindow(global ? "Global Swing" : "Channel Swing Mix",
                                         "Percent (0-100). Source notes are retained.",
                                         juce::MessageBoxIconType::NoIcon);
    dialog->addTextEditor("amount", juce::String(initial * 100, 3), "Percent");
    dialog->addButton("Apply", 1);
    dialog->addButton("Cancel", 0);
    const auto identity = controller.projectIdentity;
    auto safe = juce::Component::SafePointer<Studio>(this);
    auto alert = juce::Component::SafePointer<juce::AlertWindow>(dialog);
    dialog->enterModalState(
        true,
        juce::ModalCallbackFunction::create(
            [safe, alert, identity, action, ids](int result)
            {
                if (!safe || !alert || result != 1 || safe->controller.projectIdentity != identity)
                    return;
                const auto text = alert->getTextEditorContents("amount").trim().toStdString();
                char *end = nullptr;
                const double value = std::strtod(text.c_str(), &end);
                if (text.empty() || end != text.c_str() + text.size() || !std::isfinite(value) || value < 0 ||
                    value > 100)
                {
                    safe->status = "Swing percent must be a number from 0 to 100; nothing changed";
                    safe->repaint();
                    return;
                }
                CommandArgs args;
                args.value = value / 100;
                args.ids = ids;
                if (action == "channel.swingMix" && !ids.empty())
                    args.channel = ids.front();
                safe->command(action, args);
            }),
        true);
}
void Studio::addSwingItems(juce::PopupMenu &menu, const std::vector<Id> &ids)
{
    const auto identity = controller.projectIdentity;
    auto safe = juce::Component::SafePointer<Studio>(this);
    bool valid = !ids.empty();
    for (auto id : ids)
    {
        const auto *channel = controller.project.channel(id);
        valid &= channel && !channel->audioClip;
    }
    juce::PopupMenu mix, truncate;
    for (int percent : {0, 25, 50, 75, 100})
    {
        bool checked = valid;
        for (auto id : ids)
            if (const auto *channel = controller.project.channel(id))
                checked &= std::abs(channel->swingMix - percent / 100.) < .000001;
        juce::PopupMenu::Item item(juce::String(percent) + "%");
        item.itemID = 900 + percent;
        item.isEnabled = valid;
        item.isTicked = checked;
        item.action = [safe, identity, ids, percent]
        {
            if (!safe || safe->controller.projectIdentity != identity)
                return;
            CommandArgs args;
            args.ids = ids;
            args.value = percent / 100.;
            safe->command("rack.swingMix", args);
        };
        mix.addItem(std::move(item));
    }
    juce::PopupMenu::Item custom("Custom...");
    custom.itemID = 1001;
    custom.isEnabled = valid;
    custom.action = [safe, identity, ids]
    {
        if (safe && safe->controller.projectIdentity == identity)
            safe->swingDialog("rack.swingMix", ids);
    };
    mix.addItem(std::move(custom));
    for (bool enabled : {true, false})
    {
        bool checked = valid;
        for (auto id : ids)
            if (const auto *channel = controller.project.channel(id))
                checked &= channel->truncateSwing == enabled;
        juce::PopupMenu::Item item(enabled ? "Turn on" : "Turn off");
        item.itemID = enabled ? 1002 : 1003;
        item.isEnabled = valid;
        item.isTicked = checked;
        item.action = [safe, identity, ids, enabled]
        {
            if (!safe || safe->controller.projectIdentity != identity)
                return;
            CommandArgs args;
            args.ids = ids;
            args.enabled = enabled;
            safe->command("rack.truncateSwing", args);
        };
        truncate.addItem(std::move(item));
    }
    menu.addSubMenu("Set swing mix for selected", mix, valid);
    menu.addSubMenu("Set truncate swing notes for selected", truncate, valid);
}
juce::Result Studio::verifyRackSwing(const juce::File &folder, const juce::File &)
{
    auto fail = [](const juce::String &message) { return juce::Result::fail("Swing UI: " + message); };
    audio.stop();
    if (auto result = finishMidiTake(); result.failed()) return result;
    finishRackVelocity();
    finishRackSwing();
    auto *editor = static_cast<Editor *>(windows[0]->getContentComponent());
    auto *slider = editor->rackSwingControl();
    if (!slider)
        return fail("missing functional global control");
    const auto before = controller.project.state();
    slider->onDragStart();
    slider->setValue(30, juce::sendNotificationSync);
    slider->setValue(65, juce::sendNotificationSync);
    slider->onDragEnd();
    if (controller.project.swing != .65 || controller.hasGesture())
        return fail("actual continuous slider handlers");
    const auto changed = controller.project.state();
    controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(before))
        return fail("one slider drag Undo");
    controller.redoEdit();
    if (!controller.project.state().isEquivalentTo(changed))
        return fail("slider exact Redo");
    slider->onDragStart();
    slider->setValue(40, juce::sendNotificationSync);
    performAction("selection.cancel");
    slider->onDragEnd();
    if (!controller.project.state().isEquivalentTo(changed))
        return fail("Escape cancelled exact slider baseline");
    const std::vector<Id> targets{controller.project.channels[0].id, controller.project.channels[1].id};
    juce::PopupMenu menu;
    addSwingItems(menu, targets);
    std::function<void()> mix, truncate;
    for (juce::PopupMenu::MenuItemIterator item(menu, true); item.next();)
    {
        if (item.getItem().itemID == 950)
            mix = item.getItem().action;
        if (item.getItem().itemID == 1002)
            truncate = item.getItem().action;
    }
    if (!mix || !truncate)
        return fail("real bulk/context Swing menu actions missing");
    mix();
    if (controller.project.channel(targets[0])->swingMix != .5 ||
        controller.project.channel(targets[1])->swingMix != .5)
        return fail("menu Mix selected stable Channels");
    truncate();
    if (!controller.project.channel(targets[0])->truncateSwing ||
        !controller.project.channel(targets[1])->truncateSwing)
        return fail("menu truncation selected Channels");
    controller.undoEdit();
    controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(changed))
        return fail("menu changes exact separate history");
    const auto path = folder.getChildFile("swing-workspace.vibra");
    if (saveProject(controller.project, path).failed())
        return fail("populated Swing fixture save");
    const juce::Point<int> normal{getWidth(), getHeight()};
    for (bool large : {false, true})
    {
        setSize(large ? 2560 : 1280, large ? 1392 : 684);
        resized();
        refresh();
        auto stream = folder.getChildFile(large ? "swing-4k.png" : "swing-1080p.png").createOutputStream();
        if (!stream)
            return fail("PNG output");
        stream->setPosition(0);
        stream->truncate();
        if (!juce::PNGImageFormat{}.writeImageToStream(createComponentSnapshot(getLocalBounds(), true, 1.5f),
                                                       *stream))
            return fail("PNG encoding");
    }
    setSize(normal.x, normal.y);
    resized();
    folder.getChildFile("swing-gui.txt")
        .replaceWithText(
            "PASS actual global slider continuous handlers/one Undo/Redo/Escape and bulk Mix/truncation menu "
            "actions; populated PNGs and schema13 project. Component input, not OS interaction/listening.\n");
    return juce::Result::ok();
}
} // namespace vibera
