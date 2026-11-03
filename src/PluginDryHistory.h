// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PluginLatency.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>

namespace vibera
{
class PluginHost;
struct PluginDryHistoryFixture;

// The ACTUAL pre-effect dry ring previously embedded in PluginHost::Impl.
// Construct/pin/destroy on CONTROL. Every payload/witness method below belongs
// to ONE serialized DSP owner. CONTROL may read only phase(), never witness().
// This is not a native graph/source/reference-clock or retirement authority.
class PluginDryHistory final
{
  public:
    static constexpr int capacity = 131072;
    using Ptr = std::shared_ptr<PluginDryHistory>;
    enum class Phase : std::uint32_t
    {
        host,
        continuation
    };
    enum class Status
    {
        ready,
        wrongOwner,
        alreadyClaimed,
        staleWitness,
        incompleteHistory,
        delayChanged,
        nativeChanged,
        notDry,
        unavailable,
        invalidSpan,
        overlappingChannels,
        counterExhausted
    };
    struct Witness
    {
        std::uint64_t spans = 0, frames = 0;
        int knownFrames = 0, delay = 0;
        PluginLatencyStamp native;
        bool completedWet = false, dryEndpoint = false, exhausted = false;
        bool operator==(const Witness &) const = default;
    };

    explicit PluginDryHistory(const void *hostOwner) noexcept : original(hostOwner), writer(hostOwner) {}
    PluginDryHistory(const PluginDryHistory &) = delete;
    PluginDryHistory &operator=(const PluginDryHistory &) = delete;
    Phase phase() const noexcept
    {
        return observed.load(std::memory_order_acquire);
    }

    // DSP-only. Call AFTER the exact Host span, never infer this from a native
    // reference range, submission count or CONTROL-declared latency.
    Witness witness() const noexcept
    {
        return last;
    }

    // First selected continuation prefetch, AFTER its real root/source/Host
    // ownership checks. This validates the latest actual Host dry tap and exact
    // complete consumed stamp. The external root still proves physical/native
    // continuity and exclusive processing; these logical frames do not.
    // A refused attempt changes NO payload, owner, witness or phase.
    Status claim(const void *expectedHost, const void *nextWriter, const Witness &expected, int delay,
                 PluginLatencyStamp native, int maximumSpan) noexcept
    {
        if (writer != original)
            return Status::alreadyClaimed;
        if (!original || expectedHost != original || !nextWriter || nextWriter == original)
            return Status::wrongOwner;
        if (last != expected)
            return Status::staleWitness;
        if (last.exhausted)
            return Status::counterExhausted;
        if (delay < 0 || delay >= capacity || maximumSpan <= 0 || maximumSpan > capacity)
            return Status::invalidSpan;
        if (last.delay != delay)
            return Status::delayChanged;
        if (last.native != native || native.generation == 0 || native.nativeSamples < 0)
            return Status::nativeChanged;
        if (!last.completedWet)
            return Status::unavailable;
        if (!last.dryEndpoint)
            return Status::notDry;
        if (last.knownFrames < delay)
            return Status::incompleteHistory;
        writer = nextWriter;
        continuationDelay = delay;
        continuationMaximum = maximumSpan;
        observed.store(Phase::continuation, std::memory_order_release);
        return Status::ready;
    }

    // In-place, bounded, allocation-free pure dry continuation. Caller prepares
    // genuine writable/disjoint stereo spans. All detectable refusal happens
    // before either audio channel, ring cursor or witness changes. The worker
    // and PluginHost may be reclaimed only after the separate real source-bank
    // close ACK; this ring has no Host/worker/Plugin/Edit owning backreference.
    Status render(const void *owner, float *left, float *right, int count) noexcept
    {
        if (!owner || writer == original || writer != owner)
            return Status::wrongOwner;
        if (count < 0 || count > continuationMaximum)
            return Status::invalidSpan;
        if (count == 0)
            return Status::ready;
        if (!left || !right)
            return Status::invalidSpan;
        const auto a = reinterpret_cast<std::uintptr_t>(left);
        const auto b = reinterpret_cast<std::uintptr_t>(right);
        constexpr auto maximum = std::numeric_limits<std::uintptr_t>::max();
        if (std::uint64_t(count) > maximum / sizeof(float))
            return Status::invalidSpan;
        const auto bytes = std::uintptr_t(count) * sizeof(float);
        if (a > maximum - bytes || b > maximum - bytes)
            return Status::invalidSpan;
        if (a < b + bytes && b < a + bytes)
            return Status::overlappingChannels;
        if (last.exhausted || last.spans == std::numeric_limits<std::uint64_t>::max() ||
            std::uint64_t(count) > std::numeric_limits<std::uint64_t>::max() - last.frames)
            return Status::counterExhausted;
        for (int n = 0; n < count; ++n)
        {
            write(0, left[n]);
            write(1, right[n]);
            left[n] = tap(0, continuationDelay);
            right[n] = tap(1, continuationDelay);
            advance();
        }
        record(count, continuationDelay, last.native, true, true);
        return Status::ready;
    }

  private:
    friend class PluginHost;
    friend struct PluginDryHistoryFixture;
    // Canonical Host operations retain their original exact write-before-read
    // ordering, modulo arithmetic, startup zeroes and two-channel behavior.
    void write(int channel, float sample) noexcept
    {
        samples[std::size_t(channel)][std::size_t(cursor)] = sample;
    }
    float tap(int channel, int delay) const noexcept
    {
        return samples[std::size_t(channel)][std::size_t((cursor - delay + capacity) % capacity)];
    }
    void advance() noexcept
    {
        cursor = (cursor + 1) % capacity;
    }
    bool hostOwns(const void *owner) const noexcept
    {
        return original && writer == original && owner == original;
    }
    int hostCursor() const noexcept
    {
        return cursor;
    } // Existing fresh-promotion exclusion only.
    void record(int count, int delay, PluginLatencyStamp native, bool complete, bool dry) noexcept
    {
        if (count <= 0)
            return;
        if (last.exhausted || last.spans == std::numeric_limits<std::uint64_t>::max() ||
            std::uint64_t(count) > std::numeric_limits<std::uint64_t>::max() - last.frames)
        {
            last.exhausted = true; // Optional handoff unavailable; ordinary PCM is unchanged.
            return;
        }
        ++last.spans;
        last.frames += std::uint64_t(count);
        last.knownFrames =
            int(std::min<std::uint64_t>(capacity, std::uint64_t(last.knownFrames) + std::uint64_t(count)));
        last.delay = delay;
        last.native = native;
        last.completedWet = complete;
        last.dryEndpoint = dry;
    }
    const void *const original;
    const void *writer; // Sole DSP owner, never read/written by CONTROL.
    std::array<std::array<float, capacity>, 2> samples{};
    int cursor = 0, continuationDelay = 0, continuationMaximum = 0;
    Witness last;
    std::atomic<Phase> observed{Phase::host};
    static_assert(std::atomic<Phase>::is_always_lock_free);
};
} // namespace vibera
