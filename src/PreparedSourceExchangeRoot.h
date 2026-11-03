// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <tracktion_engine/tracktion_engine.h>
#include "PreparedMixerAudioPort.h"
#include "InitialSourceEpochFence.h"
#include <atomic>
#include <limits>
#include <memory>
#include <stdexcept>

namespace vibera::experimental
{
// Installed from the FIRST graph. CONTROL pins this owner but never inspects
// its non-atomic lineage. One Player/ProcessState is the sole DSP writer; there
// is no shared_ptr assignment, reader retry, clock read or queue peek in DSP.
class SourceExchangeContinuity final
{
  public:
    enum class Fault : std::uint32_t { none, invalidReference, nativeProofFailed, sourceEpochFailed };
    Fault observation() const noexcept { return Fault(fault.load(std::memory_order_acquire)); }
    bool hasSuccessfulBaseline() const noexcept { return baselineReady.load(std::memory_order_acquire); }
    std::uint64_t completedGeneration() const noexcept { return completed.load(std::memory_order_acquire); }
  private:
    friend class PreparedSourceExchangeRoot;
    const tracktion::graph::Node* lastRoot = nullptr;
    std::uint64_t lastGeneration = 0;
    std::int64_t lastEnd = 0;
    std::atomic<std::uint32_t> fault{std::uint32_t(Fault::none)};
    std::atomic<bool> baselineReady{false};
    std::atomic<std::uint64_t> completed{0};
    bool firstReceiptPublished = false; // Sole DSP writer, never CONTROL-read.
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free && std::atomic<bool>::is_always_lock_free &&
                  std::atomic<std::uint64_t>::is_always_lock_free);
};

