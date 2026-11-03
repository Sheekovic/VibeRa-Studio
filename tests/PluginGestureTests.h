// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PluginGestures.h"
#include <thread>
#include <vector>
#include <set>
#include <limits>
#include <memory>
static void pluginGestureCoreTests()
{
    // Different workers may be drained in any order; QPC completion time, not
    // iteration order or each worker's independent serial, chooses the target.
    for (auto ordering : {std::array{0, 1, 2}, std::array{2, 1, 0}, std::array{1, 2, 0}})
    {
        PluginGestureRecency recency;
        constexpr std::array<std::uint64_t, 3> times{300, 100, 700};
        for (int index : ordering)
            recency.consider(index + 1, times[size_t(index)]);
        require(recency.selected() == 3, "global native target follows completion time across poll orders");
        recency.consider(4, 699);
        require(!recency.selected(), "cross-thread one-tick ambiguity refuses an arbitrary native target");
        recency.consider(5, 900);
        require(recency.selected() == 5, "fresh separated native gesture recovers ambiguous selection");
    }
    PluginGestureRecency empty;
    empty.consider(1, 0);
    empty.consider(0, 50);
    require(!empty.selected(), "zero timestamps and invalid owners cannot select a global target");
    empty.consider(1, 1);
    require(empty.selected() == 1, "first nonzero QPC timestamp is admitted");
    empty.consider(2, 1);
    require(!empty.selected(), "identical cross-worker completion times refuse selection");
    PluginGestureTracker tracker;
    tracker.synchronize(1, 1, 0);
    tracker.consume({0, 1, 1, .4f, PluginGesturePhase::value});
    require(!tracker.latest(), "unbracketed telemetry is not a user gesture");
    tracker.consume({0, 1, 1, .5f, PluginGesturePhase::begin});
    tracker.consume({0, 1, 1, .8f, PluginGesturePhase::value});
    require(!tracker.latest(), "incomplete native gesture does not select last tweaked");
    tracker.consume({0, 1, 1, .8f, PluginGesturePhase::end, 100});
    require(tracker.latest() && tracker.latest()->id == 0 && tracker.latest()->before == .5f &&
                tracker.latest()->after == .8f && tracker.latest()->completedTicks == 100,
            "native zero ID and bracketed first/final values");
    tracker.consume({0xfedcba98u, 1, 1, .2f, PluginGesturePhase::begin});
    tracker.consume({0xfedcba98u, 1, 1, .4f, PluginGesturePhase::value});
    tracker.consume({0xfedcba98u, 1, 1, .9f, PluginGesturePhase::value});
    tracker.consume({0xfedcba98u, 1, 1, .9f, PluginGesturePhase::end});
    require(tracker.latest()->id == 0xfedcba98u && tracker.latest()->serial == 2 &&
                tracker.latest()->before == .2f && tracker.latest()->after == .9f,
            "sparse native ID and one completion serial per entire gesture");
    tracker.synchronize(2, 1, 0);
    tracker.consume({0, 1, 1, .1f, PluginGesturePhase::begin});
    tracker.consume({0, 1, 1, .2f, PluginGesturePhase::value});
    tracker.consume({0, 1, 1, .2f, PluginGesturePhase::end});
    require(!tracker.latest(), "stale schema cannot resurrect last tweaked");
    tracker.synchronize(2, 2, 0);
    tracker.consume({0, 2, 1, .1f, PluginGesturePhase::begin});
    tracker.consume({0, 2, 1, .2f, PluginGesturePhase::value});
    tracker.consume({0, 2, 1, .2f, PluginGesturePhase::end});
    require(!tracker.latest(), "disabled/re-enabled notification epoch rejects queued stale gesture");
    tracker.consume({0, 2, 2, .1f, PluginGesturePhase::begin});
    tracker.consume({0, 2, 2, std::numeric_limits<float>::quiet_NaN(), PluginGesturePhase::value});
    tracker.consume({0, 2, 2, .2f, PluginGesturePhase::end});
    require(!tracker.latest(), "malformed values cannot produce completed changed gesture");
    // Independently choose a reverse completion order for simultaneous controls.
    tracker.synchronize(3, 2, 0);
    for (unsigned i = 0; i < 32; ++i)
    {
        tracker.consume({i, 3, 2, .1f, PluginGesturePhase::begin});
        tracker.consume({i, 3, 2, .7f, PluginGesturePhase::value});
    }
    for (unsigned i = 32; i-- > 0;)
    {
        tracker.consume({i, 3, 2, .7f, PluginGesturePhase::end});
        require(tracker.latest() && tracker.latest()->id == i && tracker.latest()->before == .1f &&
                    tracker.latest()->after == .7f,
                "overlapping native controls retain independent boundaries");
    }
    tracker.consume({100, 3, 2, .2f, PluginGesturePhase::begin});
    tracker.synchronize(3, 2, 1);
    tracker.consume({100, 3, 2, .8f, PluginGesturePhase::value});
    tracker.consume({100, 3, 2, .8f, PluginGesturePhase::end});
    require(!tracker.latest(), "loss discards in-flight overlapping prefix");
    auto queue = std::make_unique<PluginGestureQueue>();
    callbackAllocations = callbackDestructions = 0;
    callbackAllocationProbe = true;
    unsigned accepted = 0;
    for (unsigned i = 0; i < 300; ++i)
        accepted += queue->push({i, 1, 1, .5f, PluginGesturePhase::value});
    PluginGestureEvent event;
    unsigned consumed = 0;
    bool ordered = true;
    while (queue->pop(event))
        ordered &= event.id == consumed++;
    callbackAllocationProbe = false;
    require(accepted == 256 && consumed == 256 && ordered && queue->dropped == 44,
            "bounded queue preserves FIFO prefix and explicitly counts every overflow");
    require(callbackAllocations == 0 && callbackDestructions == 0,
            "notification producer/consumer has zero observed C++ allocation/destruction");
    std::atomic<int> done{0};
    std::array<unsigned, 4> successes{};
    std::array<std::thread, 4> producers;
    std::set<unsigned> received;
    queue = std::make_unique<PluginGestureQueue>();
    for (unsigned p = 0; p < producers.size(); ++p)
        producers[p] = std::thread(
            [&, p]
            {
                for (unsigned i = 0; i < 10000; ++i)
                    successes[p] += queue->push({p * 10000 + i, 1, 1, .5f, PluginGesturePhase::value});
                ++done;
            });
    bool unique = true;
    while (done != 4)
        if (queue->pop(event))
            unique &= received.insert(event.id).second;
        else
            std::this_thread::yield();
    for (auto &producer : producers)
        producer.join();
    while (queue->pop(event))
        unique &= received.insert(event.id).second;
    unsigned total = 0;
    for (auto value : successes)
        total += value;
    require(unique && received.size() == total && total + queue->dropped == 40000,
            "four concurrent producers: exact accepted/dropped accounting and no duplicate/corrupt payload");
}
