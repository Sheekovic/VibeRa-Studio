// SPDX-License-Identifier: AGPL-3.0-or-later
// Original bounded short-message experiment. Production admission remains OFF.
#pragma once
#include "CompensationHistory.h"
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

namespace vibera::dsp
{
struct MidiHistoryLimits
{
    std::uint32_t events = 0, sources = 0, outputEvents = 0;
    std::uint64_t epoch = 0;
    std::size_t graphBytes = 0;
    bool enabled() const noexcept
    {
        return events != 0;
    }
    bool operator==(const MidiHistoryLimits &) const = default;
};
struct MidiHistoryConfiguration
{
    CompensationEdge edge;
    double sampleRate = 0;
    std::uint32_t maximumDelay = 0, maximumBlock = 0;
    MidiHistoryLimits limits;
    bool operator==(const MidiHistoryConfiguration &) const = default;
};
enum class MidiHistoryFailure : std::uint32_t
{
    none = 0,
    invalidBlock = 1,
    invalidTimestamp = 2,
    unsupportedPayload = 4,
    eventCapacity = 8,
    sourceCapacity = 16,
    outputCapacity = 32,
    gateCapacity = 64,
    referenceDiscontinuity = 128,
    sequenceExhausted = 256,
    deadlineInversion = 512
};
struct RetainedShortMidi
{
    std::int64_t due = 0;
    std::uint64_t sequence = 0;
    std::uint32_t source = 0;
    std::array<std::uint8_t, 3> bytes{};
    std::uint8_t size = 0;
};
struct MidiHistoryObservation
{
    std::uint32_t failures = 0;
    std::uint64_t panics = 0, rejected = 0, preparedEvents = 0;
};
struct MidiHistoryBatch
{
    std::span<const RetainedShortMidi> events;
    std::int64_t reference = 0;
    bool allNotesOff = false;
};
// Allocate/copy owner handles during preparation; one adopted graph writes.
// No control-side access to queue/gates/cursor. No callback shared_ptr traffic.
class MidiCompensationHistory
{
    struct Source
    {
        std::uint32_t id = 0;
        bool used = false;
        std::array<std::uint16_t, 16 * 128> gates{};
    };
    const MidiHistoryConfiguration config;
    std::vector<RetainedShortMidi> queue, staged;
    std::vector<Source> sources;
    std::size_t count = 0, stagedCount = 0;
    std::uint64_t sequence = 0;
    std::int64_t reference = 0, nextReference = 0;
    std::uint32_t frames = 0;
    bool haveReference = false, validBlock = false, panic = false;
    std::atomic<std::uint32_t> failures{0};
    std::atomic<std::uint64_t> panics{0}, rejected{0}, preparedEvents{0};
    static bool note(const RetainedShortMidi &e) noexcept
    {
        return e.size == 3 && ((e.bytes[0] & 0xf0) == 0x80 || (e.bytes[0] & 0xf0) == 0x90);
    }
    static bool before(const RetainedShortMidi &a, const RetainedShortMidi &b) noexcept
    {
        return a.due < b.due || (a.due == b.due && a.sequence < b.sequence);
    }
    void erase(std::size_t index) noexcept
    {
        for (auto n = index + 1; n < count; ++n)
            queue[n - 1] = queue[n];
        --count; // POD slots retain their storage; no object destruction.
    }
    void cancelNotes(int channel, std::uint64_t fence) noexcept
    {
        for (std::size_t n = 0; n < count;)
            if (note(queue[n]) && (channel < 0 || (queue[n].bytes[0] & 15) == channel) &&
                queue[n].sequence <= fence)
                erase(n);
            else
                ++n;
        for (auto &s : sources)
            if (channel < 0)
                s.gates.fill(0);
            else
                std::fill_n(s.gates.begin() + channel * 128, 128, std::uint16_t(0));
    }
    void safetyPanic() noexcept
    {
        panic = true;
        panics.fetch_add(1, std::memory_order_relaxed);
        cancelNotes(-1, sequence);
        // An untimed panic is applied before output messages. Retract earlier
        // staged notes too, otherwise a selected NoteOn could reopen a gate.
        for (std::size_t n = 0; n < stagedCount;)
            if (note(staged[n]) && staged[n].sequence <= sequence)
            {
                for (auto j = n + 1; j < stagedCount; ++j)
                    staged[j - 1] = staged[j];
                --stagedCount;
            }
            else
                ++n;
    }
    bool updateGates(const RetainedShortMidi &e) noexcept
    {
        if (e.size == 3 && (e.bytes[0] & 0xf0) == 0xb0 && (e.bytes[1] == 120 || e.bytes[1] == 123))
        {
            cancelNotes(e.bytes[0] & 15, e.sequence);
            return true;
        }
        if (!note(e))
            return true;
        Source *owner = nullptr;
        for (auto &s : sources)
            if (s.used && s.id == e.source)
            {
                owner = &s;
                break;
            }
        if (!owner)
            for (auto &s : sources)
                if (!s.used)
                {
                    s.used = true;
                    s.id = e.source;
                    owner = &s;
                    break;
                }
        if (!owner)
        {
            reject(MidiHistoryFailure::sourceCapacity);
            return false;
        }
        auto &gate = owner->gates[std::size_t(e.bytes[0] & 15) * 128 + e.bytes[1]];
        const bool on = (e.bytes[0] & 0xf0) == 0x90 && e.bytes[2] != 0;
        if (on && gate == std::numeric_limits<std::uint16_t>::max())
        {
            reject(MidiHistoryFailure::gateCapacity);
            return false;
        }
        if (on)
            ++gate;
        else if (gate)
            --gate;
        return true; // Preserve ordinary raw note events, not a voice-remapping policy.
    }
    static MidiHistoryConfiguration validate(MidiHistoryConfiguration c)
    {
        if (!c.edge.valid() || !std::isfinite(c.sampleRate) || c.sampleRate <= 0 || c.maximumBlock == 0 ||
            storageBytes(c.limits) > c.limits.graphBytes)
            throw std::invalid_argument("Bounded MIDI history configuration/budget refused");
        return c;
    }

