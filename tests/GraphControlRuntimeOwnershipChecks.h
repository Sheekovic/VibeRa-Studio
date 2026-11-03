// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "GraphControlRuntime.h"
#include <utility>

namespace vibera::test
{
// Original CONTROL-ownership checks. Deliberately not a DSP/player test.
// The integrating acceptance harness supplies its actual fail-fast require.
template <typename Require> void graphControlRuntimeOwnershipChecks(Require &&require)
{
    using namespace detail;
    using P = GraphControlRuntimeAttachment::Preparation;
    GraphControlRuntimeState state;
    GraphControlRuntimeAttachment source, discarded, successor;
    auto rejects = [](auto &&operation)
    {
        try
        {
            operation();
        }
        catch (const std::logic_error &)
        {
            return true;
        }
        return false;
    };
    require(source.privateConfiguration(state), "fresh owner accepts private configuration");
    require(source.preflight(state, 48000, 256, true) == P::fresh, "first initialization is fresh");
    source.complete(state, 48000, 256, P::fresh);
    const auto epoch = state.epoch;
    require(state.shells == 1 && source.isAttachedControl() && state.canRetain(),
            "initialized source owns one attachment");
    discarded.retain(state);
    require(!discarded.privateConfiguration(state), "pending replacement cannot reconfigure shared owner");
    require(rejects([&] { discarded.preflight(state, 44100, 256, true); }), "rate mismatch is refused");
    require(rejects([&] { discarded.preflight(state, 48000, 128, true); }), "block mismatch is refused");
    require(rejects([&] { discarded.preflight(state, 48000, 256, false); }), "identity mismatch is refused");
    require(state.shells == 1 && state.epoch == epoch, "refusal does not attach or reprepare source");
    require(discarded.preflight(state, 48000, 256, true) == P::retained,
            "compatible preparation is retained");
    discarded.complete(state, 48000, 256, P::retained);
    require(state.shells == 2 && state.epoch == epoch, "retained prepare does not advance DSP epoch");
    require(!discarded.detach(state) && state.shells == 1 && state.prepared,
            "discard leaves incumbent prepared");
    successor.retain(state);
    successor.complete(state, 48000, 256, successor.preflight(state, 48000, 256, true));
    require(!source.detach(state) && state.shells == 1 && successor.isAttachedControl(),
            "incumbent retirement leaves adopted owner intact");
    require(!source.detach(state) && state.shells == 1, "duplicate deinitialization is harmless");
    require(successor.detach(state) && !state.prepared && state.shells == 0,
            "only final retirement marks runtime unprepared");
    require(rejects([&] { discarded.preflight(state, 0, 256, true); }), "nonpositive rate is refused");
    GraphControlRuntimeState stale;
    GraphControlRuntimeAttachment first, delayed;
    first.complete(stale, 48000, 256, first.preflight(stale, 48000, 256, true));
    delayed.retain(stale);
    first.detach(stale);
    first.complete(stale, 48000, 256, first.preflight(stale, 48000, 256, true));
    require(rejects([&] { delayed.preflight(stale, 48000, 256, true); }),
            "stale retained admission cannot cross a source reprepare");
    require(stale.shells == 1 && first.isAttachedControl(),
            "stale refusal preserves current initialized source");
    first.detach(stale);
    GraphControlRuntimeState exhausted;
    exhausted.shells = 1;
    exhausted.prepared = true;
    exhausted.rate = 48000;
    exhausted.block = 256;
    exhausted.epoch = std::numeric_limits<std::uint64_t>::max();
    require(!exhausted.canRetain(), "exhausted epoch cannot issue a reusable identity");
}
} // namespace vibera::test
