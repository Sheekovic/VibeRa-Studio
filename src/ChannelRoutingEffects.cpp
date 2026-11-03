// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ChannelRoutingCoordinator.h"
#include <future>

namespace vibera
{
struct ChannelRoutingCoordinator::Job::EffectWork
{
    juce::String command;
    CommandArgs args;
    std::optional<HistoryDirection> history;
    int insert = 0;
    Project captured;
    PreparedPluginTransport::Ptr transport;
    std::shared_ptr<std::atomic<bool>> cancel = std::make_shared<std::atomic<bool>>(false);
    using Startup = std::pair<juce::Result, StoppedEffectWorkers::Ptr>;
    std::future<Startup> startup;
    StoppedEffectWorkers::Ptr workers;
    bool capturePending = true, cancelling = false;
};

ChannelRoutingCoordinator::~ChannelRoutingCoordinator()
{
    // std::async owners are joined on CONTROL during orderly app shutdown only.
    // No thread captures this coordinator, Controller or AudioEngine.
    for (auto &[id, job] : jobs)
        if (job->effect)
        {
            job->effect->cancel->store(true);
            if (job->audio && !job->audioPosted)
                (void)audio.cancelStoppedEffectMembership(*job->audio);
        }
}

ChannelRoutingCoordinator::Result ChannelRoutingCoordinator::effectCommand(const juce::String &command,
                                                                           const CommandArgs &args,
                                                                           int insert, Context context,
                                                                           bool busy)
{
    return beginEffect(command, args, {}, insert, context, busy);
}
ChannelRoutingCoordinator::Result
ChannelRoutingCoordinator::effectHistory(HistoryDirection direction, int insert, Context context, bool busy)
{
    return beginEffect({}, {}, direction, insert, context, busy);
}
ChannelRoutingCoordinator::Result
ChannelRoutingCoordinator::beginEffect(const juce::String &command, const CommandArgs &args,
                                       std::optional<HistoryDirection> direction, int insert, Context context,
                                       bool busy)
{
    if (!onControl())
        return {Error::wrongThread};
    poll(context);
    if (mutationBlocked())
        return {Error::busy};
    if (busy || audio.rendering() || audio.playing())
        return {Error::externalBusy};
    if (controller.hasGesture())
        return {Error::activeGesture};
    if (serial == maxOperation)
        return {Error::exhausted};
    const juce::ScopedValueSetter<bool> lease(preparing, true);
    try
    {
        auto candidate = std::make_unique<Job>();
        candidate->effect = std::make_shared<Job::EffectWork>();
        auto &work = *candidate->effect;
        work.command = command;
        work.args = args;
        work.history = direction;
        work.insert = insert;
        if (auto ready = audio.stoppedEffectWorkerTransport(work.transport); ready.failed())
            return {Error::audioRefused};
        candidate->operation = ++serial;
        candidate->project = controller.projectIdentity;
        candidate->sourceRevision = candidate->destinationRevision = controller.revision;
        candidate->context = context;
        candidate->phase = Phase::pending;
        candidate->stage = "capture";
        while (jobs.size() >= 32)
        {
            auto oldest = jobs.begin();
            if (active == oldest->second.get())
                active = nullptr;
            jobs.erase(oldest);
        }
        const auto operation = candidate->operation;
        active = jobs.emplace(operation, std::move(candidate)).first->second.get();
        return {Error::none, operation};
    }
    catch (...)
    {
        return {Error::preparationFailed};
    }
}
bool ChannelRoutingCoordinator::awaitingEffectCapture(std::uint64_t operation) const noexcept
{
    const auto *job = find(operation);
    return job && job->phase == Phase::pending && job->effect && job->effect->capturePending;
}
void ChannelRoutingCoordinator::effectCaptureInterrupted(std::uint64_t operation,
                                                         bool nativeCaptureDrained) noexcept
{
    if (!onControl() || !active || active->operation != operation || !awaitingEffectCapture(operation))
        return;
    try
    {
        failEffect(*active, "effects.capturePollFailed: Owned capture failed; draining before terminal");
        if (nativeCaptureDrained)
        {
            active->effect->capturePending = false;
            pollEffect(*active, active->context);
        }
    }
    catch (...)
    {
    } // Keep ownership and retry ordinary CONTROL service; never fabricate completion.
}
void ChannelRoutingCoordinator::failEffect(Job &job, const juce::String &detail, Error error)
{
    job.detail = detail;
    job.errorCode = detail.containsChar(':')        ? detail.upToFirstOccurrenceOf(":", false, false)
                    : detail.startsWith("effects.") ? detail
                                                    : "effects.preparationFailed";
    job.error = error;
    if (job.effect)
        job.effect->cancel->store(true);
    if (job.audio && !job.audioPosted)
    {
        const auto cancelled = audio.cancelStoppedEffectMembership(*job.audio);
        if (cancelled.wasOk())
        {
            job.effect->cancelling = true;
            job.stage = "cancelling";
            return; // Retain exact native owners until cancellation ACK.
        }
    }
    if (job.effect && (job.effect->capturePending || job.effect->startup.valid()))
    {
        job.effect->cancelling = true;
        job.stage = "cancelling";
        return; // Drain capture/startup before exposing terminal failure.
    }
    job.phase = Phase::failed;
    job.stage = "failed";
    if (job.audioPosted)
    {
        quarantined = true;
        receiptValid = false;
    }
    else
    {
        // No requested membership remains unacknowledged on this branch.
        // Release private graph registration and workers so the next edit can prepare.
        job.audio.reset();
        job.model.reset();
        job.effect->workers.reset();
    }
}
void ChannelRoutingCoordinator::effectCaptured(std::uint64_t operation, juce::Result result,
                                               Project captured) noexcept
{
    if (!onControl() || !active || active->operation != operation || !active->effect ||
        !active->effect->capturePending)
        return;
    auto &job = *active;
    auto &work = *job.effect;
    work.capturePending = false; // Actual application capture callback drained.
    try
    {
        if (work.cancel->load())
            return pollEffect(job, job.context);
        if (result.failed())
            return failEffect(job, result.getErrorMessage());
        if (controller.projectIdentity != job.project || controller.revision != job.sourceRevision)
            return failEffect(job, "effects.staleSource: Project changed during native capture",
                              Error::staleSource);
        work.captured = std::move(captured);
        auto prepared =
            work.history
                ? controller.prepareStoppedEffectHistory(*work.history, work.insert, work.captured, job.model)
                : controller.prepareStoppedEffectCommand(work.command, work.args, work.captured, job.model);
        if (prepared.failed())
            return failEffect(job, prepared.getErrorMessage(), Error::modelRefused);
        StoppedEffectChangePlan plan;
        if (auto delta = planStoppedEffectChange(work.captured, *job.model->destinationBeforeCommit(),
                                                 work.insert, plan);
            delta.failed())
            return failEffect(job, delta.getErrorMessage(), Error::unsupportedSequence);
        if (plan.appending)
        {
            const auto transport = work.transport;
            const auto cancelled = work.cancel;
            work.startup = std::async(std::launch::async,
                                      [specs = std::move(plan.added), transport, cancelled]() mutable
                                      {
                                          StoppedEffectWorkers::Ptr workers;
                                          const auto result = StoppedEffectWorkers::prepare(
                                              std::move(specs), transport,
                                              [cancelled] { return cancelled->load(); }, workers);
                                          return Job::EffectWork::Startup{result, std::move(workers)};
                                      });
            job.stage = "workers";
        }
        else
            job.stage = "prepareGraph";
    }
    catch (...)
    {
        try
        {
            failEffect(job, "effects.preparationFailed: Canonical source retained");
        }
        catch (...)
        {
        }
    }
}
ChannelRoutingCoordinator::Result ChannelRoutingCoordinator::cancelEffect(Job &job) noexcept
{
    if (!onControl())
        return {Error::wrongThread, job.operation};
    if (job.phase != Phase::pending || job.committed || job.audioPosted)
        return {Error::tooLate, job.operation, job.committed, job.nativeCompletionKnown};
    job.effect->cancel->store(true);
    job.effect->cancelling = true;
    try
    {
        job.stage = "cancelling";
        if (job.audio)
            if (auto result = audio.cancelStoppedEffectMembership(*job.audio); result.failed())
                return {Error::tooLate, job.operation};
    }
    catch (...)
    {
        return {Error::preparationFailed, job.operation};
    }
    return {Error::none, job.operation}; // Accepted cancellation, never fabricated cancelled.
}
void ChannelRoutingCoordinator::pollEffect(Job &job, Context context) noexcept
{
    auto &work = *job.effect;
    try
    {
        if (!work.cancelling &&
            (controller.projectIdentity != job.project ||
             controller.revision != (job.committed ? job.destinationRevision : job.sourceRevision) ||
             context.patternMode != job.context.patternMode || context.pattern != job.context.pattern ||
             audio.playing()))
            failEffect(job, "effects.staleContext: Source, transport or selection changed",
                       Error::superseded);
        if (job.phase != Phase::pending || work.capturePending)
            return;
        if (work.startup.valid())
        {
            if (work.startup.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                return;
            auto started = work.startup.get(); // Ready only, never a wait on CONTROL/audio.
            if (!work.cancelling)
            {
                if (started.first.failed())
                    return failEffect(job, started.first.getErrorMessage());
                work.workers = std::move(started.second);
                job.stage = "prepareGraph";
            }
        }
        if (work.cancelling)
        {
            if (job.audio)
            {
                const auto state = audio.stoppedEffectMembershipReadback(*job.audio);
                if (state.phase != AudioEngine::StoppedEffectPhase::cancelled &&
                    state.phase != AudioEngine::StoppedEffectPhase::failed)
                    return;
                if (state.phase == AudioEngine::StoppedEffectPhase::failed)
                {
                    job.error = Error::audioRefused;
                    job.errorCode = state.error ? state.error : "effects.cancellationFailed";
                    job.detail = job.errorCode;
                    quarantined = true;
                }
            }
            job.phase = job.error == Error::none ? Phase::cancelled : Phase::failed;
            job.stage = job.phase == Phase::cancelled ? "cancelled" : "failed";
            job.audio.reset();
            job.model.reset();
            work.workers.reset();
            return;
        }
        if (job.stage == "prepareGraph")
        {
            const auto prepared = audio.prepareStoppedEffectChange(controller, *job.model, work.captured,
                                                                   work.insert, work.workers, job.audio);
            if (prepared.status != AudioEngine::ChannelRoutingStatus::ready)
                return failEffect(job, prepared.message, Error::audioRefused);
            if (auto ready = audio.prepareStoppedEffectMembership(controller, *job.model, *job.audio);
                ready.failed())
                return failEffect(job, ready.getErrorMessage(), Error::audioRefused);
            if (auto requested = audio.requestStoppedEffectMembership(controller, *job.model, *job.audio);
                requested.failed())
                return failEffect(job, requested.getErrorMessage(), Error::audioRefused);
            job.postedSerial = job.audio->publicationSerial();
            job.stage = "membership";
            return;
        }
        if (!job.audio)
            return;
        const auto state = audio.stoppedEffectMembershipReadback(*job.audio);
        if (state.phase == AudioEngine::StoppedEffectPhase::failed)
            return failEffect(job, state.error ? state.error : "effects.nativeFailed", Error::audioRefused);
        if (!job.audioPosted)
        {
            if (state.phase != AudioEngine::StoppedEffectPhase::expanded)
                return;
            const auto native = audio.validateStoppedEffectChange(controller, *job.model, *job.audio);
            if (native.status == AudioEngine::ChannelRoutingStatus::busy)
                return;
            if (native.status != AudioEngine::ChannelRoutingStatus::ready)
                return failEffect(job, native.message, Error::audioRefused);
            if (auto valid = controller.validatePreparedChange(*job.model); valid.failed())
                return failEffect(job, valid.getErrorMessage(), Error::staleSource);
            auto preparedReceipt = job.project;
            const juce::ScopedValueSetter<bool> lease(preparing, true);
            if (audio.postChannelRouting(*job.audio) != AudioEngine::ChannelRoutingStatus::posted)
                return failEffect(job, "effects.graphRefused: Canonical source retained",
                                  Error::graphRefused);
            job.audioPosted = true;
            if (!controller.commitPreparedChange(*job.model))
                return failEffect(job, "effects.commitInvariant: Published graph requires recovery",
                                  Error::invariantBroken);
            job.committed = true;
            job.destinationRevision = controller.revision;
            receiptProject.swapWith(preparedReceipt);
            receiptRevision = controller.revision;
            receiptContext = context;
            receiptValid = true;
            job.stage = "settling";
            controller.deliverPendingNotifications();
            return;
        }
        const auto selected = audio.observeChannelRoutingSelection();
        job.selectedSerial = selected.selectedSerial;
        job.nativePostedSerial = selected.nativePostedSerial;
        job.referenceStart = selected.referenceStart;
        job.referenceEnd = selected.referenceEnd;
        if (state.phase == AudioEngine::StoppedEffectPhase::completed)
        {
            if (selected.status != AudioEngine::ChannelRoutingStatus::selected ||
                selected.selectedSerial != job.postedSerial || selected.postedSerial != job.postedSerial)
                return failEffect(job, "effects.selectionLost: Completed membership lacks exact graph",
                                  Error::superseded);
            job.nativeCompletionKnown = true;
            job.phase = Phase::completed;
            job.stage = "completed";
            job.audio.reset();
            job.model.reset();
            work.workers.reset();
        }
    }
    catch (...)
    {
        try
        {
            failEffect(job, "effects.preparationFailed: Inspect retained operation");
        }
        catch (...)
        {
        }
    }
}
} // namespace vibera
