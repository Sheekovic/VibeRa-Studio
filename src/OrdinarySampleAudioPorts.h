// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PreparedAppMixerAudioPorts.h"
#include "PreparedPluginTransport.h"
#include "Project.h"
#include "ForwardStageDrain.h"
#include "RemoteEffect.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <vector>

namespace vibera::ordinary_sample_ports
{
// Ordinary managerGlobal observing baseline. This grants NO finite-tail,
// zero-history or source-epoch authority. Explicit forward requests still need
// native whole-stage/root admission. MIDI health only
// restricts optional future handoffs; it never mutes normal playback.
class Policy final : public std::enable_shared_from_this<Policy>
{
  public:
    using Ptr = std::shared_ptr<Policy>;
    using Binding = experimental::AppMixerAudioPorts::Binding;
    using Probe = experimental::AppMixerAudioPorts::Probe;
    struct Receipt
    {
        std::uint64_t generation = 0, callbacks = 0;
        const void *root = nullptr; // Identity only; never dereference after retirement.
        std::int64_t referenceStart = 0, referenceEnd = 0;
        unsigned frames = 0;
        bool optionalHandoffFault = false;
        unsigned continuationState = 0; // Native state + 1; zero means no capability.
    };

  private:
    using Terminal = experimental::NativeMidiTerminal;
    using OutputProgress = RemoteEffect::RemovalProgress;
    using JoinProgress = RemoteEffect::FreshJoinProgress;
    struct Continuity
    {
        const tracktion::graph::Node *root = nullptr;
        std::uint64_t generation = 0;
        std::int64_t end = 0;
    }; // Sole selected native DSP writer. CONTROL never reads these fields.
    struct StageObservation
    {
        const void *stage = nullptr;
        std::shared_ptr<OutputProgress> progress;
        tracktion::graph::FinishedForwardOutputIdentity identity;
        std::shared_ptr<JoinProgress> join;
    };
    struct Owner
    {
        Binding binding;
        std::shared_ptr<Terminal> terminal = std::make_shared<Terminal>();
    };
    struct Build
    {
        const Ptr policy;
        const std::uint64_t generation;
        const std::vector<StageObservation> observations;
        const void *const removalStage;
        const void *const freshJoinStage;
        Build(Ptr p, std::uint64_t g, std::vector<StageObservation> actual, const void *removal,
              const void *freshJoin)
            : policy(std::move(p)), generation(g), observations(std::move(actual)), removalStage(removal),
              freshJoinStage(freshJoin)
        {
        }
    };
    // No MIDI copy/allocation. The generic native producer and its genuine
    // recorded-output observers remain in the graph, with their ordinary
    // timestamp/SysEx/MPE behavior. Only this optional diagnostic is bounded.
    class MidiConsumer final : public tracktion::graph::Node
    {
        const std::shared_ptr<Node> input;
        const std::shared_ptr<Terminal> terminal;
        const std::size_t id;
        const std::shared_ptr<experimental::NativeMidiFaultPublisher> faults;

