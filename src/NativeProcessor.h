// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace vibera
{
// Owned processors share the host's typed parameter DTOs, without pretending
// to be a VST3 component. CONTROL owns state and preparation; DSP owns events.
class NativeProcessor
{
  public:
    using ParameterInfo = juce::AudioPluginExtensions::VST3Client::ParameterInfo;
    using ParameterEvent = juce::AudioPluginExtensions::VST3Client::ParameterEvent;
    virtual ~NativeProcessor() = default;
    virtual void beginParameterPatch() noexcept {}
    virtual void endParameterPatch() noexcept {}
    virtual bool parameterInfo(juce::uint32, ParameterInfo &) const = 0;
    virtual bool parameterValue(juce::uint32, float &) const = 0;
    virtual juce::AudioProcessorParameter *parameterForId(juce::uint32) const noexcept = 0;
    virtual bool setParameterEventsForNextBlock(juce::Span<const ParameterEvent>, int) noexcept = 0;
    virtual bool flushParameterChangesForState() noexcept = 0;
    virtual bool restoreState(const void *, int) = 0;
    virtual bool acceptsStateEnvelope(const juce::XmlElement &) const = 0;
    virtual float peak(int) const noexcept = 0;
};
} // namespace vibera
