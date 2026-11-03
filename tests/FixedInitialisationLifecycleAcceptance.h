// SPDX-License-Identifier: AGPL-3.0-or-later
// ORIGINAL lifecycle fixture. UNBUILT/UNRUN. Include after require().
#pragma once
#include <tracktion_engine/tracktion_engine.h>
#include <tracktion_graph/tracktion_graph.h>
#include <tracktion_graph/tracktion_graph/tracktion_TestUtilities.h>
#include <tracktion_graph/tracktion_graph/tracktion_TestNodes.h>
#include <tracktion_engine/../3rd_party/crill/seqlock_object.h>
#include <tracktion_engine/playback/graph/tracktion_TracktionEngineNode.h>
#include <tracktion_engine/playback/graph/tracktion_PluginNode.h>
#include <tracktion_engine/playback/graph/tracktion_AuxSendNode.h>
#include <memory>
#include <stdexcept>

namespace vibera::test::fixed_initialisation
{
struct Counts
{
    unsigned initialises = 0, withoutStopping = 0, deinitialises = 0, resets = 0;
    bool throwInitialise = false, throwLatency = false;
};
// This is an original lifecycle TEST double using admitted private app metadata.
// It does not impersonate/qualify actual FixedSampler, native DSP or a provider.
class OriginalLifecycleProbe final : public tracktion::Plugin
{
    Counts &counts;

  public:
    OriginalLifecycleProbe(tracktion::PluginCreationInfo info, Counts &value) : Plugin(info), counts(value) {}
    ~OriginalLifecycleProbe() override
    {
        notifyListenersOfDeletion();
    }
    juce::String getName() const override
    {
        return "Original lifecycle fixture";
    }
    juce::String getPluginType() override
    {
        return "vibera_fixed_sampler";
    }
    juce::String getSelectableDescription() override
    {
        return getName();
    }
    BusLayout getBusses() const override
    {
        return BusLayout::singleStereoInOut();
    }
    void initialise(const tracktion::PluginInitialisationInfo &) override
    {
        ++counts.initialises;
        if (counts.throwInitialise)
            throw std::runtime_error("Original first-init failure");
    }
    void initialiseWithoutStopping(const tracktion::PluginInitialisationInfo &) override
    {
        ++counts.withoutStopping;
    }
    void deinitialise() override
    {
        ++counts.deinitialises;
    }
    void reset() override
    {
        ++counts.resets;
    }
    void applyToBuffer(const tracktion::PluginRenderContext &) override {}
    double getLatencySeconds() override
    {
        if (counts.throwLatency)
            throw std::runtime_error("Original post-init latency failure");
        return 0;
    }
};
class OriginalSendProbe final : public tracktion::AuxSendPlugin
{
    Counts &counts;

