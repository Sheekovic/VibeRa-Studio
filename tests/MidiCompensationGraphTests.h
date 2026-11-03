// SPDX-License-Identifier: AGPL-3.0-or-later
// Original actual-player admission and bounded callback experiments; production OFF.
#pragma once
#include "RetainedMidiAcceptance.h"
#include "CallbackAllocationProbe.h"

namespace vibera::draft::bounded_midi_graph
{
using namespace retained_midi;
class LegacyOutput final : public tracktion::graph::Node
{
    std::unique_ptr<tracktion::graph::LatencyNode> tap;

  public:
    LegacyOutput(std::shared_ptr<const std::vector<Event>> script, double rate)
        : tap(std::make_unique<tracktion::graph::LatencyNode>(
              std::unique_ptr<tracktion::graph::Node>(std::make_unique<Script>(std::move(script), rate)), 128))
    {
    }
    tracktion::graph::NodeProperties getNodeProperties() override
    {
        auto result = tap->getNodeProperties();
        result.nodeID = consumer;
        return result;
    }
    std::vector<Node *> getDirectInputNodes() override
    {
        return {tap.get()};
    }
    bool isReadyToProcess() override
    {
        return tap->hasProcessed();
    }
    void process(ProcessContext &pc) override
    {
        const auto source = tap->getProcessedOutput();
        setAudioOutput(tap.get(), source.audio);
        pc.buffers.midi.copyFrom(source.midi);
    }
};
// A genuine second bank with the same immutable edge, distinct node identity.
// This tests root-level duplicate-bank admission without relying on JUCE_DEBUG's
// duplicate Node-ID assertion (which two identical Latency taps would trigger
// before their prepareToPlay admission). It is never a valid rendering graph.
class DuplicateBankOutput final : public tracktion::graph::Node
{
    std::unique_ptr<Output> input;
    vibera::dsp::MidiCompensationHistory duplicate;

