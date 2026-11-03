// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "LiveTimeline.h"
#include "MixerSendAutomation.h"
#include "GraphControlRuntime.h"
#include <tracktion_engine/tracktion_engine.h>
#include <tracktion_graph/tracktion_graph.h>
#include <tracktion_graph/tracktion_graph/tracktion_TestUtilities.h>
#include <tracktion_graph/tracktion_graph/tracktion_TestNodes.h>
#include <stdexcept>
namespace vibera
{
inline float preparedSendGain(const std::shared_ptr<LiveTimeline> &timeline, int source, int target,
                              std::atomic<float> &fallback) noexcept
{
    if (!timeline)
        return fallback.load(std::memory_order_relaxed);
    auto lease = timeline->acquire();
    if (!lease.document)
        return fallback.load(std::memory_order_relaxed);
    const auto &routes = lease.document->mixerSends;
    const auto key = std::pair{source, target};
    auto it = std::lower_bound(routes.begin(), routes.end(), key,
                               [](const auto &r, auto k) { return std::pair{r.source, r.target} < k; });
    const float gain = it != routes.end() && it->source == source && it->target == target ? it->gain : 0.f;
    fallback.store(gain, std::memory_order_relaxed);
    return gain;
}
// Tracktion owns the prepared graph and delay compensation. The virtual gain hook
// avoids mutable engine parameters/ValueTrees in VibeRa's send-level callback.
class MixerSendPlugin final : public tracktion::AuxSendPlugin
{
    std::shared_ptr<LiveTimeline> timeline;
    int source = 0, target = 0, preparedBus = 0;
    MixerSendAutomation automation;
    detail::GraphControlRuntimeState lifecycle;
    detail::GraphControlRuntimeAttachment attachment;
    bool configured = false;
    mutable std::atomic<float> fallback{0};
    // CONTROL owns this handle; native GainNodes pin their own immutable handle
    // before DSP starts. The smoother is written only by the selected graph.
    mutable std::shared_ptr<tracktion::graph::SendGainState> sendGain;
    std::uint64_t retainedGainEpoch = 0;
    tracktion::graph::SendGainState::EdgeIdentity edgeIdentity() const noexcept
    {
        return {std::uint64_t(source), std::uint64_t(target), preparedBus};
    }

