// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <array>
#include <memory>
#include <vector>
#include <cmath>

// Include after ReadyGraphAcceptance.h / StableBusStageAcceptance.h. Original
// actual native Player experiment, NOT a hosted SDK plugin or app route test.
namespace warm_source_relocation_acceptance
{
using namespace tracktion::graph;
struct Owner
{
};
struct Counts
{
    unsigned source = 0, independent = 0;
    std::array<unsigned, 3> bus{};
};
struct Stage
{
    int origins = 0, exits = 0, taps = 0, producer = -1, boundary = -1, delay = -1;
};
struct Inventory
{
    std::array<Stage, 3> stages{};
    int root = -1;
    bool captured = false;
};
inline float signal(int channel, juce::int64 frame, bool other) noexcept
{
    if (frame < 0)
        return 0;
    if (other)
        return channel == 0 ? .03125f + (frame % 239 == 17 ? .0625f : 0.f)
                            : -.015625f - (frame % 241 == 29 ? .03125f : 0.f);
    return channel == 0 ? .125f + (frame % 251 == 11 ? .25f : 0.f)
                        : -.0625f - (frame % 257 == 23 ? .125f : 0.f);
}
class Producer final : public Node
{
    Counts &counts;
    const bool other;

  public:
    Producer(Counts &c, bool independent) : counts(c), other(independent) {}
    NodeProperties getNodeProperties() override
    {
        return {true, false, 2, other ? 0 : 256,
                std::size_t(other ? 0x57524f54484552ULL : 0x57524d41494eULL)};
    }
    bool isReadyToProcess() override
    {
        return true;
    }
    void process(ProcessContext &pc) override
    {
        ++(other ? counts.independent : counts.source);
        for (choc::buffer::ChannelCount c = 0; c < 2; ++c)
            for (choc::buffer::FrameCount n = 0; n < pc.buffers.audio.getNumFrames(); ++n)
                pc.buffers.audio.getSample(c, n) = signal(
                    int(c), pc.referenceSampleRange.getStart() + juce::int64(n) - (other ? 0 : 256), other);
    }
};
class Bus final : public Node
{
    std::shared_ptr<Producer> input;
    const std::shared_ptr<Owner> owner;
    Counts &counts;
    const unsigned index;

  public:
    Bus(std::shared_ptr<Producer> p, std::shared_ptr<Owner> o, Counts &c, unsigned i)
        : input(std::move(p)), owner(std::move(o)), counts(c), index(i)
    {
    }
    NodeProperties getNodeProperties() override
    {
        return {true,
                false,
                2,
                input ? input->getNodeProperties().latencyNumSamples : 0,
                std::size_t(0x575242555300ULL + index),
                owner.get(),
                this};
    }
    std::vector<Node *> getDirectInputNodes() override
    {
        return input ? std::vector<Node *>{input.get()} : std::vector<Node *>{};
    }
    bool isReadyToProcess() override
    {
        return !input || input->hasProcessed();
    }
    void process(ProcessContext &pc) override
    {
        ++counts.bus[index];
        if (!input)
            return; // Ordinary Node-owned clear supplies genuine zero.
        const auto source = input->getProcessedOutput().audio;
        const float gain = index == 0 ? .25f : index == 1 ? .5f : 1.f;
        for (choc::buffer::ChannelCount c = 0; c < 2; ++c)
            for (choc::buffer::FrameCount n = 0; n < pc.buffers.audio.getNumFrames(); ++n)
                pc.buffers.audio.getSample(c, n) = gain * source.getSample(c, n);
    }
};
class Probe final : public Node
{
    std::unique_ptr<Node> input;
    const std::array<std::shared_ptr<Owner>, 3> owners;
    Inventory &receipt;

