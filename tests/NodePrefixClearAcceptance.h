// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <tracktion_graph/tracktion_graph.h>
#include <array>
#include <cmath>
#include <limits>

// Original actual graph Player fixture. Include after require, callback heap
// probes and SyntheticCallbackTiming. No app/device/worker/provider claim.
namespace vibera::test::node_prefix_clear
{
namespace graph = tracktion::graph;
struct Counters
{
    unsigned source = 0, first = 0, second = 0, root = 0;
    bool shape = true, aliased = false;
};
inline float sparse(int channel, std::int64_t frame) noexcept
{
    // Binary fractions, independent signed L/R; additive writer deliberately
    // leaves most valid samples unwritten and therefore depends on clearing.
    const int period = channel == 0 ? 17 : 23;
    if (frame % period != 0)
        return 0;
    return (channel == 0 ? 1.f : -1.f) * float(1 + (frame / period) % 7) / 64.f;
}
class Source final : public graph::Node
{
    Counters &counters;
    int channels;

  public:
    Source(Counters &c, int n) : counters(c), channels(n) {}
    graph::NodeProperties getNodeProperties() override
    {
        return {channels != 0, false, channels, 0, 901};
    }
    std::vector<graph::Node *> getDirectInputNodes() override
    {
        return {};
    }
    bool isReadyToProcess() override
    {
        return true;
    }
    void process(ProcessContext &pc) override
    {
        ++counters.source;
        counters.shape &= pc.buffers.audio.getNumFrames() == pc.numSamples &&
                          pc.buffers.audio.getNumChannels() == unsigned(channels);
        for (int ch = 0; ch < channels; ++ch)
            for (unsigned n = 0; n < pc.numSamples; ++n)
                if (const auto sample = sparse(ch, pc.referenceSampleRange.getStart() + n); sample != 0)
                    pc.buffers.audio.getSample(unsigned(ch), n) += sample;
    }
};
class Scale final : public graph::Node
{
    graph::Node &input;
    Counters &counters;
    bool first;
    float gain;
    bool sharing = false;

  public:
    Scale(graph::Node &n, Counters &c, bool a, float g) : input(n), counters(c), first(a), gain(g) {}
    graph::NodeProperties getNodeProperties() override
    {
        auto p = input.getNodeProperties();
        p.nodeID = first ? 902 : 903;
        return p;
    }
    std::vector<graph::Node *> getDirectInputNodes() override
    {
        return {&input};
    }
    bool isReadyToProcess() override
    {
        return input.hasProcessed();
    }
    void prepareToPlay(const graph::PlaybackInitialisationInfo &info) override
    {
        if (info.enableNodeMemorySharing && input.numOutputNodes == 1 && input.canShareOutputBuffer())
        {
            sharing = true;
            setOptimisations({graph::ClearBuffers::no, graph::AllocateAudioBuffer::no});
        }
    }
    void preProcess(choc::buffer::FrameCount, juce::Range<int64_t>) override
    {
        if (sharing)
            setBufferViewToUse(&input, input.getProcessedOutput().audio);
    }
    void process(ProcessContext &pc) override
    {
        if (first)
            ++counters.first;
        else
            ++counters.second;
        const auto source = input.getProcessedOutput().audio;
        counters.shape &=
            source.getNumFrames() == pc.numSamples && pc.buffers.audio.getNumFrames() == pc.numSamples;
        if (sharing && source.getNumChannels())
            counters.aliased |= &source.getSample(0, 0) == &pc.buffers.audio.getSample(0, 0);
        for (unsigned ch = 0; ch < source.getNumChannels(); ++ch)
            for (unsigned n = 0; n < pc.numSamples; ++n)
                pc.buffers.audio.getSample(ch, n) = source.getSample(ch, n) * gain;
    }
};
class Root final : public graph::Node
{
    // Own source before dependent shells; reverse destruction removes shells
    // first. Traversal follows direct inputs and source is visited once.
    std::unique_ptr<Source> source;
    std::unique_ptr<Scale> first, second;
    Counters &counters;
    bool fanout;