  public:
    static constexpr const char *xmlTypeName = "vibera_mixer_send";
    static const char *getPluginName()
    {
        return "VibeRa Mixer Send";
    }
    explicit MixerSendPlugin(tracktion::PluginCreationInfo info) : AuxSendPlugin(info) {}
    ~MixerSendPlugin() override
    {
        attachment.detach(lifecycle);
    } // Base sends deletion notification.
    static tracktion::Plugin::Ptr createDetachedGraph(tracktion::Edit &edit, const juce::ValueTree &state)
    {
        if (state[tracktion::IDs::type].toString() != xmlTypeName)
            throw std::invalid_argument("Detached adapter type mismatch");
        return new MixerSendPlugin({edit, state, false, tracktion::PluginCreationInfo::Mode::detachedGraph});
    }
    // Closed same-Edit, same native identity and edge admission. Neither this
    // check nor the retaining factory reads the selected graph's DSP smoother.
    juce::Result admitStableBindingFrom(const MixerSendPlugin &previous) const
    {
        if (!isDetachedGraph() || &previous == this || &previous.edit != &edit || itemID != previous.itemID ||
            !configured || !timeline || !attachment.privateConfiguration(lifecycle))
            return juce::Result::fail(
                "mixerSend.binding.invalidCandidate: Fresh configured same-ID detached shell required");
        if (!previous.attachment.isAttachedControl() || !previous.lifecycle.canRetain())
            return juce::Result::fail("mixerSend.binding.inactiveSource: Initialized source required");
        if (source != previous.source || target != previous.target || preparedBus != previous.preparedBus)
            return juce::Result::fail(
                "mixerSend.binding.changedEdge: Source, destination and bus must match");
        if (!previous.sendGain ||
            previous.sendGain->canRetainControl(edgeIdentity(), previous.sendGain->epochControl()) !=
                tracktion::graph::SendGainState::Admission::ready)
            return juce::Result::fail(
                "mixerSend.binding.inactiveGain: Initialized native send gain required");
        return juce::Result::ok();
    }
    juce::Result shareCompatibleRuntimeFrom(const MixerSendPlugin &previous)
    {
        const auto admitted = admitStableBindingFrom(previous);
        if (admitted.failed())
            return admitted;
        sendGain = previous.sendGain;
        retainedGainEpoch = sendGain->epochControl();
        return juce::Result::ok();
    }
    std::uintptr_t runtimeIdentity() const noexcept
    {
        return reinterpret_cast<std::uintptr_t>(sendGain.get());
    }
    std::uint64_t runtimeEpochControl() const noexcept
    {
        return sendGain ? sendGain->epochControl() : 0;
    }
    unsigned runtimeAttachmentsControl() const noexcept
    {
        return sendGain ? sendGain->attachmentsControl() : 0;
    }
    // Native graph construction only, before initialization; never DSP. A
    // normal quiesced graph may need a fresh bank after its prior graph dies.
    // A retained candidate must instead reject its captured retired epoch.
    std::shared_ptr<tracktion::graph::SendGainState> prepareSendGainStateForGraph() const override
    {
        using Bank = tracktion::graph::SendGainState;
        if (!configured || !timeline || source <= 0 || target <= 0 || preparedBus < 0)
            throw std::logic_error("mixerSend.gain.invalidBinding: Identified prepared edge required");
        if (retainedGainEpoch != 0)
        {
            if (!sendGain ||
                sendGain->canRetainControl(edgeIdentity(), retainedGainEpoch) != Bank::Admission::ready)
                throw std::logic_error("mixerSend.gain.staleSource: Retained native bank retired or changed");
        }
        else if (!sendGain || (sendGain->attachmentsControl() == 0 && sendGain->epochControl() != 1))
            sendGain = std::make_shared<Bank>(edgeIdentity(), .005);
        if (!sendGain->matchesEdge(edgeIdentity()))
            throw std::logic_error("mixerSend.gain.changedEdge: Native send bank identity mismatch");
        return sendGain;
    }
    void initialise(const tracktion::PluginInitialisationInfo &info) override
    {
        const auto mode = attachment.preflight(lifecycle, info.sampleRate, info.blockSizeSamples, true);
        automation.prepare(timeline, source, target, info.sampleRate);
        AuxSendPlugin::initialise(info); // Keeps normal native parameter/owner initialization.
        attachment.complete(lifecycle, info.sampleRate, info.blockSizeSamples, mode);
    }
    void initialiseWithoutStopping(const tracktion::PluginInitialisationInfo &) override {}
    void deinitialise() override
    {
        AuxSendPlugin::deinitialise();
        attachment.detach(lifecycle);
    }
    juce::String getPluginType() override
    {
        return xmlTypeName;
    }
    void configure(std::shared_ptr<LiveTimeline> t, int s, int d, int bus, float gain)
    {
        attachment.requirePrivateConfiguration(lifecycle);
        timeline = std::move(t);
        source = s;
        target = d;
        fallback.store(gain);
        preparedBus = bus;
        busNumber = bus;
        state.setProperty("viberaSource", s, nullptr);
        state.setProperty("viberaTarget", d, nullptr);
        configured = true;
    }
    int destination() const
    {
        return target;
    }
    float getRealtimeSendGain() const noexcept override
    {
        return preparedSendGain(timeline, source, target, fallback);
    }
    bool hasRealtimeSendGainBlock() const noexcept override
    {
        return true;
    }
    tracktion::graph::SendGainBlockResult
    getRealtimeSendGainBlock(const tracktion::graph::SendGainBlockContext &clock,
                             std::span<float> output) const noexcept override
    {
        return automation.fill(clock, output);
    }
    bool sendAutomationHealthy() const noexcept
    {
        return automation.healthy();
    }
};
// Prepared detector-only graph tap, before the direct Master gate. This bus is
// independent of audible sends; gain/mute already apply at the source's fader.
// Application connection validation/persistence remain outside this DSP adapter.
class MixerSidechainSend final : public tracktion::AuxSendPlugin
{
  public:
    static constexpr const char *xmlTypeName = "vibera_sidechain_send";
    static const char *getPluginName()
    {
        return "VibeRa Detector Tap";
    }
    explicit MixerSidechainSend(tracktion::PluginCreationInfo info) : AuxSendPlugin(info) {}
    // Internal preparation only: caller owns a private, already identified tree.
    static tracktion::Plugin::Ptr createDetachedGraph(tracktion::Edit &edit, const juce::ValueTree &state)
    {
        if (state[tracktion::IDs::type].toString() != xmlTypeName)
            throw std::invalid_argument("Detached adapter type mismatch");
        return new MixerSidechainSend(
            {edit, state, false, tracktion::PluginCreationInfo::Mode::detachedGraph});
    }

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
        return 1.f;
    }
};
// Every insert retains a direct root so sends can feed destinations even when its
// Master route is disabled. This final gate affects only that direct path; sends
// are tapped after effects/fader/pan, before this gate.
class MixerMasterGate final : public tracktion::Plugin
{
    std::shared_ptr<LiveTimeline> timeline;
    MixerSendAutomation automation;
    int source = 0;
    struct Runtime
    {
        detail::GraphControlRuntimeState lifecycle;
        std::atomic<float> fallback{1};
        juce::SmoothedValue<float> level;
        int source = 0;
        Runtime() = default;
        Runtime(const Runtime &) = delete;
        Runtime &operator=(const Runtime &) = delete;
        Runtime(Runtime &&) = delete;
        Runtime &operator=(Runtime &&) = delete;
    };
    std::shared_ptr<Runtime> runtime = std::make_shared<Runtime>();
    detail::GraphControlRuntimeAttachment attachment;
    bool configured = false;

