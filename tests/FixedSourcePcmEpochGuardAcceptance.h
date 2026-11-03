// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "FixedSourcePcmEpochGuard.h"
#include <array>
#include <initializer_list>
#include <limits>

// PURE VALIDATOR tests. Synthetic metadata follows actual Host/Stream capture
// contracts; it does not manufacture a worker completion or prove an app race,
// graph adoption, physical callback, audible PCM or dynamic-PDC transition.
namespace fixed_source_pcm_acceptance
{
using Guard = vibera::FixedSourcePcmEpochGuard;
using Fault = Guard::Fault;
using Identity = vibera::PluginPcmIdentity;
using Segment = vibera::PluginPcmSegment;
using Capture = vibera::PluginPcmCapture;
using Stamp = vibera::PluginLatencyStamp;
using Packet = vibera::PluginPcmPacketKind;
using Mix = vibera::PluginPcmMixKind;
using Status = vibera::PluginPcmReceiptStatus;

inline Guard::Expected expected(int transport = 512, Stamp native = {0, 1}, int maximumSpan = 256)
{
    Identity identity;
    identity.instance[0] = 0x31;
    identity.instance[15] = 0xa7;
    identity.host = 0x1000;
    identity.preparedTransport = transport > 512 ? 0x2000 : 0;
    identity.quantum = 256;
    identity.transportSamples = transport;
    return {identity, native, maximumSpan};
}
inline Segment startup(int offset, int count, int packetFrame, std::uint64_t packet, int dry)
{
    Segment result;
    result.offset = offset;
    result.count = count;
    result.packetFrame = packetFrame;
    result.outputPacket = packet;
    result.packet = Packet::startup;
    result.dryDelaySamples = dry;
    return result;
}
inline Segment completed(int offset, int count, int packetFrame, std::uint64_t packet, std::uint64_t consumed,
                         Stamp stamp, int dry)
{
    auto result = startup(offset, count, packetFrame, packet, dry);
    result.packet = Packet::completed;
    result.consumed = vibera::PluginConsumedLatency{stamp, consumed};
    return result;
}
struct Snapshot
{
    std::array<Segment, 8> storage{};
    Capture capture;
    Snapshot(const Identity &identity, std::initializer_list<Segment> values) : capture(storage, identity)
    {
        require(values.size() <= storage.size(), "Pure snapshot has actual bounded storage");
        std::copy(values.begin(), values.end(), storage.begin());
        capture.identity = identity;
        capture.status = Status::complete;
        capture.count = capture.required = values.size();
    }
};
inline void good(Guard &guard, Snapshot &snapshot, int frames)
{
    require(guard.validate(snapshot.capture, frames) && guard.healthy() && !guard.failure(),
            "Literal contiguous PCM metadata accepted, no graph/worker completion implied");
}
inline void startupOneQuantum(Guard &guard, const Guard::Expected &value)
{
    Snapshot prefix(value.identity,
                    {startup(0, 256, 0, 0, value.identity.transportSamples + value.native.nativeSamples)});
    good(guard, prefix, 256);
}
template <typename Mutate> inline void startupFault(Fault expectedFault, Mutate mutate)
{
    const auto value = expected();
    Guard guard(value);
    Snapshot input(value.identity, {startup(0, 16, 0, 0, 512)});
    mutate(input);
    require(!guard.validate(input.capture, 16), "Malformed metadata is explicit noncompletion");
    const auto first = guard.failure();
    require(first && first->fault == expectedFault && first->logicalFrame == 0 && !guard.healthy(),
            "Pure validator publishes typed first source fault");
    require(!guard.validateDeclaration(value.native) && !guard.validate(input.capture, 0) &&
                guard.failure()->fault == first->fault &&
                guard.failure()->logicalFrame == first->logicalFrame,
            "No declaration/capture retry rearms or overwrites first fault");
}
template <typename Mutate> inline void owedFault(Fault expectedFault, Mutate mutate)
{
    const auto value = expected(256, {31, 1});
    Guard guard(value);
    startupOneQuantum(guard, value);
    Snapshot input(value.identity, {completed(0, 16, 0, 1, 0, {31, 1}, 287)});
    mutate(input.storage[0]);
    require(!guard.validate(input.capture, 16) && guard.failure() &&
                guard.failure()->fault == expectedFault && guard.failure()->logicalFrame == 256,
            "Owed native packet cannot hide an epoch/sequence/fallback error");
}
inline void tests()
{
    {
        const auto value = expected();
        Guard guard(value);
        require(guard.validateDeclaration({0, 1}) && guard.healthy() && !guard.failure(),
                "Expected coherent fixed declaration, no worker query in guard");
        Snapshot first(value.identity, {startup(0, 127, 0, 0, 512)});
        good(guard, first, 127);
        Snapshot second(value.identity, {startup(0, 64, 127, 0, 512)});
        good(guard, second, 64);
        Snapshot third(value.identity, {startup(0, 64, 191, 0, 512)});
        good(guard, third, 64);
        Capture empty(std::span<Segment>{}, value.identity);
        empty.identity = value.identity;
        empty.status = Status::complete;
        require(guard.validate(empty, 0) && empty.count == 0 && empty.required == 0,
                "Empty call accepted without moving independent logical cursor255");
        Snapshot crossing(value.identity, {startup(0, 1, 255, 0, 512), startup(1, 64, 0, 1, 512)});
        good(guard, crossing, 65);
        Snapshot remainder(value.identity, {startup(0, 192, 64, 1, 512)});
        good(guard, remainder, 192);
        Snapshot firstOwed(value.identity, {completed(0, 17, 0, 2, 0, {0, 1}, 512)});
        good(guard, firstOwed, 17);
        Snapshot tail(value.identity, {completed(0, 239, 17, 2, 0, {0, 1}, 512)});
        good(guard, tail, 239);
    }
    {
        // Literal ordinary1Q/offline-sized777 interval: four actual-format spans.
        const auto value = expected(256, {31, 1}, 777);
        Guard guard(value);
        Snapshot input(value.identity,
                       {startup(0, 256, 0, 0, 287), completed(256, 256, 0, 1, 0, {31, 1}, 287),
                        completed(512, 256, 0, 2, 1, {31, 1}, 287),
                        completed(768, 9, 0, 3, 2, {31, 1}, 287)});
        good(guard, input, 777);
    }
    {
        const auto value = expected(768, {257, 1}, 512);
        Guard guard(value);
        Snapshot first(value.identity, {startup(0, 256, 0, 0, 1025), startup(256, 256, 0, 1, 1025)});
        good(guard, first, 512);
        Snapshot lastStartup(value.identity, {startup(0, 256, 0, 2, 1025)});
        good(guard, lastStartup, 256);
        Snapshot expanded(value.identity, {completed(0, 256, 0, 3, 0, {257, 1}, 1025),
                                           completed(256, 26, 0, 4, 1, {257, 1}, 1025)});
        good(guard, expanded, 282);
    }
    {
        auto value = expected(768, {0, 1}, 112);
        value.identity.quantum = 16;
        value.identity.transportSamples = 128; // Prepared K8/Q16, exact plan token retained.
        Guard guard(value);
        Snapshot first(value.identity,
                       {startup(0, 16, 0, 0, 128), startup(16, 16, 0, 1, 128), startup(32, 16, 0, 2, 128),
                        startup(48, 16, 0, 3, 128), startup(64, 16, 0, 4, 128), startup(80, 16, 0, 5, 128),
                        startup(96, 16, 0, 6, 128)});
        good(guard, first, 112);
        Snapshot finalStartup(value.identity, {startup(0, 16, 0, 7, 128)});
        good(guard, finalStartup, 16);
        Snapshot owed(value.identity, {completed(0, 1, 0, 8, 0, {0, 1}, 128)});
        good(guard, owed, 1);
    }
    startupFault(Fault::identityMismatch, [](auto &s) { s.capture.identity.host = 0x1001; });
    startupFault(Fault::identityMismatch, [](auto &s) { s.capture.identity.instance[0] ^= 1; });
    startupFault(Fault::identityMismatch, [](auto &s) { s.capture.identity.preparedTransport = 0x3000; });
    startupFault(Fault::identityMismatch, [](auto &s) { s.capture.identity.quantum = 128; });
    startupFault(Fault::identityMismatch, [](auto &s) { s.capture.identity.transportSamples = 256; });
    startupFault(Fault::identityMismatch, [](auto &s) { s.capture.expectedIdentity->host = 0x3000; });
    for (auto status : {Status::empty, Status::invalidSpan, Status::capacityExceeded,
                        Status::workerUnavailable, Status::transportRefused})
        startupFault(Fault::captureIncomplete, [status](auto &s) { s.capture.status = status; });
    startupFault(Fault::captureCount, [](auto &s) { s.capture.count = 0; });
    startupFault(Fault::captureCount, [](auto &s) { s.capture.required = 0; });
    startupFault(Fault::captureCount, [](auto &s)
                 { s.capture.count = s.capture.required = std::numeric_limits<std::size_t>::max(); });
    startupFault(Fault::invalidStorage, [](auto &s) { s.capture.storage = {}; });
    startupFault(Fault::invalidSegment,
                 [](auto &s) { s.storage[0].offset = std::numeric_limits<int>::max(); });
    startupFault(Fault::invalidSegment,
                 [](auto &s) { s.storage[0].count = std::numeric_limits<int>::max(); });
    startupFault(Fault::invalidSegment, [](auto &s) { s.storage[0].count = 0; });
    startupFault(Fault::invalidSegment, [](auto &s) { s.storage[0].count = -1; });
    startupFault(Fault::packetIdentity, [](auto &s) { s.storage[0].packetFrame = -1; });
    startupFault(Fault::packetIdentity, [](auto &s) { s.storage[0].packetFrame = 256; });
    startupFault(Fault::packetIdentity,
                 [](auto &s) { s.storage[0].outputPacket = std::numeric_limits<std::uint64_t>::max(); });
    startupFault(Fault::startupMismatch, [](auto &s) { s.storage[0].packet = Packet::completed; });
    startupFault(Fault::startupMismatch,
                 [](auto &s) { s.storage[0].consumed = vibera::PluginConsumedLatency{{0, 1}, 0}; });
    startupFault(Fault::missingPacket, [](auto &s) { s.storage[0].packet = Packet::missing; });
    startupFault(Fault::invalidLatencyPacket, [](auto &s) { s.storage[0].packet = Packet::invalidLatency; });
    startupFault(Fault::forcedFallback, [](auto &s) { s.storage[0].forcedDry = true; });
    startupFault(Fault::dryDelay, [](auto &s) { s.storage[0].dryDelaySamples = 511; });
    startupFault(Fault::invalidMix,
                 [](auto &s) { s.storage[0].firstWet = std::numeric_limits<float>::quiet_NaN(); });
    startupFault(Fault::invalidMix, [](auto &s) { s.storage[0].lastWet = 2; });
    startupFault(Fault::invalidMix, [](auto &s) { s.storage[0].firstBypass = -.1f; });
    startupFault(Fault::invalidMix, [](auto &s) { s.storage[0].mix = static_cast<Mix>(99); });
    startupFault(Fault::invalidMix, [](auto &s) { s.storage[0].firstWet = .5f; });
    startupFault(Fault::invalidMix, [](auto &s) { s.storage[0].mix = Mix::delayedDry; });
    owedFault(Fault::startupMismatch, [](auto &s) { s.packet = Packet::startup; });
    owedFault(Fault::missingConsumed, [](auto &s) { s.consumed.reset(); });
    owedFault(Fault::consumedSequence, [](auto &s) { s.consumed->sequence = 1; });
    owedFault(Fault::nativeEpoch, [](auto &s) { s.consumed->stamp = {31, 2}; });
    owedFault(Fault::nativeEpoch, [](auto &s) { s.consumed->stamp = {0, 1}; });
    owedFault(Fault::nativeEpoch, [](auto &s) { s.consumed->stamp = {31, 0}; });
    owedFault(Fault::dryDelay, [](auto &s) { s.dryDelaySamples = 286; });
    owedFault(Fault::missingPacket,
              [](auto &s)
              {
                  s.packet = Packet::missing;
                  s.firstWet = s.lastWet = 0;
                  s.mix = Mix::delayedDry;
              });
    {
        const auto value = expected(256, {0, 1}, 300);
        Guard guard(value);
        Snapshot input(value.identity,
                       {startup(0, 256, 0, 0, 256), completed(256, 44, 0, 1, 0, {0, 3}, 256)});
        require(!guard.validate(input.capture, 300),
                "Returned native0 with new generation still refuses (A/B/A)");
        auto fault = guard.failure();
        require(fault && fault->fault == Fault::nativeEpoch && fault->logicalFrame == 0 &&
                    fault->segment == 1 && fault->consumed && fault->consumed->stamp == Stamp{0, 3} &&
                    input.capture.count == 2 && input.capture.status == Status::complete,
                "No accepted prefix commits; copied first failure does not mutate caller/claim rollback");
        input.storage[1].consumed->stamp = {0, 1};
        require(!guard.validate(input.capture, 300) && guard.failure()->consumed->stamp == Stamp{0, 3},
                "Control-safe write-once observation owns values, not a live capture pointer");
    }
    {
        const auto value = expected(256, {31, 1});
        Guard guard(value);
        startupOneQuantum(guard, value);
        Snapshot bypass(value.identity, {completed(0, 16, 0, 1, 0, {31, 1}, 287)});
        bypass.storage[0].firstBypass = bypass.storage[0].lastBypass = 1;
        bypass.storage[0].mix = Mix::delayedDry;
        good(guard, bypass, 16);
        Snapshot mixed(value.identity, {completed(0, 16, 16, 1, 0, {31, 1}, 287)});
        mixed.storage[0].firstWet = .25f;
        mixed.storage[0].lastWet = .75f;
        mixed.storage[0].mix = Mix::mixed;
        good(guard, mixed, 16);
        Snapshot one(value.identity, {completed(0, 1, 32, 1, 0, {31, 1}, 287)});
        one.storage[0].firstWet = one.storage[0].lastWet = .5f;
        one.storage[0].mix = Mix::mixed;
        good(guard, one, 1);
    }
    {
        auto value = expected();
        Guard guard(value);
        Snapshot prefix(value.identity, {startup(0, 256, 0, 0, 512)});
        good(guard, prefix, 256);
        require(!guard.validate(prefix.capture, 256) && guard.failure()->fault == Fault::packetIdentity,
                "Replaying old startup receipt cannot reuse logical output position");
    }
    {
        auto value = expected();
        Guard guard(value);
        require(!guard.validateDeclaration({0, 3}) && guard.failure()->fault == Fault::declarationChanged &&
                    guard.failure()->declaration == Stamp{0, 3} && !guard.validateDeclaration(value.native),
                "Known declaration change is sticky even when native sample count returns to0");
    }
    for (int frames : {-1, 257})
    {
        const auto value = expected();
        Guard guard(value);
        Snapshot snapshot(value.identity, {startup(0, 16, 0, 0, 512)});
        require(!guard.validate(snapshot.capture, frames) && guard.failure()->fault == Fault::invalidSpan,
                "Actual call must fit positive prepared bound before interval math");
    }
    const auto invalid = [](Guard::Expected value)
    {
        require(!Guard::validExpected(value), "Invalid immutable preparation rejected");
        Guard guard(value);
        require(!guard.healthy() && guard.failure()->fault == Fault::invalidExpected,
                "Invalid construction publishes failure before DSP attachment");
    };
    {
        auto v = expected();
        v.identity.host = 0;
        invalid(v);
    }
    {
        auto v = expected();
        v.identity.instance.fill(0);
        invalid(v);
    }
    {
        auto v = expected();
        v.identity.quantum = 0;
        invalid(v);
    }
    {
        auto v = expected();
        v.identity.quantum = -1;
        invalid(v);
    }
    {
        auto v = expected();
        v.identity.quantum = 8193;
        invalid(v);
    }
    {
        auto v = expected();
        v.identity.transportSamples = std::numeric_limits<int>::max();
        invalid(v);
    }
    {
        auto v = expected();
        v.identity.transportSamples = 0;
        invalid(v);
    }
    {
        auto v = expected();
        v.identity.transportSamples = 768;
        invalid(v);
    }
    {
        auto v = expected();
        v.identity.preparedTransport = 0x2000;
        v.identity.transportSamples = 256;
        invalid(v);
    }
    {
        auto v = expected();
        v.maximumSpan = 0;
        invalid(v);
    }
    {
        auto v = expected();
        v.maximumSpan = -1;
        invalid(v);
    }
    {
        auto v = expected();
        v.native = {0, 0};
        invalid(v);
    }
    {
        auto v = expected();
        v.native = {-1, 1};
        invalid(v);
    }
    {
        auto v = expected();
        v.native = {130800, 1};
        invalid(v);
    } // Q+native valid; KQ+native exceeds dry capacity.
    std::int64_t end = 0;
    const auto maximum = std::numeric_limits<std::int64_t>::max();
    require(vibera::fixed_source_pcm_detail::checkedEnd(maximum - 2, 2, end) && end == maximum &&
                !vibera::fixed_source_pcm_detail::checkedEnd(maximum - 2, 3, end) &&
                !vibera::fixed_source_pcm_detail::checkedEnd(-1, 1, end) &&
                !vibera::fixed_source_pcm_detail::checkedEnd(0, std::numeric_limits<int>::min(), end),
            "Independent checked logical-end boundary cases; no astronomical fake worker progression");
}
} // namespace fixed_source_pcm_acceptance

static void fixedSourcePcmEpochGuardTests()
{
    fixed_source_pcm_acceptance::tests();
}
