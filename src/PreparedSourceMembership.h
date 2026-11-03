// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PreparedSourceHosts.h"
#include "Commands.h"
#include <tracktion_graph/tracktion_graph.h>
#include <algorithm>
#include <memory>

namespace vibera
{
class AudioEngine;
namespace source_membership
{
// One installed Observer and one processing owner. CONTROL retains every bank,
// request and ticket until acknowledged native close or actual context exclusion.
// No bank ever moves/drains/resets Host packets, voices, serials or scopes.
class SourceMembershipBroker final : public Session::Observer
{
    struct Request;

  public:
    using Ptr = std::shared_ptr<SourceMembershipBroker>;
    enum class State : unsigned
    {
        pending,
        expanded,
        cancelled,
        completed,
        failed
    };
    enum class Error : unsigned
    {
        none,
        wrongThread,
        invalidAuthority,
        staleTicket,
        busy,
        invalidBank,
        sourceUnavailable,
        invalidScope,
        bankRefused,
        counterExhausted,
        pendingNativeClose,
        retirementRequired,
        allocationFailed,
        tooLate
    };
    struct Outcome
    {
        State state = State::pending;
        Error error = Error::none;
        std::uint64_t operation = 0, completedRequest = 0, membership = 0, physicalSerial = 0;
    };
    class Ticket final
    {
      public:
        using Ptr = std::shared_ptr<Ticket>;
        ~Ticket(); // CONTROL only. Broker pins an accepted ticket independently.
        Ticket(const Ticket &) = delete;
        Ticket &operator=(const Ticket &) = delete;

      private:
        friend class SourceMembershipBroker;
        Ticket() = default;
        SourceMembershipBroker *broker = nullptr; // Identity only, never owning.
        const Controller *controller = nullptr;
        const PreparedControllerChange *model = nullptr; // Precommit validation only.
        std::uint64_t revision = 0, operation = 0;
        juce::String identity;
        juce::ValueTree sourceState, destinationState; // CONTROL only, immutable.
        PreparedSourceHosts::Ptr source, expanded, final;
        std::unique_ptr<Request> forward, reverse, removal;
        bool requested = false, cancelRequested = false, removalRequested = false, graphPosted = false;
        std::uint64_t postedSerial = 0;
        const tracktion::engine::EditPlaybackContext::PreparedGraphChange *validatedGraph = nullptr;
    };
    // Only AudioEngine may attest predecessor DSP-reader exclusion, after exact
    // native selection followed by a current paired source/bank close under its
    // allocated zero-auxiliary policy. This does NOT attest object destruction:
    // native cached graphs/PluginPtrs may legitimately keep worker owners alive.
    class DspQuiescencePermit final
    {
        friend class vibera::AudioEngine;
        friend class SourceMembershipBroker;
        DspQuiescencePermit(const SourceMembershipBroker &b, const Ticket &t, const Controller &c,
                            const tracktion::engine::EditPlaybackContext &x, std::uint64_t selected,
                            const Session::Outcome &closed) noexcept
            : broker(&b), ticket(&t), controller(&c), context(&x), revision(c.revision), serial(selected),
              physicalSerial(closed.physicalSerial), sessionGeneration(closed.generation),
              deviceEpoch(closed.deviceEpoch)
        {
        }
        const SourceMembershipBroker *broker;
        const Ticket *ticket;
        const Controller *controller;
        const tracktion::engine::EditPlaybackContext *context;
        std::uint64_t revision, serial, physicalSerial, sessionGeneration, deviceEpoch;
    };

    static PreparedSourceHosts::Preparation prepare(const PreparedPluginTransport::Ptr &plan,
                                                    const Session::Configuration &config,
                                                    std::uint64_t initialMembershipGeneration,
                                                    std::span<const PreparedSourceHosts::Binding> bindings,
                                                    std::size_t maximumMembers, Ptr &output) noexcept
    {
        try
        {
            PreparedSourceHosts::Ptr initial;
            const auto ready = PreparedSourceHosts::prepare(plan, config, initialMembershipGeneration,
                                                            bindings, maximumMembers, initial);
            if (!ready.succeeded())
                return ready;
            Ptr next(new SourceMembershipBroker(plan, config, std::move(initial)));
            output = std::move(next);
            return {Status::ready};
        }
        catch (const std::bad_alloc &)
        {
            return {Status::allocationFailed};
        }
        catch (...)
        {
            return {Status::preparationFailed};
        }
    }
    ~SourceMembershipBroker() override = default; // CONTROL after context exclusion.
    SourceMembershipBroker(const SourceMembershipBroker &) = delete;
    SourceMembershipBroker &operator=(const SourceMembershipBroker &) = delete;