      public:
        MidiConsumer(std::shared_ptr<Node> source, std::shared_ptr<Terminal> sink, std::size_t identity,
                     std::shared_ptr<experimental::NativeMidiFaultPublisher> publisher)
            : input(std::move(source)), terminal(std::move(sink)), id(identity), faults(std::move(publisher))
        {
        }
        tracktion::graph::NodeProperties getNodeProperties() override
        {
            return {false, false, 0, input->getNodeProperties().latencyNumSamples, id};
        }
        std::vector<Node *> getDirectInputNodes() override
        {
            return {input.get()};
        }
        bool isReadyToProcess() override
        {
            return input->hasProcessed();
        }
        void process(ProcessContext &) override
        {
            terminal->consume(input->getProcessedOutput().midi);
            if (terminal->fault.load(std::memory_order_acquire) != Terminal::Fault::none)
                faults->publish(experimental::NativeMidiFaultPublisher::Fault::terminal);
        }
    };
    class AudioPort final : public tracktion::graph::Node,
                            public tracktion::graph::FinishedForwardContinuationOutput
    {
        const std::shared_ptr<Node> native;
        const std::shared_ptr<tracktion::graph::ConnectedNode> audio;
        const std::shared_ptr<MidiConsumer> midi;
        const bool usesSelector;
        const tracktion::Plugin::Ptr plugin;
        const std::shared_ptr<const void> runtime;
        const Ptr policy;
        const void *const stage;
        const std::size_t id;
        const std::shared_ptr<OutputProgress> outputProgress;
        const tracktion::graph::FinishedForwardOutputIdentity outputIdentity;

      public:
        AudioPort(std::shared_ptr<Node> mixed, tracktion::Plugin::Ptr p, const Owner &owner, Ptr authority,
                  std::shared_ptr<OutputProgress> progress = {},
                  tracktion::graph::FinishedForwardOutputIdentity identity = {})
            : native(std::move(mixed)), audio(std::make_shared<tracktion::graph::ConnectedNode>(
                                            std::size_t(p->itemID.getRawID()) ^ 0x4f524450415544ULL)),
              midi(std::make_shared<MidiConsumer>(native, owner.terminal,
                                                  std::size_t(p->itemID.getRawID()) ^ 0x4f5244504d4944ULL,
                                                  authority->faults)),
              usesSelector(native->getNodeProperties().hasAudio), plugin(std::move(p)),
              runtime(owner.binding.runtime), policy(std::move(authority)),
              stage(plugin->getPreparedAudioOutputStageIdentity()),
              id(std::size_t(plugin->itemID.getRawID()) ^ 0x4f5244504f5554ULL),
              outputProgress(std::move(progress)), outputIdentity(identity)
        {
            const auto props = native->getNodeProperties();
            if (props.numberOfChannels != 2 || props.audioCompensationStage)
                throw std::invalid_argument(
                    "ordinaryPorts.output: Actual unregistered post-remap stereo output required");
            if (usesSelector)
            {
                if (!audio->addAudioConnection(native, {0, 0}) || !audio->addAudioConnection(native, {1, 1}))
                    throw std::invalid_argument("ordinaryPorts.connection: Actual stereo selection failed");
            }
            else if (owner.binding.role != Binding::Role::masterGate)
                throw std::invalid_argument("ordinaryPorts.source: Actual sample audio producer required");
            // An unconnected Mixer stage can truthfully declare stereo buses
            // without hasAudio. Preserve its actual buffer/metadata: it is NOT
            // a zero-PCM/history certificate, and an effect may still have tails.
            setOptimisations(
                {tracktion::graph::ClearBuffers::yes, tracktion::graph::AllocateAudioBuffer::no});
        }
        tracktion::graph::NodeProperties getNodeProperties() override
        {
            auto props = usesSelector ? audio->getNodeProperties() : native->getNodeProperties();
            props.hasMidi = false; // The real scheduled consumer owns this MIDI branch.
            props.nodeID = id;
            props.audioCompensationStage = stage;
            props.audioCompensationStageOrigin = stage ? this : nullptr;
            return props;
        }
        std::vector<Node *> getDirectInputNodes() override
        {
            return {audioSource(), midi.get()};
        }
        bool isReadyToProcess() override
        {
            return audioSource()->hasProcessed() && midi->hasProcessed();
        }
        tracktion::graph::FinishedForwardOutputIdentity
        preparedFinishedForwardOutputIdentity() const noexcept override
        {
            return outputIdentity;
        }
        bool isRetainedDryContinuation() const noexcept override
        {
            return outputProgress && outputProgress->isRemoval();
        }
        bool validateFinishedForwardOutput(juce::Range<std::int64_t> range,
                                           bool afterProcessing) const noexcept override
        {
            return outputProgress && (afterProcessing ? outputProgress->finishReference(range)
                                                       : outputProgress->beginReference(range));
        }
        void refuseFinishedForwardOutput() noexcept override
        {
            if (outputProgress) outputProgress->refuse();
        }
        bool matchesFreshJoinPredecessor(const Policy *actualPolicy, const void *actualStage,
                                         const OutputProgress *previous,
                                         const tracktion::graph::FinishedForwardOutputIdentity &fresh) const
        {
            return policy.get() == actualPolicy && stage == actualStage && runtime.get() == actualStage &&
                   outputProgress.get() == previous && isRetainedDryContinuation() &&
                   outputIdentity.valid() && fresh.valid() &&
                   outputIdentity.stageRuntime == fresh.stageRuntime &&
                   outputIdentity.sourceRuntime == fresh.sourceRuntime &&
                   outputIdentity.preparedPlan == fresh.preparedPlan &&
                   outputIdentity.producingLatency == fresh.producingLatency &&
                   outputIdentity.nativeLatency == fresh.nativeLatency &&
                   outputIdentity.processingRuntime != fresh.processingRuntime &&
                   outputIdentity.instance != fresh.instance;
        }
        bool matchesFreshJoinOutput(const Policy *actualPolicy, const void *actualStage,
                                    const OutputProgress *actual,
                                    const tracktion::graph::FinishedForwardOutputIdentity &fresh) const
        {
            return policy.get() == actualPolicy && stage == actualStage && runtime.get() == actualStage &&
                   outputProgress.get() == actual && !isRetainedDryContinuation() && fresh.valid() &&
                   outputIdentity == fresh;
        }
        bool matchesSteadyRemoval(const Policy *actualPolicy, const void *actualStage,
                                  const OutputProgress *actual,
                                  const tracktion::graph::FinishedForwardOutputIdentity &expected,
                                  bool removing) const noexcept
        {
            return policy.get() == actualPolicy && stage == actualStage && runtime.get() == actualStage &&
                   outputProgress.get() == actual && isRetainedDryContinuation() == removing &&
                   expected.valid() && outputIdentity == expected;
        }
        void prepareToPlay(const tracktion::graph::PlaybackInitialisationInfo &info) override
        {
            if (!stage || !info.nodeGraphToReplace)
                return;
            bool found = false;
            for (auto *node : info.nodeGraphToReplace->orderedNodes)
            {
                const auto props = node->getNodeProperties();
                if (props.audioCompensationStage != stage)
                    continue;
                const AudioPort *prior = nullptr;
                for (auto *possible : info.nodeGraphToReplace->orderedNodes)
                    if (possible == props.audioCompensationStageOrigin)
                        prior = dynamic_cast<AudioPort *>(possible);
                if (!prior || prior->policy != policy || prior->runtime != runtime ||
                    prior->plugin->itemID != plugin->itemID)
                    throw std::invalid_argument(
                        "ordinaryPorts.predecessor: No warm mixed/foreign history migration");
                found = true;
            }
            if (!found)
                throw std::invalid_argument("ordinaryPorts.predecessor: Exact old audio stage required");
        }
        void process(ProcessContext &) override
        {
            auto *source = audioSource();
            setAudioOutput(source, source->getProcessedOutput().audio);
        }

      private:
        Node *audioSource() const noexcept
        {
            return usesSelector ? audio.get() : native.get();
        }
    };
    class TrackBoundary final : public tracktion::graph::Node
    {
        const std::shared_ptr<tracktion::graph::ConnectedNode> empty;
        const std::shared_ptr<MidiConsumer> midi;
        const std::size_t id;

