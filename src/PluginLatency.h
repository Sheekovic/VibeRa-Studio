// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <bit>
#include <cstdint>

namespace vibera
{
// One coherent native declaration. Generation is scoped to a worker instance;
// it describes processed PCM, never application graph adoption.
struct PluginLatencyStamp
{
    int nativeSamples = 0;
    std::uint32_t generation = 0;
    constexpr bool valid(int quantum) const noexcept
    {
        return generation != 0 && quantum >= 16 && quantum <= 8192 && nativeSamples >= 0 &&
               nativeSamples < 131072 - quantum;
    }
    constexpr std::uint64_t packed() const noexcept
    {
        return (std::uint64_t(generation) << 32) | std::uint32_t(nativeSamples);
    }
    static constexpr PluginLatencyStamp unpack(std::uint64_t value) noexcept
    {
        return {std::bit_cast<std::int32_t>(std::uint32_t(value)), std::uint32_t(value >> 32)};
    }
    constexpr PluginLatencyStamp next(int samples) const noexcept
    {
        auto nextGeneration = generation + 1;
        if (nextGeneration == 0)
            nextGeneration = 1;
        return samples == nativeSamples ? *this : PluginLatencyStamp{samples, nextGeneration};
    }
    bool operator==(const PluginLatencyStamp &) const = default;
};
struct PluginConsumedLatency
{
    PluginLatencyStamp stamp;
    std::uint64_t sequence = 0;
};
} // namespace vibera