// One ACTUAL final factory root, after native remapping, device assembly and
// PlayHeadPositionNode. No per-device wrapper can fulfil this contract.
// Candidate preparation is private. Cancel cannot write the shared DSP lineage.
class PreparedSourceExchangeRoot final : public tracktion::graph::Node,
                                         public tracktion::graph::ZeroStageExchangeRoot
{
  public:
    struct Request
    {
        const void* addDelayOwner = nullptr;
        const void* removeDelayOwner = nullptr;
        bool empty() const noexcept { return !addDelayOwner && !removeDelayOwner; }
    };
    PreparedSourceExchangeRoot(std::unique_ptr<Node> source,
                               std::shared_ptr<SourceExchangeContinuity> shared,
                               std::uint64_t nextGeneration, Request change,
                               std::shared_ptr<NativeMidiFaultPublisher> publisher = {},
                               std::shared_ptr<initial_source_epoch::Health> sourceHealth = {})
        : input(std::move(source)), continuity(std::move(shared)), generation(nextGeneration), request(change),
          nativeMidiFault(std::move(publisher)),sourceEpochHealth(std::move(sourceHealth))
    {
        if (!input || !continuity || !generation ||
            (!request.empty() && (!request.addDelayOwner || !request.removeDelayOwner ||
                                 request.addDelayOwner == request.removeDelayOwner)))
            throw std::invalid_argument("sourceExchange.root: Complete distinct request/owner required");
        // Own a preallocated output. Failed adopted proof clears THIS output;
        // no write-through to an old graph, alias or physical device buffer.
        setOptimisations({tracktion::graph::ClearBuffers::yes,
                          tracktion::graph::AllocateAudioBuffer::yes,
                          tracktion::graph::ShareOutputBuffer::no});
    }
    tracktion::graph::NodeProperties getNodeProperties() override
    {
        auto result = input->getNodeProperties();
        result.nodeID ^= std::size_t(0x534f555243455852ULL);
        return result;
    }
    std::vector<Node*> getDirectInputNodes() override { return {input.get()}; }
    bool isReadyToProcess() override { return input->hasProcessed(); }
    std::uint64_t zeroStageExchangeGeneration() const noexcept override { return generation; }
    const void* zeroStageExchangeContinuityIdentity() const noexcept override { return continuity.get(); }
    tracktion::graph::ZeroStageExchange* preparedZeroStageExchange() const noexcept override
    { return exchange.get(); }

  private:
    const std::unique_ptr<Node> input;
    const std::shared_ptr<SourceExchangeContinuity> continuity;
    const std::uint64_t generation;
    const Request request;
    const std::shared_ptr<NativeMidiFaultPublisher> nativeMidiFault;
    const std::shared_ptr<initial_source_epoch::Health> sourceEpochHealth;
    std::unique_ptr<tracktion::graph::ZeroStageExchange> exchange; // CONTROL mint; never reassigned by DSP.
    int maxBlock = 0;
    bool blockAdmitted = false; // This graph's sole DSP writer.
    // CONTROL-only immutable graph/configuration failure receipt. Never FIFO contents.
    static juce::String describeInventory(tracktion::graph::NodeGraph& graph)
    {
        using namespace tracktion::graph;
        juce::String text;
        const auto root = graph.rootNode->getNodeProperties();
        auto* owner = dynamic_cast<ZeroStageExchangeRoot*>(graph.rootNode.get());
        text += " root=" + juce::String::toHexString(juce::pointer_sized_int(graph.rootNode.get())) +
            " generation=" + juce::String(owner ? owner->zeroStageExchangeGeneration() : 0) +
            " audio/midi/ch/lat=" + juce::String(int(root.hasAudio)) + "/" +
            juce::String(int(root.hasMidi)) + "/" + juce::String(root.numberOfChannels) + "/" +
            juce::String(root.latencyNumSamples) + "\n";
        for (auto* node : graph.orderedNodes)
        {
            const auto props = node->getNodeProperties();
            text += " topology=" + juce::String::toHexString(juce::pointer_sized_int(node)) +
                " type=" + juce::String(typeid(*node).name()) + " id=" +
                juce::String(juce::uint64(props.nodeID)) + " audio/midi/ch/lat=" +
                juce::String(int(props.hasAudio)) + "/" + juce::String(int(props.hasMidi)) + "/" +
                juce::String(props.numberOfChannels) + "/" + juce::String(props.latencyNumSamples) + " inputs=";
            for (auto* dependency : node->getDirectInputNodes())
                text += juce::String::toHexString(juce::pointer_sized_int(dependency)) + ",";
            text += "\n";
            if (props.audioCompensationStage)
            {
                text += " stage=" + juce::String::toHexString(juce::pointer_sized_int(props.audioCompensationStage)) +
                    " node=" + juce::String::toHexString(juce::pointer_sized_int(node)) +
                    " origin=" + juce::String::toHexString(juce::pointer_sized_int(props.audioCompensationStageOrigin)) +
                    " id=" + juce::String(juce::uint64(props.nodeID)) +
                    " ch/midi/lat=" + juce::String(props.numberOfChannels) + "/" +
                    juce::String(int(props.hasMidi)) + "/" + juce::String(props.latencyNumSamples);
                if (auto* delay = dynamic_cast<LatencyNode*>(node))
                    text += " queue=" + juce::String::toHexString(juce::pointer_sized_int(delay->structuralZeroHistoryRuntimeToken())) +
                        " certified=" + juce::String(int(delay->hasStructuralZeroHistory()));
                text += "\n";
            }
            if (auto* source = dynamic_cast<PreparedSharedSourceOutput*>(node))
                if (auto identity = source->preparedSharedSourceIdentity())
                {
                    text += " source=" + juce::String::toHexString(juce::pointer_sized_int(node)) +
                        " runtime=" + juce::String::toHexString(juce::pointer_sized_int(identity->runtime)) +
                        " native=" + juce::String(juce::uint64(identity->nativeOwner)) +
                        " epoch=" + juce::String(identity->epoch) + " boundaries=";
                    for (auto* boundary : graph.orderedNodes)
                        if (boundary->getNodeProperties().audioCompensationStageOrigin == boundary)
                            text += juce::String(juce::uint64(boundary->getNodeProperties().nodeID)) + ":" +
                                juce::String(int(source->provesPreparedSharedSourceBoundary(*boundary))) + ",";
                    text += "\n";
                }
        }
        return text;
    }
    void prepareToPlay(const tracktion::graph::PlaybackInitialisationInfo& info) override
    {
        if (info.nodeGraph.rootNode.get() != this || info.enableNodeMemorySharing ||
            getNodeProperties().hasMidi || getNodeProperties().numberOfChannels != 2)
            throw std::invalid_argument("sourceExchange.finalRoot: Actual stereo audio-only unshared root required");
        maxBlock = info.blockSize;
        if (!request.empty())
        {
            if (!info.nodeGraphToReplace)
                throw std::invalid_argument("sourceExchange.baseline: Existing baseline proof required");
            try
            {
                exchange = tracktion::graph::LatencyNode::prepareZeroStageExchange(
                    *this, info, request.addDelayOwner, request.removeDelayOwner);
            }
            catch (const tracktion::graph::LatencyNode::OutputStageError& error)
            {
                throw std::invalid_argument((juce::String(error.what()) + "\nBEFORE\n" +
                    describeInventory(*info.nodeGraphToReplace) + "AFTER\n" +
                    describeInventory(info.nodeGraph)).toStdString());
            }
            if (!exchange)
                throw std::invalid_argument("sourceExchange.inventory: Native whole-inventory proof absent");
        }
    }
    void prefetchBlock(juce::Range<std::int64_t> range) override
    {
        blockAdmitted = false;
        if (continuity->observation() != SourceExchangeContinuity::Fault::none) return;
        if (range.getStart() < 0 || range.getEnd() <= range.getStart() ||
            range.getLength() > maxBlock)
        {
            continuity->fault.store(std::uint32_t(SourceExchangeContinuity::Fault::invalidReference),
                                    std::memory_order_release);
            return;
        }
        if (exchange && exchange->begin(*this, continuity.get(), continuity->lastRoot,
                        continuity->lastGeneration, continuity->lastEnd, range) !=
                            tracktion::graph::ZeroStageExchange::State::active)
        {
            continuity->fault.store(std::uint32_t(SourceExchangeContinuity::Fault::nativeProofFailed),
                                    std::memory_order_release);
            return;
        }
        blockAdmitted = true;
    }
    void process(ProcessContext& pc) override
    {
        // Player prefetches all Nodes before processing any child. Refusal is
        // observable BEFORE children, but native dispatch still processes them
        // once. This is failed output, never rollback or successful playback.
        // Closed native IO rejects external MIDI device branches separately.
        if (!blockAdmitted) return; // Ordinary Node clear produces defined silence.
        if(sourceEpochHealth&&!sourceEpochHealth->healthy())
        {
            continuity->fault.store(std::uint32_t(SourceExchangeContinuity::Fault::sourceEpochFailed),std::memory_order_release);
            return; // Real post-child source failure: no final PCM copy/successful root-generation receipt.
        }
        // Real terminals/ingress can fail DURING this callback. Observe their
        // preowned latch AFTER children, before audio or successful generation.
        if(nativeMidiFault && nativeMidiFault->observation()!=NativeMidiFaultPublisher::Fault::none)
        {
            continuity->fault.store(std::uint32_t(SourceExchangeContinuity::Fault::nativeProofFailed),
                                    std::memory_order_release);
            return;
        }
        const auto source = input->getProcessedOutput();
        if (!source.midi.isEmpty() || source.midi.isAllNotesOff ||
            source.audio.getNumChannels() != pc.buffers.audio.getNumChannels() ||
            source.audio.getNumFrames() != pc.numSamples)
        {
            continuity->fault.store(std::uint32_t(SourceExchangeContinuity::Fault::nativeProofFailed),
                                    std::memory_order_release);
            return;
        }
        for (choc::buffer::ChannelCount c = 0; c < source.audio.getNumChannels(); ++c)
            for (choc::buffer::FrameCount n = 0; n < pc.numSamples; ++n)
                pc.buffers.audio.getSample(c,n) = source.audio.getSample(c,n);
        const bool changedRoot = continuity->lastRoot != this;
        continuity->lastRoot = this;
        continuity->lastGeneration = generation;
        continuity->lastEnd = pc.referenceSampleRange.getEnd();
        if (changedRoot) continuity->completed.store(generation, std::memory_order_release);
        if (!continuity->firstReceiptPublished)
        {
            continuity->firstReceiptPublished = true;
            continuity->baselineReady.store(true, std::memory_order_release);
        }
    }
};
} // namespace vibera::experimental
