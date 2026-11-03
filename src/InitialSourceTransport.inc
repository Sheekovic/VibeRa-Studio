// SPDX-License-Identifier: AGPL-3.0-or-later
// Included after PluginPreparation/ChannelRoutingState are complete. CONTROL
// only. Explicit engineering opt-in; all ordinary callers retain default1Q.
struct AudioEngine::InitialSourceTransportState
{
    const Project source;
    Project authorizedRouting; // Sole CONTROL accepted-route projection, never a DSP document.
    const InitialSourceTransportOptions options;
    juce::String controllerIdentity; // Bound before first graph; replacement invalidates this experiment.
    const PreparedPluginTransport::Ptr plan;
    source_membership::PreparedSourceHosts::Ptr membership;
    te::SourceBurstSession::Ptr session;
    std::shared_ptr<experimental::AppMixerAudioPorts> audioPorts;
    const std::shared_ptr<initial_source_epoch::Health> sourceEpochHealth;
    InitialSourceTransportStatus status = InitialSourceTransportStatus::configured;
    bool installationAttempted = false;
    InitialSourceTransportState(const Project &p, InitialSourceTransportOptions o,
                                PreparedPluginTransport::Ptr transport)
        : source(p), authorizedRouting(p), options(o), plan(std::move(transport)),
          sourceEpochHealth(o.zeroStageSourceRoutes?std::make_shared<initial_source_epoch::Health>():nullptr)
    {
        if(options.zeroStageSourceRoutes) controllerIdentity=options.routingController->projectIdentity;
    }
};

AudioEngine::InitialSourceTransportOutcome
AudioEngine::prepareInitialSourceTransport(const Project &project, InitialSourceTransportOptions options)
{
    using Status = InitialSourceTransportStatus;
    const auto *messages = juce::MessageManager::getInstanceWithoutCreating();
    if (!messages || !messages->isThisTheMessageThread() || initialSourceTransport || edit || hasProjection ||
        timeline || compensationGraph || !engine.getActiveEdits().getEdits().isEmpty() ||
        !currentHosts.empty() || !ownedHosts.empty() || !ownedSamples.empty() || !effectPlugins.empty() ||
        !projectedChannels.empty() || !projectedInserts.empty() || !projectedAudioClips.empty() ||
        preparation[0] || preparation[1] || !retiredPreparation.empty() || capture || recoveryPlugin ||
        worker || renderEdit || deferredDocumentProjection || deferredHostProjection || !channelRouting ||
        channelRouting->effective || channelRouting->pendingGraph || !channelRouting->prepared.empty() ||
        auditionSample || midiInput.capture.active.load() || typingInput.capture.active.load())
        return {
            Status::invalidOwner,
            "source.initialOnly: Fresh CONTROL AudioEngine without native/worker/project owners required"};
    if (auto valid = project.validate(); valid.failed())
        return {Status::invalidProject, valid.getErrorMessage()};
    if (((options.audioZeroHistoryProofs && !options.audioOnlyMixerPorts) ||
         (options.zeroStageSourceRoutes && !options.audioZeroHistoryProofs)) ||
        !options.sessionGeneration || !options.membershipGeneration || !project.audioPlacements.empty() ||
        std::any_of(project.channels.begin(), project.channels.end(),
                    [](const auto &c) { return c.audioClip; }))
        return {Status::invalidProject, "source.closedProjection: Nonzero generations and "
                                        "sampler/instrument-only initial project required"};
    if (options.zeroStageSourceRoutes && (!options.routingController ||
        options.routingController->projectIdentity.isEmpty() ||
        !options.routingController->project.state().isEquivalentTo(project.state())))
        return {Status::invalidOwner,"source.canonicalOwner: Exact initial Controller/project authority required"};
    auto &manager = engine.getDeviceManager().deviceManager;
    auto *device = manager.getCurrentAudioDevice();
    if (!device || options.quantum != device->getCurrentBufferSizeSamples() ||
        options.maximumSourceScalePpm < 1000000)
        return {Status::invalidBudget,
                "source.deviceQuantum: Exact actual device quantum required, no buffer changes"};
    PreparedPluginTransport::Ptr candidatePlan;
    const auto prepared =
        PreparedPluginTransport::prepare(manager, options.quantum, {0, 1}, options.maximumSourceScalePpm,
                                         options.explicitProviderFrames, candidatePlan);
    if (!prepared.succeeded())
        return {Status::invalidBudget, "source.budgetRefused: Physical/source budget is not admissible"};
    try
    {
        // Initial0 native is ONLY a lower bound. Host startup and membership
        // still validate each actual native declaration against KQ/ring capacity.
        auto candidate = std::make_unique<InitialSourceTransportState>(project, options, candidatePlan);
        if (options.audioOnlyMixerPorts)
            candidate->audioPorts = experimental::AppMixerAudioPorts::create(options.audioZeroHistoryProofs,
                                                                  options.zeroStageSourceRoutes,candidate->sourceEpochHealth);
        initialSourceTransport = std::move(candidate); // Only successful output mutation.
        return {Status::configured,
                "source.configured: Initial policy prepared, no graph/worker/audio completion"};
    }
    catch (...)
    {
        return {Status::preparationFailed,
                "source.allocationFailed: No initial policy or native state published"};
    }
}

