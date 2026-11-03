// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PluginWrapperProtocol.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <string>
#include <thread>
#include <tuple>

// Included after the acceptance harness require(). No plugin, audio or GUI is
// launched here: host acknowledgement is the actual mailbox ownership protocol.
static void pluginWrapperProtocolTests()
{
    using Mailbox = vibera::PluginWrapperMailbox;
    using Intent = vibera::PluginWrapperIntent;
    using Action = vibera::PluginWrapperAction;
    using Phase = vibera::PluginWrapperPhase;
    using Outcome = vibera::PluginWrapperOutcome;
    const auto intent = [](Action action, float value, bool open = false, std::uint32_t parameter = 0,
                           std::uint32_t generation = 0)
    { return Intent{action, parameter, generation, value, open}; };
    const auto observe = [](const Mailbox &mailbox)
    {
        // Single-thread cases only. Concurrent terminal/plain fields are read
        // separately below, exclusively after terminal acquire publication.
        return std::make_tuple(mailbox.phase.load(), mailbox.serial, mailbox.action, mailbox.parameter,
                               mailbox.generation, mailbox.io.inputBus, mailbox.io.outputBus,
                               mailbox.io.inputChannels, mailbox.io.monoSource, mailbox.desired.load(),
                               mailbox.outcome, std::string(mailbox.operation), std::string(mailbox.code),
                               std::string(mailbox.message), mailbox.ready.load(), mailbox.bypassed.load(),
                               mailbox.mix.load(), mailbox.view.load(), mailbox.lastTweakedSelection.load());
    };
    const auto accept = [](Mailbox &mailbox)
    {
        auto expected = Phase::submitted;
        return mailbox.phase.compare_exchange_strong(expected, Phase::accepted, std::memory_order_acq_rel);
    };
    const auto finish = [](Mailbox &mailbox, Outcome outcome)
    {
        mailbox.outcome = outcome;
        std::snprintf(mailbox.operation, sizeof(mailbox.operation), "original-operation-%u", mailbox.serial);
        std::snprintf(mailbox.code, sizeof(mailbox.code), "original-terminal-code");
        std::snprintf(mailbox.message, sizeof(mailbox.message), "original-terminal-message");
        mailbox.phase.store(Phase::terminal, std::memory_order_release);
    };

    require(Mailbox::pack(.5f, true) == 0x000000013f000000ULL &&
                Mailbox::pack(.875f, false) == 0x000000003f600000ULL &&
                Mailbox::value(0x000000013e800000ULL) == .25f &&
                Mailbox::gestureOpen(0x000000013e800000ULL) && !Mailbox::gestureOpen(0x000000003f600000ULL),
            "Wrapper desired carries exact float bits and gesture end in one atomic word");

    Mailbox rejected;
    rejected.ready.store(true);
    rejected.bypassed.store(true);
    rejected.mix.store(.375f);
    rejected.view.store(vibera::PluginWrapperView::parameters);
    rejected.outcome = Outcome::cancelled;
    std::snprintf(rejected.operation, sizeof(rejected.operation), "untouched-operation");
    std::snprintf(rejected.code, sizeof(rejected.code), "untouched-code");
    std::snprintf(rejected.message, sizeof(rejected.message), "untouched-message");
    const auto beforeRejection = observe(rejected);
    const std::array<Action, 3> actions{Action::bypass, Action::mix, Action::parameter};
    const std::array<float, 5> invalidValues{-0.01f, 1.01f, std::numeric_limits<float>::infinity(),
                                             -std::numeric_limits<float>::infinity(),
                                             std::numeric_limits<float>::quiet_NaN()};
    for (auto action : actions)
        for (auto value : invalidValues)
        {
            require(!rejected.submit(intent(action, value, true, 42, 7)),
                    "Wrapper refuses range and nonfinite values before admission");
            require(observe(rejected) == beforeRejection,
                    "Rejected value preserves serial, phase, target, desired and terminal storage");
        }
    for (int action : {-1, 0, 99})
    {
        require(!rejected.submit(intent(static_cast<Action>(action), .5f)),
                "Wrapper refuses unknown action discriminants");
        require(observe(rejected) == beforeRejection, "Unknown action changes no mailbox state");
    }
    require(!rejected.submit(intent(Action::bypass, .5f)) && observe(rejected) == beforeRejection,
            "Fractional bypass is rejected atomically");
    require(!rejected.submit(intent(Action::parameter, .5f, true, 42, 0)) &&
                observe(rejected) == beforeRejection,
            "Parameter generation zero is rejected atomically");
    for (const auto action : {Action::chainCopy, Action::chainSave, Action::chainPaste, Action::chainLoad,
                              Action::chainDirectory, Action::sidechain})
    {
        require(!rejected.submit(intent(action, 1)) && !rejected.submit(intent(action, 0, true)) &&
                    !rejected.submit(intent(action, 0, false, 42, 7)) && observe(rejected) == beforeRejection,
                "Chain/detector navigation refuses scalar payloads and gestures without changing mailbox");
        Mailbox owned;
        require(owned.submit(intent(action, 0)) && owned.action == action && accept(owned),
                "Each chain/detector intent owns an exact actual CONTROL serial");
        const auto pending = observe(owned);
        require(!owned.submit(intent(action, 0)) && observe(owned) == pending,
                "Pending chain/dialog work cannot coalesce or admit a second request");
        finish(owned, Outcome::cancelled);
        require(owned.phase.load() == Phase::terminal && owned.outcome == Outcome::cancelled,
                "Cancelled chain/detector acknowledgement remains terminal until consumed");
    }

    for (const vibera::PluginMainIo invalid : {vibera::PluginMainIo{-1, 0, 2, 0},
                                               {32, 0, 2, 0},
                                               {0, 32, 2, 0},
                                               {0, 0, 0, 0},
                                               {0, 0, 2, 1},
                                               {0, 0, 1, 0},
                                               {0, 0, 1, 4}})
    {
        auto request = intent(Action::audioIo, 0);
        request.io = invalid;
        require(!rejected.submit(request) && observe(rejected) == beforeRejection,
                "Invalid typed routing leaves every immutable mailbox field unchanged");
    }
    Mailbox routing;
    auto routingRequest = intent(Action::audioIo, 0);
    routingRequest.io = {31, 17, 1, 3};
    require(routing.submit(routingRequest) && routing.serial == 1 && routing.action == Action::audioIo &&
                routing.io == routingRequest.io,
            "Routing publishes all four exact typed fields before host acceptance");
    for (const auto phase : {Phase::submitted, Phase::accepted})
    {
        if (phase == Phase::accepted)
            require(accept(routing), "Routing has actual once-only CONTROL acceptance");
        const auto owned = observe(routing);
        auto replacement = routingRequest;
        replacement.io = {};
        require(!routing.submit(replacement) &&
                    !routing.submit(intent(Action::parameter, .5f, true, 42, 7)) && observe(routing) == owned,
                "Submitted and accepted routing cannot coalesce, retarget or overwrite the owned payload");
    }
    finish(routing, Outcome::failed);
    require(routing.io == routingRequest.io && routing.outcome == Outcome::failed &&
                routing.phase.load() == Phase::terminal,
            "Routing preserves its exact requested payload through an explicit failed terminal");
    const auto retainedRouting = observe(routing);
    require(!routing.submit(intent(Action::audioIo, 0)) && observe(routing) == retainedRouting,
            "Unconsumed routing terminal cannot be overwritten by another valid selection");
    routing.phase.store(Phase::idle, std::memory_order_release);
    require(routing.submit(intent(Action::audioIo, 0)) && routing.serial == 2 &&
                routing.io == vibera::PluginMainIo{},
            "Consuming routing terminal admits a fresh stereo selection with a distinct serial");

    for (const auto action : {Action::automationCreate, Action::initialize, Action::removeInitialization,
                              Action::refresh, Action::notifications})
    {
        Mailbox control;
        const bool parameter = vibera::pluginWrapperParameterAction(action);
        require(control.submit(intent(action, 1.f, false, 0, parameter ? 7 : 0)),
                "Advanced wrapper control admits a distinct owned request, including zero native ID");
        const auto submitted = observe(control);
        require(!control.submit(intent(action, 0.f, false, 0, parameter ? 7 : 0)) &&
                    observe(control) == submitted,
                "Non-gesture advanced requests never coalesce or overwrite an owned request");
        require(accept(control), "Advanced wrapper control has actual one-time host acceptance");
        finish(control, Outcome::failed);
        require(control.phase.load() == Phase::terminal && control.outcome == Outcome::failed,
                "Advanced wrapper failure is retained separately from accepted admission");
        require(!rejected.submit(intent(action, 1.f, true, 0, 7)) && observe(rejected) == beforeRejection,
                "Only scalar value requests may open an ongoing gesture");
        if (parameter)
            require(!rejected.submit(intent(action, 1.f, false, 0, 0)) &&
                        observe(rejected) == beforeRejection,
                    "Automation wrapper requests require their exact nonzero catalogue generation");
    }
    require(!rejected.submit(intent(Action::notifications, .5f)) && observe(rejected) == beforeRejection,
            "Notification toggle requires a boolean value without mutating the mailbox");

    Mailbox gesture;
    require(gesture.submit(intent(Action::parameter, .125f, true, 4000001234u, 17)),
            "Actual parameter request is admitted");
    require(gesture.serial == 1 && gesture.phase.load() == Phase::submitted && gesture.operation[0] == 0 &&
                gesture.code[0] == 0 && gesture.message[0] == 0,
            "Admission publishes immutable identity, without claiming terminal completion");
    require(gesture.submit(intent(Action::parameter, .25f, true, 4000001234u, 17)) && gesture.serial == 1 &&
                gesture.phase.load() == Phase::submitted && gesture.desired.load() == 0x000000013e800000ULL,
            "Submitted parameter gesture coalesces onto its existing serial");
    for (auto request :
         {intent(Action::parameter, .5f, true, 42, 17), intent(Action::parameter, .5f, true, 4000001234u, 18),
          intent(Action::bypass, 1), intent(Action::mix, .5f)})
    {
        const auto baseline = observe(gesture);
        require(!gesture.submit(request) && observe(gesture) == baseline,
                "Busy request refuses different target, generation and nonparameter action unchanged");
    }
    require(accept(gesture) && !accept(gesture) && gesture.phase.load() == Phase::accepted &&
                gesture.operation[0] == 0,
            "Host acceptance is exactly once and remains distinct from terminal acknowledgement");
    const auto acceptedBaseline = observe(gesture);
    require(!gesture.submit(intent(Action::parameter, .5f, true, 42, 17)) &&
                !gesture.submit(intent(Action::parameter, .5f, true, 4000001234u, 18)) &&
                !gesture.submit(intent(Action::mix, .5f)) && observe(gesture) == acceptedBaseline,
            "Accepted gesture cannot change target/generation or admit another action");
    require(gesture.submit(intent(Action::parameter, .5f, true, 4000001234u, 17)) && gesture.serial == 1 &&
                gesture.parameter == 4000001234u && gesture.generation == 17 &&
                gesture.action == Action::parameter && gesture.desired.load() == 0x000000013f000000ULL,
            "Accepted coalescing changes only the owned atomic desired state");
    require(gesture.submit(intent(Action::parameter, .875f, false, 4000001234u, 17)) &&
                gesture.desired.load() == 0x000000003f600000ULL,
            "End publishes final value and closed gesture together");
    const auto closed = observe(gesture);
    require(!gesture.submit(intent(Action::parameter, .125f, true, 4000001234u, 17)) &&
                !gesture.submit(intent(Action::parameter, .25f, false, 4000001234u, 17)) &&
                observe(gesture) == closed,
            "Closed gesture refuses both reopening and every late final-value update");
    finish(gesture, Outcome::completed);
    require(gesture.phase.load(std::memory_order_acquire) == Phase::terminal &&
                gesture.outcome == Outcome::completed &&
                std::string(gesture.operation) == "original-operation-1" &&
                std::string(gesture.code) == "original-terminal-code",
            "CONTROL release publishes actual terminal outcome and matching immutable operation");
    const auto terminal = observe(gesture);
    require(!gesture.submit(intent(Action::mix, .25f)) && observe(gesture) == terminal,
            "Unconsumed terminal cannot be overwritten by a new request");
    // GUI consumes/copies the terminal before publishing idle; no other producer.
    const std::string completedMessage = gesture.message;
    gesture.phase.store(Phase::idle, std::memory_order_release);
    require(completedMessage == "original-terminal-message" && gesture.submit(intent(Action::mix, 0)) &&
                gesture.serial == 2 && gesture.phase.load() == Phase::submitted,
            "Only terminal consumption releases the next serial");
    for (auto phase : {Phase::submitted, Phase::accepted})
    {
        if (phase == Phase::accepted)
            require(accept(gesture), "Mix host accepts the actual request");
        const auto busy = observe(gesture);
        require(!gesture.submit(intent(Action::mix, 1)) && !gesture.submit(intent(Action::bypass, 0)) &&
                    observe(gesture) == busy,
                "Nonparameter requests never coalesce while submitted or accepted");
    }
    finish(gesture, Outcome::failed);
    require(gesture.phase.load(std::memory_order_acquire) == Phase::terminal &&
                gesture.outcome == Outcome::failed,
            "Failed acknowledgement remains terminal until consumed");
    gesture.phase.store(Phase::idle, std::memory_order_release);
    require(gesture.submit(intent(Action::bypass, 1)) && gesture.serial == 3 && accept(gesture),
            "Binary bypass admits a new immutable serial");
    finish(gesture, Outcome::cancelled);
    require(gesture.phase.load(std::memory_order_acquire) == Phase::terminal &&
                gesture.outcome == Outcome::cancelled,
            "Cancelled acknowledgement is explicit terminal outcome");

    Mailbox wrapping;
    wrapping.serial = std::numeric_limits<unsigned>::max() - 1;
    require(wrapping.submit(intent(Action::mix, 0)) &&
                wrapping.serial == std::numeric_limits<unsigned>::max() && accept(wrapping),
            "Last nonzero serial is admitted");
    finish(wrapping, Outcome::completed);
    require(wrapping.phase.load(std::memory_order_acquire) == Phase::terminal,
            "Wrap test consumes an actual terminal");
    wrapping.phase.store(Phase::idle, std::memory_order_release);
    require(wrapping.submit(intent(Action::mix, 1)) && wrapping.serial == 1,
            "Serial wrap skips reserved zero");

    // Actual SPSC execution. Only the producer writes request identity and only
    // the consumer writes terminal fields; every plain read obeys phase acquire.
    Mailbox concurrent;
    constexpr unsigned requests = 256, spinBudget = 200000;
    std::atomic<bool> stop{false};
    std::atomic<unsigned> failures{0}, submitted{0}, coalesced{0}, accepted{0}, acknowledged{0};
    const auto fail = [&](unsigned bits)
    {
        failures.fetch_or(bits, std::memory_order_relaxed);
        stop.store(true, std::memory_order_release);
    };
    const auto await = [&](Phase phase)
    {
        for (unsigned spin = 0; spin < spinBudget; ++spin)
        {
            if (stop.load(std::memory_order_acquire))
                return false;
            if (concurrent.phase.load(std::memory_order_acquire) == phase)
                return true;
            std::this_thread::yield();
        }
        return false;
    };
    std::thread producer, consumer;
    try
    {
        consumer = std::thread(
            [&]
            {
                for (unsigned number = 1; number <= requests && !stop.load(std::memory_order_acquire);
                     ++number)
                {
                    if (!await(Phase::submitted))
                    {
                        fail(1);
                        return;
                    }
                    if (concurrent.serial != number || concurrent.action != Action::parameter ||
                        concurrent.parameter != 0x10000000u + number ||
                        concurrent.generation != 0x20000000u + number ||
                        concurrent.io !=
                            vibera::PluginMainIo{int(number % 32), int((number + 7) % 32), 1, 3} ||
                        !accept(concurrent))
                    {
                        fail(2);
                        return;
                    }
                    accepted.fetch_add(1, std::memory_order_relaxed);
                    bool ended = false;
                    for (unsigned spin = 0; spin < spinBudget && !stop.load(std::memory_order_acquire);
                         ++spin)
                    {
                        const auto word = concurrent.desired.load(std::memory_order_acquire);
                        // Independent literal IEEE754/flag pairs. An end flag with
                        // any earlier value, or an open flag with final value, fails.
                        if (word != 0x000000013e000000ULL && word != 0x000000013e800000ULL &&
                            word != 0x000000013f000000ULL && word != 0x000000003f600000ULL)
                        {
                            fail(4);
                            return;
                        }
                        if (word == 0x000000003f600000ULL)
                        {
                            ended = true;
                            break;
                        }
                        std::this_thread::yield();
                    }
                    if (!ended)
                    {
                        fail(8);
                        return;
                    }
                    concurrent.outcome = static_cast<Outcome>(number % 3);
                    std::snprintf(concurrent.operation, sizeof(concurrent.operation), "request-%u", number);
                    std::snprintf(concurrent.code, sizeof(concurrent.code), "terminal-%u", number);
                    std::snprintf(concurrent.message, sizeof(concurrent.message), "generation-%u",
                                  0x20000000u + number);
                    concurrent.phase.store(Phase::terminal, std::memory_order_release);
                    // No plain mailbox field may be touched after this release:
                    // GUI can consume and start the next request immediately.
                }
            });
        producer = std::thread(
            [&]
            {
                for (unsigned number = 1; number <= requests && !stop.load(std::memory_order_acquire);
                     ++number)
                {
                    const auto parameter = 0x10000000u + number, generation = 0x20000000u + number;
                    auto request = intent(Action::parameter, .125f, true, parameter, generation);
                    request.io = {int(number % 32), int((number + 7) % 32), 1, 3};
                    if (!concurrent.submit(request))
                    {
                        fail(16);
                        return;
                    }
                    submitted.fetch_add(1, std::memory_order_relaxed);
                    if (!await(Phase::accepted))
                    {
                        fail(32);
                        return;
                    }
                    for (auto update : {intent(Action::parameter, .25f, true, parameter, generation),
                                        intent(Action::parameter, .5f, true, parameter, generation),
                                        intent(Action::parameter, .875f, false, parameter, generation)})
                    {
                        if (!concurrent.submit(update))
                        {
                            fail(64);
                            return;
                        }
                        coalesced.fetch_add(1, std::memory_order_relaxed);
                    }
                    if (concurrent.submit(intent(Action::parameter, .125f, true, parameter, generation)) ||
                        concurrent.submit(intent(Action::parameter, .25f, false, parameter, generation)))
                    {
                        fail(128);
                        return;
                    }
                    if (!await(Phase::terminal))
                    {
                        fail(256);
                        return;
                    }
                    char expectedOperation[48]{}, expectedCode[80]{}, expectedMessage[256]{};
                    std::snprintf(expectedOperation, sizeof(expectedOperation), "request-%u", number);
                    std::snprintf(expectedCode, sizeof(expectedCode), "terminal-%u", number);
                    std::snprintf(expectedMessage, sizeof(expectedMessage), "generation-%u", generation);
                    if (concurrent.serial != number || concurrent.parameter != parameter ||
                        concurrent.generation != generation || concurrent.action != Action::parameter ||
                        concurrent.io != request.io ||
                        concurrent.desired.load(std::memory_order_acquire) != 0x000000003f600000ULL ||
                        concurrent.outcome != static_cast<Outcome>(number % 3) ||
                        std::strcmp(concurrent.operation, expectedOperation) != 0 ||
                        std::strcmp(concurrent.code, expectedCode) != 0 ||
                        std::strcmp(concurrent.message, expectedMessage) != 0)
                    {
                        fail(512);
                        return;
                    }
                    acknowledged.fetch_add(1, std::memory_order_relaxed);
                    concurrent.phase.store(Phase::idle, std::memory_order_release);
                }
            });
        producer.join();
        consumer.join();
    }
    catch (...)
    {
        stop.store(true, std::memory_order_release);
        if (producer.joinable())
            producer.join();
        if (consumer.joinable())
            consumer.join();
        throw;
    }
    require(failures.load() == 0,
            "Wrapper actual two-thread immutable target/atomic desired/terminal ownership, failure bits=" +
                std::to_string(failures.load()));
    require(submitted.load() == requests && accepted.load() == requests && acknowledged.load() == requests &&
                coalesced.load() == requests * 3 && concurrent.serial == requests &&
                concurrent.phase.load() == Phase::idle,
            "Every accepted serial is acknowledged exactly once, with coalescing and final closed value "
            "retained");
}