    // A coherent selected/closed bank receipt, NOT merely an enqueued request.
    // Pair with CURRENT native Session::Outcome before claiming physical close.
    bool read(Receipt &output) const noexcept
    {
        Snapshot state;
        if (!snapshot(state))
            return false;
        auto *bank = reinterpret_cast<PreparedSourceHosts *>(state.bank);
        Receipt value;
        if (!bank || !bank->read(value))
            return false;
        Snapshot after;
        if (!snapshot(after) || state.version != after.version)
            return false;
        if (state.error != Error::none)
        {
            value.phase = Phase::fault;
            if (value.status == Status::ready)
                value.status = Status::sourceAborted;
        }
        output = value;
        return true;
    }
    std::size_t size() const noexcept // Sole CONTROL thread; no owning copy.
    {
        Snapshot state;
        return snapshot(state) && state.bank
                   ? reinterpret_cast<const PreparedSourceHosts *>(state.bank)->size()
                   : 0;
    }

    bool begin(const Session::Outcome &source) noexcept override
    {
        if (fault != Error::none)
            return false;
        if (!canPublish())
            return fail(Error::counterExhausted);
        if (opened || !validSource(source) || source.deviceOffset != 0 ||
            source.physicalSerial <= lastAttempted)
            return fail(Error::invalidScope);
        lastAttempted = source.physicalSerial;
        const auto *request = pending.exchange(nullptr, std::memory_order_acq_rel);
        if (request)
        {
            if (active != request->from && !(request->reverse && active == request->to))
                return fail(Error::staleTicket);
            active = request->to;
        }
        currentRequest = request;
        opened = active; // Even partial begin failure must abort THIS bank.
        currentSerial = source.physicalSerial;
        if (!opened->begin(source))
            return fail(Error::bankRefused);
        return true;
    }
    bool end(const Session::Outcome &source) noexcept override
    {
        if (fault != Error::none)
            return false;
        if (!canPublish())
            return fail(Error::counterExhausted);
        if (!opened || !validSource(source) || source.physicalSerial != currentSerial ||
            source.deviceOffset != source.physicalFrames)
            return fail(Error::invalidScope);
        if (!opened->end(source))
            return fail(Error::bankRefused);
        auto *closedBank = opened;
        const auto completed = currentRequest ? currentRequest->serial : completedRequest;
        opened = nullptr;
        currentRequest = nullptr; // No request access AFTER publishing its ACK.
        completedRequest = completed;
        closedSerial = source.physicalSerial;
        publish(closedBank);
        return true; // Native Session publishes its closed receipt LATER.
    }
    void abort(const Session::Outcome &source) noexcept override
    {
        // Native abort follows failed begin/end. Close only the bank selected
        // for THIS scope; never a CONTROL-selected or previous bank.
        if (opened)
            opened->abort(source);
        opened = nullptr;
        currentRequest = nullptr;
        if (fault == Error::none)
            fault = Error::sourceUnavailable;
        publish(active);
    }

  private:
    friend class vibera::AudioEngine;
    struct Request
    {
        PreparedSourceHosts *from;
        PreparedSourceHosts *to;
        std::uint64_t serial;
        bool reverse = false;
    };
    struct Snapshot
    {
        std::uint64_t version = 0, bank = 0, completed = 0, serial = 0;
        Error error = Error::none;
    };
    const PreparedPluginTransport::Ptr plan;
    const Session::Configuration configuration;
    PreparedSourceHosts::Ptr retained;      // Sole CONTROL owner for settled bank.
    Ticket::Ptr outstanding;                // Pins all published requests/banks, including cancellation.
    const Controller *controller = nullptr; // CONTROL only, caller lifetime prerequisite.
    const tracktion::engine::EditPlaybackContext *context = nullptr;
    const Session *session = nullptr; // Identity only: no Observer->Session ownership cycle.
    juce::String identity;
    std::uint64_t nextOperation = 0, nextMembership = 0; // CONTROL only.
    std::atomic<const Request *> pending{nullptr};
    PreparedSourceHosts *active = nullptr; // Sole callback owner, never CONTROL-read.
    PreparedSourceHosts *opened = nullptr;
    const Request *currentRequest = nullptr;
    std::uint64_t lastAttempted = 0, currentSerial = 0, closedSerial = 0, completedRequest = 0;
    std::uint64_t writtenRevision = 0;
    Error fault = Error::none; // Sole callback writer.
    std::atomic<std::uint64_t> revision{0};
    std::array<std::atomic<std::uint64_t>, 4> receipt{};
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free &&
                  std::atomic<const Request *>::is_always_lock_free);

