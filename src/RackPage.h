// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Studio.h"
namespace vibera
{
class RackPage final : public juce::Component, private juce::Timer
{
    juce::Component::SafePointer<Studio> owner;
    juce::ToggleButton restore{"Restore previous mute state after Channel solo"};
    juce::ToggleButton memory{"Mute removed steps instead of deleting them"};
    juce::Label title, explanation, result;
    void timerCallback() override
    {
        if (owner)
        {
            restore.setToggleState(owner->customization.restoreMuteAfterSolo, juce::dontSendNotification);
            memory.setToggleState(owner->customization.muteRemovedSteps, juce::dontSendNotification);
            memory.setEnabled(!owner->customization.running && !owner->controller.hasGesture());
        }
    }

  public:
    explicit RackPage(Studio &studio) : owner(&studio)
    {
        setLookAndFeel(&studio.getLookAndFeel());
        for (auto *component :
             std::initializer_list<juce::Component *>{&restore, &memory, &title, &explanation, &result})
            addAndMakeVisible(component);
        title.setText("Channel Rack workflow", juce::dontSendNotification);
        restore.setComponentID("rack-solo-restore");
        memory.setComponentID("rack-mute-removed-steps");
        memory.setTooltip("Keep removed step notes and their properties for reactivation. Saved workflow "
                          "preference; not musical Undo.");
        explanation.setText("Ctrl-click a mute light to solo/unsolo. Right-click it for mute and Solo.\n"
                            "Channel mute affects score playback; live MIDI stays playable.\n"
                            "When restore is off, leaving solo unmutes every Channel.\n"
                            "Mixer mute still silences the complete routed signal.\n"
                            "Mute removed steps preserves their notes and properties.\n"
                            "Reactivating a removed step restores its original properties.",
                            juce::dontSendNotification);
        explanation.setJustificationType(juce::Justification::topLeft);
        restore.onClick = [this]
        {
            if (!owner)
                return;
            const auto saved = owner->setSoloRestorePreference(restore.getToggleState());
            result.setText(saved.wasOk() ? "Saved" : saved.getErrorMessage(), juce::dontSendNotification);
            timerCallback();
        };
        memory.onClick = [this]
        {
            if (!owner)
                return;
            const auto saved = owner->setStepMemoryPreference(memory.getToggleState());
            result.setText(saved.wasOk() ? "Saved" : saved.getErrorMessage(), juce::dontSendNotification);
            timerCallback();
        };
        timerCallback();
        startTimerHz(2);
        setSize(700, 440);
    }
    ~RackPage() override
    {
        setLookAndFeel(nullptr);
    }
    void paint(juce::Graphics &g) override
    {
        g.fillAll(panel);
    }
    void resized() override
    {
        auto area = getLocalBounds().reduced(18);
        title.setBounds(area.removeFromTop(38));
        restore.setBounds(area.removeFromTop(42));
        memory.setBounds(area.removeFromTop(42));
        area.removeFromTop(10);
        explanation.setBounds(area.removeFromTop(165));
        result.setBounds(area.removeFromTop(45));
    }
};
} // namespace vibera
