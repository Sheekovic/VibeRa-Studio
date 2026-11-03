// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <tracktion_graph/tracktion_graph.h>
#include <atomic>
#include <memory>

namespace vibera
{
// Explicit prepared application/native forward handoff, not generic route admission.
// One registered output stage only. The producing input pins its exact stage
// owner, has real newly introduced latency D, and executes once. This shell
// replaces only its first D emitted frames with the old native queue. It does
// not run an incumbent graph, copy mutable DSP state, or retain arbitrary tails.
class ForwardStageDrainNode final : public tracktion::graph::Node,
                                    public tracktion::graph::ForwardOutputDrainNode
{
  public:
    // The graph is the only writer. CONTROL retains this compact receipt, not
    // a borrowed Node/queue pointer whose lifetime could end on native cleanup.
    class Progress final
    {
        friend class ForwardStageDrainNode;
        std::atomic<std::uint64_t> bits{0};
        static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
        void publish(tracktion::graph::ForwardOutputDrain::Observation value) noexcept
        {
            bits.store((std::uint64_t(value.state) << 32) | value.remaining, std::memory_order_release);
        }

      public:
        tracktion::graph::ForwardOutputDrain::Observation observation() const noexcept
        {
            const auto value = bits.load(std::memory_order_acquire);
            return {tracktion::graph::ForwardOutputDrain::State(value >> 32), std::uint32_t(value)};
        }
    };

  private:
    std::unique_ptr<Node> input;
    const void *const expectedStageOwner;
    // CONTROL converts the existing unique factory result; the successor's
    // capability may then pin this finished proof beyond old graph retirement.
    std::shared_ptr<tracktion::graph::ForwardOutputDrain> drain;
    const std::shared_ptr<Progress> progress;

  public:
    ForwardStageDrainNode(std::unique_ptr<Node> producingInput, const void *owner,
                          std::shared_ptr<Progress> receipt = {})
        : input(std::move(producingInput)), expectedStageOwner(owner), progress(std::move(receipt))
    {
        if (!input || !owner)
            throw std::invalid_argument("A graph-pinned producing input and exact stage owner are required");
        setOptimisations({tracktion::graph::ClearBuffers::no, tracktion::graph::AllocateAudioBuffer::yes});
    }
    tracktion::graph::NodeProperties getNodeProperties() override
    {
        auto value = input->getNodeProperties();
        // Preserve the ACTUAL producer origin, shape and MIDI declaration.
        // A wrapper must not forge a new output-stage identity to evade PDC.
        if (value.nodeID != 0)
            tracktion::hash_combine(value.nodeID, std::size_t(0x564652445241494eULL));
        return value;
    }
    std::vector<Node*> getDirectInputNodes() override { return {input.get()}; }
    bool isReadyToProcess() override { return input->hasProcessed(); }
    void prepareToPlay(const tracktion::graph::PlaybackInitialisationInfo& info) override
    {
        if (drain)
            throw std::invalid_argument("A forward bridge shell may only be prepared once");
        drain = tracktion::graph::LatencyNode::prepareForwardOutputDrain(*this, info, expectedStageOwner);
    }
    tracktion::graph::ForwardOutputDrain* preparedForwardOutputDrain() const noexcept override
    {
        return drain.get();
    }
    void process(ProcessContext& pc) override
    {
        const auto source = input->getProcessedOutput();
        // Node shape/capacity was prepared on CONTROL. These bounded copies
        // run only once per produced frame; ownership is unchanged in DSP.
        for (choc::buffer::ChannelCount channel = 0; channel < pc.buffers.audio.getNumChannels(); ++channel)
            for (choc::buffer::FrameCount frame = 0; frame < pc.buffers.audio.getNumFrames(); ++frame)
                pc.buffers.audio.getSample(channel, frame) = source.audio.getSample(channel, frame);
        if (!source.midi.isEmpty() || source.midi.isAllNotesOff)
        {
            drain->refuseUnexpectedMidi();
            if (progress) progress->publish(drain->observation());
            return; // Explicit failed receipt, never a native-MIDI success claim.
        }
        drain->drain(pc.buffers.audio, pc.referenceSampleRange);
        if (progress)
            progress->publish(drain->observation());
    }
};
} // namespace vibera
