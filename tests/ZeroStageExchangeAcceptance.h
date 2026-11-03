// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "ReadyGraphAcceptance.h"
#include <vector>
#include <memory>
#include <cmath>
#include <exception>

// Actual native Player/FIFO experiment with an ORIGINAL deterministic source.
// No application, worker, native MIDI, physical device or public route claim.
namespace zero_stage_exchange_acceptance
{
using namespace tracktion::graph;
struct Runtime
{
    unsigned calls = 0;
    int64_t end = 0;
};
struct Continuity
{
    const Node* root = nullptr;
    uint64_t generation = 0;
    int64_t end = 0;
};
inline float signal (int channel, int64_t frame) noexcept
{
    if (frame < 0) return 0.f;
    return channel == 0 ? .125f + (frame % 251 == 11 ? .25f : 0.f)
                        : -.0625f - (frame % 257 == 23 ? .125f : 0.f);
}
class Stage;
class Source final : public Node, public PreparedSharedSourceOutput
{
    std::shared_ptr<Runtime> runtime;
    const int latency;
public:
    Source (std::shared_ptr<Runtime> owner, int samples) : runtime (std::move (owner)), latency (samples)
    { setOptimisations ({ ClearBuffers::yes, AllocateAudioBuffer::yes, ShareOutputBuffer::no }); }
    NodeProperties getNodeProperties() override { return {true, false, 2, latency, 0x5a4558535243ULL}; }
    bool isReadyToProcess() override { return true; }
    std::optional<PreparedSharedSourceIdentity> preparedSharedSourceIdentity() override
    { return PreparedSharedSourceIdentity {runtime.get(), 0x5a4558535243ULL, 1}; }
    bool provesPreparedSharedSourceBoundary (Node&) override;
    void process (ProcessContext& pc) override
    {
        ++runtime->calls;
        runtime->end = pc.referenceSampleRange.getEnd();
        for (choc::buffer::ChannelCount c = 0; c < 2; ++c)
            for (choc::buffer::FrameCount n = 0; n < pc.numSamples; ++n)
                pc.buffers.audio.getSample (c, n) = signal (int (c), pc.referenceSampleRange.getStart() + n - latency);
    }
};
class Zero final : public Node, public StructuralAudioSilence
{
    const bool declared;
    const size_t id;
public:
    explicit Zero (bool declaration, size_t identity = 0x5a45585a45524fULL) : declared (declaration), id (identity)
    { setOptimisations ({ ClearBuffers::yes, AllocateAudioBuffer::yes, ShareOutputBuffer::no }); }
    Kind structuralAudioSilenceKind() const noexcept override
    { return declared ? Kind::zeroLeaf : Kind::unknown; }
    NodeProperties getNodeProperties() override { return {true, false, 2, 0, id}; }
    bool isReadyToProcess() override { return true; }
    void process (ProcessContext&) override {} // Actual Node clearing supplies zero.
};
class Stage final : public Node, public StructuralAudioSilence
{
    std::unique_ptr<Node> input;
    const std::shared_ptr<int> owner;
    const size_t stableID;
    const float gain;
public:
    Stage (std::unique_ptr<Node> source, std::shared_ptr<int> identity, size_t id, float level)
        : input (std::move (source)), owner (std::move (identity)), stableID (id), gain (level)
    { setOptimisations ({ ClearBuffers::yes, AllocateAudioBuffer::yes, ShareOutputBuffer::no }); }
    Kind structuralAudioSilenceKind() const noexcept override { return Kind::zeroPreservingInputs; }
    Node* actualInput() const noexcept { return input.get(); }
    NodeProperties getNodeProperties() override
    {
        auto result = input->getNodeProperties();
        result.nodeID = stableID; result.audioCompensationStage = owner.get();
        result.audioCompensationStageOrigin = this; return result;
    }
    std::vector<Node*> getDirectInputNodes() override { return {input.get()}; }
    bool isReadyToProcess() override { return input->hasProcessed(); }
    void process (ProcessContext& pc) override
    {
        const auto data = input->getProcessedOutput();
        for (choc::buffer::ChannelCount c = 0; c < 2; ++c)
            for (choc::buffer::FrameCount n = 0; n < pc.numSamples; ++n)
                pc.buffers.audio.getSample (c, n) = data.audio.getSample (c, n) * gain;
    }
};
inline bool Source::provesPreparedSharedSourceBoundary (Node& boundary)
{
    // This independently authored fixture has exactly one homogeneous finite
    // stage around this exact source. No generic DAG/multiplicity exemption.
    const auto* stage = dynamic_cast<Stage*> (&boundary);
    return stage && stage->actualInput() == this;
}
class Root final : public Node, public ZeroStageExchangeRoot
{
    std::unique_ptr<Node> input;
    const std::shared_ptr<Continuity> continuity;
    const uint64_t generation;
    const bool request;
    const void* const addingOwner;
    const void* const removingOwner;
    std::unique_ptr<ZeroStageExchange> proof;
    bool safe = true;
public:
    Root (std::unique_ptr<Node> source, std::shared_ptr<Continuity> scope, uint64_t gen,
          bool exchange, const void* add, const void* remove)
        : input (std::move (source)), continuity (std::move (scope)), generation (gen),
          request (exchange), addingOwner (add), removingOwner (remove)
    { setOptimisations ({ ClearBuffers::yes, AllocateAudioBuffer::yes, ShareOutputBuffer::no }); }
    uint64_t zeroStageExchangeGeneration() const noexcept override { return generation; }
    const void* zeroStageExchangeContinuityIdentity() const noexcept override { return continuity.get(); }
    ZeroStageExchange* preparedZeroStageExchange() const noexcept override { return proof.get(); }
    NodeProperties getNodeProperties() override
    { auto p = input->getNodeProperties(); p.nodeID = 0x5a4558524f4f54ULL; return p; }
    std::vector<Node*> getDirectInputNodes() override { return {input.get()}; }
    bool isReadyToProcess() override { return input->hasProcessed(); }
    void prepareToPlay (const PlaybackInitialisationInfo& info) override
    { if (request) proof = LatencyNode::prepareZeroStageExchange (*this, info, addingOwner, removingOwner); }
    void prefetchBlock (juce::Range<int64_t> range) override
    {
        if (proof) safe = proof->begin (*this, continuity.get(), continuity->root,
            continuity->generation, continuity->end, range) == ZeroStageExchange::State::active;
    }
    void process (ProcessContext& pc) override
    {
        if (!safe) return; // Actual fresh cleared output + visible failed proof.
        copy (pc.buffers.audio, input->getProcessedOutput().audio);
        continuity->root = this; continuity->generation = generation;
        continuity->end = pc.referenceSampleRange.getEnd(); // Actual successful DSP only.
    }
};
enum class Case { positive, defaultRefuses, unknownOld, unknownNew, wrongSource, wrongScope, extraInternal, taintAfterSeal };
struct Fixture
{
    ready_graph_acceptance::Counts pool;
    const std::shared_ptr<int> a = std::make_shared<int>(1), b = std::make_shared<int>(2);
    const std::shared_ptr<Runtime> runtime = std::make_shared<Runtime>();
    const std::shared_ptr<Continuity> continuity = std::make_shared<Continuity>();
    const juce::File evidenceFolder;
    LockFreeMultiThreadedNodePlayer player;
    Root* root = nullptr;
    const int latency = 287;
    int64_t reference = 0;
    choc::buffer::ChannelArrayBuffer<float> output {choc::buffer::Size::create (2, 256)};
    tracktion::MidiMessageArray midi;
    std::vector<float> actual, expected;
    juce::String records = "phase,start,frames,sourceCalls,selection,error,new,delete,seconds,proof\n";
    Fixture (bool declaredOld, juce::File folder)
        : evidenceFolder(std::move(folder)),
          player ([this](auto& p) { return std::make_unique<ready_graph_acceptance::CountingPool>(p, pool); })
    {
        player.setNumThreads (0);
        auto node = graph (false, declaredOld, true, false, false, false, 1);
        root = node.get(); player.setNode (std::move (node), 48000., 256);
        midi.reserve (128); actual.reserve (32768); expected.reserve (32768);
    }
    std::unique_ptr<Root> graph (bool moved, bool oldDeclared, bool newDeclared,
                                bool exchange, bool differentSource, bool differentScope, uint64_t generation,
                                bool extraInternal = false)
    {
        auto sourceRuntime = differentSource ? std::make_shared<Runtime>() : runtime;
        std::unique_ptr<Node> first, second;
        if (!moved)
        {
            first = std::make_unique<Source>(sourceRuntime, latency);
            second = std::make_unique<Zero>(oldDeclared);
        }
        else
        {
            first = std::make_unique<Zero>(newDeclared);
            second = std::make_unique<Source>(sourceRuntime, latency);
        }
        std::vector<std::unique_ptr<Node>> inputs;
        inputs.push_back (std::make_unique<Stage>(std::move (first), a, 0x5a455841ULL, .25f));
        inputs.push_back (std::make_unique<Stage>(std::move (second), b, 0x5a455842ULL, .5f));
        if (extraInternal)
        {
            std::unique_ptr<Node> zero = std::make_unique<Zero>(true, 0x5a45584558545241ULL);
            inputs.push_back(std::make_unique<LatencyNode>(std::move(zero),1));
        }
        std::unique_ptr<Node> sum = std::make_unique<SummingNode>(std::move (inputs));
        return std::make_unique<Root>(std::move (sum), differentScope ? std::make_shared<Continuity>() : continuity,
            generation, exchange, a.get(), b.get());
    }
    void drive (const char* phase, int frames, float gain, bool failedSafe = false)
    {
        output.clear(); midi.clear(); const auto calls = runtime->calls;
        const auto allocations = callbackAllocations.load(), destructions = callbackDestructions.load();
        const auto started = juce::Time::getHighResolutionTicks();
        {
            struct Probe { bool old = callbackAllocationProbe; Probe(){ callbackAllocationProbe=true; }
                ~Probe(){ callbackAllocationProbe=old; } } probe;
            player.process ({choc::buffer::FrameCount(frames), {reference, reference + frames},
                {output.getView().getStart (choc::buffer::FrameCount(frames)), midi}});
        }
        const auto seconds = juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks()-started);
        double error = 0;
        for (int c=0;c<2;++c) for(int n=0;n<frames;++n)
        {
            const float value=output.getView().getSample(choc::buffer::ChannelCount(c),choc::buffer::FrameCount(n));
            const float wanted=failedSafe ? 0.f : signal(c,reference+n-latency)*gain;
            actual.push_back(value); expected.push_back(wanted);
            error=std::isfinite(value) ? std::max(error,std::abs(double(value)-wanted)) : INFINITY;
        }
        const auto* proof=root->preparedZeroStageExchange();
        records += juce::String(phase)+","+juce::String(reference)+","+juce::String(frames)+","+
            juce::String(runtime->calls)+","+juce::String(player.graphSelection().selectedSerial)+","+
            juce::String(error,12)+","+juce::String(callbackAllocations.load()-allocations)+","+
            juce::String(callbackDestructions.load()-destructions)+","+juce::String(seconds,9)+","+
            juce::String(proof ? int(proof->observation()) : -1)+"\n";
        reference += frames;
        retain(evidenceFolder); // CONTROL evidence before any PCM/lifetime/timing assertion.
        require(error==0 && runtime->calls==calls+1 && runtime->end==reference &&
            callbackAllocations.load()==allocations && callbackDestructions.load()==destructions &&
            seconds<=double(frames)/48000. && midi.isEmpty() && !midi.isAllNotesOff,
            "Original variable PCM, first transition sample, exact source once, empty native MIDI, heap and deadline");
    }
    void retain (const juce::File& folder)
    {
        require(folder.getChildFile("callbacks.csv").replaceWithText(records), "Retain exact callback facts");
        for(int which=0;which<2;++which)
        {
            const auto& values=which ? expected : actual;
            auto stream=folder.getChildFile(which ? "expected-interleaved-call-planar-f32.raw" : "actual-interleaved-call-planar-f32.raw").createOutputStream();
            require(stream && stream->setPosition(0) && stream->truncate().wasOk() &&
                stream->write(values.data(), values.size()*sizeof(float)), "Retain every measured sample");
            stream->flush(); require(stream->getStatus().wasOk(), "Flush PCM evidence");
        }
    }
};
inline void scenario (const juce::File& folder, Case mode)
{
    require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh native zero-exchange case");
    Fixture fixture(mode!=Case::unknownOld,folder);
    std::exception_ptr failure;
    try
    {
        for(int frames : {127,64,65,256,31,256}) fixture.drive("baseline",frames,.25f);
        std::optional<LockFreeMultiThreadedNodePlayer::ReadyGraph> stale;
        if(mode==Case::positive)
        {
            {
                auto cancelled=fixture.graph(true,true,true,true,false,false,2);
                auto held=fixture.player.prepareFixedGraph(std::move(cancelled));
                require(!held.empty(),"Real complete private exchange prepared then discarded");
            }
            fixture.drive("FIRST_after_actual_candidate_cancel",127,.25f);
            stale.emplace(fixture.player.prepareFixedGraph(fixture.graph(true,true,true,true,false,false,3)));
        }
        auto candidate=fixture.graph(true,true,mode!=Case::unknownNew,mode!=Case::defaultRefuses,
            mode==Case::wrongSource,mode==Case::wrongScope,mode==Case::positive ? 4 : 2,
            mode==Case::extraInternal);
        auto* next=candidate.get();
        bool refused=false;
        std::optional<LockFreeMultiThreadedNodePlayer::ReadyGraph> ready;
        try { ready.emplace(fixture.player.prepareFixedGraph(std::move(candidate))); }
        catch(const LatencyNode::OutputStageError& error)
        {
            refused=true;
            fixture.records += "refusal,"+juce::String(error.what())+"\n";
            const auto prefix=mode==Case::defaultRefuses ? "routing.pdcStageConfiguration:" : "routing.pdcZeroExchangeScope:";
            require(error.status==LatencyNode::OutputStageStatus::configuration &&
                    juce::String(error.what()).startsWith(prefix),
                    "Exact intended native scope/default refusal, not unrelated exception");
        }
        const bool negative=mode!=Case::positive && mode!=Case::taintAfterSeal;
        if(negative)
        {
            require(refused && !ready, "Unsupported exact inventory/source/proof refuses before post");
            fixture.drive("first_after_refusal",127,.25f);
        }
        else
        {
            require(!refused && ready && next->preparedZeroStageExchange() &&
                next->preparedZeroStageExchange()->observation()==ZeroStageExchange::State::admitted,
                "Native complete inventory sealed on actual prepared root");
            const auto added=next->preparedZeroStageExchange()->addedQueueIdentity();
            const auto removed=next->preparedZeroStageExchange()->removedQueueIdentity();
            fixture.records += "actual_queues,"+juce::String::toHexString(juce::pointer_sized_int(added))+","+
                juce::String::toHexString(juce::pointer_sized_int(removed))+"\n";
            if(mode==Case::taintAfterSeal)
                const_cast<LatencyProcessor*>(static_cast<const LatencyProcessor*>(removed))->clearAudio(0);
            auto retired=fixture.player.makeGraphRetirementSlot();
            const auto serial=ready->publicationSerial();
            require(fixture.player.publishReadyGraph(*ready,retired)==LockFreeMultiThreadedNodePlayer::ReadyGraphStatus::ready,
                "Post exact native prepared holder only");
            fixture.root=next;
            fixture.drive("FIRST_adopted",64,.5f,mode==Case::taintAfterSeal);
            require(serial && fixture.player.graphSelection().selectedSerial==serial &&
                next->preparedZeroStageExchange()->observation()==(mode==Case::taintAfterSeal ?
                ZeroStageExchange::State::taintedHistory : ZeroStageExchange::State::active),
                "Actual first DSP certificate taint failure or successful activation is explicit");
            if(mode==Case::positive)
            {
                for(int frames : {65,127,256,31,256}) fixture.drive("continued",frames,.5f);
                auto emptyRetirement=fixture.player.makeGraphRetirementSlot();
                require(stale && fixture.player.validateReadyGraph(*stale,emptyRetirement)==
                    LockFreeMultiThreadedNodePlayer::ReadyGraphStatus::staleGeneration,
                    "Actual alternate prepared holder refuses after accepted post changed native generation");
            }
            require(fixture.player.retireReadyGraph(retired), "Old graph retirement only on CONTROL");
        }
    }
    catch(...){ failure=std::current_exception(); }
    fixture.retain(folder);
    if(failure) std::rethrow_exception(failure);
}
inline void tests (const juce::File& folder)
{
    require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh native exchange aggregate");
    std::exception_ptr first; juce::String report;
    for(Case mode : {Case::positive,Case::defaultRefuses,Case::unknownOld,Case::unknownNew,
                    Case::wrongSource,Case::wrongScope,Case::extraInternal,Case::taintAfterSeal})
    {
        const auto name=juce::String(int(mode));
        try { scenario(folder.getChildFile(name),mode); report+=name+"=completed\n"; }
        catch(const std::exception& error)
        { if(!first)first=std::current_exception(); report+=name+"=failed:"+juce::String(error.what())+"\n"; }
        require(folder.getChildFile("cases.txt").replaceWithText(report), "Independent case outcomes retained");
    }
    if(first)std::rethrow_exception(first);
}
} // namespace zero_stage_exchange_acceptance