juce::Result AudioEngine::validateInitialSourceProject(const Project &project) const
{
    if (!initialSourceTransport)
        return juce::Result::ok();
    const auto &state = *initialSourceTransport;
    Project normalized = project;
    if (!MixerNativeIdentity::alignDisplayOrder(normalized, state.source))
        return juce::Result::fail("source.initialOnly: Mixer identity universe must remain exact");
    if (normalized.channels.size() != state.source.channels.size())
        return juce::Result::fail("source.initialOnly: Channel identity universe must remain exact");
    std::vector<Channel> channelOrder;
    for (const auto &source : state.source.channels)
    {
        const auto *channel = normalized.channel(source.id);
        if (!channel)
            return juce::Result::fail("source.initialOnly: Channel identity universe must remain exact");
        channelOrder.push_back(*channel);
    }
    normalized.channels = std::move(channelOrder);
    // Keep the exact native source/mixer/plugin universe, while allowing the
    // existing immutable notes/velocity/pattern/automation document updates.
    // Do not silently freeze routine score edits just to keep an experiment green.
    const auto topology = [](const Project &p)
    {
        auto tree = p.state();
        for (const auto *child : {"PATTERNS", "PLACEMENTS", "AUTOMATION", "MIXER_GROUPS", "MIXER_SOLO",
                                  "CHANNEL_GROUPS", "CHANNEL_SOLO"})
            tree.removeChild(tree.getChildWithName(child), nullptr);
        for (const auto *property : {"nextId", "name", "swing"})
            tree.removeProperty(property, nullptr);
        // Display metadata/order, scope ownership and score mute never alter
        // native source membership. Mutes remain authoritative in the immutable
        // score update; this comparison only admits unchanged source topology.
        // Keep every source, sampler, route, plugin and device field exact.
        for (auto channel : tree.getChildWithName("CHANNELS"))
            for (const auto *property : {"name", "colour", "icon", "group", "muteLocked", "muted"})
                channel.removeProperty(property, nullptr);
        // Capturing the SAME live Host updates portable opaque state/bindings;
        // it neither replaces that Host nor changes source membership. Keep
        // descriptor/id/type and every source/Mixer routing field authoritative.
        for (const auto *owners : {"CHANNELS", "MIXER"})
            for (auto entry : tree.getChildWithName(owners))
                for (auto plugin : entry)
                    if (plugin.hasType("PLUGIN"))
                    {
                        plugin.removeProperty("state", nullptr);
                        if (auto bindings = plugin.getChildWithName("AUTOMATION_BINDINGS");
                            bindings.isValid())
                            plugin.removeChild(bindings, nullptr);
                    }
        // Immutable same-topology controls only. Keep every stable identity,
        // route edge/level, plugin/config/source and device contract exact.
        // Project validation + native automation admission remain authoritative.
        for (auto insert : tree.getChildWithName("MIXER"))
            for (const auto *control : {"gain", "pan", "muted", "stereoSeparation", "phaseInverted",
                                        "stereoSwapped", "name", "colour"})
                insert.removeProperty(control, nullptr);
        return tree;
    };
    auto authorizedSource = state.source;
    // Only successful joint native post may advance this exact effective map.
    // The original topology comparator still covers every route field.
    for (auto& channel : authorizedSource.channels)
        channel.mixer = state.authorizedRouting.channel(channel.id)->mixer;
    if (state.status == InitialSourceTransportStatus::preparationFailed ||
        !topology(normalized).isEquivalentTo(topology(authorizedSource)))
        return juce::Result::fail("source.initialOnly: Native topology/configuration changes require "
                                  "separately qualified membership/lifecycle");
    juce::AudioDeviceManager::PhysicalCallbackSnapshot actual;
    const auto &budget = state.plan->budget();
    if (!state.plan->read(actual) || !actual.ownerAlive || !actual.active || actual.writerConflict ||
        actual.countersExhausted || actual.deviceEpoch != budget.deviceEpoch ||
        actual.configuredFrames != budget.configuredFrames ||
        actual.maximumOuterFrames > budget.physicalFrames)
        return juce::Result::fail(
            "source.deviceChanged: Initial physical contract no longer valid; no fallback policy");
    if (state.session)
    {
        te::SourceBurstSession::Outcome native;
        if (!state.session->read(native) || native.failed || native.retired)
            return juce::Result::fail(
                "source.sessionFailed: Initial source lifecycle requires explicit CONTROL disposal");
    }
    return juce::Result::ok();
}

