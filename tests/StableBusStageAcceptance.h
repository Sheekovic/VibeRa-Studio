// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <array>
#include <vector>
#include <cmath>
#include <memory>
#include <algorithm>
#include <cstdint>

// Include after ReadyGraphAcceptance.h. Original actual native Player/FIFO
// experiment; no worker/device/SDK/hardware/listening claim. Not a graph mock.
namespace stable_bus_stage_acceptance
{
using namespace tracktion::graph;
using Player = LockFreeMultiThreadedNodePlayer;
struct Stage final
{
}; // Nonmoving owners, graph-pinned; identity is never a hash.
inline float mainSignal(int c, std::int64_t frame) noexcept
{
    return (c == 0 ? .125f : -.0625f) +
           (frame % (c == 0 ? 251 : 257) == (c == 0 ? 11 : 23) ? (c == 0 ? .25f : -.125f) : 0.f);
}
inline float otherSignal(int c, std::int64_t frame) noexcept
{
    return (c == 0 ? .03125f : -.015625f) +
           (frame % (c == 0 ? 239 : 241) == (c == 0 ? 17 : 29) ? (c == 0 ? .0625f : -.03125f) : 0.f);
}
class Source final : public Node
{
    const std::shared_ptr<Stage> owner;
    const int bus, route, channels, latency;
    const std::size_t lineage;
    unsigned &calls;

  public:
    Source(std::shared_ptr<Stage> identity, int b, int r, int ch, int l, std::size_t id, unsigned &count)
        : owner(std::move(identity)), bus(b), route(r), channels(ch), latency(l), lineage(id), calls(count)
    {
    }
    NodeProperties getNodeProperties() override
    {
        return {true, false, channels, latency, lineage, owner.get(), owner ? this : nullptr};
    }
    bool isReadyToProcess() override
    {
        return true;
    }
    void process(ProcessContext &pc) override
    {
        ++calls;
        for (choc::buffer::ChannelCount c = 0; c < pc.buffers.audio.getNumChannels(); ++c)
            for (choc::buffer::FrameCount n = 0; n < pc.buffers.audio.getNumFrames(); ++n)
            {
                const auto frame = pc.referenceSampleRange.getStart() + std::int64_t(n);
                pc.buffers.audio.getSample(c, n) = bus == 2       ? otherSignal(int(c), frame)
                                                   : bus == route ? mainSignal(int(c), frame)
                                                                  : 0.f;
            }
    }
};
class Branch final : public Node
{
    std::shared_ptr<Node> input;
    const std::size_t lineage;

