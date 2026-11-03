// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PluginControlRequests.h"
static void pluginControlRequestsTests()
{
    PluginControlRequests requests;
    const auto first = requests.reserve(0, 100);
    const auto second = requests.reserve(0xfedcba98u, 200);
    require(first && second && first != second, "control receipts have distinct nonzero tokens");
    require(requests.complete({second, 0xfedcba98u, .75f, 0}) && requests.complete({first, 0, .25f, 0}),
            "different caller results may arrive in a different order");
    PluginParameterResult out{999, 42, .9f, 3};
    require(!requests.consume(12345, out) && out.serial == 999 && out.id == 42 && out.value == .9f,
            "unowned token neither steals replies nor changes output");
    require(requests.consume(first, out) && out.id == 0 && out.value == .25f &&
                requests.consume(second, out) && out.id == 0xfedcba98u && out.value == .75f &&
                !requests.consume(first, out),
            "each exact owner receives its original native ID/value once");
    const auto invalid = requests.reserve(0, 300);
    require(!requests.complete({invalid, 7, .5f, 0}) &&
                !requests.complete({invalid, 0, std::numeric_limits<float>::quiet_NaN(), 0}) &&
                !requests.complete({invalid, 0, .5f, 99}) && !requests.ready(invalid),
            "wrong ID, malformed value and unknown worker outcome cannot forge a reply");
    require(requests.complete({invalid, 0, .5f, 2}) && !requests.complete({invalid, 0, .5f, 2}),
            "valid worker refusal is retained and duplicate acknowledgement rejected");
    requests.abandon(invalid);
    const auto pending = requests.reserve(0, 400);
    requests.abandon(pending);
    require(requests.outstanding() == 1 && requests.complete({pending, 0, .4f, 0}) &&
                requests.outstanding() == 0 && !requests.ready(pending),
            "abandoned in-flight receipt drains its physical response before reclaiming ownership");
    std::array<unsigned, 64> tokens{};
    for (unsigned i = 0; i < tokens.size(); ++i)
        tokens[i] = requests.reserve(i, 500);
    require(tokens.back() && requests.outstanding() == 64 && requests.reserve(128, 500) == 0,
            "bounded receipt capacity refuses admission instead of overwriting another caller");
    for (auto token : tokens)
        requests.abandon(token);
    requests.failPending(4);
    require(requests.outstanding() == 0, "worker loss releases abandoned requests");
    const auto finished = requests.reserve(1, 600), failed = requests.reserve(2, 600);
    requests.complete({finished, 1, .8f, 0});
    requests.failPending(6);
    require(requests.consume(finished, out) && out.result == 0 && out.value == .8f &&
                requests.consume(failed, out) && out.result == 6 && out.id == 2 && out.value == 0,
            "timeout completes only outstanding requests, preserving completed acknowledgement data");
    PluginControlRequests wrap;
    const auto nearWrap = wrap.reserve(0, std::numeric_limits<unsigned>::max() - 10);
    require(nearWrap && !wrap.expired(10, 21) && wrap.expired(10, 20),
            "control deadline accounts for unsigned Windows millisecond wrap");
    PluginControlRequests exhausted(std::numeric_limits<unsigned>::max() - 1);
    const auto last = exhausted.reserve(0, 0);
    require(last == std::numeric_limits<unsigned>::max() && exhausted.reserve(0, 0) == 0,
            "wire tokens never wrap into zero or reuse a previous caller identity");
    callbackAllocations = callbackDestructions = 0;
    callbackAllocationProbe = true;
    PluginControlRequests bounded;
    const auto token = bounded.reserve(0, 100);
    bounded.complete({token, 0, .2f, 0});
    const bool consumed = bounded.consume(token, out);
    callbackAllocationProbe = false;
    require(consumed && callbackAllocations == 0 && callbackDestructions == 0,
            "bounded receipt bookkeeping has zero observed C++ allocation/destruction");
    // The last probe is not permission to call control bookkeeping from DSP.
}
