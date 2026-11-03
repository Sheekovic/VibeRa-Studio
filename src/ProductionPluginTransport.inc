// SPDX-License-Identifier: AGPL-3.0-or-later
// Ordinary live worker transport. All ownership/preparation is CONTROL-only.
// The native context gate keeps a prepared candidate dormant while its warm
// Hosts can still belong to the incumbent context.
// Private projection storage is declared with ChannelRouting's shared owners.

juce::Result AudioEngine::prepareProductionPluginPlan(const Project &project,
                                                      PreparedPluginTransport::Ptr &output)
{
    const bool plugins = std::any_of(project.channels.begin(), project.channels.end(),
                                     [](const auto &c) { return c.instrument.has_value(); }) ||
                         std::any_of(project.mixer.begin(), project.mixer.end(),
                                     [](const auto &m) { return !m.effects.empty(); });
    auto &manager = engine.getDeviceManager().deviceManager;
    auto *device = manager.getCurrentAudioDevice();
    if (!plugins || !device)
    {
        output.reset(); // Device-free native controls/export remain available.
        return juce::Result::ok();
    }
    juce::AudioDeviceManager::PhysicalCallbackSnapshot physical;
    if (!manager.readPhysicalCallbackSnapshot(physical) || !physical.ownerAlive || !physical.active ||
        physical.writerConflict || physical.countersExhausted)
        return juce::Result::fail("source.deviceUnavailable: No coherent active physical device contract");
    const auto quantum = device->getCurrentBufferSizeSamples();
    const auto rate = device->getCurrentSampleRate();
    if (manager.getCurrentPhysicalCallbackBound() ==
            juce::AudioDeviceManager::PhysicalCallbackBound::unknown &&
        explicitPhysicalProviderFrames > quantum)
        return juce::Result::fail("source.unsupportedCallbackBound: Ordinary live context replacement "
                                  "requires physical callbacks no larger than its native quantum");
    if (productionPluginPlan)
    {
        const auto &budget = productionPluginPlan->budget();
        if (budget.deviceEpoch == physical.deviceEpoch && budget.quantum == quantum &&
            budget.configuredFrames == physical.configuredFrames &&
            productionPluginPlan->sampleRate() == rate &&
            physical.maximumOuterFrames <= budget.physicalFrames)
        {
            output = productionPluginPlan;
            return juce::Result::ok();
        }
    }
    PreparedPluginTransport::Ptr candidate;
    // Cover the native +/-10% source-rate envelope. KQ is declared plugin
    // transport latency, never a device-buffer change or callback wait.
    const auto prepared = PreparedPluginTransport::prepare(manager, quantum, {0, 1}, 1100000,
                                                           explicitPhysicalProviderFrames, candidate);
    if (!prepared.succeeded())
        return juce::Result::fail(
            prepared.source.status == transport::BurstStatus::unknownProviderBound
                ? "source.providerBoundRequired: Custom audio providers need an explicit callback-frame bound"
                : "source.budgetRefused: Physical/source callback contract cannot safely host live plugins");
    productionPluginPlan = std::move(candidate);
    output = productionPluginPlan;
    return juce::Result::ok();
}

bool AudioEngine::pluginHostConfigurationCurrent(const PluginSpec &spec,
                                                 const std::shared_ptr<PluginHost> &host, double rate,
                                                 int block) const
{
    if (!host)
        return false;
    if (initialSourceTransport)
        return host->matchesPrepared(spec, rate, block, initialSourceTransport->plan,
                                     pluginBusRequestFor(spec));
    if (!finiteEffectBaseline && engine.getDeviceManager().deviceManager.getCurrentAudioDevice())
    {
        if (!productionPluginPlan)
            return false;
        juce::AudioDeviceManager::PhysicalCallbackSnapshot physical;
        if (!productionPluginPlan->read(physical) || !physical.active || !physical.ownerAlive ||
            physical.writerConflict || physical.countersExhausted ||
            physical.deviceEpoch != productionPluginPlan->budget().deviceEpoch ||
            physical.maximumOuterFrames > productionPluginPlan->budget().physicalFrames)
            return false;
        return host->matchesPrepared(spec, rate, block, productionPluginPlan, pluginBusRequestFor(spec));
    }
    return host->matches(spec, rate, block, pluginBusRequestFor(spec));
}

bool AudioEngine::invalidateProductionPluginDevice() noexcept
{
    if (initialSourceTransport || finiteEffectBaseline || currentHosts.empty())
        return false;
    productionDeviceInvalidated = true;
    productionPluginPlan.reset(); // Incumbent projection/Hosts keep their old plan alive.
    // The coordinator retained workers but already retired its old context.
    // Let native device preparation finish BEFORE creating any new context.
    if (!edit && deferredDocumentProjection)
        return false;
    return true; // Refuse native reprepare until canonical capture/restoration retires the old context.
}

