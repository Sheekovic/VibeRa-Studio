// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "NativeEffects.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace vibera::effects
{
class NativeEffectsEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
    class ControlsLookAndFeel final : public juce::LookAndFeel_V4
    {
      public:
        void drawRotarySlider(juce::Graphics &, int, int, int, int, float, float, float,
                              juce::Slider &) override;
        void drawLinearSlider(juce::Graphics &, int, int, int, int, float, float, float,
                              juce::Slider::SliderStyle, juce::Slider &) override;
    } controlsLookAndFeel_;
    class StereoOutputMeter final : public juce::Component, public juce::SettableTooltipClient
    {
        std::array<float, 2> levels_{};

      public:
        void update(float left, float right);
        void paint(juce::Graphics &) override;
    } outputMeter_;
    struct Row
    {
        juce::Label label;
        juce::Slider slider;
        juce::Rectangle<int> card;
        juce::HostedAudioProcessorParameter *parameter = nullptr;
        juce::NormalisableRange<float> range;
        bool gesture = false;
    };
    NativeEffectInstance &instance_;
    // Response curve (Parametric EQ) or gain reduction meter (studio dynamics).
    juce::Rectangle<int> display_;
    bool curve_ = false, reduction_ = false;
    float shownReduction_ = 0;
    void paintDisplay(juce::Graphics &);
    juce::Label brand_, heading_, subtitle_, footer_;
    std::vector<std::unique_ptr<Row>> rows_;
    void timerCallback() override;

  public:
    explicit NativeEffectsEditor(NativeEffectInstance &);
    ~NativeEffectsEditor() override;
    void paint(juce::Graphics &) override;
    void resized() override;
};
} // namespace vibera::effects