      public:
        TrackBoundary(std::unique_ptr<Node> native, std::shared_ptr<Terminal> terminal, std::size_t track,
                      std::shared_ptr<experimental::NativeMidiFaultPublisher> faults)
            : empty(std::make_shared<tracktion::graph::ConnectedNode>(track ^ 0x4f524454454d50ULL)),
              midi(makeConsumer(std::move(native), std::move(terminal), track, std::move(faults))),
              id(track ^ 0x4f524454424e44ULL)
        {
            setOptimisations(
                {tracktion::graph::ClearBuffers::yes, tracktion::graph::AllocateAudioBuffer::no});
        }
        static std::shared_ptr<MidiConsumer>
        makeConsumer(std::unique_ptr<Node> native, std::shared_ptr<Terminal> terminal, std::size_t id,
                     std::shared_ptr<experimental::NativeMidiFaultPublisher> faults)
        {
            if (!native || typeid(*native) != typeid(tracktion::LiveMidiInjectingNode))
                throw std::invalid_argument("ordinaryPorts.track: Actual ordinary native injector required");
            const auto props = native->getNodeProperties();
            if (props.hasAudio || props.numberOfChannels != 0 || props.latencyNumSamples != 0)
                throw std::invalid_argument(
                    "ordinaryPorts.trackAudio: Never disconnect real clip/live audio");
            return std::make_shared<MidiConsumer>(std::shared_ptr<Node>(std::move(native)),
                                                  std::move(terminal), id ^ 0x4f5244544d4944ULL,
                                                  std::move(faults));
        }
        tracktion::graph::NodeProperties getNodeProperties() override
        {
            auto props = empty->getNodeProperties();
            props.nodeID = id;
            return props;
        }
        std::vector<Node *> getDirectInputNodes() override
        {
            return {empty.get(), midi.get()};
        }
        bool isReadyToProcess() override
        {
            return empty->hasProcessed() && midi->hasProcessed();
        }
        void process(ProcessContext &) override
        {
            setAudioOutput(empty.get(), empty->getProcessedOutput().audio);
        }
    };
    class Root final : public tracktion::graph::Node,
                       public tracktion::graph::FinishedForwardContinuationRoot
    {
        const std::unique_ptr<Node> input;
        const std::shared_ptr<Build> build;
        std::unique_ptr<tracktion::graph::FinishedForwardContinuation> continuation;
        const void *nativeContinuationStage = nullptr;
        bool steadyRemoval = false, steadyRemovalStarted = false;
        const Node *removalPreviousRoot = nullptr; // DSP compares identity only after retirement.
        std::uint64_t removalPreviousGeneration = 0;
        int removalMaximumSpan = 0;
        bool blockAdmitted = true, observationsHealthy = true;
        const Node *joinPreviousRoot = nullptr; // Identity only after old graph retirement.
        std::uint64_t joinPreviousGeneration = 0;
        int joinMaximumSpan = 0;
        bool joinStarted = false;

      public:
        Root(std::unique_ptr<Node> source, std::shared_ptr<Build> prepared)
            : input(std::move(source)), build(std::move(prepared))
        {
            setOptimisations({tracktion::graph::ClearBuffers::no, tracktion::graph::AllocateAudioBuffer::no});
        }
        tracktion::graph::NodeProperties getNodeProperties() override
        {
            auto p = input->getNodeProperties();
            p.nodeID ^= 0x4f5244524f4f54ULL;
            return p;
        }
        std::vector<Node *> getDirectInputNodes() override
        {
            return {input.get()};
        }
        bool isReadyToProcess() override
        {
            return input->hasProcessed();
        }
        const void *finishedForwardContinuityIdentity() const noexcept override
        {
            return build->policy->continuity.get();
        }
        std::uint64_t finishedForwardGeneration() const noexcept override
        {
            return build->generation;
        }
        const void *finishedForwardContinuationStage() const noexcept override
        {
            return nativeContinuationStage;
        }
        tracktion::graph::FinishedForwardContinuation *
        preparedFinishedForwardContinuation() const noexcept override
        {
            return continuation.get();
        }
        void prepareToPlay(const tracktion::graph::PlaybackInitialisationInfo &info) override
        {
            const auto props = input->getNodeProperties();
            if (info.nodeGraph.rootNode.get() != this || props.hasMidi || props.numberOfChannels != 2)
                throw std::invalid_argument(
                    "ordinaryPorts.root: Actual final stereo audio-only device root required");
            if (build->removalStage)
            {
                bool hasForward = false;
                if (info.nodeGraphToReplace)
                    for (auto *node : info.nodeGraphToReplace->orderedNodes)
                        if (node->getNodeProperties().audioCompensationStage == build->removalStage &&
                            dynamic_cast<tracktion::graph::ForwardOutputDrainNode *>(node))
                            hasForward = true;
                if (hasForward)
                {
                    // Never fall back from an unfinished/failed forward proof.
                    nativeContinuationStage = build->removalStage;
                    continuation = tracktion::graph::LatencyNode::prepareFinishedForwardContinuation(
                        *this, info, build->removalStage);
                }
                else
                {
                    auto *previous = info.nodeGraphToReplace
                                         ? dynamic_cast<Root *>(info.nodeGraphToReplace->rootNode.get())
                                         : nullptr;
                    if (!previous || previous->build->policy != build->policy ||
                        previous->build->generation >= build->generation || info.blockSize <= 0)
                        throw std::invalid_argument(
                            "ordinaryPorts.removal.root: Actual same-policy predecessor required");
                    const auto oldRoot = previous->getNodeProperties();
                    if (oldRoot.hasMidi || oldRoot.numberOfChannels != props.numberOfChannels ||
                        oldRoot.latencyNumSamples != props.latencyNumSamples)
                        throw std::invalid_argument(
                            "ordinaryPorts.removal.shape: Equal-latency root required");
                    const StageObservation *target = nullptr;
                    for (const auto &actual : build->observations)
                        if (actual.stage == build->removalStage)
                        {
                            if (target || actual.join || !actual.progress->isRemoval() ||
                                !actual.identity.valid())
                                throw std::invalid_argument(
                                    "ordinaryPorts.removal.capture: One actual removal owner required");
                            target = &actual;
                        }
                    if (!target)
                        throw std::invalid_argument(
                            "ordinaryPorts.removal.missing: Actual target stage required");
                    const auto predecessor = target->progress->predecessorControl();
                    unsigned oldOrigins = 0, newOrigins = 0;
                    const auto inspect = [&](tracktion::graph::NodeGraph &graph, bool removing)
                    {
                        for (auto *node : graph.orderedNodes)
                        {
                            const auto shape = node->getNodeProperties();
                            if (shape.audioCompensationStage != build->removalStage)
                                continue;
                            if (dynamic_cast<tracktion::graph::ForwardOutputDrainNode *>(node) ||
                                dynamic_cast<tracktion::graph::InverseOutputResidualNode *>(node))
                                throw std::invalid_argument(
                                    "ordinaryPorts.removal.bridge: Steady wet stage required");
                            if (shape.audioCompensationStageOrigin != node)
                                continue;
                            auto *port = dynamic_cast<AudioPort *>(node);
                            if (!port || !predecessor ||
                                !port->matchesSteadyRemoval(build->policy.get(), build->removalStage,
                                                            removing ? target->progress.get()
                                                                     : predecessor.get(),
                                                            target->identity, removing) ||
                                shape.hasMidi || shape.numberOfChannels != 2 ||
                                shape.latencyNumSamples != target->identity.producingLatency)
                                throw std::invalid_argument("ordinaryPorts.removal.origin: Exact same-D wet "
                                                            "and removal origins required");
                            if (removing)
                                ++newOrigins;
                            else
                                ++oldOrigins;
                        }
                    };
                    inspect(*info.nodeGraphToReplace, false);
                    inspect(info.nodeGraph, true);
                    if (oldOrigins != 1 || newOrigins != 1)
                        throw std::invalid_argument(
                            "ordinaryPorts.removal.origin: One actual origin per graph required");
                    // Ordinary final native validation still requires every
                    // output/internal FIFO to retain its exact configuration
                    // and processor. This grants no exception or zero proof.
                    steadyRemoval = true;
                    removalPreviousRoot = previous;
                    removalPreviousGeneration = previous->build->generation;
                    removalMaximumSpan = info.blockSize;
                }
            }
            if (build->freshJoinStage)
            {
                auto *previous = info.nodeGraphToReplace
                                     ? dynamic_cast<Root *>(info.nodeGraphToReplace->rootNode.get())
                                     : nullptr;
                if (continuation || !previous || previous->build->policy != build->policy ||
                    previous->build->generation >= build->generation || info.blockSize <= 0)
                    throw std::invalid_argument(
                        "ordinaryPorts.join.root: Actual same-policy predecessor required");
                const auto oldProps = previous->getNodeProperties();
                if (oldProps.hasMidi || oldProps.numberOfChannels != props.numberOfChannels ||
                    oldProps.latencyNumSamples != props.latencyNumSamples)
                    throw std::invalid_argument(
                        "ordinaryPorts.join.shape: Actual equal-latency root required");
                const StageObservation *target = nullptr;
                for (const auto &actual : build->observations)
                    if (actual.stage == build->freshJoinStage)
                    {
                        if (target || !actual.join || actual.join->outputControl() != actual.progress)
                            throw std::invalid_argument(
                                "ordinaryPorts.join.capture: One actual fresh output required");
                        target = &actual;
                    }
                if (!target)
                    throw std::invalid_argument("ordinaryPorts.join.missing: Actual target stage required");
                const auto predecessor = target->join->predecessorControl();
                unsigned origins = 0;
                for (auto *node : info.nodeGraphToReplace->orderedNodes)
                {
                    const auto old = node->getNodeProperties();
                    if (old.audioCompensationStage != build->freshJoinStage)
                        continue;
                    if (dynamic_cast<tracktion::graph::ForwardOutputDrainNode *>(node) ||
                        dynamic_cast<tracktion::graph::InverseOutputResidualNode *>(node) ||
                        dynamic_cast<tracktion::graph::LatencyNode *>(node))
                        throw std::invalid_argument(
                            "ordinaryPorts.join.forward: Previous forward proof is not a dry join");
                    if (old.audioCompensationStageOrigin != node)
                        continue;
                    auto *port = dynamic_cast<AudioPort *>(node);
                    if (!port || !predecessor ||
                        !port->matchesFreshJoinPredecessor(build->policy.get(), build->freshJoinStage,
                                                           predecessor.get(), target->identity) ||
                        old.hasMidi || old.numberOfChannels != 2 ||
                        old.latencyNumSamples != target->identity.producingLatency)
                        throw std::invalid_argument(
                            "ordinaryPorts.join.predecessor: Exact dry stage origin required");
                    ++origins;
                }
                if (origins != 1)
                    throw std::invalid_argument("ordinaryPorts.join.origin: One actual old origin required");
                origins = 0;
                for (auto *node : info.nodeGraph.orderedNodes)
                {
                    const auto now = node->getNodeProperties();
                    if (now.audioCompensationStage != build->freshJoinStage)
                        continue;
                    if (dynamic_cast<tracktion::graph::LatencyNode *>(node) ||
                        dynamic_cast<tracktion::graph::ForwardOutputDrainNode *>(node) ||
                        dynamic_cast<tracktion::graph::InverseOutputResidualNode *>(node))
                        throw std::invalid_argument(
                            "ordinaryPorts.join.queue: Same producing D without output FIFO required");
                    if (now.audioCompensationStageOrigin != node)
                        continue;
                    auto *port = dynamic_cast<AudioPort *>(node);
                    if (!port ||
                        !port->matchesFreshJoinOutput(build->policy.get(), build->freshJoinStage,
                                                      target->progress.get(), target->identity) ||
                        now.hasMidi || now.numberOfChannels != 2 ||
                        now.latencyNumSamples != target->identity.producingLatency)
                        throw std::invalid_argument(
                            "ordinaryPorts.join.output: Exact fresh stage origin required");
                    ++origins;
                }
                if (origins != 1)
                    throw std::invalid_argument(
                        "ordinaryPorts.join.origin: One actual fresh origin required");
                joinPreviousRoot = previous;
                joinPreviousGeneration = previous->build->generation;
                joinMaximumSpan = info.blockSize;
            }
        }
        void prefetchBlock(juce::Range<std::int64_t> range) override
        {
            observationsHealthy = true;
            blockAdmitted = true;
            if (continuation)
            {
                const auto &last = *build->policy->continuity;
                blockAdmitted = continuation->begin(*this, build->policy->continuity.get(), last.root,
                                                     last.generation, last.end, range);
            }
            for (const auto &actual : build->observations)
            {
                if (continuation && actual.stage == build->removalStage)
                    continue; // Native capability opens this exact adapter scope.
                if (actual.join)
                {
                    if (actual.stage == build->freshJoinStage)
                    {
                        const auto &last = *build->policy->continuity;
                        const bool follows =
                            range.getStart() >= 0 && range.getEnd() > range.getStart() &&
                            range.getLength() <= joinMaximumSpan && last.end == range.getStart() &&
                            (joinStarted ? (last.root == this && last.generation == build->generation)
                                         : (last.root == joinPreviousRoot &&
                                            last.generation == joinPreviousGeneration));
                        if (!follows)
                        {
                            actual.join->refuse();
                            blockAdmitted = observationsHealthy = false;
                            continue;
                        }
                    }
                    if (!actual.join->beginReference(range))
                        blockAdmitted = observationsHealthy = false;
                    continue; // Join owns BOTH actual dry and fresh wet scopes.
                }
                if (steadyRemoval && actual.stage == build->removalStage)
                {
                    const auto &last = *build->policy->continuity;
                    const bool follows =
                        range.getStart() >= 0 && range.getEnd() > range.getStart() &&
                        range.getLength() <= removalMaximumSpan && last.end == range.getStart() &&
                        (steadyRemovalStarted ? (last.root == this && last.generation == build->generation)
                                              : (last.root == removalPreviousRoot &&
                                                 last.generation == removalPreviousGeneration));
                    if (!follows)
                    {
                        actual.progress->refuse();
                        blockAdmitted = observationsHealthy = false;
                        continue;
                    }
                }
                if (!actual.progress->beginReference(range))
                {
                    if (actual.progress->isRemoval())
                    {
                        observationsHealthy = false;
                        blockAdmitted = false;
                    }
                }
            }
        }
        void process(ProcessContext &pc) override
        {
            const auto output = input->getProcessedOutput();
            setAudioOutput(input.get(), output.audio); // Normal audio never fails silent for a ledger fault.
            for (const auto &actual : build->observations)
            {
                if (continuation && actual.stage == build->removalStage)
                    continue;
                if (actual.join)
                {
                    if (!actual.join->finishReference(pc.referenceSampleRange))
                        blockAdmitted = observationsHealthy = false;
                    continue;
                }
                if (!actual.progress->finishReference(pc.referenceSampleRange))
                {
                    if (actual.progress->isRemoval())
                    {
                        observationsHealthy = false;
                        blockAdmitted = false;
                    }
                    // A skipped/unproducing wet effect cannot prove removal
                    // readiness, but its diagnostic never faults other stages.
                }
            }
            if (continuation && blockAdmitted)
                blockAdmitted = continuation->end(*this, pc.referenceSampleRange);
            // The removal shell contains its failed target stage. Preserve
            // unrelated summed voices and report failure; never mute the root
            // as a purported recovery from an optional handoff failure.
            // Native stopped/loop boundaries can carry a genuine global panic
            // flag even at an audio-only root. Consume that event in this real
            // terminal; do not merely discard it or label it malformed MIDI.
            // Unexpected message payloads remain an optional-handoff fault.
            build->policy->rootTerminal->consume(output.midi);
            if (!output.midi.isEmpty() || output.midi.isAllNotesOff)
                build->policy->rootMidiFlags.fetch_or(unsigned(!output.midi.isEmpty()) |
                                                          (unsigned(output.midi.isAllNotesOff) << 1),
                                                      std::memory_order_release);
            const bool optionalFault =
                !blockAdmitted || !observationsHealthy || !output.midi.isEmpty() ||
                build->policy->rootTerminal->fault.load(std::memory_order_acquire) != Terminal::Fault::none ||
                build->policy->faults->observation() != experimental::NativeMidiFaultPublisher::Fault::none;
            if (!optionalFault)
            {
                auto &last = *build->policy->continuity;
                last.root = this;
                last.generation = build->generation;
                last.end = pc.referenceSampleRange.getEnd();
                if (build->freshJoinStage)
                    joinStarted = true;
                if (steadyRemoval)
                    steadyRemovalStarted = true;
            }
            build->policy->publish(*this, *build, pc, optionalFault,
                                   continuation ? unsigned(continuation->observation()) + 1 : 0);
        }
    };
    const PreparedPluginTransport::Ptr plan;
    const std::shared_ptr<experimental::NativeMidiFaultPublisher> faults =
        std::make_shared<experimental::NativeMidiFaultPublisher>();
    const std::shared_ptr<Terminal> rootTerminal = std::make_shared<Terminal>();
    const std::shared_ptr<Continuity> continuity = std::make_shared<Continuity>();
    tracktion::Edit *edit = nullptr; // CONTROL only, no owning backreference.
    const tracktion::ProcessState *scope = nullptr;
    bool rootInstalled = false;             // CONTROL-only factory state, never DSP-read.
    std::atomic<unsigned> rootMidiFlags{0}; // Diagnostic bits: actual event/panic seen at final root.
    static_assert(std::atomic<unsigned>::is_always_lock_free);
    const void *activeForward = nullptr; // Synchronous CONTROL factory lease only.
    std::shared_ptr<ForwardStageDrainNode::Progress> activeForwardProgress;
    std::shared_ptr<OutputProgress> activeWetProgress;
    const void *activeRemoval = nullptr; // Synchronous CONTROL lease only.
    std::shared_ptr<OutputProgress> activeRemovalProgress;
    const void *activeFreshJoin = nullptr; // Synchronous CONTROL lease only.
    std::shared_ptr<JoinProgress> activeFreshJoinProgress;
    std::map<juce::uint64, Owner> owners;
    std::map<juce::uint64, std::shared_ptr<Terminal>> tracks;
    std::uint64_t nextGeneration = 0, processed = 0; // CONTROL and DSP respectively, never cross-read.
    std::atomic<std::uint64_t> publication{0};
    std::array<std::atomic<std::uint64_t>, 8> fields{};
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    explicit Policy(PreparedPluginTransport::Ptr actual) : plan(std::move(actual)) {}
    static bool physicalCurrent(const PreparedPluginTransport &actual) noexcept
    {
        juce::AudioDeviceManager::PhysicalCallbackSnapshot physical;
        const bool known = actual.read(physical);
        const auto admitted = transport::admitPhysicalBurst(known, physical, actual.budget());
        return known && physical.maximumOuterFrames <= actual.budget().physicalFrames &&
               (admitted.status == transport::BurstStatus::ready ||
                (admitted.status == transport::BurstStatus::noPhysicalCallback && physical.outerFrames == 0 &&
                 physical.maximumOuterFrames == 0));
    }
    static void control()
    {
        const auto *m = juce::MessageManager::getInstanceWithoutCreating();
        if (!m || !m->isThisTheMessageThread())
            throw std::invalid_argument("ordinaryPorts.control: CONTROL required");
    }
    void publish(Root &root, const Build &build, const tracktion::graph::Node::ProcessContext &pc,
                 bool fault, unsigned continuationState) noexcept
    {
        if (processed == std::numeric_limits<std::uint64_t>::max())
            fault = true;
        else
            ++processed;
        if (fault)
            faults->publish(experimental::NativeMidiFaultPublisher::Fault::terminal);
        if (publication.load(std::memory_order_relaxed) >= std::numeric_limits<std::uint64_t>::max() - 2)
        {
            faults->publish(experimental::NativeMidiFaultPublisher::Fault::terminal);
            return;
        }
        const std::array<std::uint64_t, 8> values{build.generation,
                                                  processed,
                                                  std::uint64_t(reinterpret_cast<std::uintptr_t>(&root)),
                                                  std::uint64_t(pc.referenceSampleRange.getStart()),
                                                  std::uint64_t(pc.referenceSampleRange.getEnd()),
                                                  std::uint64_t(pc.numSamples),
                                                  std::uint64_t(fault), continuationState};
        publication.fetch_add(1, std::memory_order_seq_cst);
        for (std::size_t i = 0; i < values.size(); ++i)
            fields[i].store(values[i], std::memory_order_seq_cst);
        publication.fetch_add(1, std::memory_order_seq_cst);
    }