  public:
    DuplicateBankOutput(std::shared_ptr<const std::vector<Event>> script, double rate)
        : input(std::make_unique<Output>(std::move(script), rate, 128)),
          duplicate({{producer, consumer, 0}, rate, 1024, 64, {256, 16, 256, 1, 1024 * 1024}})
    {
        setAudioCompensationHistory(1024, 16384);
        setMidiCompensationHistory({256, 16, 256, 1, 1024 * 1024});
    }
    std::optional<vibera::dsp::CompensationEdge> getBoundedMidiHistoryEdge() const noexcept override
    {
        return duplicate.configuration().edge;
    }
    tracktion::graph::NodeProperties getNodeProperties() override
    {
        auto result = input->getNodeProperties();
        tracktion::hash_combine(result.nodeID, std::size_t(0x42414e4b445550ULL));
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
    void process(ProcessContext &pc) override
    {
        const auto source = input->getProcessedOutput();
        setAudioOutput(input.get(), source.audio);
        pc.buffers.midi.copyFrom(source.midi);
    }
};
inline void admissionTests(const juce::File &folder)
{
    if (folder.createDirectory().failed())
        throw std::runtime_error("Owned MIDI admission folder");
    const auto script =
        std::make_shared<const std::vector<Event>>(std::vector<Event>{{16, Kind::on}, {208, Kind::off}});
    const std::array<const char *, 7> names{"legacy-to-marked", "epoch-mismatch",      "capacity-mismatch",
                                            "removed-edge",     "duplicate-bank-edge", "rate-mismatch",
                                            "block-mismatch"};
    juce::String report = "Original actual-player candidate refusal: legacy bare latency128 has no edge "
                          "key; old NoteOn144/pendingOff336 and constant PCM must survive. No RT/native/"
                          "hardware/listening qualification.\n";
    bool passed = true;
    int attempted = 0, completed = 0;
    for (const auto rate : {48000., 44100.})
        for (int mode = 0; mode < int(names.size()); ++mode)
        {
            ++attempted;
            try
            {
                tracktion::graph::LockFreeMultiThreadedNodePlayer player;
                player.setNumThreads(0);
                std::unique_ptr<tracktion::graph::Node> initial =
                    mode == 0 ? std::unique_ptr<tracktion::graph::Node>(
                                    std::make_unique<LegacyOutput>(script, rate))
                              : std::unique_ptr<tracktion::graph::Node>(
                                    std::make_unique<Output>(script, rate, 128));
                player.setNode(std::move(initial), rate, 64);
                auto *old = player.getNode();
                choc::buffer::ChannelArrayBuffer<float> pcm(choc::buffer::Size::create(2, 64));
                tracktion::MidiMessageArray midi;
                midi.reserve(256);
                std::vector<Event> observed;
                observed.reserve(8);
                bool pcmOk = true, flagsOk = true;
                auto render = [&](std::int64_t reference)
                {
                    pcm.clear();
                    midi.clear();
                    player.process({64, {reference, reference + 64}, {pcm.getView(), midi}});
                    for (int frame = 0; frame < 64; ++frame)
                    {
                        const bool ready = reference + frame >= 128;
                        pcmOk = pcmOk && pcm.getView().getSample(0, frame) == (ready ? .125f : 0.f) &&
                                pcm.getView().getSample(1, frame) == (ready ? -.25f : 0.f);
                    }
                    flagsOk = flagsOk && !midi.isAllNotesOff;
                    for (const auto &message : midi)
                    {
                        if (!message.isNoteOn() && !message.isNoteOff())
                            throw std::runtime_error("Unexpected admission-oracle MIDI event");
                        const auto offset = message.getTimeStamp() * rate;
                        if (!std::isfinite(offset) || std::abs(offset) >= 1000000)
                            throw std::runtime_error("Invalid admission-oracle MIDI timestamp");
                        observed.push_back(
                            {reference + std::llround(offset), message.isNoteOn() ? Kind::on : Kind::off,
                             message.getNoteNumber(), message.getChannel(), message.mpeSourceID});
                    }
                };
                for (std::int64_t ref = 0; ref < 256; ref += 64)
                    render(ref);
                const bool pendingContext = observed.size() == 1 && sameMessage(observed[0], {144, Kind::on});
                const double candidateRate = mode == 5 ? (rate == 48000. ? 44100. : 48000.) : rate;
                const int candidateBlock = mode == 6 ? 128 : 64;
                std::unique_ptr<tracktion::graph::Node> candidate;
                if (mode == 3)
                    candidate = std::make_unique<Script>(script, rate);
                else if (mode == 4)
                    candidate = std::make_unique<DuplicateBankOutput>(script, rate);
                else
                {
                    auto marked = std::make_unique<Output>(script, candidateRate, 128);
                    if (mode == 1)
                        marked->setMidiCompensationHistory({256, 16, 256, 2, 1024 * 1024});
                    if (mode == 2)
                        marked->setMidiCompensationHistory({512, 16, 512, 1, 1024 * 1024});
                    candidate = std::move(marked);
                }
                bool refused = false;
                juce::String reason;
                try
                {
                    player.setNode(std::move(candidate), candidateRate, candidateBlock);
                }
                catch (const std::exception &error)
                {
                    refused = true;
                    reason = error.what();
                }
                const bool oldPreserved = player.getNode() == old;
                const bool configurationPreserved =
                    player.getSampleRate() == rate && player.getBlockSize() == 64;
                render(256);
                // Same old mode remains admissible; pendingOff336 crosses this
                // valid replacement as well as the earlier refused candidate.
                std::unique_ptr<tracktion::graph::Node> valid =
                    mode == 0 ? std::unique_ptr<tracktion::graph::Node>(
                                    std::make_unique<LegacyOutput>(script, rate))
                              : std::unique_ptr<tracktion::graph::Node>(
                                    std::make_unique<Output>(script, rate, 128));
                auto *next = valid.get();
                player.setNode(std::move(valid)); // Must retain accepted rate/block defaults after refusal.
                const bool continued = player.getNode() == next;
                for (std::int64_t ref = 320; ref < 640; ref += 64)
                    render(ref);
                const Event expectedOn{144, Kind::on}, expectedOff{336, Kind::off};
                const bool exact = observed.size() == 2 && sameMessage(observed[0], expectedOn) &&
                                   sameMessage(observed[1], expectedOff);
                // Rate changes are refused by the existing retained AUDIO bank
                // before MIDI preparation; retain that concrete independent reason.
                const bool expectedReason = mode == 5 ? reason.containsIgnoreCase("retained audio history")
                                                      : reason.containsIgnoreCase("MIDI");
                const bool casePassed = pendingContext && refused && oldPreserved && configurationPreserved &&
                                        continued && exact && pcmOk && flagsOk && expectedReason;
                passed = passed && casePassed;
                ++completed;
                report +=
                    juce::String(names[std::size_t(mode)]) + " rate=" + juce::String(rate, 0) +
                    " pass=" + juce::String(int(casePassed)) +
                    " heldOn144BeforeRefusal=" + juce::String(int(pendingContext)) +
                    " refused=" + juce::String(int(refused)) + " oldNode=" + juce::String(int(oldPreserved)) +
                    " configuration=" + juce::String(int(configurationPreserved)) +
                    " continued=" + juce::String(int(continued)) +
                    " exactOn144Off336=" + juce::String(int(exact)) + " PCM=" + juce::String(int(pcmOk)) +
                    " flags=" + juce::String(int(flagsOk)) + " reason=" + reason + "\n";
            }
            catch (const std::exception &error)
            {
                passed = false;
                report += juce::String(names[std::size_t(mode)]) + " rate=" + juce::String(rate, 0) +
                          " pass=0 failureClass=case-exception-unclassified diagnostic=" + error.what() +
                          "\n";
            }
        }
    report +=
        "attempted=" + juce::String(attempted) + " completed=" + juce::String(completed) + " expected=14\n";
    if (!folder.getChildFile("bounded-midi-admission.txt").replaceWithText(report))
        throw std::runtime_error("Retain actual MIDI admission result before assertion");
    require(passed && attempted == 14 && completed == 14,
            "Actual MIDI refusal preserves old graph/queued release/PCM and valid continuation");
}

inline void callbackTests(const juce::File &folder)
{
    if (folder.createDirectory().failed())
        throw std::runtime_error("Owned MIDI callback folder");
    const auto script = std::make_shared<const std::vector<Event>>(std::vector<Event>{
        {16, Kind::on}, {208, Kind::off}, {528, Kind::on}, {592, Kind::off}, {800, Kind::allOffFlag}});
    juce::String report = "Closed original Script/Latency/Output/player sink: declared per-source/fan-in "
                          "quota and off-RT reserve. C++ new/delete counters only, NOT CRT malloc/realloc/"
                          "third-party heap interception. No native/hardware/listening qualification.\n";
    bool passed = true;
    for (const auto rate : {48000., 44100.})
    {
        tracktion::graph::LockFreeMultiThreadedNodePlayer player;
        player.setNumThreads(0);
        player.setNode(std::make_unique<Output>(script, rate, 128), rate, 64);
        choc::buffer::ChannelArrayBuffer<float> pcm(choc::buffer::Size::create(2, 64));
        tracktion::MidiMessageArray midi;
        midi.reserve(256); // Caller-owned output is outside Node::initialise.
        std::array<Event, 16> events{};
        std::size_t eventCount = 0;
        unsigned flags = 0, overruns = 0, blocks = 0;
        bool pcmOk = true, halfOpen = true, unexpected = false;
        double maximumMs = 0;
        const auto allocations = callbackAllocations.load(), destructions = callbackDestructions.load();
        for (std::int64_t ref = 0; ref < 4096; ref += 64)
        {
            // Control-side preparation/reclamation is deliberately outside the
            // callback probe; next process call must adopt without destruction.
            if (ref == 256 || ref == 512 || ref == 768)
                player.setNode(std::make_unique<Output>(script, rate,
                                                        ref == 256   ? 256
                                                        : ref == 512 ? 0
                                                                     : 128),
                               rate, 64);
            const auto began = juce::Time::getHighResolutionTicks();
            {
                struct Probe
                {
                    const bool previous = callbackAllocationProbe;
                    Probe()
                    {
                        callbackAllocationProbe = true;
                    }
                    ~Probe()
                    {
                        callbackAllocationProbe = previous;
                    }
                } probe;
                pcm.clear();
                midi.clear();
                player.process({64, {ref, ref + 64}, {pcm.getView(), midi}});
            }
            const auto elapsed = 1000. * juce::Time::highResolutionTicksToSeconds(
                                             juce::Time::getHighResolutionTicks() - began);
            maximumMs = std::max(maximumMs, elapsed);
            overruns += elapsed >= 64000. / rate;
            ++blocks;
            const auto delay = ref < 256 ? 128 : ref < 512 ? 256 : ref < 768 ? 0 : 128;
            for (int frame = 0; frame < 64; ++frame)
            {
                const bool ready = ref + frame >= delay;
                pcmOk = pcmOk && pcm.getView().getSample(0, frame) == (ready ? .125f : 0.f) &&
                        pcm.getView().getSample(1, frame) == (ready ? -.25f : 0.f);
            }
            flags += midi.isAllNotesOff;
            for (const auto &message : midi)
            {
                const auto sample = message.getTimeStamp() * rate;
                const bool finite = std::isfinite(sample) && std::abs(sample) < 1000000;
                const auto rounded = finite ? std::llround(sample) : -1;
                halfOpen = halfOpen && std::isfinite(sample) && sample >= 0 && sample < 64 && rounded >= 0 &&
                           rounded < 64;
                if (!finite || eventCount >= events.size() || (!message.isNoteOn() && !message.isNoteOff()))
                {
                    unexpected = true;
                    continue;
                }
                events[eventCount++] = {ref + rounded, message.isNoteOn() ? Kind::on : Kind::off,
                                        message.getNoteNumber(), message.getChannel(), message.mpeSourceID};
            }
        }
        const std::array<Event, 4> expected{
            {{144, Kind::on}, {336, Kind::off}, {528, Kind::on}, {592, Kind::off}}};
        bool exact = eventCount == expected.size();
        for (std::size_t index = 0; index < std::min(eventCount, expected.size()); ++index)
            exact = exact && sameMessage(events[index], expected[index]);
        const auto allocated = callbackAllocations.load() - allocations;
        const auto destroyed = callbackDestructions.load() - destructions;
        const bool casePassed = allocated == 0 && destroyed == 0 && overruns == 0 && pcmOk && halfOpen &&
                                exact && !unexpected && flags == 1 && blocks == 64;
        passed = passed && casePassed;
        report += "rate=" + juce::String(rate, 0) + " pass=" + juce::String(int(casePassed)) +
                  " blocks=" + juce::String(blocks) + " new=" + juce::String(allocated) +
                  " delete=" + juce::String(destroyed) + " overrun=" + juce::String(overruns) +
                  " maxCallbackMs=" + juce::String(maximumMs, 6) + " PCM=" + juce::String(int(pcmOk)) +
                  " exactEvents=" + juce::String(int(exact)) + " halfOpen=" + juce::String(int(halfOpen)) +
                  " unexpected=" + juce::String(int(unexpected)) + " booleanPanic=" + juce::String(flags) +
                  "\n";
    }
    if (!folder.getChildFile("bounded-midi-callback.txt").replaceWithText(report))
        throw std::runtime_error("Retain bounded C++ allocation/callback evidence before assertion");
    require(passed,
            "Bounded original callback graph meets exact musical/C++new-delete/derived-deadline conditions");
}
} // namespace vibera::draft::bounded_midi_graph
