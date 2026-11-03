// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <optional>
#include <limits>

namespace vibera
{
enum class PluginGesturePhase : unsigned
{
    begin,
    value,
    end
};
struct PluginGestureEvent
{
    std::uint32_t id = 0;
    unsigned generation = 0, epoch = 0;
    float value = 0;
    PluginGesturePhase phase = PluginGesturePhase::value;
    std::uint64_t completedTicks = 0; // Windows QPC; populated only for end notifications.
};
// Bounded MPSC queue. Producers can be plugin UI or processor threads. Each
// producer makes at most four attempts, never waits, and reports every loss.
// A reserved but unpublished head prevents reordering of begin/value/end.
class PluginGestureQueue
{
    static constexpr std::uint64_t capacity = 256;
    struct Slot
    {
        std::atomic<std::uint64_t> sequence{0};
        PluginGestureEvent event;
    };
    std::array<Slot, capacity> slots;
    std::atomic<std::uint64_t> writing{0};
    std::uint64_t reading = 0; // Exactly one control-thread consumer.

  public:
    std::atomic<std::uint64_t> dropped{0};
    PluginGestureQueue() noexcept
    {
        for (std::uint64_t i = 0; i < capacity; ++i)
            slots[i].sequence.store(i, std::memory_order_relaxed);
    }
    bool push(const PluginGestureEvent &event) noexcept
    {
        auto position = writing.load(std::memory_order_relaxed);
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            auto &slot = slots[position % capacity];
            const auto available = slot.sequence.load(std::memory_order_acquire);
            if (available == position)
            {
                if (writing.compare_exchange_strong(position, position + 1, std::memory_order_relaxed))
                {
                    slot.event = event;
                    slot.sequence.store(position + 1, std::memory_order_release);
                    return true;
                }
            }
            else if (available < position)
                break;
            else
                position = writing.load(std::memory_order_relaxed);
        }
        dropped.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    bool pop(PluginGestureEvent &event) noexcept
    {
        auto &slot = slots[reading % capacity];
        if (slot.sequence.load(std::memory_order_acquire) != reading + 1)
            return false;
        event = slot.event;
        slot.sequence.store(reading + capacity, std::memory_order_release);
        ++reading;
        return true;
    }
};
struct PluginGesture
{
    std::uint32_t id = 0;
    unsigned generation = 0;
    float before = 0, after = 0;
    std::uint64_t serial = 0;
    std::uint64_t completedTicks = 0;
    unsigned epoch = 0;
};
// Cross-worker control selection must not depend on mailbox polling order.
// Windows documents +/- one QPC tick as ambiguous across threads. Refuse an
// ambiguous pair instead of silently automating the wrong plugin.
class PluginGestureRecency
{
    std::uint64_t newest = 0;
    std::int64_t owner = 0;
    bool ambiguous = false;

  public:
    void consider(std::int64_t candidate, std::uint64_t ticks) noexcept
    {
        if (candidate <= 0 || ticks == 0)
            return;
        if (!newest)
        {
            owner = candidate;
            newest = ticks;
            return;
        }
        if (ticks > newest && ticks - newest > 1)
        {
            owner = candidate;
            newest = ticks;
            ambiguous = false;
        }
        else if (ticks >= newest || newest - ticks <= 1)
        {
            if (owner && owner != candidate)
                ambiguous = true;
            if (ticks > newest)
                newest = ticks;
        }
    }
    std::optional<std::int64_t> selected() const noexcept
    {
        return owner && !ambiguous ? std::optional(owner) : std::nullopt;
    }
    std::uint64_t newestTicks() const noexcept
    {
        return newest;
    }
};
// Control-thread interpretation. Unbracketed telemetry, incomplete gestures and
// stale schemas never select a user target. Loss invalidates the whole prefix.
class PluginGestureTracker
{
    struct Active
    {
        PluginGesture gesture;
        bool used = false, changed = false;
    };
    std::array<Active, 64> active{};
    std::optional<PluginGesture> last;
    unsigned generation = 0, epoch = 0;
    std::uint64_t serial = 0, dropped = 0, invalidations = 0;