    SourceMembershipBroker(PreparedPluginTransport::Ptr p, Session::Configuration c,
                           PreparedSourceHosts::Ptr first)
        : plan(std::move(p)), configuration(c), retained(std::move(first)),
          nextMembership(retained->membershipGeneration), active(retained.get())
    {
        publish(active); // Before publication to any callback.
    }
    static bool control() noexcept
    {
        auto *messages = juce::MessageManager::getInstanceWithoutCreating();
        return messages && messages->isThisTheMessageThread();
    }
    static bool sameConfig(const Session::Configuration &a, const Session::Configuration &b) noexcept
    {
        return a.deviceEpoch == b.deviceEpoch && a.generation == b.generation &&
               a.sampleRate == b.sampleRate && a.deviceFrames == b.deviceFrames &&
               a.maximumPhysicalFrames == b.maximumPhysicalFrames &&
               a.maximumSourceFrames == b.maximumSourceFrames &&
               a.maximumSourceScalePpm == b.maximumSourceScalePpm;
    }
    bool liveSource() const noexcept
    {
        juce::AudioDeviceManager::PhysicalCallbackSnapshot physical;
        const bool known = plan->read(physical);
        const auto admitted = transport::admitPhysicalBurst(known, physical, plan->budget());
        return known && physical.maximumOuterFrames <= plan->budget().physicalFrames &&
               (admitted.status == transport::BurstStatus::ready ||
                (admitted.status == transport::BurstStatus::noPhysicalCallback && physical.outerFrames == 0 &&
                 physical.maximumOuterFrames == 0));
    }
    bool authority(const Controller &owner) const noexcept
    {
        return control() && controller == &owner && owner.projectIdentity == identity && context && session &&
               sameConfig(session->configuration(), configuration) && liveSource();
    }
    Error bindTransitions(Controller &owner, tracktion::engine::EditPlaybackContext &actualContext,
                          const Session &actualSession)
    try
    {
        if (!control())
            return Error::wrongThread;
        if (!sameConfig(actualSession.configuration(), configuration) || !liveSource())
            return Error::invalidAuthority;
        if (controller)
            return controller == &owner && context == &actualContext && session == &actualSession &&
                           identity == owner.projectIdentity
                       ? Error::none
                       : Error::invalidAuthority;
        if (outstanding)
            return Error::busy;
        auto preparedIdentity = owner.projectIdentity; // Fallible BEFORE publication.
        identity.swapWith(preparedIdentity);
        controller = &owner;
        context = &actualContext;
        session = &actualSession;
        return Error::none; // These fields are NEVER read by DSP.
    }
    catch (...)
    {
        return Error::allocationFailed;
    }
    static bool contains(const PreparedSourceHosts &bank, const PreparedSourceHosts::Member &member)
    {
        for (const auto &value : bank.members)
            if (value.id == member.id)
                return value.host == member.host && value.instance == member.instance &&
                       value.fixedEpoch == member.fixedEpoch;
        return false;
    }
    static bool subset(const PreparedSourceHosts &a, const PreparedSourceHosts &b)
    {
        for (const auto &member : a.members)
            if (!contains(b, member))
                return false;
        return true;
    }
    Error prepareExpansion(Controller &owner, PreparedControllerChange &model,
                           std::span<const PreparedSourceHosts::Binding> bindings, std::size_t maximumMembers,
                           Ticket::Ptr &output)
    try
    {
        if (!authority(owner))
            return Error::invalidAuthority;
        if (outstanding || pending.load(std::memory_order_acquire))
            return Error::busy;
        if (nextOperation >= (std::numeric_limits<std::uint64_t>::max() - 2) / 3 ||
            nextMembership == std::numeric_limits<std::uint64_t>::max())
            return Error::counterExhausted;
        if (model.isNoOp() || !model.destinationBeforeCommit() ||
            owner.validatePreparedChange(model).failed())
            return Error::staleTicket;
        Snapshot seen;
        if (!snapshot(seen) || seen.error != Error::none || seen.bank != address(retained.get()))
            return Error::sourceUnavailable;
        PreparedSourceHosts::Ptr bank;
        if (!PreparedSourceHosts::prepare(plan, configuration, nextMembership + 1, bindings, maximumMembers,
                                          bank)
                 .succeeded())
            return Error::invalidBank;
        if (!subset(*retained, *bank))
            return Error::invalidBank;
        const auto &destination = *model.destinationBeforeCommit();
        for (const auto &member : bank->members)
            if (!contains(*retained, member))
            {
                const auto *spec = destination.plugin(member.id);
                if (!spec || owner.project.plugin(member.id) ||
                    !member.host->matchesPrepared(*spec, configuration.sampleRate, plan->budget().quantum,
                                                  plan) ||
                    member.host->recoveryState() != spec->state)
                    return Error::invalidBank;
            }
        const auto newMemberPresent = [&](const PluginSpec &spec)
        {
            return owner.project.plugin(spec.id) ||
                   std::any_of(bank->members.begin(), bank->members.end(),
                               [&](const auto &member) { return member.id == spec.id; });
        };
        for (const auto &channel : destination.channels)
            if (channel.instrument && !newMemberPresent(*channel.instrument))
                return Error::invalidBank;
        for (const auto &insert : destination.mixer)
            for (const auto &spec : insert.effects)
                if (!newMemberPresent(spec))
                    return Error::invalidBank;
        auto ticket = Ticket::Ptr(new Ticket);
        ticket->broker = this;
        ticket->controller = &owner;
        ticket->model = &model;
        ticket->revision = owner.revision;
        ticket->identity = owner.projectIdentity;
        ticket->sourceState = owner.project.state().createCopy();
        ticket->destinationState = model.destinationBeforeCommit()->state().createCopy();
        ticket->operation = nextOperation + 1;
        ticket->source = retained;
        ticket->expanded = std::move(bank);
        const auto first = ticket->operation * 3;
        ticket->forward =
            std::make_unique<Request>(Request{ticket->source.get(), ticket->expanded.get(), first});
        ticket->reverse =
            std::make_unique<Request>(Request{ticket->expanded.get(), ticket->source.get(), first + 1, true});
        ++nextMembership;
        ++nextOperation;
        output = std::move(ticket);
        return Error::none; // No pending request or Host mutation.
    }
    catch (...)
    {
        return Error::allocationFailed;
    }
    Error requestExpansion(const Ticket::Ptr &ticket, Controller &owner, PreparedControllerChange &model)
    try
    {
        if (!ticket || ticket->broker != this || ticket->controller != &owner || ticket->model != &model ||
            !authority(owner) || ticket->identity != owner.projectIdentity ||
            ticket->revision != owner.revision ||
            !ticket->sourceState.isEquivalentTo(owner.project.state()) ||
            owner.validatePreparedChange(model).failed())
            return Error::staleTicket;
        if (outstanding || ticket->requested || pending.load(std::memory_order_acquire))
            return Error::busy;
        Snapshot seen;
        if (!snapshot(seen) || seen.error != Error::none || seen.bank != address(ticket->source.get()))
            return Error::sourceUnavailable;
        outstanding = ticket; // CONTROL pins BEFORE release publication.
        ticket->requested = true;
        pending.store(ticket->forward.get(), std::memory_order_release);
        return Error::none; // Admission/pending, not expanded or completed.
    }
    catch (...)
    {
        return Error::allocationFailed;
    }
    Error cancel(const Ticket::Ptr &ticket) noexcept
    {
        if (!control())
            return Error::wrongThread;
        if (!ticket || outstanding != ticket || ticket->broker != this || ticket->removalRequested)
            return Error::staleTicket;
        if (ticket->graphPosted)
            return Error::tooLate;
        if (ticket->cancelRequested)
            return Error::none;
        Snapshot seen;
        Session::Outcome native;
        if (!snapshot(seen) || seen.error != Error::none || !session || !session->read(native) ||
            native.failed || native.retired)
            return Error::sourceUnavailable;
        ticket->cancelRequested = true;
        ticket->validatedGraph = nullptr;
        // May replace an unread forward request OR follow a forward consumed
        // concurrently. Reverse accepts either retained source or expanded bank.
        pending.store(ticket->reverse.get(), std::memory_order_release);
        return Error::none; // Only a real reverse end ACK can report cancelled.
    }
    Error validateForGraphPost(const Ticket::Ptr &ticket, Controller &owner, PreparedControllerChange &model,
                               const tracktion::engine::EditPlaybackContext::PreparedGraphChange &graph,
                               const Session::Outcome &native)
    try
    {
        if (!ticket || outstanding != ticket || ticket->broker != this)
            return Error::staleTicket;
        ticket->validatedGraph = nullptr;
        if (!authority(owner) || ticket->controller != &owner || ticket->model != &model ||
            ticket->identity != owner.projectIdentity || ticket->revision != owner.revision ||
            ticket->cancelRequested || ticket->graphPosted ||
            !ticket->sourceState.isEquivalentTo(owner.project.state()) ||
            owner.validatePreparedChange(model).failed())
            return Error::staleTicket;
        if (observe(*ticket, native).state != State::expanded)
            return Error::pendingNativeClose;
        if (!graph.publicationSerial() ||
            context->validatePreparedGraph(graph) !=
                tracktion::engine::EditPlaybackContext::ReadyGraphChangeStatus::ready)
            return Error::invalidAuthority; // Existing native/PDC/MIDI gates remain mandatory.
        ticket->validatedGraph = &graph;
        return Error::none; // Sole CONTROL, immediately before post/commit, no pump/reentry.
    }
    catch (...)
    {
        return Error::allocationFailed;
    }
    Error markGraphPosted(const Ticket::Ptr &ticket,
                          const tracktion::engine::EditPlaybackContext::PreparedGraphChange &graph) noexcept
    {
        if (!control())
            return Error::wrongThread;
        if (!ticket || outstanding != ticket || ticket->cancelRequested || ticket->graphPosted ||
            ticket->validatedGraph != &graph || !graph.wasPublished() || !graph.publicationSerial() ||
            context->validatePreparedGraph(graph) !=
                tracktion::engine::EditPlaybackContext::ReadyGraphChangeStatus::published)
            return Error::staleTicket;
        // Actual published native ticket, not a guessed serial. Latch before
        // readback: an unavailable receipt must NEVER allow cancellation after post.
        ticket->graphPosted = true;
        ticket->postedSerial = graph.publicationSerial();
        const auto selected = context->getGraphSelection();
        return selected.postedSerial == ticket->postedSerial ? Error::none : Error::sourceUnavailable;
    }
    Error requestRemoval(const Ticket::Ptr &ticket, Controller &owner,
                         std::span<const PreparedSourceHosts::Binding> bindings, std::size_t maximumMembers,
                         const DspQuiescencePermit &proof)
    try
    {
        if (!ticket || outstanding != ticket || !authority(owner) || ticket->cancelRequested ||
            ticket->removalRequested || proof.broker != this || proof.ticket != ticket.get() ||
            proof.controller != &owner || proof.context != context || proof.revision != owner.revision ||
            !ticket->graphPosted || proof.serial != ticket->postedSerial || !proof.serial ||
            !proof.physicalSerial || proof.sessionGeneration != configuration.generation ||
            proof.deviceEpoch != configuration.deviceEpoch ||
            !ticket->destinationState.isEquivalentTo(owner.project.state()))
            return Error::retirementRequired;
        const auto selected = context->getGraphSelection();
        if (selected.state != tracktion::graph::GraphSelectionState::selected ||
            selected.selectedSerial != proof.serial || selected.postedSerial != proof.serial)
            return Error::retirementRequired;
        Session::Outcome native;
        if (!session->read(native) || native.physicalSerial < proof.physicalSerial ||
            observe(*ticket, native).state != State::expanded)
            return Error::pendingNativeClose;
        if (nextMembership == std::numeric_limits<std::uint64_t>::max())
            return Error::counterExhausted;
        PreparedSourceHosts::Ptr bank;
        if (!PreparedSourceHosts::prepare(plan, configuration, nextMembership + 1, bindings, maximumMembers,
                                          bank)
                 .succeeded() ||
            !subset(*bank, *ticket->expanded))
            return Error::invalidBank;
        for (const auto &member : ticket->expanded->members)
            if (bool(owner.project.plugin(member.id)) != contains(*bank, member))
                return Error::invalidBank;
        auto request =
            std::make_unique<Request>(Request{ticket->expanded.get(), bank.get(), ticket->operation * 3 + 2});
        ticket->final = std::move(bank);
        ticket->removal = std::move(request);
        ++nextMembership;
        ticket->removalRequested = true;
        pending.store(ticket->removal.get(), std::memory_order_release);
        return Error::none; // No removed Host can retire before actual close ACK.
    }
    catch (...)
    {
        return Error::allocationFailed;
    }
    Outcome observe(const Ticket &ticket, const Session::Outcome &native) const noexcept
    {
        Outcome out;
        out.operation = ticket.operation;
        if (ticket.broker != this || outstanding.get() != &ticket)
        {
            out.state = State::failed;
            out.error = Error::staleTicket;
            return out;
        }
        Snapshot state;
        if (!snapshot(state))
        {
            out.error = Error::pendingNativeClose;
            return out;
        }
        out.completedRequest = state.completed;
        out.physicalSerial = state.serial;
        auto *bank = reinterpret_cast<PreparedSourceHosts *>(state.bank);
        out.membership = bank ? bank->membershipGeneration : 0;
        if (state.error != Error::none)
        {
            out.state = State::failed;
            out.error = state.error;
            return out;
        }
        Receipt closed;
        Session::Outcome actual;
        if (!session || !session->read(actual))
        {
            out.error = Error::pendingNativeClose;
            return out;
        }
        if (actual.failed || actual.retired)
        {
            out.state = State::failed;
            out.error = Error::sourceUnavailable;
            return out;
        }
        if (actual.generation != native.generation || actual.deviceEpoch != native.deviceEpoch ||
            actual.physicalSerial != native.physicalSerial || actual.failed != native.failed ||
            actual.retired != native.retired || actual.scopeOpen != native.scopeOpen ||
            actual.status != native.status)
        {
            out.error = Error::pendingNativeClose;
            return out;
        }
        if (!bank || !bank->read(closed) || !PreparedSourceHosts::closedWith(closed, native) ||
            closed.serial != state.serial || pending.load(std::memory_order_acquire))
        {
            out.error = Error::pendingNativeClose;
            return out;
        }
        if (ticket.cancelRequested)
        {
            if (state.completed == ticket.reverse->serial && bank == ticket.source.get())
                out.state = State::cancelled;
        }
        else if (ticket.removalRequested)
        {
            if (state.completed == ticket.removal->serial && bank == ticket.final.get())
                out.state = State::completed;
        }
        else if (state.completed == ticket.forward->serial && bank == ticket.expanded.get())
            out.state = State::expanded;
        return out;
    }
    Error acknowledge(const Ticket::Ptr &ticket, const Session::Outcome &native) noexcept
    {
        if (!control())
            return Error::wrongThread;
        if (!ticket || outstanding != ticket || !session)
            return Error::staleTicket;
        const auto status = observe(*ticket, native);
        if (status.state != State::completed && status.state != State::cancelled)
            return Error::pendingNativeClose;
        retained = status.state == State::cancelled ? ticket->source : ticket->final;
        outstanding.reset(); // CONTROL only; no callback accesses request after ACK.
        return Error::none;
    }
    static std::uint64_t address(const void *pointer) noexcept
    {
        return std::uint64_t(reinterpret_cast<std::uintptr_t>(pointer));
    }
    bool validSource(const Session::Outcome &s) const noexcept
    {
        return s.status == Session::Status::ready && !s.failed && !s.retired && s.scopeOpen &&
               s.generation == configuration.generation && s.deviceEpoch == configuration.deviceEpoch &&
               s.physicalSerial && s.physicalFrames > 0 &&
               s.physicalFrames <= configuration.maximumPhysicalFrames && s.sourceQuota > 0 &&
               s.sourceQuota <= configuration.maximumSourceFrames;
    }
    bool canPublish() const noexcept
    {
        return writtenRevision <= std::numeric_limits<std::uint64_t>::max() - 4;
    }
    bool fail(Error error) noexcept
    {
        if (fault == Error::none)
            fault = error;
        publish(active);
        return false;
    }
    void publish(PreparedSourceHosts *bank) noexcept
    {
        if (writtenRevision > std::numeric_limits<std::uint64_t>::max() - 2)
            return;
        revision.store(writtenRevision + 1);
        receipt[0].store(address(bank));
        receipt[1].store(completedRequest);
        receipt[2].store(closedSerial);
        receipt[3].store(unsigned(fault));
        writtenRevision += 2;
        revision.store(writtenRevision);
    }
    bool snapshot(Snapshot &out) const noexcept
    {
        for (int attempt = 0; attempt != 3; ++attempt)
        {
            const auto before = revision.load();
            if (before & 1)
                continue;
            Snapshot next{before, receipt[0].load(), receipt[1].load(), receipt[2].load(),
                          Error(receipt[3].load())};
            if (before == revision.load())
            {
                out = next;
                return true;
            }
        }
        return false;
    }
};
inline SourceMembershipBroker::Ticket::~Ticket() = default;
} // namespace source_membership
} // namespace vibera
