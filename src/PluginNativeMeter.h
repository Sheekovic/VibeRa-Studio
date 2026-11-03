// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>

namespace vibera
{
enum class PluginNativeMeterStatus : unsigned
{
    unsupported,
    notProcessed,
    ready,
    invalid,
    unavailable
};

struct PluginNativeMeterReading
{
    PluginNativeMeterStatus status = PluginNativeMeterStatus::unavailable;
    float leftPeak = 0, rightPeak = 0;
    std::uint64_t synthState = 0; // Optional owned-synth last-block telemetry; zero means unsupported.
};

inline constexpr const char *pluginNativeMeterStatusName(PluginNativeMeterStatus status) noexcept
{
    switch (status)
    {
    case PluginNativeMeterStatus::unsupported:
        return "unsupported";
    case PluginNativeMeterStatus::notProcessed:
        return "notProcessed";
    case PluginNativeMeterStatus::ready:
        return "ready";
    case PluginNativeMeterStatus::invalid:
        return "invalid";
    case PluginNativeMeterStatus::unavailable:
        return "unavailable";
    }
    return "invalid";
}

// One worker CONTROL publisher. No DSP code calls this transport. The pair is
// one atomic observation, but the underlying native channel atomics are NOT a
// packet-coherent timestamp. Peaks retain the last processed block while stopped.
struct PluginNativeMeterTelemetry
{
    std::atomic<std::uint64_t> peaks{0};
    std::atomic<std::uint64_t> synth{0};
    std::atomic<PluginNativeMeterStatus> status{PluginNativeMeterStatus::unsupported};

    void publish(PluginNativeMeterStatus next, float left = 0, float right = 0,
                 std::uint64_t synthState = 0) noexcept
    {
        synth.store(synthState, std::memory_order_release);
        if (next == PluginNativeMeterStatus::ready)
        {
            if (!std::isfinite(left) || !std::isfinite(right) || left < 0 || right < 0)
            {
                status.store(PluginNativeMeterStatus::invalid, std::memory_order_release);
                return; // Keep the prior valid payload for an already-started reader.
            }
            peaks.store(std::uint64_t(std::bit_cast<std::uint32_t>(left)) |
                            (std::uint64_t(std::bit_cast<std::uint32_t>(right)) << 32),
                        std::memory_order_release);
        }
        status.store(next, std::memory_order_release);
    }

    PluginNativeMeterReading read() const noexcept
    {
        const auto observed = status.load(std::memory_order_acquire);
        switch (observed)
        {
        case PluginNativeMeterStatus::unsupported:
        case PluginNativeMeterStatus::notProcessed:
        case PluginNativeMeterStatus::invalid:
        case PluginNativeMeterStatus::unavailable:
            return {observed, 0, 0,
                    observed == PluginNativeMeterStatus::notProcessed ? synth.load(std::memory_order_acquire)
                                                                      : 0};
        case PluginNativeMeterStatus::ready:
            break;
        default:
            return {PluginNativeMeterStatus::invalid, 0, 0};
        }
        const auto pair = peaks.load(std::memory_order_acquire);
        const float left = std::bit_cast<float>(std::uint32_t(pair));
        const float right = std::bit_cast<float>(std::uint32_t(pair >> 32));
        if (!std::isfinite(left) || !std::isfinite(right) || left < 0 || right < 0)
            return {PluginNativeMeterStatus::invalid, 0, 0};
        return {PluginNativeMeterStatus::ready, left, right, synth.load(std::memory_order_acquire)};
    }
};
static_assert(std::atomic<PluginNativeMeterStatus>::is_always_lock_free &&
              std::atomic<std::uint64_t>::is_always_lock_free);
} // namespace vibera