bool AudioEngine::deviceRestorationRequired() const
{
    if (initialSourceTransport || finiteEffectBaseline)
        return false;
    using Fence = te::DeviceManager::PreparationFenceStatus;
    const auto fence = engine.getDeviceManager().getPreparationFenceStatus();
    if (productionDeviceInvalidated && fence == Fence::refused)
        return true;
    if (fence != Fence::passed || !productionPluginProjection || !productionPluginPlan ||
        currentHosts.empty())
        return false;
    // A session-only failure can leave every native worker healthy. Reuse their
    // current state; dead workers still require explicit saved-state recovery.
    for (const auto &[id, host] : currentHosts)
    {
        const auto *spec = projectedProject.plugin(id);
        if (!spec || !pluginHostConfigurationCurrent(*spec, host, projectedRate, projectedBlock))
            return false;
    }
    te::SourceBurstSession::Outcome native;
    source_membership::Receipt members;
    return productionDeviceInvalidated ||
           (productionPluginProjection->session->read(native) &&
            productionPluginProjection->members->read(members) &&
            (native.failed || native.retired || members.status != source_membership::Status::ready));
}

void AudioEngine::bindRoutingController(Controller &owner)
{
    const auto *messages = juce::MessageManager::getInstanceWithoutCreating();
    jassert(messages && messages->isThisTheMessageThread());
    if (!messages || !messages->isThisTheMessageThread())
        return;
    // Bound by the real application coordinator, never inferred from equal bytes.
    jassert(!routingController || routingController == &owner);
    if (!routingController)
        routingController = &owner;
}

juce::Result AudioEngine::prepareStoppedEffectMembership(Controller &owner, PreparedControllerChange &model,
                                                         PreparedChannelRouting &candidate)
{
    using Broker = source_membership::SourceMembershipBroker;
    const auto *messages = juce::MessageManager::getInstanceWithoutCreating();
    if (!messages || !messages->isThisTheMessageThread() || routingController != &owner)
        return juce::Result::fail("effects.invalidController: Use the bound canonical Controller on CONTROL");
    if (!candidate.impl || candidate.impl->owner != this || !candidate.impl->stoppedEffectChange ||
        candidate.impl->membership || candidate.impl->posted || !model.destinationBeforeCommit() ||
        owner.validatePreparedChange(model).failed())
        return juce::Result::fail(
            "effects.staleTransaction: Unpublished matching Controller/native tickets required");
    auto &ready = *candidate.impl;
    if (!stoppedEffectHistoryWitnessCurrent(owner, model, candidate))
        return juce::Result::fail("effects.invalidHistoryWitness: Canonical captured history changed");
    if (!model.destinationBeforeCommit()->state().isEquivalentTo(ready.destination.state()))
        return juce::Result::fail("effects.destinationChanged: Canonical and native destinations differ");
    if (auto valid = validateGraphProjection(candidate, false, true);
        valid.status != ChannelRoutingStatus::ready)
        return juce::Result::fail(valid.message);
    if (!currentProductionPluginContext(ready.context) ||
        (channelRouting->membership && channelRouting->membership->requested &&
         !channelRouting->membership->acknowledged))
        return juce::Result::fail("effects.membershipBusy: Healthy unowned native source required");
    auto state = std::make_shared<StoppedEffectMembership>();
    state->members = productionPluginProjection->members;
    state->session = productionPluginProjection->session;
    state->context = ready.context;
    state->controller = &owner;
    state->forward = ready.forward;
    state->removalShell = ready.removalShell;
    state->retiredWetShells = ready.retiredWetShells;
    state->removalProgress = ready.removalProgress;
    state->freshJoin = ready.freshJoin;
    if (auto *shell = dynamic_cast<RemoteEffect *>(state->removalShell.get()))
        state->removalWorker = shell->removalWorkerPinControl();
    if (state->members->bindTransitions(owner, *state->context, *state->session) != Broker::Error::none)
        return juce::Result::fail("effects.invalidAuthority: Canonical native source binding refused");
    std::vector<source_membership::PreparedSourceHosts::Binding> bindings;
    bindings.reserve(ready.workers.size() + ready.addedWorkers.size());
    for (const auto &worker : ready.workers)
        bindings.push_back({ready.capturedSource.plugin(worker.id), worker.host, worker.buses, {}});
    for (const auto &worker : ready.addedWorkers)
        bindings.push_back({ready.destination.plugin(worker.id), worker.host, worker.buses, {}});
    if (state->members->prepareExpansion(owner, model, bindings, bindings.size(), state->ticket) !=
        Broker::Error::none)
        return juce::Result::fail("effects.membershipRefused: Exact warm union preparation failed");
    state->readback.phase = StoppedEffectPhase::prepared;
    ready.membership = std::move(state);
    return juce::Result::ok(); // Prepared ONLY; no callback request, graph or history publication.
}