juce::Result AudioEngine::installInitialSourceTransport(te::Edit &candidate, const Project &project)
{
    using Status = InitialSourceTransportStatus;
    auto &state = *initialSourceTransport;
    const auto fail = [&](const juce::String &message)
    {
        state.status = Status::preparationFailed;
        return juce::Result::fail(message);
    };
    if (state.installationAttempted || edit || hasProjection ||
        candidate.initialSourceGeneration != state.options.sessionGeneration || !candidate.closedNativeIO)
        return fail("source.initialOwner: Exact private initial closed Edit required");
    try
    {
        std::vector<source_membership::PreparedSourceHosts::Binding> bindings;
        bindings.reserve(currentHosts.size());
        const auto append = [&](const PluginSpec &spec) -> bool
        {
            const auto found = currentHosts.find(spec.id);
            if (found == currentHosts.end() || !found->second)
                return false;
            initial_source_epoch::Fence::Ptr fixed;
            if(state.sourceEpochHealth)
            {
                for(auto* track:te::getAudioTracks(candidate))
                    for(auto* plugin:track->pluginList)
                        if(auto* sampler=dynamic_cast<FixedSampler*>(plugin))
                        {
                            const auto& actual=sampler->initialSourceEpochFenceControl();
                            if(actual&&actual->pluginId()==spec.id)
                            {
                                if(fixed||actual->sourceRuntime()!=sampler->runtimeOwnerControl().get())return false;
                                fixed=actual;
                            }
                        }
                if(!fixed)return false;
            }
            bindings.push_back({&spec, found->second, {},std::move(fixed)});
            return true;
        };
        for (const auto &channel : project.channels)
            if (channel.instrument && !append(*channel.instrument))
                return fail("source.missingInstrument: Required worker unavailable");
        for (const auto &insert : project.mixer)
            for (const auto &spec : insert.effects)
                if (!append(spec))
                    return fail("source.missingEffect: Required worker unavailable");
        if (bindings.size() != currentHosts.size())
            return fail("source.hostUniverse: Exact unique initial Host universe required");
        const auto &b = state.plan->budget();
        te::SourceBurstSession::Configuration configuration{b.deviceEpoch,
                                                            state.options.sessionGeneration,
                                                            state.plan->sampleRate(),
                                                            b.configuredFrames,
                                                            b.physicalFrames,
                                                            b.sourceFrames,
                                                            b.maximumSourceScalePpm};
        source_membership::PreparedSourceHosts::Ptr membership;
        const auto members = source_membership::PreparedSourceHosts::prepare(
            state.plan, configuration, state.options.membershipGeneration, bindings, bindings.size(),
            membership,state.sourceEpochHealth);
        if (!members.succeeded())
            return fail("source.memberRefused: Exact declared worker/plan/bus membership failed");
        te::SourceBurstSession::Ptr session;
        auto &manager = engine.getDeviceManager();
        if (te::SourceBurstSession::prepare(manager.deviceManager, configuration, membership, session) !=
            te::SourceBurstSession::Status::prepared)
            return fail("source.sessionPreparationFailed: No native context publication");
        // The closed Edit has been awaiting a session SINCE construction. Its
        // context can be prepared/published without running graph/input/clock.
        // All host pins and native source admission exist BEFORE any processing.
        state.installationAttempted = true;
        candidate.getTransport().ensureContextAllocated();
        auto *context = candidate.getTransport().getCurrentPlaybackContext();
        if (!context || context->nativeIOAttachmentRefused() || context->sourceLifecycleInvalidated())
            return fail("source.closedIORefused: Native IO/lifecycle is outside the initial closed scope");
        const auto installed = manager.installInitialSourceBurstSession(*context, session);
        if (installed != te::SourceBurstSession::Status::pendingFirstSerial)
            return fail("source.installationRefused: Native initial session was not admitted");
        state.membership = std::move(membership);
        state.session = std::move(session);
        state.status = Status::pending; // Not actual DSP/worker/audio completion.
        return juce::Result::ok();
    }
    catch (...)
    {
        return fail("source.nativePreparationFailed: Private initial source setup failed");
    }
}

