// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "ForwardStageDrainAcceptance.h"
#include <array>
#include <memory>

// Include after ParameterAcceptance.h, timing/heap helpers and ReadyGraphAcceptance.
// Actual original SDK live worker + actual native Player/forward-drain capability.
// Full-Q callbacks only; not app, physical device, MIDI or inverse qualification.
namespace stopped_effect_forward_sdk_acceptance
{
using namespace tracktion::graph;
using Fixture = forward_stage_drain_acceptance::Fixture;
using Producer = forward_stage_drain_acceptance::Producer;
using Owner = forward_stage_drain_acceptance::Owner;
using Player = LockFreeMultiThreadedNodePlayer;
class SdkProducer final : public Node
{
    const std::shared_ptr<Owner> owner;
    const std::shared_ptr<vibera::PluginHost> host;
    unsigned &sourceCalls;
    const std::size_t lineage;
    juce::AudioBuffer<float> audio{2, 256}; // CONTROL construction only.
    std::array<vibera::PluginPcmSegment, 1> spans{};
    vibera::PluginPcmCapture emitted{std::span<vibera::PluginPcmSegment>{spans}};

  public:
    unsigned workerCalls = 0; // Sole serialized processing owner, excluded reads.
    bool wrongShape = false;
    SdkProducer(std::shared_ptr<Owner> stage, std::shared_ptr<vibera::PluginHost> worker, unsigned &count,
                std::size_t id)
        : owner(std::move(stage)), host(std::move(worker)), sourceCalls(count), lineage(id)
    {
    }
    NodeProperties getNodeProperties() override
    {
        return {true, false, 2, 287, lineage, owner.get(), this};
    }
    bool isReadyToProcess() override
    {
        return true;
    }
    const vibera::PluginPcmCapture &receipt() const noexcept
    {
        return emitted;
    }
    void process(ProcessContext &pc) override
    {
        ++sourceCalls;
        if (pc.buffers.audio.getNumChannels() != 2 || pc.buffers.audio.getNumFrames() != 256 ||
            pc.referenceSampleRange.getLength() != 256)
        {
            wrongShape = true;
            return;
        } // Driver must fail; no wider policy claimed.
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < 256; ++n)
                audio.setSample(
                    c, n,
                    forward_stage_drain_acceptance::mainSample(c, pc.referenceSampleRange.getStart() + n));
        // Default1Q live NONBLOCKING host. No renderer wait, source prewarming,
        // state capture or hidden prefix submission in this processing call.
        host->process(audio, 0, 256, nullptr, 0, pc.referenceSampleRange.getStart() / 48000., 120, true,
                      false, 4, 4, nullptr, 0, 0, true, nullptr, 0, 1, &emitted);
        ++workerCalls;
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < 256; ++n)
                pc.buffers.audio.getSample(choc::buffer::ChannelCount(c), choc::buffer::FrameCount(n)) =
                    audio.getSample(c, n);
    }
};
inline void tests(const juce::File &folder, const juce::File &binary)
{
    require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh SDK forward attempt");
    const auto spec = parameterSpec(binary);
    require(!spec.instrument && spec.name == "VibeRa Mixer Latency Fixture",
            "Explicit original native31 unity-delay SDK effect only");
    juce::String report = "Native Player + original SDK live effect, Q-aligned256 only.\n"
                          "Positive cold insertion D287->0; no app/native MIDI/inverse/partial1Q claim.\n";
    for (const auto &file : {juce::File::getSpecialLocation(juce::File::currentExecutableFile),
                             vibera::pluginWorkerExecutable(), binary})
        report += file.getFullPathName() + " sha256=" + juce::SHA256(file).toHexString() + "\n";
    Fixture f(287);
    const auto retain = [&]
    {
        f.retain(folder); // Original full literal PCM/timings, checked raw flush/status.
        juce::FileOutputStream out(folder.getChildFile("sdk-forward.txt"));
        require(out.openedOk() && out.setPosition(0) && out.truncate().wasOk() &&
                    out.writeText(report, false, false, "\n"),
                "Retain SDK/transition evidence");
        out.flush();
        require(out.getStatus().wasOk(), "Flush SDK/transition evidence");
    };
    const auto startup = [&]
    {
        auto host = std::make_shared<vibera::PluginHost>();
        require(host->start(spec, 48000, 256, false).wasOk() && host->alive() &&
                    host->processingQuantum() == 256 && host->transportLatencySamples() == 256 &&
                    host->latencySamples() == 287 && host->declaredLatency().nativeSamples == 31,
                "CONTROL native startup ready; real default256 transport plus declared31");
        require(host->statistics().submitted == 0 && !host->consumedLatency(),
                "No hidden prewarming of native DSP/source before adoption");
        return host;
    };
    const auto graph = [&](const std::shared_ptr<vibera::PluginHost> &host, SdkProducer *&producer,
                           vibera::ForwardStageDrainNode *&bridge)
    {
        const auto base = std::size_t(0x565346570000ULL + f.generation++ * 16);
        auto effect = std::make_unique<SdkProducer>(f.owners[0], host, f.calls[0], base);
        producer = effect.get();
        auto output = std::make_unique<vibera::ForwardStageDrainNode>(std::move(effect), f.owners[0].get());
        bridge = output.get();
        std::vector<std::unique_ptr<Node>> nodes;
        nodes.push_back(std::move(output));
        nodes.push_back(std::make_unique<Producer>(f.owners[1], 1, 0, 2, false, base + 1, f.calls[1]));
        // Original genuinely delayed zero anchor fixes root287, unchanged scope.
        nodes.push_back(std::make_unique<Producer>(nullptr, 2, 287, 2, false, base + 2, f.calls[2]));
        return std::make_unique<SummingNode>(std::move(nodes));
    };
    const auto drive = [&](const char *phase, bool deferAssertion = false)
    {
        const bool good = f.process(256); // Existing independent exact sparse stereo oracle unchanged.
        report += juce::String(phase) + ",end=" + juce::String(f.position) +
                  ",exact=" + juce::String(int(good)) + "\n";
        retain();
        if (!deferAssertion)
            require(good, "Every actual native graph sample/counter/heap/unchanged256-frame deadline");
        dispatchMessages(6); // CONTROL pacing only; no callback wait or turnaround claim.
        return good;
    };
    try
    {
        for (int n = 0; n < 8; ++n)
            drive("old_full_startup_and_sparse_PCM");
        {
            auto cancelledHost = startup();
            const auto calls = f.calls;
            SdkProducer *producer = nullptr;
            vibera::ForwardStageDrainNode *bridge = nullptr;
            {
                auto candidate = f.player.prepareFixedGraph(graph(cancelledHost, producer, bridge));
                require(!candidate.empty() && f.calls == calls && producer->workerCalls == 0 &&
                            bridge->preparedForwardOutputDrain()->observation().state ==
                                ForwardOutputDrain::State::admitted,
                        "Private actual SDK graph admits exact existing forward proof without processing");
                drive("private_candidate_old_graph_only");
                require(producer->workerCalls == 0 && cancelledHost->statistics().submitted == 0,
                        "Unadopted SDK worker/source is never processed");
            } // Actual ready-holder/bridge destruction on CONTROL.
            cancelledHost.reset();
            drive("cancelled_candidate_incumbent_exact");
        }
        auto host = startup();
        const auto pid = host->processId();
        const auto instance = host->runtimeIdentity();
        SdkProducer *producer = nullptr;
        vibera::ForwardStageDrainNode *bridge = nullptr;
        auto candidate = f.player.prepareFixedGraph(graph(host, producer, bridge));
        auto *proof = bridge->preparedForwardOutputDrain();
        const auto queue = proof->queueRuntimeToken();
        const auto serial = candidate.publicationSerial();
        auto retired = f.player.makeGraphRetirementSlot();
        bool clean = true;
        require(ready_graph_acceptance::publishWithoutNewDelete(f.player, candidate, retired, clean) ==
                        Player::ReadyGraphStatus::ready &&
                    clean && serial && queue,
                "Exact prepared native post with no observed new/delete");
        const auto adoption = f.position; // Known before processing, no phase fit.
        for (int callback = 0; callback < 8; ++callback)
        {
            const bool correct = drive(callback == 0   ? "selected_first256_old_prefix"
                                       : callback == 1 ? "next256_old31_then_real_SDK225"
                                                       : "post_transition_real_SDK_PCM",
                                       true);
            const auto observation = proof->observation();
            const auto &capture = producer->receipt();
            const auto stats = host->statistics();
            const auto expectedConsumed = callback - 1;
            const auto selected = f.player.graphSelection();
            // Retain actual provider/selection facts BEFORE any assertion can
            // throw. An accepted submission is never completed PCM evidence.
            report += "observed,callback=" + juce::String(callback) +
                      ",workerCalls=" + juce::String(producer->workerCalls) +
                      ",submitted=" + juce::String(juce::int64(stats.submitted)) +
                      ",processed=" + juce::String(juce::int64(stats.processed)) +
                      ",missed=" + juce::String(juce::int64(stats.missed)) +
                      ",dropped=" + juce::String(juce::int64(stats.dropped)) +
                      ",alive=" + juce::String(int(host->alive())) +
                      ",deliveryFailed=" + juce::String(int(host->deliveryFailed())) +
                      ",declaredNative=" + juce::String(host->declaredLatency().nativeSamples) +
                      ",captureStatus=" + juce::String(int(capture.status)) +
                      ",segments=" + juce::String(juce::int64(capture.count)) +
                      ",selectedSerial=" + juce::String(juce::int64(selected.selectedSerial)) +
                      ",referenceStart=" + juce::String(selected.referenceStart) +
                      ",referenceEnd=" + juce::String(selected.referenceEnd) +
                      ",drainState=" + juce::String(int(observation.state)) +
                      ",remaining=" + juce::String(observation.remaining) + "\n";
            if (capture.count && !capture.storage.empty())
            {
                const auto &segment = capture.storage[0];
                report += "emitted,packet=" + juce::String(int(segment.packet)) +
                          ",mix=" + juce::String(int(segment.mix)) +
                          ",forcedDry=" + juce::String(int(segment.forcedDry)) +
                          ",offset=" + juce::String(segment.offset) +
                          ",count=" + juce::String(segment.count) +
                          ",consumed=" + juce::String(int(bool(segment.consumed)));
                if (segment.consumed)
                    report += ",sequence=" + juce::String(juce::int64(segment.consumed->sequence)) +
                              ",native=" + juce::String(segment.consumed->stamp.nativeSamples);
                report += "\n";
            }
            retain();
            require(correct, "Every actual native graph sample/counter/heap/unchanged256-frame deadline");
            require(!producer->wrongShape && producer->workerCalls == unsigned(callback + 1) &&
                        stats.submitted == unsigned(callback + 1) && stats.missed == 0 &&
                        stats.dropped == 0 && host->alive() && !host->deliveryFailed() &&
                        host->processId() == pid && host->runtimeIdentity() == instance &&
                        host->packetFrame() == 0 && host->latencySamples() == 287 &&
                        host->declaredLatency().nativeSamples == 31,
                    "Actual worker retained/processes once, no misses/drop/restart/latency substitution");
            require(capture.status == vibera::PluginPcmReceiptStatus::complete && capture.count == 1 &&
                        capture.identity.quantum == 256 && capture.identity.transportSamples == 256 &&
                        capture.storage[0].offset == 0 && capture.storage[0].count == 256 &&
                        !capture.storage[0].forcedDry &&
                        capture.storage[0].mix == vibera::PluginPcmMixKind::wet &&
                        capture.storage[0].packet == (callback == 0
                                                          ? vibera::PluginPcmPacketKind::startup
                                                          : vibera::PluginPcmPacketKind::completed) &&
                        (callback == 0
                             ? !capture.storage[0].consumed
                             : capture.storage[0].consumed &&
                                   capture.storage[0].consumed->sequence == std::uint64_t(expectedConsumed) &&
                                   capture.storage[0].consumed->stamp.nativeSamples == 31),
                    "Emitted actual wet packet provenance, never infer completed PCM from submission");
            report += "transition,serial=" + juce::String(juce::int64(serial)) +
                      ",adoption=" + juce::String(adoption) +
                      ",remaining=" + juce::String(observation.remaining) +
                      ",workerCalls=" + juce::String(producer->workerCalls) + "\n";
            retain();
            require(selected.selectedSerial == serial && selected.referenceStart == f.position - 256 &&
                        selected.referenceEnd == f.position && proof->queueRuntimeToken() == queue &&
                        observation.state == (callback == 0 ? ForwardOutputDrain::State::draining
                                                            : ForwardOutputDrain::State::finished) &&
                        observation.remaining == (callback == 0 ? 31u : 0u),
                    "Actual selected is distinct from exact287-frame audio transition finished");
            if (callback == 0)
                require(f.player.retireReadyGraph(retired),
                        "CONTROL retires old graph while bridge independently pins remaining31");
        }
        report += "PASS real SDK forward287->0 only; inverse/app/native MIDI/partial1Q remain incomplete.\n";
        retain();
    }
    catch (...)
    {
        report += "FAILED; preserve every captured sample.\n";
        retain();
        throw;
    }
}
} // namespace stopped_effect_forward_sdk_acceptance
