// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PluginHost.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>

namespace vibera
{
namespace fixed_source_pcm_detail
{
// Checked logical Host interval, not a native reference/source-position claim.
inline bool checkedEnd(std::int64_t start, int frames, std::int64_t &end) noexcept
{
    if (start < 0 || frames < 0 || frames > std::numeric_limits<std::int64_t>::max() - start)
        return false;
    end = start + frames;
    return true;
}
} // namespace fixed_source_pcm_detail

// Initial-source-only helper. Construct CONTROL before ANY Host samples, using
// actual immutable prepared Host identity/declaration and admitted maximum span.
// Retain this nonmoving object with the same processing Runtime across shells.
// There is no warm-origin inference, reset/rearm, Host ownership or graph ack.
class FixedSourcePcmEpochGuard final
{
  public:
    struct Expected
    {
        PluginPcmIdentity identity;
        PluginLatencyStamp native;
        int maximumSpan = 0;
    };
    enum class Fault : std::uint32_t
    {
        none,
        invalidExpected,
        invalidSpan,
        logicalOverflow,
        identityMismatch,
        captureIncomplete,
        captureCount,
        invalidStorage,
        invalidSegment,
        packetIdentity,
        startupMismatch,
        missingPacket,
        invalidLatencyPacket,
        missingConsumed,
        consumedSequence,
        nativeEpoch,
        dryDelay,
        forcedFallback,
        invalidMix,
        declarationChanged
    };
    struct Failure
    {
        Fault fault = Fault::none;
        std::int64_t logicalFrame = 0;
        std::size_t segment = 0;
        PluginPcmReceiptStatus receipt = PluginPcmReceiptStatus::empty;
        PluginPcmIdentity actualIdentity;
        std::optional<PluginConsumedLatency> consumed;
        std::optional<PluginLatencyStamp> declaration;
        int offset = 0, count = 0;
    };

    explicit FixedSourcePcmEpochGuard(Expected value) noexcept : expected(value)
    {
        if (!validExpected(value))
            refuse(Fault::invalidExpected); // CONTROL construction, before publication.
    }
    FixedSourcePcmEpochGuard(const FixedSourcePcmEpochGuard &) = delete;
    FixedSourcePcmEpochGuard &operator=(const FixedSourcePcmEpochGuard &) = delete;
    FixedSourcePcmEpochGuard(FixedSourcePcmEpochGuard &&) = delete;
    FixedSourcePcmEpochGuard &operator=(FixedSourcePcmEpochGuard &&) = delete;

    const Expected &expectation() const noexcept
    {
        return expected;
    }
    bool healthy() const noexcept
    {
        return published.load(std::memory_order_acquire) == 0;
    }

    // CONTROL-safe only while object lifetime is pinned. Payload is written ONCE
    // before release-publication, never modified afterward and never read on a
    // healthy result. No seqlock/retry, live cursor read or two-writer payload.
    std::optional<Failure> failure() const noexcept
    {
        if (published.load(std::memory_order_acquire) == 0)
            return {};
        return firstFailure;
    }

    // SINGLE PROCESSING OWNER. Useful before native/input/clock work; no Host
    // access here. This alone cannot fence a later asynchronous output packet.
    bool validateDeclaration(PluginLatencyStamp actual) noexcept
    {
        if (!healthy())
            return false;
        if (actual != expected.native)
            return refuse(Fault::declarationChanged, nullptr, 0, nullptr, actual);
        return true;
    }

    // SINGLE PROCESSING OWNER, after actual Stream/Host emission and BEFORE its
    // PCM reaches a downstream queue. False requires failed-source containment
    // by the caller (clear/refuse output, fail root completion/close scopes).
    // Host/worker/MIDI/params may already have advanced: NO rollback or recovery.
    // Capture must remain valid/disjoint until return; CONTROL cannot inspect
    // that mutable caller payload concurrently. No input/capture mutation here.
    bool validate(const PluginPcmCapture &capture, int frames) noexcept
    {
        if (!healthy())
            return false;
        if (frames < 0 || frames > expected.maximumSpan)
            return refuse(Fault::invalidSpan, &capture);
        std::int64_t end = 0;
        if (!fixed_source_pcm_detail::checkedEnd(logicalFrame, frames, end))
            return refuse(Fault::logicalOverflow, &capture);
        if (capture.identity != expected.identity ||
            (capture.expectedIdentity && *capture.expectedIdentity != expected.identity))
            return refuse(Fault::identityMismatch, &capture);
        if (capture.status != PluginPcmReceiptStatus::complete)
            return refuse(Fault::captureIncomplete, &capture);
        const int quantum = expected.identity.quantum;
        const int cursor = int(logicalFrame % quantum);
        const auto required =
            frames == 0 ? std::size_t(0) : std::size_t(1 + (std::int64_t(cursor) + frames - 1) / quantum);
        if (capture.count != required || capture.required != required)
            return refuse(Fault::captureCount, &capture);
        if (capture.count > capture.storage.size())
            return refuse(Fault::invalidStorage, &capture);
        if (required != 0)
        {
            const auto address = reinterpret_cast<std::uintptr_t>(capture.storage.data());
            if (address == 0 || address % alignof(PluginPcmSegment) != 0)
                return refuse(Fault::invalidStorage, &capture);
        }
        int covered = 0;
        auto positioned = logicalFrame;
        const auto packets = std::uint64_t(expected.identity.transportSamples / quantum);
        const auto dryDelay = expected.identity.transportSamples + expected.native.nativeSamples;
        for (std::size_t index = 0; index < required; ++index)
        {
            const auto &segment = capture.storage[index];
            const int packetFrame = int(positioned % quantum);
            const auto outputPacket = std::uint64_t(positioned / quantum);
            const int wanted = std::min(frames - covered, quantum - packetFrame);
            if (segment.offset != covered || segment.count <= 0 || segment.count != wanted)
                return refuse(Fault::invalidSegment, &capture, index, &segment);
            if (segment.packetFrame != packetFrame || segment.outputPacket != outputPacket)
                return refuse(Fault::packetIdentity, &capture, index, &segment);
            if (segment.packet == PluginPcmPacketKind::missing)
                return refuse(Fault::missingPacket, &capture, index, &segment);
            if (segment.packet == PluginPcmPacketKind::invalidLatency)
                return refuse(Fault::invalidLatencyPacket, &capture, index, &segment);
            if (segment.forcedDry)
                return refuse(Fault::forcedFallback, &capture, index, &segment);
            if (outputPacket < packets)
            {
                if (segment.packet != PluginPcmPacketKind::startup || segment.consumed)
                    return refuse(Fault::startupMismatch, &capture, index, &segment);
            }
            else
            {
                if (segment.packet != PluginPcmPacketKind::completed)
                    return refuse(Fault::startupMismatch, &capture, index, &segment);
                if (!segment.consumed)
                    return refuse(Fault::missingConsumed, &capture, index, &segment);
                if (segment.consumed->sequence != outputPacket - packets)
                    return refuse(Fault::consumedSequence, &capture, index, &segment);
                if (segment.consumed->stamp != expected.native)
                    return refuse(Fault::nativeEpoch, &capture, index, &segment);
            }
            if (segment.dryDelaySamples != dryDelay)
                return refuse(Fault::dryDelay, &capture, index, &segment);
            if (!validMix(segment))
                return refuse(Fault::invalidMix, &capture, index, &segment);
            covered += segment.count;    // Bounded by validated frames/wanted, no offset+count overflow.
            positioned += segment.count; // Full interval checked before any addition.
        }
        if (covered != frames || positioned != end)
            return refuse(Fault::invalidSegment, &capture);
        logicalFrame = end; // Commit continuity only after the ENTIRE capture passed.
        return true;
    }

