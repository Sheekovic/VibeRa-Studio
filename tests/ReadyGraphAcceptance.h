// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <tracktion_graph/tracktion_graph.h>
#include <array>
#include <cmath>
#include <cstddef>
#include <new>
#include <type_traits>
#include <stdexcept>
#include <string>

// Include after require and CallbackAllocationProbe. No provider/device/input.
namespace ready_graph_acceptance
{
using Player = tracktion::graph::LockFreeMultiThreadedNodePlayer;
using Status = Player::ReadyGraphStatus;
struct Counts
{
    unsigned creates = 0, clears = 0, preparations = 0, processes = 0, destructions = 0;
};
// An original supported ThreadPool implementation without physical helper
// threads. Actual NodePlayer executes its prepared graphs on the calling owner.
// Counts prove fixed preparation/publication do not reconfigure the pool.
class CountingPool final : public Player::ThreadPool
{
    Counts &counts;

  public:
    CountingPool(Player &owner, Counts &value) : ThreadPool(owner), counts(value) {}
    void createThreads(size_t, juce::AudioWorkgroup) override
    {
        ++counts.creates;
        resetExitSignal();
    }
    void clearThreads() override
    {
        ++counts.clears;
        signalShouldExit();
    }
    void signalOne() override {}
    void signal(int) override {}
    void signalAll() override {}
    void waitForFinalNode() override {}
};
class Constant : public tracktion::graph::Node
{
    const float value;
    const int latency;
    Counts &counts;

  public:
    Constant(float v, int l, Counts &c) : value(v), latency(l), counts(c) {}
    ~Constant() override
    {
        ++counts.destructions;
    }
    tracktion::graph::NodeProperties getNodeProperties() override
    {
        return {true, false, 1, latency, 0x7669627261726479ULL};
    }
    void prepareToPlay(const tracktion::graph::PlaybackInitialisationInfo &) override
    {
        ++counts.preparations;
    }
    bool isReadyToProcess() override
    {
        return true;
    }
    void process(ProcessContext &context) override
    {
        ++counts.processes;
        for (choc::buffer::FrameCount frame = 0; frame < context.buffers.audio.getNumFrames(); ++frame)
            context.buffers.audio.getSample(0, frame) = value;
    }
};
class Refusing final : public Constant
{
  public:
    explicit Refusing(Counts &c) : Constant(.75f, 37, c) {}
    void prepareToPlay(const tracktion::graph::PlaybackInitialisationInfo &) override
    {
        throw std::runtime_error("Original candidate preparation refusal");
    }
};
class Reentrant final : public Constant
{
    Player &owner;

  public:
    Reentrant(Counts &c, Player &p) : Constant(.75f, 37, c), owner(p) {}
    void prepareToPlay(const tracktion::graph::PlaybackInitialisationInfo &) override
    {
        owner.clearNode();
    }
};
class Borrowed final : public tracktion::graph::Node
{
    Node *input;

