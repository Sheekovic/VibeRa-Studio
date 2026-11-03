// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../src/ForwardStageDrain.h"
#include <array>
#include <vector>
#include <cstdint>
#include <limits>

// Include after ReadyGraphAcceptance.h and the existing C++ callback heap probe.
// Actual native Player, SummingNode, LatencyNode and FIFO, original signals.
// Serialized processing owner; no SDK/physical callback/concurrency/listening
// evidence. No application PDC gate is disabled to execute this experiment.
namespace forward_stage_drain_acceptance
{
using namespace tracktion::graph;
using Player = LockFreeMultiThreadedNodePlayer;
using Drain = ForwardOutputDrain;
struct Owner final {};
inline float mainSample(int channel, std::int64_t frame) noexcept
{
    // Exact binary fractions with independently placed positive/negative edges.
    const auto phase = frame % (channel == 0 ? 53 : 47);
    return (channel == 0 ? .125f : -.0625f) +
           (phase == (channel == 0 ? 3 : 17) ? (channel == 0 ? .25f : -.125f) : 0.f) +
           (phase == (channel == 0 ? 29 : 31) ? (channel == 0 ? -.0625f : .03125f) : 0.f);
}
inline float unaffectedSample(int channel, std::int64_t frame) noexcept
{
    return (channel == 0 ? .03125f : -.015625f) +
           (frame % (channel == 0 ? 61 : 59) == (channel == 0 ? 7 : 11)
                ? (channel == 0 ? -.015625f : .0078125f) : 0.f);
}
// A real private D-sample ring, not a falsely declared source latency. A newly
// prepared instance starts cold; the native old queue covers exactly its D
// startup frames. No mutable predecessor state is copied or processed twice.
class Producer final : public Node
{
    const std::shared_ptr<Owner> owner;
    const int kind, delay, channels;
    const bool midiDeclared;
    const std::size_t id;
    unsigned& calls;
    std::vector<float> ring;
    int cursor = 0;
public:
    Producer(std::shared_ptr<Owner> identity, int signalKind, int latency, int count,
             bool hasMidi, std::size_t lineage, unsigned& counter)
        : owner(std::move(identity)), kind(signalKind), delay(latency), channels(count),
          midiDeclared(hasMidi), id(lineage), calls(counter),
          ring(std::size_t(count) * std::size_t(std::max(1, latency)), 0.f) {}
    NodeProperties getNodeProperties() override
    {
        return {true, midiDeclared, channels, delay, id, owner.get(), owner ? this : nullptr};
    }
    bool isReadyToProcess() override { return true; }
    void process(ProcessContext& pc) override
    {
        ++calls;
        for (choc::buffer::FrameCount frame = 0; frame < pc.buffers.audio.getNumFrames(); ++frame)
        {
            const auto absolute = pc.referenceSampleRange.getStart() + std::int64_t(frame);
            for (int channel = 0; channel < channels; ++channel)
            {
                const auto value = kind == 0 ? mainSample(channel, absolute)
                                   : kind == 1 ? unaffectedSample(channel, absolute) : 0.f;
                auto& output = pc.buffers.audio.getSample(choc::buffer::ChannelCount(channel), frame);
                if (delay == 0) output = value;
                else
                {
                    auto& stored = ring[std::size_t(channel * delay + cursor)];
                    output = stored;
                    stored = value;
                }
            }
            if (delay && ++cursor == delay) cursor = 0;
        }
    }
};
struct Fixture final
{
    ready_graph_acceptance::Counts poolCounts;
    std::array<std::shared_ptr<Owner>, 2> owners{std::make_shared<Owner>(), std::make_shared<Owner>()};
    Player player;
    const int delay;
    choc::buffer::ChannelArrayBuffer<float> output{choc::buffer::Size::create(2, 256)};
    tracktion::MidiMessageArray midi;
    std::vector<float> actual = std::vector<float>(65536, 0.f), expected = std::vector<float>(65536, 0.f);
    std::vector<double> times, deadlines;
    std::vector<int> spans;
    std::array<unsigned, 3> calls{};
    unsigned generation = 1;
    std::int64_t position = 0;
    std::uint64_t firstMismatch = std::numeric_limits<std::uint64_t>::max();
    bool clean = true;
    vibera::ForwardStageDrainNode* newestBridge = nullptr;
    explicit Fixture(int latency)
        : player([this](Player& p) { return std::make_unique<ready_graph_acceptance::CountingPool>(p, poolCounts); }),
          delay(latency)
    {
        times.reserve(512); deadlines.reserve(512); spans.reserve(512); midi.reserve(128);
        player.setNumThreads(0);
        player.setNode(graph(false, false), 48000., 256);
    }
    // bad1 shape, bad2 target path, bad3 unchanged path, bad4 MIDI scope,
    // bad5 missing exact owner. These are genuine final graph differences.
    std::unique_ptr<Node> graph(bool introduceDelay, bool bridge, int bad = 0)
    {
        std::vector<std::unique_ptr<Node>> nodes;
        const auto base = std::size_t(0x564652000000ULL + generation++ * 16);
        auto target = std::make_unique<Producer>(bad == 5 ? std::make_shared<Owner>() : owners[0], 0,
            introduceDelay ? delay + (bad == 2 ? 1 : 0) : 0, bad == 1 ? 1 : 2, bad == 4, base, calls[0]);
        newestBridge = nullptr;
        if (bridge)
        {
            auto node = std::make_unique<vibera::ForwardStageDrainNode>(std::move(target), owners[0].get());
            newestBridge = node.get();
            nodes.push_back(std::move(node));
        }
        else nodes.push_back(std::move(target));
        nodes.push_back(std::make_unique<Producer>(owners[1], 1, bad == 3 ? 1 : 0, 2, false, base + 1, calls[1]));
        // Unchanged independent zero-valued actual delay branch fixes root D.
        nodes.push_back(std::make_unique<Producer>(nullptr, 2, delay, 2, false, base + 2, calls[2]));
        return std::make_unique<SummingNode>(std::move(nodes));
    }
    bool process(int frames = 256)
    {
        if (frames <= 0 || frames > 256 || 2 * (position + frames) > std::int64_t(actual.size())) return false;
        output.clear(); midi.clear();
        const auto countBefore = calls;
        const auto allocations = callbackAllocations.load(), destructions = callbackDestructions.load();
        const auto start = juce::Time::getHighResolutionTicks();
        {
            struct Probe
            {
                const bool previous = callbackAllocationProbe;
                Probe() { callbackAllocationProbe = true; }
                ~Probe() { callbackAllocationProbe = previous; }
            } probe;
            player.process({choc::buffer::FrameCount(frames), {position, position + frames},
                            {output.getView().getStart(choc::buffer::FrameCount(frames)), midi}});
        }
        const auto elapsed = juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - start);
        const double deadline = double(frames) / 48000.;
        times.push_back(elapsed); deadlines.push_back(deadline); spans.push_back(frames);
        bool valid = calls[0] == countBefore[0] + 1 && calls[1] == countBefore[1] + 1 &&
                     calls[2] == countBefore[2] + 1 && callbackAllocations.load() == allocations &&
                     callbackDestructions.load() == destructions && midi.isEmpty() && !midi.isAllNotesOff &&
                     elapsed <= deadline;
        clean &= valid;
        for (int frame = 0; frame < frames; ++frame)
            for (int channel = 0; channel < 2; ++channel)
            {
                const auto sourceFrame = position + frame - delay;
                const auto wanted = sourceFrame < 0 ? 0.f : mainSample(channel, sourceFrame) + unaffectedSample(channel, sourceFrame);
                const auto observed = output.getView().getSample(choc::buffer::ChannelCount(channel), choc::buffer::FrameCount(frame));
                const auto index = std::size_t(2 * (position + frame) + channel);
                actual[index] = observed; expected[index] = wanted;
                if (!std::isfinite(observed) || observed != wanted)
                {
                    valid = false;
                    if (firstMismatch == std::numeric_limits<std::uint64_t>::max()) firstMismatch = std::uint64_t(index);
                }
            }
        position += frames;
        return valid;
    }
    void retain(const juce::File &directory, bool durable = true) const
    {
        const auto write = [&](const juce::String &name, const std::vector<float> &samples)
        {
            // Progress snapshots bypass the userspace buffer: every sample is
            // written before assertions, including evidence of a killed test.
            // Physical disk durability is checked at each terminal scenario.
            const auto file = directory.getChildFile(name);
            const auto bytes = std::size_t(position) * 2 * sizeof(float);
            auto stream = file.createOutputStream(durable ? 0x8000 : 0);
            require(bool(stream) && stream->setPosition(0) &&
                        (durable ? stream->truncate().wasOk() : file.getSize() <= juce::int64(bytes)),
                    "Fresh full primitive PCM evidence; progress cannot shrink");
            require(stream->write(samples.data(), bytes), "Retain every sample, no excluded startup/tail");
            if (durable)
                stream->flush();
            require(stream->getStatus().wasOk() && stream->getPosition() == juce::int64(bytes) &&
                        (durable || file.getSize() == juce::int64(bytes)),
                    "Actual raw evidence bytes/flush/status must succeed");
        };
        write("actual-f32-interleaved.raw", actual); write("expected-f32-interleaved.raw", expected);
        juce::String rows = "callback,frames,seconds,deadlineSeconds\n";
        for (std::size_t index = 0; index < times.size(); ++index)
            rows += juce::String(int(index)) + "," + juce::String(spans[index]) + "," + juce::String(times[index], 9) + "," + juce::String(deadlines[index], 9) + "\n";
        if (durable)
            require(directory.getChildFile("callbacks.csv").replaceWithText(rows),
                    "Retain actual component durations/deadlines");
        else
        {
            const auto file = directory.getChildFile("callbacks.csv");
            const auto bytes = rows.getNumBytesAsUTF8();
            juce::FileOutputStream stream(file, 0);
            require(stream.openedOk() && stream.setPosition(0) && file.getSize() <= juce::int64(bytes) &&
                        stream.write(rows.toRawUTF8(), bytes) && stream.getStatus().wasOk() &&
                        stream.getPosition() == juce::int64(bytes) && file.getSize() == juce::int64(bytes),
                    "Write every callback duration/deadline before assertions");
        }
    }
};
// Independent queue-order oracle for reference-gap cases. A physical/native
// reference discontinuity is not a musical edit-time seek; no edit clock exists
// in this closed actual Player fixture. Every old/new/unaffected sample stays
// in the captured stream, including the deliberately mixed pre-gap old FIFO.
struct ClockFixture final
{
    Fixture f;
    std::vector<float> oldMain, other, freshMain;
    int oldCursor = 0, otherCursor = 0, freshCursor = 0, oldRemaining;
    bool newGraph = false;
    std::int64_t lastReferenceEnd = 0;
    Drain* proof = nullptr;
    Player::GraphRetirementSlot retired;
    explicit ClockFixture(int delay)
        : f(delay), oldMain(std::size_t(2 * delay), 0.f), other(std::size_t(2 * delay), 0.f),
          freshMain(std::size_t(2 * delay), 0.f), oldRemaining(delay) {}
    bool publish()
    {
        auto ready = f.player.prepareFixedGraph(f.graph(true, true));
        proof = f.newestBridge->preparedForwardOutputDrain();
        retired = f.player.makeGraphRetirementSlot();
        if (f.player.publishReadyGraph(ready, retired) != Player::ReadyGraphStatus::ready)
        {
            proof = nullptr; // Local failed ready ticket owns the candidate.
            return false;
        }
        newGraph = true;
        return true;
    }
    bool render(std::int64_t reference, int frames, bool expectDrain = true)
    {
        if (reference < 0 || frames < 1 || frames > 256 ||
            2 * (f.position + frames) > std::int64_t(f.actual.size())) return false;
        // Independently model literal queue order BEFORE invoking the product.
        // No observed phase, PCM fitting, FIFO inspection or returned-status
        // selection of the oracle is used. Refused drains must expose fresh
        // new-producer output and leave every old queue frame unconsumed.
        for (int frame = 0; frame < frames; ++frame)
        {
            const bool readOld = !newGraph || (expectDrain && oldRemaining > 0);
            for (int channel = 0; channel < 2; ++channel)
            {
                const auto absolute = reference + frame;
                auto& mainOld = oldMain[std::size_t(channel * f.delay + oldCursor)];
                auto& unaffected = other[std::size_t(channel * f.delay + otherCursor)];
                auto& mainNew = freshMain[std::size_t(channel * f.delay + freshCursor)];
                const auto target = readOld ? mainOld : mainNew;
                f.expected[std::size_t(2 * (f.position + frame) + channel)] = target + unaffected;
                unaffected = unaffectedSample(channel, absolute);
                if (newGraph) mainNew = mainSample(channel, absolute);
                else mainOld = mainSample(channel, absolute);
            }
            if (++otherCursor == f.delay) otherCursor = 0;
            if (readOld && ++oldCursor == f.delay) oldCursor = 0;
            if (newGraph)
            {
                if (++freshCursor == f.delay) freshCursor = 0;
                if (readOld) --oldRemaining;
            }
        }
        f.output.clear(); f.midi.clear();
        const auto before = f.calls;
        const auto allocations = callbackAllocations.load(), destructions = callbackDestructions.load();
        const auto start = juce::Time::getHighResolutionTicks();
        {
            struct Probe
            {
                const bool previous = callbackAllocationProbe;
                Probe() { callbackAllocationProbe = true; }
                ~Probe() { callbackAllocationProbe = previous; }
            } probe;
            f.player.process({choc::buffer::FrameCount(frames), {reference, reference + frames},
                              {f.output.getView().getStart(choc::buffer::FrameCount(frames)), f.midi}});
        }
        const auto elapsed = juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - start);
        const double deadline = double(frames) / 48000.;
        f.times.push_back(elapsed); f.deadlines.push_back(deadline); f.spans.push_back(frames);
        bool valid = f.calls[0] == before[0] + 1 && f.calls[1] == before[1] + 1 && f.calls[2] == before[2] + 1 &&
                     callbackAllocations.load() == allocations && callbackDestructions.load() == destructions &&
                     f.midi.isEmpty() && !f.midi.isAllNotesOff && elapsed <= deadline;
        f.clean &= valid;
        for (int frame = 0; frame < frames; ++frame)
            for (int channel = 0; channel < 2; ++channel)
            {
                const auto index = std::size_t(2 * (f.position + frame) + channel);
                const auto observed = f.output.getView().getSample(choc::buffer::ChannelCount(channel), choc::buffer::FrameCount(frame));
                f.actual[index] = observed;
                if (!std::isfinite(observed) || observed != f.expected[index])
                {
                    valid = false;
                    if (f.firstMismatch == std::numeric_limits<std::uint64_t>::max()) f.firstMismatch = std::uint64_t(index);
                }
            }
        f.position += frames;
        lastReferenceEnd = reference + frames;
        return valid;
    }
    bool warm()
    {
        bool valid = true;
        for (int callback = 0; callback < 4; ++callback) valid &= render(lastReferenceEnd, 256);
        return valid;
    }
};