AudioEngine::InitialSourceTransportReadback AudioEngine::initialSourceTransportReadback() const
{
    InitialSourceTransportReadback output;
    if (!initialSourceTransport)
        return output;
    const auto &state = *initialSourceTransport;
    const auto &b = state.plan->budget();
    output.status = state.status;
    output.quantum = b.quantum;
    output.packets = b.packets;
    output.transportSamples = b.transportSamples;
    output.physicalFrames = b.physicalFrames;
    output.maximumSourceFrames = b.sourceFrames;
    output.deviceEpoch = b.deviceEpoch;
    output.sessionGeneration = state.options.sessionGeneration;
    output.membershipGeneration = state.options.membershipGeneration;
    output.fixedEpochConfigured=bool(state.sourceEpochHealth);
    output.fixedEpochHealthy=!state.sourceEpochHealth||state.sourceEpochHealth->healthy();
    output.sourceExchangeFaulted=initialSourceExchangeFaulted();
    output.completedExchangeGeneration=initialSourceCompletedExchangeGeneration();
    juce::AudioDeviceManager::PhysicalCallbackSnapshot physical;
    output.physicalKnown = state.plan->read(physical);
    output.physicalStatus =
        !output.physicalKnown        ? transport::BurstStatus::telemetryUnavailable
        : !physical.ownerAlive       ? transport::BurstStatus::managerReleased
        : !physical.active           ? transport::BurstStatus::deviceStopped
        : physical.writerConflict    ? transport::BurstStatus::writerConflict
        : physical.countersExhausted ? transport::BurstStatus::counterExhausted
        : physical.deviceEpoch != b.deviceEpoch || physical.configuredFrames != b.configuredFrames
            ? transport::BurstStatus::deviceChanged
        : physical.maximumOuterFrames > b.physicalFrames ? transport::BurstStatus::physicalBurstExceeded
                                                         : transport::BurstStatus::ready;
    if (state.membership)
        output.memberCount = state.membership->size();
    if (state.membership)
        output.membershipKnown = state.membership->read(output.membership);
    if (state.session)
        output.nativeKnown = state.session->read(output.native);
    if (!output.physicalKnown || (state.session && (!output.nativeKnown || !output.membershipKnown)))
        output.status = InitialSourceTransportStatus::readbackUnavailable;
    else if (output.physicalStatus != transport::BurstStatus::ready)
        output.status = InitialSourceTransportStatus::sourceFault;
    else if (output.nativeKnown)
    {
        if (output.native.failed || output.native.retired ||
            output.membership.status != source_membership::Status::ready)
            output.status = InitialSourceTransportStatus::sourceFault;
        else
        {
            output.pairedClosed =
                source_membership::PreparedSourceHosts::closedWith(output.membership, output.native);
            output.status = output.pairedClosed ? InitialSourceTransportStatus::ready
                                                : InitialSourceTransportStatus::pending;
        }
    }
    if (!output.fixedEpochHealthy||initialSourceExchangeFaulted()) output.status = InitialSourceTransportStatus::sourceFault;
    return output;
}

