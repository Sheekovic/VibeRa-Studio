// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <atomic>
#include <cstdint>
#include <optional>

namespace vibera
{
struct CompensationGraphObservation
{
    std::uint64_t desired = 0, prepared = 0, adopted = 0;
    std::optional<std::int64_t> firstAdoptedReference;
};
struct CompensationGraphState
{
    std::atomic<std::uint64_t> desired{0}, prepared{0}, adopted{0}, revision{0};
    std::atomic<std::int64_t> firstReference{0};
    std::atomic<std::uint64_t> phaseFailures{0};
    CompensationGraphObservation read() const noexcept
    {
        CompensationGraphObservation result;
        result.desired = desired.load(std::memory_order_acquire);
        result.prepared = prepared.load(std::memory_order_acquire);
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            const auto before = revision.load();
            if ((before & 1) != 0)
                continue;
            const auto epoch = adopted.load();
            const auto reference = firstReference.load();
            if (revision.load() == before)
            {
                result.adopted = epoch;
                if (epoch != 0)
                    result.firstAdoptedReference = reference;
                break;
            }
        }
        return result;
    }
};

static_assert(std::atomic<std::int64_t>::is_always_lock_free);
} // namespace vibera