  public:
    explicit Borrowed(Node *value) : input(value) {}
    tracktion::graph::NodeProperties getNodeProperties() override
    {
        return input->getNodeProperties();
    }
    std::vector<Node *> getDirectInputNodes() override
    {
        return {input};
    }
    bool isReadyToProcess() override
    {
        return input->hasProcessed();
    }
    void process(ProcessContext &) override
    {
        setAudioOutput(input, input->getProcessedOutput().audio);
    }
};
struct Fixture
{
    Counts counts;
    Player player;
    choc::buffer::ChannelArrayBuffer<float> pcm{choc::buffer::Size::create(1, 128)};
    tracktion::MidiMessageArray midi;
    std::int64_t reference = 0;
    Fixture() : player([this](Player &p) { return std::make_unique<CountingPool>(p, counts); })
    {
        player.setNumThreads(0);
        player.setNode(std::make_unique<Constant>(.125f, 11, counts), 48000., 128);
        midi.reserve(128); // External player sink, prepared off the processing probe.
    }
    bool process(float expected)
    {
        const auto before = counts.processes;
        pcm.clear();
        midi.clear();
        const bool previous = callbackAllocationProbe;
        const auto allocations = callbackAllocations.load(), destructions = callbackDestructions.load();
        callbackAllocationProbe = true;
        player.process({128, {reference, reference + 128}, {pcm.getView(), midi}});
        callbackAllocationProbe = previous;
        reference += 128;
        bool valid = callbackAllocations.load() == allocations &&
                     callbackDestructions.load() == destructions &&
                     counts.processes == before + (player.getNode() ? 1 : 0);
        for (choc::buffer::FrameCount frame = 0; frame < 128; ++frame)
            valid = valid && std::isfinite(pcm.getView().getSample(0, frame)) &&
                    pcm.getView().getSample(0, frame) == expected;
        return valid;
    }
};
inline Status publishWithoutNewDelete(Player &player, Player::ReadyGraph &ready,
                                      Player::GraphRetirementSlot &slot, bool &clean)
{
    const auto allocations = callbackAllocations.load(), destructions = callbackDestructions.load();
    const bool previous = callbackAllocationProbe;
    callbackAllocationProbe = true; // Commit instrumentation, NOT an audio callback claim.
    const auto result = player.publishReadyGraph(ready, slot);
    callbackAllocationProbe = previous;
    clean = clean && callbackAllocations.load() == allocations && callbackDestructions.load() == destructions;
    return result;
}
} // namespace ready_graph_acceptance

