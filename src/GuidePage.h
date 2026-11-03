// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Studio.h"
namespace vibera
{
class GuidePage final : public juce::Component, private juce::Timer
{
    juce::Component::SafePointer<Studio> owner;
    juce::ToggleButton enabled{"Metronome (Ctrl+M)"};
    juce::ComboBox bars, sound, denominator;
    juce::Slider gain, numerator;
    juce::Label title, barsLabel, soundLabel, gainLabel, meterLabel, info;
    void timerCallback() override
    {
        if (!owner)
            return;
        enabled.setToggleState(owner->audio.metronome(), juce::dontSendNotification);
        bars.setSelectedId(owner->audio.countInBars() + 1, juce::dontSendNotification);
        sound.setSelectedId(owner->audio.guideSound() + 1, juce::dontSendNotification);
        if (!gain.isMouseButtonDown())
            gain.setValue(owner->audio.guideGain(), juce::dontSendNotification);
        if (!numerator.isMouseButtonDown())
            numerator.setValue(owner->controller.project.numerator, juce::dontSendNotification);
        denominator.setSelectedId(owner->controller.project.denominator, juce::dontSendNotification);
    }
    void changeMeter()
    {
        if (!owner)
            return;
        CommandArgs a;
        a.numerator = int(numerator.getValue());
        a.denominator = denominator.getSelectedId();
        owner->command("project.meter", a);
    }

  public:
    explicit GuidePage(Studio &studio) : owner(&studio)
    {
        for (auto *c : std::initializer_list<juce::Component *>{&enabled, &bars, &sound, &gain, &numerator,
                                                                &denominator, &title, &barsLabel, &soundLabel,
                                                                &gainLabel, &meterLabel, &info})
            addAndMakeVisible(c);
        title.setText("Transport and recording guide", juce::dontSendNotification);
        barsLabel.setText("Recording count-in", juce::dontSendNotification);
        soundLabel.setText("Original click sound", juce::dontSendNotification);
        gainLabel.setText("Click level", juce::dontSendNotification);
        meterLabel.setText("Project time signature", juce::dontSendNotification);
        bars.addItem("Off", 1);
        bars.addItem("1 bar", 2);
        bars.addItem("2 bars", 3);
        sound.addItem("Click", 1);
        sound.addItem("Wood", 2);
        sound.addItem("Beep", 3);
        for (int value : {2, 4, 8, 16})
            denominator.addItem(juce::String(value), value);
        gain.setRange(0, 1, .01);
        gain.setSliderStyle(juce::Slider::LinearHorizontal);
        gain.setTextBoxStyle(juce::Slider::TextBoxRight, false, 72, 26);
        numerator.setRange(1, 32, 1);
        numerator.setSliderStyle(juce::Slider::IncDecButtons);
        numerator.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 56, 26);
        info.setText("Arm Rec, enable Count-in, then Play. The lead-in follows the audio clock and project "
                     "meter. MIDI is monitored during the countdown; notes played before the recording "
                     "boundary are not stored.\n\nOriginal clicks route through the Master effects and "
                     "fader. They are excluded from offline WAV. The countdown clicks even when "
                     "Metronome is off. Preferences are local; time signature is saved in the project and "
                     "undoable. Existing patterns keep their musical duration; new patterns use one bar.",
                     juce::dontSendNotification);
        info.setJustificationType(juce::Justification::topLeft);
        timerCallback();
        enabled.onClick = [this]
        {
            if (owner)
            {
                owner->audio.setMetronome(enabled.getToggleState());
                owner->refresh();
            }
        };
        bars.onChange = [this]
        {
            if (owner)
            {
                owner->audio.setCountInBars(bars.getSelectedId() - 1);
                owner->refresh();
            }
        };
        sound.onChange = [this]
        {
            if (owner)
                owner->audio.setGuideSound(sound.getSelectedId() - 1);
        };
        gain.onValueChange = [this]
        {
            if (owner)
                owner->audio.setGuideGain(float(gain.getValue()));
        };
        numerator.onValueChange = [this] { changeMeter(); };
        denominator.onChange = [this] { changeMeter(); };
        startTimer(200);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced(20);
        title.setBounds(r.removeFromTop(32));
        r.removeFromTop(12);
        enabled.setBounds(r.removeFromTop(30));
        r.removeFromTop(10);
        auto row = r.removeFromTop(34);
        barsLabel.setBounds(row.removeFromLeft(220));
        bars.setBounds(row);
        r.removeFromTop(10);
        row = r.removeFromTop(34);
        soundLabel.setBounds(row.removeFromLeft(220));
        sound.setBounds(row);
        r.removeFromTop(10);
        row = r.removeFromTop(34);
        gainLabel.setBounds(row.removeFromLeft(220));
        gain.setBounds(row);
        r.removeFromTop(10);
        row = r.removeFromTop(34);
        meterLabel.setBounds(row.removeFromLeft(220));
        numerator.setBounds(row.removeFromLeft(100));
        row.removeFromLeft(12);
        denominator.setBounds(row.removeFromLeft(110));
        r.removeFromTop(20);
        info.setBounds(r);
    }
};
} // namespace vibera
