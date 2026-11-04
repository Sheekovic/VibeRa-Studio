// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "RemoteEffect.h"
#include <cmath>
#include <tracktion_graph/tracktion_graph.h>
#include <tracktion_engine/../3rd_party/crill/seqlock_object.h>
#include <tracktion_engine/playback/graph/tracktion_TracktionEngineNode.h>
#include <tracktion_engine/playback/graph/tracktion_PluginNode.h>
#include <set>

namespace vibera
{
// Internal closed-scope experiment, never a project-serialized generic PDC plan.
struct CompensationPhasePairPlan
{
    std::shared_ptr<PluginHost> source, wet;
    Id sourceId = 0, wetId = 0;
    int quantum = 0;
    // CONTROL-acquired owner. Null preserves the original legacy1Q contract.
    // A nonnull plan admits only the separately validated production3Q scope.
    PreparedPluginTransport::Ptr prepared;
};
class CompensationPhasePairClock
{
    const CompensationPhasePairPlan plan;
    tracktion::PluginNode *const sourceNode, *const wetNode;
    juce::Range<std::int64_t> reference;
    std::optional<PluginConsumedLatency> sourceBefore, wetBefore;
    std::uint64_t sourceSubmitted = 0, wetSubmitted = 0;
    int sourceCursor = -1, wetCursor = -1;

    static bool advanced(const std::optional<PluginConsumedLatency> &before,
                         const std::optional<PluginConsumedLatency> &after) noexcept
    {
        return after && (before ? before->sequence != UINT64_MAX && after->sequence == before->sequence + 1
                                : after->sequence == 0);
    }

