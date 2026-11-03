// SPDX-License-Identifier: AGPL-3.0-or-later
// ORIGINAL SOURCE-ONLY DRAFT. Include after ReadyGraphAcceptance.h/probe/require.
#pragma once
#include <tracktion_engine/tracktion_engine.h>
#include <tracktion_graph/tracktion_graph.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>

namespace vibera::test::selected_graph_latency
{
using Player = tracktion::graph::LockFreeMultiThreadedNodePlayer;
using State = Player::GraphSelectionState;

// The score is independently known from reference samples. No output fitting,
// state reset, wallclock, plugin worker or guessed processing quantum.
class ReferencePulse final : public tracktion::graph::Node
{
  public:
    tracktion::graph::NodeProperties getNodeProperties() override
    {
        return {true, false, 1, 0, 0x766962726170756cULL};
    }
    bool isReadyToProcess() override
    {
        return true;
    }
    void process(ProcessContext &context) override
    {
        for (choc::buffer::FrameCount frame = 0; frame < context.buffers.audio.getNumFrames(); ++frame)
        {
            const auto absolute = context.referenceSampleRange.getStart() + std::int64_t(frame);
            context.buffers.audio.getSample(0, frame) = absolute % 128 == 0 ? .5f : 0.f;
        }
    }
};

// Actual NodePlayer + actual native LatencyNode DSP. Native recording itself is
// NOT run: the exact input correction arithmetic is checked against independently
// observed delayed pulses and reference time. Historical events need lineage.
inline void playerTests(const juce::File &folder)
{
    using ready_graph_acceptance::CountingPool;
    using ready_graph_acceptance::Counts;
    require(folder.createDirectory().wasOk(), "Owned selected-graph evidence folder");
    juce::String report =
        "Original actual player/native delay PCM and selected-holder timestamp arithmetic.\n"
        "No native MIDI/Wave recording, worker, hardware, listening or deadline qualification.\n";
    const auto output = folder.getChildFile("selected-graph-latency.txt");
    require(!output.exists(), "Selected-graph evidence must be fresh");
    bool passed = true;
    auto check = [&](bool condition, const char *name)
    {
        passed = passed && condition;
        report += juce::String(name) + "=" + juce::String(int(condition)) + "\n";
        require(output.replaceWithText(report), "Retain selected graph evidence before final assertion");
    };
    Counts counts;
    Player player([&](Player &p) { return std::make_unique<CountingPool>(p, counts); });
    player.setNumThreads(0);
    player.enablePooledMemoryAllocations(false);
    choc::buffer::ChannelArrayBuffer<float> pcm(choc::buffer::Size::create(1, 128));
    tracktion::MidiMessageArray midi;
    midi.reserve(128);
    std::int64_t reference = 0;
    auto process = [&](int expectedPulseOffset)
    {
        pcm.clear();
        midi.clear();
        const auto allocations = callbackAllocations.load(), destructions = callbackDestructions.load();
        struct Scope
        {
            bool previous = callbackAllocationProbe;
            Scope()
            {
                callbackAllocationProbe = true;
            }
            ~Scope()
            {
                callbackAllocationProbe = previous;
            }
        } scope;
        player.process({128, {reference, reference + 128}, {pcm.getView(), midi}});
        bool correct =
            callbackAllocations.load() == allocations && callbackDestructions.load() == destructions;
        for (unsigned frame = 0; frame < 128; ++frame)
            correct = correct &&
                      pcm.getView().getSample(0, frame) == (int(frame) == expectedPulseOffset ? .5f : 0.f);
        reference += 128;
        return correct;
    };
    check(player.graphSelection().state == State::none, "empty_player_explicit_none");
    player.setNode(std::make_unique<ReferencePulse>(), 48000., 128);
    auto posted = player.graphSelection();
    check(posted.state == State::awaitingFirstSelection && posted.postedSerial > 0 &&
              posted.selectedSerial == 0 && posted.latencySamples == 0,
          "initial_post_is_not_actual_selection");
    check(process(0), "actual_initial_pulse_PCM_no_observed_callback_new_delete");
    const auto first = player.graphSelection();
    check(first.state == State::selected && first.selectedSerial == first.postedSerial &&
              first.latencySamples == 0 && first.referenceStart == 0 && first.referenceEnd == 128,
          "initial_selected_holder_exact_reference_range");
    auto delayed = std::make_unique<tracktion::graph::LatencyNode>(
        std::unique_ptr<tracktion::graph::Node>(std::make_unique<ReferencePulse>()), 37);
    auto candidate = player.prepareFixedGraph(std::move(delayed));
    auto retired = player.makeGraphRetirementSlot();
    check(candidate.latencySamples() == 37 && player.graphSelection().selectedSerial == first.selectedSerial,
          "real_native_delay_prepared_privately");
    bool clean = true;
    check(ready_graph_acceptance::publishWithoutNewDelete(player, candidate, retired, clean) ==
                  Player::ReadyGraphStatus::ready &&
              clean && player.getPreparedLatencySamples() == 37,
          "posted_delay_changes_before_actual_selection_no_commit_new_delete");
    const auto pending = player.graphSelection();
    check(pending.state == State::selected && pending.postedSerial > first.postedSerial &&
              pending.selectedSerial == first.selectedSerial && pending.latencySamples == 0 &&
              pending.referenceStart == 0 && pending.referenceEnd == 128,
          "posting_new_delay_retains_old_actual_selection_receipt");
    check(process(37), "actual_native_delay_moves_reference128_pulse_to165_without_exclusions");
    const auto adopted = player.graphSelection();
    check(adopted.state == State::selected && adopted.selectedSerial == adopted.postedSerial &&
              adopted.selectedSerial == pending.postedSerial && adopted.latencySamples == 37 &&
              adopted.referenceStart == 128 && adopted.referenceEnd == 256,
          "actual_changed_graph_selection_receipt_matches_observed_DSP");
    // Native MidiInputDevice subtracts graph latency converted to milliseconds;
    // WaveInputDevice adds it to record offset. Independently observed pulse
    // reference165 must map to original score128, rather than current posted0.
    const auto observedPulseReference = adopted.referenceStart + 37;
    const double latencyMs = 1000. * double(adopted.latencySamples) / 48000.;
    const double correctedMidiReference = double(observedPulseReference) - latencyMs * 48000. / 1000.;
    const auto waveRecordingOffset = 9 + adopted.latencySamples;
    const double roundingBound = 16. * std::numeric_limits<double>::epsilon() * 165.;
    check(std::abs(correctedMidiReference - 128.) <= roundingBound && waveRecordingOffset == 46 &&
              observedPulseReference - adopted.latencySamples == 128,
          "native_input_correction_arithmetic_matches_genuine_delayed_pulse");
    check(process(37), "second_actual_delay_block_exact_PCM");
    const auto following = player.graphSelection();
    check(following.selectedSerial == adopted.selectedSerial && following.referenceStart == 256 &&
              following.referenceEnd == 384,
          "same_holder_range_advances_without_serial_change");
    auto stale = player.prepareFixedGraph(std::make_unique<ReferencePulse>());
    player.setNode(std::make_unique<ReferencePulse>(), 48000., 128);
    check(player.publishReadyGraph(stale, retired) == Player::ReadyGraphStatus::staleGeneration &&
              player.graphSelection().selectedSerial == following.selectedSerial,
          "stale_candidate_refusal_does_not_fabricate_selection");
    check(process(0), "actual_replacement_zero_latency_PCM");
    check(player.graphSelection().latencySamples == 0 &&
              player.graphSelection().selectedSerial > adopted.selectedSerial,
          "zero_latency_is_valid_selected_graph_not_none");
    player.clearNode();
    check(player.graphSelection().state == State::none, "clear_invalidates_stale_receipts");
    player.setNode(std::make_unique<ReferencePulse>(), 48000., 128);
    check(player.graphSelection().state == State::awaitingFirstSelection &&
              player.graphSelection().selectedSerial == 0,
          "post_clear_does_not_report_pre_clear_selection");
    check(process(0) && player.graphSelection().state == State::selected,
          "post_clear_first_actual_selection_restores_readback");
    require(passed, "Actual changed-latency player and input arithmetic acceptance");
}

// Optional second integration into the existing ORIGINAL same-Edit fixture.
// Caller owns setup/driver and must compare its actual captured PCM against its
// predeclared original waveform and exact additional37-sample delay, including
// the cold37 samples; no phase fitting or transition exclusion. This function
// alone does not create a provider/device, claim recording, or process two graphs.
template <class DriveAndVerifyExtraDelay, class Require>
void contextTest(tracktion::EditPlaybackContext &context,
                 std::shared_ptr<tracktion::EditPlaybackContext::GraphOverlay> overlay,
                 DriveAndVerifyExtraDelay drive, Require check)
{
    const auto source = context.getGraphSelection();
    check(source.state == State::selected && source.selectedSerial == source.postedSerial,
          "Actual incumbent graph must already be selected");
    int selected = -1;
    check(context.tryGetSelectedGraphLatencySamples(selected) && selected == source.latencySamples,
          "Actual EPC forwards typed selected latency");
    // Preserve caller's owning private resolver/admission; reject an ambiguous
    // double final-stage transform rather than silently replacing its behavior.
    check(overlay && !overlay->lastStage, "Context delay experiment needs one unwrapped native output");
    overlay->lastStage = [](tracktion::OutputDevice &, const tracktion::CreateNodeParams &,
                            std::unique_ptr<tracktion::graph::Node> root)
    { return std::make_unique<tracktion::graph::LatencyNode>(std::move(root), 37); };
    auto candidate = context.prepareGraphOnly(overlay);
    check(candidate->latencySamples() == source.latencySamples + 37,
          "Actual retained Edit candidate adds genuine native delay37");
    check(context.publishPreparedGraph(*candidate) ==
              tracktion::EditPlaybackContext::ReadyGraphChangeStatus::ready,
          "Actual retained context posts prepared changed-delay graph");
    check(context.getLatencySamples() == source.latencySamples + 37 &&
              context.tryGetSelectedGraphLatencySamples(selected) && selected == source.latencySamples,
          "Legacy native prepared mirror distinguished from selected graph");
    drive(37); // MUST actually process/capture/verify the next app callback.
    const auto target = context.getGraphSelection();
    check(target.state == State::selected && target.selectedSerial == target.postedSerial &&
              target.selectedSerial != source.selectedSerial &&
              context.tryGetSelectedGraphLatencySamples(selected) && selected == source.latencySamples + 37,
          "Actual app callback adopts changed native latency before typed correction");
}
} // namespace vibera::test::selected_graph_latency
