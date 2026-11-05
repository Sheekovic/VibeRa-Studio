// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "LiveTimeline.h"
#include "GraphControlRuntime.h"
#include "PreparedBalance.h"

namespace vibera
{
// Retained channel stage, after sampler/native instrument and before routing.
// All settings arrive through the same immutable document as notes and Mixer
// controls. The disabled engine volume object remains for engine accessors only.
class ChannelBalance final : public tracktion::Plugin
{
    std::shared_ptr<LiveTimeline> timeline;
    struct Runtime
    {
        detail::GraphControlRuntimeState lifecycle;
        dsp::PreparedBalance balance;
        float gain = 1, pan = 0;
        double activeRate = 0;
        Id channel = 0;
        tracktion::PanLaw law = tracktion::PanLawLinear;
        Runtime() = default;
        Runtime(const Runtime &) = delete;
        Runtime &operator=(const Runtime &) = delete;
        Runtime(Runtime &&) = delete;
        Runtime &operator=(Runtime &&) = delete;
    };
    std::shared_ptr<Runtime> runtime = std::make_shared<Runtime>();
    detail::GraphControlRuntimeAttachment attachment;
    Id channel = 0;
    tracktion::PanLaw panLaw = tracktion::PanLawLinear;
    bool configured = false;
    void detachRuntime() noexcept
    {
        if (attachment.detach(runtime->lifecycle))
            runtime->activeRate = 0;
    }

  public:
    static constexpr const char *xmlTypeName = "vibera_channel_balance";
    explicit ChannelBalance(tracktion::PluginCreationInfo info) : Plugin(info) {}
    ~ChannelBalance() override
    {
        detachRuntime();
        notifyListenersOfDeletion();
    }
    // Native per-adapter CPU telemetry is unused; overall callback health is independent.
    bool shouldMeasureCpuUsage() const noexcept override
    {
        return false;
    }
    juce::String getName() const override
    {
        return "VibeRa Channel Balance";
    }
    juce::String getPluginType() override
    {
        return xmlTypeName;
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
        return false;
    }
    int getNumOutputChannelsGivenInputs(int channels) override
    {
        return channels;
    }
    BusLayout getBusses() const override
    {
        return BusLayout::singleStereoInOut();
    }
    // CONTROL only: private identified state, no native registry insertion.
    static tracktion::Plugin::Ptr createDetachedGraph(tracktion::Edit &edit, const juce::ValueTree &state)
    {
        if (state[tracktion::IDs::type].toString() != xmlTypeName)
            throw std::invalid_argument("Detached adapter type mismatch");
        return new ChannelBalance({edit, state, false, tracktion::PluginCreationInfo::Mode::detachedGraph});
    }
    void configure(std::shared_ptr<LiveTimeline> value, const Channel &owner, tracktion::PanLaw law)
    {
        attachment.requirePrivateConfiguration(runtime->lifecycle);
        timeline = std::move(value);
        channel = owner.id;
        runtime->gain = owner.audioClip && owner.muted ? 0.f : owner.gain;
        runtime->pan = owner.pan;
        panLaw = law;
        configured = true;
    }
    juce::Result shareCompatibleRuntimeFrom(const ChannelBalance &source)
    {
        if (!isDetachedGraph() || &source == this || &source.edit != &edit || itemID != source.itemID ||
            !configured || !timeline || !attachment.privateConfiguration(runtime->lifecycle))
            return juce::Result::fail(
                "channelBalance.runtime.invalidCandidate: Fresh configured same-ID detached shell required");
        if (!source.attachment.isAttachedControl() || !source.runtime->lifecycle.canRetain())
            return juce::Result::fail("channelBalance.runtime.inactiveSource: Initialized source required");
        if (channel != source.channel || panLaw != source.panLaw || channel != source.runtime->channel ||
            panLaw != source.runtime->law)
            return juce::Result::fail(
                "channelBalance.runtime.incompatibleSource: Channel and pan law must match");
        runtime = source.runtime;
        attachment.retain(runtime->lifecycle);
        return juce::Result::ok();
    }
    // CONTROL only. Keeps no PluginPtr/Edit alive past graph retirement.
    std::shared_ptr<const void> runtimeOwnerControl() const { return runtime; }
    std::uintptr_t runtimeIdentity() const noexcept
    {
        return reinterpret_cast<std::uintptr_t>(runtime.get());
    }
    unsigned runtimeAttachmentsControl() const noexcept
    {
        return runtime->lifecycle.shells;
    }
    std::uint64_t runtimeEpochControl() const noexcept
    {
        return runtime->lifecycle.epoch;
    }
    void initialiseWithoutStopping(const tracktion::PluginInitialisationInfo &) override {}
    void initialise(const tracktion::PluginInitialisationInfo &info) override
    {
        const auto mode = attachment.preflight(runtime->lifecycle, info.sampleRate, info.blockSizeSamples,
                                               runtime->channel == channel && runtime->law == panLaw);
        if (mode == detail::GraphControlRuntimeAttachment::Preparation::retained)
        {
            attachment.complete(runtime->lifecycle, info.sampleRate, info.blockSizeSamples, mode);
            return; // No live ramp/fallback read, reset or copy.
        }
        // A genuine device reprepare starts at the latest admitted controls.
        if (timeline)
            if (auto next = timeline->acquire(); next.document)
                if (const auto *owner = next.document->find(channel))
                {
                    runtime->gain = owner->gain;
                    runtime->pan = owner->pan;
                }
        const auto ready =
            runtime->balance.prepare(info.sampleRate, dsp::PreparedBalance::Mode::channelGainAndPan, panLaw,
                                     runtime->gain, runtime->pan);
        if (ready != dsp::PreparedBalance::Result::ok)
            throw std::logic_error(
                "channelBalance.runtime.invalidBalance: Valid balance preparation required");
        runtime->channel = channel;
        runtime->law = panLaw;
        runtime->activeRate = info.sampleRate;
        attachment.complete(runtime->lifecycle, info.sampleRate, info.blockSizeSamples, mode);
    }
    void deinitialise() override
    {
        detachRuntime();
    }
    void restorePluginStateFromValueTree(const juce::ValueTree &) override {}
    void applyToBuffer(const tracktion::PluginRenderContext &fc) override
    {
        if (!fc.destBuffer || !timeline || runtime->activeRate <= 0)
            return;
        auto next = timeline->acquire();
        if (next.document)
            if (const auto *owner = next.document->find(channel))
            {
                runtime->gain = owner->gain;
                runtime->pan = owner->pan;
                // Channel volume/pan automation (block-rate target, smoothed by the balance).
                if (fc.isPlaying || fc.isRendering)
                {
                    const auto &doc = *next.document;
                    const double seconds = fc.editTime.getStart().inSeconds() +
                                           .5 * fc.bufferNumSamples / std::max(1., runtime->activeRate);
                    const double beat = doc.tempoMap.beatAt(seconds);
                    if (const auto *curve = doc.automation.find({AutomationParameter::channelGain, std::int64_t(channel), 0}))
                        runtime->gain = curve->valueAt(beat);
                    if (const auto *curve = doc.automation.find({AutomationParameter::channelPan, std::int64_t(channel), 0}))
                        runtime->pan = curve->valueAt(beat);
                }
            }
        // No document pin survives a callback on an inactive clip track. A
        // bounded acquisition miss retains its last target, never drops PCM.
        runtime->balance.process(*fc.destBuffer, fc.bufferStartSample, fc.bufferNumSamples, runtime->gain,
                                 runtime->pan);
    }
};
} // namespace vibera
