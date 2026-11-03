// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <tracktion_graph/tracktion_graph.h>

namespace graph_publication_detail
{
class Constant final : public tracktion::graph::Node
{
    const float value;
    const int declaredLatency;

  public:
    explicit Constant(float v, int latency) : value(v), declaredLatency(latency) {}
    tracktion::graph::NodeProperties getNodeProperties() override
    {
        return {true, false, 1, declaredLatency, 0x7669627261707562ULL};
    }
    std::vector<Node *> getDirectInputNodes() override
    {
        return {};
    }
    bool isReadyToProcess() override
    {
        return true;
    }
    void process(ProcessContext &context) override
    {
        for (choc::buffer::FrameCount n = 0; n < context.buffers.audio.getNumFrames(); ++n)
            context.buffers.audio.getSample(0, n) = value;
    }
};
} // namespace graph_publication_detail

// Each attempt owns a fresh real engine player. Inject at successive C++ new
// sites on CONTROL only, not at guessed implementation-specific allocation sizes.
// Old exact PCM and a subsequent successful replacement are independent oracles.
// Declared numbers exercise metadata caching only, not native delay DSP.
static void graphPublicationTests(const juce::File &folder)
{
    require(folder.createDirectory().wasOk(), "Owned graph-publication evidence");
    juce::String report = "Original constant graph, actual lock-free player, control allocation failures. No "
                          "hardware/listening.\n";
    unsigned exceptions = 0, swallowed = 0, attempts = 0;
    bool passed = true, exhaustedSites = false;
    for (int failureIndex = 0; failureIndex < 512; ++failureIndex)
    {
        ++attempts;
        tracktion::graph::LockFreeMultiThreadedNodePlayer player;
        player.setNumThreads(0);
        player.setNode(std::make_unique<graph_publication_detail::Constant>(.125f, 11), 48000., 128);
        auto *old = player.getNode();
        choc::buffer::ChannelArrayBuffer<float> output(choc::buffer::Size::create(1, 128));
        tracktion::engine::MidiMessageArray midi;
        std::int64_t reference = 0;
        const auto process = [&](float expected)
        {
            output.clear();
            midi.clear();
            player.process({128, {reference, reference + 128}, {output.getView(), midi}});
            reference += 128;
            for (choc::buffer::FrameCount n = 0; n < 128; ++n)
                if (!std::isfinite(output.getView().getSample(0, n)) ||
                    output.getView().getSample(0, n) != expected)
                    return false;
            return true;
        };
        require(process(.125f), "Actual old graph produces independent exact PCM");
        auto replacement = std::make_unique<graph_publication_detail::Constant>(.75f, 37);
        preparationAllocationFailureHit = false;
        preparationAllocationFailureAfter = failureIndex;
        bool threw = false;
        try
        {
            player.setNode(std::move(replacement), 44100., 256);
        }
        catch (const std::bad_alloc &)
        {
            threw = true;
        }
        catch (...)
        {
            preparationAllocationFailureAfter = -1;
            throw;
        }
        const bool reached = preparationAllocationFailureHit;
        preparationAllocationFailureAfter = -1; // Before assertions, cleanup or evidence I/O.
        const bool oldCacheRetained = !threw || player.getNode() == old;
        // Never dereference a cache proven invalid by this oracle.
        const bool pcm = oldCacheRetained && process(threw ? .125f : .75f);
        const bool latency = oldCacheRetained && player.getPreparedLatencySamples() == (threw ? 11 : 37);
        const bool configuration = player.getSampleRate() == (threw ? 48000. : 44100.) &&
                                   player.getBlockSize() == (threw ? 128 : 256);
        bool next = false;
        if (oldCacheRetained && pcm && latency && configuration)
        {
            player.setNode(std::make_unique<graph_publication_detail::Constant>(.5f, 19), 48000., 128);
            next = process(.5f) && player.getPreparedLatencySamples() == 19;
        }
        exceptions += threw;
        swallowed += reached && !threw;
        report += "site=" + juce::String(failureIndex) + " injected=" + juce::String(int(reached)) +
                  " badAlloc=" + juce::String(int(threw)) +
                  " oldCache=" + juce::String(int(oldCacheRetained)) + " exactPCM=" + juce::String(int(pcm)) +
                  " cachedLatency=" + juce::String(int(latency)) +
                  " acceptedConfiguration=" + juce::String(int(configuration)) +
                  " subsequentReplacement=" + juce::String(int(next)) + "\n";
        passed = passed && oldCacheRetained && pcm && latency && configuration && next;
        if (!passed || !reached)
        {
            exhaustedSites = !reached;
            break;
        }
    }
    report += "attempts=" + juce::String(attempts) + " allocationFailures=" + juce::String(exceptions) +
              " supportedFallbacks=" + juce::String(swallowed) +
              " exhaustedSites=" + juce::String(int(exhaustedSites)) + "\n";
    require(folder.getChildFile("graph-publication.txt").replaceWithText(report),
            "Retain actual allocation-failure outcomes before assertion");
    require(passed && exhaustedSites && exceptions >= 5,
            "Allocation failures preserve active graph/caches/PCM and allow subsequent publication");
}
