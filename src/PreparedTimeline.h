// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "LiveTimeline.h"
#include <limits>
#include <type_traits>

namespace vibera
{
// Control-thread-only publication primitive. Each actual graph must pin its own
// timeline handle BEFORE publication. Never rebind a plugin shared with an active
// graph. This owner does not select/process/adopt an audio graph.
class TimelineProjection
{
    struct State
    {
        std::shared_ptr<LiveTimeline> current;
        std::uint64_t revision = 1;
        bool alive = true;
        explicit State(std::shared_ptr<LiveTimeline> initial) : current(std::move(initial)) {}
    };
    std::shared_ptr<State> state;

  public:
    enum class Status
    {
        unprepared,
        ready,
        invalidSource,
        missingDocument,
        serialExhausted,
        wrongOwner,
        staleSource,
        changedCandidate,
        ownerGone,
        alreadyPublished,
        published
    };
    class Prepared
    {
        friend class TimelineProjection;
        std::shared_ptr<State> owner;
        std::shared_ptr<LiveTimeline> source, candidate, retired;
        std::uint64_t revision = 0, sourcePublication = 0, candidatePublication = 0;
        Status status = Status::unprepared;

      public:
        Prepared() = default;
        Prepared(const Prepared &) = delete;
        Prepared &operator=(const Prepared &) = delete;
        Prepared(Prepared &&) noexcept = default;
        Prepared &operator=(Prepared &&) noexcept = default;
        // Lifetime operations/destruction are CONTROL ONLY, outside canonical
        // commit. Candidate graph owners pin this exact handle during preparation.
        const std::shared_ptr<LiveTimeline> &preparedTimeline() const noexcept
        {
            return candidate;
        }
        const std::shared_ptr<LiveTimeline> &retiredTimeline() const noexcept
        {
            return retired;
        }
        Status preparationStatus() const noexcept
        {
            return status;
        }
    };

    explicit TimelineProjection(std::shared_ptr<LiveTimeline> initial)
        : state(std::make_shared<State>(std::move(initial)))
    {
    }
    TimelineProjection(const TimelineProjection &) = delete;
    TimelineProjection &operator=(const TimelineProjection &) = delete;
    TimelineProjection(TimelineProjection &&) = delete;
    TimelineProjection &operator=(TimelineProjection &&) = delete;
    ~TimelineProjection()
    {
        state->alive = false;
    } // Control only; outstanding tickets retain identity.

    const std::shared_ptr<LiveTimeline> &currentTimeline() const noexcept
    {
        return state->current;
    }

    // All compilation, native bindings, cut inputs and other immutable resources
    // must already belong to document. Allocations can throw ONLY here; document
    // remains owned by the caller if allocating the private timeline fails.
    Prepared prepare(std::unique_ptr<LiveDocument> &document) const
    {
        Prepared result;
        if (!state->current)
        {
            result.status = Status::invalidSource;
            return result;
        }
        if (!document)
        {
            result.status = Status::missingDocument;
            return result;
        }
        if (state->revision == std::numeric_limits<std::uint64_t>::max() ||
            state->current->controlPublicationSerial() == std::numeric_limits<std::uint64_t>::max())
        {
            result.status = Status::serialExhausted;
            return result;
        }
        auto candidate = std::make_shared<LiveTimeline>();
        // Empty private scope has no published/read slots. This cannot refuse;
        // it searches/installs and retires nothing before canonical admission.
        const bool installed = candidate->publish(document);
        jassert(installed);
        if (!installed)
            return result; // Defensive only; no application/model mutation.
        result.owner = state;
        result.source = state->current;
        result.sourcePublication = result.source->controlPublicationSerial();
        result.candidatePublication = candidate->controlPublicationSerial();
        result.revision = state->revision;
        result.candidate = std::move(candidate);
        result.status = Status::ready;
        return result;
    }

    // Final admission BEFORE any canonical model/history mutation. One control
    // owner, no message pumping/reentry until all admitted publications complete.
    Status validate(const Prepared &ready) const noexcept
    {
        if (ready.status == Status::published)
            return Status::alreadyPublished;
        if (ready.status != Status::ready)
            return ready.status;
        if (!ready.owner || !ready.owner->alive)
            return Status::ownerGone;
        if (ready.owner != state)
            return Status::wrongOwner;
        if (ready.revision != state->revision || ready.source != state->current ||
            ready.sourcePublication != state->current->controlPublicationSerial())
            return Status::staleSource;
        if (!ready.candidate || ready.candidatePublication != ready.candidate->controlPublicationSerial())
            return Status::changedCandidate;
        return Status::ready;
    }

    // Already-reserved ownership slots and scalar updates only. No callbacks,
    // shared-owner release, slot search, publication allocation or destruction.
    // After final validation above, failure is excluded by sole-control ownership;
    // retain the defensive refusal for callers outside a canonical transaction.
    Status publish(Prepared &ready) noexcept
    {
        const auto admission = validate(ready);
        if (admission != Status::ready)
            return admission;
        state->current.swap(ready.candidate);
        ready.retired.swap(ready.candidate);
        ++state->revision;
        ready.status = Status::published;
        return Status::published;
    }
};
static_assert(noexcept(
    std::declval<std::shared_ptr<LiveTimeline> &>().swap(std::declval<std::shared_ptr<LiveTimeline> &>())));
static_assert(std::is_nothrow_move_constructible_v<TimelineProjection::Prepared>);
} // namespace vibera
