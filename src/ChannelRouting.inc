// SPDX-License-Identifier: AGPL-3.0-or-later
// Included inside namespace vibera, after FixedSampler/GuidePlugin definitions.
// CONTROL-only production preparation. No UI/MCP command mutation lives here.
static_assert(std::is_nothrow_swappable_v<Project>, "Routing Project publication must not throw");
// Shared private storage precedes graph and source service implementations.
struct AudioEngine::ProductionPluginProjection
{
    PreparedPluginTransport::Ptr plan;
    source_membership::SourceMembershipBroker::Ptr members;
    te::SourceBurstSession::Ptr session;
    ordinary_sample_ports::Policy::Ptr ports;
    te::EditPlaybackContext *context = nullptr; // Borrowed from this projection's Edit.
};
struct AudioEngine::EffectiveRoutingLists
{
    te::Edit *edit = nullptr;
    juce::WeakReference<te::EditPlaybackContext> context;
    std::shared_ptr<LiveTimeline> scope;
    std::map<const te::PluginList *, te::Plugin::Array> lists;
    std::map<std::uint64_t, te::Plugin::Ptr> nativeIds;
    std::map<int, te::Plugin::Ptr> gains;
    te::Plugin::Array helpers;
    // Deliberately no predecessor set, GraphOverlay or native ticket: no cycle
    // and no permanent history chain. Native Nodes pin their actual PluginPtrs.
};
struct AudioEngine::StoppedEffectDelta
{
    StoppedEffectChangePlan plan;
    Project capturedSource;
    StoppedEffectWorkers::Ptr workers;
};
struct AudioEngine::InitialSourceRoutePermit
{
    const Controller* canonicalOwner = nullptr; // Compared only; never dereferenced via stored address.
    const PreparedControllerChange *canonicalTicket = nullptr;
    Project source, publicationProject;
    Id channel = 0;
    int from = -1, to = -1;
    std::shared_ptr<experimental::AppMixerAudioPorts::PreparedRoute> binding;
};
struct AudioEngine::StoppedEffectMembership
{
    source_membership::SourceMembershipBroker::Ptr members;
    source_membership::SourceMembershipBroker::Ticket::Ptr ticket;
    te::SourceBurstSession::Ptr session;
    te::EditPlaybackContext *context = nullptr; // Borrowed; checked before any use.
    Controller *controller = nullptr;
    StoppedEffectReadback readback;
    std::shared_ptr<ForwardStageDrainNode::Progress> forward;
    te::Plugin::Ptr removalShell;
    te::Plugin::Array retiredWetShells;
    std::shared_ptr<RemoteEffect::RemovalProgress> removalProgress;
    std::shared_ptr<RemoteEffect::FreshJoinProgress> freshJoin;
    std::shared_ptr<PluginHost> removalWorker;
    std::uint64_t graphSerial = 0;
    bool requested = false, cancelRequested = false, removalRequested = false, acknowledged = false;
};
struct AudioEngine::PreparedChannelRouting::Impl
{
    AudioEngine *owner = nullptr;
    te::Edit *edit = nullptr;
    te::EditPlaybackContext *context = nullptr;
    std::uint64_t generation = 0, documentGeneration = 0, publicationSerial = 0;
    double rate = 0;
    int block = 0;
    bool patternMode = false, posted = false, validated = false;
    Id pattern = 0;
    Project sourceProject, destination;
    juce::ValueTree nativeState;
    std::shared_ptr<EffectiveRoutingLists> sourceLists, candidate;
    std::unique_ptr<TimelineProjection> newTimelineOwner;
    TimelineProjection *timelineOwner = nullptr;
    TimelineProjection::Prepared timelineTicket;
    std::shared_ptr<LiveTimeline> timelineAlias;
    std::map<Id, ProjectedChannel> channels;
    std::map<Id, te::Plugin::Ptr> effects;
    struct Worker
    {
        Id id = 0;
        std::shared_ptr<PluginHost> host;
        juce::String instance;
        PluginBusRequest buses;
        int quantum = 0, latency = 0;
        std::uint64_t declaration = 0;
        unsigned parameterRevision = 0; // Added private workers only; legacy route semantics unchanged.
    };
    std::vector<Worker> workers, addedWorkers;
    std::shared_ptr<InitialSourceRoutePermit> sourceRoute;
    bool stoppedEffectChange = false;
    std::shared_ptr<ForwardStageDrainNode::Progress> forward;
    te::Plugin::Ptr removalShell;
    te::Plugin::Array retiredWetShells;
    std::shared_ptr<RemoteEffect::RemovalProgress> removalProgress;
    std::shared_ptr<RemoteEffect::FreshJoinProgress> freshJoin;
    std::shared_ptr<StoppedEffectMembership> membership, previousMembership;
    Project capturedSource;
    StoppedEffectWorkers::Ptr newEffectWorkers;
    const PreparedControllerChange *capturedHistoryModel = nullptr; // Sole CONTROL ticket, pre-post only.
    int capturedHistoryInsert = 0;
    std::map<Id, std::shared_ptr<PluginHost>> destinationHosts;
    std::map<Id, juce::String> destinationErrors;
    std::map<Id, AudioEngine::Revision> destinationObserved;
    std::shared_ptr<te::EditPlaybackContext::GraphOverlay> overlay;
    // Declared last and explicitly destroyed first while Edit is alive.
    std::unique_ptr<te::EditPlaybackContext::PreparedGraphChange> graph;
    void clearNativeOwners() noexcept
    {
        graph.reset();
        sourceRoute.reset();
        overlay.reset();
        candidate.reset();
        sourceLists.reset();
        channels.clear();
        effects.clear();
        workers.clear();
        addedWorkers.clear();
        destinationHosts.clear();
        destinationErrors.clear();
        destinationObserved.clear();
        newEffectWorkers.reset(); // CONTROL; new Hosts also have the engine retirement pin.
        membership.reset();       // Context teardown must also release borrowed-session reader owners.
        previousMembership.reset();
        forward.reset();
        removalShell = nullptr;
        retiredWetShells.clear();
        removalProgress.reset();
        freshJoin.reset();
        timelineTicket = TimelineProjection::Prepared{};
        timelineAlias.reset();
        newTimelineOwner.reset();
        timelineOwner = nullptr;
    }
    ~Impl();
};
struct AudioEngine::ChannelRoutingState
{
    std::uint64_t generation = 1, postedSerial = 0;
    te::EditPlaybackContext *context = nullptr;
    std::shared_ptr<EffectiveRoutingLists> effective;
    std::unique_ptr<TimelineProjection> timelineOwner;
    std::unique_ptr<te::EditPlaybackContext::PreparedGraphChange> pendingGraph;
    std::vector<PreparedChannelRouting::Impl *> prepared;
    std::uint64_t sourceExchangeGeneration = 0;
    bool selected = false, deferredLatency = false, explicitFullReplacement = false;
    std::shared_ptr<StoppedEffectMembership> membership;
    bool deviceInvalidated = false;
};
AudioEngine::PreparedChannelRouting::Impl::~Impl()
{
    if (owner && membership && !posted)
        owner->abandonStoppedEffectMembership(membership);
    if (owner && owner->channelRouting)
    {
        auto &entries = owner->channelRouting->prepared;
        entries.erase(std::remove(entries.begin(), entries.end(), this), entries.end());
    }
    clearNativeOwners();
}
AudioEngine::PreparedChannelRouting::PreparedChannelRouting() : impl(std::make_unique<Impl>()) {}
AudioEngine::PreparedChannelRouting::~PreparedChannelRouting() = default;
bool AudioEngine::PreparedChannelRouting::wasPosted() const noexcept
{
    return impl && impl->posted;
}

std::uint64_t AudioEngine::PreparedChannelRouting::publicationSerial() const noexcept
{
    return impl ? impl->publicationSerial : 0;
}