  public:
    class ForwardLease final
    {
        friend class Policy;
        const Ptr owner;
        const void *const stage;
        const std::shared_ptr<ForwardStageDrainNode::Progress> progress;
        ForwardLease(Ptr p, const tracktion::Plugin &source, std::shared_ptr<OutputProgress> wet)
            : owner(std::move(p)), stage(source.getPreparedAudioOutputStageIdentity()),
              progress(std::make_shared<ForwardStageDrainNode::Progress>())
        {
            control();
            const auto found = owner->owners.find(source.itemID.getRawID());
            if (!owner->rootInstalled)
                throw std::invalid_argument("ordinaryPorts.forward.root: Actual baseline root required");
            if (!owner->edit || &source.edit != owner->edit || owner->activeForward || owner->activeRemoval ||
                owner->activeFreshJoin)
                throw std::invalid_argument("ordinaryPorts.forward.owner: Exact idle Edit factory required");
            if (!physicalCurrent(*owner->plan))
                throw std::invalid_argument(
                    "ordinaryPorts.forward.device: Exact active prepared plan required");
            if (owner->faults->observation() != experimental::NativeMidiFaultPublisher::Fault::none)
            {
                juce::String detail =
                    "ordinaryPorts.forward.midi: Baseline MIDI/root fault retained; rootFlags=" +
                    juce::String(owner->rootMidiFlags.load(std::memory_order_acquire));
                const auto append = [&](auto id, const std::shared_ptr<Terminal> &terminal, const char *kind)
                {
                    const auto receipt = terminal->observe();
                    if (receipt.fault != Terminal::Fault::none)
                        detail += " " + juce::String(kind) + "=" + juce::String(juce::int64(id)) + ":fault" +
                                  juce::String(unsigned(receipt.fault));
                };
                for (const auto &[id, entry] : owner->owners)
                    append(id, entry.terminal, "port");
                for (const auto &[id, terminal] : owner->tracks)
                    append(id, terminal, "track");
                throw std::invalid_argument(
                    detail.toStdString()); // CONTROL-only compact actual fault evidence.
            }
            if (!stage || found == owner->owners.end() ||
                found->second.binding.role != Binding::Role::masterGate ||
                found->second.binding.runtime.get() != stage)
                throw std::invalid_argument(
                    "ordinaryPorts.forward.stage: Exact current Mixer stage required");
            if (wet && (wet->isRemoval() ||
                        wet->bindingControl().pcm.preparedTransport !=
                            reinterpret_cast<std::uintptr_t>(owner->plan.get()) ||
                        wet->observation().fault != OutputProgress::Fault::none))
                throw std::invalid_argument("ordinaryPorts.forward.capture: Exact prepared wet owner required");
            owner->activeForwardProgress = progress;
            owner->activeWetProgress = std::move(wet);
            owner->activeForward = stage;
        }

