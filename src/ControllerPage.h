// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Studio.h"

namespace vibera
{
class ControllerPage final : public juce::Component, private juce::ListBoxModel, private juce::Timer
{
    juce::Component::SafePointer<Studio> owner;
    juce::ComboBox input, output, action, mode, port, channel, kind, layer, mixer, sendDestination;
    juce::ListBox list{"Controller mappings", this};
    juce::TextEditor title, minimum, maximum, number;
    juce::ToggleButton pickup{"Pickup (avoid value jumps)"}, banked{"Follow Mixer bank"},
        enabled{"Mapping enabled"}, nativeMode{"Follow Launchkey mode"};
    juce::TextButton add{"Add mapping"}, remove{"Remove"}, apply{"Apply"}, learn{"MIDI Learn"},
        cancel{"Cancel Learn"}, preset{"Launchkey MK3 preset"}, connect{"Connect Launchkey"};
    juce::TextButton projectLinks{"Project sampler links"};
    juce::Label heading, ports, live, hint, message;
    std::vector<juce::String> actionIds;
    std::vector<int> mixerTargets, sendTargets;
    juce::String bindingProfileToken;
    std::optional<int> chosenSendTarget() const
    {
        const int i = sendDestination.getSelectedId() - 1;
        return i >= 0 && i < int(sendTargets.size()) ? std::optional<int>{sendTargets[size_t(i)]}
                                                     : std::nullopt;
    }
    void populateSendTargets(int wanted)
    {
        if (!owner)
            return;
        sendDestination.clear(juce::dontSendNotification);
        sendTargets.clear();
        const auto add = [&](int id, const juce::String &label)
        {
            sendTargets.push_back(id);
            sendDestination.addItem(label, int(sendTargets.size()));
        };
        for (const auto &insert : owner->controller.project.mixer)
            add(insert.index, "Send to " + insert.name + " [" + juce::String(insert.index) + "]");
        if (std::find(sendTargets.begin(), sendTargets.end(), wanted) == sendTargets.end())
            add(wanted, wanted < 0 ? "Choose send destination"
                                   : "Unavailable destination [" + juce::String(wanted) + "]");
        const auto i = std::find(sendTargets.begin(), sendTargets.end(), wanted);
        sendDestination.setSelectedId(int(i - sendTargets.begin()) + 1, juce::dontSendNotification);
    }
    std::vector<std::pair<int, juce::String>> observedMixerRows;
    juce::String observedMixerProject;
    bool observedBanked = false;
    std::optional<int> chosenMixerTarget() const
    {
        const int selected = mixer.getSelectedId() - 1;
        if (selected < 0 || selected >= int(mixerTargets.size()))
            return {};
        return mixerTargets[size_t(selected)];
    }
    void populateMixerTargets(int wanted, bool followBank)
    {
        if (!owner)
            return;
        mixer.clear(juce::dontSendNotification);
        mixerTargets.clear();
        observedMixerRows.clear();
        const auto addTarget = [&](int target, const juce::String &name)
        {
            mixerTargets.push_back(target);
            mixer.addItem(name, int(mixerTargets.size()));
        };
        addTarget(-1, "Selected Mixer insert");
        addTarget(0, "Master (independent of bank)");
        for (const auto &insert : owner->controller.project.mixer)
            observedMixerRows.emplace_back(insert.index, insert.name);
        if (followBank)
            for (int slot = 1; slot < MixerNativeIdentity::capacity; ++slot)
                addTarget(slot, "Bank display slot " + juce::String(slot));
        else
            for (const auto &insert : owner->controller.project.mixer)
                if (insert.index != 0)
                    addTarget(insert.index, insert.name + " [ID " + juce::String(insert.index) + "]");
        if (std::find(mixerTargets.begin(), mixerTargets.end(), wanted) == mixerTargets.end())
            addTarget(wanted,
                      (followBank ? "Invalid bank slot " : "Unavailable insert ID ") + juce::String(wanted));
        const auto found = std::find(mixerTargets.begin(), mixerTargets.end(), wanted);
        mixer.setSelectedId(int(found - mixerTargets.begin()) + 1, juce::dontSendNotification);
        observedMixerProject = owner->controller.projectIdentity;
        observedBanked = followBank;
    }
    void refreshMixerTargets()
    {
        if (!owner)
            return;
        std::vector<std::pair<int, juce::String>> actual;
        for (const auto &insert : owner->controller.project.mixer)
            actual.emplace_back(insert.index, insert.name);
        if (actual != observedMixerRows || observedMixerProject != owner->controller.projectIdentity ||
            observedBanked != banked.getToggleState())
        {
            const auto destination = chosenSendTarget().value_or(-1);
            populateMixerTargets(chosenMixerTarget().value_or(-1), banked.getToggleState());
            populateSendTargets(destination);
        }
    }

