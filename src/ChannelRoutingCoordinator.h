// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "AudioEngine.h"
#include "Commands.h"
#include "MixerNativeIdentity.h"
#include "StoppedEffectChangePlan.h"
#include <map>

namespace vibera
{
// CONTROL-only actual Controller/native publication coordinator. No shadow
// history, pointer input, audio callback work or inferred DSP acknowledgement.
// Studio supplies shared canonical admission; release/runtime qualification is separate.
class ChannelRoutingCoordinator final
{
  public:
    static constexpr std::uint64_t maxOperation = 9007199254740991ull;
    enum class Phase
    {
        preparing,
        pending,
        completed,
        failed,
        cancelled
    };
    enum class Error
    {
        none,
        wrongThread,
        busy,
        externalBusy,
        activeGesture,
        exhausted,
        unsupportedSequence,
        modelRefused,
        audioRefused,
        staleSource,
        graphRefused,
        invariantBroken,
        projectReplaced,
        contextLost,
        deviceChanged,
        superseded,
        preparationFailed,
        unknownOperation,
        tooLate
    };
    struct Context
    {
        bool patternMode = false;
        Id pattern = 0;
    };
    enum class AudioDisposition { liveGraph, deferredNoDevice, noAudioWork };
    struct Result
    {
        Error error = Error::none;
        std::uint64_t operation = 0;
        bool committed = false, nativeCompletionKnown = false;
        bool accepted() const noexcept
        {
            return error == Error::none;
        }
    };
    struct Job
    {
        std::uint64_t operation = 0, sourceRevision = 0, destinationRevision = 0;
        std::uint64_t postedSerial = 0, selectedSerial = 0, nativePostedSerial = 0;
        std::int64_t referenceStart = 0, referenceEnd = 0;
        juce::String project, detail, errorCode, stage;
        Context context;
        Phase phase = Phase::preparing;
        Error error = Error::none;
        bool audioPosted = false, committed = false, nativeCompletionKnown = false;
        bool noOp = false;
        AudioDisposition audioDisposition = AudioDisposition::liveGraph;
        std::unique_ptr<AudioEngine::PreparedDeferredRouting> deferred;
        std::unique_ptr<PreparedControllerChange> model;
        std::unique_ptr<AudioEngine::PreparedChannelRouting> audio;
        struct EffectWork;
        std::shared_ptr<EffectWork> effect;
    };

    ChannelRoutingCoordinator(Controller &model, AudioEngine &engine) : controller(model), audio(engine)
    {
        audio.bindRoutingController(controller);
    }
    ChannelRoutingCoordinator(const ChannelRoutingCoordinator &) = delete;
    ChannelRoutingCoordinator &operator=(const ChannelRoutingCoordinator &) = delete;
    ~ChannelRoutingCoordinator();

    // Actual application transactions, not backend-only tickets. The caller
    // supplies the owned asynchronous native capture to this exact operation.
    Result effectCommand(const juce::String &, const CommandArgs &, int insert, Context, bool externalBusy);
    Result effectHistory(HistoryDirection, int insert, Context, bool externalBusy);
    void effectCaptured(std::uint64_t, juce::Result, Project) noexcept;
    void effectCaptureInterrupted(std::uint64_t, bool nativeCaptureDrained) noexcept;
    bool awaitingEffectCapture(std::uint64_t) const noexcept;

    // Caller must finish its OWN Rack/MIDI previews through checked helpers,
    // and pass true for file/native-state/export/recording/other async ownership.
    // All unowned gestures refuse; the supplied owner never steals one.
    Result assign(const CommandArgs &args, Context context, bool externalBusy)
    {
        return prepareAndPost(
            context, externalBusy, [&](auto &prepared)
            { return controller.prepareStructuralCommand("channel.mixer", args, prepared); });
    }
    // Route-only macro/batch aggregate: one shared canonical mutator, one real
    // history transaction and one graph. Mixed sequences MUST be preflighted
    // before any legacy gesture/first step and refused until a joint plan exists.
    Result sequence(const std::vector<CommandArgs> &assignments, const juce::String &label, Context context,
                    bool externalBusy)
    {
        return prepareAndPost(
            context, externalBusy, [&](auto &prepared)
            { return controller.prepareChannelRoutingSequence(assignments, label, prepared); });
    }
    // Caller first previews the actual history destination and chooses this
    // path iff Channel assignments change. A mixed history destination refuses
    // before moving the actual cursor; no partial rollback is attempted.
    Result history(HistoryDirection direction, Context context, bool externalBusy)
    {
        return prepareAndPost(context, externalBusy, [&](auto &prepared)
                              { return controller.prepareHistoryChange(direction, prepared); });
    }