    static bool validExpected(const Expected &value) noexcept
    {
        const auto &id = value.identity;
        bool hasUuid = false;
        for (auto byte : id.instance)
            hasUuid |= byte != 0;
        if (!hasUuid || id.host == 0 || value.maximumSpan <= 0 || !value.native.valid(id.quantum) ||
            id.transportSamples <= 0 || id.transportSamples % id.quantum != 0)
            return false;
        const auto packets = id.transportSamples / id.quantum;
        if (packets < 1 || packets > 8 || (id.preparedTransport == 0 && packets > 2) ||
            (id.preparedTransport != 0 && packets < 2))
            return false;
        // Exact existing Host dry-capacity contract, not a clamp or latency change.
        return std::int64_t(id.transportSamples) + value.native.nativeSamples < 131072;
    }

  private:
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
    static_assert(std::is_trivially_copyable_v<Failure>);
    const Expected expected;
    std::int64_t logicalFrame = 0; // DSP only; no CONTROL getter/inferred warm origin.
    Failure firstFailure;
    std::atomic<std::uint32_t> published{0};

    bool refuse(Fault fault, const PluginPcmCapture *capture = nullptr, std::size_t segment = 0,
                const PluginPcmSegment *actual = nullptr,
                std::optional<PluginLatencyStamp> declaration = {}) noexcept
    {
        if (published.load(std::memory_order_relaxed) == 0)
        {
            firstFailure.fault = fault;
            firstFailure.logicalFrame = logicalFrame;
            firstFailure.segment = segment;
            firstFailure.declaration = declaration;
            if (capture)
            {
                firstFailure.receipt = capture->status;
                firstFailure.actualIdentity = capture->identity;
            }
            if (actual)
            {
                firstFailure.consumed = actual->consumed;
                firstFailure.offset = actual->offset;
                firstFailure.count = actual->count;
            }
            published.store(std::uint32_t(fault), std::memory_order_release);
        }
        return false;
    }
    static bool finiteUnit(float value) noexcept
    {
        return std::isfinite(value) && value >= 0 && value <= 1;
    }
    static PluginPcmMixKind endpointKind(float wet, float bypass) noexcept
    {
        return wet == 0 || bypass == 1   ? PluginPcmMixKind::delayedDry
               : wet == 1 && bypass == 0 ? PluginPcmMixKind::wet
                                         : PluginPcmMixKind::mixed;
    }
    static bool validMix(const PluginPcmSegment &segment) noexcept
    {
        if (!finiteUnit(segment.firstWet) || !finiteUnit(segment.lastWet) ||
            !finiteUnit(segment.firstBypass) || !finiteUnit(segment.lastBypass))
            return false;
        const auto first = endpointKind(segment.firstWet, segment.firstBypass);
        const auto last = endpointKind(segment.lastWet, segment.lastBypass);
        if (segment.count == 1 &&
            (segment.firstWet != segment.lastWet || segment.firstBypass != segment.lastBypass))
            return false;
        switch (segment.mix)
        {
        case PluginPcmMixKind::wet:
            return first == segment.mix && last == segment.mix;
        case PluginPcmMixKind::delayedDry:
            return first == segment.mix && last == segment.mix;
        // Endpoints cannot reconstruct a ramp's interior. Actual Host mix
        // classification is retained; a one-sample segment has no interior.
        case PluginPcmMixKind::mixed:
            return segment.count != 1 || first == segment.mix;
        }
        return false;
    }
};
} // namespace vibera
