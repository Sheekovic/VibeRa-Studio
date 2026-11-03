// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PluginHost.h"
#include "InitialSourceEpochFence.h"
#include <tracktion_engine/tracktion_engine.h>
#include <array>
#include <atomic>
#include <limits>
#include <span>
#include <vector>

namespace vibera::source_membership
{
using Session = tracktion::engine::SourceBurstSession;
class SourceMembershipBroker; // CONTROL-owned ordinary membership coordinator.

// Only an INITIAL, fixed, required Host set. There is deliberately no live
// replace/promote/remove API. An optional candidate refusal must not abort an
// incumbent; that needs a separate acknowledged membership protocol.
enum class Status : unsigned
{
    ready,
    invalidConfiguration,
    memberLimit,
    invalidMember,
    duplicateMember,
    incompatibleTransport,
    unsupportedLatency,
    sourceUnavailable,
    allocationFailed,
    preparationFailed,
    invalidScope,
    serialReused,
    beginRefused,
    endRefused,
    sourceAborted,
    counterExhausted,
    fixedSourceEpochRefused
};
enum class Phase : unsigned
{
    prepared,
    open,
    closed,
    aborted,
    fault
};
struct Receipt
{
    Phase phase = Phase::prepared;
    Status status = Status::ready;
    std::uint64_t sessionGeneration = 0, deviceEpoch = 0, membershipGeneration = 0;
    std::uint64_t serial = 0, lastClosedSerial = 0, lastAbortedSerial = 0;
    std::size_t failedMember = std::numeric_limits<std::size_t>::max();
    PluginTransportStatus host = PluginTransportStatus::ready;
    transport::BurstStatus source = transport::BurstStatus::ready;
    int quota = 0;
};

namespace detail
{
// Original narrow injectable scope seam. Production adapters below call the
// EXISTING PluginHost API. Tests using this seam qualify the coordinator only,
// never actual worker/graph/PCM compatibility.
struct Target
{
    void *owner = nullptr;
    PluginTransportOutcome (*begin)(void *, int) noexcept = nullptr;
    PluginTransportOutcome (*end)(void *) noexcept = nullptr;
    void (*abort)(void *) noexcept = nullptr;
};

class ScopeLedger final
{
  public:
    ScopeLedger(Session::Configuration configuration, std::uint64_t membership,
                std::span<const Target> preparedTargets) noexcept
        : config(configuration), targets(preparedTargets)
    {
        current.sessionGeneration = config.generation;
        current.deviceEpoch = config.deviceEpoch;
        current.membershipGeneration = membership;
        publish();
    }
    ScopeLedger(const ScopeLedger &) = delete;
    ScopeLedger &operator=(const ScopeLedger &) = delete;

    // One native processing owner, or CONTROL after native callback exclusion.
    // Never concurrently call these methods from CONTROL and DSP. Scalar
    // read() is the only concurrent CONTROL observation API.
    bool begin(const Session::Outcome &source) noexcept
    {
        if (current.status != Status::ready)
            return false;
        if (!canPublish())
            return refuse(Status::counterExhausted);
        if (current.phase == Phase::open || !valid(source) || source.deviceOffset != 0)
            return refuse(Status::invalidScope);
        if (source.physicalSerial <= lastAttempted)
            return refuse(Status::serialReused);
        lastAttempted = source.physicalSerial;
        current.serial = source.physicalSerial;
        current.quota = source.sourceQuota;
        physicalFrames = source.physicalFrames;
        for (std::size_t i = 0; i != targets.size(); ++i)
        {
            const auto result = targets[i].begin(targets[i].owner, source.sourceQuota);
            if (!result.succeeded() || result.source.status != transport::BurstStatus::ready ||
                result.source.deviceEpoch != source.deviceEpoch ||
                result.source.callbackSerial != source.physicalSerial ||
                result.source.actualFrames != source.sourceQuota ||
                result.source.preparedFrames != config.maximumSourceFrames ||
                result.sourceQuota != source.sourceQuota)
                return refuse(Status::beginRefused, i, result);
        }
        current.phase = Phase::open;
        publish();
        return true; // Scope admission, NOT asynchronous DSP completion.
    }

