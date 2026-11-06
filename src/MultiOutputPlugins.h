#pragma once
#include "PluginHost.h"
#include <tracktion_engine/tracktion_engine.h>
#include <memory>

namespace vibera
{
// Multi-output instruments. Each extra stereo output bus of a plugin instrument plays through
// an internal output track: a silent dependency send on the instrument's Channel track feeds an
// aux return on the output track, so the graph always renders the instrument first; the tap
// then copies that bus's samples from the instrument host into the output track, which routes
// to the chosen Mixer insert. Plugin delay compensation follows the aux send/return path.

// Carries no audio (exact zero gain); it only orders the output track after the instrument.
class MultiOutputDependencySend final : public tracktion::AuxSendPlugin
{
  public:
    static constexpr const char *xmlTypeName = "vibera_multiout_order";
    static const char *getPluginName()
    {
        return "VibeRa Multi-output Order";
    }
    explicit MultiOutputDependencySend(tracktion::PluginCreationInfo info) : AuxSendPlugin(info) {}
    juce::String getPluginType() override
    {
        return xmlTypeName;
    }
    void configure(int preparedBus)
    {
        busNumber = preparedBus;
    }
    float getRealtimeSendGain() const noexcept override
    {
        return 0.f;
    }
};

// Replaces its buffer with one extra output bus of the instrument host (stereo).
class MultiOutputTap final : public tracktion::Plugin
{
    std::shared_ptr<PluginHost> host;
    int bus = 0;

  public:
    static constexpr const char *xmlTypeName = "vibera_multiout_tap";
    static const char *getPluginName()
    {
        return "VibeRa Multi-output Tap";
    }
    explicit MultiOutputTap(tracktion::PluginCreationInfo info) : Plugin(info) {}
    ~MultiOutputTap() override
    {
        notifyListenersOfDeletion();
    }
    // CONTROL only, before the graph is built.
    void configure(std::shared_ptr<PluginHost> source, int extraBus)
    {
        host = std::move(source);
        bus = extraBus;
    }
    bool shouldMeasureCpuUsage() const noexcept override
    {
        return false;
    }
    juce::String getName() const override
    {
        return getPluginName();
    }
    juce::String getPluginType() override
    {
        return xmlTypeName;
    }
    juce::String getShortName(int) override
    {
        return "MultiOut";
    }
    juce::String getSelectableDescription() override
    {
        return getName();
    }
    bool isSynth() override
    {
        return false;
    }
    bool takesAudioInput() override
    {
        return true;
    }
    bool producesAudioWhenNoAudioInput() override
    {
        return true;
    }
    int getNumOutputChannelsGivenInputs(int) override
    {
        return 2;
    }
    BusLayout getBusses() const override
    {
        return BusLayout::singleStereoInOut();
    }
    void restorePluginStateFromValueTree(const juce::ValueTree &) override {}
    void initialise(const tracktion::PluginInitialisationInfo &) override {}
    void deinitialise() override {}
    void applyToBuffer(const tracktion::PluginRenderContext &fc) override
    {
        if (!fc.destBuffer)
            return;
        const int start = fc.bufferStartSample, count = fc.bufferNumSamples;
        for (int c = 0; c < std::min(2, fc.destBuffer->getNumChannels()); ++c)
        {
            const float *source = host ? host->extraOutput(bus, c) : nullptr;
            if (source && start >= 0 && start + count <= ipc::frames)
                fc.destBuffer->copyFrom(c, start, source + start, count);
            else
                fc.destBuffer->clear(c, start, count);
        }
    }
};

// Output tracks a project needs: one per extra output bus of every plugin instrument.
inline int multiOutputTrackCount(const Project &project)
{
    int count = 0;
    for (const auto &channel : project.channels)
        if (channel.instrument && !channel.audioClip)
            count += int(channel.instrument->extraOutputMixers.size());
    return count;
}
} // namespace vibera