AudioEngine::ChannelRoutingReadback AudioEngine::channelRoutingReadback() const noexcept
{
    ChannelRoutingReadback result;
    if (!channelRouting || !channelRouting->effective)
        return result;
    const auto &current = *channelRouting;
    result.generation = current.generation;
    result.postedSerial = current.postedSerial;
    result.overlayActive = true;
    if (current.deviceInvalidated)
    {
        result.status = ChannelRoutingStatus::deviceChanged;
        return result;
    }
    if (!edit || current.effective->context.get() != current.context ||
        edit->getTransport().getCurrentPlaybackContext() != current.context ||
        projectedRate != engine.getDeviceManager().getSampleRate() ||
        projectedBlock != engine.getDeviceManager().getBlockSize())
    {
        result.status = ChannelRoutingStatus::staleContext;
        return result;
    }
    const auto selected = current.context->getGraphSelection();
    result.nativePostedSerial = selected.postedSerial;
    result.selectedSerial = selected.selectedSerial;
    result.referenceStart = selected.referenceStart;
    result.referenceEnd = selected.referenceEnd;
    if (initialSourceExchangeFaulted())
    { result.status=ChannelRoutingStatus::graphRefused; return result; }
    if (current.sourceExchangeGeneration &&
        initialSourceCompletedExchangeGeneration() < current.sourceExchangeGeneration)
    { result.status=ChannelRoutingStatus::posted; return result; }
    if (selected.state == tracktion::graph::GraphSelectionState::awaitingFirstSelection &&
        selected.postedSerial == current.postedSerial)
    {
        result.status = ChannelRoutingStatus::posted;
        return result;
    }
    if (selected.state != tracktion::graph::GraphSelectionState::selected)
    {
        result.status = ChannelRoutingStatus::readbackUnavailable;
        return result;
    }
    if (current.selected && selected.selectedSerial >= current.postedSerial)
        result.status = ChannelRoutingStatus::selected;
    else if (selected.selectedSerial == current.postedSerial)
        result.status = ChannelRoutingStatus::selected;
    else if (selected.postedSerial == current.postedSerial)
        result.status = ChannelRoutingStatus::posted;
    else
        result.status = ChannelRoutingStatus::staleProjection;
    result.nativeSelected = result.status == ChannelRoutingStatus::selected;
    if (current.membership && current.membership->requested)
    {
        const auto phase = current.membership->readback.phase;
        if (phase == StoppedEffectPhase::failed)
            result.status = ChannelRoutingStatus::graphRefused;
        else if (!current.membership->acknowledged)
            result.status =
                ChannelRoutingStatus::posted; // Graph selection alone cannot complete the transaction.
    }
    return result;
}
void AudioEngine::serviceChannelRouting()
{
    (void)observeChannelRoutingSelection();
}
AudioEngine::ChannelRoutingReadback AudioEngine::observeChannelRoutingSelection()
{
    const auto *manager = juce::MessageManager::getInstanceWithoutCreating();
    if (!manager || !manager->isThisTheMessageThread())
    {
        ChannelRoutingReadback result;
        result.status = ChannelRoutingStatus::unavailable;
        return result;
    }
    const auto result = channelRoutingReadback();
    if (channelRouting && result.nativeSelected)
    {
        // selectedSerial is an actual consistent native observation. A newer
        // nativePostedSerial does not erase the selection already observed.
        // Subsequent >= readback is admitted only after this exact witness.
        channelRouting->selected = true;
        // This is the holder returned at publication. It can be empty or own
        // an earlier pending graph, rather than the immediately preceding DSP
        // graph. Cleaning it up is NOT a predecessor-reader retirement witness.
        channelRouting->pendingGraph.reset(); // CONTROL-only returned-holder cleanup.
    }
    serviceStoppedEffectMembership();
    return channelRoutingReadback(); // Reflect any actual final ACK or service failure.
}
bool AudioEngine::channelRoutingPostPending() const noexcept
{
    const bool effectsPending = channelRouting && channelRouting->membership &&
                                channelRouting->membership->requested &&
                                !channelRouting->membership->acknowledged;
    return effectsPending || (channelRouting && channelRouting->effective &&
                              (channelRouting->deviceInvalidated || !channelRouting->selected || !edit ||
                               channelRouting->effective->context.get() != channelRouting->context ||
                               edit->getTransport().getCurrentPlaybackContext() != channelRouting->context ||
                               projectedRate != engine.getDeviceManager().getSampleRate() ||
                               projectedBlock != engine.getDeviceManager().getBlockSize()));
}
bool AudioEngine::beforeDeviceReprepare(void *owner) noexcept
{
    auto *self = static_cast<AudioEngine *>(owner);
    if (!self || !self->channelRouting)
        return false;
    self->deviceEverActive.store(true, std::memory_order_release); // CONTROL lifetime witness, never reset by device loss.
    auto &state = *self->channelRouting;
    // CONTROL before native contextLock/release/reprepare. Returned native
    // holders may also pin an old initialized Runtime: retire those here too.
    // Their native destructor waits pool grace; selected/queued holders remain
    // independently owned by the actual native Player, never by this slot.
    state.pendingGraph.reset();
    for (auto *prepared : state.prepared)
    {
        prepared->owner = nullptr;
        prepared->clearNativeOwners();
    }
    state.prepared.clear();
    if (state.effective)
        state.deviceInvalidated = true; // Same-config restart also interrupts exact adoption lineage.
    if (state.generation != std::numeric_limits<std::uint64_t>::max())
        ++state.generation;
    // Preserve selected native graph and effective ownership while a genuine
    // device change is suspended. Reinitialising its private live helpers is
    // unsupported; explicit full canonical restoration must retire this Edit
    // before the exact owner schedules normal preparation again.
    const bool finiteRefused = self->refuseFiniteEffectDeviceReprepare();
    const bool productionRefused = self->invalidateProductionPluginDevice();
    return !state.effective && !finiteRefused && !productionRefused;
}
void AudioEngine::releaseChannelRoutingForEdit() noexcept
{
    if (!channelRouting)
        return;
    // Explicit full Edit replacement/destruction only, BEFORE Edit destruction.
    // Clear external private tickets while the owning Edit still exists. Do not
    // call fallible transport/ValueTree operations from this noexcept cleanup.
    // Active native Nodes pin their actual PluginPtrs, and the native context
    // pins its active overlay. Ordinary Edit destruction quiesces that context
    // before native cache/list/Track/Clip destruction; Stop alone is not proof.
    channelRouting->pendingGraph.reset();
    if (channelRouting->membership && !channelRouting->membership->acknowledged)
    {
        channelRouting->membership->readback.phase = StoppedEffectPhase::failed;
        channelRouting->membership->readback.error = "effects.contextLost";
    }
    channelRouting->membership.reset(); // Session independently pins any in-flight bank.
    for (auto *prepared : channelRouting->prepared)
    {
        prepared->owner = nullptr;
        prepared->clearNativeOwners();
    }
    channelRouting->prepared.clear();
    channelRouting->effective.reset();
    channelRouting->timelineOwner.reset();
    channelRouting->context = nullptr;
    channelRouting->postedSerial = 0;
    channelRouting->selected = false;
    channelRouting->deferredLatency = false;
    channelRouting->deviceInvalidated = false;
    if (channelRouting->generation != std::numeric_limits<std::uint64_t>::max())
        ++channelRouting->generation;
}
te::Plugin *AudioEngine::effectiveMixerGain(int insert) const noexcept
{
    if (!edit)
        return nullptr;
    if (channelRouting && channelRouting->effective)
    {
        const auto found = channelRouting->effective->gains.find(insert);
        return found == channelRouting->effective->gains.end() ? nullptr : found->second.get();
    }
    const te::PluginList *list = &edit->getMasterPluginList();
    if (insert != 0)
    {
        const auto found = projectedInserts.find(insert);
        if (found == projectedInserts.end())
            return nullptr;
        list = &found->second->pluginList;
    }
    for (auto *plugin : *list)
        if (dynamic_cast<AutomationGain *>(plugin))
            return plugin;
    return nullptr;
}
juce::Result AudioEngine::rebuildFullProjection(const Project &p, bool patternMode, Id pattern)
{
    if (initialSourceTransport) return juce::Result::fail("source.initialOnly: Full replacement is outside the initial transport experiment");
    const juce::ScopedValueSetter<bool> request(channelRouting->explicitFullReplacement, true);
    return rebuild(p, patternMode, pattern);
}

juce::Result AudioEngine::rebuildFullProjection(const Project &p, bool patternMode, Id pattern,
                                                Controller &owner, PreparedControllerChange &model,
                                                const std::function<juce::Result()> &finalAdmission)
{
    const auto *messages = juce::MessageManager::getInstanceWithoutCreating();
    if (!messages || !messages->isThisTheMessageThread() || routingController != &owner ||
        fullProjectionController || deferredDeviceFinalAdmission || initialSourceTransport ||
        finiteEffectBaseline)
        return juce::Result::fail(
            "routing.invalidFullProjectionOwner: Bound ordinary CONTROL transaction required");
    if (auto valid = owner.validatePreparedChange(model); valid.failed())
        return valid;
    if (finalAdmission)
        if (auto admitted = finalAdmission(); admitted.failed())
            return admitted;
    if (auto valid = owner.validatePreparedChange(model); valid.failed())
        return valid;
    const auto *destination = model.destinationBeforeCommit();
    if (!destination || !destination->state().isEquivalentTo(p.state()))
        return juce::Result::fail(
            "routing.fullProjectionDestinationChanged: Exact canonical destination required");
    if (patternMode && !p.pattern(pattern))
        return juce::Result::fail("Selected Pattern is unavailable");
    // Commit swaps the ticket's destination. Never let rebuild's Project
    // reference alias that slot across native/model publication.
    const Project target = p;
    const juce::ScopedValueSetter<Controller *> ownerScope(fullProjectionController, &owner);
    const juce::ScopedValueSetter<PreparedControllerChange *> modelScope(fullProjectionChange, &model);
    const juce::ScopedValueSetter<const std::function<juce::Result()> *> admissionScope(
        fullProjectionAdmission, &finalAdmission);
    if (!engine.getDeviceManager().deviceManager.getCurrentAudioDevice())
    {
        for (const auto &row : target.mixer)
            for (const auto &effect : row.effects)
            {
                const auto *previous = owner.project.plugin(effect.id);
                const auto oldDetector =
                    previous ? previous->detector : std::optional<PluginDetectorAssignment>{};
                if (effect.detector != oldDetector)
                    return juce::Result::fail(
                        "sidechain.deviceRequired: Prepare an audio device before changing detector "
                        "assignments; native layout negotiation is required");
            }
        std::unique_ptr<PreparedDeferredRouting> deferred;
        if (auto ready = prepareDeferredRouting(target, deferred); ready.failed())
            return ready;
        if (auto ready = prepareDeferredNativeHosts(target, *deferred); ready.failed())
            return ready;
        if (finalAdmission)
            if (auto admitted = finalAdmission(); admitted.failed())
                return admitted;
        if (auto valid = owner.validatePreparedChange(model); valid.failed())
            return valid;
        if (!canDeferChannelRouting())
            return juce::Result::fail(
                "routing.fullProjectionDeviceChanged: Initial no-device admission changed");
        if (!owner.commitPreparedChange(model))
            return juce::Result::fail("routing.fullProjectionCommitRefused: Canonical source retained");
        retireNoDeviceProjection(*deferred);
        publishDeferredNativeHosts(*deferred);
        return juce::Result::ok(); // Graph deferred; genuine native controls/state/editor now available.
    }
    const auto *incumbent = edit.get();
    const bool wasPlaying = playing();
    const auto resumeIncumbent = [&]() noexcept
    {
        try
        {
            if (!model.wasCommitted() && wasPlaying && edit.get() == incumbent && edit && !playing())
                edit->getTransport().play(false);
            return true;
        }
        catch (...)
        {
            return false;
        }
    };
    try
    {
        const auto result = rebuildFullProjection(target, patternMode, pattern);
        if (!resumeIncumbent())
            return juce::Result::fail(
                "routing.incumbentResumeFailed: Canonical source retained; restore playback");
        return result;
    }
    catch (...)
    {
        if (!resumeIncumbent())
            return juce::Result::fail(
                "routing.incumbentResumeFailed: Canonical source retained; restore playback");
        return juce::Result::fail(
            model.wasCommitted()
                ? "routing.fullProjectionActivationFailed: Destination committed; inspect audio restoration"
                : "routing.fullProjectionPreparationFailed: Canonical source retained");
    }
}

