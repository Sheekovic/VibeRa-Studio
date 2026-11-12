// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <atomic>
namespace vibera
{
// Polyphony meter: how many sampler voices sounded in the last audio block. Each voice bank adds its
// active voices once per block (the epoch guards repeated slices of one block); the Master output
// publishes the sum and opens the next block. Plugin instruments keep their own voices and are not
// counted. Audio thread: atomics only, no allocation, no locks.
class PolyphonyMeter final
{
    std::atomic<int> gathering{0}, shown{0};
    std::atomic<unsigned> block{1};

  public:
    static PolyphonyMeter &instance() noexcept
    {
        static PolyphonyMeter meter;
        return meter;
    }
    // AUDIO, voice bank: `seen` is the bank's own last counted block.
    void add(int voices, unsigned &seen) noexcept
    {
        const auto now = block.load(std::memory_order_relaxed);
        if (seen == now)
            return;
        seen = now;
        if (voices > 0)
            gathering.fetch_add(voices, std::memory_order_relaxed);
    }
    // AUDIO, Master output: once per block, after every bank rendered.
    void publish() noexcept
    {
        shown.store(gathering.exchange(0, std::memory_order_relaxed), std::memory_order_relaxed);
        block.fetch_add(1, std::memory_order_relaxed);
    }
    int voices() const noexcept
    {
        return shown.load(std::memory_order_relaxed);
    }
    void reset() noexcept
    {
        gathering.store(0);
        shown.store(0);
    }
};
} // namespace vibera
