// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "NativeEffectsRegistry.h"
#include "../NativeProcessor.h"
#include "dsp/EffectsEngine.h"
#include <array>
#include <atomic>
#include <memory>
#include <span>

namespace vibera::effects
{
// Built into Studio and its worker. Each instance has one immutable effect
// identity; the VST3 ParameterInfo/Event types below are shared host DTOs only.
class NativeEffectInstance final : public juce::AudioPluginInstance, public NativeProcessor
{
  public:
    using ParameterInfo = juce::AudioPluginExtensions::VST3Client::ParameterInfo;
    using ParameterEvent = juce::AudioPluginExtensions::VST3Client::ParameterEvent;
    explicit NativeEffectInstance(EffectId);
    ~NativeEffectInstance() override;
    EffectId effectId() const noexcept
    {
        return effect_;
    }
    bool parameterInfo(juce::uint32, ParameterInfo &) const;
    bool parameterValue(juce::uint32, float &) const;
    juce::AudioProcessorParameter *parameterForId(juce::uint32) const noexcept;
    bool setParameterEventsForNextBlock(juce::Span<const ParameterEvent>, int frames) noexcept;
    // A named adapter avoids ambiguity for the host's empty-span {} calls.
    bool setParameterEvents(std::span<const ParameterEvent> events, int frames) noexcept
    {
        return setParameterEventsForNextBlock({events.data(), events.size()}, frames);
    }
    bool flushParameterChangesForState() noexcept;
    bool restoreState(const void *, int);
    bool acceptsStateEnvelope(const juce::XmlElement &xml) const override
    {
        return xml.hasTagName("VIBERA_NATIVE_EFFECT") && xml.getStringAttribute("schemaVersion") == "1" &&
               xml.getStringAttribute("effect") == effectSpec(effect_).slug;
    }
    bool lastRestoreAccepted() const noexcept
    {
        return restoreAccepted_.load(std::memory_order_relaxed);
    }
    float peak(int channel) const noexcept;
    // Gain reduction of the dynamics processors (dB, <= 0), for the editor meter.
    float gainReductionDb() const noexcept { return reduction_.load(std::memory_order_relaxed); }

    void fillInPluginDescription(juce::PluginDescription &) const override;
    const juce::String getName() const override;
    void prepareToPlay(double, int) override;
    void releaseResources() override;
    void reset() override;
    bool isBusesLayoutSupported(const BusesLayout &) const override;
    void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;
    bool acceptsMidi() const override
    {
        return false;
    }
    bool producesMidi() const override
    {
        return false;
    }
    double getTailLengthSeconds() const override;
    bool hasEditor() const override
    {
        return true;
    }
    juce::AudioProcessorEditor *createEditor() override;
    int getNumPrograms() override
    {
        return 1;
    }
    int getCurrentProgram() override
    {
        return 0;
    }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override
    {
        return "Default";
    }
    void changeProgramName(int, const juce::String &) override {}
    void getStateInformation(juce::MemoryBlock &) override;
    void setStateInformation(const void *, int) override;

  private:
    class NativeParameter;
    NativeParameter *findParameter(juce::uint32) const noexcept;
    Values nativeValues() const noexcept;
    const EffectId effect_;
    const EffectSpec &spec_;
    std::array<NativeParameter *, maximumParameters> parameters_{};
    Engine engine_;
    std::array<ParameterEvent, juce::AudioPluginExtensions::VST3Client::maximumParameterEvents> events_{};
    std::size_t eventCount_ = 0;
    int eventFrames_ = 0;
    bool prepared_ = false;
    std::atomic<bool> restoreAccepted_{true};
    // Negative sentinel means no actual native output block since preparation.
    // Existing DSP peak stores replace it; zero then means genuine silence.
    std::atomic<float> peakLeft_{-1}, peakRight_{-1};
    std::atomic<float> reduction_{0};
};

std::unique_ptr<juce::AudioPluginInstance> createNativeEffect(const juce::PluginDescription &);
} // namespace vibera::effects