juce::Result AudioEngine::requestStoppedEffectMembership(Controller &owner, PreparedControllerChange &model,
                                                         PreparedChannelRouting &candidate)
{
    using Broker = source_membership::SourceMembershipBroker;
    const auto *messages = juce::MessageManager::getInstanceWithoutCreating();
    if (!messages || !messages->isThisTheMessageThread() || routingController != &owner || !candidate.impl ||
        candidate.impl->owner != this || !candidate.impl->membership || candidate.impl->posted ||
        candidate.impl->membership->requested || candidate.impl->membership->acknowledged ||
        candidate.impl->membership->readback.phase != StoppedEffectPhase::prepared)
        return juce::Result::fail("effects.staleTransaction: Unrequested real membership ticket required");
    auto &ready = *candidate.impl;
    auto &state = *ready.membership;
    if (!stoppedEffectHistoryWitnessCurrent(owner, model, candidate))
        return juce::Result::fail(
            "effects.invalidHistoryWitness: Canonical captured history changed before request");
    if (auto valid = validateGraphProjection(candidate, false, true);
        valid.status != ChannelRoutingStatus::ready)
        return juce::Result::fail(valid.message);
    if (state.members->requestExpansion(state.ticket, owner, model) != Broker::Error::none)
        return juce::Result::fail("effects.membershipRefused: Source/model changed before publication");
    state.requested = true;
    state.readback.phase = StoppedEffectPhase::pending;
    channelRouting->membership = ready.membership; // Pins it even if the candidate is abandoned.
    return juce::Result::ok(); // Admission ONLY; a real closed callback must acknowledge expansion.
}

void AudioEngine::abandonStoppedEffectMembership(
    const std::shared_ptr<StoppedEffectMembership> &state) noexcept
{
    if (!state || !state->requested || state->acknowledged || state->readback.graphPosted ||
        state->readback.phase == StoppedEffectPhase::failed)
        return;
    using Broker = source_membership::SourceMembershipBroker;
    if (state->members->cancel(state->ticket) == Broker::Error::none)
    {
        state->cancelRequested = true;
        state->readback.phase = StoppedEffectPhase::cancelling;
    }
    else
    {
        state->readback.phase = StoppedEffectPhase::failed;
        state->readback.error = "effects.cancelRefused";
    }
}

juce::Result AudioEngine::cancelStoppedEffectMembership(PreparedChannelRouting &candidate)
{
    const auto *messages = juce::MessageManager::getInstanceWithoutCreating();
    if (!messages || !messages->isThisTheMessageThread() || !candidate.impl ||
        candidate.impl->owner != this || !candidate.impl->membership)
        return juce::Result::fail("effects.staleTransaction: Owned CONTROL membership ticket required");
    auto &state = *candidate.impl->membership;
    candidate.impl->validated = false; // Cancellation invalidates earlier publication admission.
    if (candidate.impl->posted || state.readback.graphPosted)
        return juce::Result::fail(
            "effects.tooLate: Published graph requires a new canonical inverse transaction");
    if (!state.requested)
    {
        state.readback.phase = StoppedEffectPhase::cancelled;
        state.acknowledged = true; // No request was published; no native scope can reference it.
        return juce::Result::ok();
    }
    if (state.acknowledged)
        return state.readback.phase == StoppedEffectPhase::cancelled
                   ? juce::Result::ok()
                   : juce::Result::fail("effects.tooLate: Transaction already completed");
    abandonStoppedEffectMembership(candidate.impl->membership);
    return state.readback.phase == StoppedEffectPhase::failed
               ? juce::Result::fail(state.readback.error)
               : juce::Result::ok(); // Cancelling, NOT cancelled.
}