  public:
    Branch(std::shared_ptr<Node> source, std::size_t id) : input(std::move(source)), lineage(id) {}
    NodeProperties getNodeProperties() override
    {
        auto props = input->getNodeProperties();
        props.nodeID = lineage; // Ordinary distinct topology ID, NOT a stage identity.
        return props;
    }
    std::vector<Node *> getDirectInputNodes() override
    {
        return {input.get()};
    }
    bool isReadyToProcess() override
    {
        return input->hasProcessed();
    }
    void process(ProcessContext &) override
    {
        setAudioOutput(input.get(), input->getProcessedOutput().audio);
    }
};
struct Fixture final
{
    ready_graph_acceptance::Counts pools;
    std::array<std::shared_ptr<Stage>, 3> owners{std::make_shared<Stage>(), std::make_shared<Stage>(),
                                                 std::make_shared<Stage>()};
    Player player;
    choc::buffer::ChannelArrayBuffer<float> pcm{choc::buffer::Size::create(2, 256)};
    tracktion::MidiMessageArray midi;
    std::vector<int> routes = std::vector<int>(32768, -1);
    unsigned calls = 0, generation = 1;
    int activeRoute = 0, lastFrames = 0;
    std::int64_t reference = 0;
    Node *root = nullptr;
    std::array<LatencyNode *, 3> taps{};
    Fixture()
        : player([this](Player &p)
                 { return std::make_unique<ready_graph_acceptance::CountingPool>(p, pools); })
    {
        player.setNumThreads(0);
        auto initial = graph(0);
        root = initial.get();
        player.setNode(std::move(initial), 48000., 256);
        midi.reserve(128);
        taps = stageTaps(*root);
    }
    std::unique_ptr<Node> graph(int route, int delay = 256, int bad = 0)
    {
        std::vector<std::unique_ptr<Node>> nodes;
        const auto identityBase = std::size_t(0x564253000000ULL + 16 * generation++);
        for (int bus = 0; bus < 3; ++bus)
        {
            if (bad == 3 && bus == 2)
                continue; // Explicit stage removal.
            if (bad == 4 && bus == 0)
            {
                auto shared = std::make_shared<Source>(owners[0], bus, route, 2, 0, identityBase, calls);
                nodes.push_back(std::make_unique<Branch>(shared, identityBase + 8));
                nodes.push_back(std::make_unique<Branch>(shared, identityBase + 9));
                continue;
            }
            auto owner = bad == 1 && bus == 1 ? owners[0] : owners[std::size_t(bus)];
            nodes.push_back(std::make_unique<Source>(owner, bus, route, bad == 2 && bus == 1 ? 1 : 2, 0,
                                                     identityBase + std::size_t(bus), calls));
        }
        // Independent zero producer with declared delay: models the unchanged
        // unused wet branch's PDC requirement. It does not fake a hosted plugin.
        nodes.push_back(std::make_unique<Source>(nullptr, 3, route, 2, delay, identityBase + 3, calls));
        return std::make_unique<SummingNode>(std::move(nodes));
    }
    std::array<LatencyNode *, 3> stageTaps(Node &value)
    {
        std::array<LatencyNode *, 3> result{};
        for (auto *node : getNodes(value, VertexOrdering::postordering))
            if (auto *tap = dynamic_cast<LatencyNode *>(node))
                for (std::size_t bus = 0; bus < owners.size(); ++bus)
                    if (tap->getNodeProperties().audioCompensationStage == owners[bus].get())
                    {
                        require(!result[bus], "One actual compensation FIFO per exact output owner");
                        result[bus] = tap;
                    }
        for (auto *tap : result)
            require(tap != nullptr, "Every declared bus has actual native PDC tap");
        return result;
    }
    bool process(int frames = 256)
    {
        if (frames < 1 || frames > 256 || reference + frames > std::int64_t(routes.size()))
            return false;
        lastFrames = frames;
        std::fill_n(routes.begin() + std::ptrdiff_t(reference), frames, activeRoute);
        pcm.clear();
        midi.clear();
        const auto before = calls;
        const auto allocations = callbackAllocations.load(), destructions = callbackDestructions.load();
        {
            struct Probe
            {
                bool previous = callbackAllocationProbe;
                Probe()
                {
                    callbackAllocationProbe = true;
                }
                ~Probe()
                {
                    callbackAllocationProbe = previous;
                }
            } probe;
            player.process({choc::buffer::FrameCount(frames),
                            {reference, reference + frames},
                            {pcm.getView().getStart(choc::buffer::FrameCount(frames)), midi}});
        }
        bool good = calls == before + 4 && callbackAllocations.load() == allocations &&
                    callbackDestructions.load() == destructions;
        for (int n = 0; n < frames; ++n)
        {
            const auto source = reference + n - 256; // Literal delay, no output alignment/fit.
            for (int c = 0; c < 2; ++c)
            {
                const float main = source < 0 ? 0.f : mainSignal(c, source);
                const float other = source < 0 ? 0.f : otherSignal(c, source);
                const auto actual =
                    pcm.getView().getSample(choc::buffer::ChannelCount(c), choc::buffer::FrameCount(n));
                good = good && std::isfinite(actual) && actual == main + other;
                for (int bus = 0; bus < 3; ++bus)
                {
                    const float expected = bus == 2                                            ? other
                                           : source >= 0 && routes[std::size_t(source)] == bus ? main
                                                                                               : 0.f;
                    const auto output = taps[std::size_t(bus)]->getProcessedOutput().audio;
                    good = good && output.getSample(choc::buffer::ChannelCount(c),
                                                    choc::buffer::FrameCount(n)) == expected;
                }
            }
        }
        reference += frames;
        return good;
    }
    bool transition(int route, int frames)
    {
        auto next = graph(route);
        auto *nextRoot = next.get();
        auto prepared = player.prepareFixedGraph(std::move(next));
        const auto candidateTaps = stageTaps(*nextRoot);
        const auto request = prepared.publicationSerial();
        auto retired = player.makeGraphRetirementSlot();
        if (player.publishReadyGraph(prepared, retired) != Player::ReadyGraphStatus::ready)
            return false;
        activeRoute = route;
        root = nextRoot;
        taps = candidateTaps;
        const bool pcmGood = process(frames);
        const auto selected = player.graphSelection();
        return pcmGood && selected.state == GraphSelectionState::selected &&
               selected.selectedSerial == request;
        // retired owner quiesces/destructs here, CONTROL after actual callback.
    }
};
} // namespace stable_bus_stage_acceptance

