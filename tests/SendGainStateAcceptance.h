// SPDX-License-Identifier: AGPL-3.0-or-later
// Original SOURCE-ONLY acceptance; include after ReadyGraphAcceptance.h/probes.
#pragma once
#include <tracktion_graph/tracktion_graph.h>
#include <array>
#include <memory>
#include <type_traits>

namespace vibera::test::send_gain_state
{
using Player = tracktion::graph::LockFreeMultiThreadedNodePlayer;
using Bank = tracktion::graph::SendGainState;
using Node = tracktion::graph::Node;
struct Counts
{
    unsigned sourceCalls = 0, gainCalls = 0;
};
class Ones final : public Node
{
    Counts &counts;
    const std::size_t identity;
  public:
    Ones(Counts &c, std::size_t id) : counts(c), identity(id) {}
    tracktion::graph::NodeProperties getNodeProperties() override
    { return {true, false, 1, 0, identity}; }
    bool isReadyToProcess() override { return true; }
    void process(ProcessContext &context) override
    {
        ++counts.sourceCalls;
        for (choc::buffer::FrameCount frame = 0; frame < context.buffers.audio.getNumFrames(); ++frame)
            context.buffers.audio.getSample(0, frame) = 1.f;
    }
};
inline std::unique_ptr<Node> graph(std::shared_ptr<Bank> bank, Counts &counts, float target,
                                  bool duplicate = false, int bus = 42)
{
    std::vector<std::unique_ptr<Node>> roots;
    auto send = std::make_unique<tracktion::graph::SendNode>(
        std::make_unique<Ones>(counts, 0x76696273656e6431ULL), bus,
        [&counts, target] { ++counts.gainCalls; return target; }, std::nullopt, bank);
    roots.push_back(std::make_unique<tracktion::graph::SinkNode>(std::move(send)));
    if (duplicate)
    {
        auto second = std::make_unique<tracktion::graph::SendNode>(
            std::make_unique<Ones>(counts, 0x76696273656e6432ULL), bus,
            [&counts, target] { ++counts.gainCalls; return target; }, std::nullopt, bank);
        roots.push_back(std::make_unique<tracktion::graph::SinkNode>(std::move(second)));
    }
    roots.push_back(std::make_unique<tracktion::graph::ReturnNode>(bus));
    return std::make_unique<tracktion::graph::SummingNode>(std::move(roots));
}

inline void tests(const juce::File &folder)
{
    static_assert(!std::is_copy_constructible_v<Bank> && !std::is_move_constructible_v<Bank>);
    require(folder.createDirectory().wasOk(), "Owned send-gain evidence folder");
    const auto reportFile = folder.getChildFile("send-gain-state.txt");
    require(!reportFile.exists(), "Send gain receipt must be fresh");
    juce::String report =
        "Original actual NodePlayer/SendNode/ReturnNode/GainNode tests. Fixed preparation64, "
        "variable7+13+17 blocks; exact binary-fraction oracle.\n"
        "No engine/provider/UI/real-device/timing/listening claim. Ordinary native branch preserved.\n";
    bool passed = true;
    auto check = [&](bool ok, const char *name)
    {
        passed = passed && ok;
        report += juce::String(name) + "=" + juce::String(int(ok)) + "\n";
        require(reportFile.replaceWithText(report), "Retain original ramp/failure evidence");
    };
    auto bank = std::make_shared<Bank>(Bank::EdgeIdentity{11, 22, 42});
    std::weak_ptr<Bank> weak = bank;
    Counts counts;
    ready_graph_acceptance::Counts poolCounts;
    {
        Player player([&](Player &p)
        { return std::make_unique<ready_graph_acceptance::CountingPool>(p, poolCounts); });
        player.setNumThreads(0);
        player.enablePooledMemoryAllocations(false);
        player.setNode(graph(bank, counts, 1.f), 48000., 64);
        const auto epoch = bank->epochControl();
        check(counts.gainCalls == 0 && bank->attachmentsControl() == 1 &&
                  bank->canRetainControl({11, 22, 42}, epoch) == Bank::Admission::ready,
              "control_construction_and_prepare_never_call_getter_or_copy_DSP");
        tracktion::MidiMessageArray midi;
        midi.reserve(64);
        std::int64_t reference = 0;
        auto process = [&](int frames, auto oracle)
        {
            // Control allocates output before entering the actual callback probe.
            choc::buffer::ChannelArrayBuffer<float> pcm(choc::buffer::Size::create(1, frames));
            pcm.clear(); midi.clear();
            const auto allocations = callbackAllocations.load(), destructions = callbackDestructions.load();
            const auto sourceCalls = counts.sourceCalls, gainCalls = counts.gainCalls;
            bool correct;
            {
                struct Scope
                {
                    bool previous = callbackAllocationProbe;
                    Scope() { callbackAllocationProbe = true; }
                    ~Scope() { callbackAllocationProbe = previous; }
                } scope;
                player.process({choc::buffer::FrameCount(frames), {reference, reference + frames},
                                {pcm.getView(), midi}});
                correct = callbackAllocations.load() == allocations &&
                          callbackDestructions.load() == destructions &&
                          counts.sourceCalls == sourceCalls + 1 && counts.gainCalls == gainCalls + 1;
                for (int frame = 0; frame < frames; ++frame)
                    correct = correct && pcm.getView().getSample(0, frame) == oracle(frame);
            }
            reference += frames;
            return correct;
        };
        check(process(7, [](int i) { return float(i + 1) / 64.f; }),
              "new_route_zero_to_target_first7_exact_samples");
        const auto getterBefore = counts.gainCalls;
        {
            auto discarded = player.prepareFixedGraph(graph(bank, counts, 1.f));
            check(bank->attachmentsControl() == 2 && counts.gainCalls == getterBefore,
                  "private_candidate_attaches_without_reading_advancing_ramp");
        }
        check(bank->attachmentsControl() == 1 && bank->epochControl() == epoch,
              "discarding_candidate_retains_live_bank_and_epoch");
        auto candidate = player.prepareFixedGraph(graph(bank, counts, 1.f));
        auto retired = player.makeGraphRetirementSlot();
        bool clean = true;
        check(ready_graph_acceptance::publishWithoutNewDelete(player, candidate, retired, clean) ==
                  Player::ReadyGraphStatus::ready && clean && counts.gainCalls == getterBefore,
              "publish_unchanged_edge_after7_has_no_control_getter_new_delete");
        check(process(13, [](int i) { return float(8 + i) / 64.f; }),
              "adopted_unchanged_edge_continues_inflight7_to20_without_snap");
        check(player.retireReadyGraph(retired), "control_retirement_of_previous_graph");
        auto changedTarget = player.prepareFixedGraph(graph(bank, counts, .5f));
        auto retired2 = player.makeGraphRetirementSlot();
        check(ready_graph_acceptance::publishWithoutNewDelete(player, changedTarget, retired2, clean) ==
                  Player::ReadyGraphStatus::ready && clean,
              "publish_new_target_after20_retains_same_preowned_bank");
        check(process(17, [](int i) { return .3125f + .1875f * float(i + 1) / 64.f; }),
              "new_target_starts_from_last_emitted20_over64_exact17_samples");
        check(player.retireReadyGraph(retired2), "retire_incumbent_without_touching_new_owner_DSP");
        check(process(13, [](int i) { return .3125f + .1875f * float(18 + i) / 64.f; }),
              "partial_new_target_ramp_continues_after_retirement");
        check(process(34, [](int i) { return .3125f + .1875f * float(31 + i) / 64.f; }),
              "fixed64_duration_independent_of_variable_callback_partition");
        check(process(7, [](int) { return .5f; }), "settled_target_exact_no_ramp_replay");
        const auto beforeRefusals = counts.gainCalls;
        bool duplicate = false, configuration = false, wrongBus = false;
        try { auto refused = player.prepareFixedGraph(graph(bank, counts, 1.f, true)); }
        catch (const std::invalid_argument &) { duplicate = true; }
        try
        {
            ready_graph_acceptance::Counts otherPool;
            Player other([&](Player &p)
            { return std::make_unique<ready_graph_acceptance::CountingPool>(p, otherPool); });
            other.setNumThreads(0);
            other.setNode(graph(bank, counts, 1.f), 44100., 128);
        }
        catch (const std::invalid_argument &) { configuration = true; }
        try { auto refused = graph(bank, counts, 1.f, false, 43); }
        catch (const std::invalid_argument &) { wrongBus = true; }
        check(duplicate && configuration && wrongBus && counts.gainCalls == beforeRefusals &&
                  bank->epochControl() == epoch &&
                  bank->canRetainControl({11, 23, 42}, epoch) == Bank::Admission::changedEdge,
              "duplicate_graph_writers_configuration_and_changed_edge_refuse_without_DSP_reads");
        check(process(13, [](int) { return .5f; }), "all_refusals_preserve_original_actual_PCM");
        bool swept = true, exhausted = false;
        unsigned injected = 0;
        for (int site = 0; site < 512; ++site)
        {
            auto node = graph(bank, counts, 1.f); // Ownership prepared before arming fault.
            auto *incumbent = player.getNode();
            const auto attached = bank->attachmentsControl(), gainCalls = counts.gainCalls;
            Player::ReadyGraph pending;
            preparationAllocationFailureHit = false;
            preparationAllocationFailureAfter = site;
            bool threw = false;
            try { pending = player.prepareFixedGraph(std::move(node)); }
            catch (const std::bad_alloc &) { threw = true; }
            catch (...) { preparationAllocationFailureAfter = -1; throw; }
            const bool hit = preparationAllocationFailureHit;
            preparationAllocationFailureAfter = -1; // Before retirement/readback/evidence.
            const bool ownership = bank->attachmentsControl() == attached + (threw ? 0u : 1u);
            pending = {}; // CONTROL retirement; no prepared graph was processed.
            const bool unchanged = player.getNode() == incumbent && counts.gainCalls == gainCalls &&
                                   bank->attachmentsControl() == attached && bank->epochControl() == epoch;
            const bool pcm = process(7, [](int) { return .5f; });
            swept = swept && ownership && unchanged && pcm;
            report += "prepareFault=" + juce::String(site) + " hit=" + juce::String(int(hit)) +
                      " threw=" + juce::String(int(threw)) + " ownership=" + juce::String(int(ownership)) +
                      " unchanged=" + juce::String(int(unchanged)) + " exactPCM=" + juce::String(int(pcm)) + "\n";
            require(reportFile.replaceWithText(report), "Retain every native gain preparation fault");
            injected += hit;
            if (!swept || !hit) { exhausted = !hit; break; }
        }
        check(swept && exhausted && injected > 0,
              "all_reached_actual_preparation_allocations_leave_old_ramp_bank_and_PCM_intact");
        player.clearNode();
        check(player.retireReadyGraph(retired) && player.retireReadyGraph(retired2),
              "all_holder_retirement_is_explicit_control_only");
        check(bank->attachmentsControl() == 0 && bank->epochControl() != epoch &&
                  bank->canRetainControl({11, 22, 42}, epoch) == Bank::Admission::epochChanged,
              "final_quiesced_retirement_invalidates_old_epoch");
        bool stale = false;
        try { player.setNode(graph(bank, counts, 1.f), 48000., 64); }
        catch (const std::invalid_argument &) { stale = true; }
        check(stale && bank->attachmentsControl() == 0,
              "retired_bank_cannot_be_reopened_by_stale_candidate");
        auto fresh = std::make_shared<Bank>(Bank::EdgeIdentity{11, 22, 42});
        player.setNode(graph(fresh, counts, 1.f), 48000., 64);
        check(process(7, [](int i) { return float(i + 1) / 64.f; }),
              "genuinely_new_route_uses_fresh_zero_start_not_retired_gain");
        player.clearNode();
        check(fresh->attachmentsControl() == 0, "fresh_bank_final_teardown_balanced");
    }
    bank.reset();
    check(weak.expired(), "native_graph_state_owners_retired_on_control_no_leak");
    // Ordinary native path is intentionally unchanged. This documents current
    // JUCE setTargetValue-before-reset snap rather than pretending it ramps.
    {
        Counts ordinary;
        ready_graph_acceptance::Counts pc;
        Player player([&](Player &p)
        { return std::make_unique<ready_graph_acceptance::CountingPool>(p, pc); });
        player.setNumThreads(0);
        float gain = .25f;
        auto input = std::make_unique<Ones>(ordinary, 0x7669626f7264696eULL);
        auto root = std::make_unique<tracktion::graph::GainNode>(std::move(input), [&] { ++ordinary.gainCalls; return gain; });
        player.setNode(std::move(root), 48000., 64);
        check(ordinary.gainCalls == 1, "ordinary_constructor_getter_behavior_preserved");
        gain = .75f;
        choc::buffer::ChannelArrayBuffer<float> pcm(choc::buffer::Size::create(1, 64));
        tracktion::MidiMessageArray midi;
        midi.reserve(64); pcm.clear();
        player.process({64, {0, 64}, {pcm.getView(), midi}});
        bool unchangedOrdinary = ordinary.gainCalls == 2;
        for (unsigned frame = 0; frame < 64; ++frame)
            unchangedOrdinary = unchangedOrdinary && pcm.getView().getSample(0, frame) == .75f;
        check(unchangedOrdinary, "ordinary_setTarget_before_reset_snap_behavior_documented_preserved");
    }
    require(passed, "Actual native stable send gain acceptance");
}
} // namespace vibera::test::send_gain_state
