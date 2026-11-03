// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Studio.h"
namespace vibera
{
class MidiPage final : public juce::Component, private juce::Timer
{
    juce::Component::SafePointer<Studio> owner;
    juce::ComboBox devices, channel;
    juce::Label title, deviceLabel, channelLabel, target, state, guidance;
    juce::TextButton rescan{"Refresh devices"}, panic{"All Notes Off"};
    juce::ToggleButton quantizeStart{"Quantize note starts to Snap"},
        quantizeEnd{"Quantize note ends to Snap"};
    juce::Array<juce::MidiDeviceInfo> available;
    void timerCallback() override
    {
        if (!owner)
            return;
        auto next = juce::MidiInput::getAvailableDevices();
        if (next != available || devices.getNumItems() == 0)
        {
            available = next;
            devices.clear(juce::dontSendNotification);
            devices.addItem("Off", 1);
            for (int i = 0; i < available.size(); ++i)
                devices.addItem(available[i].name, i + 2);
        }
        auto wanted = owner->audio.midiInput.identifier();
        int selected = 1;
        for (int i = 0; i < available.size(); ++i)
            if (available[i].identifier == wanted)
                selected = i + 2;
        if (wanted.isNotEmpty() && selected == 1)
        {
            if (devices.getNumItems() == available.size() + 1)
                devices.addItem("Disconnected input (waiting)", available.size() + 2);
            selected = available.size() + 2;
        }
        devices.setSelectedId(selected, juce::dontSendNotification);
        channel.setSelectedId(owner->audio.midiInput.channel() + 1, juce::dontSendNotification);
        auto *c = owner->controller.project.channel(owner->selectedChannel);
        target.setText("Playing selected Channel: " + (c ? c->name : "none"), juce::dontSendNotification);
        state.setText(owner->audio.midiInput.status(), juce::dontSendNotification);
    }

  public:
    explicit MidiPage(Studio &studio) : owner(&studio)
    {
        for (auto *c : std::initializer_list<juce::Component *>{
                 &devices, &channel, &title, &deviceLabel, &channelLabel, &target, &state, &guidance, &rescan,
                 &panic, &quantizeStart, &quantizeEnd})
            addAndMakeVisible(c);
        title.setText("MIDI input", juce::dontSendNotification);
        deviceLabel.setText("Input device", juce::dontSendNotification);
        channelLabel.setText("Receive channel", juce::dontSendNotification);
        channel.addItem("All channels (Omni)", 1);
        for (int i = 1; i <= 16; ++i)
            channel.addItem("MIDI channel " + juce::String(i), i + 1);
        guidance.setText(
            "Select an input and Channel, then play. Arm Rec, Play, perform, Stop keeps one undoable take; "
            "Esc cancels. Notes preview live. Pattern overdubs loops; Song places or extends its Pattern. "
            "Use Count-in and Transport settings for a lead-in.\n"
            "Recording stores notes/velocity; controller/pedal/bend recording is pending. Built-ins monitor "
            "velocity/sustain. VST3 also receives bend, CC, program and pressure; response depends on the "
            "plugin.",
            juce::dontSendNotification);
        guidance.setJustificationType(juce::Justification::topLeft);
        quantizeStart.setToggleState(studio.quantizeRecordingStart, juce::dontSendNotification);
        quantizeEnd.setToggleState(studio.quantizeRecordingEnd, juce::dontSendNotification);
        quantizeStart.onClick = [this]
        {
            if (owner)
            {
                owner->quantizeRecordingStart = quantizeStart.getToggleState();
                owner->audio.midiInput.setRecordingQuantize(owner->quantizeRecordingStart,
                                                            owner->quantizeRecordingEnd);
            }
        };
        quantizeEnd.onClick = [this]
        {
            if (owner)
            {
                owner->quantizeRecordingEnd = quantizeEnd.getToggleState();
                owner->audio.midiInput.setRecordingQuantize(owner->quantizeRecordingStart,
                                                            owner->quantizeRecordingEnd);
            }
        };
        devices.onChange = [this]
        {
            if (owner)
            {
                auto i = devices.getSelectedId() - 2;
                if (i >= available.size())
                    return;
                owner->audio.midiInput.setDevice(i < 0 ? juce::String{} : available[i].identifier);
                timerCallback();
            }
        };
        channel.onChange = [this]
        {
            if (owner)
                owner->audio.midiInput.setChannel(channel.getSelectedId() - 1);
        };
        rescan.onClick = [this] { timerCallback(); };
        panic.onClick = [this]
        {
            if (owner)
                owner->audio.midiInput.panic();
        };
        timerCallback();
        startTimer(1000);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced(20);
        title.setBounds(r.removeFromTop(32));
        r.removeFromTop(12);
        deviceLabel.setBounds(r.removeFromTop(24));
        devices.setBounds(r.removeFromTop(32));
        r.removeFromTop(16);
        channelLabel.setBounds(r.removeFromTop(24));
        channel.setBounds(r.removeFromTop(32));
        r.removeFromTop(16);
        target.setBounds(r.removeFromTop(28));
        state.setBounds(r.removeFromTop(28));
        r.removeFromTop(14);
        auto actions = r.removeFromTop(32);
        rescan.setBounds(actions.removeFromLeft(160));
        actions.removeFromLeft(12);
        panic.setBounds(actions.removeFromLeft(160));
        r.removeFromTop(20);
        quantizeStart.setBounds(r.removeFromTop(28));
        quantizeEnd.setBounds(r.removeFromTop(28));
        r.removeFromTop(8);
        guidance.setBounds(r);
    }
};
} // namespace vibera