__declspec(noinline) static void stableBusStageTests(const juce::File &folder)
{
    using namespace stable_bus_stage_acceptance;
    require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh owned stable-output-stage evidence");
    auto fixture = std::make_unique<Fixture>(); // No oversized main stack frame.
    auto &f = *fixture;
    juce::String report =
        "Original actual native Player/FIFO stage experiment. No worker/device/hardware/listening claim.\n";
    bool passed = true;
    unsigned cases = 0;
    const auto check = [&](bool ok, const char *name)
    {
        ++cases;
        passed &= ok;
        report += juce::String(name) + "=" + juce::String(int(ok)) + "\n";
        require(folder.getChildFile("stable-bus-stages.txt").replaceWithText(report),
                "Retain every actual stage case before aggregate assertion");
        if (!ok)
        {
            auto out = folder.getChildFile("case-" + juce::String(cases) + "-last-actual-callback.raw")
                           .createOutputStream();
            require(bool(out) && out->setPosition(0) && out->truncate().wasOk(),
                    "Fresh owned last callback evidence, not an invented failed callback");
            for (int c = 0; c < 2; ++c)
                for (int frame = 0; frame < f.lastFrames; ++frame)
                {
                    const float actual = f.pcm.getView().getSample(choc::buffer::ChannelCount(c),
                                                                   choc::buffer::FrameCount(frame));
                    require(out->write(&actual, sizeof(actual)),
                            "Retain exact actual last callback PCM bits");
                }
        }
    };
    check(f.process() && f.process() && f.process(),
          "genuine_first_cold_then_exact_literal256_PCM_and_three_bus_queues");
    {
        auto candidate = f.player.prepareFixedGraph(f.graph(1));
        check(!candidate.empty() && f.process(),
              "private_preparation_discard_does_not_process_or_reset_live_queue");
    }
    check(f.process(), "candidate_discard_leaves_every_queued_sample_unchanged");
    for (int i = 0; i < 12; ++i)
        check(f.transition((i + 1) & 1, std::array<int, 4>{256, 73, 183, 127}[std::size_t(i % 4)]),
              "route_undo_redo_exact_all_tail_frames_and_unaffected_bus_including_partial_blocks");
    const auto refuse = [&](int bad, int delay, LatencyNode::OutputStageStatus expected)
    {
        auto *const old = f.player.getNode();
        const auto before = f.player.graphSelection();
        bool refused = false;
        try
        {
            auto ignored = f.player.prepareFixedGraph(f.graph(1 - f.activeRoute, delay, bad));
        }
        catch (const LatencyNode::OutputStageError &error)
        {
            refused = error.status == expected;
        }
        const auto after = f.player.graphSelection();
        return refused && f.player.getNode() == old && after.postedSerial == before.postedSerial &&
               after.selectedSerial == before.selectedSerial && f.process();
    };
    check(refuse(1, 256, LatencyNode::OutputStageStatus::duplicate),
          "same_owner_two_origins_refused_no_pointer_hash_alias_or_PCM_change");
    check(refuse(4, 256, LatencyNode::OutputStageStatus::fanout),
          "one_producer_two_consumer_queues_refused_no_simultaneous_shared_FIFO_processing");
    check(refuse(2, 256, LatencyNode::OutputStageStatus::shape),
          "channel_shape_refused_before_publication_old_queues_intact");
    check(refuse(3, 256, LatencyNode::OutputStageStatus::missing),
          "missing_bus_stage_refused_before_publication_old_tail_and_unaffected_bus_intact");
    check(refuse(0, 512, LatencyNode::OutputStageStatus::configuration),
          "changed_delay_refused_without_claiming_general_variable_PDC");
    // A queued B is not evidence its buffers were actually adopted. C must not
    // prepare against B while the sole processing owner is still rendering A.
    auto queuedGraph = f.graph(1 - f.activeRoute);
    auto *queuedRoot = queuedGraph.get();
    auto queued = f.player.prepareFixedGraph(std::move(queuedGraph));
    auto retired = f.player.makeGraphRetirementSlot();
    const auto serial = queued.publicationSerial();
    check(f.player.publishReadyGraph(queued, retired) == Player::ReadyGraphStatus::ready,
          "post_B_without_fake_adoption");
    bool refusedQueued = false;
    try
    {
        auto ignored = f.player.prepareFixedGraph(f.graph(f.activeRoute));
    }
    catch (const LatencyNode::OutputStageError &error)
    {
        refusedQueued = error.status == LatencyNode::OutputStageStatus::predecessorPending;
    }
    f.activeRoute = 1 - f.activeRoute;
    f.root = queuedRoot;
    f.taps = f.stageTaps(*queuedRoot);
    check(refusedQueued && f.process() && f.player.graphSelection().selectedSerial == serial,
          "unadopted_predecessor_refused_then_actual_B_adoption_keeps_exact_history");
    report +=
        "terminal=" + juce::String(passed ? "completed" : "failed") + ";cases=" + juce::String(cases) + "\n";
    require(folder.getChildFile("stable-bus-stages.txt").replaceWithText(report),
            "Actual terminal stage outcome");
    require(passed, "Actual same-stage queue carry failed; no warmup/deadline/oracle adjustment permitted");
}
