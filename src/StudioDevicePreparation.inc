// SPDX-License-Identifier: AGPL-3.0-or-later
// Original CONTROL device preparation coordinator; included in Studio.cpp.
namespace
{
const char* devicePreparationPhase(Studio::DeferredDevicePreparation::Phase value) noexcept
{
    using Phase = Studio::DeferredDevicePreparation::Phase;
    switch (value)
    {
    case Phase::closing: return "closingEditors";
    case Phase::capturing: return "capturing";
    case Phase::preparing: return "preparing";
    case Phase::draining: return "draining";
    case Phase::completed: return "completed";
    case Phase::failed: return "failed";
    case Phase::cancelled: return "cancelled";
    }
    return "unknown";
}
}
bool Studio::devicePreparationBusy() const noexcept
{
    if (!devicePreparation) return false;
    using Phase = DeferredDevicePreparation::Phase;
    const auto phase = devicePreparation->phase;
    return phase != Phase::completed && phase != Phase::failed && phase != Phase::cancelled;
}
juce::Result Studio::devicePreparationSourceCurrent(const DeferredDevicePreparation& job,
                                                   bool finalNative) const
{
    if (controller.projectIdentity != job.identity || controller.revision != job.revision ||
        patternMode != job.patternMode || selectedPattern != job.pattern || controller.hasGesture())
        return juce::Result::fail("device.sourceChanged: Document session, revision or edit owner changed");
    const auto& expected = job.captureApplied ? job.captured : job.source;
    if (!expected.state().isEquivalentTo(controller.project.state()))
        return juce::Result::fail("device.sourceChanged: Exact document content changed during preparation");
    const auto& manager = audio.engine.getDeviceManager();
    if (!manager.deviceManager.getCurrentAudioDevice() || manager.getSampleRate() != job.rate ||
        manager.getBlockSize() != job.block)
        return juce::Result::fail("device.configurationChanged: Actual device configuration changed");
    for (const auto& binding : job.bindings)
    {
        const auto* requested = controller.project.plugin(binding.id);
        if (!requested || requested->descriptor != binding.descriptor || !binding.host->alive() ||
            binding.host->runtimeIdentity() != binding.instance)
            return juce::Result::fail("device.instanceChanged: Captured native owner is no longer current");
        // Before candidate construction the public host mapping must be exact.
        // During final construction AudioEngine temporarily installs candidate
        // bookkeeping; source owners remain pinned and no message pump occurs.
        if (!finalNative && audio.preparedPluginHost(controller.project, binding.id) != binding.host)
            return juce::Result::fail("device.instanceChanged: Prepared native mapping changed");
        if (!binding.requiresCapture &&
            !audio.pluginHostConfigurationCurrent(*requested, binding.host, job.rate, job.block))
            return juce::Result::fail("device.instanceChanged: Exact reusable worker configuration changed");
        if (finalNative)
        {
            const auto candidate = audio.parameterHost(binding.id);
            if (!candidate || !candidate->alive() ||
                !audio.pluginHostConfigurationCurrent(*requested, candidate, job.rate, job.block) ||
                (!binding.requiresCapture && candidate != binding.host) ||
                (binding.requiresCapture && candidate->recoveryState() != requested->state))
                return juce::Result::fail("device.workerPreparationFailed: Exact prepared replacement is unavailable; source worker retained");
        }
        if (job.captureValidated && binding.requiresCapture &&
            (binding.host->parameterRevision() != binding.capturedRevision ||
             binding.host->editorBusy() || binding.host->editorWindowState().exists ||
             binding.host->pendingParameterRequests()))
            return juce::Result::fail("device.captureChanged: Native source changed after capture");
    }
    return juce::Result::ok();
}
juce::Result Studio::beginDevicePreparation(bool requestPlay)
{
    try
    {
    if (devicePreparationServicing)
        return juce::Result::fail("device.busy: Device preparation service already owns CONTROL");
    if (devicePreparationBusy())
    {
        devicePreparation->playRequested |= requestPlay;
        return juce::Result::ok(); // Already pending, not a completed preparation.
    }
    const bool restoring = audio.deviceRestorationRequired();
    const bool initialDeferred =
        audio.documentProjectionDeferred() && !audio.projectionDiagnostics().edit && !audio.playing();
    if ((!restoring && !initialDeferred) ||
        !audio.engine.getDeviceManager().deviceManager.getCurrentAudioDevice())
        return juce::Result::fail("device.unavailable: A deferred projection or refused device restoration "
                                  "and actual device are required");
    if (routingCoordinator.mutationBlocked() || controller.hasGesture() || noteSlicePending() ||
        noteStretchPending() || midiTaking || hasPendingMidiTake() || afterPluginState || pendingExport ||
        audio.rendering() || midiFileBusy || !audioImports.idle() || audioImportAfterTake ||
        pluginRecovery || customization.running || pluginControlsPending() ||
        !audio.deferredWorkerPreparationIdle() || audio.pluginStateCapturePending() ||
        std::any_of(pluginOperations.begin(), pluginOperations.end(),
                    [](const auto& value) { return value.state == "pending"; }))
        return juce::Result::fail("device.busy: Finish active edit, native control or file work before preparation");
    if (devicePreparationSerial == ChannelRoutingCoordinator::maxOperation)
        return juce::Result::fail("device.serialExhausted: Restart session before another preparation");
    auto job = std::make_unique<DeferredDevicePreparation>();
    job->operation = devicePreparationSerial + 1;
    job->identity = controller.projectIdentity; job->revision = controller.revision;
    job->source = controller.project; job->patternMode = patternMode; job->pattern = selectedPattern;
    job->rate = audio.engine.getDeviceManager().getSampleRate();
    job->block = audio.engine.getDeviceManager().getBlockSize();
    // A refused device is already suspended, but the old transport can still
    // retain Play. Preserve that intent before backend restoration retires it.
    job->playRequested = requestPlay || (restoring && audio.playing());
    job->instrumentIntent = pendingInstrument;
    if (initialDeferred)
    {
        // No native graph can read these workers. Bind a truly fresh worker to
        // the actual device plan before deciding whether state capture/editor
        // closure is needed. Existing DSP history always refuses promotion.
        const auto ready = audio.prepareDeviceRestoration(controller.project);
        if (ready.failed())
            return ready;
    }
    const auto bind = [&](const PluginSpec& spec)
    {
        if (const auto host = audio.preparedPluginHost(controller.project, spec.id))
        {
            if (!host->alive()) throw std::runtime_error("device.workerUnavailable: Recover the retained native owner before preparation");
            job->bindings.push_back(
                {spec.id,
                 spec.descriptor,
                 host->runtimeIdentity(),
                 {},
                 host,
                 0,
                 !audio.pluginHostConfigurationCurrent(spec, host, job->rate, job->block)});
        }
        // A missing-plugin placeholder has no prepared native owner to capture;
        // normal canonical saved-state/missing-plugin policy remains authoritative.
    };
    for (const auto& channel : controller.project.channels) if (channel.instrument) bind(*channel.instrument);
    for (const auto& insert : controller.project.mixer) for (const auto& effect : insert.effects) bind(effect);
    job->workersReusedOnly = std::none_of(job->bindings.begin(), job->bindings.end(),
        [](const auto& binding) { return binding.requiresCapture; });
    if (job->workersReusedOnly) job->phase = DeferredDevicePreparation::Phase::preparing;
    // Entire persistent owner is prepared before native closes/capture admission.
    devicePreparation = std::move(job);
    devicePreparationSerial = devicePreparation->operation;
    pendingInstrument.reset(); // Persistent job now owns this queued UI intent.
    status = devicePreparationPendingStatus;
    return juce::Result::ok();
    }
    catch (const std::exception& error)
    { return juce::Result::fail(juce::String("device.admissionFailed: ") + error.what()); }
    catch (...)
    { return juce::Result::fail("device.admissionFailed: Persistent preparation owner was not admitted"); }
}
juce::Result Studio::cancelDevicePreparation(std::uint64_t operation)
{
    if (!devicePreparation || devicePreparation->operation != operation)
        return juce::Result::fail("device.unknownOperation: Current device preparation token required");
    if (!devicePreparationBusy())
        return juce::Result::fail("device.tooLate: Preparation is already terminal");
    devicePreparation->cancelRequested = true;
    devicePreparation->playRequested = false;
    // Remains pending until actual native closes/capture/preparation drain.
    return juce::Result::ok();
}
juce::Result Studio::retryDevicePreparation(std::uint64_t operation, bool requestPlay)
{
    if (!devicePreparation || devicePreparation->operation != operation)
        return juce::Result::fail("device.unknownOperation: Current device preparation token required");
    if (devicePreparationBusy())
        return juce::Result::fail("device.busy: The owned native operation has not drained");
    if (devicePreparation->identity != controller.projectIdentity)
        return juce::Result::fail("device.sourceChanged: Retry cannot target a replacement project");
    return beginDevicePreparation(requestPlay);
}
void Studio::serviceDevicePreparation()
{
    if (devicePreparationServicing) return;
    if (devicePreparation && !devicePreparationBusy() && devicePreparation->identity != controller.projectIdentity)
        devicePreparation.reset(); // Expire a terminal previous-project result; never reuse its capture.
    if (!devicePreparationBusy())
    {
        // A completed earlier operation may precede a genuine new device
        // refusal. Failed/cancelled receipts still require explicit Play/retry;
        // an unchanged timer must not repeatedly close editors or capture.
        const bool restoring = audio.deviceRestorationRequired();
        if (restoring && audio.engine.getDeviceManager().getPreparationFenceStatus() !=
                             te::DeviceManager::PreparationFenceStatus::refused)
            return; // Session-only recovery requires explicit Play/retry; never a timer restart loop.
        if ((devicePreparation &&
             (!restoring || devicePreparation->phase != DeferredDevicePreparation::Phase::completed)) ||
            (!restoring && !audio.documentProjectionDeferred()) ||
            !audio.engine.getDeviceManager().deviceManager.getCurrentAudioDevice())
            return;
        if (auto admitted = beginDevicePreparation(false); admitted.failed())
        { projectionError = admitted.getErrorMessage(); return; }
    }
    const juce::ScopedValueSetter<bool> service(devicePreparationServicing, true);
    auto& job = *devicePreparation;
    using Phase = DeferredDevicePreparation::Phase;
    auto refuse = [&](const juce::Result& failure)
    {
        if (job.code.isEmpty())
        {
            job.code = failure.getErrorMessage().upToFirstOccurrenceOf(":", false, false);
            job.message = failure.getErrorMessage();
        }
        job.phase = Phase::draining;
    };
    auto finish = [&](Phase phase)
    {
        job.phase = phase; // Intrinsic outcome before fallible UI publication.
        if (job.instrumentIntent && !pendingInstrument &&
            job.instrumentIntent->matches(controller.projectIdentity, controller.project))
            pendingInstrument = std::move(job.instrumentIntent);
        job.bindings.clear(); // Only CONTROL; engine retains predecessors until retirement completes.
        status = phase == Phase::completed ? routingReadyStatus : devicePreparationStoppedStatus;
    };
    auto closesFinished = [&]
    {
        bool finished = true;
        pollPluginOperations();
        for (const auto& binding : job.bindings)
            if (binding.closeOperation.isNotEmpty())
            {
                const auto found = std::find_if(pluginOperations.begin(), pluginOperations.end(),
                    [&](const auto& value) { return value.id == binding.closeOperation; });
                if (found == pluginOperations.end())
                { refuse(juce::Result::fail("device.editorReceiptLost: Owned editor receipt disappeared")); continue; }
                if (found->state == "pending") { finished = false; continue; }
                if (found->state != "completed" || !found->editorResult || !found->editorResult->succeeded())
                    refuse(juce::Result::fail("device.editorCloseFailed: Actual native editor close did not succeed"));
            }
        return finished;
    };
    try
    {
        if (job.cancelRequested) job.phase = Phase::draining;
        if (job.phase != Phase::draining)
            if (auto valid = devicePreparationSourceCurrent(job, false); valid.failed()) refuse(valid);
        if (job.phase == Phase::closing)
        {
            for (auto& binding : job.bindings)
                if (binding.requiresCapture && binding.closeOperation.isEmpty())
                {
                    juce::String code, operation;
                    const juce::ScopedValueSetter<bool> own(devicePreparationDispatch, true);
                    const auto result = requestPluginEditor(binding.id, false, binding.instance, operation, code);
                    if (result.failed()) { refuse(result); break; }
                    binding.closeOperation.swapWith(operation);
                }
            if (!closesFinished() || job.phase == Phase::draining) return;
            job.phase = Phase::capturing;
            const auto operation = job.operation;
            const juce::ScopedValueSetter<bool> own(devicePreparationDispatch, true);
            job.captureAdmitted = true; // Own the request even if later UI formatting throws.
            std::vector<Id> capturedIds;
            for (const auto& binding : job.bindings) if (binding.requiresCapture) capturedIds.push_back(binding.id);
            withPluginState([safe=juce::Component::SafePointer<Studio>(this), operation]
                (juce::Result result, Project snapshot)
            {
                if (!safe || !safe->devicePreparation || safe->devicePreparation->operation != operation) return;
                auto& current = *safe->devicePreparation;
                // Move actual snapshot into persistent ownership before other work.
                using std::swap; swap(current.captured, snapshot);
                current.captureResult = std::move(result);
                current.captureFinished = true;
            }, &capturedIds);
            return;
        }
        if (job.phase == Phase::capturing && job.captureFinished)
        {
            if (job.captureResult.failed()) { refuse(job.captureResult); return; }
            if (auto valid = devicePreparationSourceCurrent(job, false); valid.failed()) { refuse(valid); return; }
            for (auto& binding : job.bindings)
            {
                if (!binding.requiresCapture) continue;
                const auto captured = audio.capturedPluginRevision(binding.id, binding.host);
                const auto* snapshot = job.captured.plugin(binding.id);
                if (!captured || !snapshot || snapshot->descriptor != binding.descriptor ||
                    snapshot->state != binding.host->recoveryState())
                { refuse(juce::Result::fail("device.captureUnavailable: Exact native capture receipt required")); return; }
                binding.capturedRevision = *captured; // Packet's capture revision, never later current value.
            }
            job.captureValidated = true;
            if (auto valid = devicePreparationSourceCurrent(job, false); valid.failed()) { refuse(valid); return; }
            std::unique_ptr<PreparedControllerChange> prepared;
            if (auto result = controller.prepareNativeStateRefresh(job.captured, prepared); result.failed())
            { refuse(result); return; }
            if (auto result = controller.validatePreparedChange(*prepared); result.failed())
            { refuse(result); return; }
            if (auto valid = devicePreparationSourceCurrent(job, false); valid.failed()) { refuse(valid); return; }
            if (!controller.commitPreparedChange(*prepared))
            { refuse(juce::Result::fail("device.staleCapture: Canonical opaque refresh was not admitted")); return; }
            job.documentCommitted = !prepared->isNoOp();
            job.captureApplied = true;
            job.phase = Phase::preparing;
            controller.deliverPendingNotifications();
        }
        if (job.phase == Phase::preparing)
        {
            // Capture/refresh is complete before this call may retire the old
            // context. Retry native device preparation with NO candidate graph;
            // only its actual success permits the dormant replacement below.
            const auto nativeReady = audio.prepareDeviceRestoration(controller.project);
            if (nativeReady.failed())
            {
                if (AudioEngine::preparationPending(nativeReady))
                    return;
                refuse(nativeReady);
                return;
            }
            const auto result = audio.rebuildDeferredDeviceProjection(controller.project, patternMode,
                selectedPattern, [&](bool ready) { return devicePreparationSourceCurrent(job, ready); });
            if (result.failed())
            {
                if (AudioEngine::preparationPending(result)) return;
                refuse(result); return;
            }
            job.nativePrepared = true;
            engineRevision = controller.revision; needsRebuild = false; projectionError.clear();
            const bool requestPlay = job.playRequested;
            finish(Phase::completed);
            if (requestPlay && controller.projectIdentity == job.identity && !audio.playing()) play.onClick();
            return;
        }
        if (job.phase == Phase::draining)
        {
            if (!closesFinished()) return;
            if (job.captureAdmitted && !job.captureFinished)
            {
                // Recover only this admitted owner if an exception occurred
                // before withPluginState installed its application callback.
                if (afterPluginState) return;
                if (audio.pluginStateCapturePending())
                {
                    Project discard; auto result = juce::Result::ok();
                    if (!audio.pollPluginState(discard, result)) return;
                }
                job.captureFinished = true;
            }
            if (!audio.drainDeferredWorkerPreparation(true)) return;
            finish(job.cancelRequested ? Phase::cancelled : Phase::failed);
        }
    }
    catch (...)
    {
        // Capture/close owners remain persistent. No false rollback after a
        // successful opaque refresh or native preparation.
        if (job.nativePrepared) finish(Phase::completed);
        else
        {
            job.phase = Phase::draining;
            if (job.code.isEmpty())
            {
                job.code.swapWith(job.exceptionCode);
                job.message.swapWith(job.exceptionMessage);
            }
        }
    }
}