AudioEngine::ChannelRoutingOutcome
AudioEngine::validateStoppedEffectChange(Controller &owner, PreparedControllerChange &model,
                                         PreparedChannelRouting &candidate) const
{
    using S = ChannelRoutingStatus;
    using Broker = source_membership::SourceMembershipBroker;
    const auto *messages = juce::MessageManager::getInstanceWithoutCreating();
    if (!messages || !messages->isThisTheMessageThread() || routingController != &owner || !candidate.impl ||
        candidate.impl->owner != this || !candidate.impl->membership)
        return {S::unsupportedDelta,
                "effects.canonicalAdmissionRequired: Actual CONTROL transaction required"};
    auto &ready = *candidate.impl;
    ready.validated = false;
    if (!stoppedEffectHistoryWitnessCurrent(owner, model, candidate))
        return {S::staleProjection,
                "effects.invalidHistoryWitness: Canonical captured history changed before publication"};
    if (auto valid = validateGraphProjection(candidate, false, true); valid.status != S::ready)
        return valid;
    auto &state = *ready.membership;
    te::SourceBurstSession::Outcome native;
    if (!state.requested || state.acknowledged || state.cancelRequested || state.removalRequested)
        return {S::staleProjection,
                "effects.staleTransaction: Membership is not an active pre-post transaction"};
    if (state.readback.phase == StoppedEffectPhase::failed)
        return {S::graphRefused, state.readback.error};
    if (!state.session->read(native))
        return {S::busy, "effects.readbackPending: Coherent native source read required"};
    const auto admission =
        state.members->validateForGraphPost(state.ticket, owner, model, *ready.graph, native);
    if (admission == Broker::Error::pendingNativeClose)
        return {S::busy,
                "effects.membershipPending: Exact expanded native-close ACK required before publication"};
    if (admission != Broker::Error::none)
        return {S::staleProjection, "effects.staleMembership: Canonical/native membership admission changed"};
    if (ready.forward)
    {
        ordinary_sample_ports::Policy::Receipt source;
        if (!productionPluginProjection || !productionPluginProjection->ports)
            return {S::staleContext, "effects.forwardOwnerChanged: Actual source port owner required"};
        if (!productionPluginProjection->ports->read(source))
            return {S::busy, "effects.forwardReadbackPending: Coherent actual root receipt required"};
        if (source.optionalHandoffFault)
            return {S::staleProjection, "effects.forwardScopeFailed: Actual source/MIDI root failed"};
    }
    ready.validated = true;
    return {S::ready, {}};
}