    bool end(const Session::Outcome &source) noexcept
    {
        if (current.status != Status::ready)
            return false;
        if (!canPublish())
            return refuse(Status::counterExhausted);
        if (current.phase != Phase::open || !valid(source) || source.physicalSerial != current.serial ||
            source.physicalFrames != physicalFrames || source.sourceQuota != current.quota ||
            source.deviceOffset != source.physicalFrames || source.sourceProcessed < 0 ||
            source.sourceProcessed > source.sourceQuota)
            return refuse(Status::invalidScope);
        bool okay = true;
        for (std::size_t i = 0; i != targets.size(); ++i)
        {
            // Visit all members even if an earlier end refused. A later member
            // must not retain an open scope. abortAll below remains mandatory.
            const auto result = targets[i].end(targets[i].owner);
            // Current Host end reports ready/quota/processed, NOT epoch/serial.
            // Its validatePhysicalSpan(0) checks the same begun serial internally;
            // begin above first bound that actual serial to the native source.
            if (!result.succeeded() || result.source.status != transport::BurstStatus::ready ||
                result.sourceQuota != current.quota || result.processedFrames < 0 ||
                result.processedFrames > current.quota)
            {
                if (okay)
                    rememberFailure(Status::endRefused, i, result);
                okay = false;
            }
        }
        if (!okay)
        {
            abortAll();
            current.phase = Phase::fault;
            publish();
            return false;
        }
        current.lastClosedSerial = current.serial;
        current.phase = Phase::closed;
        publish();
        return true; // Native session has NOT yet published its own closed state.
    }

    void abort(const Session::Outcome &source) noexcept
    {
        abortAll(); // Also safe after partial begin, ended members and repeat abort.
        if (current.status == Status::ready)
            current.status = Status::sourceAborted;
        current.lastAbortedSerial = source.physicalSerial;
        current.phase = Phase::aborted;
        publish(); // Sticky fault; never clears a worker fault or retries a serial.
    }

    bool sourceEpochRefused(const Session::Outcome& source,std::size_t member)noexcept
    {
        abortAll(); // Existing metadata-only abort; never Host transport fault/reset/shutdown.
        if(current.status==Status::ready)current.status=Status::fixedSourceEpochRefused;
        current.failedMember=member;current.serial=source.physicalSerial;
        current.lastAbortedSerial=source.physicalSerial;current.phase=Phase::fault;
        publish();return false;
    }
    // Three bounded coherent attempts. False preserves caller state. Fixed
    // scalar atomics avoid a C++ data race in a traditional plain-POD seqlock.
    bool read(Receipt &output) const noexcept
    {
        for (int attempt = 0; attempt != 3; ++attempt)
        {
            const auto before = revision.load();
            if (before & 1)
                continue;
            std::array<std::uint64_t, fields> value{};
            for (std::size_t i = 0; i != fields; ++i)
                value[i] = published[i].load();
            if (before != revision.load())
                continue;
            Receipt candidate;
            candidate.phase = Phase(value[0]);
            candidate.status = Status(value[1]);
            candidate.sessionGeneration = value[2];
            candidate.deviceEpoch = value[3];
            candidate.membershipGeneration = value[4];
            candidate.serial = value[5];
            candidate.lastClosedSerial = value[6];
            candidate.lastAbortedSerial = value[7];
            candidate.failedMember =
                value[8] == 0 ? std::numeric_limits<std::size_t>::max() : std::size_t(value[8] - 1);
            candidate.host = PluginTransportStatus(value[9]);
            candidate.source = transport::BurstStatus(value[10]);
            candidate.quota = int(value[11]);
            output = candidate;
            return true;
        }
        return false;
    }

    // Pair with a CURRENT coherent native outcome, not just observer end ACK.
    // A peer context may fail AFTER our end() without another observer callback.
    // This is a compatibility predicate, NOT a graph-publication lock/authority.
    static bool closedWith(const Receipt &receipt, const Session::Outcome &source) noexcept
    {
        return receipt.status == Status::ready && receipt.phase == Phase::closed &&
               source.status == Session::Status::ready && !source.failed && !source.retired &&
               !source.scopeOpen && receipt.sessionGeneration == source.generation &&
               receipt.deviceEpoch == source.deviceEpoch && receipt.serial != 0 &&
               receipt.lastClosedSerial == receipt.serial && receipt.serial == source.physicalSerial;
    }