juce::var Studio::devicePreparationState(std::uint64_t operation) const
{
    auto data = std::make_unique<juce::DynamicObject>();
    const auto* job = devicePreparation.get();
    const bool known = job && (!operation || job->operation == operation);
    data->setProperty("operation", juce::int64(operation ? operation : job ? job->operation : 0));
    data->setProperty("known", known);
    data->setProperty("currentProjectIdentity", controller.projectIdentity);
    data->setProperty("currentRevision", juce::int64(controller.revision));
    data->setProperty("completionScope", "nativePreparation");
    data->setProperty("nativeCompletionKnown", false); // No actual selected-graph/callback claim.
    data->setProperty("transportPlaying", audio.playing());
    data->setProperty("retention", "Current operation only; retry allocates a new token");
    if (!known)
    {
        data->setProperty("phase", "none");
        data->setProperty("completionState", operation ? "failed" : "completed");
        data->setProperty("documentCommitted", false);
        data->setProperty("nativePrepared", false);
        if (operation)
        {
            auto error = std::make_unique<juce::DynamicObject>();
            error->setProperty("code", "device.unknownOperation");
            error->setProperty("message", "Current operation token required; prior retries expire");
            data->setProperty("error", juce::var(error.release()));
        }
        return data.release();
    }
    using Phase = DeferredDevicePreparation::Phase;
    data->setProperty("phase", devicePreparationPhase(job->phase));
    data->setProperty("completionState", job->phase == Phase::completed ? "completed" :
        job->phase == Phase::failed ? "failed" : job->phase == Phase::cancelled ? "cancelled" : "pending");
    data->setProperty("projectIdentity", job->identity);
    data->setProperty("sourceRevision", juce::int64(job->revision));
    data->setProperty("sampleRate", job->rate);
    data->setProperty("blockSize", job->block);
    data->setProperty("documentCommitted", job->documentCommitted);
    data->setProperty("stateCaptured", job->captureApplied);
    data->setProperty("workersReusedOnly", job->workersReusedOnly);
    data->setProperty("nativePrepared", job->nativePrepared);
    data->setProperty("playRequested", job->playRequested);
    data->setProperty("cancellationRequested", job->cancelRequested);
    if (job->code.isNotEmpty())
    {
        auto error = std::make_unique<juce::DynamicObject>();
        error->setProperty("code", job->code); error->setProperty("message", job->message);
        data->setProperty("error", juce::var(error.release()));
    }
    return data.release();
}
juce::var Studio::devicePreparationSchema()
{
    return juce::JSON::parse(R"JSON({"type":"object","additionalProperties":false,
      "required":["operation","known","currentProjectIdentity","currentRevision","completionScope","nativeCompletionKnown","transportPlaying","retention","phase","completionState","documentCommitted","nativePrepared"],
      "properties":{
        "operation":{"type":"integer","minimum":0,"maximum":9007199254740991},
        "known":{"type":"boolean"},"currentProjectIdentity":{"type":"string"},
        "currentRevision":{"type":"integer","minimum":0},"projectIdentity":{"type":"string"},
        "sourceRevision":{"type":"integer","minimum":0},
        "completionScope":{"type":"string","enum":["nativePreparation"]},
        "nativeCompletionKnown":{"type":"boolean","enum":[false]},
        "transportPlaying":{"type":"boolean"},"retention":{"type":"string"},
        "phase":{"type":"string","enum":["none","closingEditors","capturing","preparing","draining","completed","failed","cancelled"]},
        "completionState":{"type":"string","enum":["pending","completed","failed","cancelled"]},
        "documentCommitted":{"type":"boolean"},"nativePrepared":{"type":"boolean"},
        "sampleRate":{"type":"number","exclusiveMinimum":0},"blockSize":{"type":"integer","minimum":1},
        "stateCaptured":{"type":"boolean"},"workersReusedOnly":{"type":"boolean"},
        "playRequested":{"type":"boolean"},"cancellationRequested":{"type":"boolean"},
        "error":{"type":"object","additionalProperties":false,"required":["code","message"],"properties":{"code":{"type":"string"},"message":{"type":"string"}}}
      }})JSON");
}
