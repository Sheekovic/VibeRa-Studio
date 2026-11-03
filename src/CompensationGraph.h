// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "CompensationGraphState.h"
#include "CompensationPhasePair.h"
#include <tracktion_engine/tracktion_engine.h>
#include <tracktion_graph/tracktion_graph.h>
#include <atomic>
#include <memory>
#include <optional>
#include <typeinfo>

namespace vibera
{
// Audio-output scope marker. Its child remains a normal engine graph. Only the
// first actual processing call acknowledges this marker's adoption. It neither
// waits for workers nor pretends to acknowledge every output of a multi-device
// graph. MIDI output is dispatched by its own engine device path, not this wave
// output marker. Graph/state owners retire on the existing non-RT reclaimer.
class CompensationGraphMarker final : public tracktion::graph::Node
{
    std::unique_ptr<Node> input;
    std::shared_ptr<CompensationGraphState> state;
    std::shared_ptr<juce::String> diagnostics;
    const std::uint64_t epoch;
    bool adopted = false;
    const std::optional<CompensationPhasePairPlan> phasePair;
    std::unique_ptr<CompensationPhasePairClock> phaseClock;
    tracktion::graph::LatencyNode* phaseTap = nullptr;
    bool bindPhasePair(TransformOptions &options)
    {
        if (!phasePair) return false;
        const auto &plan = *phasePair;
        tracktion::PluginNode *sourceNode = nullptr, *wetNode = nullptr;
        int sources = 0, wets = 0;
        const auto currentNodes = tracktion::graph::getNodes(options.rootNode, tracktion::graph::VertexOrdering::postordering);
        for (auto *node : currentNodes)
            if (auto *plugin = dynamic_cast<tracktion::PluginNode *>(node))
                if (auto *remote = dynamic_cast<RemoteEffect *>(&plugin->getPlugin()))
                {
                    if (remote->phasePairPluginId() == plan.sourceId && remote->phasePairHost() == plan.source.get())
                    { sourceNode = plugin; ++sources; }
                    else if (remote->phasePairPluginId() == plan.wetId && remote->phasePairHost() == plan.wet.get())
                    { wetNode = plugin; ++wets; }
                    else throw std::invalid_argument("Phase-pair graph contains an unknown worker");
                }
        if (sources != 1 || wets != 1) throw std::invalid_argument("Phase-pair worker graph identity is not unique");
        tracktion::graph::LatencyNode *found = nullptr;
        int joins = 0;
        for (auto *node : currentNodes)
            if (auto *sum = dynamic_cast<tracktion::graph::SummingNode *>(node))
            {
                tracktion::graph::LatencyNode *dry = nullptr, *wet = nullptr;
                int dryCount = 0, wetCount = 0;
                for (auto *edge : sum->getDirectInputNodes())
                {
                    auto *logical = tracktion::graph::LatencyNode::logicalInputFor(edge, sum);
                    const auto mask = phasePairAncestors(logical, plan);
                    if (mask == 1) { dry = dynamic_cast<tracktion::graph::LatencyNode *>(edge); ++dryCount; }
                    if (mask == 3) { wet = dynamic_cast<tracktion::graph::LatencyNode *>(edge); ++wetCount; }
                    if (mask == 2) throw std::invalid_argument("Phase-pair wet branch has no common source");
                }
                if (dryCount && wetCount)
                {
                    if (dryCount != 1 || wetCount != 1 || !dry || !wet || !dry->belongsTo(sum) || !wet->belongsTo(sum))
                        throw std::invalid_argument("Phase-pair join needs unique prepared dry/wet audio taps");
                    const auto dryProperties = dry->uncompensatedInput()->getNodeProperties();
                    const auto wetProperties = wet->uncompensatedInput()->getNodeProperties();
                    if (dryProperties.numberOfChannels != 2 || wetProperties.numberOfChannels != 2 ||
                        wet->compensationSamples() != 0 ||
                        wetProperties.latencyNumSamples - dryProperties.latencyNumSamples !=
                            dynamic_cast<RemoteEffect &>(wetNode->getPlugin()).preparedGraphLatencySamples())
                        return false; // Other graph transforms may still be discovering Return inputs.
                    found = dry; ++joins;
                }
            }
        if (joins == 0) return false; // Final prepare must reject an unresolved closed join.
        if (joins != 1 || !found) throw std::invalid_argument("Phase-pair graph needs exactly one closed parallel join");
        if (!phaseClock) phaseClock = std::make_unique<CompensationPhasePairClock>(plan, sourceNode, wetNode);
        if (phaseTap && phaseTap != found) throw std::invalid_argument("Phase-pair tap changed during graph transforms");
        phaseTap = found;
        return found->bindRuntimeAudioDelay({wetNode, phaseClock.get(), &CompensationPhasePairClock::beginThunk,
                                            &CompensationPhasePairClock::resolveThunk});
    }

