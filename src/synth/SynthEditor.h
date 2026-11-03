// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "SynthPresets.h"
#include "SynthBrowser.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <functional>
#include <memory>

namespace vibera::synth
{
class SynthEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
  public:
    // All callbacks run on the message thread. The processor's keyboard listener
    // must publish events through its bounded queue; the DSP must not lock this
    // MidiKeyboardState or perform filesystem work.
    struct Callbacks
    {
        std::function<juce::String()> currentPresetId;
        std::function<juce::Result(const juce::String &)> selectFactoryPreset;
        std::function<juce::Result(const juce::File &)> saveUserPreset;
        std::function<juce::Result(const juce::File &)> loadUserPreset;
        std::function<SynthBrowserState()> browserState;
        std::function<juce::Result(const SynthBrowserState &)> setBrowserState;
        std::function<int()> activeVoiceCount;
        std::function<bool(int)> externalNoteHeld; // keys held by host MIDI input
        std::function<float(int)> peak;
        std::function<juce::String()> loadStatus;
    };
    SynthEditor(juce::AudioProcessor &, juce::MidiKeyboardState &, Callbacks);
    ~SynthEditor() override;
    void paint(juce::Graphics &) override;
    void resized() override;
    void focusLost(FocusChangeType) override;
    void visibilityChanged() override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    void timerCallback() override;
};
} // namespace vibera::synth
