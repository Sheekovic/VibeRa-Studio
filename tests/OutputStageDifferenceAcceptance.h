// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <cstring>

// Include after ReadyGraphAcceptance.h and StableBusStageAcceptance.h. Actual
// native Player preparation/FIFO PCM, not a synthetic replacement for the graph.
// Exact compiled/run evidence is retained separately.
__declspec(noinline) static void outputStageDifferenceTests(const juce::File &folder)
{
    using namespace stable_bus_stage_acceptance;
    require(!folder.exists() && folder.createDirectory().wasOk(),
            "Fresh owned output-stage difference evidence");
    auto fixture = std::make_unique<Fixture>();
    auto &f = *fixture;
    bool passed = true;
    unsigned cases = 0;
    juce::String report =
        "Original native Player/FIFO diagnostic acceptance. No SDK/device/listening claim.\n";
    const auto check = [&](bool ok, const char *name)
    {
        passed &= ok;
        ++cases;
        report += juce::String(name) + "=" + juce::String(int(ok)) + "\n";
        require(folder.getChildFile("stage-differences.txt").replaceWithText(report),
                "Retain every diagnostic case before aggregate assertion");
        if (!ok)
        {
            auto out = folder.getChildFile("case-" + juce::String(cases) + "-last-actual-callback.raw")
                           .createOutputStream();
            require(bool(out) && out->setPosition(0) && out->truncate().wasOk(),
                    "Fresh owned actual PCM failure evidence");
            for (int c = 0; c < 2; ++c)
                for (int n = 0; n < f.lastFrames; ++n)
                {
                    const float value =
                        f.pcm.getView().getSample(choc::buffer::ChannelCount(c), choc::buffer::FrameCount(n));
                    require(out->write(&value, sizeof(value)), "Retain actual callback float bits");
                }
        }
    };
    check(f.process() && f.process() && f.process(),
          "unchanged_literal256_initial_silence_then_exact_PCM_all_three_queues");

    const auto refuse = [&](int nextDelay, const char *label)
    {
        auto *const oldRoot = f.player.getNode();
        const auto oldSelection = f.player.graphSelection();
        const auto oldCalls = f.calls;
        bool typed = false;
        LatencyNode::OutputStageDifference difference;
        try
        {
            auto unexpected = f.player.prepareFixedGraph(f.graph(f.activeRoute, nextDelay));
        }
        catch (const LatencyNode::OutputStageError &error)
        {
            typed = error.status == LatencyNode::OutputStageStatus::configuration &&
                    std::strcmp(error.what(), "routing.pdcStageConfiguration: Delay/rate/path change "
                                              "requires a qualified joint plan") == 0;
            difference = error.difference; // POD only: no captured FIFO handle/cursor/audio.
        }
        const auto afterSelection = f.player.graphSelection();
        bool knownOwner = false;
        for (const auto &owner : f.owners)
            knownOwner |= difference.runtimeOwner == owner.get();
        const auto &before = difference.before;
        const auto &after = difference.after;
        const bool beforeCorrect = before.present && before.channels == 2 && before.exits == 1 &&
                                   before.delayPresent && before.processorPresent &&
                                   before.processorDelayKnown && before.requestedDelaySamples == 256 &&
                                   before.processorDelaySamples == 256 && before.producerPathKnown &&
                                   before.producerPathLatencySamples == 0 && before.boundaryPathKnown &&
                                   before.boundaryPathLatencySamples == 256 && before.producerNodeId != 0 &&
                                   before.boundaryNodeId != 0;
        const bool afterCorrect =
            after.present && after.channels == 2 && after.exits == 1 &&
            after.delayPresent == (nextDelay != 0) && after.processorPresent == (nextDelay != 0) &&
            after.processorDelayKnown == (nextDelay != 0) && after.requestedDelaySamples == nextDelay &&
            (!after.processorDelayKnown || after.processorDelaySamples == nextDelay) &&
            after.producerPathKnown && after.producerPathLatencySamples == 0 && after.boundaryPathKnown &&
            after.boundaryPathLatencySamples == nextDelay && after.producerNodeId != 0 &&
            after.boundaryNodeId != 0;
        report += "receipt delay=" + juce::String(nextDelay) +
                  " known=" + juce::String(int(difference.known)) +
                  " beforeRequested=" + juce::String(before.requestedDelaySamples) +
                  " afterRequested=" + juce::String(after.requestedDelaySamples) +
                  " beforeProducer=" + juce::String(before.producerPathLatencySamples) +
                  " afterProducer=" + juce::String(after.producerPathLatencySamples) +
                  " beforeBoundary=" + juce::String(before.boundaryPathLatencySamples) +
                  " afterBoundary=" + juce::String(after.boundaryPathLatencySamples) +
                  " configMatch=" + juce::String(difference.processorConfigurationMatch) + "\n";
        check(typed && difference.known && knownOwner && beforeCorrect && afterCorrect &&
                  difference.processorConfigurationMatch == (nextDelay == 0 ? -1 : 0) &&
                  !difference.sameLegacyProcessor && f.calls == oldCalls && f.player.getNode() == oldRoot &&
                  afterSelection.postedSerial == oldSelection.postedSerial &&
                  afterSelection.selectedSerial == oldSelection.selectedSerial,
              label);
        // The refusal MUST preserve the incumbent queue, not merely its metadata.
        check(f.process(), "refusal_preserves_every_incumbent_PCM_frame_no_callback_heap_or_source_replay");
    };
    refuse(128, "changed_nonzero_delay_reports_actual_prepared_256_to128_without_publishing");
    refuse(0, "removed_delay_reports_actual_256_to_none_without_erasing_queue");
    check(f.transition(1, 73) && f.transition(0, 183),
          "compatible_success_still_retains_exact_queues_partial_PCM_and_selected_serial");
    const LatencyNode::OutputStageError legacy(LatencyNode::OutputStageStatus::shape, "original legacy text");
    check(legacy.status == LatencyNode::OutputStageStatus::shape && !legacy.difference.known &&
              std::strcmp(legacy.what(), "original legacy text") == 0,
          "legacy_two_argument_error_keeps_status_text_unknown_receipt");
    report += "cases=" + juce::String(cases) + " passed=" + juce::String(int(passed)) + "\n";
    require(folder.getChildFile("stage-differences.txt").replaceWithText(report),
            "Retain final diagnostic result before aggregate assertion");
    require(passed, "Actual native output-stage diagnostic/unchanged refusal/PCM acceptance");
}
