// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PreparedTimeline.h"
#include "CallbackAllocationProbe.h"

// Original source-level acceptance draft. Root wires/runs in the existing test
// executable. No actual Tracktion adoption, audio output or hardware is claimed.
static void preparedTimelineAcceptance()
{
    using Publication = vibera::TimelineProjection;
    using Status = Publication::Status;
    auto document = [](std::uint64_t generation, float gain)
    {
        auto result = std::make_unique<vibera::LiveDocument>();
        result->generation = generation;
        result->mixer.push_back({1, gain, false, 0});
        return result;
    };
    auto scope = [&](std::uint64_t generation, float gain)
    {
        auto result = std::make_shared<vibera::LiveTimeline>();
        auto initial = document(generation, gain);
        require(result->publish(initial), "Prepare independent initial scope");
        return result;
    };
    auto incumbent = scope(1, .25f);
    Publication owner(incumbent);
    auto oldLease = incumbent->acquire();
    auto changed = document(2, .75f);
    auto ready = owner.prepare(changed);
    require(!changed && ready.preparationStatus() == Status::ready && owner.validate(ready) == Status::ready,
            "Preparation owns the entire candidate without changing incumbent publication");
    auto candidateGraphScope = ready.preparedTimeline(); // Prepared graph must pin this exact handle.
    require(candidateGraphScope && candidateGraphScope != incumbent &&
                incumbent->acquire().document->generation == 1 &&
                candidateGraphScope->acquire().document->generation == 2,
            "Candidate graph scope reads its immutable candidate while old graph still reads old source");
    const auto allocBefore = callbackAllocations.load(), deletesBefore = callbackDestructions.load();
    callbackAllocationProbe = true;
    const auto committed = owner.publish(ready);
    callbackAllocationProbe = false;
    require(committed == Status::published && callbackAllocations.load() == allocBefore &&
                callbackDestructions.load() == deletesBefore,
            "Control commit moves already owned slots without intercepted C++ new/delete");
    require(owner.currentTimeline() == candidateGraphScope && ready.retiredTimeline() == incumbent &&
                oldLease.document->generation == 1 && oldLease.document->mixer[0].gain == .25f,
            "Publication keeps old pinned document and retirement ownership intact");
    // Deterministic delayed-adoption ownership oracle. This does not impersonate
    // an actual NodePlayer: it verifies the scope any graph must already retain.
    for (int callback = 0; callback < 4; ++callback)
    {
        callbackAllocationProbe = true;
        auto old = incumbent->acquire();
        const bool remainsOld =
            old.document && old.document->generation == 1 && old.document->mixer[0].gain == .25f;
        old = {};
        callbackAllocationProbe = false;
        require(remainsOld, "Old scope cannot observe structural candidate while graph adoption is delayed");
    }
    require(callbackAllocations.load() == allocBefore && callbackDestructions.load() == deletesBefore,
            "Bounded scoped leases do not allocate or retire document ownership");
    auto observed = candidateGraphScope->acquire();
    require(observed.document && observed.document->generation == 2 &&
                observed.document->mixer[0].gain == .75f,
            "New scoped reader sees exactly its candidate after application commit");
    require(owner.publish(ready) == Status::alreadyPublished,
            "Consumed ticket cannot publish twice or clear current scope");
    {
        auto next = document(3, .5f);
        auto stale = owner.prepare(next);
        auto routine = document(4, .625f);
        require(owner.currentTimeline()->publish(routine),
                "Routine live edits remain available during preparation");
        require(
            owner.validate(stale) == Status::staleSource && owner.publish(stale) == Status::staleSource &&
                owner.currentTimeline()->acquire().document->generation == 4,
            "Routine publication invalidates structural ticket without freezing or undoing the live edit");
    }
    {
        auto next = document(5, .5f);
        auto altered = owner.prepare(next);
        auto notAdmitted = document(6, 1.f);
        require(altered.preparedTimeline()->publish(notAdmitted),
                "Exercise unexpected private-candidate mutation");
        require(owner.publish(altered) == Status::changedCandidate &&
                    owner.currentTimeline()->acquire().document->generation == 4,
                "Changed prepared document cannot enter canonical commit");
    }
    {
        auto first = document(7, .4f), second = document(8, .8f);
        auto a = owner.prepare(first), b = owner.prepare(second);
        require(owner.publish(a) == Status::published && owner.publish(b) == Status::staleSource &&
                    owner.currentTimeline()->acquire().document->generation == 7,
                "Multiple independently prepared candidates cannot overwrite a newer publication");
        Publication foreign(incumbent);
        auto next = document(9, .5f);
        auto notOwned = owner.prepare(next);
        require(foreign.publish(notOwned) == Status::wrongOwner && foreign.currentTimeline() == incumbent,
                "Foreign owner refuses a valid ticket without touching either scope");
    }
    {
        const auto before = owner.currentTimeline();
        auto next = document(10, .5f);
        {
            auto cancelled = owner.prepare(next);
        }
        require(owner.currentTimeline() == before && before->acquire().document->generation == 7,
                "Precommit cancellation destroys private owners only");
        std::unique_ptr<vibera::LiveDocument> missing;
        auto empty = owner.prepare(missing);
        Publication invalid({});
        auto fresh = document(11, .5f);
        auto noSource = invalid.prepare(fresh);
        require(empty.preparationStatus() == Status::missingDocument &&
                    noSource.preparationStatus() == Status::invalidSource && fresh,
                "Invalid preparation retains caller document and cannot publish");
    }
    {
        Publication::Prepared orphan;
        {
            Publication temporary(incumbent);
            auto next = document(12, .5f);
            orphan = temporary.prepare(next);
        }
        require(owner.publish(orphan) == Status::ownerGone,
                "Ticket retains identity but cannot resurrect a destroyed control owner");
    }
    {
        // Force only preparation's actual allocation; assert strings/fixtures are
        // constructed before arming. The hook resets before throwing/unwinding.
        const auto before = owner.currentTimeline();
        const auto serial = before->controlPublicationSerial();
        auto next = document(13, .5f);
        auto *expected = next.get();
        bool threw = false;
        preparationAllocationFailureHit = false;
        preparationAllocationFailureAfter = 0;
        try
        {
            auto failed = owner.prepare(next);
        }
        catch (const std::bad_alloc &)
        {
            threw = true;
        }
        preparationAllocationFailureAfter = -1;
        require(threw && preparationAllocationFailureHit && next.get() == expected &&
                    owner.currentTimeline() == before && before->controlPublicationSerial() == serial,
                "Actual candidate allocation failure preserves input, current scope and publication serial");
        auto retry = owner.prepare(next);
        require(owner.publish(retry) == Status::published &&
                    owner.currentTimeline()->acquire().document->generation == 13,
                "Failed preparation remains retryable without stale slot reservations");
    }
    {
        // Original three-slot pressure behavior stays real, not bypassed.
        auto bounded = scope(20, .2f);
        auto a = bounded->acquire();
        auto second = document(21, .3f);
        require(bounded->publish(second), "Publish second pinned-slot document");
        auto b = bounded->acquire();
        auto third = document(22, .4f);
        require(bounded->publish(third), "Publish third pinned-slot document");
        auto c = bounded->acquire();
        auto blocked = document(23, .5f);
        const auto serial = bounded->controlPublicationSerial();
        require(!bounded->publish(blocked) && blocked && bounded->controlPublicationSerial() == serial,
                "Busy slots preserve the routine pending document and freshness serial");
        Publication independent(bounded);
        auto fresh = document(24, .6f);
        auto prepared = independent.prepare(fresh);
        require(independent.publish(prepared) == Status::published && a.document->generation == 20 &&
                    b.document->generation == 21 && c.document->generation == 22,
                "Prepared independent scope never requires an incumbent free slot or invalidates readers");
    }
}