  public:
    void reset() noexcept
    {
        active = {};
        last.reset();
    }
    void synchronize(unsigned schema, unsigned notificationEpoch, std::uint64_t lost) noexcept
    {
        if (generation != schema || epoch != notificationEpoch || dropped != lost)
            reset();
        generation = schema;
        epoch = notificationEpoch;
        dropped = lost;
    }
    // Every completion is returned even when QPC recency keeps a different latest target.
    std::optional<PluginGesture> consume(const PluginGestureEvent &event) noexcept
    {
        if (!generation || event.generation != generation || event.epoch != epoch)
            return {};
        if (!std::isfinite(event.value) || event.value < 0 || event.value > 1)
        {
            if (invalidations != std::numeric_limits<std::uint64_t>::max())
                ++invalidations;
            return {};
        }
        Active *entry = nullptr;
        for (auto &candidate : active)
            if (candidate.used && candidate.gesture.id == event.id)
                entry = &candidate;
        if (event.phase == PluginGesturePhase::begin)
        {
            if (entry)
            {
                reset();
                if (invalidations != std::numeric_limits<std::uint64_t>::max())
                    ++invalidations;
                return {};
            } // Malformed nested gesture, fail closed.
            for (auto &candidate : active)
                if (!candidate.used)
                {
                    entry = &candidate;
                    break;
                }
            if (!entry)
            {
                reset();
                if (invalidations != std::numeric_limits<std::uint64_t>::max())
                    ++invalidations;
                return {};
            }
            *entry = {{event.id, generation, event.value, event.value, 0}, true, false};
        }
        else if (entry && event.phase == PluginGesturePhase::value)
        {
            entry->gesture.after = event.value;
            entry->changed = true;
        }
        else if (entry && event.phase == PluginGesturePhase::end)
        {
            if (entry->changed && entry->gesture.before != entry->gesture.after)
            {
                if (serial == std::numeric_limits<std::uint64_t>::max())
                {
                    reset();
                    if (invalidations != std::numeric_limits<std::uint64_t>::max())
                        ++invalidations;
                    return {};
                }
                entry->gesture.serial = ++serial;
                entry->gesture.epoch = epoch;
                entry->gesture.completedTicks = event.completedTicks;
                if (!last || !last->completedTicks || !event.completedTicks ||
                    event.completedTicks >= last->completedTicks)
                    last = entry->gesture;
                const auto completed = entry->gesture;
                entry->used = false;
                return completed;
            }
            entry->used = false;
        }
        return {};
    }
    std::uint64_t invalidatedCompletions() const noexcept
    {
        return invalidations;
    }
    const std::optional<PluginGesture> &latest() const noexcept
    {
        return last;
    }
};
// CONTROL-only retained completions, independent of Last Tweaked selection.
// Overflow/invalidation quarantines the whole pending prefix and is observable.
class PluginCompletedGestures
{
    std::array<PluginGesture, 256> entries{};
    std::size_t first = 0, count = 0;
    std::uint64_t lost = 0;

  public:
    void invalidate(std::uint64_t reasonUnits = 1) noexcept
    {
        const auto maximum = std::numeric_limits<std::uint64_t>::max();
        const auto discarded = std::uint64_t(count);
        const auto extra = reasonUnits > maximum - discarded ? maximum : reasonUnits + discarded;
        lost = extra > maximum - lost ? maximum : lost + extra;
        first = count = 0;
    }
    bool push(const PluginGesture &gesture) noexcept
    {
        if (count == entries.size())
        {
            invalidate();
            return false;
        }
        entries[(first + count) % entries.size()] = gesture;
        ++count;
        return true;
    }
    bool pop(PluginGesture &output) noexcept
    {
        if (!count)
            return false;
        output = entries[first];
        first = (first + 1) % entries.size();
        --count;
        return true;
    }
    std::uint64_t losses() const noexcept
    {
        return lost;
    }
};

static_assert(std::atomic<std::uint64_t>::is_always_lock_free && std::atomic<float>::is_always_lock_free);
} // namespace vibera
