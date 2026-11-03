// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PluginEditorRequests.h"
#include <thread>
#include <utility>

// Original pure request/ownership/handshake tests. Include after the acceptance
// require(bool,message) helper. These are not native editor or provider evidence.
static void pluginEditorRequestsTests()
{
    using namespace vibera;
    const PluginEditorWindowState visible{true, true, true, false, PluginEditorAvailability::available, 7};
    const PluginEditorWindowState hidden{true, true, false, false, PluginEditorAvailability::available, 8};
    const PluginEditorWindowState minimised{true, true, false, true, PluginEditorAvailability::available, 9};
    const PluginEditorWindowState closed{true, false, false, false, PluginEditorAvailability::available, 10};
    const PluginEditorWindowState unsupported{
        true, false, false, false, PluginEditorAvailability::unsupported, 11};
    const PluginEditorWindowState nullEditor{
        true, false, false, false, PluginEditorAvailability::creationFailed, 12};
    for (const auto &state : {visible, hidden, minimised, closed, unsupported, nullEditor})
        require(state.valid() && PluginEditorWindowState::validPacked(state.packed()) &&
                    PluginEditorWindowState::unpack(state.packed()) == state,
                "editor readback distinguishes visible/hidden/minimised/closed/unsupported/null");
    require(!PluginEditorWindowState::validPacked(visible.packed() | 0x100) &&
                !PluginEditorWindowState{true, false, true}.valid() &&
                !PluginEditorWindowState{true, true, true, true}.valid(),
            "reserved wire bits and impossible visible/minimised states rejected");

    PluginEditorRequests requests;
    std::array<unsigned, 256> admissionTicks{};
    auto reserve = [&](PluginEditorAction action, unsigned now)
    {
        const auto result = requests.reserve(action, now);
        if (result.serial && result.serial < admissionTicks.size())
            admissionTicks[result.serial] = now;
        return result;
    };
    auto acknowledge = [&](PluginEditorResult result)
    {
        result.admittedAt = result.serial < admissionTicks.size() ? admissionTicks[result.serial] : 0;
        result.completedAt = result.admittedAt + 1;
        return requests.completeNative(result, result.completedAt + 1) ==
               PluginEditorReplyDisposition::completed;
    };
    const auto first = reserve(PluginEditorAction::open, 100);
    const auto second = reserve(PluginEditorAction::close, 101);
    require(first.serial && second.serial > first.serial && !requests.ready(first.serial),
            "accepted editor request is pending, not completed");
    PluginEditorResult result{999};
    const auto sentinel = result;
    require(!requests.consume(first.serial, result) && result == sentinel &&
                !acknowledge(
                    {second.serial, PluginEditorAction::open, PluginEditorOutcome::opened, visible, true}) &&
                !acknowledge({999, PluginEditorAction::close, PluginEditorOutcome::closed, closed, true}),
            "wrong/unready owner cannot consume or overwrite another receipt");
    require(
        acknowledge({second.serial, PluginEditorAction::close, PluginEditorOutcome::closed, closed, true}) &&
            acknowledge(
                {first.serial, PluginEditorAction::open, PluginEditorOutcome::opened, visible, true}) &&
            requests.consume(first.serial, result) && result.serial == first.serial && result.succeeded() &&
            !requests.consume(first.serial, result) && requests.ready(second.serial),
        "out-of-order completed callers retain independently consumable exact receipts");
    requests.failPending(PluginEditorOutcome::workerUnavailable);
    require(requests.consume(second.serial, result) && result.outcome == PluginEditorOutcome::closed,
            "later worker failure does not rewrite an already completed receipt");

    const auto cancelled = reserve(PluginEditorAction::open, 200);
    require(requests.cancelBeforeDispatch(cancelled.serial, hidden) &&
                requests.consume(cancelled.serial, result) &&
                result.outcome == PluginEditorOutcome::cancelled && !result.dispatched &&
                result.window == hidden,
            "pre-dispatch cancellation retains the actual prior window state");
    const auto tooLate = reserve(PluginEditorAction::open, 300);
    requests.markDispatched(tooLate.serial);
    require(!requests.cancelBeforeDispatch(tooLate.serial, closed) &&
                !requests.expired(5300, pluginEditorTimeoutMs) &&
                requests.expired(5301, pluginEditorTimeoutMs),
            "native work cannot be reported cancelled; deadline has exact millisecond boundary");
    requests.failPending(PluginEditorOutcome::timeout);
    require(requests.consume(tooLate.serial, result) && result.outcome == PluginEditorOutcome::timeout &&
                result.dispatched && !result.window.known && !result.succeeded(),
            "post-dispatch timeout is explicit uncertainty, not completion or false closed state");
    const auto crashed = reserve(PluginEditorAction::close, 400);
    requests.failPending(PluginEditorOutcome::workerUnavailable);
    require(requests.consume(crashed.serial, result) &&
                result.outcome == PluginEditorOutcome::workerUnavailable && !result.window.known,
            "worker failure retains an explicit owner-specific failure receipt");

    for (auto pair : {std::pair{PluginEditorOutcome::unsupported, unsupported},
                      std::pair{PluginEditorOutcome::creationFailed, nullEditor}})
    {
        const auto token = reserve(PluginEditorAction::open, 500);
        require(acknowledge({token.serial, PluginEditorAction::open, pair.first, pair.second, true}) &&
                    requests.consume(token.serial, result) && !result.succeeded() &&
                    result.window == pair.second,
                "missing/null native editor is a real failure with distinct capability readback");
    }
    const auto wrong = reserve(PluginEditorAction::open, 600);
    require(
        !acknowledge({wrong.serial, PluginEditorAction::open, PluginEditorOutcome::opened, hidden, true}) &&
            !acknowledge({wrong.serial, PluginEditorAction::open, PluginEditorOutcome::timeout, {}, true}) &&
            !requests.ready(wrong.serial),
        "malformed/native-forged host outcomes do not complete owners");
    requests.failPending(PluginEditorOutcome::invalidAcknowledgement);
    require(requests.consume(wrong.serial, result) &&
                result.outcome == PluginEditorOutcome::invalidAcknowledgement,
            "invalid acknowledgement fails without discarding history of completed owners");

    std::array<unsigned, 64> tokens{};
    for (auto &token : tokens)
    {
        token = reserve(PluginEditorAction::open, 700).serial;
        require(token && acknowledge({token, PluginEditorAction::open, PluginEditorOutcome::broughtToFront,
                                      visible, true}),
                "bounded independently owned completed receipt admitted");
    }
    require(requests.outstanding() == 64 &&
                reserve(PluginEditorAction::close, 701).code == PluginEditorAdmissionCode::capacity,
            "saturation refuses new requests instead of dropping another caller's result");
    requests.abandon(tokens[11]);
    const auto abandoned = reserve(PluginEditorAction::open, 702);
    requests.abandon(abandoned.serial);
    require(acknowledge(
                {abandoned.serial, PluginEditorAction::open, PluginEditorOutcome::opened, visible, true}) &&
                requests.outstanding() == 63 && !requests.ready(abandoned.serial),
            "legacy/abandoned owner drains its eventual physical result without receipt leaks");
    for (unsigned i = 0; i < tokens.size(); ++i)
        if (i != 11)
            require(requests.consume(tokens[i], result) && result.serial == tokens[i],
                    "all unconsumed completed results survive saturation and other callers' abandon");
    PluginEditorRequests exhausted(std::numeric_limits<unsigned>::max());
    require(exhausted.reserve(PluginEditorAction::open, 0).code ==
                    PluginEditorAdmissionCode::serialExhausted &&
                reserve(PluginEditorAction(99), 0).code == PluginEditorAdmissionCode::invalidAction,
            "wire tokens never wrap/reuse; invalid typed action rejected");
    PluginEditorRequests clockWrap;
    const auto wrap = clockWrap.reserve(PluginEditorAction::open, std::numeric_limits<unsigned>::max() - 10);
    require(wrap.serial && !clockWrap.expired(10, 21) && clockWrap.expired(11, 21),
            "unsigned monotonic millisecond wrap retains the exact timeout budget");

    // Strict admission-to-native-completion budget, independent of host polling.
    auto stamped = [&](unsigned serial, unsigned admitted, unsigned completed)
    {
        PluginEditorResult result{serial, PluginEditorAction::open, PluginEditorOutcome::opened, visible,
                                  true};
        result.admittedAt = admitted;
        result.completedAt = completed;
        return result;
    };
    PluginEditorRequests timing;
    auto timely = timing.reserve(PluginEditorAction::open, 100);
    require(timing.completeNative(stamped(timely.serial, 100, 5100), 20000) ==
                    PluginEditorReplyDisposition::completed &&
                timing.consume(timely.serial, result) && result.succeeded() && result.completionKnown &&
                result.completedAt - result.admittedAt == 5000,
            "exact5000ms native completion succeeds even when the host polls much later");
    auto genuinelyLate = timing.reserve(PluginEditorAction::open, 200);
    require(timing.completeNative(stamped(genuinelyLate.serial, 200, 5201), 20000) ==
                    PluginEditorReplyDisposition::timeout &&
                timing.consume(genuinelyLate.serial, result) &&
                result.outcome == PluginEditorOutcome::timeout && !result.succeeded() &&
                result.nativeOutcome == PluginEditorOutcome::opened && result.completionKnown &&
                result.window == visible && result.dispatched,
            "native completion at5001ms fails deadline while preserving observed native/window truth");
    auto future = timing.reserve(PluginEditorAction::open, 300);
    require(timing.completeNative(stamped(future.serial, 300, 500), 499) ==
                    PluginEditorReplyDisposition::invalid &&
                timing.completeNative(stamped(future.serial, 301, 400), 500) ==
                    PluginEditorReplyDisposition::invalid &&
                !timing.ready(future.serial),
            "future and mismatched admission ticks cannot forge timely success");
    timing.markDispatched(future.serial);
    timing.failPending(PluginEditorOutcome::timeout);
    require(timing.completeNative(stamped(future.serial, 300, 400), 7000) ==
                    PluginEditorReplyDisposition::alreadyTerminal &&
                timing.consume(future.serial, result) && result.outcome == PluginEditorOutcome::timeout &&
                !result.completionKnown && !result.window.known &&
                timing.completeNative(stamped(future.serial, 300, 400), 7001) ==
                    PluginEditorReplyDisposition::invalid,
            "late acknowledgements cannot rewrite retained terminal failure or resurrect consumed ownership");
    auto abandonedFailure = timing.reserve(PluginEditorAction::open, 400);
    timing.abandon(abandonedFailure.serial);
    timing.failPending(PluginEditorOutcome::workerUnavailable);
    require(timing.completeNative(stamped(abandonedFailure.serial, 400, 500), 8000) ==
                    PluginEditorReplyDisposition::invalid &&
                timing.outstanding() == 0,
            "late reply after abandoned worker failure cannot leak or recreate a receipt");
    require(clockWrap.completeNative(stamped(wrap.serial, std::numeric_limits<unsigned>::max() - 10, 10),
                                     15) == PluginEditorReplyDisposition::completed &&
                clockWrap.consume(wrap.serial, result) && result.succeeded(),
            "admission/completion stamps share wrap-safe uptime arithmetic across the32bit rollover");
    PluginEditorRequests lateWrap;
    const unsigned nearWrap = std::numeric_limits<unsigned>::max() - 2500;
    auto crossed = lateWrap.reserve(PluginEditorAction::open, nearWrap);
    require(lateWrap.completeNative(stamped(crossed.serial, nearWrap, 3000), 4000) ==
                    PluginEditorReplyDisposition::timeout &&
                lateWrap.consume(crossed.serial, result) && result.completedAt - result.admittedAt == 5501,
            "genuinely over-budget native completion remains late across clock wrap");
    require(!pluginEditorPhaseBusy(PluginEditorPhase::idle) &&
                !pluginEditorPhaseBusy(PluginEditorPhase::reply) &&
                pluginEditorPhaseBusy(PluginEditorPhase::pending) &&
                pluginEditorPhaseBusy(PluginEditorPhase::nativeRunning) &&
                pluginEditorPhaseBusy(PluginEditorPhase::hostWriting) &&
                pluginEditorPhaseBusy(PluginEditorPhase::hostReading),
            "physical busy query excludes completed reply/unread receipt but covers native work and "
            "ownership transitions");

    PluginEditorMailbox physical;
    auto phase = PluginEditorPhase::idle;
    require(physical.phase.compare_exchange_strong(phase, PluginEditorPhase::hostWriting),
            "host owns an idle physical editor mailbox before writing fields");
    physical.serial = 41;
    physical.admittedAt = 1000;
    physical.action = PluginEditorAction::open;
    physical.phase.store(PluginEditorPhase::pending, std::memory_order_release);
    phase = PluginEditorPhase::idle;
    require(!physical.phase.compare_exchange_strong(phase, PluginEditorPhase::hostWriting) &&
                physical.serial == 41 && phase == PluginEditorPhase::pending,
            "a concurrent caller is busy and cannot overwrite the accepted owner's physical request");
    phase = PluginEditorPhase::pending;
    require(physical.phase.compare_exchange_strong(phase, PluginEditorPhase::nativeRunning,
                                                   std::memory_order_acquire),
            "native worker acquires only a published pending request");
    physical.outcome = PluginEditorOutcome::opened;
    physical.completedAt = 1500;
    physical.window = visible.packed();
    physical.phase.store(PluginEditorPhase::reply, std::memory_order_release);
    phase = PluginEditorPhase::reply;
    require(physical.phase.compare_exchange_strong(phase, PluginEditorPhase::hostReading,
                                                   std::memory_order_acquire) &&
                physical.serial == 41 && physical.outcome == PluginEditorOutcome::opened &&
                physical.window == visible.packed() && physical.admittedAt == 1000 &&
                physical.completedAt == 1500,
            "published native reply retains the exact request and observed lifecycle state");
    physical.phase.store(PluginEditorPhase::idle, std::memory_order_release);

    // A pure physical-mailbox race: exactly one party can win the same pending
    // phase. The worker never sees nativeRunning after a successful cancellation.
    for (int attempt = 0; attempt < 16; ++attempt)
    {
        PluginEditorMailbox wire;
        wire.phase.store(PluginEditorPhase::pending);
        bool nativeBegan = false, cancelWon = false;
        std::thread worker(
            [&]
            {
                auto expected = PluginEditorPhase::pending;
                nativeBegan = wire.phase.compare_exchange_strong(expected, PluginEditorPhase::nativeRunning,
                                                                 std::memory_order_acquire);
            });
        std::thread cancellation(
            [&]
            {
                auto expected = PluginEditorPhase::pending;
                cancelWon = wire.phase.compare_exchange_strong(expected, PluginEditorPhase::hostWriting,
                                                               std::memory_order_acquire);
            });
        worker.join();
        cancellation.join();
        require(nativeBegan != cancelWon &&
                    wire.phase.load() ==
                        (nativeBegan ? PluginEditorPhase::nativeRunning : PluginEditorPhase::hostWriting),
                "cancellation and native dispatch cannot both own one physical request");
    }
}