  private:
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    static constexpr std::size_t fields = 12;
    const Session::Configuration config;
    const std::span<const Target> targets; // CONTROL-retained backing, immutable for this lifetime.
    Receipt current;                       // Sole native processing writer only; never read on CONTROL.
    int physicalFrames = 0;
    std::uint64_t lastAttempted = 0, writtenRevision = 0;
    mutable std::atomic<std::uint64_t> revision{0};
    std::array<std::atomic<std::uint64_t>, fields> published{};

    bool valid(const Session::Outcome &value) const noexcept
    {
        return value.status == Session::Status::ready && !value.failed && !value.retired && value.scopeOpen &&
               value.generation == config.generation && value.deviceEpoch == config.deviceEpoch &&
               value.physicalSerial != 0 && value.physicalFrames > 0 &&
               value.physicalFrames <= config.maximumPhysicalFrames && value.sourceQuota > 0 &&
               value.sourceQuota <= config.maximumSourceFrames;
    }
    bool canPublish() const noexcept
    {
        return writtenRevision <= std::numeric_limits<std::uint64_t>::max() - 4;
    }
    void publish() noexcept
    {
        if (writtenRevision > std::numeric_limits<std::uint64_t>::max() - 2)
            return; // Never wrap. At exhaustion a previous fault receipt remains sticky.
        revision.store(writtenRevision + 1);
        const std::array<std::uint64_t, fields> values{
            unsigned(current.phase),
            unsigned(current.status),
            current.sessionGeneration,
            current.deviceEpoch,
            current.membershipGeneration,
            current.serial,
            current.lastClosedSerial,
            current.lastAbortedSerial,
            current.failedMember == std::numeric_limits<std::size_t>::max() ? 0 : current.failedMember + 1,
            unsigned(current.host),
            unsigned(current.source),
            std::uint64_t(current.quota)};
        for (std::size_t i = 0; i != fields; ++i)
            published[i].store(values[i]);
        writtenRevision += 2;
        revision.store(writtenRevision);
    }
    void abortAll() noexcept
    {
        for (const auto &target : targets)
            target.abort(target.owner);
    }
    void rememberFailure(Status status, std::size_t member, PluginTransportOutcome result) noexcept
    {
        current.status = status;
        current.failedMember = member;
        current.host = result.status;
        current.source = result.source.status;
    }
    bool refuse(Status status, std::size_t member = std::numeric_limits<std::size_t>::max(),
                PluginTransportOutcome result = {}) noexcept
    {
        rememberFailure(status, member, result);
        abortAll();
        current.phase = Phase::fault;
        publish();
        return false;
    }
};
} // namespace detail

// CONTROL prepares/pins immutable members before installing an INITIAL session.
// The actual session owns this Observer; owner release/destruction MUST occur
// on CONTROL after native context/callback exclusion. No use_count heuristics.
// CALLER PROOF REQUIRED: exact same JUCE manager/session lineage as plan, and
// one exclusive installed membership authority per Host. Scalar epochs/serials
// can coincide across managers; prepare() deduplicates only this binding span.
// Existing Host begin detects a second scope but abort-all can then disrupt its
// incumbent. Never share a Host with another processing observer. This factory
// does not reserve cross-session ownership and is not production activation.
class PreparedSourceHosts final : public Session::Observer
{
  public:
    using Ptr = std::shared_ptr<PreparedSourceHosts>;
    struct Binding
    {
        const PluginSpec *specification = nullptr; // Borrowed only for prepare().
        std::shared_ptr<PluginHost> host;
        PluginBusRequest buses{};
        initial_source_epoch::Fence::Ptr fixedEpoch; // Optional INITIAL-only actual Runtime owner.
    };
    struct Preparation
    {
        Status status = Status::invalidConfiguration;
        std::size_t member = std::numeric_limits<std::size_t>::max();
        bool succeeded() const noexcept
        {
            return status == Status::ready;
        }
    };