      public:
        ~ForwardLease()
        {
            owner->activeForward = nullptr;
            owner->activeForwardProgress.reset(); // CONTROL; prepared graph retains its own receipt pin.
            owner->activeWetProgress.reset();
        }
        ForwardLease(const ForwardLease &) = delete;
        ForwardLease &operator=(const ForwardLease &) = delete;
        std::shared_ptr<ForwardStageDrainNode::Progress> receipt() const
        {
            return progress;
        }
    };
    std::unique_ptr<ForwardLease> leaseForward(const tracktion::Plugin &source,
                                             std::shared_ptr<OutputProgress> wetProgress = {})
    {
        return std::unique_ptr<ForwardLease>(
            new ForwardLease(shared_from_this(), source, std::move(wetProgress)));
    }
    class RemovalLease final
    {
        friend class Policy;
        const Ptr owner;
        RemovalLease(Ptr p, const tracktion::Plugin &source, std::shared_ptr<OutputProgress> progress)
            : owner(std::move(p))
        {
            control();
            const auto stage = source.getPreparedAudioOutputStageIdentity();
            const auto found = owner->owners.find(source.itemID.getRawID());
            if (!owner->rootInstalled || !owner->edit || &source.edit != owner->edit ||
                owner->activeForward || owner->activeRemoval || owner->activeFreshJoin ||
                !physicalCurrent(*owner->plan) ||
                owner->faults->observation() != experimental::NativeMidiFaultPublisher::Fault::none ||
                !stage || found == owner->owners.end() ||
                found->second.binding.role != Binding::Role::masterGate ||
                found->second.binding.runtime.get() != stage || !progress || !progress->isRemoval() ||
                progress->bindingControl().pcm.preparedTransport !=
                    reinterpret_cast<std::uintptr_t>(owner->plan.get()) ||
                progress->observation().fault != OutputProgress::Fault::none)
                throw std::invalid_argument(
                    "ordinaryPorts.removal.owner: Exact live stage and prepared retained history required");
            owner->activeRemovalProgress = std::move(progress);
            owner->activeRemoval = stage;
        }