  public:
    CompensationGraphMarker(std::unique_ptr<Node> source, std::shared_ptr<CompensationGraphState> observation,
                            std::uint32_t maximumDelay, std::size_t budget,
                            std::shared_ptr<juce::String> preparationDiagnostics,
                            std::optional<CompensationPhasePairPlan> phase = {})
        : input(std::move(source)), state(std::move(observation)),
          diagnostics(std::move(preparationDiagnostics)),
          epoch(state->desired.fetch_add(1, std::memory_order_acq_rel) + 1), phasePair(std::move(phase))
    {
        setAudioCompensationHistory(maximumDelay, budget); // Control/preparation only.
        setOptimisations({tracktion::graph::ClearBuffers::no, tracktion::graph::AllocateAudioBuffer::no});
    }
    tracktion::graph::NodeProperties getNodeProperties() override
    {
        auto properties = input->getNodeProperties();
        properties.hasMidi = false;
        tracktion::hash_combine(properties.nodeID, std::size_t(0x766962726165706fULL));
        return properties;
    }
    std::vector<Node *> getDirectInputNodes() override
    {
        return {input.get()};
    }
    bool isReadyToProcess() override
    {
        return input->hasProcessed();
    }
    tracktion::graph::TransformResult transform(TransformOptions &options) override
    {
        // Private graph preparation only; never invent pointer-based reuse IDs.
        diagnostics->clear();
        for (auto *node :
             tracktion::graph::getNodes(options.rootNode, tracktion::graph::VertexOrdering::postordering))
            if (dynamic_cast<tracktion::graph::SummingNode *>(node))
                for (auto *edge : node->getDirectInputNodes())
                {
                    auto *logical = tracktion::graph::LatencyNode::logicalInputFor(edge, node);
                    const auto properties = logical->getNodeProperties();
                    auto *tap = dynamic_cast<tracktion::graph::LatencyNode *>(edge);
                    if (properties.hasAudio && properties.numberOfChannels > 0 &&
                        (!tap || !tap->belongsTo(node)))
                        *diagnostics +=
                            juce::String(typeid(*logical).name()) +
                            " audio-edge ID=" + juce::String(juce::uint64(properties.nodeID)) +
                            " consumer=" + juce::String(juce::uint64(node->getNodeProperties().nodeID)) +
                            "; ";
                }
        return bindPhasePair(options) ? tracktion::graph::TransformResult::connectionsMade : tracktion::graph::TransformResult::none;
    }
    void prepareToPlay(const tracktion::graph::PlaybackInitialisationInfo &) override
    {
        if (phasePair && (!phaseClock || !phaseTap)) throw std::invalid_argument("Phase-pair binding not prepared");
        state->prepared.store(epoch, std::memory_order_release);
    }
    void process(ProcessContext &context) override
    {
        if (!adopted)
        {
            state->revision.fetch_add(1);
            state->firstReference.store(context.referenceSampleRange.getStart());
            state->adopted.store(epoch);
            state->revision.fetch_add(1);
            adopted = true;
        }
        if (phaseTap && phaseTap->runtimeAudioDelayFailures())
            state->phaseFailures.fetch_add(1, std::memory_order_relaxed);
        setAudioOutput(input.get(), input->getProcessedOutput().audio);
    }
};

// Internal control-thread experiment binding. Production opt-in remains off
// until memory admission, all-output adoption, MIDI and failure recovery have
// separate acceptance. The existing private graph prepare/publish path is used.
class CompensationGraphBinding final : private tracktion::TransportControl::Listener
{
    tracktion::TransportControl &transport;
    tracktion::EditPlaybackContext *installed = nullptr;
    const std::uint32_t maximumDelay;
    const std::size_t budget;
    std::shared_ptr<CompensationGraphState> state = std::make_shared<CompensationGraphState>();
    std::shared_ptr<juce::String> diagnostics = std::make_shared<juce::String>();
    juce::String failure;
    const std::optional<CompensationPhasePairPlan> phasePair;
    bool holdPublication = false;