  public:
    Root(Counters &c, bool f, int channels)
        : source(std::make_unique<Source>(c, channels)),
          first(std::make_unique<Scale>(*source, c, true, .5f)), counters(c), fanout(f)
    {
        if (fanout)
            second = std::make_unique<Scale>(*source, c, false, -.25f);
    }
    graph::NodeProperties getNodeProperties() override
    {
        auto p = source->getNodeProperties();
        p.nodeID = 904;
        return p;
    }
    std::vector<graph::Node *> getDirectInputNodes() override
    {
        return fanout ? std::vector<graph::Node *>{first.get(), second.get()}
                      : std::vector<graph::Node *>{first.get()};
    }
    bool isReadyToProcess() override
    {
        return first->hasProcessed() && (!fanout || second->hasProcessed());
    }
    void process(ProcessContext &pc) override
    {
        ++counters.root;
        const auto a = first->getProcessedOutput().audio;
        counters.shape &=
            a.getNumFrames() == pc.numSamples && pc.buffers.audio.getNumFrames() == pc.numSamples;
        if (fanout)
        {
            const auto b = second->getProcessedOutput().audio;
            counters.shape &= b.getNumFrames() == pc.numSamples;
            for (unsigned ch = 0; ch < a.getNumChannels(); ++ch)
                for (unsigned n = 0; n < pc.numSamples; ++n)
                    pc.buffers.audio.getSample(ch, n) += a.getSample(ch, n) + b.getSample(ch, n);
        }
        else
            for (unsigned ch = 0; ch < a.getNumChannels(); ++ch)
                for (unsigned n = 0; n < pc.numSamples; ++n)
                    pc.buffers.audio.getSample(ch, n) += a.getSample(ch, n);
    }
};

inline void run(const juce::File &folder, bool pooling, bool sharing, bool fanout, int channels)
{
    require(!folder.exists() && folder.createDirectory().wasOk(),
            "Fresh prefix-clear actual Player evidence");
    Counters counts;
    graph::LockFreeMultiThreadedNodePlayer player;
    player.setNumThreads(0);
    player.enablePooledMemoryAllocations(pooling);
    player.enableNodeMemorySharing(sharing);
    player.setNode(std::make_unique<Root>(counts, fanout, channels), 48000., 282);
    choc::buffer::ChannelArrayBuffer<float> output(2, 282);
    tracktion::MidiMessageArray midi;
    // Full first block poisons unwritten tail opportunities; shrinking then
    // growing MUST clear newly exposed valid samples, even across a gap.
    constexpr std::array<int, 16> shape{282, 1, 17, 281, 64, 2, 127, 65, 256, 1, 282, 3, 255, 1, 128, 282};
    juce::String csv = "index,frames,start,error,new,delete,ms,source,first,second,root,alias\n";
    std::int64_t reference = 0;
    bool all = true;
    for (unsigned i = 0; i < shape.size(); ++i)
    {
        if (i == 7)
            reference += 5003; // Physical reference gap, no phase fitting.
        if (i == 11)
            reference = 29; // Backward reference/seek, same clear contract.
        const int frames = shape[i];
        output.clear();
        midi.clear();
        const auto allocation = callbackAllocations.load(), destruction = callbackDestructions.load();
        const auto began = juce::Time::getHighResolutionTicks();
        {
            struct Probe
            {
                bool old = callbackAllocationProbe;
                Probe()
                {
                    callbackAllocationProbe = true;
                }
                ~Probe()
                {
                    callbackAllocationProbe = old;
                }
            } probe;
            player.process({unsigned(frames),
                            {reference, reference + frames},
                            {output.getView().getStart(unsigned(frames)), midi}});
        }
        const auto elapsed =
            SyntheticCallbackTiming::milliseconds(began, juce::Time::getHighResolutionTicks());
        double error = 0;
        for (int ch = 0; ch < 2; ++ch)
            for (int n = 0; n < frames; ++n)
            {
                const float expected =
                    ch < channels ? sparse(ch, reference + n) * (fanout ? .25f : .5f) : 0.f;
                const auto actual = output.getView().getSample(unsigned(ch), unsigned(n));
                error = std::isfinite(actual) ? std::max(error, std::abs(double(actual) - expected))
                                              : std::numeric_limits<double>::infinity();
            }
        const bool okay = error == 0 && counts.shape && counts.source == i + 1 && counts.first == i + 1 &&
                          counts.root == i + 1 && counts.second == (fanout ? i + 1 : 0) &&
                          callbackAllocations.load() == allocation &&
                          callbackDestructions.load() == destruction && midi.isEmpty() && !midi.isAllNotesOff;
        all &= okay;
        csv += juce::String(int(i)) + "," + juce::String(frames) + "," + juce::String(reference) + "," +
               juce::String(error, 12) + "," + juce::String(callbackAllocations.load() - allocation) + "," +
               juce::String(callbackDestructions.load() - destruction) + "," + juce::String(elapsed, 6) +
               "," + juce::String(int(counts.source)) + "," + juce::String(int(counts.first)) + "," +
               juce::String(int(counts.second)) + "," + juce::String(int(counts.root)) + "," +
               juce::String(int(counts.aliased)) + "\n";
        require(folder.getChildFile("callbacks.csv").replaceWithText(csv),
                "Retain prefix-clear evidence before assertions");
        require(okay, "Actual Player prefix clear PCM/shape/once/heap failure");
        reference += frames;
    }
    // Pooled allocatedView has precedence in Node::process, even if a shell
    // requested borrowing. The independent non-pooled case proves real alias.
    require(!sharing || pooling || fanout || channels == 0 || counts.aliased,
            "Supported single-consumer alias actually exercised");
    require(!fanout || !counts.aliased, "Fanout cannot overwrite its sibling input through aliasing");
    player.clearNode(); // CONTROL destruction only; no repeated graph or worker claims.
    require(all, "All actual prefix-clear spans retained and exact");
}
inline void tests(const juce::File &folder)
{
    require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh prefix-clear attempt");
    juce::String outcomes;
    unsigned failures = 0;
    const auto attempt = [&](const juce::String &name, bool pooling, bool sharing, bool fanout, int channels)
    {
        try
        {
            run(folder.getChildFile(name), pooling, sharing, fanout, channels);
            outcomes += name + "=passed\n";
        }
        catch (const std::exception &error)
        {
            ++failures;
            outcomes += name + "=failed:" + juce::String(error.what()) + "\n";
        }
        require(folder.getChildFile("outcomes.txt").replaceWithText(outcomes),
                "Retain every independent baseline/patched scenario, including failures");
    };
    for (const bool pooling : {false, true})
        for (const bool sharing : {false, true})
            for (const bool fanout : {false, true})
                attempt("pool" + juce::String(int(pooling)) + "-share" + juce::String(int(sharing)) +
                            "-fanout" + juce::String(int(fanout)),
                        pooling, sharing, fanout, 2);
    attempt("zero-audio-channels", false, false, false, 0);
    require(failures == 0, "Prefix-clear scenarios failed; every baseline/patched outcome is retained");
}
} // namespace vibera::test::node_prefix_clear
