// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "ForwardStageDrainAcceptance.h"
#include "StoppedEffectNativeAcceptance.h"
#include "../src/InverseStageResidual.h"
#include <array>
#include <memory>
#include <atomic>
#include <exception>
#include <cstdint>

// Actual ORIGINAL finite-delay SDK + native graph. Full-Q256, steady unity,
// no MIDI/automation/sidechain/post-controls. Never arbitrary plugin-tail proof.
namespace stopped_effect_inverse_sdk_acceptance
{
using namespace tracktion::graph;
using Fixture = forward_stage_drain_acceptance::Fixture;
using Producer = forward_stage_drain_acceptance::Producer;
using Owner = forward_stage_drain_acceptance::Owner;
using Player = LockFreeMultiThreadedNodePlayer;
struct LastCapture
{
    bool hasCapture = false, residual = false;
    vibera::PluginPcmReceiptStatus status = vibera::PluginPcmReceiptStatus::empty;
    std::size_t count = 0, required = 0;
    vibera::PluginPcmIdentity identity;
    vibera::PluginPcmSegment first;
    FiniteInverseResidualStatus provider = FiniteInverseResidualStatus::unsupported;
    juce::int64 start = 0, end = 0;
};
struct Runtime
{
    std::shared_ptr<vibera::PluginHost> host; // CONTROL reset only AFTER finished+old-holder retired.
    const std::shared_ptr<Owner> source;
    std::atomic<bool> healthy{true};
    // Sole DSP owner fields; never copied/read by CONTROL preparation.
    const Node* writer = nullptr;
    juce::int64 end = 0;
    bool clockKnown = false;
    unsigned normalCalls = 0, zeroCalls = 0;
    LastCapture last; // Sole DSP writer, fixture CONTROL reads only between serialized callbacks.
    Runtime(std::shared_ptr<vibera::PluginHost> h, std::shared_ptr<Owner> s)
        : host(std::move(h)), source(std::move(s)) {}
    std::optional<FiniteInverseResidualIdentity> identity(const Node* producer) const noexcept
    {
        // Immutable fixture declaration, NOT inferred from name/latency. Only
        // this original source-defined pure-delay adapter implements it.
        if (!host || !healthy.load(std::memory_order_acquire) || host->processingQuantum() != 256 ||
            host->transportLatencySamples() != 256 || host->declaredLatency().nativeSamples != 31)
            return {};
        return FiniteInverseResidualIdentity{producer, this, source.get(), 1, 287, 2, 256, 48000.};
    }
};
class Dry final : public Node, public InverseLogicalSource
{
    const std::shared_ptr<Owner> source;
    unsigned &calls;
    const std::size_t id;
  public:
    Dry(std::shared_ptr<Owner> s, unsigned &n, std::size_t i) : source(std::move(s)), calls(n), id(i) {}
    const void* inverseLogicalSourceIdentity() const noexcept override { return source.get(); }
    NodeProperties getNodeProperties() override { return {true, false, 2, 0, id, source.get(), this}; }
    bool isReadyToProcess() override { return true; }
    void process(ProcessContext &pc) override
    {
        ++calls;
        for (choc::buffer::ChannelCount c = 0; c < 2; ++c)
            for (choc::buffer::FrameCount n = 0; n < pc.buffers.audio.getNumFrames(); ++n)
                pc.buffers.audio.getSample(c, n) = forward_stage_drain_acceptance::mainSample(
                    int(c), pc.referenceSampleRange.getStart() + juce::int64(n));
    }
};
class Sdk final : public Node, public FiniteInverseResidualProvider
{
    const std::shared_ptr<Runtime> runtime;
    const Node* const prior; // Null means original normal producer; otherwise residual-only.
    unsigned &sourceCalls;
    const std::size_t id;
    juce::AudioBuffer<float> audio{2, 256};
    std::array<vibera::PluginPcmSegment, 1> spans{};
    vibera::PluginPcmCapture capture{std::span<vibera::PluginPcmSegment>{spans}};
    int remaining = 287;
    void record(bool processed) noexcept
    {
        auto& out = runtime->last;
        out.hasCapture = processed; out.residual = prior != nullptr;
        out.provider = status; out.start = outputRange.getStart(); out.end = outputRange.getEnd();
        if (processed)
        {
            out.status = capture.status; out.count = capture.count; out.required = capture.required;
            out.identity = capture.identity;
            if (capture.count) out.first = capture.storage[0];
        }
    }
    FiniteInverseResidualStatus status = FiniteInverseResidualStatus::unsupported;
    juce::Range<juce::int64> outputRange;
  public:
    Sdk(std::shared_ptr<Runtime> r, const Node* original, unsigned &calls, std::size_t lineage)
        : runtime(std::move(r)), prior(original), sourceCalls(calls), id(lineage) {}
    NodeProperties getNodeProperties() override
    { return {true, false, 2, 287, id, prior ? nullptr : runtime->source.get(), prior ? nullptr : this}; }
    bool isReadyToProcess() override { return true; }
    std::optional<FiniteInverseResidualIdentity> finiteInverseResidualIdentity() const noexcept override
    { return runtime->identity(prior ? prior : this); }
    FiniteInverseResidualStatus finiteInverseResidualOutput(juce::Range<int64_t> range, int count) const noexcept override
    { return prior && range == outputRange && count == 256 ? status : FiniteInverseResidualStatus::unsupported; }
    void process(ProcessContext &pc) override
    {
        outputRange = pc.referenceSampleRange;
        if (prior && remaining == 0) return; // NO Host/shared_ptr access after completion/reclamation.
        if (pc.buffers.audio.getNumChannels() != 2 || pc.buffers.audio.getNumFrames() != 256 ||
            pc.referenceSampleRange.getLength() != 256)
        { status = FiniteInverseResidualStatus::shape; record(false); return; }
        if (prior && (!runtime->clockKnown || runtime->end != pc.referenceSampleRange.getStart() ||
            runtime->writer != (remaining == 287 ? prior : this) || runtime->host->packetFrame() != 0))
        { status = FiniteInverseResidualStatus::clock; runtime->healthy.store(false); record(false); return; }
        if (!prior) ++sourceCalls;
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < 256; ++n)
                audio.setSample(c, n, prior ? 0.f : forward_stage_drain_acceptance::mainSample(
                    c, pc.referenceSampleRange.getStart() + n));
        const auto callIndex = runtime->normalCalls + runtime->zeroCalls;
        runtime->host->process(audio, 0, 256, nullptr, 0, pc.referenceSampleRange.getStart() / 48000.,
            120, true, false, 4, 4, nullptr, 0, 0, true, nullptr, 0, 1, &capture);
        const bool wet = capture.status == vibera::PluginPcmReceiptStatus::complete && capture.count == 1 &&
            capture.identity.quantum == 256 && capture.identity.transportSamples == 256 &&
            capture.storage[0].offset == 0 && capture.storage[0].count == 256 &&
            capture.storage[0].packetFrame == 0 && capture.storage[0].outputPacket == callIndex &&
            !capture.storage[0].forcedDry && capture.storage[0].mix == vibera::PluginPcmMixKind::wet &&
            (callIndex == 0
                 ? capture.storage[0].packet == vibera::PluginPcmPacketKind::startup && !capture.storage[0].consumed
                 : capture.storage[0].packet == vibera::PluginPcmPacketKind::completed &&
                   capture.storage[0].consumed && capture.storage[0].consumed->sequence == callIndex - 1 &&
                   capture.storage[0].consumed->stamp.nativeSamples == 31);
        if (!wet || (prior && capture.storage[0].packet != vibera::PluginPcmPacketKind::completed))
        { status = FiniteInverseResidualStatus::worker; runtime->healthy.store(false); record(true); return; }
        if (prior) { ++runtime->zeroCalls; remaining -= std::min(remaining, 256); }
        else ++runtime->normalCalls;
        runtime->writer = this;
        runtime->end = pc.referenceSampleRange.getEnd();
        runtime->clockKnown = true;
        status = FiniteInverseResidualStatus::ready;
        record(true);
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < 256; ++n)
                pc.buffers.audio.getSample(choc::buffer::ChannelCount(c), choc::buffer::FrameCount(n)) = audio.getSample(c, n);
    }
};
// CONTROL-only witness, retained after a failed candidate is destroyed. Numeric
// identities are diagnostics only: never dereference a retired candidate token.
struct MutationWitness
{
    unsigned attempted = 0, completed = 0;
    std::uintptr_t queue = 0, processor = 0;
    bool reprepare = false, capabilityPresent = false;
};
// CONTROL-only original fault injector; never shipped by the app. The real
// public processor APIs mutate the actual candidate, without clearing MIDI.
inline void foreignMutation(LatencyProcessor& processor, bool reprepare)
{
    if (reprepare) processor.prepareToPlay(48000., 256, 2);
    else
    {
        tracktion::MidiMessageArray input;
        input.addMidiMessage(juce::MidiMessage::noteOn(1, 60, juce::uint8(100)), 0);
        processor.writeMIDI(input);
    }
}
class MutateDuringPrepare final : public Node
{
    std::unique_ptr<Node> input;
    LatencyNode& actualQueue;
    const bool reprepare;
    MutationWitness& witness;
    vibera::InverseStageResidualNode* const& adapter;
public:
    MutateDuringPrepare(std::unique_ptr<Node> node, LatencyNode& queue, bool reset,
                        MutationWitness& record, vibera::InverseStageResidualNode* const& shell)
        : input(std::move(node)), actualQueue(queue), reprepare(reset), witness(record), adapter(shell) {}
    NodeProperties getNodeProperties() override
    {
        auto p = input->getNodeProperties();
        if (p.nodeID) tracktion::hash_combine(p.nodeID, std::size_t(0x4d55544154454f4eULL));
        return p;
    }
    std::vector<Node*> getDirectInputNodes() override { return {input.get()}; }
    bool isReadyToProcess() override { return input->hasProcessed(); }
    void prepareToPlay(const PlaybackInitialisationInfo&) override
    {
        ++witness.attempted;
        witness.queue = reinterpret_cast<std::uintptr_t>(&actualQueue);
        witness.reprepare = reprepare;
        witness.capabilityPresent = adapter && adapter->preparedInverseOutputResidual();
        auto* queue = const_cast<LatencyProcessor*>(static_cast<const LatencyProcessor*>(actualQueue.structuralZeroHistoryRuntimeToken()));
        witness.processor = reinterpret_cast<std::uintptr_t>(queue);
        require(queue != nullptr, "Mutation requires the actual initialized private queue");
        foreignMutation(*queue, reprepare);
        ++witness.completed; // Only after the real public API returns.
    }
    void process(ProcessContext& pc) override
    {
        const auto value = input->getProcessedOutput();
        for (choc::buffer::ChannelCount c = 0; c < pc.buffers.audio.getNumChannels(); ++c)
            for (choc::buffer::FrameCount n = 0; n < pc.buffers.audio.getNumFrames(); ++n)
                pc.buffers.audio.getSample(c, n) = value.audio.getSample(c, n);
        pc.buffers.midi.copyFrom(value.midi);
    }
};
// mode0 positive,1/2 mutation before mint,3/4 between mint/final seal,
//5/6 after seal before post (first DSP must report failed callback).
inline void scenario(const juce::File &folder, const juce::File &binary, const PluginSpec &spec,
                     const juce::String &scannedBinaryHash, int mode)
{
    require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh inverse SDK evidence");
    require(juce::SHA256(binary).toHexString() == scannedBinaryHash,
            "Scanned original fixture bytes must remain exact before every fresh worker");
    require(!spec.instrument && spec.name == "VibeRa Mixer Latency Fixture", "Explicit original SDK fixture input");
    // CONTROL-only phase costs. Keep all original callbacks, evidence bytes,
    // worker lifetime assertions and the aggregate deadline unchanged while
    // separating slow evidence/dispatch/teardown from real DSP timing.
    struct ControlTiming
    {
        const juce::File folder;
        const double began = juce::Time::getMillisecondCounterHiRes();
        const TestDispatchStatistics dispatchBefore = testDispatchStatistics();
        double retainMs = 0, hostResetMs = 0, scopeEnded = 0;
        unsigned retains = 0;
        ~ControlTiming()
        {
            const auto ended = juce::Time::getMillisecondCounterHiRes();
            const auto after = testDispatchStatistics();
            const juce::String text =
                "totalMs=" + juce::String(ended - began, 3) + "\nretainCalls=" + juce::String(retains) +
                "\nretainMs=" + juce::String(retainMs, 3) + "\nhostResetMs=" + juce::String(hostResetMs, 3) +
                "\nscopeTeardownMs=" + juce::String(scopeEnded ? ended - scopeEnded : 0, 3) +
                "\ndispatchCalls=" + juce::String(juce::int64(after.calls - dispatchBefore.calls)) +
                "\ndispatchMs=" + juce::String(after.totalMs - dispatchBefore.totalMs, 3) +
                "\ndispatchWaitMs=" + juce::String(after.waitMs - dispatchBefore.waitMs, 3) +
                "\ndispatchMessageMs=" + juce::String(after.messageMs - dispatchBefore.messageMs, 3) + "\n";
            folder.getChildFile("control-timings.txt").replaceWithText(text);
        }
    } timing{folder}; // Declared first: observes graph/worker destruction too.
    Fixture f(287);
    juce::String report = "ORIGINAL finite31 SDK inverse, fixed full-Q256/steady unity/no MIDI.\n";
    for (const auto &file : {juce::File::getSpecialLocation(juce::File::currentExecutableFile),
                             vibera::pluginWorkerExecutable(), binary})
        report += file.getFullPathName() + " sha256=" + juce::SHA256(file).toHexString() + "\n";
    const auto retain = [&](bool durable = false)
    {
        const auto began = juce::Time::getMillisecondCounterHiRes();
        f.retain(folder, durable);
        const auto file = folder.getChildFile("sdk-inverse.txt");
        const auto bytes = report.getNumBytesAsUTF8();
        juce::FileOutputStream out(file, 0);
        // Report/PCM/CSV snapshots only grow. Avoid JUCE truncate() here: on
        // Windows it physically flushes even an unbuffered stream. Refuse an
        // unexpected shorter snapshot instead of retaining stale suffix bytes.
        require(out.openedOk() && out.setPosition(0) &&
                    (durable ? out.truncate().wasOk() : file.getSize() <= juce::int64(bytes)) &&
                    out.write(report.toRawUTF8(), bytes),
                "Retain inverse receipts");
        if (durable)
            out.flush();
        require(out.getStatus().wasOk() && out.getPosition() == juce::int64(bytes) &&
                    file.getSize() == juce::int64(bytes),
                "Write exact inverse receipts; terminal evidence flush must succeed");
        timing.retainMs += juce::Time::getMillisecondCounterHiRes() - began;
        ++timing.retains;
    };
    auto host = std::make_shared<vibera::PluginHost>();
    const auto startupBegan = juce::Time::getMillisecondCounterHiRes();
    const auto startup = host->start(spec, 48000, 256, false);
    report += "startupMs=" + juce::String(juce::Time::getMillisecondCounterHiRes() - startupBegan, 3) +
        ",startup=" + startup.getErrorMessage() + ",alive=" + juce::String(int(host->alive())) +
        ",native=" + juce::String(host->declaredLatency().nativeSamples) +
        ",transport=" + juce::String(host->transportLatencySamples()) +
        ",submitted=" + juce::String(juce::int64(host->statistics().submitted)) + "\n";
    retain();
    require(startup.wasOk() && host->alive() && host->latencySamples() == 287 &&
                host->declaredLatency().nativeSamples == 31 && host->statistics().submitted == 0,
            "Actual original SDK startup ready, no source/native DSP prewarming");
    vibera::test::stopped_effects::ProcessObject process(*host);
    const auto pid = host->processId();
    const auto instance = host->runtimeIdentity();
    const std::weak_ptr<vibera::PluginHost> weak = host;
    auto runtime = std::make_shared<Runtime>(std::move(host), f.owners[0]);
    const auto releaseHost = [&]
    {
        const auto began = juce::Time::getMillisecondCounterHiRes();
        runtime->host.reset(); // CONTROL only; unchanged normal owned RAII shutdown.
        timing.hostResetMs += juce::Time::getMillisecondCounterHiRes() - began;
    };
    Sdk* original = nullptr;
    vibera::InverseStageResidualNode* currentBridge = nullptr;
    LatencyNode* currentDryQueue = nullptr;
    int preparationMutation = 0;
    MutationWitness mutation;
    const auto mutationSummary = [&]
    {
        return "mutationAttempted=" + juce::String(mutation.attempted) +
            ",mutationCompleted=" + juce::String(mutation.completed) +
            ",queue=" + juce::String::toHexString(juce::int64(mutation.queue)) +
            ",processor=" + juce::String::toHexString(juce::int64(mutation.processor)) +
            ",reprepare=" + juce::String(int(mutation.reprepare)) +
            ",capabilityPresentAtInjection=" + juce::String(int(mutation.capabilityPresent)) + "\n";
    };
    const auto graph = [&](bool wet, bool inverse, vibera::InverseStageResidualNode *&adapter)
    {
        adapter = nullptr;
        const auto base = std::size_t(0x56494e560000ULL + f.generation++ * 16);
        std::unique_ptr<Node> target;
        if (wet)
        {
            auto producer = std::make_unique<Sdk>(runtime, nullptr, f.calls[0], base);
            original = producer.get(); target = std::move(producer);
        }
        else
        {
            target = std::make_unique<Dry>(f.owners[0], f.calls[0], base);
            if (inverse)
            {
                auto queue = std::make_unique<LatencyNode>(std::move(target), 287);
                currentDryQueue = queue.get();
                std::unique_ptr<Node> dry = std::move(queue);
                if (preparationMutation == 1 || preparationMutation == 2)
                    dry = std::make_unique<MutateDuringPrepare>(std::move(dry), *currentDryQueue, preparationMutation == 2, mutation, currentBridge);
                auto residual = std::make_unique<Sdk>(runtime, original, f.calls[0], base + 3);
                auto wrapper = std::make_unique<vibera::InverseStageResidualNode>(std::move(dry), std::move(residual), f.owners[0].get());
                adapter = wrapper.get(); currentBridge = adapter; target = std::move(wrapper);
                if (preparationMutation == 3 || preparationMutation == 4)
                    target = std::make_unique<MutateDuringPrepare>(std::move(target), *currentDryQueue, preparationMutation == 4, mutation, currentBridge);
            }
        }
        std::vector<std::unique_ptr<Node>> nodes;
        nodes.push_back(std::move(target));
        nodes.push_back(std::make_unique<Producer>(f.owners[1], 1, 0, 2, false, base + 1, f.calls[1]));
        nodes.push_back(std::make_unique<Producer>(nullptr, 2, 287, 2, false, base + 2, f.calls[2]));
        return std::make_unique<SummingNode>(std::move(nodes));
    };
    const auto observations = [&](const char* phase)
    {
        const auto selected = f.player.graphSelection();
        const auto& last = runtime->last;
        report += juce::String(phase) + ",end=" + juce::String(f.position) + ",normal=" +
            juce::String(runtime->normalCalls) + ",zero=" + juce::String(runtime->zeroCalls) +
            ",healthy=" + juce::String(int(runtime->healthy.load())) +
            ",selected=" + juce::String(juce::int64(selected.selectedSerial)) +
            ",posted=" + juce::String(juce::int64(selected.postedSerial)) +
            ",ref=" + juce::String(selected.referenceStart) + ":" + juce::String(selected.referenceEnd) +
            ",provider=" + juce::String(int(last.provider)) + ",providerRef=" + juce::String(last.start) + ":" + juce::String(last.end) +
            ",residual=" + juce::String(int(last.residual)) + ",capturePresent=" + juce::String(int(last.hasCapture));
        if (last.hasCapture)
        {
            report += ",captureStatus=" + juce::String(int(last.status)) + ",count=" + juce::String(juce::int64(last.count)) +
                ",required=" + juce::String(juce::int64(last.required)) + ",Q=" + juce::String(last.identity.quantum) +
                ",transport=" + juce::String(last.identity.transportSamples) +
                ",captureHost=" + juce::String::toHexString(juce::int64(last.identity.host)) +
                ",captureTransportToken=" + juce::String::toHexString(juce::int64(last.identity.preparedTransport)) + ",captureInstance=";
            for (auto byte : last.identity.instance) report += juce::String::toHexString(int(byte)).paddedLeft('0', 2);
            if (last.count)
                report += ",kind=" + juce::String(int(last.first.packet)) + ",packet=" + juce::String(juce::int64(last.first.outputPacket)) +
                    ",offset=" + juce::String(last.first.offset) + ",frames=" + juce::String(last.first.count) +
                    ",packetFrame=" + juce::String(last.first.packetFrame) + ",forcedDry=" + juce::String(int(last.first.forcedDry)) +
                    ",mix=" + juce::String(int(last.first.mix)) + ",dryDelay=" + juce::String(last.first.dryDelaySamples) +
                    ",consumed=" + (last.first.consumed ? juce::String(juce::int64(last.first.consumed->sequence)) : "absent") +
                    ",native=" + (last.first.consumed ? juce::String(last.first.consumed->stamp.nativeSamples) : "absent") +
                    ",nativeGeneration=" + (last.first.consumed ? juce::String(last.first.consumed->stamp.generation) : "absent") +
                    ",wet=" + juce::String(last.first.firstWet, 9) + ":" + juce::String(last.first.lastWet, 9) +
                    ",bypass=" + juce::String(last.first.firstBypass, 9) + ":" + juce::String(last.first.lastBypass, 9);
        }
        if (runtime->host)
        {
            const auto stats = runtime->host->statistics();
            report += ",pid=" + juce::String(runtime->host->processId()) + ",instance=" + runtime->host->runtimeIdentity() +
                ",alive=" + juce::String(int(runtime->host->alive())) + ",submitted=" + juce::String(juce::int64(stats.submitted)) +
                ",missed=" + juce::String(juce::int64(stats.missed)) + ",dropped=" + juce::String(juce::int64(stats.dropped));
        }
        if (currentBridge && currentBridge->preparedInverseOutputResidual())
        {
            const auto state = currentBridge->preparedInverseOutputResidual()->observation();
            report += ",bridge=" + juce::String(int(state.state)) + ",remaining=" + juce::String(state.remaining);
        }
        report += "\n";
        retain(); // Always BEFORE any combined assertion using these observations.
    };
    const auto drive = [&](const char *phase, bool expectedFault = false)
    {
        const bool exact = f.process(256); // ORIGINAL unchanged literal t-287 sparse PCM/counters/deadline/heap.
        report += "actualOriginalPcmOracle=" + juce::String(int(exact)) + "\n";
        observations(phase);
        const auto selected = f.player.graphSelection();
        bool failedCallbackPcm = true;
        if (expectedFault)
            for (int c = 0; c < 2; ++c)
                for (int n = 0; n < 256; ++n)
                    failedCallbackPcm &= f.output.getView().getSample(choc::buffer::ChannelCount(c), choc::buffer::FrameCount(n)) ==
                        forward_stage_drain_acceptance::unaffectedSample(c, f.position - 256 + n - 287);
        require(selected.referenceStart == f.position - 256 && selected.referenceEnd == f.position &&
                    runtime->healthy.load() && (expectedFault ? !exact && f.clean && failedCallbackPcm : exact),
                "Every original oracle retained; intentional failed-callback negative is explicit");
        if (runtime->host)
        {
            const auto stats = runtime->host->statistics();
            require(runtime->host->alive() && runtime->host->processId() == pid &&
                        runtime->host->runtimeIdentity() == instance && stats.missed == 0 && stats.dropped == 0 &&
                        stats.submitted == runtime->normalCalls + runtime->zeroCalls,
                    "Same actual worker, exactly one old-tail quantum/no restart/miss/drop");
        }
        dispatchMessages(6); // CONTROL only, no processing wait/prewarming.
    };
    struct ScopeEnd
    {
        ControlTiming &timing;
        ~ScopeEnd()
        {
            timing.scopeEnded = juce::Time::getMillisecondCounterHiRes();
        }
    } scopeEnd{timing};
    try
    {
        vibera::InverseStageResidualNode *adapter = nullptr;
        f.player.setNode(graph(true, false, adapter), 48000., 256); // Setup before FIRST callback only.
        for (int n = 0; n < 8; ++n) drive("cold_actual_wet_startup_and_old_source");
        if (mode >= 1 && mode <= 4)
        {
            preparationMutation = mode;
            const auto callsBefore = f.calls;
            const auto submittedBefore = runtime->host->statistics().submitted;
            const auto selectedBefore = f.player.graphSelection();
            bool taintRefused = false;
            try { auto invalid = f.player.prepareFixedGraph(graph(false, true, adapter)); }
            catch (const LatencyNode::OutputStageError& e)
            {
                report += "actualRefusal=" + juce::String(e.what()) + "\n";
                // A generic configuration/scope refusal is NOT taint coverage.
                // The native guard must emit this exact structured diagnostic
                // only at its actual unknownCreationMutation check.
                taintRefused = e.status == LatencyNode::OutputStageStatus::configuration &&
                    juce::String(e.what()).startsWith("routing.pdcInverseTaintedNewDry:");
            }
            report += mutationSummary();
            currentBridge = nullptr; // Failed ready graph destroyed; never read its pointer.
            observations("actual_foreign_mutation_prepare_refused");
            require(taintRefused && mutation.attempted == 1 && mutation.completed == 1 &&
                        mutation.queue == reinterpret_cast<std::uintptr_t>(currentDryQueue) && mutation.processor != 0 &&
                        mutation.reprepare == (mode == 2 || mode == 4) && mutation.capabilityPresent == (mode >= 3) &&
                        f.calls == callsBefore && runtime->zeroCalls == 0 &&
                        runtime->host->statistics().submitted == submittedBefore &&
                        f.player.graphSelection().selectedSerial == selectedBefore.selectedSerial &&
                        f.player.graphSelection().postedSerial == selectedBefore.postedSerial &&
                        f.player.graphSelection().referenceStart == selectedBefore.referenceStart &&
                        f.player.graphSelection().referenceEnd == selectedBefore.referenceEnd,
                    "Real public MIDI/reprepare refuses at mint/seal without incumbent mutation");
            drive("foreign_prepare_refusal_old_graph_full_PCM");
            report += "PASS real candidate mutation refusal; incumbent all original oracles unchanged.\n";
            retain(true);
            return;
        }
        bool refused = false;
        try { auto invalid = f.player.prepareFixedGraph(graph(false, false, adapter)); }
        catch (const LatencyNode::OutputStageError &e)
        {
            report += "noPlanRefusal=" + juce::String(e.what()) + "\n";
            refused = e.status == LatencyNode::OutputStageStatus::configuration;
        }
        observations("no_plan_prepare_outcome");
        require(refused, "Existing no-plan inverse remains refused");
        drive("no_plan_refusal_incumbent");
        {
            const auto zero = runtime->zeroCalls;
            auto discarded = f.player.prepareFixedGraph(graph(false, true, adapter));
            observations("private_inverse_prepared_admission");
            require(adapter->preparedInverseOutputResidual()->observation().state == InverseOutputResidual::State::admitted,
                    "Actual inverse prepared capability, not caller-set bypass");
            drive("private_inverse_old_effect_only");
            require(runtime->zeroCalls == zero, "Prepared residual never processed before publication");
            currentBridge = nullptr;
        }
        drive("cancelled_inverse_incumbent_exact");
        auto ready = f.player.prepareFixedGraph(graph(false, true, adapter));
        auto *proof = adapter->preparedInverseOutputResidual();
        const auto queue = proof->newDryQueueRuntimeToken();
        observations("fresh_inverse_final_seal");
        if (mode == 5 || mode == 6)
        {
            const auto selectedBefore = f.player.graphSelection();
            const auto submittedBefore = runtime->host->statistics().submitted;
            const auto callsBefore = f.calls;
            auto* actual = const_cast<LatencyProcessor*>(static_cast<const LatencyProcessor*>(queue));
            foreignMutation(*actual, mode == 6); // Real public API BEFORE post, no clearing to green.
            observations("after_seal_foreign_mutation_before_post");
            require(f.calls == callsBefore && runtime->zeroCalls == 0 &&
                        runtime->host->statistics().submitted == submittedBefore &&
                        f.player.graphSelection().selectedSerial == selectedBefore.selectedSerial &&
                        f.player.graphSelection().postedSerial == selectedBefore.postedSerial &&
                        f.player.graphSelection().referenceStart == selectedBefore.referenceStart &&
                        f.player.graphSelection().referenceEnd == selectedBefore.referenceEnd,
                    "Prepost mutation touches private queue only, no incumbent/source/worker change");
            auto retiredFault = f.player.makeGraphRetirementSlot();
            require(f.player.publishReadyGraph(ready, retiredFault) == Player::ReadyGraphStatus::ready,
                    "Post acceptance is NOT a healthy DSP completion");
            drive("first_DSP_tainted_new_dry_queue_explicit_fault", true);
            const auto state = proof->observation();
            observations("typed_first_use_mutation_refusal");
            bool otherExact = true;
            for (int c = 0; c < 2; ++c)
                for (int n = 0; n < 256; ++n)
                    otherExact &= f.output.getView().getSample(choc::buffer::ChannelCount(c), choc::buffer::FrameCount(n)) ==
                        forward_stage_drain_acceptance::unaffectedSample(c, f.position - 256 + n - 287);
            require(state.state == InverseOutputResidual::State::newDryMutation && state.remaining == 287 &&
                        f.clean && otherExact && runtime->zeroCalls == 1,
                    "First DSP fails closed before inverse output, preserves unaffected source/deadline/heap, never finished");
            require(f.player.retireReadyGraph(retiredFault), "Failed candidate old holder retired on CONTROL");
            report += "PASS explicit failed-callback negative ONLY; original full PCM oracle intentionally remains false.\n";
            retain(true);
            return;
        }
        const auto serial = ready.publicationSerial();
        auto retired = f.player.makeGraphRetirementSlot();
        bool clean = true;
        const auto post = ready_graph_acceptance::publishWithoutNewDelete(f.player, ready, retired, clean);
        report += "post=" + juce::String(int(post)) + ",noNewDelete=" + juce::String(int(clean)) + "\n";
        observations("actual_inverse_post_outcome");
        require(post == Player::ReadyGraphStatus::ready && clean, "Actual no-allocation inverse post");
        drive("selected_first_old_residual256");
        require(proof->observation().state == InverseOutputResidual::State::rendering &&
                    proof->observation().remaining == 31 && runtime->zeroCalls == 1 &&
                    f.player.graphSelection().selectedSerial == serial,
                "Actual selected leaves31 frames pending, no fake completion");
        require(f.player.retireReadyGraph(retired), "Old effect graph actually retired CONTROL; residual pins SAME Runtime");
        drive("old_residual31_then_new_dry225");
        require(proof->observation().state == InverseOutputResidual::State::finished &&
                    proof->observation().remaining == 0 && proof->newDryQueueRuntimeToken() == queue && runtime->zeroCalls == 2,
                "Exact literal287-frame inverse completion and same new dry FIFO");
        // Acquire finished synchronizes all last Host reads. Old source holder
        // is retired; future residual process returns BEFORE any host access.
        releaseHost(); // CONTROL only, never a DSP last-owner release.
        const bool ownerExpired = weak.expired(), workerExited = process.exited();
        report += "ownerExpired=" + juce::String(int(ownerExpired)) + ",ownedKernelExited=" + juce::String(int(workerExited)) + "\n";
        observations("actual_finished_worker_reclamation");
        require(ownerExpired && workerExited, "Completed exact owned worker exited on CONTROL");
        for (int n = 0; n < 4; ++n) drive("completed_dry_with_no_worker_pin_or_calls");
        const auto totalZero = runtime->zeroCalls;
        auto stable = f.player.prepareFixedGraph(graph(false, false, adapter));
        auto returned = f.player.makeGraphRetirementSlot();
        const auto stableSerial = stable.publicationSerial();
        require(f.player.publishReadyGraph(stable, returned) == Player::ReadyGraphStatus::ready,
                "Finished inverse finalizes through exact retained-queue stable branch");
        drive("finalized_dry_queue_history_retained");
        require(f.player.graphSelection().selectedSerial == stableSerial && runtime->zeroCalls == totalZero &&
                    f.player.retireReadyGraph(returned), "Finalization/reclamation on CONTROL with no source reset");
        currentBridge = nullptr;
        report += "PASS finite original SDK inverse ONLY; app/MIDI/automation/arbitrary tails/partial1Q pending.\n";
        retain(true);
    }
    catch (...)
    {
        const auto originalFailure = std::current_exception();
        report += mutationSummary(); // Also retain injection facts on unexpected exceptions.
        // This closed driver has no concurrent callbacks: its sole process
        // call has unwound. Release only our owned worker on CONTROL before
        // aggregate proceeds to another fresh scenario, using normal Host RAII.
        releaseHost();
        report += "FAILED; preserve all original samples; ownedWorkerExited=" +
                  juce::String(int(process.exited())) + "\n";
        retain(true);
        std::rethrow_exception(originalFailure);
    }
}
inline void tests(const juce::File& folder, const juce::File& binary)
{
    require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh finite inverse aggregate");
    const std::array<const char*, 7> names{"positive", "midi_before_mint", "repeat_prepare_before_mint",
        "midi_before_seal", "repeat_prepare_before_seal", "midi_before_post", "repeat_prepare_before_post"};
    juce::String summary = "Original finite SDK inverse: seven independent fresh scenarios.\n";
    // Only immutable discovery is shared. Every scenario still constructs a
    // fresh native graph/worker/state and runs all original PCM/lifetime cases.
    const auto scannedBinaryHash = juce::SHA256(binary).toHexString();
    const auto scanBegan = juce::Time::getMillisecondCounterHiRes();
    const auto spec = parameterSpec(binary);
    require(juce::SHA256(binary).toHexString() == scannedBinaryHash,
            "Original binary must remain exact through isolated discovery");
    summary += "isolatedScans=1,binarySha256=" + scannedBinaryHash + ",scanMs=" +
               juce::String(juce::Time::getMillisecondCounterHiRes() - scanBegan, 3) + "\n";
    juce::String firstFailure;
    unsigned passed = 0, failed = 0;
    const auto retainAggregate = [&]
    {
        juce::FileOutputStream out(folder.getChildFile("aggregate.txt"));
        require(out.openedOk() && out.setPosition(0) && out.truncate().wasOk() &&
                    out.writeText(summary, false, false, "\n"), "Retain every inverse case outcome");
        out.flush(); require(out.getStatus().wasOk(), "Flush inverse aggregate");
    };
    for (int mode = 0; mode < int(names.size()); ++mode)
    {
        const juce::String name(names[std::size_t(mode)]);
        const auto caseBegan = juce::Time::getMillisecondCounterHiRes();
        summary += "RUNNING " + name + "\n"; retainAggregate();
        try
        {
            // Exceptions unwind the complete scenario and its graph/Host owners
            // on CONTROL before another fresh worker can be constructed.
            scenario(folder.getChildFile(name), binary, spec, scannedBinaryHash, mode);
            ++passed; summary += "PASSED " + name + "\n";
        }
        catch (const std::exception& error)
        {
            const auto text = name + ": " + juce::String(error.what());
            if (firstFailure.isEmpty()) firstFailure = text;
            ++failed; summary += "FAILED " + text + "\n";
        }
        catch (...)
        {
            const auto text = name + ": non-standard C++ exception";
            if (firstFailure.isEmpty()) firstFailure = text;
            ++failed; summary += "FAILED " + text + "\n";
        }
        summary += "caseMs=" + juce::String(juce::Time::getMillisecondCounterHiRes() - caseBegan, 3) + "\n";
        retainAggregate(); // Each completed/failed case is durable; never retry.
    }
    summary += "terminal=" + juce::String(failed ? "failed" : "passed") +
               ",passed=" + juce::String(passed) + ",failed=" + juce::String(failed) + "\n";
    retainAggregate();
    require(failed == 0 && passed == names.size(),
            ("All seven independent finite SDK inverse cases must pass; first failure: " + firstFailure).toStdString());
}
} // namespace stopped_effect_inverse_sdk_acceptance