    void install()
    {
        installed = transport.getCurrentPlaybackContext();
        if (!installed)
            return;
        installed->setInsertOptionalLastStageNodeForDeviceCallback(
            [observation = state, maximum = maximumDelay, report = diagnostics,
             bytes = budget, phase = phasePair](tracktion::OutputDevice &, const tracktion::CreateNodeParams &,
                             std::unique_ptr<tracktion::graph::Node> node)
            {
                return std::make_unique<CompensationGraphMarker>(std::move(node), observation, maximum, bytes,
                                                                 report, phase);
            });
    }
    void playbackContextChanged() override
    {
        refresh(); // Includes callback installation inside the exception boundary.
    }

  public:
    CompensationGraphBinding(tracktion::TransportControl &owner, std::uint32_t maximum, std::size_t bytes,
                           std::optional<CompensationPhasePairPlan> phase = {})
        : transport(owner), maximumDelay(maximum), budget(bytes), phasePair(std::move(phase))
    {
        try
        {
            transport.addListener(this);
            install();
        }
        catch (...)
        {
            // A throwing constructor has no destructor to remove this listener.
            transport.removeListener(this);
            if (installed && installed == transport.getCurrentPlaybackContext())
                installed->setInsertOptionalLastStageNodeForDeviceCallback({});
            throw;
        }
    }
    ~CompensationGraphBinding() override
    {
        transport.removeListener(this);
        if (installed && installed == transport.getCurrentPlaybackContext())
            installed->setInsertOptionalLastStageNodeForDeviceCallback({});
    }
    juce::Result refresh()
    {
        try
        {
            if (holdPublication) throw std::logic_error("Experimental graph publication is held");
            auto *context = transport.getCurrentPlaybackContext();
            if (!context)
            {
                failure = "Compensation experiment needs a prepared playback context";
                return juce::Result::fail(failure);
            }
            if (context != installed)
                install();
            // Narrow prepared-graph publication. No fallible device/MIDI/thread
            // preparation runs after publication; unrelated provider side
            // effects during graph construction are not transactional rollback.
            context->reallocateGraphOnly();
            failure.clear();
            return juce::Result::ok();
        }
        catch (const std::exception &error)
        {
            failure = juce::String(error.what()) + " | " + *diagnostics;
        }
        catch (...)
        {
            failure = "Unknown compensation graph preparation failure";
        }
        return juce::Result::fail(failure);
    }
    bool publicationHeld() const noexcept { return holdPublication; }
    juce::Result holdPublicationForTest(bool hold)
    {
        if (!phasePair) return juce::Result::fail("Publication hold needs the explicit internal phase-pair scope");
        holdPublication = hold;
        return juce::Result::ok();
    }
    CompensationGraphObservation observation() const noexcept
    {
        return state->read();
    }
    juce::String error() const
    {
        return state->phaseFailures.load(std::memory_order_relaxed) ?
            juce::String("Consumed-phase packet/reference/capacity invariant failed; audio result is unqualified") : failure;
    }
};
} // namespace vibera
