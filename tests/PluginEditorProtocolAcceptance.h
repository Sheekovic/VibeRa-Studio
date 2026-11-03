// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PluginEditorRequests.h"
#include <initializer_list>

// Pure protocol/model coverage. Include after require(bool, message); call beside
// pluginEditorRequestsTests(). This does not establish a native peer or editor.
static void pluginEditorProtocolAcceptance()
{
    using namespace vibera;
    using Availability = PluginEditorAvailability;
    const PluginEditorWindowState visible{true, true, true, false, Availability::controlsOnly, 0xabcdef01u};
    const PluginEditorWindowState hidden{true, true, false, false, Availability::controlsOnly, 2};
    const PluginEditorWindowState minimised{true, true, false, true, Availability::controlsOnly, 3};
    const PluginEditorWindowState closed{true, false, false, false, Availability::controlsOnly, 4};
    require(unsigned(Availability::unknown) == 0 && unsigned(Availability::available) == 1 &&
                unsigned(Availability::unsupported) == 2 && unsigned(Availability::creationFailed) == 3 &&
                unsigned(Availability::controlsOnly) == 4,
            "Existing editor availability values remain stable; controls-only uses the third field bit");
    for (const auto &state : {visible, hidden, minimised, closed})
        require(state.valid() && PluginEditorWindowState::validPacked(state.packed()) &&
                    PluginEditorWindowState::unpack(state.packed()) == state,
                "Controls-only peer lifecycle round-trips all flags and full revision without native UI");
    require((visible.packed() & 0x70) == 0x40 &&
                !PluginEditorWindowState::validPacked(visible.packed() | 0x80) &&
                !PluginEditorWindowState::validPacked(visible.packed() | 0x100) &&
                !PluginEditorWindowState::validPacked(visible.packed() | 0x80000000ULL),
            "Bit6 is intentional availability data; bits7 through31 remain reserved");
    for (unsigned unknown = 5; unknown != 8; ++unknown)
    {
        auto state = visible;
        state.availability = Availability(unknown);
        require(!state.valid() && !PluginEditorWindowState::validPacked(state.packed()) &&
                    unsigned(PluginEditorWindowState::unpack(state.packed()).availability) == unknown,
                "Every unknown three-bit availability value is retained then rejected, never aliased");
    }
    for (auto availability : {Availability::unsupported, Availability::creationFailed})
    {
        auto state = visible;
        state.availability = availability;
        require(!state.valid() && !PluginEditorWindowState::validPacked(state.packed()),
                "Unsupported and failed native availability still cannot claim an existing peer");
    }
    require(!PluginEditorWindowState{false, false, false, false, Availability::controlsOnly}.valid() &&
                !PluginEditorWindowState{true, false, true, false, Availability::controlsOnly}.valid() &&
                !PluginEditorWindowState{true, true, true, true, Availability::controlsOnly}.valid(),
            "Controls-only capability does not weaken unknown, visible, or minimised shape constraints");

    PluginEditorRequests requests;
    PluginEditorResult result;
    auto reply = [&](PluginEditorAction action, PluginEditorOutcome outcome, PluginEditorWindowState state,
                     unsigned completed = 101)
    {
        const auto admission = requests.reserve(action, 100);
        require(admission.code == PluginEditorAdmissionCode::accepted && admission.serial,
                "Exact action obtains an independently owned controls-only request");
        PluginEditorResult response{admission.serial, action, outcome, state, true};
        response.admittedAt = 100;
        response.completedAt = completed;
        return response;
    };
    auto completes = [&](PluginEditorResult response)
    {
        return requests.completeNative(response, response.completedAt + 1) ==
                   PluginEditorReplyDisposition::completed &&
               requests.consume(response.serial, result);
    };
    for (auto action : {PluginEditorAction::settings, PluginEditorAction::parameters})
        for (auto outcome : {PluginEditorOutcome::opened, PluginEditorOutcome::broughtToFront})
            require(completes(reply(action, outcome, visible)) && result.succeeded() &&
                        result.window == visible && result.completionKnown && result.nativeOutcome == outcome,
                    "Explicit Settings/Parameters succeeds only with acknowledged existing visible controls "
                    "peer");

    auto nativeOpen = reply(PluginEditorAction::open, PluginEditorOutcome::opened, visible);
    require(!nativeOpen.succeeded() &&
                requests.completeNative(nativeOpen, 102) == PluginEditorReplyDisposition::invalid &&
                !requests.ready(nativeOpen.serial),
            "Native Open can never claim successful native UI from a controls-only peer");
    nativeOpen.outcome = PluginEditorOutcome::broughtToFront;
    require(!nativeOpen.succeeded() &&
                requests.completeNative(nativeOpen, 102) == PluginEditorReplyDisposition::invalid,
            "Bringing controls-only to front is also not native Open success");
    nativeOpen.outcome = PluginEditorOutcome::unsupported;
    require(completes(nativeOpen) && !result.succeeded() && result.window == visible &&
                result.nativeOutcome == PluginEditorOutcome::unsupported,
            "Native Open refusal truthfully retains an existing visible controls-only peer");
    for (const auto &state : {hidden, minimised})
        require(completes(reply(PluginEditorAction::open, PluginEditorOutcome::unsupported, state)) &&
                    !result.succeeded() && result.window == state,
                "Native Open refusal preserves an existing hidden/minimised controls-only peer");
    require(completes(reply(PluginEditorAction::close, PluginEditorOutcome::closed, closed)) &&
                result.succeeded() && result.window == closed,
            "Closing the actual peer retains known controls-only capability without pretending native UI");

    for (auto action : {PluginEditorAction::settings, PluginEditorAction::parameters})
    {
        for (const auto &state : {hidden, minimised, closed})
        {
            const auto response = reply(action, PluginEditorOutcome::opened, state);
            require(!response.succeeded() &&
                        requests.completeNative(response, 102) == PluginEditorReplyDisposition::invalid &&
                        !requests.ready(response.serial),
                    "Controls view success requires an actual visible, nonminimised peer");
            requests.abandon(response.serial);
        }
        const auto response = reply(action, PluginEditorOutcome::unsupported, visible);
        require(requests.completeNative(response, 102) == PluginEditorReplyDisposition::invalid,
                "Controls-only unsupported-with-peer exception is restricted to native Open");
        requests.abandon(response.serial);
    }
    const auto forgedFailure = reply(PluginEditorAction::open, PluginEditorOutcome::creationFailed, visible);
    require(requests.completeNative(forgedFailure, 102) == PluginEditorReplyDisposition::invalid,
            "Controls-only peer cannot disguise native creation failure as valid availability");
    requests.abandon(forgedFailure.serial);

    auto undispatched = reply(PluginEditorAction::settings, PluginEditorOutcome::opened, visible);
    undispatched.dispatched = false;
    require(requests.completeNative(undispatched, 102) == PluginEditorReplyDisposition::invalid,
            "Controls-only completion still requires native worker dispatch ownership");
    requests.abandon(undispatched.serial);
    auto wrongClock = reply(PluginEditorAction::parameters, PluginEditorOutcome::opened, visible);
    wrongClock.admittedAt = 99;
    require(requests.completeNative(wrongClock, 102) == PluginEditorReplyDisposition::invalid,
            "Controls-only completion cannot bypass exact admission clock identity");
    requests.abandon(wrongClock.serial);

    const auto timely = reply(PluginEditorAction::settings, PluginEditorOutcome::opened, visible, 5100);
    require(requests.completeNative(timely, 20000) == PluginEditorReplyDisposition::completed &&
                requests.consume(timely.serial, result) && result.succeeded(),
            "Controls-only exact5000ms completion remains valid despite late host observation");
    const auto late = reply(PluginEditorAction::parameters, PluginEditorOutcome::opened, visible, 5101);
    require(requests.completeNative(late, 20000) == PluginEditorReplyDisposition::timeout &&
                requests.consume(late.serial, result) && !result.succeeded() &&
                result.outcome == PluginEditorOutcome::timeout && result.completionKnown &&
                result.nativeOutcome == PluginEditorOutcome::opened && result.window == visible,
            "Controls-only completion at5001ms fails while retaining truthful actual peer state");
    const auto cancelled = requests.reserve(PluginEditorAction::settings, 300);
    require(requests.cancelBeforeDispatch(cancelled.serial, closed) &&
                requests.consume(cancelled.serial, result) && !result.succeeded() &&
                result.outcome == PluginEditorOutcome::cancelled && result.window == closed,
            "Controls-only cancellation preserves capability without claiming an opened peer");
    requests.failPending(PluginEditorOutcome::workerUnavailable);
    require(requests.outstanding() == 0,
            "Abandoned malformed controls-only replies drain through existing terminal-failure ownership");
}