juce::Result AudioEngine::retryDevicePreparation()
{
    const auto *messageManager = juce::MessageManager::getInstanceWithoutCreating();
    if (!messageManager || !messageManager->isThisTheMessageThread())
        return juce::Result::fail("device.wrongThread: Recovery requires CONTROL");
    auto &manager = engine.getDeviceManager();
    const auto staged = manager.getPreparationDeviceConfiguration();
    if (!hasProjection || !edit || channelRouting->effective || projectedRate != staged.sampleRate ||
        projectedBlock != staged.blockSize || projectedRate != manager.getSampleRate() ||
        projectedBlock != manager.getBlockSize())
        return juce::Result::fail("device.restoreRequired: Restore the canonical full projection first");
    if (!manager.retryPreparationFence(this))
        return juce::Result::fail("device.retryUnavailable: No owned refused preparation is ready to retry");
    // Acknowledges queueing ONLY. Actual status/callback evidence is separate.
    return juce::Result::ok();
}

AudioEngine::ChannelRoutingOutcome AudioEngine::ensureChannelRoutingContext(const Project &source,
                                                                            bool patternMode, Id pattern)
{
    using Status = ChannelRoutingStatus;
    const auto *manager = juce::MessageManager::getInstanceWithoutCreating();
    if (!manager || !manager->isThisTheMessageThread())
        return {Status::unavailable, "routing.wrongThread: Native preparation requires CONTROL"};
    if (!edit || !hasProjection || projectedPatternMode != patternMode || projectedPattern != pattern)
        return {Status::staleProjection,
                "routing.staleProjection: Project mode/Pattern must be projected first"};
    auto &transport = edit->getTransport();
    const bool absent = transport.getCurrentPlaybackContext() == nullptr;
    try
    {
        const auto &devices = engine.getDeviceManager();
        if (!devices.deviceManager.getCurrentAudioDevice() || devices.getSampleRate() != projectedRate ||
            devices.getBlockSize() != projectedBlock)
            return {Status::deviceChanged, "routing.deviceChanged: Full device restoration required"};
        if (!source.state().isEquivalentTo(projectedProject.state()))
        {
            // Save can refresh canonical opaque bytes without a musical revision
            // or DSP change. Retain the exact graph/workers and reconcile ONLY
            // acknowledged captured metadata; never ignore other source drift.
            if (capture || recoveryPlugin || !channelRouting->prepared.empty() || channelRoutingPostPending())
                return {Status::busy, "routing.busy: Finish native preparation before captured metadata reconciliation"};
            auto acknowledged = projectedProject;
            const auto reconcile = [&](PluginSpec& plugin)
            {
                const auto* requested = source.plugin(plugin.id);
                if (!requested || requested->descriptor != plugin.descriptor) return false;
                if (requested->state == plugin.state) return true;
                const auto host = currentHosts.find(plugin.id);
                const auto captured = lastCapturedRevisions.find(plugin.id);
                if (host == currentHosts.end() || !host->second || !host->second->alive() ||
                    captured == lastCapturedRevisions.end() || captured->second.host != host->second ||
                    requested->state != host->second->recoveryState()) return false;
                plugin.state = requested->state;
                return true;
            };
            bool capturedOnly = true;
            for (auto& channel : acknowledged.channels)
                if (channel.instrument) capturedOnly &= reconcile(*channel.instrument);
            for (auto& insert : acknowledged.mixer)
                for (auto& plugin : insert.effects) capturedOnly &= reconcile(plugin);
            if (!capturedOnly || !source.state().isEquivalentTo(acknowledged.state()))
                return {Status::staleProjection,
                        "routing.staleProjection: Only exact owned captured metadata may reconcile without projection"};
            using std::swap;
            swap(projectedProject, acknowledged); // CONTROL metadata only; no graph, timeline or worker mutation.
        }
        // Settle the Edit's already-requested native rebuild BEFORE publishing a
        // private route. Otherwise its delayed timer can supersede the route's
        // exact selection receipt (notably immediately after loading a sample).
        // This existing native service runs on CONTROL, with no message pump.
        edit->dispatchPendingUpdatesSynchronously();
        auto *context = transport.getCurrentPlaybackContext();
        if (context && context->isPlaybackGraphAllocated())
            return {Status::ready, {}};
        if (transport.isPlaying() || channelRouting->effective || capture || recoveryPlugin)
            return {Status::busy, "routing.busy: Cold routing requires a stopped ordinary projection"};
        const auto preservedPosition = transport.getPosition();
        // Native instance preparation only: do not Play/Stop/seek, initialise the
        // global device, or request unconditional graph reallocation.
        transport.ensureContextAllocated(false);
        context = transport.getCurrentPlaybackContext();
        if (context && context->isPlaybackGraphAllocated() && !transport.isPlaying())
        {
            // A fresh native tempo snapshot starts at beat zero. Its first
            // reference update remaps that snapshot before processing a queued
            // position. Repost the existing stopped cursor, as native playback
            // setup does, without seeking the transport or starting playback.
            context->postPosition(preservedPosition);
            return {Status::ready, {}};
        }
    }
    catch (...)
    {
        // Only a context created by this attempt is owned for failure cleanup.
        if (absent)
            try
            {
                transport.freePlaybackContext();
            }
            catch (...)
            {
            }
        return {Status::preparationFailed, "routing.contextPreparationFailed: Canonical document unchanged"};
    }
    if (absent)
        transport.freePlaybackContext();
    return {Status::unavailable, "routing.unavailable: No allocated native graph; no device forced open"};
}

AudioEngine::ChannelRoutingOutcome
AudioEngine::prepareChannelRouting(const Project &destination,
                                   std::unique_ptr<PreparedChannelRouting> &output)
{
    if (initialSourceTransport) return {ChannelRoutingStatus::unsupportedDelta, "source.initialOnly: Routing/PDC needs separate source membership qualification"};
    return prepareGraphProjection(destination, nullptr, output);
}

AudioEngine::ChannelRoutingOutcome AudioEngine::prepareStoppedEffectChange(
    const Project &capturedSource, const Project &destination, int insert,
    const StoppedEffectWorkers::Ptr &workers, std::unique_ptr<PreparedChannelRouting> &output)
{
    return prepareStoppedEffectChangeImpl(capturedSource, destination, insert, workers, output, false);
}

AudioEngine::ChannelRoutingOutcome AudioEngine::prepareStoppedEffectChange(
    Controller &owner, PreparedControllerChange &model, const Project &capturedSource, int insert,
    const StoppedEffectWorkers::Ptr &workers, std::unique_ptr<PreparedChannelRouting> &output)
{
    const auto *messages = juce::MessageManager::getInstanceWithoutCreating();
    if (!messages || !messages->isThisTheMessageThread() || routingController != &owner ||
        owner.validatePreparedChange(model).failed())
        return {ChannelRoutingStatus::unsupportedDelta,
                "effects.invalidHistoryWitness: Same bound CONTROL transaction required"};
    const auto witness = model.nativeEffectHistoryBeforeCommit();
    if (!witness || witness->insert != insert || !witness->capturedSource || !witness->destination ||
        witness->destination != model.destinationBeforeCommit() ||
        !witness->capturedSource->state().isEquivalentTo(capturedSource.state()))
        return {ChannelRoutingStatus::unsupportedDelta,
                "effects.invalidHistoryWitness: Exact owned capture/history edge required"};
    auto result =
        prepareStoppedEffectChangeImpl(capturedSource, *witness->destination, insert, workers, output, true);
    if (result.status == ChannelRoutingStatus::ready && output)
    {
        output->impl->capturedHistoryModel = &model;
        output->impl->capturedHistoryInsert = insert;
    }
    return result;
}