    // Caller serializes CONTROL host preparation, device changes and graph
    // replacement. maximumMembers is an explicit admitted graph resource bound,
    // not a second/contradictory hard-coded Project format limit. Empty is legal.
    static Preparation prepare(const PreparedPluginTransport::Ptr &plan, const Session::Configuration &config,
                               std::uint64_t membershipGeneration, std::span<const Binding> bindings,
                               std::size_t maximumMembers, Ptr &output,
                               std::shared_ptr<initial_source_epoch::Health> fixedHealth = {}) noexcept
    {
        Preparation result;
        try
        {
            if (!plan || membershipGeneration == 0 || config.generation == 0 || config.deviceEpoch == 0)
                return result;
            const auto &budget = plan->budget();
            if (config.deviceEpoch != budget.deviceEpoch || config.sampleRate != plan->sampleRate() ||
                config.deviceFrames != budget.configuredFrames ||
                config.maximumPhysicalFrames != budget.physicalFrames ||
                config.maximumSourceFrames != budget.sourceFrames ||
                config.maximumSourceScalePpm != budget.maximumSourceScalePpm)
                return result;
            if (bindings.size() > maximumMembers)
                return {Status::memberLimit};
            juce::AudioDeviceManager::PhysicalCallbackSnapshot physical;
            const bool knownPhysical = plan->read(physical);
            const auto physicalAdmission = transport::admitPhysicalBurst(knownPhysical, physical, budget);
            // Every candidate checks historical counterexamples, including when
            // a later CURRENT callback happens to fit the old smaller bound.
            // Before the first callback of THIS epoch, JUCE resets both frame
            // fields but retains its globally monotonic serial. Do not require
            // serial zero after a legitimate same-format device restart.
            if (!knownPhysical || physical.maximumOuterFrames > budget.physicalFrames ||
                (physicalAdmission.status != transport::BurstStatus::ready &&
                 !(physicalAdmission.status == transport::BurstStatus::noPhysicalCallback &&
                   physical.outerFrames == 0 && physical.maximumOuterFrames == 0)))
                return {Status::sourceUnavailable};
            std::vector<Member> members;
            std::vector<detail::Target> targets;
            members.reserve(bindings.size());
            targets.reserve(bindings.size());
            for (std::size_t i = 0; i != bindings.size(); ++i)
            {
                result.member = i;
                const auto &binding = bindings[i];
                if (!binding.specification || binding.specification->id <= 0 || !binding.host ||
                    binding.host->runtimeIdentity().isEmpty())
                    return {Status::invalidMember, i};
                const auto &host = binding.host;
                for (const auto &member : members)
                    if (member.id == binding.specification->id || member.host.get() == host.get() ||
                        member.instance == host->runtimeIdentity())
                        return {Status::duplicateMember, i};
                if (host->offline() || !host->matchesPrepared(*binding.specification, config.sampleRate,
                                                              budget.quantum, plan, binding.buses))
                    return {Status::incompatibleTransport, i};
                const auto latency = host->declaredLatency();
                if (!latency.valid(budget.quantum) ||
                    latency.nativeSamples >= PreparedPluginTransport::dryCapacity - budget.transportSamples)
                    return {Status::unsupportedLatency, i};
                if((fixedHealth&&!binding.fixedEpoch) || (binding.fixedEpoch&&
                   (!fixedHealth||binding.fixedEpoch->pluginId()!=binding.specification->id||
                    !binding.fixedEpoch->matchesControl(*host,plan,fixedHealth))))
                    return {Status::invalidMember,i};
                // No state capture, editor close, parameter write, Host reset,
                // policy conversion or worker creation in membership preparation.
                members.push_back({binding.specification->id, host, host->runtimeIdentity(),binding.fixedEpoch});
                targets.push_back({host.get(), beginHost, endHost, abortHost});
            }
            Ptr candidate(new PreparedSourceHosts(plan, config, membershipGeneration, std::move(members),
                                                  std::move(targets),std::move(fixedHealth)));
            output = std::move(candidate); // Failure has never changed output or incumbent Hosts.
            return {Status::ready};
        }
        catch (const std::bad_alloc &)
        {
            result.status = Status::allocationFailed;
        }
        catch (...)
        {
            result.status = Status::preparationFailed;
        }
        return result;
    }