  public:
    static constexpr const char *xmlTypeName = "vibera_master_route";
    static const char *getPluginName()
    {
        return "VibeRa Master Route";
    }
    explicit MixerMasterGate(tracktion::PluginCreationInfo info) : Plugin(info) {}
    ~MixerMasterGate() override
    {
        attachment.detach(runtime->lifecycle);
        notifyListenersOfDeletion();
    }
    // Native per-adapter CPU telemetry is unused; overall callback health is independent.
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
    juce::String getSelectableDescription() override
    {
        return getName();
    }
    int getNumOutputChannelsGivenInputs(int n) override
    {
        return n;
    }
    BusLayout getBusses() const override
    {
        return BusLayout::singleStereoInOut();
    }
    bool takesAudioInput() override
    {
        return true;
    }
    bool producesAudioWhenNoAudioInput() override
    {
        return false;
    }
    static tracktion::Plugin::Ptr createDetachedGraph(tracktion::Edit &edit, const juce::ValueTree &state)
    {
        if (state[tracktion::IDs::type].toString() != xmlTypeName)
            throw std::invalid_argument("Detached adapter type mismatch");
        return new MixerMasterGate({edit, state, false, tracktion::PluginCreationInfo::Mode::detachedGraph});
    }
    juce::Result shareCompatibleRuntimeFrom(const MixerMasterGate &previous)
    {
        if (!isDetachedGraph() || &previous == this || &previous.edit != &edit || itemID != previous.itemID ||
            !configured || !timeline || !attachment.privateConfiguration(runtime->lifecycle))
            return juce::Result::fail(
                "masterGate.runtime.invalidCandidate: Fresh configured same-ID detached shell required");
        if (!previous.attachment.isAttachedControl() || !previous.runtime->lifecycle.canRetain())
            return juce::Result::fail("masterGate.runtime.inactiveSource: Initialized source required");
        if (source != previous.source || source != previous.runtime->source)
            return juce::Result::fail("masterGate.runtime.incompatibleSource: Mixer source must match");
        runtime = previous.runtime;
        attachment.retain(runtime->lifecycle);
        return juce::Result::ok();
    }
    const void *getPreparedAudioOutputStageIdentity() const noexcept override
    {
        // This exact nonmoving owner is shared ONLY by compatible same-ID/Edit
        // shells. Both ordinary initial and private successor graphs opt in.
        // Native Nodes retain their PluginPtr, which pins Runtime through old
        // graph retirement. No pointer-derived hash or fanout ordinal is used.
        return configured && source > 0 ? runtime.get() : nullptr;
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
    const std::shared_ptr<LiveTimeline>& timelineReference() const noexcept { return timeline; }
    void configure(std::shared_ptr<LiveTimeline> t, int s, float gain)
    {
        attachment.requirePrivateConfiguration(runtime->lifecycle);
        timeline = std::move(t);
        source = s;
        runtime->fallback.store(gain);
        runtime->level.setCurrentAndTargetValue(gain);
        configured = true;
    }
    void initialise(const tracktion::PluginInitialisationInfo &info) override
    {
        const auto mode = attachment.preflight(runtime->lifecycle, info.sampleRate, info.blockSizeSamples,
                                               runtime->source == source);
        automation.prepare(timeline, source, 0, info.sampleRate);
        if (mode == detail::GraphControlRuntimeAttachment::Preparation::fresh)
        {
            runtime->level.reset(info.sampleRate, .005);
            runtime->source = source;
        }
        attachment.complete(runtime->lifecycle, info.sampleRate, info.blockSizeSamples, mode);
    }
    void initialiseWithoutStopping(const tracktion::PluginInitialisationInfo &) override {}
    void deinitialise() override
    {
        attachment.detach(runtime->lifecycle);
    }
    bool sendAutomationHealthy() const noexcept
    {
        return automation.healthy();
    }
    void restorePluginStateFromValueTree(const juce::ValueTree &) override {}
    void applyToBuffer(const tracktion::PluginRenderContext &fc) override
    {
        if (!fc.destBuffer)
            return;
        if (fc.bufferNumSamples <= 0)
            return;
        const auto block = automation.read(fc.editTime.getStart().inSeconds(),
                                           fc.editTime.getLength().inSeconds() / fc.bufferNumSamples,
                                           fc.bufferNumSamples, fc.isPlaying || fc.isRendering);
        if (!block.valid)
        {
            fc.destBuffer->clear(fc.bufferStartSample, fc.bufferNumSamples);
            return; // Explicit unhealthy readback; never label a fault as manual automation.
        }
        runtime->fallback.store(block.manual, std::memory_order_relaxed);
        runtime->level.setTargetValue(block.manual);
        for (int frame = 0; frame < fc.bufferNumSamples; ++frame)
        {
            // Advance manual smoothing even while a Song curve owns output.
            const float manual = runtime->level.getNextValue();
            const float gain = block.curve ? block.curve->valueAt(block.beat + frame * block.step) : manual;
            for (int ch = 0; ch < fc.destBuffer->getNumChannels(); ++ch)
                fc.destBuffer->getWritePointer(ch, fc.bufferStartSample)[frame] *= gain;
        }
    }
};
} // namespace vibera