bool AudioEngine::stoppedEffectHistoryWitnessCurrent(Controller &owner, PreparedControllerChange &model,
                                                     const PreparedChannelRouting &candidate) const
{
    if (!candidate.impl)
        return false;
    const auto &ready = *candidate.impl;
    if (!ready.capturedHistoryModel)
        return true; // Legacy API retains its stricter removed-state check.
    if (routingController != &owner || ready.capturedHistoryModel != &model ||
        owner.validatePreparedChange(model).failed())
        return false;
    const auto witness = model.nativeEffectHistoryBeforeCommit();
    return witness && witness->insert == ready.capturedHistoryInsert && witness->capturedSource &&
           witness->destination && witness->destination == model.destinationBeforeCommit() &&
           witness->capturedSource->state().isEquivalentTo(ready.capturedSource.state()) &&
           witness->destination->state().isEquivalentTo(ready.destination.state());
}

AudioEngine::ChannelRoutingOutcome
AudioEngine::prepareStoppedEffectChangeImpl(const Project &capturedSource, const Project &destination,
                                            int insert, const StoppedEffectWorkers::Ptr &workers,
                                            std::unique_ptr<PreparedChannelRouting> &output,
                                            bool historyWitness)
{
    if (initialSourceTransport)
        return {ChannelRoutingStatus::unsupportedDelta,
                "source.initialOnly: Effect membership changes are outside the initial transport experiment"};
    using S = ChannelRoutingStatus;
    // Master has a different output-stage owner from ordinary inserts. Use the
    // canonical full structural path rather than borrowing an insert's stage.
    if (insert == 0)
        return {S::unsupportedDelta,
                "effects.masterFullRebuildRequired: Master changes use the canonical structural path"};
    try
    {
        const auto *manager = juce::MessageManager::getInstanceWithoutCreating();
        if (!manager || !manager->isThisTheMessageThread() || playing())
            return {S::busy, "effects.stoppedControlRequired: Stop transport and use CONTROL"};
        StoppedEffectDelta delta;
        if (auto result = planStoppedEffectChange(capturedSource, destination, insert, delta.plan);
            result.failed())
            return {S::unsupportedDelta, result.getErrorMessage()};
        const auto hasNative = [](const Project &project)
        {
            for (const auto &track : project.mixer)
                for (const auto &effect : track.effects)
                {
                    const auto xml = juce::XmlDocument::parse(effect.descriptor);
                    juce::PluginDescription type;
                    if (xml && type.loadFromXml(*xml) && effects::isNativeDescription(type))
                        return true;
                }
            return false;
        };
        if (hasNative(capturedSource) || hasNative(destination))
            return {
                S::unsupportedDelta,
                "effects.nativeFullRebuildRequired: Native effect graphs use the canonical structural path"};
        delta.capturedSource = capturedSource;
        delta.workers = workers;
        if (delta.plan.appending)
        {
            PreparedPluginTransport::Ptr plan;
            if (auto admitted = stoppedEffectWorkerTransport(plan); admitted.failed())
                return {S::workerChanged, admitted.getErrorMessage()};
            if (!workers || workers->specs != delta.plan.added || workers->rate != projectedRate ||
                workers->block != projectedBlock || workers->hosts.size() != delta.plan.added.size())
                return {S::workerChanged, "effects.invalidWorkers: Exact private restored workers required"};
            if (workers->transportPlan != plan)
                return {S::workerChanged, "effects.transportChanged: Exact current session plan required"};
            for (const auto &spec : workers->specs)
            {
                const auto found = workers->hosts.find(spec.id);
                if (found == workers->hosts.end() || !found->second ||
                    !found->second->matchesPrepared(spec, projectedRate, projectedBlock, plan))
                    return {S::workerChanged, "effects.transportChanged: Private worker plan changed"};
            }
            for (const auto &[id, host] : workers->hosts)
                if (!host || currentHosts.contains(id) || !host->alive() ||
                    std::any_of(currentHosts.begin(), currentHosts.end(),
                        [&](const auto &entry) { return entry.second == host; }))
                    return {S::workerChanged, "effects.workerChanged: Fresh independent worker required"};
            if (workers->claimed.exchange(true))
                return {S::workerChanged, "effects.workersConsumed: Start fresh workers after prior graph admission"};
        }
        else if (workers)
            return {S::unsupportedDelta, "effects.unexpectedWorkers: Removal inverse accepts no new workers"};
        else
            for (const auto &spec : delta.plan.removed)
            {
                const auto *projected = projectedProject.plugin(spec.id);
                if (!projected || (!historyWitness && spec.state != projected->state))
                    return {S::unsupportedDelta, "effects.nativeHistoryReconciliationRequired: Newer removed state must enter the Redo snapshot first"};
            }
        return prepareGraphProjection(destination, &delta, output);
    }
    catch (...) { return {S::preparationFailed, "effects.preparationFailed: Incumbent graph/model retained"}; }
}

