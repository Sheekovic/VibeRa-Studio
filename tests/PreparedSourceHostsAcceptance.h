// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PreparedSourceHosts.h"
#include "SyntheticDevice.h"
#include <array>

// Original coordinator UNIT seams plus actual JUCE manager / immutable-plan
// preparation. FakeScope is not a plugin fixture, worker, graph or PCM oracle.
// Include after Acceptance.cpp's require(). No native desktop/hardware used.
namespace prepared_source_hosts_acceptance
{
using namespace vibera;
using namespace vibera::source_membership;
namespace scope_detail = vibera::source_membership::detail;
struct FakeScope
{
    int begins = 0, ends = 0, aborts = 0, quota = 0, processed = 0;
    bool open = false, refuseBegin = false, refuseEnd = false;
    std::uint64_t epoch = 0, serial = 0;
    int reportedQuotaDelta = 0, reportedMaximum = 64;
    transport::BurstStatus reportedSource = transport::BurstStatus::ready;
    scope_detail::Target target() noexcept
    {
        return {this, begin, end, abort};
    }
    static PluginTransportOutcome begin(void *pointer, int value) noexcept
    {
        auto &self = *static_cast<FakeScope *>(pointer);
        ++self.begins;
        if (self.open)
        {
            PluginTransportOutcome active;
            active.status = PluginTransportStatus::burstAlreadyActive;
            return active;
        }
        self.quota = value;
        self.open = true; // Refusal can occur AFTER a scope was partly opened.
        PluginTransportOutcome result;
        result.status =
            self.refuseBegin ? PluginTransportStatus::workerUnavailable : PluginTransportStatus::ready;
        result.sourceQuota = value;
        result.source.status = self.reportedSource;
        result.source.deviceEpoch = self.epoch;
        result.source.callbackSerial = self.serial;
        result.source.actualFrames = value + self.reportedQuotaDelta;
        result.source.preparedFrames = self.reportedMaximum;
        return result;
    }
    static PluginTransportOutcome end(void *pointer) noexcept
    {
        auto &self = *static_cast<FakeScope *>(pointer);
        ++self.ends;
        const bool wasOpen = self.open;
        self.open = false;
        PluginTransportOutcome result;
        result.status = !wasOpen         ? PluginTransportStatus::burstMissing
                        : self.refuseEnd ? PluginTransportStatus::workerUnavailable
                                         : PluginTransportStatus::ready;
        result.source.status = transport::BurstStatus::ready;
        result.sourceQuota = self.quota;
        result.processedFrames = self.processed;
        return result;
    }
    static void abort(void *pointer) noexcept
    {
        auto &self = *static_cast<FakeScope *>(pointer);
        ++self.aborts;
        self.open = false;
    }
};
inline void stamp(std::span<FakeScope> scopes, std::uint64_t serial) noexcept
{
    for (auto &scope : scopes)
    {
        scope.epoch = 9;
        scope.serial = serial;
    }
}
inline Session::Configuration config()
{
    return {9, 17, 48000, 64, 64, 64, 1000000};
}
inline Session::Outcome source(std::uint64_t serial, bool ending = false)
{
    Session::Outcome result;
    result.status = Session::Status::ready;
    result.generation = 17;
    result.deviceEpoch = 9;
    result.physicalSerial = serial;
    result.physicalFrames = 64;
    result.sourceQuota = 64;
    result.deviceOffset = ending ? 64 : 0;
    result.sourceProcessed = ending ? 64 : 0;
    result.scopeOpen = true;
    return result;
}
inline Receipt read(const scope_detail::ScopeLedger &ledger)
{
    Receipt result;
    require(ledger.read(result), "Bounded coordinator receipt available after serialized call");
    return result;
}
inline void scopeTests()
{
    {
        std::array<FakeScope, 3> scopes;
        stamp(scopes, 1);
        scopes[0].processed = 64;
        scopes[1].processed = 0; // Native graph pruning/mute receives no DSP calls.
        scopes[2].processed = 32;
        const std::array targets{scopes[0].target(), scopes[1].target(), scopes[2].target()};
        scope_detail::ScopeLedger ledger(config(), 23, targets);
        auto initial = read(ledger);
        require(initial.phase == Phase::prepared && initial.sessionGeneration == 17 &&
                    initial.deviceEpoch == 9 && initial.membershipGeneration == 23 && initial.serial == 0,
                "Immutable membership/session identities start prepared, never completed");
        require(ledger.begin(source(1)), "All required members begin once");
        require(read(ledger).phase == Phase::open, "Scope open is not completion");
        require(ledger.end(source(1, true)), "All/fewer/zero processed frames are legal");
        auto receipt = read(ledger);
        auto closed = source(1, true);
        require(!scope_detail::ScopeLedger::closedWith(receipt, closed),
                "Observer end precedes native scope close");
        closed.scopeOpen = false;
        require(scope_detail::ScopeLedger::closedWith(receipt, closed),
                "Same coherent native serial closes scope ACK");
        closed.failed = true;
        closed.status = Session::Status::peerRefused;
        require(!scope_detail::ScopeLedger::closedWith(receipt, closed) &&
                    read(ledger).phase == Phase::closed,
                "Later peer abort invalidates ACK even without a second observer notification");
        closed.failed = false;
        closed.status = Session::Status::ready;
        closed.retired = true;
        require(!scope_detail::ScopeLedger::closedWith(receipt, closed),
                "Native retirement invalidates closed ACK");
        for (const auto &scope : scopes)
            require(scope.begins == 1 && scope.ends == 1 && scope.aborts == 0 && !scope.open,
                    "Every required member includes zero-pruned member exactly once");
        stamp(scopes, 2);
        require(ledger.begin(source(2)) && ledger.end(source(2, true)),
                "Next genuine serial retains fixed bank");
        require(!ledger.begin(source(2)) && read(ledger).status == Status::serialReused,
                "Closed physical serial cannot be reopened");
        for (const auto &scope : scopes)
            require(scope.begins == 2 && scope.ends == 2 && scope.aborts == 1,
                    "Serial refusal does not begin any member and closes metadata only");
        require(!ledger.begin(source(3)), "First coordinator fault remains sticky");
    }
    {
        std::array<FakeScope, 3> scopes;
        stamp(scopes, 4);
        scopes[1].refuseBegin = true;
        const std::array targets{scopes[0].target(), scopes[1].target(), scopes[2].target()};
        scope_detail::ScopeLedger ledger(config(), 24, targets);
        require(!ledger.begin(source(4)), "Partial begin refusal fails required membership");
        auto receipt = read(ledger);
        require(receipt.phase == Phase::fault && receipt.status == Status::beginRefused &&
                    receipt.failedMember == 1 && receipt.host == PluginTransportStatus::workerUnavailable,
                "Structured first failing member and actual scope status retained");
        require(scopes[0].begins == 1 && scopes[1].begins == 1 && scopes[2].begins == 0,
                "No later begin after refusal");
        for (const auto &scope : scopes)
            require(scope.aborts == 1 && !scope.open,
                    "Abort ALL members, including partially opened and untouched");
        ledger.abort(source(4)); // Native always follows begin(false) by abort().
        receipt = read(ledger);
        require(receipt.phase == Phase::aborted && receipt.status == Status::beginRefused &&
                    receipt.lastAbortedSerial == 4 && receipt.failedMember == 1,
                "Native abort is idempotent metadata cleanup and retains original refusal");
        for (const auto &scope : scopes)
            require(scope.aborts == 2, "Repeat abort remains bounded with no double lifecycle owner release");
    }
    {
        std::array<FakeScope, 3> scopes;
        stamp(scopes, 5);
        scopes[0].refuseEnd = true;
        const std::array targets{scopes[0].target(), scopes[1].target(), scopes[2].target()};
        scope_detail::ScopeLedger ledger(config(), 25, targets);
        require(ledger.begin(source(5)) && !ledger.end(source(5, true)),
                "Real required end refusal is failure");
        for (const auto &scope : scopes)
            require(scope.ends == 1 && scope.aborts == 1 && !scope.open,
                    "End visits all members before abort-all despite first refusal");
        require(read(ledger).lastClosedSerial == 0 && read(ledger).status == Status::endRefused,
                "Failed end never invents a successful closed serial");
    }
    for (int invalid = 0; invalid != 4; ++invalid)
    {
        FakeScope scope;
        const std::array targets{scope.target()};
        stamp(std::span(&scope, 1), 6);
        scope_detail::ScopeLedger ledger(config(), 26, targets);
        auto outcome = source(6);
        if (invalid == 0)
            ++outcome.generation;
        if (invalid == 1)
            ++outcome.deviceEpoch;
        if (invalid == 2)
            outcome.sourceQuota = 65;
        if (invalid == 3)
            outcome.deviceOffset = 1;
        require(!ledger.begin(outcome) && scope.begins == 0 && scope.aborts == 1 &&
                    read(ledger).status == Status::invalidScope,
                "Wrong session/epoch, excess source quota and suffix-first refusal before any begin");
    }
    // An actual successful Host begin carries its retained manager's native
    // epoch/serial/quota. Never manufacture that identity from native Outcome.
    for (int mismatch = 0; mismatch != 5; ++mismatch)
    {
        FakeScope scope;
        stamp(std::span(&scope, 1), 9);
        if (mismatch == 0)
            ++scope.epoch;
        if (mismatch == 1)
            ++scope.serial;
        if (mismatch == 2)
            ++scope.reportedQuotaDelta;
        if (mismatch == 3)
            ++scope.reportedMaximum;
        if (mismatch == 4)
            scope.reportedSource = transport::BurstStatus::deviceChanged;
        const std::array targets{scope.target()};
        scope_detail::ScopeLedger ledger(config(), 29, targets);
        require(!ledger.begin(source(9)) && read(ledger).status == Status::beginRefused &&
                    read(ledger).failedMember == 0 && scope.aborts == 1 && !scope.open,
                "Successful target status alone cannot join wrong actual epoch/serial/quota/source result");
    }
    {
        FakeScope shared;
        stamp(std::span(&shared, 1), 10);
        const std::array targets{shared.target()};
        scope_detail::ScopeLedger incumbent(config(), 30, targets), forbiddenSecond(config(), 31, targets);
        require(incumbent.begin(source(10)), "Original unit target admits exclusive incumbent");
        require(!forbiddenSecond.begin(source(10)) && !shared.open && shared.aborts == 1 &&
                    read(forbiddenSecond).host == PluginTransportStatus::burstAlreadyActive,
                "NEGATIVE unsupported sharing: second observer cleanup can disrupt incumbent scope");
        require(!incumbent.end(source(10, true)) && read(incumbent).status == Status::endRefused,
                "No reservation enforcement claimed: caller must prohibit cross-session shared Hosts");
    }
    {
        scope_detail::ScopeLedger empty(config(), 27, {});
        require(empty.begin(source(7)) && empty.end(source(7, true)),
                "Sampler-only empty fixed worker bank still acknowledges genuine source scope");
        auto peer = source(7, true);
        peer.failed = true;
        empty.abort(peer);
        require(read(empty).status == Status::sourceAborted && read(empty).lastAbortedSerial == 7,
                "Explicit native abort after empty membership remains a sticky failure");
    }
    {
        FakeScope scope;
        const std::array targets{scope.target()};
        stamp(std::span(&scope, 1), 8);
        scope_detail::ScopeLedger ledger(config(), 28, targets);
        const auto allocations = callbackAllocations.load(), destructions = callbackDestructions.load();
        const bool previous = callbackAllocationProbe;
        callbackAllocationProbe = true;
        const bool begun = ledger.begin(source(8));
        const bool ended = ledger.end(source(8, true));
        Receipt receipt;
        const bool known = ledger.read(receipt);
        ledger.abort(source(8));
        callbackAllocationProbe = previous;
        require(begun && ended && known && callbackAllocations.load() == allocations &&
                    callbackDestructions.load() == destructions,
                "Coordinator scope/journal/abort seam observes no allocation/destruction; not actual DSP "
                "timing proof");
    }
}

// Actual manager/plan, no worker subprocess. Strong-output guarantee uses an
// actual valid incumbent EMPTY observer, not an invented constructor/test hook.
inline void preparationTests()
{
    juce::AudioDeviceManager manager;
    manager.addAudioDeviceType(std::make_unique<SyntheticDeviceType>());
    juce::XmlElement setup("DEVICESETUP");
    setup.setAttribute("deviceType", "VibeRa synthetic");
    setup.setAttribute("audioOutputDeviceName", "In-memory output");
    setup.setAttribute("audioDeviceRate", 48000.);
    setup.setAttribute("audioDeviceBufferSize", 256);
    require(manager.initialise(0, 2, &setup, false).isEmpty(), "Actual isolated original provider manager");
    PreparedPluginTransport::Ptr plan;
    require(PreparedPluginTransport::prepare(manager, 256, {0, 1}, 1000000, 256, plan).succeeded(),
            "Actual immutable prepared transport from retained manager telemetry");
    const auto &budget = plan->budget();
    Session::Configuration configuration{budget.deviceEpoch,          37,
                                         plan->sampleRate(),          budget.configuredFrames,
                                         budget.physicalFrames,       budget.sourceFrames,
                                         budget.maximumSourceScalePpm};
    PreparedSourceHosts::Ptr incumbent;
    require(PreparedSourceHosts::prepare(plan, configuration, 41, {}, 0, incumbent).succeeded() &&
                incumbent && incumbent->size() == 0,
            "Actual fixed empty source membership prepared before first physical packet");
    const auto original = incumbent;
    Receipt before;
    require(incumbent->read(before) && before.phase == Phase::prepared, "Prepared incumbent receipt");
    auto wrong = configuration;
    ++wrong.generation;  // Session generation is caller-selected and can differ.
    ++wrong.deviceEpoch; // Device epoch MUST exactly match retained plan.
    require(PreparedSourceHosts::prepare(plan, wrong, 42, {}, 0, incumbent).status ==
                    Status::invalidConfiguration &&
                incumbent == original,
            "Rejected epoch preserves exact incumbent owner, no replacement/pause/reset");
    require(PreparedSourceHosts::prepare({}, configuration, 42, {}, 0, incumbent).status ==
                    Status::invalidConfiguration &&
                incumbent == original,
            "Missing plan cannot manufacture a policy or replace incumbent");
    const std::array<PreparedSourceHosts::Binding, 1> nullBinding{};
    require(PreparedSourceHosts::prepare(plan, configuration, 42, nullBinding, 0, incumbent).status ==
                    Status::memberLimit &&
                incumbent == original,
            "Explicit resource bound refused without output mutation");
    require(PreparedSourceHosts::prepare(plan, configuration, 42, nullBinding, 1, incumbent).status ==
                    Status::invalidMember &&
                incumbent == original,
            "Invalid exact member refused without output mutation");
    Receipt after;
    require(incumbent->read(after) && after.phase == before.phase && after.status == before.status &&
                after.serial == before.serial && after.membershipGeneration == before.membershipGeneration,
            "Rejected candidates do not mutate incumbent scope journal");
    for (int budgetToFailure = 0; budgetToFailure != 2; ++budgetToFailure)
    {
        // Empty membership uses object then shared control-block allocation.
        // Either failure must destroy only the PRIVATE candidate on CONTROL.
        preparationAllocationFailureHit = false;
        preparationAllocationFailureAfter = budgetToFailure;
        const auto failed = PreparedSourceHosts::prepare(plan, configuration, 43, {}, 0, incumbent);
        preparationAllocationFailureAfter = -1;
        require(preparationAllocationFailureHit && failed.status == Status::allocationFailed &&
                    incumbent == original,
                "Injected CONTROL allocation refusal preserves exact incumbent and pending scope receipt");
    }
    auto *device = dynamic_cast<SyntheticDevice *>(manager.getCurrentAudioDevice());
    require(device != nullptr, "Owned actual in-memory provider for historical bound counterexample");
    juce::AudioBuffer<float> largeOutput(2, 512), shortOutput(2, 64);
    device->process(largeOutput);
    device->process(shortOutput); // Current outer fits; historical maximum still invalidates256 plan.
    juce::AudioDeviceManager::PhysicalCallbackSnapshot actual;
    require(plan->read(actual) && actual.maximumOuterFrames >= 512 && actual.outerFrames == 64,
            "Genuine JUCE physical history exceeds bound even after a small current callback");
    require(PreparedSourceHosts::prepare(plan, configuration, 44, {}, 0, incumbent).status ==
                    Status::sourceUnavailable &&
                incumbent == original,
            "Historical physical counterexample always rejects old smaller plan without replacing incumbent");
    const auto previousSerial = actual.callbackSerial;
    const auto previousEpoch = actual.deviceEpoch;
    manager.closeAudioDevice();
    manager.restartLastAudioDevice();
    require(manager.readPhysicalCallbackSnapshot(actual) && actual.active &&
                actual.deviceEpoch != previousEpoch && actual.callbackSerial == previousSerial &&
                previousSerial > 0 && actual.outerFrames == 0 && actual.maximumOuterFrames == 0,
            "Genuine device restart resets epoch frame history but retains the monotonic physical serial");
    PreparedPluginTransport::Ptr restartedPlan;
    require(PreparedPluginTransport::prepare(manager, 256, {0, 1}, 1000000, 256, restartedPlan).succeeded(),
            "Fresh exact-epoch source budget before its first physical callback");
    auto restartedConfig = configuration;
    restartedConfig.deviceEpoch = actual.deviceEpoch;
    restartedConfig.generation = 45;
    require(PreparedSourceHosts::prepare(restartedPlan, restartedConfig, 45, {}, 0, incumbent).succeeded() &&
                incumbent != original,
            "Fresh membership accepts genuine first-callback absence after a nonzero-serial restart");
    auto retained = incumbent;
    require(PreparedSourceHosts::prepare(plan, configuration, 46, {}, 0, incumbent).status ==
                    Status::sourceUnavailable &&
                incumbent == retained,
            "A restarted device never authorizes the obsolete epoch's membership");
}
} // namespace prepared_source_hosts_acceptance

static void preparedSourceHostsTests()
{
    prepared_source_hosts_acceptance::scopeTests();
    prepared_source_hosts_acceptance::preparationTests();
}