  public:
    CompensationPhasePairClock(CompensationPhasePairPlan value, tracktion::PluginNode *source,
                               tracktion::PluginNode *wet)
        : plan(std::move(value)), sourceNode(source), wetNode(wet)
    {
        if (plan.prepared)
        {
            const auto &budget = plan.prepared->budget();
            juce::AudioDeviceManager::PhysicalCallbackSnapshot physical;
            const bool known = plan.prepared->read(physical);
            const auto owner = reinterpret_cast<std::uintptr_t>(plan.prepared.get());
            if (!plan.source || !plan.wet || plan.source == plan.wet || !plan.source->alive() ||
                !plan.wet->alive() || plan.source->offline() || plan.wet->offline() || plan.quantum != 256 ||
                budget.quantum != plan.quantum || budget.packets != 3 || budget.transportSamples != 768 ||
                budget.configuredFrames != 256 || budget.physicalFrames != 256 ||
                budget.sourceFrames != 282 || budget.maximumSourceScalePpm != 1100000 ||
                !std::isfinite(plan.prepared->sampleRate()) || plan.prepared->sampleRate() < 8000 ||
                plan.prepared->sampleRate() > 192000 || !known || !physical.ownerAlive || !physical.active ||
                physical.writerConflict || physical.countersExhausted ||
                physical.deviceEpoch != budget.deviceEpoch ||
                physical.configuredFrames != budget.configuredFrames ||
                physical.maximumOuterFrames > budget.physicalFrames ||
                plan.source->processingQuantum() != plan.quantum ||
                plan.wet->processingQuantum() != plan.quantum ||
                plan.source->transportLatencySamples() != budget.transportSamples ||
                plan.wet->transportLatencySamples() != budget.transportSamples ||
                plan.source->transportPacketCount() != budget.packets ||
                plan.wet->transportPacketCount() != budget.packets ||
                plan.source->pcmIdentity().preparedTransport != owner ||
                plan.wet->pcmIdentity().preparedTransport != owner ||
                !plan.source->transportOutcome().succeeded() || !plan.wet->transportOutcome().succeeded() ||
                plan.source->declaredLatency().nativeSamples != 0 ||
                plan.source->busLayout().auxiliaryChannels != 0 ||
                plan.wet->busLayout().auxiliaryChannels != 0 || plan.wet->latencySamples() < 768)
                throw std::invalid_argument("Phase-pair needs an exact healthy prepared3Q stereo plan");
            // PluginHost::start requires the exact plan rate/quantum before startup.
            // AudioEngine additionally validates current rate and both descriptors.
            return;
        }
        if (!plan.source || !plan.wet || plan.quantum <= 0 ||
            plan.source->transportLatencySamples() != plan.quantum ||
            plan.wet->transportLatencySamples() != plan.quantum)
            throw std::invalid_argument("Phase-pair clock supports one-quantum transport only");
    }
    void begin(juce::Range<std::int64_t> range) noexcept
    {
        reference = range;
        sourceBefore = plan.source->consumedLatency();
        wetBefore = plan.wet->consumedLatency();
        sourceSubmitted = plan.source->statistics().submitted;
        wetSubmitted = plan.wet->statistics().submitted;
        sourceCursor = plan.source->packetFrame();
        wetCursor = plan.wet->packetFrame();
    }
    tracktion::graph::LatencyNode::RuntimeAudioDelay resolve(juce::Range<std::int64_t> range,
                                                             std::uint32_t frames) const noexcept
    {
        using Read = tracktion::graph::LatencyNode::RuntimeAudioDelay;
        using Status = tracktion::graph::LatencyNode::RuntimeAudioDelayStatus;
        const auto source = plan.source->consumedLatency(), wet = plan.wet->consumedLatency();
        const auto sourceStats = plan.source->statistics(), wetStats = plan.wet->statistics();
        if (!sourceNode->hasProcessed() || !wetNode->hasProcessed() ||
            sourceNode->getReferenceSampleRange() != range || wetNode->getReferenceSampleRange() != range ||
            sourceNode->getNumSamples() != int(frames) || wetNode->getNumSamples() != int(frames) ||
            range != reference || frames != std::uint32_t(plan.quantum) ||
            range.getLength() != plan.quantum || sourceCursor != 0 || wetCursor != 0 ||
            plan.source->packetFrame() != 0 || plan.wet->packetFrame() != 0 ||
            sourceSubmitted == UINT64_MAX || wetSubmitted == UINT64_MAX ||
            sourceStats.submitted != sourceSubmitted + 1 || wetStats.submitted != wetSubmitted + 1 ||
            sourceStats.missed || sourceStats.dropped || wetStats.missed || wetStats.dropped)
            return Read{Status::invalidReceipt, 0};
        if (plan.prepared)
        {
            const auto &budget = plan.prepared->budget(); // Immutable graph-pinned configuration.
            if (sourceSubmitted != wetSubmitted)
                return Read{Status::invalidReceipt, 0};
            // Exactly K initial quanta may have no consumed packet. A later gap
            // never becomes successful dry fallback or another cold epoch.
            if (!sourceBefore && !wetBefore && !source && !wet)
                return sourceSubmitted < std::uint64_t(budget.packets)
                           ? Read{Status::ready, std::uint32_t(budget.transportSamples)}
                           : Read{Status::invalidReceipt, 0};
            if (sourceSubmitted < std::uint64_t(budget.packets) || !advanced(sourceBefore, source) ||
                !advanced(wetBefore, wet) || source->sequence != wet->sequence ||
                source->sequence != sourceSubmitted - std::uint64_t(budget.packets) ||
                source->stamp.nativeSamples != 0 || !source->stamp.valid(plan.quantum) ||
                !wet->stamp.valid(plan.quantum) ||
                wet->stamp.nativeSamples >= PreparedPluginTransport::dryCapacity - budget.transportSamples)
                return Read{Status::invalidReceipt, 0};
            return Read{Status::ready, std::uint32_t(budget.transportSamples + wet->stamp.nativeSamples)};
        }
        // First pipeline quantum contains no consumed PCM at either host. It is
        // an explicit zero-output startup, not a tag inferred from a declaration.
        if (!sourceBefore && !wetBefore && !source && !wet && sourceSubmitted == 0 && wetSubmitted == 0)
            return Read{Status::ready, std::uint32_t(plan.quantum)};
        if (!advanced(sourceBefore, source) || !advanced(wetBefore, wet) ||
            source->sequence != wet->sequence || source->stamp.nativeSamples != 0 ||
            !source->stamp.valid(plan.quantum) || !wet->stamp.valid(plan.quantum))
            return Read{Status::invalidReceipt, 0};
        return Read{Status::ready, std::uint32_t(plan.quantum + wet->stamp.nativeSamples)};
    }
    static void beginThunk(void *context, juce::Range<std::int64_t> range) noexcept
    {
        static_cast<CompensationPhasePairClock *>(context)->begin(range);
    }
    static tracktion::graph::LatencyNode::RuntimeAudioDelay
    resolveThunk(void *context, juce::Range<std::int64_t> range, std::uint32_t frames) noexcept
    {
        return static_cast<const CompensationPhasePairClock *>(context)->resolve(range, frames);
    }
};

// Preparation only. Classify AUDIO ancestry, ignoring a tap's scheduling-only
// dependency. This never introspects, copies or modifies live DSP state.
inline unsigned phasePairAncestors(tracktion::graph::Node *node, const CompensationPhasePairPlan &plan,
                                   std::set<tracktion::graph::Node *> &visiting)
{
    if (!visiting.insert(node).second)
        throw std::invalid_argument("Consumed-phase audio ancestry cycle");
    unsigned mask = 0;
    if (auto *plugin = dynamic_cast<tracktion::PluginNode *>(node))
        if (auto *remote = dynamic_cast<RemoteEffect *>(&plugin->getPlugin()))
        {
            const auto id = remote->phasePairPluginId();
            auto *host = remote->phasePairHost();
            if (id == plan.sourceId && host == plan.source.get())
                mask |= 1;
            else if (id == plan.wetId && host == plan.wet.get())
                mask |= 2;
            else
                throw std::invalid_argument("Consumed-phase graph contains an unknown native worker");
        }
    const auto inputs =
        dynamic_cast<tracktion::graph::LatencyNode *>(node)
            ? std::vector<tracktion::graph::Node *>{static_cast<tracktion::graph::LatencyNode *>(node)
                                                        ->uncompensatedInput()}
            : node->getDirectInputNodes();
    for (auto *input : inputs)
        mask |= phasePairAncestors(input, plan, visiting);
    visiting.erase(node);
    return mask;
}
inline unsigned phasePairAncestors(tracktion::graph::Node *node, const CompensationPhasePairPlan &plan)
{
    std::set<tracktion::graph::Node *> visiting;
    return phasePairAncestors(node, plan, visiting);
}
} // namespace vibera