  public:
    OriginalSendProbe(tracktion::PluginCreationInfo info, Counts &value) : AuxSendPlugin(info), counts(value)
    {
    }
    ~OriginalSendProbe() override
    {
        notifyListenersOfDeletion();
    }
    void initialise(const tracktion::PluginInitialisationInfo &) override
    {
        ++counts.initialises;
        if (counts.throwInitialise)
            throw std::runtime_error("Original send first-init failure");
    }
    void initialiseWithoutStopping(const tracktion::PluginInitialisationInfo &) override
    {
        ++counts.withoutStopping;
    }
    void deinitialise() override
    {
        ++counts.deinitialises;
    }
    float getRealtimeSendGain() const noexcept override
    {
        return 1;
    }
};
template <class Operation> bool throws(Operation operation)
{
    try
    {
        operation();
    }
    catch (...)
    {
        return true;
    }
    return false;
}

// Driver supplies a genuine disposable Edit, valid private unattached states
// with distinct reserved IDs, and actual native ProcessState. This method NEVER
// processes nodes, sends OS input, initializes hardware or proves musical PCM.
inline void tests(tracktion::Edit &edit, const juce::ValueTree &privateSampler,
                  const juce::ValueTree &privateSend, tracktion::ProcessState &state)
{
    using Mode = tracktion::PluginCreationInfo::Mode;
    auto sampler = privateSampler.createCopy();
    sampler.setProperty(tracktion::IDs::type, "vibera_fixed_sampler", nullptr);
    Counts counts;
    auto *raw = new OriginalLifecycleProbe({edit, sampler, false, Mode::detachedGraph}, counts);
    tracktion::Plugin::Ptr owner = raw;
    auto make = [&](bool fixed)
    {
        return std::make_unique<tracktion::PluginNode>(std::make_unique<tracktion::graph::SilentNode>(2),
                                                       owner, 48000.0, 256, nullptr, state, false, true, -1,
                                                       fixed);
    };
    counts.throwInitialise = true;
    require(throws([&] { (void)make(true); }) && counts.initialises == 1 && counts.withoutStopping == 0 &&
                counts.deinitialises == 1 && owner->baseClassNeedsInitialising() && !owner->isInitialising(),
            "Failed first-private initialization releases native reference and initialization flag");
    require(throws([&] { (void)make(true); }) && counts.initialises == 2 && counts.withoutStopping == 0 &&
                counts.deinitialises == 2 && owner->baseClassNeedsInitialising(),
            "Retrying exact failed candidate runs real initialization again, never withoutStopping");
    counts.throwInitialise = false;
    counts.throwLatency = true;
    require(throws([&] { (void)make(true); }) && counts.initialises == 3 && counts.deinitialises == 3 &&
                owner->baseClassNeedsInitialising() && !owner->isInitialising(),
            "Post-first-init latency failure releases exactly one new private attachment");
    counts.throwLatency = false;
    auto first = make(true);
    require(counts.initialises == 4 && counts.withoutStopping == 0 && !owner->baseClassNeedsInitialising(),
            "Same private candidate can complete a genuine fresh initialization after failures");
    const auto resets = counts.resets;
    auto retained = make(true);
    require(counts.initialises == 4 && counts.withoutStopping == 0 && counts.resets == resets,
            "Positive-count fixed path performs no native reinitialization/reset");
    retained.reset();
    require(counts.deinitialises == 3, "Discarding second attachment preserves first initialized node");
    first.reset();
    require(counts.deinitialises == 4 && owner->baseClassNeedsInitialising(),
            "Final successful attachment tears down exactly once");
    auto ordinary = make(false);
    auto ordinarySecond = make(false);
    require(counts.initialises == 5 && counts.withoutStopping == 1,
            "Default generic node path retains original normal lifecycle behavior");
    ordinarySecond.reset();
    ordinary.reset();
    require(counts.deinitialises == 5 && owner->baseClassNeedsInitialising(),
            "Ordinary count lifetime still balances");
    auto incumbent = make(false);
    counts.throwLatency = true;
    require(throws([&] { (void)make(true); }) && counts.deinitialises == 5 &&
                !owner->baseClassNeedsInitialising(),
            "Throwing retained-node latency query preserves genuine incumbent attachment");
    counts.throwLatency = false;
    incumbent.reset();
    require(counts.deinitialises == 6 && owner->baseClassNeedsInitialising(),
            "Incumbent remains final teardown owner after failed retain");

    auto sendState = privateSend.createCopy();
    sendState.setProperty(tracktion::IDs::type, "vibera_sidechain_send", nullptr);
    Counts sendCounts;
    auto *send = new OriginalSendProbe({edit, sendState, false, Mode::detachedGraph}, sendCounts);
    tracktion::Plugin::Ptr sendOwner = send;
    auto makeSend = [&](bool fixed)
    {
        return std::make_unique<tracktion::AuxSendNode>(std::make_unique<tracktion::graph::SilentNode>(2), 0,
                                                        tracktion::SampleRateAndBlockSize{48000.0, 256},
                                                        *send, state.playHeadState, nullptr, true, fixed);
    };
    sendCounts.throwInitialise = true;
    require(throws([&] { (void)makeSend(true); }) && sendCounts.initialises == 1 &&
                sendCounts.deinitialises == 1 && sendOwner->baseClassNeedsInitialising() &&
                !sendOwner->isInitialising(),
            "AuxSend first-init failure balances its own native count");
    sendCounts.throwInitialise = false;
    auto sendFirst = makeSend(true);
    auto sendRetained = makeSend(true);
    require(sendCounts.initialises == 2 && sendCounts.withoutStopping == 0,
            "AuxSend same candidate truly retries then retains");
    sendRetained.reset();
    require(sendCounts.deinitialises == 1, "AuxSend retained discard preserves incumbent");
    sendFirst.reset();
    require(sendCounts.deinitialises == 2 && sendOwner->baseClassNeedsInitialising(),
            "AuxSend final private cleanup exactly once");
}
} // namespace vibera::test::fixed_initialisation