    const Job *find(std::uint64_t operation) const noexcept
    {
        const auto found = jobs.find(operation);
        return found == jobs.end() ? nullptr : found->second.get();
    }
    bool mutationBlocked() const noexcept
    {
        return preparing || quarantined || (active && active->phase == Phase::pending);
    }
    // Use in controller.changed BEFORE refresh. This receipt is installed
    // before notification and survives observer throw/retry at the same source.
    // Never blanket-clear needsRebuild for another revision/mode/Pattern.
    bool projectionCovers(Context context) const noexcept
    {
        return receiptValid && receiptProject == controller.projectIdentity &&
               receiptRevision == controller.revision && receiptContext.patternMode == context.patternMode &&
               receiptContext.pattern == context.pattern;
    }
    // Timer/service path only. No JSON/string allocation inside the state
    // transition. Readback never equates posted/accepted with selected.
    void poll(Context context) noexcept
    {
        if (!onControl() || preparing || !active || active->phase != Phase::pending)
            return;
        auto &job = *active;
        if (job.effect)
            return pollEffect(job, context);
        if (job.project != controller.projectIdentity)
            return terminalFailure(job, Error::projectReplaced);
        if (controller.revision != job.destinationRevision ||
            context.patternMode != job.context.patternMode || context.pattern != job.context.pattern)
            return terminalFailure(job, Error::superseded);
        const auto readback = audio.observeChannelRoutingSelection();
        job.selectedSerial = readback.selectedSerial;
        job.nativePostedSerial = readback.nativePostedSerial;
        job.referenceStart = readback.referenceStart;
        job.referenceEnd = readback.referenceEnd;
        using Status = AudioEngine::ChannelRoutingStatus;
        if (readback.status == Status::deviceChanged)
            return terminalFailure(job, Error::deviceChanged);
        if (readback.status == Status::staleContext || readback.status == Status::ownerGone ||
            !readback.overlayActive)
            return terminalFailure(job, Error::contextLost);
        if (readback.status == Status::staleProjection)
            return terminalFailure(job, Error::superseded);
        if (readback.status == Status::selected && readback.postedSerial == job.postedSerial &&
            readback.selectedSerial == job.postedSerial)
        {
            job.nativeCompletionKnown = true;
            job.phase = Phase::completed;
            // The engine owns the effective graph. This retires only external
            // private bookkeeping on CONTROL after a truthful acknowledgement.
            job.audio.reset();
            job.model.reset();
        }
        // ReadbackUnavailable, posted and bounded stale snapshots stay pending.
    }
    Result cancel(std::uint64_t operation) noexcept
    {
        const auto *job = find(operation);
        if (!job)
            return {Error::unknownOperation, operation};
        if (job->effect)
            return cancelEffect(*jobs.find(operation)->second);
        // No asynchronous pre-post phase is exposed by this bounded API.
        // Cancellation cannot undo an already published musical transaction.
        return {Error::tooLate, operation, job->committed, job->nativeCompletionKnown};
    }
    // Explicit caller acknowledgement ONLY AFTER actual full projection
    // replacement/recovery succeeded for this canonical source. This does not
    // complete an interrupted job; it clears quarantine for future operations.
    Result fullProjectionRestored(Context context)
    {
        if (!onControl())
            return {Error::wrongThread};
        if (preparing)
            return {Error::busy};
        // Verify actual canonical projection/context before clearing quarantine.
        // This may prepare a stopped cold context; it never forces a device.
        try
        {
            const auto restored =
                audio.ensureChannelRoutingContext(controller.project, context.patternMode, context.pattern);
            if (restored.status != AudioEngine::ChannelRoutingStatus::ready ||
                audio.channelRoutingReadback().overlayActive)
                return {Error::audioRefused};
        }
        catch (...)
        {
            return {Error::preparationFailed};
        }
        if (active && active->phase == Phase::pending)
            terminalFailure(*active, Error::contextLost);
        quarantined = false;
        receiptValid = false;
        return {};
    }