void AudioEngine::withInitialSourceTransportContext(
    const std::function<void(te::Edit &, te::EditPlaybackContext &)> &driver)
{
    const auto *messages = juce::MessageManager::getInstanceWithoutCreating();
    if (!messages || !messages->isThisTheMessageThread() || !driver || !initialSourceTransport ||
        !initialSourceTransport->session || !edit || !hasProjection || playing() ||
        edit->initialSourceGeneration != initialSourceTransport->options.sessionGeneration ||
        !edit->closedNativeIO)
        throw std::invalid_argument("Exact stopped initial transport fixture CONTROL owner required");
    auto *context = edit->getTransport().getCurrentPlaybackContext();
    if (!context)
        throw std::invalid_argument("Actual initial native context required");
    driver(*edit, *context); // No caller may retain these references past driver.
}

std::vector<AudioEngine::InitialSourceEpochObservation> AudioEngine::initialSourceEpochObservations()const
{
    const auto* messages=juce::MessageManager::getInstanceWithoutCreating();
    if(!messages||!messages->isThisTheMessageThread())
        throw std::invalid_argument("sourceEpoch.control: Pinned CONTROL observation required");
    std::vector<InitialSourceEpochObservation> result;
    if(initialSourceTransport&&initialSourceTransport->membership)
        for(const auto& source:initialSourceTransport->membership->fixedEpochObservationsControl())
            result.push_back({source.plugin,source.runtime,source.expected,source.failure});
    return result;
}

AudioEngine::InitialAudioPortReadback AudioEngine::initialAudioPortReadback() const
{
    InitialAudioPortReadback out;
    if (!initialSourceTransport || !initialSourceTransport->audioPorts) return out;
    const auto value=initialSourceTransport->audioPorts->observe();
    out.configured=true;out.terminalFault=value.terminalFault;
    out.owners=value.owners;out.runtimeOwners=value.runtimeOwners;out.nativeEdges=value.nativeEdges;out.tracks=value.tracks;
    out.builds=value.builds;out.portsPrepared=value.portsPrepared;out.audioBlocks=value.audioBlocks;
    out.nonemptyAudioMidi=value.nonemptyAudioMidi;
    out.nativeOn=value.nativeOn;out.nativeOff=value.nativeOff;out.nativePanic=value.nativePanic;out.nativeHeld=value.nativeHeld;
    return out;
}

std::vector<AudioEngine::InitialZeroQueueObservation> AudioEngine::initialAudioZeroQueues() const
{
    std::vector<InitialZeroQueueObservation> output;
    if (initialSourceTransport && initialSourceTransport->audioPorts)
        for (const auto& item : initialSourceTransport->audioPorts->zeroHistoryObservations())
            output.push_back({item.nativeOwner, item.build, item.processor, item.certified, item.createdHere,
                item.preparationObserved, item.memorySharing, item.outputZero, item.proofTrace});
    return output;
}


std::vector<AudioEngine::InitialMixerTrackMidiObservation> AudioEngine::initialMixerTrackMidiObservations() const
{
    std::vector<InitialMixerTrackMidiObservation> out;
    if(initialSourceTransport&&initialSourceTransport->audioPorts)
        for(const auto& x:initialSourceTransport->audioPorts->trackMidiObservations())
            out.push_back({x.nativeId,x.mixer,x.scope,x.runtime,x.readers,x.ingress.enabled,x.ingress.registered,
                std::uint32_t(x.ingress.fault),std::uint32_t(x.terminal.fault),x.ingress.accepted,x.ingress.delivered,
                x.terminal.on,x.terminal.off,x.terminal.panicMessages,x.terminal.panicFlags,x.terminal.active});
    return out;
}