AudioEngine::ChannelRoutingOutcome AudioEngine::prepareGraphProjection(const Project &destination,
    const StoppedEffectDelta *effectChange, std::unique_ptr<PreparedChannelRouting> &output,
    std::shared_ptr<InitialSourceRoutePermit> sourcePermit)
{
    using S = ChannelRoutingStatus;
    const auto fail = [](S status, const juce::String &message)
    { return ChannelRoutingOutcome{status, message}; };
    try
    {
        serviceChannelRouting();
        if (initialSourceTransport && !sourcePermit)
            return fail(S::unsupportedDelta,"source.initialOnly: Explicit canonical source exchange required");
        if (sourcePermit)
            if (auto validSource=validateInitialSourceRoute(*sourcePermit); validSource.failed())
                return fail(S::unsupportedDelta,validSource.getErrorMessage());
        if (!edit || !timeline || !hasProjection)
            return fail(S::unavailable, "routing.unavailable: Prepare a project first");
        if (compensationGraph)
            return fail(S::unsupportedCompensation, "routing.compensationActive: Experimental compensation "
                                                    "needs an explicitly qualified joint plan");
        if (capture || recoveryPlugin)
            return fail(S::busy, "routing.busy: Finish native capture/recovery before routing preparation");
        if (channelRouting->effective)
        {
            const auto state = channelRoutingReadback().status;
            if (state == S::deviceChanged)
                return fail(state, "routing.deviceChanged: Explicit full projection restoration is required");
            if (state == S::staleContext || state == S::staleProjection)
                return fail(state, "routing.staleContext: Explicit full projection restoration is required");
            if (state == S::readbackUnavailable)
                return fail(state, "routing.readbackUnavailable: Retry actual native selection readback");
            if (channelRoutingPostPending())
                return fail(S::busy,
                            "routing.pendingAdoption: Await the current posted graph's actual selection");
        }
        if (channelRouting->generation == std::numeric_limits<std::uint64_t>::max() ||
            timelineGeneration == std::numeric_limits<std::uint64_t>::max() ||
            updates == std::numeric_limits<std::uint64_t>::max())
            return fail(S::serialExhausted, "routing.serialExhausted: A new projection is required");
        const auto valid = destination.validate();
        if (valid.failed())
            return fail(S::invalidProject, valid.getErrorMessage());
        Project normalized = destination;
        if (!MixerNativeIdentity::alignDisplayOrder(normalized, projectedProject))
            return fail(S::unsupportedDelta,
                        "routing.unsupportedDelta: Mixer ID universe must remain exact");
        std::map<int, te::AudioTrack *> verifiedInserts;
        const auto bindings = prepareMixerNativeBindings(*edit, projectedProject, verifiedInserts);
        if (bindings.failed() || verifiedInserts != projectedInserts)
            return fail(S::staleProjection,
                        "routing.staleProjection: Native Mixer identity bindings changed");
        if (normalized.channels.size() != projectedProject.channels.size())
            return fail(S::unsupportedDelta,
                        "routing.unsupportedDelta: Channel identities must remain exact");
        bool changed = false;
        if (effectChange)
        {
            // Opaque capture may refresh metadata without changing any live native
            // owner. Compare every other source field against the actual projection.
            auto source = effectChange->capturedSource;
            for (const auto &[id, host] : currentHosts)
            {
                const auto *native = projectedProject.plugin(id);
                auto *captured = source.plugin(id);
                const auto revision = capturedPluginRevision(id, host);
                if (!native || !captured || native->descriptor != captured->descriptor || !host ||
                    !host->alive() || !revision || *revision != host->parameterRevision() ||
                    captured->state != host->recoveryState() || host->editorBusy() ||
                    host->editorWindowState().exists || host->pendingParameterRequests())
                    return fail(S::workerChanged, "effects.captureRequired: Exact fresh closed-editor native baseline required");
                captured->state = native->state;
            }
            if (!source.state().isEquivalentTo(projectedProject.state()))
                return fail(S::staleProjection, "effects.staleSource: Captured source changed");
            // Normalize only the explicitly planned chain and fresh opaque metadata.
            auto *target = const_cast<MixerInsert *>(mixerInsert(normalized, effectChange->plan.insert));
            const auto *old = mixerInsert(projectedProject, effectChange->plan.insert);
            if (!target || !old) return fail(S::unsupportedDelta, "effects.unavailableInsert: Native target missing");
            target->effects = old->effects; target->effectsBypassed = old->effectsBypassed;
            normalized.nextId = projectedProject.nextId;
            for (const auto &[id, host] : currentHosts)
                if (auto *value = normalized.plugin(id)) value->state = projectedProject.plugin(id)->state;
            changed = true;
        }
        for (auto &channel : normalized.channels)
        {
            const auto *source = projectedProject.channel(channel.id);
            if (!source)
                return fail(S::unsupportedDelta, "routing.unsupportedDelta: Channel identity changed");
            changed |= channel.mixer != source->mixer;
            channel.mixer = source->mixer;
        }
        if (!normalized.state().isEquivalentTo(projectedProject.state()))
            return fail(S::unsupportedDelta,
                        "routing.unsupportedDelta: Only existing Channel-to-Mixer destinations may change");
        if (!changed)
            return fail(S::noOp, "routing.noOp: Destination already matches");
        auto *context = edit->getTransport().getCurrentPlaybackContext();
        if (!context || !context->isPlaybackGraphAllocated())
            return fail(S::unavailable, "routing.unavailable: Existing allocated native context required");
        if (projectedRate <= 0 || projectedBlock <= 0 ||
            projectedRate != engine.getDeviceManager().getSampleRate() ||
            projectedBlock != engine.getDeviceManager().getBlockSize())
            return fail(S::deviceChanged,
                        "routing.deviceChanged: Device projection must be rebuilt explicitly");
        auto ticket = std::unique_ptr<PreparedChannelRouting>(new PreparedChannelRouting());
        auto &ready = *ticket->impl;
        ready.previousMembership = channelRouting->membership; // Retire predecessor outside post/commit.
        ready.edit = edit.get();
        ready.context = context;
        ready.generation = channelRouting->generation;
        ready.rate = projectedRate;
        ready.block = projectedBlock;
        ready.patternMode = projectedPatternMode;
        ready.pattern = projectedPattern;
        ready.sourceProject = projectedProject;
        ready.destination = destination;
        ready.sourceRoute = std::move(sourcePermit);
        ready.documentGeneration = timelineGeneration + 1;
        ready.nativeState = edit->state.createCopy();
        ready.nativeState.getChildWithName(te::IDs::TRANSPORT).removeProperty(te::IDs::position, nullptr);
        ready.channels = projectedChannels;
        ready.effects = effectPlugins;
        if (effectChange)
        {
            ready.stoppedEffectChange = true;
            ready.capturedSource = effectChange->capturedSource;
            ready.newEffectWorkers = effectChange->workers;
            ready.destinationHosts = currentHosts;
            ready.destinationErrors = pluginErrors;
            ready.destinationObserved = observedParameterRevisions;
            for (const auto &spec : effectChange->plan.removed)
            {
                ready.destinationHosts.erase(spec.id); ready.destinationErrors.erase(spec.id);
                ready.destinationObserved.erase(spec.id); ready.effects.erase(spec.id);
            }
            if (effectChange->workers)
            {
                ownedHosts.reserve(ownedHosts.size() + effectChange->workers->hosts.size());
                for (const auto &[id, host] : effectChange->workers->hosts)
                {
                    ready.destinationHosts.emplace(id, host);
                    ready.destinationObserved.emplace(id, Revision{host, host->parameterRevision()});
                    ready.addedWorkers.push_back({id, host, host->runtimeIdentity(), host->busLayout(),
                        host->processingQuantum(), host->latencySamples(), host->declaredLatency().generation});
                    ready.addedWorkers.back().parameterRevision = host->parameterRevision();
                    ownedHosts.push_back(host); // Pre-pin; no last-owner retirement in DSP.
                }
            }
        }
        if (channelRouting->effective)
            ready.sourceLists = channelRouting->effective;
        else
        {
            auto baseline = std::make_shared<EffectiveRoutingLists>();
            baseline->edit = edit.get();
            baseline->context = context;
            baseline->scope = timeline;
            const auto addList = [&](te::PluginList &list)
            {
                auto &plugins = baseline->lists.emplace(&list, te::Plugin::Array{}).first->second;
                for (auto *plugin : list)
                {
                    if (!plugin->itemID.isValid() ||
                        !baseline->nativeIds.emplace(plugin->itemID.getRawID(), te::Plugin::Ptr(plugin))
                             .second)
                        throw std::invalid_argument(
                            "routing.duplicateOwner: Native IDs must be unique across lists");
                    plugins.add(plugin);
                }
            };
            addList(edit->getMasterPluginList());
            for (auto *track : te::getAudioTracks(*edit))
            {
                addList(track->pluginList);
                for (auto *clip : track->getClips())
                    if (auto *list = clip->getPluginList())
                    {
                        if (!list->getPlugins().isEmpty())
                            throw std::invalid_argument("routing.unsupportedClipPlugins: Active clip effects "
                                                        "need separate retained admission");
                        addList(*list);
                    }
            }
            baseline->helpers = edit->getPluginCache().getPlugins();
            baseline->helpers.addIfNotAlreadyThere(edit->getMasterVolumePlugin().get());
            ready.sourceLists = std::move(baseline);
        }
        if (ready.sourceLists->scope != timeline)
            return fail(S::staleProjection, "routing.staleProjection: Effective timeline alias diverged");
        if (!channelRouting->timelineOwner)
        {
            ready.newTimelineOwner = std::make_unique<TimelineProjection>(timeline);
            ready.timelineOwner = ready.newTimelineOwner.get();
        }
        else
            ready.timelineOwner = channelRouting->timelineOwner.get();
        if (ready.timelineOwner->currentTimeline() != timeline)
            return fail(S::staleProjection, "routing.staleProjection: Timeline authority diverged");
        auto document = compileLiveDocument(destination, projectedPatternMode, projectedPattern,
                                            ready.documentGeneration);
        if (!document)
            return fail(S::invalidProject, "routing.documentFailed: Cannot compile destination");
        std::vector<SamplerCutInput> cuts;
        for (const auto &[id, projected] : projectedChannels)
            if (auto *sampler = dynamic_cast<FixedSampler *>(projected.sampler.get()))
                sampler->appendCutInputs(cuts);
        bindLiveCutInputs(*document, cuts);
        const auto native =
            prepareNativeAutomationRoutes(destination, *document,
                effectChange ? ready.destinationHosts : currentHosts, projectedRate, true);
        if (native.failed())
            return fail(S::workerChanged, native.getErrorMessage());
        ready.timelineTicket = ready.timelineOwner->prepare(document);
        if (ready.timelineTicket.preparationStatus() != TimelineProjection::Status::ready)
            return fail(S::preparationFailed, "routing.timelineFailed: Private timeline admission refused");
        ready.timelineAlias = ready.timelineTicket.preparedTimeline();
        auto candidate = std::make_shared<EffectiveRoutingLists>();
        candidate->edit = edit.get();
        candidate->context = context;
        candidate->scope = ready.timelineAlias;
        candidate->helpers = ready.sourceLists->helpers;
        const auto retain = [](const juce::Result &result)
        {
            if (result.failed())
                throw std::invalid_argument(result.getErrorMessage().toStdString());
        };
        // CONTROL native initialization count plus explicit Runtime ownership,
        // never cache membership or shared_ptr::use_count. An empty original
        // bus has no DSP to preserve. An inactive shell with another live owner
        // must NOT start a fresh smoother/Host packet alongside that old owner.
        const auto retainRuntime = [&](auto &value, const auto &source)
        {
            if (!source.baseClassNeedsInitialising())
                retain(value.shareCompatibleRuntimeFrom(source));
            else if (source.runtimeAttachmentsControl() != 0)
                throw std::invalid_argument("routing.inactiveSharedOwner: Current dormant shell still has an "
                                            "active predecessor Runtime");
        };
        const auto specHost = [&](Id id) -> std::shared_ptr<PluginHost>
        {
            const auto &hosts = effectChange ? ready.destinationHosts : currentHosts;
            const auto found = hosts.find(id);
            return found == hosts.end() ? nullptr : found->second;
        };
        for (const auto &[list, plugins] : ready.sourceLists->lists)
        {
            Id channelId = 0;
            int insert = -1;
            if (list == &edit->getMasterPluginList())
                insert = 0;
            for (const auto &[id, projected] : projectedChannels)
                if (list == &projected.track->pluginList)
                    channelId = id;
            for (const auto &[id, track] : projectedInserts)
                if (list == &track->pluginList)
                    insert = id;
            const bool effectTarget = effectChange &&
                (effectChange->plan.insert == 0 ? list == &edit->getMasterPluginList()
                    : projectedInserts.contains(effectChange->plan.insert) &&
                      list == &projectedInserts.at(effectChange->plan.insert)->pluginList);
            bool addedEffects = false;
            const RemoteEffect *freshJoinSource = nullptr;
            if (effectTarget && effectChange->plan.appending)
                for (auto *plugin : plugins)
                    if (auto *source = dynamic_cast<RemoteEffect *>(plugin);
                        source && source->isRemovalShell())
                    {
                        if (freshJoinSource || effectChange->plan.added.size() != 1 ||
                            effectChange->plan.added.front().id != source->phasePairPluginId())
                            throw std::invalid_argument("effects.retainedDryJoinRequired: Prepare an exact "
                                                        "retained-slot replacement");
                        freshJoinSource = source;
                    }
            te::Plugin::Array prepared;
            for (auto *plugin : plugins)
            {
                if (effectTarget && effectChange->plan.appending && dynamic_cast<AutomationGain *>(plugin))
                {
                    if (addedEffects) throw std::invalid_argument("effects.duplicateAnchor: Duplicate fader owner");
                    addedEffects = true;
                    const auto *chain = mixerInsert(destination, effectChange->plan.insert);
                    for (const auto &spec : effectChange->plan.added)
                    {
                        juce::ValueTree state(te::IDs::PLUGIN);
                        state.setProperty(te::IDs::type, RemoteEffect::xmlTypeName, nullptr);
                        edit->createNewItemID().writeID(state, nullptr); // Private tree; no list/cache insertion.
                        auto remote = RemoteEffect::createDetachedGraph(*edit, state);
                        auto host = specHost(spec.id);
                        auto *value = dynamic_cast<RemoteEffect *>(remote.get());
                        value->configure(host, spec.bypass || chain->effectsBypassed, ready.timelineAlias,
                                         spec.id, spec.wet);
                        if (productionPluginProjection && productionPluginProjection->ports)
                        {
                            const auto history = host ? host->prepareDryHistoryRetention() : std::nullopt;
                            if (!history)
                                throw std::invalid_argument(
                                    "effects.historyUnavailable: Actual prepared Host history required");
                            retain(value->prepareOutputObservation(history->history, history->identity,
                                                                   history->writer));
                        }
                        if (freshJoinSource)
                        {
                            retain(value->prepareFreshJoinFrom(*freshJoinSource));
                            ready.freshJoin = value->freshJoinProgressControl();
                        }
                        if (!candidate->nativeIds.emplace(remote->itemID.getRawID(), remote).second)
                            throw std::invalid_argument("effects.duplicateOwner: Native ID collision");
                        ready.effects.emplace(spec.id, remote);
                        prepared.add(remote);
                    }
                }
                te::Plugin::Ptr replacement;
                auto state = plugin->state.createCopy();
                if (auto *source = dynamic_cast<FixedSampler *>(plugin))
                {
                    const auto *model = destination.channel(channelId);
                    if (!model)
                        throw std::invalid_argument("routing.samplerOwner: Missing Channel owner");
                    replacement = FixedSampler::createDetachedGraph(*edit, state);
                    auto *value = dynamic_cast<FixedSampler *>(replacement.get());
                    value->setTimeline(ready.timelineAlias, channelId);
                    retain(source->baseClassNeedsInitialising() && source->runtimeAttachmentsControl() == 0
                               ? value->shareDormantSampleRuntimeFrom(*source)
                               : value->shareCompatibleRuntimeFrom(*source));
                    ready.channels.at(channelId).sampler = replacement;
                }
                else if (auto *source = dynamic_cast<ChannelBalance *>(plugin))
                {
                    const auto *model = destination.channel(channelId);
                    if (!model)
                        throw std::invalid_argument("routing.balanceOwner: Missing Channel owner");
                    replacement = ChannelBalance::createDetachedGraph(*edit, state);
                    auto *value = dynamic_cast<ChannelBalance *>(replacement.get());
                    value->configure(ready.timelineAlias, *model,
                                     projectedChannels.at(channelId).track->getVolumePlugin()->getPanLaw());
                    retainRuntime(*value, *source);
                }
                else if (auto *source = dynamic_cast<RemoteEffect *>(plugin))
                {
                    if (source == freshJoinSource)
                        continue; // Replace this exact slot; never append a second retained delay.
                    const auto id = source->phasePairPluginId();
                    const auto *model = destination.plugin(id);
                    if (!model && source->isRemovalShell())
                    {
                        replacement = RemoteEffect::createDetachedGraph(*edit, state);
                        retain(dynamic_cast<RemoteEffect *>(replacement.get())
                                   ->shareDryRemovalFrom(*source, ready.timelineAlias));
                    }
                    else if (!model && effectTarget && !effectChange->plan.appending &&
                             std::any_of(effectChange->plan.removed.begin(), effectChange->plan.removed.end(),
                                         [&](const auto &spec) { return spec.id == id; }))
                    {
                        if (ready.removalShell)
                            throw std::invalid_argument("effects.multipleRemovalPending: Joint chain removal "
                                                        "requires a prepared multi-owner handoff");
                        replacement = RemoteEffect::createDetachedGraph(*edit, state);
                        auto *value = dynamic_cast<RemoteEffect *>(replacement.get());
                        retain(value->configureRemovalFrom(*source, ready.timelineAlias));
                        ready.removalShell = replacement;
                        ready.retiredWetShells.add(source);
                        // Initial public PluginLists/cache can retain the same
                        // obsolete wet owner after several detached graph clones.
                        // Pin only exact Runtime/progress/Host matches for later
                        // CONTROL release under the same actual final-close proof.
                        for (auto *helper : ready.sourceLists->helpers)
                            if (auto *wet = dynamic_cast<RemoteEffect *>(helper);
                                wet && wet->retainsWetPredecessorControl(*value))
                                ready.retiredWetShells.addIfNotAlreadyThere(wet);
                        ready.removalProgress = value->outputProgressControl();
                    }
                    else if (!model)
                        throw std::invalid_argument("routing.effectOwner: Missing effect owner");
                    else
                    {
                        replacement = RemoteEffect::createDetachedGraph(*edit, state);
                        auto *value = dynamic_cast<RemoteEffect *>(replacement.get());
                        auto host = specHost(id);
                        value->configure(host, model->bypass, ready.timelineAlias, id, model->wet);
                        if (auto bus = source->getSidechainBusIDOverride())
                            retain(value->connectSidechain(source->getSidechainSourceID(), *bus));
                        if (host)
                            retainRuntime(*value, *source);
                        ready.effects.at(id) = replacement;
                    }
                }
                else if (auto *source = dynamic_cast<AutomationGain *>(plugin))
                {
                    const auto *model = mixerInsert(destination, insert);
                    if (!model)
                        throw std::invalid_argument("routing.gainOwner: Missing Mixer owner");
                    const auto *volume = insert == 0 ? edit->getMasterVolumePlugin().get()
                                                     : projectedInserts.at(insert)->getVolumePlugin();
                    replacement = AutomationGain::createDetachedGraph(*edit, state);
                    auto *value = dynamic_cast<AutomationGain *>(replacement.get());
                    value->configure(ready.timelineAlias, *model, volume->getPanLaw());
                    retainRuntime(*value, *source);
                    candidate->gains[insert] = replacement;
                }
                else if (auto *source = dynamic_cast<MixerSendPlugin *>(plugin))
                {
                    const auto *route = mixerRoute(destination, insert, source->destination());
                    if (!route)
                        throw std::invalid_argument("routing.sendOwner: Existing edge missing");
                    replacement = MixerSendPlugin::createDetachedGraph(*edit, state);
                    auto *value = dynamic_cast<MixerSendPlugin *>(replacement.get());
                    value->configure(ready.timelineAlias, insert, source->destination(), source->busNumber,
                                     route->gain);
                    retainRuntime(*value, *source);
                }
                else if (auto *source = dynamic_cast<MixerMasterGate *>(plugin))
                {
                    const auto *route = mixerRoute(destination, insert, 0);
                    replacement = MixerMasterGate::createDetachedGraph(*edit, state);
                    auto *value = dynamic_cast<MixerMasterGate *>(replacement.get());
                    value->configure(ready.timelineAlias, insert, route ? route->gain : 0);
                    retainRuntime(*value, *source);
                }
                else if (auto *source = dynamic_cast<GuidePlugin *>(plugin))
                {
                    replacement = GuidePlugin::createDetachedGraph(*edit, state);
                    auto *value = dynamic_cast<GuidePlugin *>(replacement.get());
                    value->configure(guide, ready.timelineAlias);
                    retainRuntime(*value, *source);
                }
                else if (auto *volume = dynamic_cast<te::VolumeAndPanPlugin *>(plugin);
                         volume && !volume->isEnabled())
                    replacement = plugin;
                else if (dynamic_cast<te::AuxReturnPlugin *>(plugin) ||
                         dynamic_cast<MixerSidechainSend *>(plugin) ||
                         (dynamic_cast<te::LevelMeterPlugin *>(plugin) &&
                          bool(edit->state["viberaOwnMetering"])))
                    replacement = plugin;
                else
                    throw std::invalid_argument(
                        "routing.unsupportedPlugin: Unknown active native processing owner");
                if (!candidate->nativeIds.emplace(replacement->itemID.getRawID(), replacement).second)
                    throw std::invalid_argument(
                        "routing.duplicateOwner: Candidate IDs duplicate across lists");
                prepared.add(replacement);
            }
            if (effectTarget && effectChange->plan.appending && !addedEffects)
                throw std::invalid_argument("effects.missingAnchor: No unique native Mixer fader owner");
            candidate->lists.emplace(list, std::move(prepared));
        }
        std::vector<te::GraphOutputRouting::Route> routes;
        for (auto *track : te::getAudioTracks(*edit))
        {
            const auto channel = std::find_if(projectedChannels.begin(), projectedChannels.end(),
                                              [track](const auto &v) { return v.second.track == track; });
            if (channel != projectedChannels.end())
            {
                const auto destinationIndex = destination.channel(channel->first)->mixer;
                if (destinationIndex == 0)
                    routes.push_back({track->itemID, te::GraphOutputRouting::Target::defaultWaveDevice, {}});
                else
                    routes.push_back({track->itemID, te::GraphOutputRouting::Target::destinationTrack,
                                      projectedInserts.at(destinationIndex)->itemID});
            }
            else
            {
                if (std::none_of(projectedInserts.begin(), projectedInserts.end(),
                                 [track](const auto &v) { return v.second == track; }))
                    throw std::invalid_argument("routing.unknownTrack: Fixed track universe changed");
                routes.push_back({track->itemID, te::GraphOutputRouting::Target::defaultWaveDevice, {}});
            }
        }
        auto overlay = std::make_shared<te::EditPlaybackContext::GraphOverlay>();
        overlay->outputRouting = te::GraphOutputRouting::prepare(*edit, std::move(routes));
        overlay->plugins = [candidate](const te::PluginList &list)
        {
            if (&list.getEdit() != candidate->edit)
                throw std::invalid_argument("routing.foreignList: Different Edit");
            const auto found = candidate->lists.find(&list);
            if (found == candidate->lists.end())
                throw std::invalid_argument(
                    "routing.unknownList: No registered-original fallback under an effective overlay");
            return found->second;
        };
        overlay->fixedInitialisationAdmission = [candidate](const te::Plugin &plugin)
        {
            const auto found = candidate->nativeIds.find(plugin.itemID.getRawID());
            if (found != candidate->nativeIds.end() && found->second.get() == &plugin)
                return true;
            if (candidate->helpers.contains(const_cast<te::Plugin *>(&plugin)) &&
                dynamic_cast<const te::VolumeAndPanPlugin *>(&plugin) && !plugin.isEnabled())
                return true;
            throw std::invalid_argument("routing.unownedPlugin: Exact candidate/helper pointer required");
        };
        // Leave lastStage empty: native context's independently owned final-stage
        // policy is preserved. Active experimental compensation was refused.
        ready.candidate = candidate;
        ready.overlay = overlay;
        for (const auto &[id, host] : currentHosts)
        {
            if (!host || !host->alive())
                return fail(S::workerChanged, "routing.workerChanged: Worker unavailable before preparation");
            ready.workers.push_back({id, host, host->runtimeIdentity(), host->busLayout(),
                                     host->processingQuantum(), host->latencySamples(),
                                     host->declaredLatency().generation});
        }
        auto sourceBuild = ready.sourceRoute
                               ? leaseInitialSourceRoute(*ready.sourceRoute, overlay->outputRouting)
                               : std::shared_ptr<void>{};
        std::unique_ptr<ordinary_sample_ports::Policy::ForwardLease> forwardBuild;
        std::unique_ptr<ordinary_sample_ports::Policy::RemovalLease> removalBuild;
        std::unique_ptr<ordinary_sample_ports::Policy::FreshJoinLease> freshJoinBuild;
        if (effectChange && productionPluginProjection && productionPluginProjection->ports)
        {
            const te::PluginList *list = effectChange->plan.insert == 0
                                             ? &edit->getMasterPluginList()
                                             : &projectedInserts.at(effectChange->plan.insert)->pluginList;
            const auto &plugins = ready.sourceLists->lists.at(list);
            te::Plugin *stage = nullptr;
            for (auto *plugin : plugins)
                if (dynamic_cast<MixerMasterGate *>(plugin))
                {
                    if (stage)
                        throw std::invalid_argument("effects.duplicateStage: Unique Mixer stage required");
                    stage = plugin;
                }
            if (!stage)
                throw std::invalid_argument("effects.missingStage: Actual Mixer output stage required");
            if (ready.freshJoin)
                freshJoinBuild = productionPluginProjection->ports->leaseFreshJoin(*stage, ready.freshJoin);
            else if (effectChange->plan.appending)
            {
                std::shared_ptr<RemoteEffect::RemovalProgress> wet;
                if (effectChange->plan.added.size() == 1)
                {
                    const auto &spec = effectChange->plan.added.front();
                    auto *value = dynamic_cast<RemoteEffect *>(ready.effects.at(spec.id).get());
                    wet = value->outputProgressControl();
                }
                forwardBuild = productionPluginProjection->ports->leaseForward(*stage, std::move(wet));
                ready.forward = forwardBuild->receipt();
            }
            else if (ready.removalProgress)
                removalBuild = productionPluginProjection->ports->leaseRemoval(*stage, ready.removalProgress);
        }
        ready.graph = context->prepareGraphOnly(overlay);
        forwardBuild.reset(); // CONTROL factory metadata only; graph/transaction pin real drain progress.
        removalBuild.reset();
        freshJoinBuild.reset();
        sourceBuild.reset();  // Ends only this synchronous candidate lease; authority unchanged.
        // Exact reserved-ticket scalar, not a racing bounded selection receipt.
        // Requires the separately guarded native publicationSerial successor.
        ready.publicationSerial = ready.graph ? ready.graph->publicationSerial() : 0;
        if (ready.publicationSerial == 0)
            return fail(S::graphRefused, "routing.graphRefused: Native ticket has no publication identity");
        // Registration can allocate but is STILL before publication. Destructor
        // unregisters only after owner is assigned and insertion succeeded.
        channelRouting->prepared.push_back(&ready);
        ready.owner = this;
        const auto admission =
            validateGraphProjection(*ticket, bool(ready.sourceRoute), effectChange != nullptr);
        if (admission.status != S::ready)
            return admission;
        output.swap(ticket);
        return {S::ready, {}};
    }
    catch (const tracktion::graph::LatencyNode::OutputStageError &error)
    {
        // Internal stopped-effect diagnostics only. Default routing/error text
        // stays unchanged, and the SAME native refusal remains a failure.
        if (!effectChange || !error.difference.known)
            return fail(S::preparationFailed, "routing.preparationFailed: " + juce::String(error.what()));
        const auto encode = [](const tracktion::graph::LatencyNode::OutputStageSnapshot &value)
        {
            auto object = std::make_unique<juce::DynamicObject>();
            object->setProperty("present", value.present);
            object->setProperty("channels", value.channels);
            object->setProperty("consumerExits", value.exits);
            object->setProperty("delayPresent", value.delayPresent);
            object->setProperty("requestedDelaySamples", value.requestedDelaySamples);
            object->setProperty("processorPresent", value.processorPresent);
            object->setProperty("processorDelaySamples", value.processorDelayKnown
                ? juce::var(value.processorDelaySamples) : juce::var());
            object->setProperty("producerPathLatencySamples", value.producerPathKnown
                ? juce::var(value.producerPathLatencySamples) : juce::var());
            object->setProperty("boundaryPathLatencySamples", value.boundaryPathKnown
                ? juce::var(value.boundaryPathLatencySamples) : juce::var());
            object->setProperty("producerNodeToken", value.producerNodeId != 0
                ? juce::var(juce::String::toHexString(juce::uint64(value.producerNodeId))) : juce::var());
            object->setProperty("boundaryNodeToken", value.boundaryNodeId != 0
                ? juce::var(juce::String::toHexString(juce::uint64(value.boundaryNodeId))) : juce::var());
            object->setProperty("pathIdentityKind", "native_node_id_not_project_id_or_pcm_provenance");
            object->setProperty("processorSampleRate", juce::var());
            object->setProperty("processorChannels", juce::var());
            object->setProperty("queuedFrames", juce::var());
            object->setProperty("queuedMidiEvents", juce::var());
            return juce::var(object.release());
        };
        const auto &difference = error.difference;
        auto receipt = std::make_unique<juce::DynamicObject>();
        receipt->setProperty("kind", "prepared_output_stage_difference");
        receipt->setProperty("version", 1);
        receipt->setProperty("firstFailureOnly", true);
        receipt->setProperty("ownerKind", "runtime_address_not_project_id");
        receipt->setProperty("ownerRuntimeToken", juce::String::toHexString(
            juce::uint64(reinterpret_cast<std::uintptr_t>(difference.runtimeOwner))));
        receipt->setProperty("before", encode(difference.before));
        receipt->setProperty("after", encode(difference.after));
        receipt->setProperty("processorConfigurationMatch", difference.processorConfigurationMatch < 0
            ? juce::var() : juce::var(difference.processorConfigurationMatch != 0));
        receipt->setProperty("sameLegacyProcessor", difference.sameLegacyProcessor);
        receipt->setProperty("unavailableInternals", "processor rate/channels and mutable FIFO/MIDI contents not exposed or read");
        return fail(S::preparationFailed, "routing.preparationFailed: " + juce::String(error.what()) +
                    " | stageDifference=" + juce::JSON::toString(juce::var(receipt.release()), true));
    }
    catch (const std::exception &error)
    {
        return fail(S::preparationFailed, "routing.preparationFailed: " + juce::String(error.what()));
    }
    catch (...)
    {
        return fail(S::preparationFailed,
                    "routing.preparationFailed: Native graph preparation failed; source retained");
    }
}

