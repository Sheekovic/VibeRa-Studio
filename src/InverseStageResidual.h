// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <tracktion_graph/tracktion_graph.h>
#include <memory>

namespace vibera
{
// ORIGINAL internal finite-delay test shell. No app graph constructs it.
// dryInput already contains the genuine, newly prepared native D-frame FIFO.
// residualInput pins/advances only the exact old finite-provider Runtime.
class InverseStageResidualNode final : public tracktion::graph::Node,
                                       public tracktion::graph::InverseOutputResidualNode
{
    std::unique_ptr<Node> dryInput, residualInput;
    const void* const owner;
    std::unique_ptr<tracktion::graph::InverseOutputResidual> capability;
public:
    InverseStageResidualNode(std::unique_ptr<Node> dry, std::unique_ptr<Node> residual, const void* identity)
        : dryInput(std::move(dry)), residualInput(std::move(residual)), owner(identity)
    {
        if (!dryInput || !residualInput || !owner)
            throw std::invalid_argument("Prepared new-dry, exact finite residual and stage owner required");
        setOptimisations({tracktion::graph::ClearBuffers::yes, tracktion::graph::AllocateAudioBuffer::yes});
    }
    tracktion::graph::NodeProperties getNodeProperties() override
    {
        auto p = dryInput->getNodeProperties(); // Preserve actual dry producer origin.
        if (p.nodeID) tracktion::hash_combine(p.nodeID, std::size_t(0x56494e5645525345ULL));
        return p;
    }
    std::vector<Node*> getDirectInputNodes() override { return {dryInput.get(), residualInput.get()}; }
    bool isReadyToProcess() override { return dryInput->hasProcessed() && residualInput->hasProcessed(); }
    void prepareToPlay(const tracktion::graph::PlaybackInitialisationInfo& info) override
    {
        if (capability) throw std::invalid_argument("Inverse shell is one-shot");
        capability = tracktion::graph::LatencyNode::prepareInverseOutputResidual(*this, info, owner);
    }
    tracktion::graph::InverseOutputResidual* preparedInverseOutputResidual() const noexcept override
    { return capability.get(); }
    Node* inverseResidualInputNode() const noexcept override { return residualInput.get(); }
    void process(ProcessContext& pc) override
    {
        if (!capability->validateNewDryQueue(pc.referenceSampleRange))
            return; // Typed provenance/history failure; no output publication.
        const auto dry = dryInput->getProcessedOutput();
        const auto residual = residualInput->getProcessedOutput();
        if (!dry.midi.isEmpty() || dry.midi.isAllNotesOff || !residual.midi.isEmpty() || residual.midi.isAllNotesOff)
        {
            capability->refuseUnexpectedMidi();
            return; // Typed failed callback, zero-initialized output; never healthy playback.
        }
        capability->select(pc.buffers.audio, dry.audio, residual.audio, pc.referenceSampleRange);
        // Failed selection leaves the Node-cleared output. CONTROL containment
        // is required; no normal playback/recovery/completion claim on failure.
    }
};
} // namespace vibera