  public:
    static std::size_t storageBytes(const MidiHistoryLimits &limits)
    {
        if (limits.events == 0 || limits.events > 65536 || limits.sources == 0 || limits.sources > 256 ||
            limits.outputEvents == 0 || limits.outputEvents > 1048576 || limits.epoch == 0)
            throw std::invalid_argument("Invalid bounded MIDI history limits");
        return std::size_t(limits.events) * 2 * sizeof(RetainedShortMidi) +
               std::size_t(limits.sources) * sizeof(Source);
    }
    explicit MidiCompensationHistory(MidiHistoryConfiguration c)
        : config(validate(c)), queue(config.limits.events), staged(config.limits.events),
          sources(config.limits.sources)
    {
    }
    const MidiHistoryConfiguration &configuration() const noexcept
    {
        return config;
    }
    MidiHistoryObservation observation() const noexcept
    {
        return {failures.load(std::memory_order_relaxed), panics.load(std::memory_order_relaxed),
                rejected.load(std::memory_order_relaxed), preparedEvents.load(std::memory_order_relaxed)};
    }
    bool allNotesOff() const noexcept
    {
        return panic;
    } // DSP owner only.
    void reject(MidiHistoryFailure reason) noexcept
    {
        failures.fetch_or(std::uint32_t(reason), std::memory_order_relaxed);
        rejected.fetch_add(1, std::memory_order_relaxed);
        safetyPanic(); // A typed failure, never successful silent overflow.
    }
    bool beginBlock(std::int64_t start, std::uint32_t length, bool allOff) noexcept
    {
        panic = false;
        stagedCount = 0;
        validBlock = length != 0 && length <= config.maximumBlock &&
                     start <= std::numeric_limits<std::int64_t>::max() - std::int64_t(length);
        if (!validBlock)
        {
            reject(MidiHistoryFailure::invalidBlock);
            return false;
        }
        reference = start;
        frames = length;
        if (haveReference && start != nextReference)
        {
            count = 0; // Different reference stream: no stale controls or notes replay.
            for (auto &s : sources)
                s = {};
            reject(MidiHistoryFailure::referenceDiscontinuity);
        }
        haveReference = true;
        nextReference = start + length;
        if (allOff)
            safetyPanic(); // Before current-block messages: they may start new gates.
        return true;
    }
    bool enqueue(std::uint32_t frame, std::uint32_t delay, std::uint32_t source,
                 std::span<const std::uint8_t> payload) noexcept
    {
        // Only validated complete MIDI1 short messages. SysEx/meta are not promised.
        const auto status = payload.empty() ? 0u : payload[0];
        const auto required = status >= 0x80 && status < 0xf0    ? (((status & 0xe0) == 0xc0) ? 2u : 3u)
                              : status == 0xf1 || status == 0xf3 ? 2u
                              : status == 0xf2                   ? 3u
                              : status == 0xf6 || status == 0xf8 || status == 0xfa || status == 0xfb ||
                                      status == 0xfc || status == 0xfe || status == 0xff
                                  ? 1u
                                  : 0u;
        if (payload.size() > 3 || payload.size() != required || required == 0 ||
            (payload.size() > 1 && payload[1] >= 0x80) || (payload.size() > 2 && payload[2] >= 0x80))
        {
            reject(MidiHistoryFailure::unsupportedPayload);
            return false;
        }
        if (!validBlock || frame >= frames || delay > config.maximumDelay ||
            reference > std::numeric_limits<std::int64_t>::max() - std::int64_t(frame) - delay)
        {
            reject(MidiHistoryFailure::invalidTimestamp);
            return false;
        }
        // Enqueue-time delay changes may move a release before an older,
        // still-pending attack. Raw MIDI has no per-note identity with which to
        // repair this crossing safely. Refuse it explicitly and use the same
        // bounded safety panic as other unsupported input; never reopen later.
        const auto due = reference + frame + delay;
        const bool release =
            payload.size() == 3 && ((status & 0xf0) == 0x80 || ((status & 0xf0) == 0x90 && payload[2] == 0));
        if (release)
            for (std::size_t n = 0; n < count; ++n)
            {
                const auto &pending = queue[n];
                if (pending.size == 3 && (pending.bytes[0] & 0xf0) == 0x90 && pending.bytes[2] != 0 &&
                    pending.source == source && (pending.bytes[0] & 15) == (status & 15) &&
                    pending.bytes[1] == payload[1] && pending.due >= due)
                {
                    reject(MidiHistoryFailure::deadlineInversion);
                    return false;
                }
            }
        if (sequence == std::numeric_limits<std::uint64_t>::max())
        {
            reject(MidiHistoryFailure::sequenceExhausted);
            return false;
        }
        if (count == queue.size())
        {
            reject(MidiHistoryFailure::eventCapacity);
            return false;
        }
        RetainedShortMidi e{due, ++sequence, source};
        e.size = std::uint8_t(payload.size());
        std::copy(payload.begin(), payload.end(), e.bytes.begin());
        auto where = count;
        while (where && before(e, queue[where - 1]))
        {
            queue[where] = queue[where - 1];
            --where;
        }
        queue[where] = e;
        ++count;
        return true;
    }
    MidiHistoryBatch drain() noexcept
    {
        stagedCount = 0;
        if (!validBlock)
            return {{}, reference, panic};
        while (count && queue[0].due < nextReference)
        {
            const auto e = queue[0];
            erase(0);
            if (e.due < reference)
            {
                reject(MidiHistoryFailure::invalidTimestamp);
                continue;
            }
            if (!updateGates(e))
                continue;
            if (stagedCount == config.limits.outputEvents || stagedCount == staged.size())
            {
                reject(MidiHistoryFailure::outputCapacity);
                continue;
            }
            staged[stagedCount++] = e;
        }
        preparedEvents.fetch_add(stagedCount, std::memory_order_relaxed);
        return {{staged.data(), stagedCount}, reference, panic};
    }
};
static_assert(std::atomic<std::uint32_t>::is_always_lock_free &&
              std::atomic<std::uint64_t>::is_always_lock_free);
} // namespace vibera::dsp