static void readyGraphTests(const juce::File &folder)
{
    using namespace ready_graph_acceptance;
    static_assert(std::is_nothrow_move_constructible_v<Player::ReadyGraph>);
    static_assert(!std::is_copy_constructible_v<Player::ReadyGraph>);
    static_assert(noexcept(std::declval<Player &>().publishReadyGraph(
        std::declval<Player::ReadyGraph &>(), std::declval<Player::GraphRetirementSlot &>())));
    require(folder.createDirectory().wasOk(), "Owned ready-graph evidence directory");
    juce::String report =
        "DRAFT actual player, original constant nodes/counting pool, fixed non-pooled configuration. "
        "C++new/delete probes only; no provider/multithread-deadline/hardware/listening claim.\n";
    bool passed = true;
    unsigned cases = 0;
    const auto check = [&](bool value, const char *name)
    {
        ++cases;
        passed = passed && value;
        report += juce::String(name) + "=" + juce::String(int(value)) + "\n";
    };
    {
        Fixture f;
        check(f.process(.125f), "initial_actual_PCM_once_no_observed_new_delete");
        auto *old = f.player.getNode();
        const auto creates = f.counts.creates, clears = f.counts.clears, processes = f.counts.processes;
        auto ready = f.player.prepareFixedGraph(std::make_unique<Constant>(.75f, 37, f.counts));
        auto competing = f.player.prepareFixedGraph(std::make_unique<Constant>(.5f, 19, f.counts));
        auto slot = f.player.makeGraphRetirementSlot();
        check(f.player.getNode() == old && f.player.getSampleRate() == 48000 &&
                  f.player.getBlockSize() == 128 && f.player.getPreparedLatencySamples() == 11 &&
                  ready.latencySamples() == 37 && f.counts.creates == creates && f.counts.clears == clears &&
                  f.counts.processes == processes &&
                  f.player.validateReadyGraph(ready, slot) == Status::ready && f.process(.125f),
              "fallible_preparation_is_private_no_threads_or_DSP");
        bool clean = true;
        check(publishWithoutNewDelete(f.player, ready, slot, clean) == Status::ready && clean &&
                  ready.empty() && f.player.getPreparedLatencySamples() == 37 && f.process(.75f) &&
                  f.counts.creates == creates && f.counts.clears == clears,
              "commit_and_actual_next_PCM_no_allocation_destruction_or_pool_reconfigure");
        check(f.player.validateReadyGraph(competing, slot) == Status::staleGeneration &&
                  publishWithoutNewDelete(f.player, competing, slot, clean) == Status::staleGeneration &&
                  !competing.empty() && clean && f.process(.75f),
              "successful_publication_invalidates_other_handle");
        auto third = f.player.prepareFixedGraph(std::make_unique<Constant>(.25f, 13, f.counts));
        check(publishWithoutNewDelete(f.player, third, slot, clean) == Status::ready && clean &&
                  !slot.empty() && f.process(.25f),
              "retired_actual_owner_transferred_without_commit_destruction");
        auto fourth = f.player.prepareFixedGraph(std::make_unique<Constant>(.5f, 19, f.counts));
        auto *current = f.player.getNode();
        check(f.player.validateReadyGraph(fourth, slot) == Status::retirementSlotNotEmpty &&
                  publishWithoutNewDelete(f.player, fourth, slot, clean) == Status::retirementSlotNotEmpty &&
                  f.player.getNode() == current && !fourth.empty() && clean && f.process(.25f),
              "nonempty_retirement_refuses_without_consuming_candidate");
        const auto destroyed = f.counts.destructions;
        check(f.player.retireReadyGraph(slot) && slot.empty() && f.counts.destructions == destroyed + 1 &&
                  f.process(.25f),
              "explicit_control_retirement_after_commit");
        check(publishWithoutNewDelete(f.player, fourth, slot, clean) == Status::ready && clean &&
                  f.process(.5f),
              "retirement_slot_reusable_after_control_retirement");
        check(f.player.retireReadyGraph(slot), "final_explicit_retirement");
    }
    {
        Fixture f, other;
        auto ready = f.player.prepareFixedGraph(std::make_unique<Constant>(.75f, 37, f.counts));
        auto slot = f.player.makeGraphRetirementSlot(), foreignSlot = other.player.makeGraphRetirementSlot();
        Player::ReadyGraph empty;
        Player::GraphRetirementSlot unboundSlot;
        check(f.player.validateReadyGraph(empty, slot) == Status::emptyCandidate &&
                  other.player.validateReadyGraph(ready, foreignSlot) == Status::foreignOwner &&
                  f.player.validateReadyGraph(ready, foreignSlot) == Status::foreignRetirementSlot &&
                  f.player.validateReadyGraph(ready, unboundSlot) == Status::foreignRetirementSlot &&
                  f.process(.125f) && other.process(.125f),
              "empty_foreign_and_unbound_admission_refusals");
        bool refused = false, reentrant = false, borrowed = false;
        try
        {
            auto r = f.player.prepareFixedGraph(std::make_unique<Refusing>(f.counts));
        }
        catch (const std::runtime_error &)
        {
            refused = true;
        }
        try
        {
            auto r = f.player.prepareFixedGraph(std::make_unique<Reentrant>(f.counts, f.player));
        }
        catch (const std::logic_error &)
        {
            reentrant = true;
        }
        try
        {
            auto r = f.player.prepareFixedGraph(std::make_unique<Borrowed>(f.player.getNode()));
        }
        catch (const std::invalid_argument &)
        {
            borrowed = true;
        }
        check(refused && reentrant && borrowed && f.player.validateReadyGraph(ready, slot) == Status::ready &&
                  f.process(.125f),
              "prepare_failure_reentry_and_live_Node_refuse_keep_prior_ticket_valid");
    }
    {
        // A is truly adopted; B is posted but deliberately NOT processed yet.
        // C borrows A, which is absent from the last-posted B graph.
        Fixture f;
        const bool originalPCM = f.process(.125f);
        auto *actualA = f.player.getNode();
        auto queuedB = f.player.prepareFixedGraph(std::make_unique<Constant>(.75f, 37, f.counts));
        auto slot = f.player.makeGraphRetirementSlot();
        bool clean = true;
        const bool postedB = publishWithoutNewDelete(f.player, queuedB, slot, clean) == Status::ready;
        auto *posted = f.player.getNode();
        const auto prepared = f.counts.preparations, processed = f.counts.processes;
        bool refused = false;
        try
        {
            auto c = f.player.prepareFixedGraph(std::make_unique<Borrowed>(actualA));
        }
        catch (const std::invalid_argument &)
        {
            refused = true;
        }
        bool retainedPCM = actualA->hasProcessed();
        if (retainedPCM)
        {
            const auto audio = actualA->getProcessedOutput().audio;
            retainedPCM = audio.getNumChannels() == 1 && audio.getNumFrames() == 128;
            for (choc::buffer::FrameCount frame = 0; retainedPCM && frame < 128; ++frame)
                retainedPCM = audio.getSample(0, frame) == .125f;
        }
        const bool noMutation = f.counts.preparations == prepared && f.counts.processes == processed &&
                                f.player.getNode() == posted && f.player.getPreparedLatencySamples() == 37;
        check(originalPCM && postedB && clean && posted != actualA && refused && noMutation && retainedPCM &&
                  f.process(.75f),
              "queued_B_borrowed_actual_A_refuses_before_reinitialisation_keeps_both_exact_PCM");
    }
    for (int mutation = 0; mutation < 8; ++mutation)
    {
        Fixture f;
        auto ready = f.player.prepareFixedGraph(std::make_unique<Constant>(.75f, 37, f.counts));
        auto slot = f.player.makeGraphRetirementSlot();
        float expected = .125f;
        if (mutation == 0)
        {
            f.player.setNode(std::make_unique<Constant>(.25f, 13, f.counts), 48000., 128);
            expected = .25f;
        }
        if (mutation == 1)
        {
            f.player.clearNode();
            expected = 0;
        }
        if (mutation == 2)
            f.player.prepareToPlay(44100., 256);
        if (mutation == 3)
            f.player.setNumThreads(1);
        if (mutation == 4)
            f.player.enablePooledMemoryAllocations(true);
        if (mutation == 5)
            f.player.enableNodeMemorySharing(true);
        if (mutation == 6)
            f.player.setLatencyCompensationEnabled(false);
        if (mutation == 7)
        {
            f.player.enableNodeMemorySharing(true);
            f.player.enableNodeMemorySharing(false);
        }
        auto *current = f.player.getNode();
        const auto rate = f.player.getSampleRate();
        const auto block = f.player.getBlockSize();
        const auto latency = f.player.getPreparedLatencySamples();
        bool clean = true;
        const auto result = publishWithoutNewDelete(f.player, ready, slot, clean);
        check(result != Status::ready && !ready.empty() && slot.empty() && clean &&
                  f.player.getNode() == current && f.player.getSampleRate() == rate &&
                  f.player.getBlockSize() == block && f.player.getPreparedLatencySamples() == latency &&
                  f.process(expected),
              ("stale_mutation_" + std::to_string(mutation)).c_str());
        if (mutation == 4)
        {
            bool refused = false;
            try
            {
                auto r = f.player.prepareFixedGraph(std::make_unique<Constant>(.75f, 37, f.counts));
            }
            catch (const std::invalid_argument &)
            {
                refused = true;
            }
            check(refused && f.player.fixedGraphPreparationReadiness() == Status::pooledMemoryUnsupported &&
                      f.process(.125f),
                  "pooled_fixed_path_explicitly_refuses_without_disabling_pool");
        }
    }
    {
        // Same object address is NOT sufficient authority after destruction.
        Counts counts;
        alignas(Player) std::byte storage[sizeof(Player)];
        const auto creator = [&](Player &p) { return std::make_unique<CountingPool>(p, counts); };
        auto *first = new (storage) Player(creator);
        first->setNumThreads(0);
        first->setNode(std::make_unique<Constant>(.125f, 11, counts), 48000., 128);
        auto oldReady = first->prepareFixedGraph(std::make_unique<Constant>(.75f, 37, counts));
        first->~Player();
        auto *second = new (storage) Player(creator);
        second->setNumThreads(0);
        second->setNode(std::make_unique<Constant>(.25f, 13, counts), 48000., 128);
        auto slot = second->makeGraphRetirementSlot();
        check(second->validateReadyGraph(oldReady, slot) == Status::foreignOwner && !oldReady.empty(),
              "owner_address_ABA_refuses_with_retained_unique_identity");
        second->~Player(); // Independent original nodes outlive no external Edit/plugin.
    }
    // Actual C++new failure sweep, independent exact old PCM/config and normal
    // subsequent publication after EVERY reached site; no guessed allocation sizes.
    unsigned faultAttempts = 0, exceptions = 0, fallbacks = 0;
    bool faultPassed = true, exhausted = false;
    for (int site = 0; site < 512; ++site)
    {
        ++faultAttempts;
        Fixture f;
        require(f.process(.125f), "Fault sweep actual incumbent PCM");
        auto *old = f.player.getNode();
        auto node = std::make_unique<Constant>(.75f, 37, f.counts);
        Player::ReadyGraph prepared;
        const auto creates = f.counts.creates, clears = f.counts.clears, processes = f.counts.processes;
        preparationAllocationFailureHit = false;
        preparationAllocationFailureAfter = site;
        bool threw = false;
        try
        {
            prepared = f.player.prepareFixedGraph(std::move(node));
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
        const bool hit = preparationAllocationFailureHit;
        preparationAllocationFailureAfter = -1; // Before cleanup/assertion/evidence.
        const bool unchanged = f.player.getNode() == old && f.player.getPreparedLatencySamples() == 11 &&
                               f.player.getSampleRate() == 48000. && f.player.getBlockSize() == 128 &&
                               f.counts.creates == creates && f.counts.clears == clears &&
                               f.counts.processes == processes;
        auto slot = f.player.makeGraphRetirementSlot();
        const bool candidate =
            threw ? prepared.empty() : f.player.validateReadyGraph(prepared, slot) == Status::ready;
        const bool oldPCM = unchanged && f.process(.125f);
        auto next = f.player.prepareFixedGraph(std::make_unique<Constant>(.5f, 19, f.counts));
        bool clean = true;
        const bool replacement = publishWithoutNewDelete(f.player, next, slot, clean) == Status::ready &&
                                 clean && f.process(.5f) && f.player.retireReadyGraph(slot);
        exceptions += threw;
        fallbacks += hit && !threw;
        faultPassed = faultPassed && unchanged && candidate && oldPCM && replacement;
        report += "faultSite=" + juce::String(site) + " injected=" + juce::String(int(hit)) +
                  " badAlloc=" + juce::String(int(threw)) + " unchanged=" + juce::String(int(unchanged)) +
                  " candidate=" + juce::String(int(candidate)) + " exactOldPCM=" + juce::String(int(oldPCM)) +
                  " nextPublication=" + juce::String(int(replacement)) + "\n";
        if (!faultPassed || !hit)
        {
            exhausted = !hit;
            break;
        }
    }
    check(faultPassed && exhausted && exceptions >= 5,
          "all_actual_preparation_allocation_sites_preserve_incumbent");
    report += "cases=" + juce::String(cases) + " faultAttempts=" + juce::String(faultAttempts) +
              " allocationFailures=" + juce::String(exceptions) +
              " permittedFallbacks=" + juce::String(fallbacks) +
              " exhaustedSites=" + juce::String(int(exhausted)) + "\n";
    require(folder.getChildFile("ready-graph-verification.txt").replaceWithText(report),
            "Ready-graph results retained before aggregate assertion");
    require(passed, "Actual-player ready preparation/refusal/staleness/commit/retirement/PCM contracts");
}