      public:
        ~RemovalLease()
        {
            owner->activeRemoval = nullptr;
            owner->activeRemovalProgress.reset(); // No DSP scope abort on CONTROL cancellation.
        }
        RemovalLease(const RemovalLease &) = delete;
        RemovalLease &operator=(const RemovalLease &) = delete;
    };
    std::unique_ptr<RemovalLease> leaseRemoval(const tracktion::Plugin &source,
                                             std::shared_ptr<OutputProgress> removalProgress)
    {
        return std::unique_ptr<RemovalLease>(
            new RemovalLease(shared_from_this(), source, std::move(removalProgress)));
    }
    class FreshJoinLease final
    {
        friend class Policy;
        const Ptr owner;
        FreshJoinLease(Ptr p, const tracktion::Plugin &source, std::shared_ptr<JoinProgress> join)
            : owner(std::move(p))
        {
            control();
            const auto stage = source.getPreparedAudioOutputStageIdentity();
            const auto found = owner->owners.find(source.itemID.getRawID());
            if (!owner->rootInstalled || !owner->edit || &source.edit != owner->edit ||
                owner->activeForward || owner->activeRemoval || owner->activeFreshJoin ||
                !physicalCurrent(*owner->plan) ||
                owner->faults->observation() != experimental::NativeMidiFaultPublisher::Fault::none ||
                !stage || found == owner->owners.end() ||
                found->second.binding.role != Binding::Role::masterGate ||
                found->second.binding.runtime.get() != stage || !join ||
                join->observation().phase != JoinProgress::Phase::prepared ||
                join->observation().fault != JoinProgress::Fault::none)
                throw std::invalid_argument(
                    "ordinaryPorts.join.owner: Actual stage and fresh prepared join required");
            const auto previous = join->predecessorControl();
            const auto output = join->outputControl();
            if (!previous || !output || previous == output || !previous->isRemoval() || output->isRemoval() ||
                previous->observation().phase != OutputProgress::Phase::dry ||
                previous->observation().fault != OutputProgress::Fault::none ||
                output->observation().phase != OutputProgress::Phase::prepared ||
                output->observation().fault != OutputProgress::Fault::none)
                throw std::invalid_argument(
                    "ordinaryPorts.join.capture: Exact dry and fresh wet owners required");
            const auto &before = previous->bindingControl();
            const auto &after = output->bindingControl();
            if (!before.processingRuntime || !after.processingRuntime ||
                before.processingRuntime == after.processingRuntime || !before.dryHistory ||
                !after.dryHistory || before.dryHistory == after.dryHistory ||
                before.pcm.instance == after.pcm.instance ||
                before.pcm.preparedTransport != reinterpret_cast<std::uintptr_t>(owner->plan.get()) ||
                after.pcm.preparedTransport != before.pcm.preparedTransport ||
                after.pcm.quantum != before.pcm.quantum ||
                after.pcm.transportSamples != before.pcm.transportSamples ||
                after.native.nativeSamples != before.native.nativeSamples ||
                after.latency != before.latency || after.latency <= 0)
                throw std::invalid_argument(
                    "ordinaryPorts.join.configuration: Actual distinct fresh equal-D worker required");
            owner->activeFreshJoinProgress = std::move(join);
            owner->activeFreshJoin = stage;
        }

      public:
        ~FreshJoinLease()
        {
            owner->activeFreshJoin = nullptr;
            owner->activeFreshJoinProgress.reset(); // CONTROL cancellation never opens or aborts live scopes.
        }
        FreshJoinLease(const FreshJoinLease &) = delete;
        FreshJoinLease &operator=(const FreshJoinLease &) = delete;
    };
    std::unique_ptr<FreshJoinLease> leaseFreshJoin(const tracktion::Plugin &source,
                                                   std::shared_ptr<JoinProgress> join)
    {
        return std::unique_ptr<FreshJoinLease>(
            new FreshJoinLease(shared_from_this(), source, std::move(join)));
    }