  private:
    Controller &controller;
    AudioEngine &audio;
    std::map<std::uint64_t, std::unique_ptr<Job>> jobs;
    Job *active = nullptr;
    std::uint64_t serial = 0, receiptRevision = 0;
    juce::String receiptProject;
    Context receiptContext;
    bool preparing = false, quarantined = false, receiptValid = false;
    Result beginEffect(const juce::String &, const CommandArgs &, std::optional<HistoryDirection>, int,
                       Context, bool externalBusy);
    void pollEffect(Job &, Context) noexcept;
    Result cancelEffect(Job &) noexcept;
    void failEffect(Job &, const juce::String &, Error = Error::preparationFailed);
    static bool onControl() noexcept
    {
        const auto *manager = juce::MessageManager::getInstanceWithoutCreating();
        return manager && manager->isThisTheMessageThread();
    }
    void terminalFailure(Job &job, Error failure) noexcept
    {
        job.phase = Phase::failed;
        job.error = failure;
        job.nativeCompletionKnown = false;
        receiptValid = false;
        quarantined = true; // Explicit recovery; never keep applying legacy edits.
    }
    static bool routeOnlyDestination(const Project& source, const Project& target)
    {
        if (source.channels.size() != target.channels.size()) return false;
        auto normalized = target;
        if (!MixerNativeIdentity::alignDisplayOrder(normalized, source)) return false;
        for (auto& channel : normalized.channels)
        {
            const auto* old = source.channel(channel.id);
            if (!old) return false;
            channel.mixer = old->mixer;
        }
        return normalized.state().isEquivalentTo(source.state());
    }
    template <class Prepare> Result prepareAndPost(Context context, bool externalBusy, Prepare &&prepare)
    {
        if (!onControl())
            return {Error::wrongThread};
        poll(context);
        if (mutationBlocked())
            return {Error::busy};
        if (externalBusy || audio.rendering())
            return {Error::externalBusy};
        if (controller.hasGesture())
            return {Error::activeGesture};
        if (serial == maxOperation)
            return {Error::exhausted};
        const juce::ScopedValueSetter<bool> lease(preparing, true);
        Job *admitted = nullptr;
        try
        {
            // Bounded terminal receipt retention. Caller tools must document
            // expiry; eviction never changes the active musical projection.
            while (jobs.size() >= 32)
            {
                auto oldest = jobs.begin();
                jassert(oldest->second->phase != Phase::pending);
                if (active == oldest->second.get())
                    active = nullptr;
                jobs.erase(oldest); // CONTROL retirement, before publication.
            }
            auto candidate = std::make_unique<Job>();
            candidate->operation = ++serial;
            candidate->project = controller.projectIdentity;
            candidate->sourceRevision = controller.revision;
            candidate->destinationRevision = controller.revision;
            candidate->context = context;
            if (auto result = prepare(candidate->model); result.failed())
            {
                candidate->detail = result.getErrorMessage();
                candidate->phase = Phase::failed;
                candidate->error = Error::modelRefused;
            }
            else if (!candidate->model || !candidate->model->destinationBeforeCommit())
            {
                candidate->phase = Phase::failed;
                candidate->error = Error::modelRefused;
            }
            else if (candidate->model->isNoOp())
            {
                candidate->phase = Phase::completed;
                candidate->noOp = true;
                candidate->audioDisposition = AudioDisposition::noAudioWork;
                candidate->nativeCompletionKnown = true; // No graph work requested.
            }
            else if (!routeOnlyDestination(controller.project, *candidate->model->destinationBeforeCommit()))
            {
                candidate->phase = Phase::failed; candidate->error = Error::unsupportedSequence;
                candidate->detail = "routing.unsupportedMixedHistory: A joint route/content history plan is required";
            }
            else
            {
                if (audio.canDeferChannelRouting())
                {
                    candidate->audioDisposition = AudioDisposition::deferredNoDevice;
                    if (auto ready = audio.prepareDeferredRouting(*candidate->model->destinationBeforeCommit(), candidate->deferred);
                        ready.failed())
                    {
                        candidate->phase = Phase::failed; candidate->error = Error::audioRefused;
                        candidate->detail = ready.getErrorMessage();
                    }
                }
                else
                {
                auto cold = audio.ensureChannelRoutingContext(controller.project, context.patternMode,
                                                              context.pattern);
                if (cold.status != AudioEngine::ChannelRoutingStatus::ready)
                {
                    candidate->detail = cold.message;
                    candidate->phase = Phase::failed;
                    candidate->error = Error::audioRefused;
                }
                else
                {
                    auto prepared = audio.prepareChannelRouting(*candidate->model->destinationBeforeCommit(),
                                                                candidate->audio);
                    if (prepared.status != AudioEngine::ChannelRoutingStatus::ready || !candidate->audio)
                    {
                        candidate->detail = prepared.message;
                        candidate->phase = Phase::failed;
                        candidate->error = Error::audioRefused;
                    }
                    else
                        candidate->postedSerial = candidate->audio->publicationSerial();
                }
            }
                }
            const auto operation = candidate->operation;
            // Allocate real result ownership and pre-own the receipt string
            // BEFORE final admission/publication. No message pump thereafter.
            auto preparedReceiptProject = candidate->project;
            const auto [it, inserted] = jobs.emplace(operation, std::move(candidate));
            jassert(inserted);
            admitted = it->second.get();
            if (admitted->phase != Phase::preparing)
                return {admitted->error, operation, false, admitted->nativeCompletionKnown};
            const bool documentOnly = admitted->audioDisposition == AudioDisposition::deferredNoDevice;
            if (documentOnly && !audio.canDeferChannelRouting())
            {
                admitted->phase = Phase::failed;
                admitted->error = Error::audioRefused;
                return {admitted->error, operation};
            }
            if (!documentOnly)
            if (auto valid = audio.validateChannelRouting(*admitted->audio);
                valid.status != AudioEngine::ChannelRoutingStatus::ready)
            {
                admitted->detail = valid.message;
                admitted->phase = Phase::failed;
                admitted->error = Error::audioRefused;
                return {admitted->error, operation};
            }
            if (auto valid = controller.validatePreparedChange(*admitted->model); valid.failed())
            {
                admitted->detail = valid.getErrorMessage();
                admitted->phase = Phase::failed;
                admitted->error = Error::staleSource;
                return {admitted->error, operation};
            }
            // Only bounded noexcept calls and intrinsic state between here and
            // receipt installation. Controller validation just passed on this
            // sole CONTROL lease; a later refusal is an invariant failure.
            if (!documentOnly)
            {
            const auto posted = audio.postChannelRouting(*admitted->audio);
            if (posted != AudioEngine::ChannelRoutingStatus::posted)
            {
                admitted->phase = Phase::failed;
                admitted->error = Error::graphRefused;
                return {admitted->error, operation};
            }
            admitted->audioPosted = true;
            }
            active = admitted;
            if (!controller.commitPreparedChange(*admitted->model))
            {
                terminalFailure(*admitted, Error::invariantBroken);
                return {Error::invariantBroken, operation, false};
            }
            admitted->committed = true;
            admitted->destinationRevision = controller.revision;
            admitted->phase = documentOnly ? Phase::completed : Phase::pending;
            receiptProject.swapWith(preparedReceiptProject);
            receiptRevision = controller.revision;
            receiptContext = context;
            receiptValid = !documentOnly;
            if (documentOnly) audio.retireNoDeviceProjection(*admitted->deferred);
            // Observer delivery is exception-contained/retryable by Controller.
            // Reentry remains refused until this outer CONTROL lease returns.
            controller.deliverPendingNotifications();
            if (documentOnly)
            {
                admitted->model.reset();
                admitted->deferred.reset(); // Retire old PCM/ticket owners only on CONTROL.
            }
            return {Error::none, operation, true, false};
        }
        catch (...)
        {
            // Fallible preparation only. If publication already happened it is
            // never represented as a successful rollback or a retryable command.
            if (admitted)
            {
                if (admitted->audioPosted)
                {
                    terminalFailure(*admitted, Error::invariantBroken);
                    return {Error::invariantBroken, admitted->operation, admitted->committed};
                }
                admitted->phase = Phase::failed;
                admitted->error = Error::preparationFailed;
                return {Error::preparationFailed, admitted->operation};
            }
            return {Error::preparationFailed};
        }
    }
};
} // namespace vibera