bool AudioEngine::initialSourceExchangeFaulted() const noexcept
{
    return initialSourceTransport && initialSourceTransport->options.zeroStageSourceRoutes &&
        initialSourceTransport->audioPorts &&
        ((initialSourceTransport->sourceEpochHealth&&!initialSourceTransport->sourceEpochHealth->healthy()) ||
         initialSourceTransport->audioPorts->exchangeFault() != experimental::SourceExchangeContinuity::Fault::none ||
         initialSourceTransport->audioPorts->terminalFaultControl());
}
std::uint64_t AudioEngine::initialSourceCompletedExchangeGeneration() const noexcept
{
    return initialSourceTransport && initialSourceTransport->audioPorts
        ? initialSourceTransport->audioPorts->completedExchangeGeneration() : 0;
}

juce::Result AudioEngine::validateInitialSourceRoute(const InitialSourceRoutePermit& permit) const
{
    if (!initialSourceTransport || !initialSourceTransport->options.zeroStageSourceRoutes ||
        !initialSourceTransport->audioPorts || !initialSourceTransport->session ||
        !initialSourceTransport->membership || !permit.canonicalOwner || !permit.canonicalTicket ||
        permit.canonicalOwner != initialSourceTransport->options.routingController ||
        !permit.source.state().isEquivalentTo(projectedProject.state()))
        return juce::Result::fail("source.routeAuthority: Exact initial source/canonical ownership required");
    if (auto valid=validateInitialSourceProject(permit.source); valid.failed()) return valid;
    const auto& instrument=*permit.source.channels.front().instrument;
    const auto worker=currentHosts.find(instrument.id);
    if(worker==currentHosts.end() || !worker->second ||
       !worker->second->matchesPrepared(instrument,projectedRate,projectedBlock,initialSourceTransport->plan))
        return juce::Result::fail("source.routeWorker: Exact unchanged prepared Host/plan/config required");
    const auto health=initialSourceTransportReadback();
    if (!health.pairedClosed || health.status != InitialSourceTransportStatus::ready ||
        (permit.binding && !initialSourceTransport->audioPorts->validateRoute(*permit.binding)))
        return juce::Result::fail("source.routeHealth: Current paired membership/whole-route authority required");
    return juce::Result::ok();
}

std::shared_ptr<void> AudioEngine::leaseInitialSourceRoute(InitialSourceRoutePermit& permit,
    std::shared_ptr<const te::GraphOutputRouting> routes)
{
    const auto ownerFor = [&](int index) -> const void*
    {
        const auto found=projectedInserts.find(index);
        if (found==projectedInserts.end()) return nullptr;
        const auto* list=&found->second->pluginList;
        const auto plugins=channelRouting->effective ? channelRouting->effective->lists.at(list)
                                                     : list->getPlugins();
        const void* owner=nullptr;
        for(auto* plugin:plugins) if(dynamic_cast<MixerMasterGate*>(plugin))
        {
            if(owner) throw std::invalid_argument("source.routeOwner: Duplicate actual final gate");
            owner=plugin->getPreparedAudioOutputStageIdentity();
        }
        return owner;
    };
    permit.binding=initialSourceTransport->audioPorts->prepareRoute(std::move(routes),
        ownerFor(permit.from),ownerFor(permit.to));
    return std::shared_ptr<void>(initialSourceTransport->audioPorts->leaseRoute(*permit.binding));
}

void AudioEngine::publishInitialSourceRoute(InitialSourceRoutePermit& permit) noexcept
{
    initialSourceTransport->audioPorts->publishRoute(*permit.binding);
    using std::swap;
    swap(initialSourceTransport->authorizedRouting,permit.publicationProject);
}