void AudioEngine::serviceStoppedEffectMembership()
{
    using Broker = source_membership::SourceMembershipBroker;
    const auto *messages = juce::MessageManager::getInstanceWithoutCreating();
    if (!messages || !messages->isThisTheMessageThread() || !channelRouting || !channelRouting->membership)
        return;
    auto &state = *channelRouting->membership;
    // The removed Host is intentionally absent from currentHosts while its
    // existing stream finishes the fade. Service only this retained owned
    // instance, including graceful exit after final membership acknowledgement.
    if (state.removalWorker)
    {
        state.removalWorker->service();
        if (state.acknowledged && state.removalWorker->retirementComplete())
            state.removalWorker.reset();
    }
    if (!state.requested || state.acknowledged || state.readback.phase == StoppedEffectPhase::failed)
        return;
    const auto fail = [&](const char *error)
    {
        state.readback.phase = StoppedEffectPhase::failed;
        state.readback.error = error;
    };
    // Bounded seqlock read contention is NOT context loss. Only immutable
    // owner/context/device mismatches are terminal here; coherent reads follow.
    if (state.controller != routingController || !edit || productionDeviceInvalidated ||
        !productionPluginPlan || !productionPluginProjection ||
        productionPluginProjection->context != state.context ||
        edit->getTransport().getCurrentPlaybackContext() != state.context ||
        productionPluginProjection->plan != productionPluginPlan ||
        productionPluginProjection->members != state.members ||
        productionPluginProjection->session != state.session)
        return fail("effects.sourceLost");
    try
    {
        // Observe selection BEFORE the source close. A pre-selection close or
        // the returned pending-holder's destruction cannot exclude predecessor
        // readers. A superseding publication invalidates this transaction.
        const auto selected = state.context->getGraphSelection();
        if (state.readback.graphPosted && selected.postedSerial > state.graphSerial)
            return fail("effects.graphSuperseded");
        te::SourceBurstSession::Outcome native;
        if (!state.session->read(native))
            return; // Bounded read contention is pending, not a fabricated failure/completion.
        // Acquire actual native close BEFORE root health. A pre-close healthy
        // read could miss a fault from that very callback and falsely complete.
        if (state.forward || state.removalProgress || state.freshJoin)
        {
            ordinary_sample_ports::Policy::Receipt source;
            if (!productionPluginProjection->ports)
                return fail("effects.forwardOwnerChanged");
            if (!productionPluginProjection->ports->read(source))
                return; // A bounded read miss is pending, never fabricated source loss.
            if (source.optionalHandoffFault)
            {
                if (state.freshJoin)
                {
                    using Fault = RemoteEffect::FreshJoinProgress::Fault;
                    switch (state.freshJoin->observation().fault)
                    {
                    case Fault::revoked:
                        return fail("effects.freshJoinRootRefused");
                    case Fault::clock:
                        return fail("effects.freshJoinClock");
                    case Fault::span:
                        return fail("effects.freshJoinSpan");
                    case Fault::predecessor:
                        return fail("effects.freshJoinPredecessor");
                    case Fault::freshOwner:
                        return fail("effects.freshJoinOwnerChanged");
                    case Fault::packet:
                        return fail("effects.freshJoinPacket");
                    case Fault::native:
                        return fail("effects.freshJoinNative");
                    case Fault::history:
                        return fail("effects.freshJoinHistory");
                    case Fault::overflow:
                        return fail("effects.freshJoinOverflow");
                    default:
                        break;
                    }
                }
                if (state.removalProgress && source.continuationState)
                {
                    using Native = tracktion::graph::FinishedForwardContinuation::State;
                    switch (Native(source.continuationState - 1))
                    {
                    case Native::stalePredecessor: return fail("effects.removalStalePredecessor");
                    case Native::discontinuity: return fail("effects.removalReferenceDiscontinuity");
                    case Native::priorFailed: return fail("effects.removalPriorDrainFailed");
                    case Native::wrongRoot: return fail("effects.removalWrongRoot");
                    case Native::wrongContinuity: return fail("effects.removalWrongContinuity");
                    case Native::invalidReference: return fail("effects.removalInvalidReference");
                    case Native::notAdmitted: return fail("effects.removalNotAdmitted");
                    case Native::sourceChanged:
                        switch (state.removalProgress->observation().fault)
                        {
                        case RemoteEffect::RemovalProgress::Fault::clock: return fail("effects.removalObserverClock");
                        case RemoteEffect::RemovalProgress::Fault::span: return fail("effects.removalObserverSpan");
                        case RemoteEffect::RemovalProgress::Fault::history: return fail("effects.removalObserverHistory");
                        case RemoteEffect::RemovalProgress::Fault::native: return fail("effects.removalObserverNative");
                        case RemoteEffect::RemovalProgress::Fault::identity: return fail("effects.removalObserverIdentity");
                        case RemoteEffect::RemovalProgress::Fault::packet: return fail("effects.removalObserverPacket");
                        case RemoteEffect::RemovalProgress::Fault::overflow: return fail("effects.removalObserverOverflow");
                        default: return fail("effects.removalSourceChanged");
                        }
                    case Native::incompleteOutput: return fail("effects.removalIncompleteOutput");
                    default: break;
                    }
                }
                return fail("effects.forwardScopeFailed");
            }
        }
        const auto status = state.members->observe(*state.ticket, native);
        state.readback.operation = status.operation;
        state.readback.membership = status.membership;
        state.readback.physicalSerial = status.physicalSerial;
        if (status.state == Broker::State::failed)
            return fail("effects.membershipFailed");
        if (status.state == Broker::State::cancelled || status.state == Broker::State::completed)
        {
            const auto acknowledged = state.members->acknowledge(state.ticket, native);
            if (acknowledged == Broker::Error::pendingNativeClose)
                return; // A new physical begin may race the previous closed snapshot.
            if (acknowledged != Broker::Error::none)
                return fail("effects.acknowledgementRefused");
            state.acknowledged = true;
            state.readback.phase = status.state == Broker::State::cancelled ? StoppedEffectPhase::cancelled
                                                                            : StoppedEffectPhase::completed;
            state.ticket
                .reset(); // CONTROL after actual ACK: release source/union pins, keep compact receipt.
            if (status.state == Broker::State::completed && state.removalShell)
            {
                auto *shell = dynamic_cast<RemoteEffect *>(state.removalShell.get());
                if (!shell || state.retiredWetShells.isEmpty())
                    return fail("effects.workerRetirementRefused");
                for (auto *plugin : state.retiredWetShells)
                {
                    auto *previous = dynamic_cast<RemoteEffect *>(plugin);
                    if (!previous || !previous->retainsWetPredecessorControl(*shell))
                        return fail("effects.retiredWetOwnerChanged");
                }
                for (auto *plugin : state.retiredWetShells)
                    if (static_cast<RemoteEffect *>(plugin)->releaseRetiredWetWorkerControl(*shell).failed())
                        return fail("effects.workerRetirementRefused");
                if (shell->releaseRemovalWorkerControl().failed())
                    return fail("effects.workerRetirementRefused");
                state.removalShell = nullptr;
                state.retiredWetShells.clear();
            }
            return;
        }
        if (status.state != Broker::State::expanded)
            return; // Preserve cancelling/retiring while their actual close is pending.
        state.readback.phase =
            state.readback.graphPosted ? StoppedEffectPhase::retiring : StoppedEffectPhase::expanded;
        if (!state.readback.graphPosted || state.removalRequested)
            return;
        if (selected.state != tracktion::graph::GraphSelectionState::selected ||
            selected.selectedSerial != state.graphSerial || selected.postedSerial != state.graphSerial)
            return;
        // Pinned EnginePolicy's allocated graph has zero auxiliary workers.
        // Native player processMutex excludes the earlier caller's processing
        // before B is selected. The paired close observed above follows B's
        // processing, including all contexts and Host scopes. This proves DSP
        // quiescence, NOT destruction of A's cached graph/PluginPtrs/workers.
        if (!state.context->isPlaybackGraphAllocated() ||
            engine.getEngineBehaviour().getNumberOfCPUsToUseForAudio() != 1)
            return fail("effects.unsupportedQuiescencePolicy");
        const auto confirmed = state.context->getGraphSelection();
        if (confirmed.state != tracktion::graph::GraphSelectionState::selected ||
            confirmed.selectedSerial != state.graphSerial || confirmed.postedSerial != state.graphSerial)
            return;
        state.readback.graphRetired = true; // Predecessor DSP readers only; no object-reclamation promise.
        if (state.freshJoin)
        {
            const auto joined = state.freshJoin->observation();
            if (joined.phase == RemoteEffect::FreshJoinProgress::Phase::failed)
                return fail("effects.freshJoinFailed");
            if (joined.phase != RemoteEffect::FreshJoinProgress::Phase::joined)
                return; // Actual same-time wet endpoint precedes membership completion.
        }
        if (state.removalProgress)
        {
            using Phase = RemoteEffect::RemovalProgress::Phase;
            const auto removal = state.removalProgress->observation();
            if (removal.phase == Phase::failed)
                return fail("effects.removalFadeFailed");
            if (removal.phase != Phase::dry)
                return; // Actual fade and ring transfer precede the smaller source bank.
        }
        if (state.forward)
        {
            // Selection alone is not a drained delay queue. Keep the union and
            // transaction pending until the adopted native drain really ends.
            using Phase = tracktion::graph::ForwardOutputDrain::State;
            switch (state.forward->observation().state)
            {
            case Phase::prepared:
            case Phase::admitted:
            case Phase::draining:
                return;
            case Phase::finished:
                break;
            default:
                return fail("effects.forwardDrainFailed");
            }
        }
        const Broker::DspQuiescencePermit retired(*state.members, *state.ticket, *state.controller,
                                                  *state.context, state.graphSerial, native);
        std::vector<source_membership::PreparedSourceHosts::Binding> final;
        final.reserve(currentHosts.size());
        for (const auto &[id, host] : currentHosts)
        {
            const auto *spec = projectedProject.plugin(id);
            if (!spec || !host)
                return fail("effects.finalMembershipChanged");
            final.push_back({spec, host, host->busLayout(), {}});
        }
        const auto requested =
            state.members->requestRemoval(state.ticket, *state.controller, final, final.size(), retired);
        if (requested == Broker::Error::pendingNativeClose)
            return;
        if (requested != Broker::Error::none)
            return fail("effects.retirementRefused");
        state.removalRequested = true;
        // Terminal success waits for this final bank's real end + native close.
    }
    catch (...)
    {
        fail("effects.membershipServiceFailed");
    }
}