inline void referenceContinuityCases(const juce::File& root, bool& passed, unsigned& count, juce::String& receipt)
{
    // Each failed capability is intentionally sticky. Fresh fixtures give the
    // positive refill test a genuine new admission, never a silent retry/unlock.
    const auto run = [&](const char* name, const auto& scenario)
    {
        auto owned = std::make_unique<ClockFixture>(287);
        auto& c = *owned;
        const auto evidence = root.getChildFile(name);
        require(evidence.createDirectory().wasOk(), "Owned original continuity case evidence");
        const bool result = scenario(c);
        ++count; passed &= result;
        receipt += juce::String(name) + "=" + juce::String(int(result)) + "\n";
        if (c.proof)
        {
            const auto observed = c.proof->observation();
            receipt += "actualDrainState=" + juce::String(int(observed.state)) + ";remaining=" + juce::String(observed.remaining) + "\n";
        }
        c.f.retain(evidence);
        require(root.getChildFile("forward-drain.txt").replaceWithText(receipt), "Retain every continuity outcome before aggregate");
    };
    run("unknown_old_reference_before_any_actual_callback", [](ClockFixture& c)
    {
        const bool posted = c.publish();
        if (!posted) return false;
        const bool pcm = c.render(0, 256, false);
        const auto observed = c.proof->observation();
        return posted && pcm && observed.state == Drain::State::unknownReference && observed.remaining == 287 && c.oldRemaining == 287;
    });
    run("cold_old_256_does_not_certify_287_initial_silence", [](ClockFixture& c)
    {
        bool valid = c.render(0, 256);
        if (!c.publish()) return false;
        valid &= c.render(256, 256, false);
        const auto observed = c.proof->observation();
        return valid && observed.state == Drain::State::incompleteHistory && observed.remaining == 287;
    });
    run("first_adoption_reference_forward_gap_refused", [](ClockFixture& c)
    {
        bool valid = c.warm();
        const auto end = c.lastReferenceEnd;
        if (!c.publish()) return false;
        valid &= c.render(end + 31, 256, false);
        const auto observed = c.proof->observation();
        return valid && observed.state == Drain::State::discontinuity && observed.remaining == 287;
    });
    run("first_adoption_reference_backward_refused", [](ClockFixture& c)
    {
        bool valid = c.warm();
        const auto end = c.lastReferenceEnd;
        if (!c.publish()) return false;
        valid &= c.render(end - 17, 256, false);
        const auto observed = c.proof->observation();
        return valid && observed.state == Drain::State::discontinuity && observed.remaining == 287;
    });
    run("prior_gap_then_old_256_leaves_31_unknown_queued_frames", [](ClockFixture& c)
    {
        bool valid = c.warm();
        valid &= c.render(c.lastReferenceEnd + 19, 256);
        if (!c.publish()) return false;
        valid &= c.render(c.lastReferenceEnd, 256, false);
        const auto observed = c.proof->observation();
        return valid && observed.state == Drain::State::incompleteHistory && observed.remaining == 287 && c.oldRemaining == 287;
    });
    run("prior_backward_then_old_256_history_also_refused", [](ClockFixture& c)
    {
        bool valid = c.warm();
        valid &= c.render(c.lastReferenceEnd - 13, 256);
        if (!c.publish()) return false;
        valid &= c.render(c.lastReferenceEnd, 256, false);
        const auto observed = c.proof->observation();
        return valid && observed.state == Drain::State::incompleteHistory && observed.remaining == 287;
    });
    run("real_gap_then_256_plus_31_old_frames_fully_refill_history", [](ClockFixture& c)
    {
        bool valid = c.warm();
        valid &= c.render(c.lastReferenceEnd + 19, 256);
        valid &= c.render(c.lastReferenceEnd, 31);
        if (!c.publish()) return false;
        valid &= c.render(c.lastReferenceEnd, 256);
        const auto partial = c.proof->observation();
        valid &= partial.state == Drain::State::draining && partial.remaining == 31;
        valid &= c.f.player.retireReadyGraph(c.retired);
        valid &= c.render(c.lastReferenceEnd, 65);
        const auto complete = c.proof->observation();
        return valid && complete.state == Drain::State::finished && complete.remaining == 0 && c.oldRemaining == 0;
    });
    run("mid_drain_reference_gap_refuses_remaining_prefix", [](ClockFixture& c)
    {
        bool valid = c.warm();
        if (!c.publish()) return false;
        valid &= c.render(c.lastReferenceEnd, 256);
        valid &= c.render(c.lastReferenceEnd + 1, 65, false);
        const auto observed = c.proof->observation();
        return valid && observed.state == Drain::State::discontinuity && observed.remaining == 31 && c.oldRemaining == 31;
    });
    run("continued_retired_native_writer_refuses_before_next_prefix", [](ClockFixture& c)
    {
        bool valid = c.warm();
        LatencyNode* oldTap = nullptr;
        for (auto* node : getNodes(*c.f.player.getNode(), VertexOrdering::postordering))
            if (auto* tap = dynamic_cast<LatencyNode*>(node); tap &&
                tap->getNodeProperties().audioCompensationStage == c.f.owners[0].get()) oldTap = tap;
        if (!oldTap) return false;
        const auto oldEnd = c.lastReferenceEnd;
        if (!c.publish()) return false;
        valid &= c.render(c.lastReferenceEnd, 256);
        // Explicit adversarial low-level native Node invocation, NOT a second
        // healthy Player graph. The retained holder pins oldTap until this call.
        // Same serialized processing thread, no concurrent unsafe writer test.
        tracktion::MidiMessageArray ignoredMidi;
        ignoredMidi.reserve(128);
        choc::buffer::ChannelArrayBuffer<float> ignoredAudio(choc::buffer::Size::create(2, 256));
        Node::ProcessContext pc{256, {oldEnd, oldEnd + 256}, {ignoredAudio.getView(), ignoredMidi}};
        const auto before = c.f.calls;
        const auto allocations = callbackAllocations.load(), destructions = callbackDestructions.load();
        {
            struct Probe
            {
                const bool previous = callbackAllocationProbe;
                Probe() { callbackAllocationProbe = true; }
                ~Probe() { callbackAllocationProbe = previous; }
            } probe;
            oldTap->process(pc);
        }
        valid &= c.f.calls == before && callbackAllocations.load() == allocations && callbackDestructions.load() == destructions;
        valid &= c.render(c.lastReferenceEnd, 65, false);
        const auto observed = c.proof->observation();
        return valid && observed.state == Drain::State::writerChanged && observed.remaining == 31 && c.oldRemaining == 31;
    });
}

} // namespace forward_stage_drain_acceptance