AudioEngine::ChannelRoutingOutcome AudioEngine::prepareInitialSourceChannelRouting(Controller& controller,
    PreparedControllerChange& canonical, std::unique_ptr<PreparedChannelRouting>& output)
{
    using S=ChannelRoutingStatus;
    const auto* messages=juce::MessageManager::getInstanceWithoutCreating();
    if(!messages || !messages->isThisTheMessageThread() || !initialSourceTransport ||
       !initialSourceTransport->options.zeroStageSourceRoutes)
        return {S::unsupportedDelta,"source.initialOnly: Explicit prepared initial exchange is disabled"};
    if(initialSourceTransport->options.routingController!=&controller ||
       initialSourceTransport->controllerIdentity!=controller.projectIdentity)
        return {S::staleProjection,"source.canonicalOwner: Exact initial Controller/session required"};
    try
    {
        if(auto valid=controller.validatePreparedChange(canonical);valid.failed())
            return {S::staleProjection,valid.getErrorMessage()};
        const auto* destination=canonical.destinationBeforeCommit();
        if(!destination || canonical.isNoOp()) return {S::noOp,"source.routeNoOp: No audio/history publication"};
        if(!controller.project.state().isEquivalentTo(projectedProject.state()) ||
           projectedProject.channels.size()!=1 || !projectedProject.channels.front().instrument ||
           currentHosts.size()!=1 || !projectedAudioClips.empty())
            return {S::unsupportedDelta,"source.routeScope: One exact held instrument source required"};
        for(const auto& insert:projectedProject.mixer)
        {
            if(!insert.effects.empty() || (insert.index && (insert.routes.size()!=1 ||
               insert.routes.front().target!=0 || insert.routes.front().gain!=1.f)))
                return {S::unsupportedDelta,"source.routeScope: Effects/sends/populated histories unqualified"};
        }
        auto permit=std::make_shared<InitialSourceRoutePermit>();
        permit->canonicalOwner=&controller; permit->canonicalTicket=&canonical;
        permit->source=projectedProject; permit->publicationProject=*destination;
        permit->channel=projectedProject.channels.front().id;
        permit->from=projectedProject.channels.front().mixer;
        const auto* target=destination->channel(permit->channel);
        if(!target || !permit->from || !target->mixer || target->mixer==permit->from)
            return {S::unsupportedDelta,"source.routeScope: Two distinct non-Master stages required"};
        permit->to=target->mixer;
        std::unique_ptr<PreparedChannelRouting> candidate;
        auto result=prepareGraphProjection(*destination,nullptr,candidate,permit);
        if(result.status!=S::ready) return result;
        // Final canonical check is repeated after ALL expensive graph work.
        result=validateInitialSourceChannelRouting(controller,canonical,*candidate);
        if(result.status!=S::ready) return result;
        output.swap(candidate);
        return result;
    }
    catch(const std::exception& e) { return {S::preparationFailed,"source.routePreparationFailed: "+juce::String(e.what())}; }
    catch(...) { return {S::preparationFailed,"source.routePreparationFailed: Incumbent and canonical cursor retained"}; }
}

AudioEngine::ChannelRoutingOutcome AudioEngine::validateInitialSourceChannelRouting(Controller& controller,
    PreparedControllerChange& canonical, PreparedChannelRouting& audio) const
{
    using S=ChannelRoutingStatus;
    if(audio.impl) audio.impl->validated=false;
    if(audio.impl && !audio.impl->owner)
        return {S::ownerGone,"source.ownerGone: Prepared native owner was explicitly released"};
    if(!initialSourceTransport || initialSourceTransport->options.routingController!=&controller ||
       initialSourceTransport->controllerIdentity!=controller.projectIdentity)
        return {S::staleProjection,"source.canonicalOwner: Initial canonical owner/session changed"};
    if(!audio.impl || !audio.impl->sourceRoute ||
       audio.impl->sourceRoute->canonicalOwner!=&controller ||
       audio.impl->sourceRoute->canonicalTicket!=&canonical)
        return {S::staleProjection,"source.canonicalTicket: Exact same prepared transaction required"};
    if(auto valid=controller.validatePreparedChange(canonical);valid.failed())
        return {S::staleProjection,valid.getErrorMessage()};
    const auto* destination=canonical.destinationBeforeCommit();
    if(!destination || !destination->state().isEquivalentTo(audio.impl->destination.state()) ||
       !controller.project.state().isEquivalentTo(audio.impl->sourceRoute->source.state()))
        return {S::staleProjection,"source.canonicalProject: Canonical and native destination/source diverged"};
    auto result=validateGraphProjection(audio,true);
    if(result.status!=S::ready) return result;
    if(auto valid=controller.validatePreparedChange(canonical);valid.failed())
    {
        audio.impl->validated=false;
        return {S::staleProjection,valid.getErrorMessage()};
    }
    return result;
}