AudioEngine::StoppedEffectReadback
AudioEngine::stoppedEffectMembershipReadback(PreparedChannelRouting &candidate)
{
    const auto *messages = juce::MessageManager::getInstanceWithoutCreating();
    if (!messages || !messages->isThisTheMessageThread())
        return {StoppedEffectPhase::failed, "effects.controlRequired"};
    if (!candidate.impl || candidate.impl->owner != this || !candidate.impl->membership)
        return {StoppedEffectPhase::failed, "effects.ownerGone"};
    (void)observeChannelRoutingSelection();
    return candidate.impl->membership->readback;
}

bool AudioEngine::currentProductionPluginContext(const te::EditPlaybackContext *context) const noexcept
{
    if (!context || !productionPluginPlan || !productionPluginProjection || productionDeviceInvalidated ||
        productionPluginProjection->plan != productionPluginPlan ||
        productionPluginProjection->context != context)
        return false;
    te::SourceBurstSession::Outcome source;
    source_membership::Receipt members;
    return productionPluginProjection->session->read(source) &&
           productionPluginProjection->members->read(members) && !source.failed && !source.retired &&
           members.status == source_membership::Status::ready &&
           source.generation == members.sessionGeneration &&
           source.deviceEpoch == productionPluginPlan->budget().deviceEpoch &&
           members.deviceEpoch == source.deviceEpoch;
}