AudioEngine::ChannelRoutingOutcome AudioEngine::validateChannelRouting(PreparedChannelRouting &ticket) const
{
    return validateGraphProjection(ticket, false);
}
AudioEngine::ChannelRoutingOutcome AudioEngine::validateGraphProjection(PreparedChannelRouting &ticket,
                                                                        bool sourceAdmission,
                                                                        bool privateEffectPreparation) const
{
    using S = ChannelRoutingStatus;
    const auto fail = [](S s, const char *message) { return ChannelRoutingOutcome{s, message}; };
    try
    {
        if (ticket.impl)
            ticket.impl->validated = false;
        if (ticket.impl && !ticket.impl->owner)
            return fail(S::ownerGone, "routing.ownerGone: Prepared Edit ownership was explicitly released");
        if (!ticket.impl || ticket.impl->owner != this || ticket.impl->posted)
            return fail(S::staleProjection, "routing.staleProjection: Ticket is foreign or posted");
        const auto &ready = *ticket.impl;
        if (ready.sourceRoute)
        {
            if (!sourceAdmission)
                return fail(S::unsupportedDelta,
                            "source.canonicalAdmissionRequired: Validate the actual Controller ticket");
            if (auto valid = validateInitialSourceRoute(*ready.sourceRoute); valid.failed())
                return {S::unsupportedDelta, valid.getErrorMessage()};
        }
        if (ready.stoppedEffectChange && playing())
            return fail(S::busy, "effects.stoppedRequired: Transport started during preparation");
        if (channelRouting->deviceInvalidated)
            return fail(S::deviceChanged,
                        "routing.deviceChanged: Explicit full projection restoration required");
        if (ready.edit != edit.get() || ready.generation != channelRouting->generation ||
            ready.patternMode != projectedPatternMode || ready.pattern != projectedPattern ||
            !ready.sourceProject.state().isEquivalentTo(projectedProject.state()))
            return fail(S::staleProjection, "routing.staleProjection: Source changed during preparation");
        if (ready.sourceLists->context.get() != ready.context ||
            ready.context != edit->getTransport().getCurrentPlaybackContext())
            return fail(S::staleContext, "routing.staleContext: Playback context changed");
        if (ready.rate != engine.getDeviceManager().getSampleRate() ||
            ready.block != engine.getDeviceManager().getBlockSize())
            return fail(S::deviceChanged, "routing.deviceChanged: Rate/buffer changed");
        const bool ownMembership = ready.membership && channelRouting->membership == ready.membership &&
                                   ready.membership->requested && !ready.membership->readback.graphPosted;
        if (channelRoutingPostPending() && !ownMembership)
            return fail(S::busy, "routing.pendingAdoption: Previous graph not selected");
        if (compensationGraph || capture || recoveryPlugin)
            return fail(S::busy, "routing.busy: Capture/recovery/compensation owns native projection");
        if (ready.timelineOwner->validate(ready.timelineTicket) != TimelineProjection::Status::ready)
            return fail(S::staleProjection, "routing.staleTimeline: Source or candidate scope changed");
        if (channelRouting->effective && ready.sourceLists != channelRouting->effective)
            return fail(S::staleProjection, "routing.staleOwners: Effective source set changed");
        auto native = edit->state.createCopy();
        native.getChildWithName(te::IDs::TRANSPORT).removeProperty(te::IDs::position, nullptr);
        if (!native.isEquivalentTo(ready.nativeState))
            return fail(S::staleProjection, "routing.nativeChanged: Native tracks/lists/settings changed");
        if (ready.workers.size() != currentHosts.size())
            return fail(S::workerChanged, "routing.workerChanged: Host set changed");
        for (const auto &worker : ready.workers)
        {
            const auto found = currentHosts.find(worker.id);
            if (found == currentHosts.end() || found->second != worker.host || !worker.host->alive() ||
                worker.instance != worker.host->runtimeIdentity() ||
                worker.buses != worker.host->busLayout() ||
                worker.quantum != worker.host->processingQuantum() ||
                worker.latency != worker.host->latencySamples() ||
                worker.declaration != worker.host->declaredLatency().generation)
                return fail(S::workerChanged,
                            "routing.workerChanged: Native identity/configuration/declaration changed");
        }
        if (ready.stoppedEffectChange)
        {
            if (!currentProductionPluginContext(ready.context))
                return fail(S::workerChanged, "effects.transportChanged: Source session plan changed");
            for (const auto &source : ready.workers)
            {
                const auto *captured = ready.capturedSource.plugin(source.id);
                const auto revision = capturedPluginRevision(source.id, source.host);
                if (!captured || !revision || *revision != source.host->parameterRevision() ||
                    captured->state != source.host->recoveryState() || source.host->editorBusy() ||
                    source.host->editorWindowState().exists || source.host->pendingParameterRequests() ||
                    !source.host->matchesPrepared(*captured, projectedRate, projectedBlock,
                                                  productionPluginPlan, source.buses))
                    return fail(S::workerChanged, "effects.staleCapture: Newer native state remains unsaved");
            }
            for (const auto &worker : ready.addedWorkers)
                if (!worker.host->alive() || worker.host->runtimeIdentity() != worker.instance ||
                    worker.host->busLayout() != worker.buses ||
                    worker.host->processingQuantum() != worker.quantum ||
                    worker.host->latencySamples() != worker.latency ||
                    worker.host->declaredLatency().generation != worker.declaration ||
                    worker.host->parameterRevision() != worker.parameterRevision ||
                    worker.host->editorBusy() || worker.host->editorWindowState().exists ||
                    worker.host->pendingParameterRequests() || !ready.destination.plugin(worker.id) ||
                    !worker.host->matchesPrepared(*ready.destination.plugin(worker.id), projectedRate,
                                                  projectedBlock, productionPluginPlan, worker.buses))
                    return fail(S::workerChanged, "effects.workerChanged: Private native worker changed");
            // Only private graph preparation may precede membership ACK. Final
            // publication requires the actual canonical Controller + native ticket.
            if (!privateEffectPreparation)
                return fail(S::unsupportedDelta,
                            "effects.canonicalAdmissionRequired: Validate the real Controller transaction");
        }
        ready.overlay->outputRouting->validateForBuild(*edit, true);
        if (ready.context->validatePreparedGraph(*ready.graph) !=
            te::EditPlaybackContext::ReadyGraphChangeStatus::ready)
            return fail(S::graphRefused, "routing.graphRefused: Ready native graph became stale");
        if (ready.forward)
        {
            ordinary_sample_ports::Policy::Receipt source;
            if (!productionPluginProjection || !productionPluginProjection->ports)
                return fail(S::staleContext, "effects.forwardOwnerChanged: Actual source port owner required");
            if (!productionPluginProjection->ports->read(source))
                return fail(S::busy, "effects.forwardReadbackPending: Coherent actual root receipt required");
            if (source.optionalHandoffFault)
                return fail(S::staleProjection, "effects.forwardScopeFailed: Actual source/MIDI root failed");
        }
        ticket.impl->validated = !ready.stoppedEffectChange;
        return {S::ready, {}};
    }
    catch (...)
    {
        return fail(S::preparationFailed, "routing.validationFailed: Source retained before publication");
    }
}
AudioEngine::ChannelRoutingStatus AudioEngine::postChannelRouting(PreparedChannelRouting &ticket) noexcept
{
    static_assert(std::is_nothrow_swappable_v<Project>);
    using S = ChannelRoutingStatus;
    // Final FALLIBLE validateChannelRouting plus Controller validation must just
    // have passed under sole CONTROL/no reentry. This path performs no strings,
    // callbacks, native state reads, allocation or old-owner destruction.
    if (ticket.impl && !ticket.impl->owner)
        return S::ownerGone;
    if (!ticket.impl || ticket.impl->owner != this || ticket.impl->posted || !ticket.impl->validated)
        return S::staleProjection;
    auto &ready = *ticket.impl;
    if (ready.generation != channelRouting->generation || !edit ||
        ready.context != edit->getTransport().getCurrentPlaybackContext() || channelRouting->pendingGraph ||
        ready.timelineOwner->validate(ready.timelineTicket) != TimelineProjection::Status::ready)
        return S::staleProjection;
    if (ready.stoppedEffectChange &&
        (!ready.membership || !ready.membership->requested || ready.membership->acknowledged ||
         ready.membership->cancelRequested || ready.membership->readback.graphPosted ||
         ready.membership->readback.phase == StoppedEffectPhase::failed))
        return S::staleProjection;
    if (ready.context->publishPreparedGraph(*ready.graph) !=
        te::EditPlaybackContext::ReadyGraphChangeStatus::ready)
        return S::graphRefused;
    if (ready.stoppedEffectChange)
    {
        // A failure here is POSTCOMMIT, not a claim that the old graph survived.
        // Finish publication so the caller must commit the matching model; readback
        // retains an explicit failure and recovery is required.
        auto &membership = *ready.membership;
        const auto marked = membership.members->markGraphPosted(membership.ticket, *ready.graph);
        membership.readback.graphPosted = true;
        membership.graphSerial = ready.publicationSerial;
        if (marked != source_membership::SourceMembershipBroker::Error::none &&
            marked != source_membership::SourceMembershipBroker::Error::sourceUnavailable)
        {
            membership.readback.phase = StoppedEffectPhase::failed;
            membership.readback.error = "effects.postCommitMembershipFailure";
        }
    }
    if (ready.sourceRoute) publishInitialSourceRoute(*ready.sourceRoute);
    const auto timelineStatus = ready.timelineOwner->publish(ready.timelineTicket);
    jassert(timelineStatus == TimelineProjection::Status::published);
    (void)timelineStatus;
    if (ready.newTimelineOwner)
        channelRouting->timelineOwner.swap(ready.newTimelineOwner);
    timeline.swap(ready.timelineAlias);
    channelRouting->effective.swap(ready.candidate);
    projectedChannels.swap(ready.channels);
    effectPlugins.swap(ready.effects);
    if (ready.stoppedEffectChange)
    {
        channelRouting->membership = ready.membership; // Preowned CONTROL reference.
        currentHosts.swap(ready.destinationHosts);
        pluginErrors.swap(ready.destinationErrors);
        observedParameterRevisions.swap(ready.destinationObserved);
    }
    using std::swap;
    swap(projectedProject, ready.destination);
    timelineGeneration = ready.documentGeneration;
    ++updates;
    ++channelRouting->generation;
    channelRouting->context = ready.context;
    channelRouting->selected = false;
    channelRouting->postedSerial = ready.publicationSerial;
    channelRouting->sourceExchangeGeneration = ready.sourceRoute ? ready.sourceRoute->binding->preparedGeneration() : 0;
    if (!ready.stoppedEffectChange)
        channelRouting->membership.reset(); // Only terminal predecessors were admitted.
    channelRouting->pendingGraph.swap(ready.graph);
    ready.posted = true;
    return S::posted; // Completion requires actual native selected receipt.
}