static void forwardStageDrainTests(const juce::File& folder)
{
    using namespace forward_stage_drain_acceptance;
    require(!folder.exists() && folder.createDirectory().wasOk(), "New owned forward stage-drain attempt");
    bool passed = true;
    unsigned cases = 0;
    juce::String receipt = "scope=actual_native_Player_serialized_original_audio_only_one_shot_D_to_zero\n"
                           "not_qualified=application_transition,inverse,SDK,physical_callback,concurrency,listening\n";
    for (const int delay : {273, 287})
    {
        auto owned = std::make_unique<Fixture>(delay);
        auto& f = *owned;
        const auto evidence = folder.getChildFile("delay-" + juce::String(delay));
        require(evidence.createDirectory().wasOk(), "Owned latency-specific evidence");
        const auto check = [&](bool result, const char* name)
        {
            ++cases; passed &= result;
            receipt += "D=" + juce::String(delay) + ";" + name + "=" + juce::String(int(result)) + "\n";
            f.retain(evidence);
            require(folder.getChildFile("forward-drain.txt").replaceWithText(receipt), "Retain each original case before aggregate");
        };
        bool warm = true;
        for (int index = 0; index < 4; ++index) warm &= f.process();
        check(warm, "all_literal_startup_and_sparse_stereo_frames");
        {
            auto candidate = f.player.prepareFixedGraph(f.graph(true, true));
            auto* proof = f.newestBridge->preparedForwardOutputDrain();
            const auto before = proof->observation();
            const bool oldContinues = f.process(127);
            const auto after = proof->observation();
            check(!candidate.empty() && before.state == Drain::State::admitted && before.remaining == unsigned(delay) &&
                  after.state == before.state && after.remaining == before.remaining && oldContinues,
                  "private_prepare_does_not_read_old_queue_or_execute_new_source");
        }
        check(f.process(129), "cancel_discard_keeps_exact_all_old_and_unaffected_queued_PCM");
        const auto refuse = [&](int bad, bool useBridge)
        {
            const auto before = f.player.graphSelection();
            auto* root = f.player.getNode();
            const auto calls = f.calls;
            bool refused = false;
            try { auto ignored = f.player.prepareFixedGraph(f.graph(true, useBridge, bad)); }
            catch (const LatencyNode::OutputStageError&) { refused = true; }
            const auto after = f.player.graphSelection();
            const bool unchanged = root == f.player.getNode() && calls == f.calls &&
                before.postedSerial == after.postedSerial && before.selectedSerial == after.selectedSerial;
            const bool pcm = f.process(37);
            return refused && unchanged && pcm;
        };
        check(refuse(0, false), "default_D_to_zero_still_refused_without_sealed_bridge");
        check(refuse(1, true), "channel_shape_refused_before_publication_and_queue_change");
        check(refuse(2, true), "changed_root_target_path_refused_before_publication");
        check(refuse(3, true), "unaffected_source_path_change_refused_before_publication");
        check(refuse(4, true), "native_MIDI_scope_explicitly_refused_not_dropped");
        check(refuse(5, true), "foreign_stage_owner_refused_not_hashed_to_identity");
        {
            auto unchanged = f.player.prepareFixedGraph(f.graph(false, false));
            auto stale = f.player.prepareFixedGraph(f.graph(true, true));
            auto returned = f.player.makeGraphRetirementSlot();
            const auto serial = unchanged.publicationSerial();
            bool valid = f.player.publishReadyGraph(unchanged, returned) == Player::ReadyGraphStatus::ready;
            valid &= f.process();
            valid &= f.player.graphSelection().selectedSerial == serial;
            valid &= f.player.retireReadyGraph(returned);
            auto unused = f.player.makeGraphRetirementSlot();
            valid &= f.player.validateReadyGraph(stale, unused) == Player::ReadyGraphStatus::staleGeneration;
            valid &= f.process(73);
            check(valid, "stale_actual_ready_ticket_cannot_drain_a_replaced_predecessor");
        }
        auto candidate = f.player.prepareFixedGraph(f.graph(true, true));
        auto* proof = f.newestBridge->preparedForwardOutputDrain();
        const auto token = proof->queueRuntimeToken();
        const auto serial = candidate.publicationSerial();
        auto returned = f.player.makeGraphRetirementSlot();
        bool commitClean = true;
        check(ready_graph_acceptance::publishWithoutNewDelete(f.player, candidate, returned, commitClean) ==
                  Player::ReadyGraphStatus::ready && commitClean && token && proof->originalDelaySamples() == delay,
              "explicit_validated_same_root_joint_forward_proof_published_without_heap");
        const bool firstPCM = f.process(256);
        const auto partial = proof->observation();
        check(firstPCM && f.player.graphSelection().selectedSerial == serial &&
              partial.state == Drain::State::draining && partial.remaining == unsigned(delay - 256),
              "actual_adoption_drains_256_and_keeps_literal_17_or_31_pending");
        check(f.player.retireReadyGraph(returned), "old_native_graph_retires_on_CONTROL_while_drain_handle_pins_remaining_queue");
        const bool crossingPCM = f.process(65);
        const auto finished = proof->observation();
        check(crossingPCM && finished.state == Drain::State::finished && finished.remaining == 0 &&
              proof->queueRuntimeToken() == token,
              "exact_273_or_287_boundary_old_prefix_then_new_real_delay_output_no_tail_gap");
        bool subsequent = true;
        for (const int frames : {1, 127, 128, 256}) subsequent &= f.process(frames);
        check(subsequent && proof->observation().state == Drain::State::finished && f.clean,
              "all_later_source_once_unaffected_queue_PCM_and_unweakened_span_deadlines");
    }
    referenceContinuityCases(folder, passed, cases, receipt);
    receipt += "terminal=" + juce::String(passed ? "completed" : "failed") + ";cases=" + juce::String(cases) + "\n";
    require(folder.getChildFile("forward-drain.txt").replaceWithText(receipt), "Original actual terminal aggregate");
    require(passed, "Forward queue-drain experiment failed; no phase fitting/oracle/deadline changes permitted");
}