juce::Result AudioEngine::stoppedEffectWorkerTransport(PreparedPluginTransport::Ptr &output) const
{
    const auto *messages = juce::MessageManager::getInstanceWithoutCreating();
    if (!messages || !messages->isThisTheMessageThread())
        return juce::Result::fail("effects.controlRequired: Prepare native workers on CONTROL");
    if (playing() || initialSourceTransport || finiteEffectBaseline)
        return juce::Result::fail("effects.stoppedRequired: Ordinary stopped projection required");
    const auto &manager = engine.getDeviceManager();
    auto *device = manager.deviceManager.getCurrentAudioDevice();
    if (!device || !edit || !productionPluginProjection || !productionPluginPlan ||
        productionPluginProjection->plan != productionPluginPlan ||
        productionPluginProjection->context != edit->getTransport().getCurrentPlaybackContext() ||
        productionDeviceInvalidated ||
        manager.getPreparationFenceStatus() != te::DeviceManager::PreparationFenceStatus::passed)
        return juce::Result::fail("effects.sourceUnavailable: Exact prepared native source context required");
    if (preparation[0] || preparation[1] || capture || worker || renderEdit || compensationGraph ||
        recoveryPlugin || channelRoutingPostPending() || !channelRouting->prepared.empty())
        return juce::Result::fail("effects.busy: Finish native projection, capture or recovery work first");
    const auto &plan = productionPluginPlan;
    const auto &budget = plan->budget();
    juce::AudioDeviceManager::PhysicalCallbackSnapshot physical;
    const bool known = plan->read(physical);
    const auto admitted = transport::admitPhysicalBurst(known, physical, budget);
    if (!known || device->getCurrentSampleRate() != plan->sampleRate() ||
        device->getCurrentBufferSizeSamples() != budget.quantum ||
        physical.maximumOuterFrames > budget.physicalFrames ||
        (admitted.status != transport::BurstStatus::ready &&
         !(admitted.status == transport::BurstStatus::noPhysicalCallback && physical.outerFrames == 0 &&
           physical.maximumOuterFrames == 0)))
        return juce::Result::fail("effects.transportChanged: Actual native device contract changed");
    te::SourceBurstSession::Outcome source;
    source_membership::Receipt members;
    if (!productionPluginProjection->session->read(source) ||
        !productionPluginProjection->members->read(members) || source.failed || source.retired ||
        members.status != source_membership::Status::ready ||
        source.generation != members.sessionGeneration || source.deviceEpoch != budget.deviceEpoch ||
        members.deviceEpoch != budget.deviceEpoch ||
        productionPluginProjection->members->size() != currentHosts.size())
        return juce::Result::fail("effects.sourceUnavailable: Coherent healthy source membership required");
    output = plan; // Failure never replaces a caller-owned plan or creates a lookalike.
    return juce::Result::ok();
}

juce::Result AudioEngine::prepareDeviceRestoration(const Project &captured)
{
    const auto *messages = juce::MessageManager::getInstanceWithoutCreating();
    if (!messages || !messages->isThisTheMessageThread() || initialSourceTransport || finiteEffectBaseline)
        return juce::Result::fail("device.invalidOwner: Ordinary CONTROL restoration required");
    auto &manager = engine.getDeviceManager();
    if (!manager.deviceManager.getCurrentAudioDevice())
        return juce::Result::fail("device.unavailable: Actual audio device required");
    if (auto valid = captured.validate(); valid.failed())
        return valid;
    using Fence = te::DeviceManager::PreparationFenceStatus;
    const bool sourceOnly = manager.getPreparationFenceStatus() == Fence::passed && productionPluginPlan;
    if (edit)
    {
        if (!deviceRestorationRequired() || capture || worker || renderEdit || preparation[0] ||
            !channelRouting->prepared.empty())
            return juce::Result::fail(
                "device.busy: Suspended ordinary projection and drained preparation required");
        // The validated canonical document may contain a musical edit that had
        // not reached the old graph when its device stopped. Reconstruct THAT
        // document; require exact native ownership/capture, not old note bytes.
        for (const auto &[id, host] : currentHosts)
        {
            const auto *plugin = captured.plugin(id);
            const auto *native = projectedProject.plugin(id);
            if (sourceOnly)
            {
                if (!plugin || !native || plugin->descriptor != native->descriptor ||
                    !pluginHostConfigurationCurrent(*plugin, host, projectedRate, projectedBlock))
                    return juce::Result::fail("source.workerChanged: Exact healthy warm worker required");
                continue; // No opaque reload, editor close or native device reset.
            }
            const auto receipt = lastCapturedRevisions.find(id);
            if (!plugin || !native || plugin->descriptor != native->descriptor || !host->alive() ||
                host->editorBusy() || host->editorWindowState().exists || host->pendingParameterRequests() ||
                receipt == lastCapturedRevisions.end() || receipt->second.host != host ||
                receipt->second.value != host->parameterRevision() || plugin->state != host->recoveryState())
                return juce::Result::fail(
                    "device.stateCaptureRequired: Exact current native state required before retirement");
        }
        // Actual refused native fence has suspended this device. Old context
        // exclusion/destruction occurs on CONTROL; workers and their captured
        // portable state remain recoverable, including cancellation afterward.
        deferredPositionBeat = positionBeat();
        deferredLooping = looping();
        stop();
        deferredDocumentProjection = true;
        deferredHostProjection = true;
        compensationGraph.reset();
        releaseChannelRoutingForEdit();
        projectedChannels.clear();
        projectedAudioClips.clear();
        projectedInserts.clear();
        effectPlugins.clear();
        edit.reset();
        productionPluginProjection.reset();
        if (!sourceOnly)
            productionPluginPlan.reset();
        hasProjection = false;
    }
    if (!deferredDocumentProjection)
        return juce::Result::fail("device.invalidOwner: Deferred captured projection required");
    if (manager.getPreparationFenceStatus() == Fence::refused)
    {
        if (!productionDeviceRetryQueued)
        {
            if (!manager.retryPreparationFence(this))
                return juce::Result::fail("device.retryUnavailable: Owned native retry could not be queued");
            productionDeviceRetryQueued = true;
        }
        return juce::Result::fail("Preparing native device...");
    }
    productionDeviceRetryQueued = false;
    if (manager.getPreparationFenceStatus() != Fence::passed)
        return juce::Result::fail("device.preparationFailed: Native device preparation has not passed");
    PreparedPluginTransport::Ptr plan;
    if (auto ready = prepareProductionPluginPlan(captured, plan); ready.failed())
        return ready;
    const bool promotionCandidate =
        plan && std::any_of(currentHosts.begin(), currentHosts.end(),
                            [](const auto &entry)
                            {
                                return entry.second && entry.second->transportPolicy() ==
                                                           PluginHost::TransportPolicy::oneQuantum;
                            });
    // Finalize only genuinely unprocessed control-only workers. A missing Edit
    // alone is insufficient if another preparation/render may inspect them.
    // Replacement may already own preparation on a later coordinator poll.
    // Promotion is optional: skip it while readers exist, rather than failing
    // that valid capture/replacement job. Every actual promotion still requires
    // complete reader exclusion; processed/mismatched Hosts remain untouched.
    if (promotionCandidate && !preparation[0] && !preparation[1] && retiredPreparation.empty() &&
        !renderEdit && !worker && !capture && !channelRouting->effective && !channelRouting->pendingGraph &&
        channelRouting->prepared.empty() && engine.getActiveEdits().getEdits().isEmpty())
        for (const auto &[id, host] : currentHosts)
            if (const auto *spec = captured.plugin(id);
                spec && host &&
                !host->matchesPrepared(*spec, plan->sampleRate(), plan->budget().quantum, plan,
                                       pluginBusRequestFor(*spec)))
            {
                // Typed refusal leaves the Host untouched. Processed/mismatched
                // workers follow normal capture/replacement; never force reuse.
                (void)host->promoteFreshPreparedTransport(*spec, plan->sampleRate(), plan->budget().quantum,
                                                          plan, pluginBusRequestFor(*spec));
            }
    return juce::Result::ok();
}