    juce::Array<juce::MidiDeviceInfo> inputs, outputs;
    bool rebuilding = false, wasLearning = false;
    int row = -1;
    int getNumRows() override
    {
        return owner ? int(owner->midiController.bindings.size()) : 0;
    }
    void populateActions(int selectedMode, const juce::String &selectedAction)
    {
        action.clear(juce::dontSendNotification);
        actionIds.clear();
        int selected = 0;
        for (const auto &command : commandRegistry())
        {
            if (!command.userAction)
                continue;
            if (selectedMode > 0 && selectedMode != 4 &&
                !MidiController::parameterActions().contains(command.id))
                continue;
            const int id = action.getNumItems() + 1;
            action.addItem(command.name, id);
            actionIds.push_back(command.id);
            if (command.id == selectedAction)
                selected = id;
        }
        if (selectedMode == 0)
            for (const auto &macro : owner->customization.macros)
            {
                const int id = action.getNumItems() + 1;
                action.addItem(macro.name, id);
                actionIds.push_back(macro.id);
                if (macro.id == selectedAction)
                    selected = id;
            }
        action.setSelectedId(selected, juce::dontSendNotification);
        action.setEnabled(selectedMode != 4);
    }
    void paintListBoxItem(int index, juce::Graphics &g, int width, int height, bool selected) override
    {
        if (!owner || index < 0 || index >= getNumRows())
            return;
        const auto &binding = owner->midiController.bindings[size_t(index)];
        g.fillAll(selected ? grid : panel);
        label(g, binding.label.isNotEmpty() ? binding.label : binding.action, {8, 0, width - 16, height}, ink,
              12);
    }
    void selectedRowsChanged(int index) override
    {
        if (!owner || index < 0 || index >= getNumRows())
        {
            row = -1;
            return;
        }
        row = index;
        bindingProfileToken.clear();
        if (auto identified = owner->midiController.bindingProfileIdentity(bindingProfileToken);
            identified.failed())
            message.setText(identified.getErrorMessage(), juce::dontSendNotification);
        const auto &binding = owner->midiController.bindings[size_t(row)];
        rebuilding = true;
        title.setText(binding.label, false);
        populateActions(binding.mode, binding.action);
        mode.setSelectedId(binding.mode + 1, juce::dontSendNotification);
        port.setSelectedId(binding.port + 1, juce::dontSendNotification);
        channel.setSelectedId(binding.channel + 1, juce::dontSendNotification);
        kind.setSelectedId(binding.kind + 1, juce::dontSendNotification);
        layer.setSelectedId(binding.layer + 1, juce::dontSendNotification);

        number.setText(juce::String(binding.number), false);
        minimum.setText(juce::String(binding.minimum), false);
        maximum.setText(juce::String(binding.maximum), false);
        pickup.setToggleState(binding.pickup, juce::dontSendNotification);
        banked.setToggleState(binding.banked, juce::dontSendNotification);
        populateMixerTargets(binding.mixer, binding.banked);
        populateSendTargets(binding.sendTarget);
        sendDestination.setVisible(binding.action == "mixer.send");
        resized();
        enabled.setToggleState(binding.enabled, juce::dontSendNotification);
        nativeMode.setToggleState(binding.nativeArea != 0, juce::dontSendNotification);
        rebuilding = false;
    }
    juce::String chosenAction() const
    {
        const int index = action.getSelectedId() - 1;
        return index >= 0 && index < int(actionIds.size()) ? actionIds[size_t(index)] : juce::String{};
    }
    bool commit()
    {
        if (!owner || row < 0 || row >= getNumRows())
            return false;
        auto binding = owner->midiController.bindings[size_t(row)];
        binding.label = title.getText();
        binding.action = chosenAction();
        binding.mode = mode.getSelectedId() - 1;
        binding.port = port.getSelectedId() - 1;
        binding.channel = channel.getSelectedId() - 1;
        binding.kind = kind.getSelectedId() - 1;
        binding.layer = layer.getSelectedId() - 1;
        const auto target = chosenMixerTarget();
        if (!target)
        {
            message.setText("Choose an explicit Mixer target; mapping preserved", juce::dontSendNotification);
            return false;
        }
        binding.mixer = *target;
        binding.sendTarget = binding.action == "mixer.send" ? chosenSendTarget().value_or(-1) : -1;
        binding.number = number.getText().getIntValue();
        binding.minimum = minimum.getText().getDoubleValue();
        binding.maximum = maximum.getText().getDoubleValue();
        binding.pickup = pickup.getToggleState();
        binding.banked = banked.getToggleState();
        binding.enabled = enabled.getToggleState();
        binding.nativeArea = nativeMode.getToggleState() ? (binding.number >= 53 ? 2 : 1) : 0;
        const auto result = owner->midiController.setBinding(size_t(row), binding, bindingProfileToken);
        if (result.wasOk())
        {
            bindingProfileToken.clear();
            (void)owner->midiController.bindingProfileIdentity(bindingProfileToken);
        }
        message.setText(result.wasOk() ? "Mapping saved" : result.getErrorMessage(),
                        juce::dontSendNotification);
        list.updateContent();
        list.repaint();
        return result.wasOk();
    }
    void refreshPorts()
    {
        if (!owner)
            return;
        const auto nextIn = juce::MidiInput::getAvailableDevices(),
                   nextOut = juce::MidiOutput::getAvailableDevices();
        rebuilding = true;
        auto update = [](juce::ComboBox &box, const juce::Array<juce::MidiDeviceInfo> &devices,
                         const juce::String &wanted)
        {
            box.clear(juce::dontSendNotification);
            box.addItem("Off", 1);
            int selected = 1;
            for (int i = 0; i < devices.size(); ++i)
            {
                box.addItem(devices[i].name, i + 2);
                if (devices[i].identifier == wanted)
                    selected = i + 2;
            }
            if (wanted.isNotEmpty() && selected == 1)
            {
                selected = devices.size() + 2;
                box.addItem("Disconnected (waiting)", selected);
            }
            box.setSelectedId(selected, juce::dontSendNotification);
        };
        if (nextIn != inputs || input.getNumItems() == 0)
        {
            inputs = nextIn;
            update(input, inputs, owner->midiController.inputIdentifier());
        }
        if (nextOut != outputs || output.getNumItems() == 0)
        {
            outputs = nextOut;
            update(output, outputs, owner->midiController.outputIdentifier());
        }
        rebuilding = false;
    }
    void timerCallback() override
    {
        if (!owner)
            return;
        refreshPorts();
        refreshMixerTargets();
        live.setText(owner->midiController.lastMessage, juce::dontSendNotification);
        ports.setText("Control input / output | " + owner->midiController.status(),
                      juce::dontSendNotification);
        const bool learning = owner->midiController.learnIndex >= 0;
        if (wasLearning && !learning)
        {
            selectedRowsChanged(row);
            list.repaint();
        }
        wasLearning = learning;
    }

