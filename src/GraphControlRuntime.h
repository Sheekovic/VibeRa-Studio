// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace vibera::detail
{
// CONTROL-only metadata. Does not own/copy DSP, acquire a document, infer
// lifetime from use_count, or run in an audio callback. One serialized owner.
struct GraphControlRuntimeState
{
    unsigned shells = 0;
    bool prepared = false;
    double rate = 0;
    int block = 0;
    std::uint64_t epoch = 0;
    bool canRetain() const noexcept
    {
        return prepared && shells != 0 && epoch != std::numeric_limits<std::uint64_t>::max();
    }
};
class GraphControlRuntimeAttachment
{
    bool attached = false, pending = false;
    std::uint64_t expectedEpoch = 0;

  public:
    enum class Preparation
    {
        fresh,
        retained
    };
    bool isAttachedControl() const noexcept
    {
        return attached;
    }
    bool privateConfiguration(const GraphControlRuntimeState &state) const noexcept
    {
        return !attached && !pending && state.shells == 0;
    }
    void requirePrivateConfiguration(const GraphControlRuntimeState &state) const
    {
        if (!privateConfiguration(state))
            throw std::logic_error(
                "controlRuntime.configurationInUse: Immutable graph binding is already attached or pending");
    }
    void retain(const GraphControlRuntimeState &state) noexcept
    {
        expectedEpoch = state.epoch;
        pending = true;
    }
    Preparation preflight(const GraphControlRuntimeState &state, double rate, int block,
                          bool immutableBindingMatches) const
    {
        if (!std::isfinite(rate) || rate <= 0 || block <= 0)
            throw std::logic_error("controlRuntime.invalidDevice: Positive finite graph rate/block required");
        if (pending && (!state.canRetain() || state.epoch != expectedEpoch))
            throw std::logic_error(
                "controlRuntime.staleSource: Source preparation changed before attachment");
        if ((!attached && state.shells > 0) || (attached && state.shells > 1))
        {
            if (!state.prepared || state.rate != rate || state.block != block || !immutableBindingMatches ||
                (!attached && state.shells == std::numeric_limits<unsigned>::max()))
                throw std::logic_error(
                    "controlRuntime.incompatibleSource: Shared identity/rate/block cannot change");
            return Preparation::retained;
        }
        return Preparation::fresh;
    }
    // Caller has finished any fallible fresh DSP preparation. Retained attach
    // calls ONLY this value-only completion, with no DSP reads/writes/reset.
    void complete(GraphControlRuntimeState &state, double rate, int block, Preparation mode) noexcept
    {
        if (mode == Preparation::fresh)
        {
            state.rate = rate;
            state.block = block;
            if (state.epoch != std::numeric_limits<std::uint64_t>::max())
                ++state.epoch;
            state.prepared = true;
        }
        if (!attached)
        {
            ++state.shells;
            attached = true;
        }
        pending = false;
    }
    bool detach(GraphControlRuntimeState &state) noexcept
    {
        if (!attached)
            return false;
        attached = false;
        if (state.shells == 0 || --state.shells != 0)
            return false;
        state.prepared = false;
        return true; // Last initialized shell, after graph readers quiesce.
    }
};
} // namespace vibera::detail