    bool begin(const Session::Outcome &source) noexcept override
    {
        // Validate ALL immutable fixed members before opening ANY Host scope.
        // Native SourceBurstSession calls this before native input/clock work.
        if(fixedHealth)
        {
            for(std::size_t i=0;i<members.size();++i)
                if(members[i].fixedEpoch&&!members[i].fixedEpoch->declaration(*members[i].host))
                    return ledger.sourceEpochRefused(source,i);
            if(!fixedHealth->healthy())return ledger.sourceEpochRefused(source,0);
        }
        return ledger.begin(source);
    }
    bool end(const Session::Outcome &source) noexcept override
    {
        const bool closed=ledger.end(source); // Always close ALL actual opened Host scopes first.
        if(fixedHealth)
        {
            for(std::size_t i=0;i<members.size();++i)
                if(members[i].fixedEpoch&&!members[i].fixedEpoch->healthy())
                    return ledger.sourceEpochRefused(source,i);
            if(!fixedHealth->healthy())return ledger.sourceEpochRefused(source,0);
        }
        return closed;
    }
    void abort(const Session::Outcome &source) noexcept override
    {
        ledger.abort(source);
    }
    bool read(Receipt &result) const noexcept
    {
        return ledger.read(result);
    }
    struct FixedEpochObservation
    {
        std::int64_t plugin=0;const void* runtime=nullptr;
        FixedSourcePcmEpochGuard::Expected expected;
        std::optional<initial_source_epoch::Fence::Failure> failure;
    };
    std::vector<FixedEpochObservation> fixedEpochObservationsControl()const
    {
        std::vector<FixedEpochObservation> result;
        for(const auto& member:members)if(member.fixedEpoch)
            result.push_back({member.id,member.fixedEpoch->sourceRuntime(),member.fixedEpoch->expectation(),
                member.fixedEpoch->failure()});
        return result; // Expected immutable values + owned write-once acquired failure only.
    }
    std::size_t size() const noexcept
    {
        return members.size();
    } // Immutable, not active DSP count.
    static bool closedWith(const Receipt &receipt, const Session::Outcome &source) noexcept
    {
        return detail::ScopeLedger::closedWith(receipt, source);
    }

  private:
    friend class SourceMembershipBroker; // Immutable metadata only; no ledger/queue inspection.
    struct Member
    {
        std::int64_t id;
        std::shared_ptr<PluginHost> host;
        juce::String instance; // CONTROL-only identity; never accessed in callback.
        initial_source_epoch::Fence::Ptr fixedEpoch;
    };
    const PreparedPluginTransport::Ptr plan;
    const Session::Configuration configuration;
    const std::uint64_t membershipGeneration;
    const std::vector<Member> members;
    const std::vector<detail::Target> targets;
    const std::shared_ptr<initial_source_epoch::Health> fixedHealth;
    detail::ScopeLedger ledger; // Destroyed before its targets/pins; no move after construction.
    PreparedSourceHosts(PreparedPluginTransport::Ptr transportPlan, Session::Configuration config,
                        std::uint64_t generation, std::vector<Member> retained,
                        std::vector<detail::Target> preparedTargets,
                        std::shared_ptr<initial_source_epoch::Health> sourceHealth)
        : plan(std::move(transportPlan)), configuration(config), membershipGeneration(generation),
          members(std::move(retained)), targets(std::move(preparedTargets)),
          fixedHealth(std::move(sourceHealth)), ledger(config, generation, targets)
    {
    }
    static PluginTransportOutcome beginHost(void *owner, int quota) noexcept
    {
        return static_cast<PluginHost *>(owner)->beginPhysicalBurst(quota);
    }
    static PluginTransportOutcome endHost(void *owner) noexcept
    {
        return static_cast<PluginHost *>(owner)->endPhysicalBurst();
    }
    static void abortHost(void *owner) noexcept
    {
        static_cast<PluginHost *>(owner)->abortPhysicalBurst();
    }
};
} // namespace vibera::source_membership
