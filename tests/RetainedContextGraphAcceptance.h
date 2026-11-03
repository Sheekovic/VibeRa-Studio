// SPDX-License-Identifier: AGPL-3.0-or-later
// ORIGINAL, UNBUILT experiment. A real same-Edit app-adapter fixture is required.
#pragma once
#include <tracktion_engine/tracktion_engine.h>
#include <tracktion_graph/tracktion_graph.h>
#include <atomic>
#include <memory>
#include <stdexcept>

namespace vibera::test::retained_context
{
struct OutputObservation
{
    std::atomic<unsigned> calls{0};
    std::atomic<juce::int64> firstReference{-1};
    unsigned constructed = 0; // Sole control owner only.
};
class OutputMarker final : public tracktion::graph::Node
{
    std::unique_ptr<Node> input;
    std::shared_ptr<OutputObservation> observation;
    bool first = true;

  public:
    OutputMarker(std::unique_ptr<Node> node, std::shared_ptr<OutputObservation> state)
        : input(std::move(node)), observation(std::move(state))
    {
        ++observation->constructed;
        setOptimisations({tracktion::graph::ClearBuffers::no, tracktion::graph::AllocateAudioBuffer::no});
    }
    tracktion::graph::NodeProperties getNodeProperties() override
    {
        auto result = input->getNodeProperties();
        result.hasMidi = false; // Explicit single audio-output experiment only.
        tracktion::hash_combine(result.nodeID, std::size_t(0x636f6e7465787470ULL));
        return result;
    }
    std::vector<Node *> getDirectInputNodes() override
    {
        return {input.get()};
    }
    bool isReadyToProcess() override
    {
        return input->hasProcessed();
    }
    void process(ProcessContext &context) override
    {
        if (first)
        {
            observation->firstReference.store(context.referenceSampleRange.getStart(),
                                              std::memory_order_release);
            first = false;
        }
        observation->calls.fetch_add(1, std::memory_order_release);
        setAudioOutput(input.get(), input->getProcessedOutput().audio);
    }
};

// REQUIRED driver contract:
// capture() includes actual Edit XML/list/cache identities/order, canonical
// history and worker identities; never compare mutable counters as snapshots.
// driveAndVerify(tag) processes the actual existing SyntheticDevice/engine path,
// retains PCM/reference/source worker processed counters and checks independent
// original musical/phase/cadence oracles. It must never process a preview graph.
// caller constructs only admitted original detached adapters before invoking.
template <class Capture, class DriveAndVerify, class Require, class RetainSnapshots>
void run(tracktion::EditPlaybackContext &context,
         std::function<tracktion::Plugin::Array(const tracktion::PluginList &)> resolver,
         std::function<bool(const tracktion::Plugin &)> fixedInitialisationAdmission, Capture capture,
         DriveAndVerify driveAndVerify, Require require, RetainSnapshots retainSnapshots)
{
    using Context = tracktion::EditPlaybackContext;
    const auto before = capture();
    auto observation = std::make_shared<OutputObservation>();
    auto overlay = std::make_shared<Context::GraphOverlay>();
    overlay->plugins = std::move(resolver);
    overlay->fixedInitialisationAdmission = std::move(fixedInitialisationAdmission);
    overlay->lastStage = [observation](tracktion::OutputDevice &, const tracktion::CreateNodeParams &,
                                       std::unique_ptr<tracktion::graph::Node> node)
    { return std::make_unique<OutputMarker>(std::move(node), observation); };
    {
        auto refused = context.prepareGraphOnly(overlay);
        require(observation->constructed == 1, "Fixture requires one actual audio output");
        require(observation->calls.load() == 0 && capture() == before,
                "Preparing real private graph must not adopt or mutate live registration");
        driveAndVerify("private_candidate_not_published");
        require(observation->calls.load() == 0, "Old graph continues before publication");
    } // Genuine prepared native candidate destruction on control, without post.
    const auto afterCandidateDestruction = capture(); // Same single capture as the original assertion.
    retainSnapshots("after-private-candidate-destroyed", before, afterCandidateDestruction);
    require(afterCandidateDestruction == before, "Destroying candidate preserves live identities/history");
    driveAndVerify("after_private_candidate_destroyed");
    {
        auto throwing = std::make_shared<Context::GraphOverlay>(*overlay);
        throwing->lastStage =
            [](tracktion::OutputDevice &, const tracktion::CreateNodeParams &,
               std::unique_ptr<tracktion::graph::Node>) -> std::unique_ptr<tracktion::graph::Node>
        { throw std::runtime_error("Original deliberate post-native-factory admission failure"); };
        bool caught = false;
        try
        {
            (void)context.prepareGraphOnly(throwing);
        }
        catch (const std::runtime_error &)
        {
            caught = true;
        }
        require(caught && capture() == before,
                "Late private graph construction failure preserves live registration");
        driveAndVerify("after_post_factory_admission_failure");
    }
    auto candidate = context.prepareGraphOnly(overlay);
    require(context.validatePreparedGraph(*candidate) == Context::ReadyGraphChangeStatus::ready,
            "Actual ready holder admitted by same retained context");
    require(context.publishPreparedGraph(*candidate) == Context::ReadyGraphChangeStatus::ready,
            "Prepared native holder publication succeeds");
    require(candidate->wasPublished(), "Publication readback is explicit");
    require(capture() == before, "Graph publication does not register private same-ID plugins");
    driveAndVerify("actual_candidate_adopted");
    require(observation->calls.load() > 0 && observation->firstReference.load() >= 0,
            "Actual output processing acknowledges adoption, not acceptance");
    const auto constructions = observation->constructed;
    context.reallocateGraphOnly(); // Later normal build must retain adopted overlay.
    require(observation->constructed == constructions + 1,
            "Ordinary rebuild consumes persistent adopted resolver/marker");
    driveAndVerify("after_ordinary_rebuild_uses_overlay");
    require(capture() == before, "Rebuild preserves live cache/list/history owners");
    {
        auto stale = context.prepareGraphOnly(overlay);
        context.reallocateGraphOnly();
        require(context.publishPreparedGraph(*stale) != Context::ReadyGraphChangeStatus::ready &&
                    !stale->wasPublished(),
                "Ordinary graph mutation invalidates an earlier ready holder without publication");
        driveAndVerify("after_stale_candidate_refused");
    }
}
} // namespace vibera::test::retained_context
