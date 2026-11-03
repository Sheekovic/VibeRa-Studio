// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <atomic>
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <algorithm>

namespace vibera
{
// Prepared lane ownership. A producer publishes only an ordinal; it never
// touches another instrument's voices or allocates in the audio path.
using SamplerCutClock = std::atomic<std::uint64_t>;
static_assert(SamplerCutClock::is_always_lock_free);
struct SamplerCutSignal
{
    std::shared_ptr<SamplerCutClock> clock; // Bound before enabling the MIDI lane.
    std::atomic<std::uint64_t> lastOn{0};
    std::uint64_t next() noexcept
    {
        if (!clock)
            return 0;
        return clock->fetch_add(1, std::memory_order_relaxed) + 1;
    }
    void publish(std::uint64_t ordinal) noexcept
    {
        if (ordinal)
            lastOn.store(ordinal, std::memory_order_release);
    }
    std::uint64_t trigger() noexcept
    {
        const auto ordinal = next();
        publish(ordinal);
        return ordinal;
    }
};
struct SamplerCutInput
{
    std::int64_t channel = 0;
    std::shared_ptr<const SamplerCutSignal> signal;
    std::uint64_t admitted = 0;
    int lane = 0;
};
class SamplerCutMonitor
{
    struct Slot
    {
        const SamplerCutSignal *signal = nullptr;
        std::uint64_t observed = 0;
        std::int64_t channel = 0;
        int lane = 0;
    };
    std::array<Slot, 384> slots{}; // 128 Channels x MIDI/typing/preview.
    int group = -1;
    std::size_t size = 0;

  public:
    void reset() noexcept
    {
        slots = {};
        group = -1;
        size = 0;
    }
    std::uint64_t poll(int cutBy, std::span<const SamplerCutInput> sources) noexcept
    {
        if (group != cutBy)
        {
            slots = {};
            size = 0;
            group = cutBy;
        }
        const auto count = std::min(sources.size(), slots.size());
        bool rebind = count != size;
        for (std::size_t i = 0; !rebind && i < count; ++i)
            rebind = slots[i].signal != sources[i].signal.get() || slots[i].channel != sources[i].channel ||
                     slots[i].lane != sources[i].lane;
        if (rebind)
        {
            std::array<Slot, 384> next{};
            std::size_t old = 0;
            for (std::size_t i = 0; i < count; ++i)
            {
                const auto &route = sources[i];
                while (old < size && (slots[old].channel < route.channel ||
                                      (slots[old].channel == route.channel && slots[old].lane < route.lane)))
                    ++old;
                next[i] = old < size && slots[old].channel == route.channel &&
                                  slots[old].lane == route.lane && slots[old].signal == route.signal.get()
                              ? slots[old]
                              : Slot{route.signal.get(), route.admitted, route.channel, route.lane};
            }
            slots = next;
            size = count;
        }
        std::uint64_t latest = 0;
        for (std::size_t i = 0; i < count; ++i)
        {
            const auto &source = sources[i];
            auto &slot = slots[i];
            const auto ordinal = source.signal->lastOn.load(std::memory_order_acquire);
            if (ordinal > slot.observed)
                latest = std::max(latest, ordinal);
            slot.observed = ordinal;
        }
        return latest;
    }
};
} // namespace vibera