  public:
    explicit ControllerPage(Studio &studio) : owner(&studio)
    {
        addAndMakeVisible(projectLinks);
        projectLinks.setComponentID("settings.controller.projectLinks");
        projectLinks.setTooltip(
            "Project-owned fixed sampler MIDI CC links; edit, disable or remove with Undo/Redo.");
        projectLinks.onClick = [this]
        {
            if (owner)
                owner->samplerControllerLinksMenu(&projectLinks);
        };
        for (auto *component : std::initializer_list<juce::Component *>{
                 &input,           &output,  &action,     &mode,  &port,    &channel, &kind,   &layer,
                 &sendDestination, &mixer,   &list,       &title, &minimum, &maximum, &number, &pickup,
                 &banked,          &enabled, &nativeMode, &add,   &remove,  &apply,   &learn,  &cancel,
                 &preset,          &connect, &heading,    &ports, &live,    &hint,    &message})
            addAndMakeVisible(component);
        heading.setText("Controllers / native MIDI Learn", juce::dontSendNotification);
        hint.setText("Select an action, click MIDI Learn, then press or move a hardware control. Keys keep "
                     "playing on the MIDI tab's input. "
                     "Layer 0 is normal; assign a held button as Modifier for Layers 1-3. Relative modes "
                     "support endless encoders. "
                     "Range applies to parameters. Fixed Mixer targets retain stable IDs; banked targets "
                     "follow current "
                     "non-Master display positions. Master ignores the bank. Plugin "
                     "parameter automation remains pending.",
                     juce::dontSendNotification);
        hint.setJustificationType(juce::Justification::topLeft);
        list.setRowHeight(30);
        for (const auto &name : {"Button / press", "Absolute parameter", "Relative / two's complement",
                                 "Relative / offset 64", "Modifier / hold layer"})
            mode.addItem(name, mode.getNumItems() + 1);
        port.addItem("Performance input", 1);
        port.addItem("Control port", 2);
        channel.addItem("Any MIDI channel", 1);
        for (int i = 1; i <= 16; ++i)
            channel.addItem("MIDI channel " + juce::String(i), i + 1);
        for (const auto &name :
             {"CC", "Note", "Program change", "Pitch bend", "MIDI Start", "MIDI Stop", "MIDI Continue"})
            kind.addItem(name, kind.getNumItems() + 1);
        for (int i = 0; i < 4; ++i)
            layer.addItem(i ? "Layer " + juce::String(i) : "Layer 0 / normal", i + 1);
        populateMixerTargets(-1, false);
        populateSendTargets(-1);
        sendDestination.setVisible(false);
        sendDestination.setTooltip("Explicit stable send destination. The source-to-destination route must "
                                   "exist; deleted routes never retarget this mapping.");
        mixer.setTooltip(
            "Target for Mixer gain/pan/separation/mute/phase/swap. Fixed IDs never follow another "
            "insert after deletion. Banked positive slots follow current display order excluding Master.");
        banked.onClick = [this]
        {
            if (!rebuilding)
                populateMixerTargets(chosenMixerTarget().value_or(-1), banked.getToggleState());
        };
        title.setTextToShowWhenEmpty("Mapping name", muted);
        number.setTextToShowWhenEmpty("CC/note number", muted);
        minimum.setTextToShowWhenEmpty("Minimum", muted);
        maximum.setTextToShowWhenEmpty("Maximum", muted);
        for (auto *editor : {&minimum, &maximum})
            editor->setInputRestrictions(16, "-0123456789.eE");
        number.setInputRestrictions(3, "0123456789");
        add.onClick = [this]
        {
            if (!owner || getNumRows() >= 512)
                return;
            ControllerBinding binding;
            binding.action = "transport.play";
            binding.label = "New mapping";
            binding.enabled = false;
            owner->midiController.bindings.push_back(binding);
            owner->midiController.publish();
            list.updateContent();
            list.selectRow(getNumRows() - 1);
        };
        remove.onClick = [this]
        {
            if (!owner || row < 0 || row >= getNumRows())
                return;
            juce::String token;
            (void)owner->midiController.bindingProfileIdentity(token);
            if (const auto removed = owner->midiController.removeBinding(size_t(row), token); removed.failed())
            {
                message.setText(removed.getErrorMessage(), juce::dontSendNotification);
                return;
            }
            bindingProfileToken.clear();
            (void)owner->midiController.bindingProfileIdentity(bindingProfileToken);
            list.updateContent();
            if (getNumRows())
                list.selectRow(std::min(row, getNumRows() - 1));
            else
                row = -1;
        };
        apply.onClick = [this] { commit(); };
        learn.onClick = [this]
        {
            if (commit())
            {
                owner->midiController.armLearn(row);
                wasLearning = true;
                message.setText(
                    "Listening: move or press one control. The action will not run while learning.",
                    juce::dontSendNotification);
            }
        };
        cancel.onClick = [this]
        {
            if (owner)
                owner->midiController.cancelLearn();
            message.setText("Learning cancelled", juce::dontSendNotification);
        };
        mode.onChange = [this]
        {
            if (rebuilding || !owner || row < 0 || row >= getNumRows())
                return;
            populateActions(mode.getSelectedId() - 1, chosenAction());
        };
        action.onChange = [this]
        {
            if (rebuilding || mode.getSelectedId() <= 1)
                return;
            const auto id = chosenAction();
            sendDestination.setVisible(id == "mixer.send");
            resized();
            double low = 0, high = 1;
            if (id == "project.tempo" || id == "channel.sampler.sourceTempo")
            {
                low = 30;
                high = 300;
            }
            else if (id == "pattern.length")
            {
                low = 1;
                high = 64;
            }
            else if (id.endsWith(".pan"))
            {
                low = -1;
                high = 1;
            }
            else if (id.endsWith(".gain"))
                high = 1.5;
            else if (id == "mixer.send")
                high = 2;
            else if (id == "channel.sampler.root")
            {
                low = -1;
                high = 127;
            }
            else if (id == "channel.sampler.cents")
            {
                low = -1200;
                high = 1200;
            }
            else if (id.endsWith(".attack") || id.endsWith(".decay") || id.endsWith(".release"))
                high = 2;
            minimum.setText(juce::String(low), false);
            maximum.setText(juce::String(high), false);
        };
        auto portsChanged = [this]
        {
            if (rebuilding || !owner)
                return;
            const int a = input.getSelectedId() - 2, b = output.getSelectedId() - 2;
            const auto result = owner->setControllerPorts(
                a >= 0 && a < inputs.size() ? inputs[a].identifier : juce::String{},
                b >= 0 && b < outputs.size() ? outputs[b].identifier : juce::String{});
            message.setText(result.wasOk() ? "Control ports updated" : result.getErrorMessage(),
                            juce::dontSendNotification);
        };
        input.onChange = output.onChange = portsChanged;
        preset.onClick = [this]
        {
            auto safe = juce::Component::SafePointer<ControllerPage>(this);
            juce::AlertWindow::showAsync(
                juce::MessageBoxOptions()
                    .withIconType(juce::MessageBoxIconType::QuestionIcon)
                    .withTitle("Replace controller mappings?")
                    .withMessage("Load the Launchkey MK3 preset instead of current mappings?")
                    .withButton("Load preset")
                    .withButton("Cancel"),
                [safe](int answer)
                {
                    if (safe && answer == 1)
                        safe->loadPreset();
                });
        };
        connect.onClick = [this]
        {
            if (!owner)
                return;
            juce::String text;
            const auto connected = owner->connectLaunchkey(text);
            if (!connected.getErrorMessage().startsWith("controller.launchkeyNotFound"))
            {
                input.clear(juce::dontSendNotification);
                output.clear(juce::dontSendNotification);
                refreshPorts();
            }
            message.setText(text, juce::dontSendNotification);
        };
        setSize(720, 800);
        refreshPorts();
        if (getNumRows())
            list.selectRow(0);
        timerCallback();
        startTimerHz(8);
    }
    ~ControllerPage() override
    {
        if (owner)
            owner->midiController.cancelLearn();
    }
    void loadPreset()
    {
        if (!owner)
            return;
        owner->loadLaunchkeyPreset();
        list.updateContent();
        list.selectRow(0);
        message.setText("MK3 preset loaded; mappings remain customizable.", juce::dontSendNotification);
    }
    void paint(juce::Graphics &g) override
    {
        g.fillAll(panel);
        const int x = std::min(250, getWidth() / 3) + 22, form = getWidth() - x - 12;
        label(g, "Minimum", {x, 544, form / 2 - 4, 18}, muted, 11);
        label(g, "Maximum", {x + form / 2 + 4, 544, form / 2 - 4, 18}, muted, 11);
    }
    void resized() override
    {
        const int w = getWidth(), left = std::min(250, w / 3), x = left + 22, form = w - x - 12;
        heading.setBounds(12, 8, w - 24, 28);
        ports.setBounds(12, 42, w - 24, 28);
        input.setBounds(12, 76, (w - 31) / 2, 30);
        output.setBounds(19 + (w - 31) / 2, 76, (w - 31) / 2, 30);
        preset.setBounds(12, 116, 180, 30);
        connect.setBounds(200, 116, 160, 30);
        hint.setBounds(12, 156, w - 24, 70);
        projectLinks.setBounds(12, 230, 190, 28);
        live.setBounds(212, 230, w - 224, 28);
        list.setBounds(12, 270, left, getHeight() - 330);
        title.setBounds(x, 270, form, 30);
        action.setBounds(x, 310, form, 30);
        mode.setBounds(x, 350, form, 30);
        port.setBounds(x, 390, form / 2 - 4, 30);
        channel.setBounds(x + form / 2 + 4, 390, form / 2 - 4, 30);
        kind.setBounds(x, 430, form / 2 - 4, 30);
        number.setBounds(x + form / 2 + 4, 430, form / 2 - 4, 30);
        layer.setBounds(x, 470, form, 30);
        const bool send = sendDestination.isVisible();
        mixer.setBounds(x, 510, send ? form / 2 - 4 : form, 30);
        sendDestination.setBounds(x + form / 2 + 4, 510, form / 2 - 4, 30);
        minimum.setBounds(x, 566, form / 2 - 4, 30);
        maximum.setBounds(x + form / 2 + 4, 566, form / 2 - 4, 30);
        pickup.setBounds(x, 606, form / 2, 28);
        enabled.setBounds(x + form / 2, 606, form / 2, 28);
        banked.setBounds(x, 638, form / 2, 28);
        nativeMode.setBounds(x + form / 2, 638, form / 2, 28);
        apply.setBounds(x, 676, 90, 30);
        learn.setBounds(x + 98, 676, 110, 30);
        cancel.setBounds(x + 216, 676, 108, 30);
        add.setBounds(12, getHeight() - 50, 110, 30);
        remove.setBounds(130, getHeight() - 50, 100, 30);
        message.setBounds(x, 714, form, 58);
    }
};
} // namespace vibera