  public:
    Probe(std::unique_ptr<Node> p, std::array<std::shared_ptr<Owner>, 3> o, Inventory &r)
        : input(std::move(p)), owners(std::move(o)), receipt(r)
    {
        setOptimisations({ClearBuffers::no, AllocateAudioBuffer::no});
    }
    NodeProperties getNodeProperties() override
    {
        auto p = input->getNodeProperties();
        p.nodeID = 0x575250524f4245ULL;
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
    void prepareToPlay(const PlaybackInitialisationInfo &) override
    {
        // CONTROL preparation only: immutable graph metadata, no FIFO/PCM reads.
        receipt = {};
        receipt.root = input->getNodeProperties().latencyNumSamples;
        for (auto *node : getNodes(*input, VertexOrdering::postordering))
        {
            const auto p = node->getNodeProperties();
            for (unsigned bus = 0; bus < 3; ++bus)
                if (p.audioCompensationStage == owners[bus].get())
                {
                    auto &s = receipt.stages[bus];
                    if (p.audioCompensationStageOrigin == node)
                    {
                        ++s.origins;
                        s.producer = p.latencyNumSamples;
                    }
                    if (auto *tap = dynamic_cast<LatencyNode *>(node))
                    {
                        ++s.taps;
                        s.delay = tap->compensationSamples();
                    }
                }
        }
        for (auto *node : input->getDirectInputNodes())
        {
            const auto p = node->getNodeProperties();
            for (unsigned bus = 0; bus < 3; ++bus)
                if (p.audioCompensationStage == owners[bus].get())
                {
                    ++receipt.stages[bus].exits;
                    receipt.stages[bus].boundary = p.latencyNumSamples;
                }
        }
        receipt.captured = true;
    }
    void process(ProcessContext &) override
    {
        setAudioOutput(input.get(), input->getProcessedOutput().audio);
    }
};
inline void tests(const juce::File &folder)
{
    require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh warm-relocation evidence");
    juce::String report =
        "Original native modeled pure-delay producer; NOT worker/SDK/app/MIDI/listening qualification.\n";
    std::vector<float> actualPcm, expectedPcm;
    actualPcm.reserve(32768);
    expectedPcm.reserve(32768);
    const auto retain = [&]
    {
        const auto raw = [&](const char *name, const std::vector<float> &values)
        {
            juce::FileOutputStream output(folder.getChildFile(name));
            require(output.openedOk() && output.setPosition(0) && output.truncate().wasOk() &&
                        output.write(values.data(), values.size() * sizeof(float)),
                    "Write exact warm PCM evidence");
            output.flush();
            require(output.getStatus().wasOk(), "Flush exact warm PCM evidence");
        };
        raw("actual-callback-planar-f32le.raw", actualPcm);
        raw("expected-callback-planar-f32le.raw", expectedPcm);
        juce::FileOutputStream stream(folder.getChildFile("result.txt"));
        require(stream.openedOk() && stream.setPosition(0) && stream.truncate().wasOk() &&
                    stream.writeText(report, false, false, "\n"),
                "Write warm relocation evidence");
        stream.flush();
        require(stream.getStatus().wasOk(), "Flush warm relocation evidence");
    };
    try
    {
        for (double rate : {48000., 44100.})
        {
            Counts counts;
            std::array<std::shared_ptr<Owner>, 3> owners{std::make_shared<Owner>(), std::make_shared<Owner>(),
                                                         std::make_shared<Owner>()};
            Inventory incumbent, candidate;
            const auto graph = [&](unsigned route, Inventory &inventory)
            {
                auto main = std::make_shared<Producer>(counts, false);
                auto other = std::make_shared<Producer>(counts, true);
                std::vector<std::unique_ptr<Node>> buses;
                for (unsigned bus = 0; bus < 3; ++bus)
                    buses.push_back(std::make_unique<Bus>(bus == 2       ? other
                                                          : bus == route ? main
                                                                         : nullptr,
                                                          owners[bus], counts, bus));
                return std::make_unique<Probe>(std::make_unique<SummingNode>(std::move(buses)), owners,
                                               inventory);
            };
            LockFreeMultiThreadedNodePlayer player;
            player.setNumThreads(0);
            player.setNode(graph(0, incumbent), rate, 256);
            choc::buffer::ChannelArrayBuffer<float> pcm{choc::buffer::Size::create(2, 256)};
            tracktion::MidiMessageArray midi;
            midi.reserve(128);
            juce::int64 reference = 0;
            const auto process = [&](int frames)
            {
                const auto before = counts;
                const auto allocations = callbackAllocations.load(),
                           destructions = callbackDestructions.load();
                midi.clear();
                pcm.clear();
                const auto begin = juce::Time::getHighResolutionTicks();
                {
                    struct AllocationProbe
                    {
                        bool old = callbackAllocationProbe;
                        AllocationProbe()
                        {
                            callbackAllocationProbe = true;
                        }
                        ~AllocationProbe()
                        {
                            callbackAllocationProbe = old;
                        }
                    } probe;
                    player.process({choc::buffer::FrameCount(frames),
                                    {reference, reference + frames},
                                    {pcm.getView().getStart(choc::buffer::FrameCount(frames)), midi}});
                }
                const auto elapsed = double(juce::Time::getHighResolutionTicks() - begin) /
                                     double(juce::Time::getHighResolutionTicksPerSecond());
                require(counts.source == before.source + 1 && counts.independent == before.independent + 1,
                        "Each producer once, no aggregate counter substitute");
                for (unsigned bus = 0; bus < 3; ++bus)
                    require(counts.bus[bus] == before.bus[bus] + 1, "Each bus once");
                bool exactPcm = true;
                for (int c = 0; c < 2; ++c)
                    for (int n = 0; n < frames; ++n)
                    {
                        const auto source = reference + n - 256;
                        const auto wanted = .25f * signal(c, source, false) + signal(c, source, true);
                        const auto actual = pcm.getView().getSample(choc::buffer::ChannelCount(c),
                                                                    choc::buffer::FrameCount(n));
                        actualPcm.push_back(actual);
                        expectedPcm.push_back(wanted);
                        exactPcm = exactPcm && actual == wanted;
                    }
                report += "callback," + juce::String(rate) + "," + juce::String(reference) + "," +
                          juce::String(frames) + "," + juce::String(elapsed, 9) + "\n";
                require(exactPcm, "Every literal signed PCM sample including startup/tail");
                require(callbackAllocations.load() == allocations &&
                            callbackDestructions.load() == destructions && elapsed <= frames / rate,
                        "Unchanged physical deadline and callback heap oracle");
                reference += frames;
            };
            for (int repeat = 0; repeat < 4; ++repeat)
                for (int frames : {127, 64, 65, 256, 31})
                    process(frames);
            const auto before = counts;
            bool refused = false;
            try
            {
                auto unused = player.prepareFixedGraph(graph(1, candidate));
            }
            catch (const LatencyNode::OutputStageError &error)
            {
                refused =
                    error.status == LatencyNode::OutputStageStatus::configuration && error.difference.known;
                report += "unchanged_guard," + juce::String(error.what()) + "\n";
            }
            require(counts.source == before.source && counts.independent == before.independent &&
                        counts.bus == before.bus,
                    "Private preparation does not process producers or controls");
            require(incumbent.captured && candidate.captured && incumbent.root == 256 &&
                        candidate.root == 256,
                    "Both actual complete stage inventories before refusal");
            for (unsigned bus = 0; bus < 3; ++bus)
            {
                for (bool after : {false, true})
                {
                    const auto &s = (after ? candidate : incumbent).stages[bus];
                    const bool warm = bus == (after ? 1u : 0u);
                    report += juce::String(after ? "after" : "before") + "," + juce::String(bus) + "," +
                              juce::String(s.producer) + "," + juce::String(s.delay) + "," +
                              juce::String(s.boundary) + "\n";
                    require(s.origins == 1 && s.exits == 1 && s.producer == (warm ? 256 : 0) &&
                                s.boundary == 256 && s.taps == (warm ? 0 : 1) && s.delay == (warm ? -1 : 256),
                            "Warm relocation stage contract independent of measured PCM");
                }
            }
            require(refused, "Default no-joint-plan guard must refuse, never bypassed by this experiment");
            for (int frames : {65, 127, 64, 31, 256})
                process(frames);
        }
        report +=
            "PASS diagnostic/refusal/incumbent continuation only; route adoption remains unimplemented.\n";
        retain();
    }
    catch (...)
    {
        report += "FAILED; preserve this attempt.\n";
        retain();
        throw;
    }
}
} // namespace warm_source_relocation_acceptance