juce::Result
AudioEngine::stageProductionPluginProjection(te::Edit &candidate, const Project &project,
                                             const std::map<Id, std::shared_ptr<PluginHost>> &hosts,
                                             std::shared_ptr<ProductionPluginProjection> &output)
{
    if (!productionPluginPlan)
        return juce::Result::ok();
    if (productionSessionGeneration == std::numeric_limits<std::uint64_t>::max())
        return juce::Result::fail("source.generationExhausted: No candidate published");
    auto prepared = std::make_shared<ProductionPluginProjection>();
    prepared->plan = productionPluginPlan;
    std::vector<source_membership::PreparedSourceHosts::Binding> bindings;
    bindings.reserve(hosts.size());
    for (const auto &[id, host] : hosts)
    {
        const auto *spec = project.plugin(id);
        if (!spec || !host)
            return juce::Result::fail(
                "source.memberUnavailable: Actual projection Host has no matching plugin");
        bindings.push_back({spec, host, pluginBusRequestFor(*spec), {}});
    }
    const auto &budget = prepared->plan->budget();
    const auto generation = ++productionSessionGeneration;
    te::SourceBurstSession::Configuration config{
        budget.deviceEpoch,    generation,          prepared->plan->sampleRate(), budget.configuredFrames,
        budget.physicalFrames, budget.sourceFrames, budget.maximumSourceScalePpm};
    const auto membership = source_membership::SourceMembershipBroker::prepare(
        prepared->plan, config, generation, bindings, bindings.size(), prepared->members);
    if (!membership.succeeded())
        return juce::Result::fail(
            "source.membershipRefused: Live worker membership does not match its prepared transport");
    auto &manager = engine.getDeviceManager();
    if (te::SourceBurstSession::prepare(manager.deviceManager, config, prepared->members,
                                        prepared->session) != te::SourceBurstSession::Status::prepared)
        return juce::Result::fail("source.sessionRefused: No native source session prepared");
    candidate.getTransport().ensureContextAllocated();
    prepared->context = candidate.getTransport().getCurrentPlaybackContext();
    if (!prepared->context || manager.stageSourceBurstSession(*prepared->context, prepared->session) !=
                                  te::SourceBurstSession::Status::staged)
        return juce::Result::fail(
            "source.stageRefused: Candidate remains dormant; previous playback retained");
    output = std::move(prepared);
    return juce::Result::ok();
}