    static Ptr prepare(const Project &p, const PreparedPluginTransport::Ptr &actual, bool stereoDevice)
    {
        control();
        if (!actual || !physicalCurrent(*actual) || !stereoDevice || p.channels.size() != 1 ||
            p.channels.front().instrument || p.channels.front().audioClip || p.channels.front().mixer <= 0 ||
            !p.audioPlacements.empty() || !tracktion::EditNodeBuilder::usesDefaultLastStageNode())
            return {};
        for (const auto &mixer : p.mixer)
            for (const auto &route : mixer.routes)
                if (route.target != 0)
                    return {}; // Sends/sidechains/general routing need their own consumer contract.
        return Ptr(new Policy(actual));
    }
    struct Inventory
    {
        std::size_t samples = 0, outputStages = 0, trackMidi = 0;
        const tracktion::Edit *edit = nullptr;
        const tracktion::ProcessState *scope = nullptr;
        const PreparedPluginTransport *plan = nullptr;
    };
    Inventory inventoryControl() const
    {
        control();
        Inventory out;
        out.edit = edit;
        out.scope = scope;
        out.plan = plan.get();
        out.trackMidi = tracks.size();
        for (const auto &[id, owner] : owners)
        {
            (void)id;
            if (owner.binding.role == Binding::Role::sampler)
                ++out.samples;
            if (owner.binding.role == Binding::Role::masterGate)
                ++out.outputStages;
        }
        return out; // Installed factory inventory; real root receipt/native close are separate.
    }
    bool read(Receipt &out) const noexcept
    {
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            const auto version = publication.load(std::memory_order_seq_cst);
            if (version & 1)
                continue;
            std::array<std::uint64_t, 8> copy{};
            for (std::size_t i = 0; i < copy.size(); ++i)
                copy[i] = fields[i].load(std::memory_order_seq_cst);
            if (version != publication.load(std::memory_order_seq_cst))
                continue;
            if (!copy[0] || !copy[1])
                return false; // Preparation is not actual DSP completion.
            out = {copy[0],
                   copy[1],
                   reinterpret_cast<const void *>(std::uintptr_t(copy[2])),
                   std::int64_t(copy[3]),
                   std::int64_t(copy[4]),
                   unsigned(copy[5]),
                   bool(copy[6]) ||
                       faults->observation() != experimental::NativeMidiFaultPublisher::Fault::none,
                   unsigned(copy[7])};
            return true;
        }
        return false;
    }
    // Returns false on an unsupported FIRST native baseline BEFORE changing
    // params. A previously installed policy cannot fall back to mixed histories.
    bool install(tracktion::Edit &candidate, tracktion::CreateNodeParams &params, Probe probe)
    {
        control();
        const auto refuse = [&]()
        {
            if (edit)
                throw std::invalid_argument(
                    "ordinaryPorts.unsupported: Optional candidate preserves incumbent");
            return false;
        };
        if (!probe || !physicalCurrent(*plan) || candidate.initialSourceGeneration ||
            candidate.closedNativeIO || !candidate.requireSourceBurstSession || params.forRendering ||
            !params.includePlugins || !params.includeMasterPlugins || params.allowedClips ||
            params.allowedTracks || params.preparePluginFinalOutput || params.prepareNativeTrackClipOutput ||
            params.prepareWholeGraphOutput || params.omitAccountedNoopMidiObserver ||
            params.boundedNativeMidiIngress || params.insertOptionalLastStageNodeForDevice ||
            !tracktion::EditNodeBuilder::usesDefaultLastStageNode() ||
            !candidate.getRackList().getTypes().isEmpty() || params.sampleRate != plan->sampleRate() ||
            params.blockSize != plan->budget().quantum ||
            (edit && (&candidate != edit || scope != &params.processState)))
            return refuse();
        std::map<juce::uint64, Owner> first;
        std::map<juce::uint64, std::shared_ptr<Terminal>> firstTracks;
        std::map<juce::uint64, std::vector<juce::uint64>> stageEffects;
        std::set<juce::uint64> found;
        auto resolved = std::make_shared<std::map<const tracktion::PluginList *, tracktion::Plugin::Array>>();
        const auto add = [&](const tracktion::PluginList &list)
        {
            resolved->emplace(&list, params.pluginListResolver ? params.pluginListResolver(list)
                                                               : list.getPlugins());
        };
        add(candidate.getMasterPluginList());
        for (auto *track : tracktion::getAudioTracks(candidate))
        {
            if (track->hasAnyLiveInputs() || track->isFrozen(tracktion::AudioTrack::individualFreeze) ||
                (!track->getOutput().usesDefaultAudioOut() && !track->getOutput().getDestinationTrack()) ||
                (track->getModifierList() && !track->getModifierList()->getModifiers().isEmpty()))
                return refuse();
            for (auto *slot : track->getClipSlotList().getClipSlots())
                if (slot->getClip())
                    return refuse();
            for (auto *clip : track->getClips())
            {
                auto *midi = dynamic_cast<tracktion::MidiClip *>(clip);
                if (!midi || !midi->getSequence().isEmpty() || !clip->getListeners().isEmpty() ||
                    (clip->getPluginList() && clip->getPluginList()->size()))
                    return refuse();
            }
            add(track->pluginList);
            if (track->state.hasProperty("viberaMixerIndex"))
            {
                if (int(track->state["viberaMixerIndex"]) <= 0)
                    return refuse();
                const auto &chain = resolved->at(&track->pluginList);
                if (chain.isEmpty() || !chain.getLast() ||
                    probe(*chain.getLast()).role != Binding::Role::masterGate)
                    return refuse();
                unsigned gates = 0, gains = 0, returns = 0;
                std::vector<juce::uint64> effects;
                for (auto *plugin : chain)
                {
                    if (!plugin)
                        return refuse();
                    const auto role = probe(*plugin).role;
                    if (role == Binding::Role::masterGate)
                        ++gates;
                    else if (role == Binding::Role::mixerGain)
                        ++gains;
                    else if (role == Binding::Role::auxReturn)
                        ++returns;
                    else if (role == Binding::Role::effect)
                        effects.push_back(plugin->itemID.getRawID());
                    else if (role != Binding::Role::effect && role != Binding::Role::disabledBalance)
                        return refuse();
                }
                if (gates != 1 || gains != 1 || returns != 1)
                    return refuse();
                stageEffects.emplace(chain.getLast()->itemID.getRawID(), std::move(effects));
                const auto id = track->itemID.getRawID();
                if (edit && !tracks.contains(id))
                    return refuse();
                firstTracks.emplace(id, edit ? tracks.at(id) : std::make_shared<Terminal>());
            }
        }
        for (const auto &[list, plugins] : *resolved)
            for (auto *plugin : plugins)
            {
                (void)list;
                if (!plugin || &plugin->edit != &candidate)
                    return refuse();
                const auto id = plugin->itemID.getRawID();
                const auto b = probe(*plugin);
                if (!id || !found.insert(id).second || b.role == Binding::Role::unknown ||
                    plugin->isFrozen() ||
                    (b.role != Binding::Role::disabledBalance && !plugin->isEnabled()) ||
                    b.role == Binding::Role::send || b.role == Binding::Role::sidechain)
                    return refuse();
                const bool wrapped = b.role == Binding::Role::sampler || b.role == Binding::Role::masterGate;
                if (wrapped)
                {
                    if (!b.runtime)
                        return refuse();
                    const auto old = owners.find(id);
                    if (edit && (old == owners.end() || old->second.binding.runtime != b.runtime ||
                                 old->second.binding.role != b.role))
                        return refuse();
                    first.emplace(id, edit ? old->second : Owner{b});
                }
            }
        const auto sources = std::count_if(first.begin(), first.end(), [](const auto &owner)
                                           { return owner.second.binding.role == Binding::Role::sampler; });
        if (sources != 1 || firstTracks.empty() ||
            (edit && (first.size() != owners.size() || firstTracks.size() != tracks.size())))
            return refuse();
        if (nextGeneration == std::numeric_limits<std::uint64_t>::max())
            return refuse();
        const void *sourceRuntime = nullptr;
        for (const auto &[id, owner] : first)
        {
            (void)id;
            if (owner.binding.role == Binding::Role::sampler)
                sourceRuntime = owner.binding.runtime.get();
        }
        std::vector<StageObservation> observations;
        std::map<juce::uint64, std::size_t> effectObservation;
        std::set<const OutputProgress *> uniqueProgress;
        for (const auto &[list, plugins] : *resolved)
            for (auto *plugin : plugins)
            {
                (void)list;
                if (typeid(*plugin) != typeid(RemoteEffect)) continue;
                auto &effect = static_cast<RemoteEffect &>(*plugin);
                auto progress = effect.outputProgressControl();
                auto join = effect.freshJoinProgressControl();
                if (join && (!progress || join->outputControl() != progress))
                    return refuse();
                if (join && join->observation().phase != JoinProgress::Phase::joined &&
                    join != activeFreshJoinProgress)
                    return refuse(); // No unleased first-use path around exact old root/stage binding.
                if (!progress) continue;
                if (!uniqueProgress.insert(progress.get()).second) return refuse();
                const auto &actual = progress->bindingControl(); // Immutable CONTROL metadata only.
                tracktion::graph::FinishedForwardOutputIdentity identity;
                if (actual.processingRuntime == effect.runtimeOwnerControl().get() &&
                    actual.dryHistory && actual.hostWriter && actual.pcm.host &&
                    actual.pcm.preparedTransport == reinterpret_cast<std::uintptr_t>(plan.get()) &&
                    actual.pcm.quantum == plan->budget().quantum &&
                    actual.native.valid(actual.pcm.quantum) && actual.maximumSpan >= params.blockSize &&
                    std::int64_t(actual.pcm.transportSamples) + actual.native.nativeSamples == actual.latency &&
                    actual.latency == effect.preparedGraphLatencySamples())
                {
                    identity.sourceRuntime = sourceRuntime;
                    identity.processingRuntime = actual.processingRuntime;
                    identity.preparedPlan = plan.get();
                    identity.instance = actual.pcm.instance;
                    identity.declarationGeneration = actual.native.generation;
                    identity.nativeLatency = actual.native.nativeSamples;
                    identity.producingLatency = actual.latency;
                }
                // Even an unrelated wet observer must surround actual calls.
                // Invalid/stale evidence cannot authorize a native continuation.
                effectObservation.emplace(plugin->itemID.getRawID(), observations.size());
                observations.push_back({nullptr, std::move(progress), identity, std::move(join)});
            }
        bool forwardBound = !activeWetProgress, removalBound = !activeRemoval, joinBound = !activeFreshJoin;
        std::map<juce::uint64, std::size_t> stageObservation;
        for (const auto &[id, effects] : stageEffects)
        {
            if (effects.size() != 1 || !effectObservation.contains(effects.front())) continue;
            const auto index = effectObservation.at(effects.front());
            auto &actual = observations[index];
            const auto owner = first.find(id);
            if (owner == first.end()) return refuse();
            actual.stage = owner->second.binding.runtime.get();
            actual.identity.stageRuntime = actual.stage;
            stageObservation.emplace(id, index);
            if (actual.stage == activeForward && activeWetProgress)
            {
                if (actual.progress != activeWetProgress || !actual.identity.valid() || actual.progress->isRemoval())
                    return refuse();
                forwardBound = true;
            }
            if (actual.stage == activeRemoval)
            {
                if (actual.progress != activeRemovalProgress || !actual.identity.valid() || !actual.progress->isRemoval())
                    return refuse();
                removalBound = true;
            }
            if (actual.stage == activeFreshJoin)
            {
                if (actual.join != activeFreshJoinProgress || !actual.identity.valid() ||
                    actual.progress->isRemoval())
                    return refuse();
                joinBound = true;
            }
        }
        if (!forwardBound || !removalBound || !joinBound)
            return refuse();
        if (!edit)
        {
            owners = std::move(first);
            tracks = std::move(firstTracks);
            edit = &candidate;
            scope = &params.processState;
        }
        const auto self = shared_from_this();
        auto build = std::make_shared<Build>(self, ++nextGeneration, std::move(observations), activeRemoval,
                                             activeFreshJoin);
        params.pluginListResolver = [resolved](const tracktion::PluginList &list)
        { return resolved->at(&list); };
        params.prepareNativeTrackClipOutput =
            [self](tracktion::AudioTrack &track,
                   std::unique_ptr<tracktion::graph::Node> native) -> std::unique_ptr<tracktion::graph::Node>
        {
            if (!track.state.hasProperty("viberaMixerIndex"))
                return native;
            return std::make_unique<TrackBoundary>(std::move(native),
                                                   self->tracks.at(track.itemID.getRawID()),
                                                   std::size_t(track.itemID.getRawID()), self->faults);
        };
        params.preparePluginFinalOutput = [self, probe, build, stageObservation, forward = activeForward,
                                           progress = activeForwardProgress](tracktion::Plugin &plugin)
            -> std::optional<tracktion::CreateNodeParams::PluginFinalOutputPlan>
        {
            const auto actual = probe(plugin);
            if (actual.role != Binding::Role::sampler && actual.role != Binding::Role::masterGate)
                return {};
            const auto &owner = self->owners.at(plugin.itemID.getRawID());
            if (actual.role != owner.binding.role || actual.runtime != owner.binding.runtime)
                throw std::invalid_argument(
                    "ordinaryPorts.ownerChanged: Actual source/stage Runtime changed");
            tracktion::CreateNodeParams::PluginFinalOutputPlan result;
            result.moveAudioCompensationStage = plugin.getPreparedAudioOutputStageIdentity() != nullptr;
            std::shared_ptr<OutputProgress> observed;
            tracktion::graph::FinishedForwardOutputIdentity identity;
            if (const auto stage = stageObservation.find(plugin.itemID.getRawID()); stage != stageObservation.end())
            {
                observed = build->observations[stage->second].progress;
                identity = build->observations[stage->second].identity;
            }
            result.wrap =
                [self, p = tracktion::Plugin::Ptr(&plugin), owner, forward, progress, observed, identity](
                    std::unique_ptr<tracktion::graph::Node> output) -> std::unique_ptr<tracktion::graph::Node>
            {
                std::unique_ptr<tracktion::graph::Node> port = std::make_unique<AudioPort>(
                    std::shared_ptr<tracktion::graph::Node>(std::move(output)), p, owner, self, observed, identity);
                if (forward && p->getPreparedAudioOutputStageIdentity() == forward)
                    return std::make_unique<ForwardStageDrainNode>(std::move(port), forward, progress);
                return port;
            };
            return result;
        };
        params.prepareWholeGraphOutput =
            [build](std::unique_ptr<tracktion::graph::Node> root) -> std::unique_ptr<tracktion::graph::Node>
        {
            if (!root)
                throw std::invalid_argument("ordinaryPorts.root: Missing actual native output");
            const auto p = root->getNodeProperties();
            if (p.hasMidi || p.numberOfChannels != 2)
            {
                if (build->policy->rootInstalled)
                    throw std::invalid_argument(
                        "ordinaryPorts.root: Optional candidate changed output/MIDI contract");
                build->policy->faults->publish(experimental::NativeMidiFaultPublisher::Fault::terminal);
                return root; // Keep the actual unsupported root/MIDI native; never report ready.
            }
            auto wrapped = std::make_unique<Root>(std::move(root), build);
            build->policy->rootInstalled = true;
            return wrapped;
        };
        return true;
    }
};
} // namespace vibera::ordinary_sample_ports
