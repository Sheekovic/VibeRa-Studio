// SPDX-License-Identifier: AGPL-3.0-or-later
// Included inside namespace vibera in Studio.cpp. CONTROL-only application seam.
namespace
{
bool routingNativeEffectDescriptor(const juce::String &descriptor)
{
    const auto xml = juce::XmlDocument::parse(descriptor);
    juce::PluginDescription type;
    return xml && type.loadFromXml(*xml) && effects::isNativeDescription(type);
}
bool routingHasNativeEffects(const Project &project)
{
    for (const auto &insert : project.mixer)
        for (const auto &effect : insert.effects)
            if (routingNativeEffectDescriptor(effect.descriptor))
                return true;
    return false;
}
bool routingStructuralDifference(const Project &source, const Project &target)
{
    if (source.channels.size() != target.channels.size() || source.mixer.size() != target.mixer.size() ||
        source.tempo != target.tempo || source.numerator != target.numerator ||
        source.denominator != target.denominator)
        return true;
    for (const auto &channel : source.channels)
    {
        const auto *next = target.channel(channel.id);
        if (!next || channel.source != next->source || channel.audioClip != next->audioClip ||
            bool(channel.instrument) != bool(next->instrument))
            return true;
        if (channel.instrument &&
            (channel.instrument->id != next->instrument->id ||
             channel.instrument->descriptor != next->instrument->descriptor ||
             pluginMainIoFor(*channel.instrument) != pluginMainIoFor(*next->instrument)))
            return true;
    }
    for (const auto &old : source.mixer)
    {
        const auto *found = mixerInsert(target, old.index);
        if (!found)
            return true;
        const auto &next = *found;
        if (old.effects.size() != next.effects.size() || old.routes.size() != next.routes.size())
            return true;
        for (std::size_t j = 0; j < old.effects.size(); ++j)
            if (old.effects[j].id != next.effects[j].id ||
                old.effects[j].descriptor != next.effects[j].descriptor ||
                old.effects[j].detector != next.effects[j].detector ||
                pluginMainIoFor(old.effects[j]) != pluginMainIoFor(next.effects[j]))
                return true;
        for (std::size_t j = 0; j < old.routes.size(); ++j)
            if (old.routes[j].target != next.routes[j].target)
                return true;
    }
    return !source.state()
                .getChildWithName("AUDIO_PLACEMENTS")
                .isEquivalentTo(target.state().getChildWithName("AUDIO_PLACEMENTS"));
}
bool mixerInsertOnlyHistory(const Project &source, const Project &target)
{
    auto normalized = target;
    for (auto &row : normalized.mixer)
        for (auto &effect : row.effects)
            if (const auto *current = source.plugin(effect.id);
                current && !current->instrument && current->descriptor == effect.descriptor)
                effect.state = current->state; // Exact matching native opaque state only.
    return pureMixerInsertHistory(source, normalized) || pureMixerInsertHistory(normalized, source);
}
bool mixerRoutingOnlyHistory(const Project &before, const Project &after)
{
    if (before.channels.size() != after.channels.size() || before.mixer.size() != after.mixer.size())
        return false;
    auto normalized = after;
    bool changed = false;
    for (const auto &old : before.channels)
    {
        auto *next = normalized.channel(old.id);
        if (!next)
            return false;
        changed |= next->mixer != old.mixer;
        next->mixer = old.mixer;
    }
    for (const auto &old : before.mixer)
    {
        auto next = std::find_if(normalized.mixer.begin(), normalized.mixer.end(),
                                 [&](const auto &row) { return row.index == old.index; });
        if (next == normalized.mixer.end())
            return false;
        changed |= next->routes.size() != old.routes.size();
        if (next->routes.size() == old.routes.size())
            for (std::size_t i = 0; i < old.routes.size(); ++i)
                changed |= next->routes[i].target != old.routes[i].target;
        next->routes = old.routes;
    }
    for (auto &channel : normalized.channels)
        if (channel.instrument)
            if (const auto *old = before.plugin(channel.instrument->id);
                old && old->descriptor == channel.instrument->descriptor)
                channel.instrument->state = old->state;
    for (auto &row : normalized.mixer)
        for (auto &slot : row.effects)
            if (const auto *old = before.plugin(slot.id); old && old->descriptor == slot.descriptor)
                slot.state = old->state;
    // No nextId, note, control, detector, order or membership normalization.
    return changed && normalized.state().isEquivalentTo(before.state());
}
bool mixerEffectsOnlyHistory(const Project &source, const Project &target)
{
    if (source.mixer.size() != target.mixer.size())
        return false;
    auto normalized = target;
    normalized.nextId = source.nextId; // Reserved fresh IDs are not a musical/history difference.
    bool structural = false;
    for (const auto &old : source.mixer)
    {
        auto next = std::find_if(normalized.mixer.begin(), normalized.mixer.end(),
                                 [&](const auto &v) { return v.index == old.index; });
        if (next == normalized.mixer.end())
            return false;
        structural |= old.effects.size() != next->effects.size();
        if (old.effects.size() == next->effects.size())
            for (std::size_t i = 0; i < old.effects.size(); ++i)
                structural |= old.effects[i].id != next->effects[i].id ||
                              old.effects[i].descriptor != next->effects[i].descriptor ||
                              old.effects[i].detector != next->effects[i].detector;
        next->effects = old.effects;
        next->effectsBypassed = old.effectsBypassed;
    }
    return structural && source.state().isEquivalentTo(normalized.state());
}
const char* routingErrorCode(ChannelRoutingCoordinator::Error error) noexcept
{
    using Error = ChannelRoutingCoordinator::Error;
    switch (error)
    {
    case Error::none: return "none";
    case Error::wrongThread: return "routing.wrongThread";
    case Error::busy: return "routing.busy";
    case Error::externalBusy: return "routing.externalBusy";
    case Error::activeGesture: return "routing.activeGesture";
    case Error::exhausted: return "routing.serialExhausted";
    case Error::unsupportedSequence: return "routing.unsupportedSequence";
    case Error::modelRefused: return "routing.modelRefused";
    case Error::audioRefused: return "routing.audioRefused";
    case Error::staleSource: return "routing.staleSource";
    case Error::graphRefused: return "routing.graphRefused";
    case Error::invariantBroken: return "routing.invariantBroken";
    case Error::projectReplaced: return "routing.projectReplaced";
    case Error::contextLost: return "routing.contextLost";
    case Error::deviceChanged: return "routing.deviceChanged";
    case Error::superseded: return "routing.superseded";
    case Error::preparationFailed: return "routing.preparationFailed";
    case Error::unknownOperation: return "routing.unknownOperation";
    case Error::tooLate: return "routing.tooLate";
    }
    return "routing.unknownError";
}
} // namespace
bool Studio::routingPending()
{
    routingCoordinator.poll({patternMode, selectedPattern});
    const auto* job = routingCoordinator.find(lastRoutingOperation);
    return job && job->phase == ChannelRoutingCoordinator::Phase::pending;
}
juce::Result Studio::routingSourceAdmission()
{
    if (mixerChainApplying())
        return juce::Result::fail("effectChain.busy: Await or cancel chain Apply first");
    if (devicePreparationBusy()) return juce::Result::fail("device.busy: Finish/cancel native preparation first");
    routingCoordinator.poll({patternMode, selectedPattern});
    if (routingCoordinator.mutationBlocked())
        return juce::Result::fail("routing.busy: Await selection or restore interrupted audio");
    if (controller.hasGesture() || (mcpGesture && mcpGesture->state == "pending"))
        return juce::Result::fail("routing.activeGesture: Finish or cancel the current gesture");
    if (recordArmed || midiTaking || hasPendingMidiTake())
        return juce::Result::fail("routing.externalBusy: Finish recording before structural routing");
    if (auto result = finishRackEditsChecked(); result.failed()) return result;
    if (auto result = finishMidiTake(); result.failed()) return result;
    if (recordArmed || midiTaking || hasPendingMidiTake() || afterPluginState || pendingExport ||
        midiFileBusy || audio.rendering() || !audioImports.idle() || audioImportAfterTake ||
        pluginRecovery || mcpExportOperation.isNotEmpty() || customization.running ||
        std::any_of(pluginOperations.begin(), pluginOperations.end(), [](const auto& job)
        { return job.state == "pending" && job.kind != "refresh" && job.kind != "editor"; }))
        return juce::Result::fail("routing.externalBusy: Finish recording, import, file or plugin work");
    if (!audio.canDeferChannelRouting() &&
        (needsRebuild || engineRevision != controller.revision || projectionError.isNotEmpty()))
    {
        const auto result = audio.rebuild(controller.project, patternMode, selectedPattern);
        if (result.failed()) return result;
        engineRevision = controller.revision;
        needsRebuild = false;
        projectionError.clear();
    }
    return juce::Result::ok();
}
juce::Result Studio::routingResult(const ChannelRoutingCoordinator::Result& result)
{
    lastRoutingDispatchOperation = result.operation; // Intrinsic receipt before formatting.
    if (result.operation) lastRoutingOperation = result.operation;
    if (result.accepted())
    {
        status = routingPending() ? routingPendingStatus : routingReadyStatus;
        return juce::Result::ok();
    }
    const auto* job = routingCoordinator.find(result.operation);
    return juce::Result::fail(juce::String(routingErrorCode(result.error)) + ": " +
        (job && job->detail.isNotEmpty() ? job->detail : "Routing was refused; inspect routing state"));
}
juce::Result Studio::applyRoutingSequence(const std::vector<CommandArgs>& assignments,
                                        const juce::String& label)
{
    lastRoutingDispatchOperation = 0;
    if (auto admitted = routingSourceAdmission(); admitted.failed()) return admitted;
    return routingResult(routingCoordinator.sequence(assignments, label,
        {patternMode, selectedPattern}, false));
}
juce::Result Studio::captureRoutingEffect(const ChannelRoutingCoordinator::Result &admitted)
{
    const auto result = routingResult(admitted);
    if (result.failed() || !routingCoordinator.awaitingEffectCapture(admitted.operation))
        return result;
    // Native admission still validates every incumbent Host's exact revision.
    // The owned capture policy reuses acknowledged unchanged closed-worker bytes;
    // changed targets still capture, without pausing unrelated worker DSP.
    auto safe = juce::Component::SafePointer<Studio>(this);
    const auto operation = admitted.operation;
    const juce::ScopedValueSetter<bool> captureLease(effectCaptureDispatch, true);
    std::function<void(juce::Result, Project)> completion;
    try
    {
        completion = [safe, operation](juce::Result captured, Project snapshot)
        {
            if (safe)
                safe->routingCoordinator.effectCaptured(operation, captured, std::move(snapshot));
        };
        withPluginState(completion);
    }
    catch (...)
    {
        // If actual native capture was installed, retain its completion callback
        // for ordinary timer drain. Never report cancellation just from admission.
        if (!afterPluginState && audio.pluginStateCapturePending())
        {
            afterPluginState = std::move(completion);
            effectCaptureCallbackOperation = operation;
        }
        else if (!afterPluginState)
            routingCoordinator.effectCaptured(
                operation, juce::Result::fail("effects.captureFailed: Native capture could not start"), {});
    }
    return result;
}
bool Studio::routingStructuralCommand(const juce::String &id, const CommandArgs &args) const
{
    if (id == "channel.mixer")
        return false; // Canonical coordinator path only.
    if (id == "plugin.effect.move" && audio.canDeferChannelRouting())
        return false; // Initial context-free ordering changes no PCM/layout/Host state to restore.
    if (id == "mixer.send")
    {
        const auto found = std::find_if(controller.project.mixer.begin(), controller.project.mixer.end(),
                                        [&](const auto &insert) { return insert.index == args.mixer; });
        return found == controller.project.mixer.end() ||
            std::none_of(found->routes.begin(), found->routes.end(),
                [&](const auto& route) { return route.target == args.target; });
    }
    if (id == "audio.add" || id == "audio.trim" || id == "audio.takes" || id == "audio.comp" || id == "audio.fade" || id == "audio.stretchApply" || id == "project.tempo" || id == "project.meter" ||
        id == "channel.add" || id == "channel.clone" || id == "channel.delete" ||
        id == "channel.paste" || id == "channel.source" || id == "channel.preset" || id == "mixer.route" ||
        id == "mixer.routeOnly") return true;
    if (id.startsWith("clip.") && id != "clip.group" && id != "clip.ungroup" && id != "clip.add")
    {
        auto targets = args.ids;
        if (id == "clip.unique") targets = {args.target};
        else if (args.grouping.value_or(controller.clipGroupingEnabled))
            targets = expandedClipSelection(controller.project, targets);
        return std::any_of(controller.project.audioPlacements.begin(), controller.project.audioPlacements.end(),
            [&](const auto& clip) { return std::find(targets.begin(), targets.end(), clip.id) != targets.end(); });
    }
    if (id.startsWith("notes.") || id.startsWith("steps.") || id.startsWith("rack.") ||
        id.startsWith("pattern.") || id.startsWith("automation.") || id.startsWith("plugin.binding.") ||
        id.startsWith("channel.sampler.") || id == "project.swing" || id == "channel.gain" || id == "channel.pitch" || id == "channel.pitchRange" || id == "channel.keyLabel" ||
        id == "channel.pan" || id == "channel.mute" || id == "channel.rename" || id == "channel.colour" ||
        id == "channel.solo" || id == "channel.unsolo" || id == "channel.swingMix" ||
        id == "channel.muteLock" || id == "channel.properties" || id == "channel.truncateSwing" ||
        id == "channel.moveUp" || id == "channel.moveDown" || id == "clip.add" || id == "clip.group" ||
        id == "clip.ungroup" || id == "mixer.gain" || id == "mixer.pan" || id == "mixer.mute" ||
        id == "mixer.solo" || id == "mixer.unsolo" || id == "mixer.unmuteAll" ||
        id == "mixer.soloConnected" || id == "mixer.rename" || id == "mixer.colour" ||
        id == "plugin.effect.mix" || id == "mixer.effects.bypass" || id == "plugin.bypass" ||
        isMixerInsertOrderCommand(id) || isMixerGroupCommand(id))
        return false;
    return true; // Unknown/new canonical operations conservatively restore first.
}
juce::Result Studio::restoreRoutingBeforeStructural()
{
    if (mixerChainApplying())
        return juce::Result::fail("effectChain.busy: Chain Apply owns the source projection");
    if (devicePreparationBusy()) return juce::Result::fail("device.busy: Finish/cancel native preparation first");
    if (routingApplicationPreparing)
        return juce::Result::fail("routing.busy: Structural preparation is already owned");
    if (routingPending())
        return juce::Result::fail("routing.pending: Wait for the actual selected graph before structural work");
    if (audio.documentProjectionDeferred() && audio.engine.getDeviceManager().deviceManager.getCurrentAudioDevice())
        return juce::Result::fail("device.preparationRequired: Finish automatic native preparation or retry through Play before structural work");
    const bool hadOverlay = audio.channelRoutingReadback().overlayActive;
    if (!hadOverlay && !audio.documentProjectionDeferred()) return juce::Result::ok();
    if (afterPluginState || pendingExport || audio.rendering() || midiTaking || hasPendingMidiTake())
        return juce::Result::fail("routing.externalBusy: Finish recording or project I/O before structural work");
    const juce::ScopedValueSetter<bool> lease(routingApplicationPreparing, true);
    // Restore CURRENT canonical music before changing its model/history. This
    // is explicitly the existing full replacement path for structural work,
    // never a route/note/step/velocity fallback. A failed restore changes no model.
    const auto restored = audio.rebuildFullProjection(controller.project, patternMode, selectedPattern);
    if (restored.failed()) return restored;
    if (hadOverlay)
    {
        const auto admitted = routingCoordinator.fullProjectionRestored({patternMode, selectedPattern});
        if (!admitted.accepted()) return routingResult(admitted);
    }
    engineRevision = controller.revision;
    needsRebuild = false;
    projectionError.clear();
    return juce::Result::ok();
}
bool Studio::requiresBoundedMixerStructural(const juce::String &id, const CommandArgs &args) const
{
    if (id == "plugin.io")
        return true;
    const bool route = id == "channel.mixer" || id == "mixer.route" || id == "mixer.routeOnly" ||
                       (id == "mixer.send" && routingStructuralCommand(id, args));
    const auto *removed = id == "plugin.remove" ? controller.project.plugin(args.target) : nullptr;
    const bool effect =
        id == "plugin.effect.insert" || id == "plugin.effect.move" || (removed && !removed->instrument);
    if (!route && !effect)
        return false;
    if ((route || id == "plugin.effect.move") && audio.canDeferChannelRouting())
        return false; // Initial document routing/order retains its proven deferred canonical path.
    for (const auto &row : controller.project.mixer)
        for (const auto &slot : row.effects)
            if (slot.detector)
                return true;
    // Existing initial-source/native experimental route authority is separate.
    if (audio.initialSourceTransportReadback().status != AudioEngine::InitialSourceTransportStatus::none)
        return false;
    // The retained fast path is qualified for its original VST3/PDC scope.
    // Native effect graphs use the supported bounded structural replacement.
    if (routingHasNativeEffects(controller.project) ||
        (id == "plugin.effect.insert" && routingNativeEffectDescriptor(args.text)))
        return true;
    if (route)
    {
        // The retained ordinary path already compensates fixed worker transport
        // latency. A plugin's additional native latency or an auxiliary audible
        // topology needs the general bounded reconfiguration path.
        if ((id == "mixer.route" || id == "mixer.routeOnly" || id == "mixer.send") && args.target > 0)
            return true;
        for (const auto &row : controller.project.mixer)
            for (const auto &edge : row.routes)
                if (edge.target != 0)
                    return true;
        for (const auto &runtime : audio.pluginRuntimeStates())
            if (auto host = audio.parameterHost(runtime.plugin);
                host && host->declaredLatency().nativeSamples > 0)
                return true;
        return false; // Existing fixed-transport/native0 route capability stays retained.
    }
    if (id == "plugin.effect.move")
        return true;
    PreparedPluginTransport::Ptr transport;
    if (!audio.playing() && audio.stoppedEffectWorkerTransport(transport).wasOk())
        for (const auto &row : controller.project.mixer)
            if (row.index != 0 &&
                ((id == "plugin.effect.insert" && row.index == args.mixer && row.effects.empty()) ||
                 (id == "plugin.remove" && row.effects.size() == 1 && row.effects.front().id == args.target)))
                return false; // Existing proven empty-chain append / one-slot removal.
    return true;
}
std::optional<juce::Result> Studio::admitControllerCommand(const juce::String &id, const CommandArgs &args,
                                                           const juce::String &owner)
{
    lastStructuralOperation.clear();
    lastMixerEffectReplacementOperation.clear();
    lastMixerEffectSidechainOperation.clear();
    lastLifecycleOperation.clear();
    // Burn is admitted during playback (live voices reconcile to the new IDs);
    // recording keeps refusing so new identities never retrigger a take.
    if (id == "pattern.burnChannelLoop" && (recordArmed || midiTaking || hasPendingMidiTake()))
        return juce::Result::fail(
            "channelLoop.transportBusy: Stop recording before materializing loop identities");
    if (mixerChainApplying())
        return juce::Result::fail("effectChain.busy: Chain Apply owns canonical publication");
    if (devicePreparationBusy()) return juce::Result::fail("device.busy: Finish/cancel native preparation first");
    if (routingApplicationPreparing)
        return juce::Result::fail("routing.busy: Structural preparation is already owned");
    routingCoordinator.poll({patternMode, selectedPattern});
    if (routingCoordinator.mutationBlocked())
        return juce::Result::fail("routing.busy: Await native selection or restore interrupted audio");
    if (isMixerInsertLifecycleCommand(id))
    {
        if (owner.isNotEmpty() || controller.hasGesture() || customization.running)
            return juce::Result::fail(
                "mixer.activeGesture: Insert membership is not a gesture or macro command");
        if (id == "mixer.insert.delete")
        {
            auto candidate = controller.project;
            if (auto valid = deleteMixerInsert(candidate, args.mixer,
                                               args.mixerDelete.value_or(MixerInsertDeletePolicy{}));
                valid.failed())
                return valid;
        }
        auto request = chain_workflow_detail::object();
        chain_workflow_detail::put(request, "action",
                                   id == "mixer.insert.create" ? "insertCreate" : "insertDelete");
        chain_workflow_detail::put(request, "mixer", id == "mixer.insert.create" ? 0 : args.mixer);
        chain_workflow_detail::put(request, "projectIdentity", controller.projectIdentity);
        chain_workflow_detail::put(request, "revision", juce::int64(controller.revision));
        const juce::ScopedValueSetter<const CommandArgs *> own(mixerInsertLifecycleDispatch, &args);
        const auto outcome = mixerChainOperation(request);
        lastLifecycleOperation = outcome["operation"].toString();
        return outcome["completionState"].toString() == "failed"
                   ? juce::Result::fail(outcome["code"].toString() + ": " + outcome["message"].toString())
                   : juce::Result::ok();
    }
    if (id == "plugin.effect.sidechain")
    {
        if (!args.detector)
            return juce::Result::fail("sidechain.invalidArguments: Required detector is null or object");
        if (owner.isNotEmpty() || controller.hasGesture() || customization.running)
            return juce::Result::fail("effectChain.activeGesture: Sidechain is not a gesture or macro");
        auto candidate = controller.project;
        if (auto valid = setPluginDetector(candidate, args.target, *args.detector); valid.failed())
            return valid;
        const auto target =
            std::find_if(controller.project.mixer.begin(), controller.project.mixer.end(),
                         [&](const auto &row)
                         {
                             return std::any_of(row.effects.begin(), row.effects.end(),
                                                [&](const auto &slot) { return slot.id == args.target; });
                         });
        if (target == controller.project.mixer.end())
            return juce::Result::fail("sidechain.unavailableTarget: Existing Mixer effect required");
        if (candidate.state().isEquivalentTo(controller.project.state()))
            return juce::Result::fail("sidechain.noChange: Detector assignment is already current");
        auto request = chain_workflow_detail::object();
        chain_workflow_detail::put(request, "action", "sidechain");
        chain_workflow_detail::put(request, "mixer", target->index);
        chain_workflow_detail::put(request, "target", juce::int64(args.target));
        chain_workflow_detail::put(request, "projectIdentity", controller.projectIdentity);
        chain_workflow_detail::put(request, "revision", juce::int64(controller.revision));
        const juce::ScopedValueSetter<const CommandArgs *> own(mixerEffectSidechainDispatch, &args);
        const auto outcome = mixerChainOperation(request, canonicalStructuralAdmissionOperation);
        lastMixerEffectSidechainOperation = outcome["operation"].toString();
        return outcome["completionState"].toString() == "failed"
                   ? juce::Result::fail(outcome["code"].toString() + ": " + outcome["message"].toString())
                   : juce::Result::ok();
    }
    if (id == "plugin.effect.replace")
    {
        // Delegate before Controller execution: no model-first/native-later replacement.
        if (owner.isNotEmpty() || controller.hasGesture() || customization.running)
            return juce::Result::fail(
                "effectChain.activeGesture: Replacement is not a gesture or macro command");
        const auto target =
            std::find_if(controller.project.mixer.begin(), controller.project.mixer.end(),
                         [&](const auto &row)
                         {
                             return std::any_of(row.effects.begin(), row.effects.end(),
                                                [&](const auto &slot) { return slot.id == args.target; });
                         });
        if (target == controller.project.mixer.end())
            return juce::Result::fail("effects.unavailableTarget: Existing Mixer effect ID required");
        auto request = chain_workflow_detail::object();
        chain_workflow_detail::put(request, "action", "replace");
        chain_workflow_detail::put(request, "mixer", target->index);
        chain_workflow_detail::put(request, "target", juce::int64(args.target));
        chain_workflow_detail::put(request, "descriptor", args.text);
        chain_workflow_detail::put(request, "projectIdentity", controller.projectIdentity);
        chain_workflow_detail::put(request, "revision", juce::int64(controller.revision));
        const juce::ScopedValueSetter<bool> own(mixerEffectReplacementDispatch, true);
        const auto outcome = mixerChainOperation(request);
        lastMixerEffectReplacementOperation = outcome["operation"].toString();
        return outcome["completionState"].toString() == "failed"
                   ? juce::Result::fail(outcome["code"].toString() + ": " + outcome["message"].toString())
                   : juce::Result::ok();
    }
    if (isMixerAutomationTargetCommand(id) || isSamplerAutomationTargetCommand(id) ||
        id == "automation.source.rename" || id == "automation.source.clone")
        if (auto admitted = admitMixerAutomation(owner); admitted.failed()) return admitted;
    if (isMixerInsertOrderCommand(id) || isMixerGroupCommand(id))
    {
        juce::String code;
        if (auto admitted = admitMixerInsertOrder(code, owner); admitted.failed()) return admitted;
    }
    if (id == "plugin.io")
    {
        if (!args.pluginIo)
            return juce::Result::fail("pluginIo.invalidArguments: Complete input/output selection required");
        auto candidate = controller.project;
        if (auto valid = setPluginMainIo(candidate, args.target, *args.pluginIo); valid.failed())
            return valid;
        const auto host = audio.preparedPluginHost(controller.project, args.target);
        if (!host || !host->alive())
            return juce::Result::fail(
                "pluginIo.unavailable: Prepare the current plugin before changing its native layout");
        if (candidate.state().isEquivalentTo(controller.project.state()))
            return juce::Result::ok();
    }
    if (requiresBoundedMixerStructural(id, args))
    {
        if (owner.isNotEmpty() || controller.hasGesture() || customization.running)
            return juce::Result::fail(
                "effectChain.activeGesture: Structural Apply is not a gesture or macro");
        auto request = chain_workflow_detail::object();
        chain_workflow_detail::put(request, "action", "structural");
        chain_workflow_detail::put(request, "mixer", 0);
        chain_workflow_detail::put(request, "projectIdentity", controller.projectIdentity);
        chain_workflow_detail::put(request, "revision", juce::int64(controller.revision));
        const juce::ScopedValueSetter<const CommandArgs *> own(mixerStructuralDispatch, &args);
        const juce::ScopedValueSetter<juce::String> command(mixerStructuralCommandDispatch, id);
        const auto outcome = mixerChainOperation(request, canonicalStructuralAdmissionOperation);
        lastStructuralOperation = outcome["operation"].toString();
        return outcome["completionState"].toString() == "failed"
                   ? juce::Result::fail(outcome["code"].toString() + ": " + outcome["message"].toString())
                   : juce::Result::ok();
    }
    if (id == "channel.mixer")
    {
        lastRoutingDispatchOperation = 0;
        if (owner.isNotEmpty())
            return juce::Result::fail("routing.activeGesture: Routing cannot update a leased preview");
        if (auto admitted = routingSourceAdmission(); admitted.failed()) return admitted;
        return routingResult(routingCoordinator.assign(args, {patternMode, selectedPattern}, false));
    }
    // A healthy ordinary stopped source can perform the prepared empty-chain
    // transaction without a full replacement. Other existing structural paths
    // retain their previous behavior; no failed prepared transaction falls back.
    PreparedPluginTransport::Ptr effectTransport;
    if ((id == "plugin.effect.insert" || id == "plugin.remove") && !audio.playing() &&
        !routingHasNativeEffects(controller.project) &&
        (id != "plugin.effect.insert" || !routingNativeEffectDescriptor(args.text)) &&
        audio.stoppedEffectWorkerTransport(effectTransport).wasOk())
    {
        const auto target = std::find_if(
            controller.project.mixer.begin(), controller.project.mixer.end(),
            [&](const auto &insert)
            {
                return insert.index != 0 &&
                       (id == "plugin.effect.insert"
                            ? insert.index == args.mixer && insert.effects.empty()
                            : insert.effects.size() == 1 && insert.effects.front().id == args.target);
            });
        if (target != controller.project.mixer.end())
        {
            if (owner.isNotEmpty())
                return juce::Result::fail("effects.activeGesture: Finish the gesture first");
            if (auto admitted = routingSourceAdmission(); admitted.failed())
                return admitted;
            return captureRoutingEffect(routingCoordinator.effectCommand(
                id, args, target->index, {patternMode, selectedPattern}, false));
        }
    }
    if ((audio.channelRoutingReadback().overlayActive || audio.documentProjectionDeferred()) && routingStructuralCommand(id, args))
        if (auto restored = restoreRoutingBeforeStructural(); restored.failed()) return restored;
    return std::nullopt; // Authoritative ordinary Controller mutation, unchanged.
}
std::optional<juce::Result> Studio::admitControllerHistory(HistoryDirection direction)
{
    lastMixerChainHistoryOperation.clear();
    lastStructuralOperation.clear();
    if (nativeScalarBusy()) return juce::Result::fail("parameterHistory.busy: Finish the owned native scalar edit");
    lastLifecycleOperation.clear();
    if (mixerChainApplying())
        return juce::Result::fail("effectChain.busy: Chain Apply owns history");
    if (devicePreparationBusy()) return juce::Result::fail("device.busy: Finish/cancel native preparation first");
    if (routingApplicationPreparing)
        return juce::Result::fail("routing.busy: Structural preparation is already owned");
    lastRoutingDispatchOperation = 0;
    routingCoordinator.poll({patternMode, selectedPattern});
    if (routingCoordinator.mutationBlocked())
        return juce::Result::fail("routing.busy: Await native selection or restore interrupted audio");
    const bool effective = audio.channelRoutingReadback().overlayActive;
    if (!effective && controller.hasGesture()) return std::nullopt;
    if (controller.hasGesture())
        return juce::Result::fail("routing.activeGesture: Finish/cancel the preview before routed history");
    HistoryDestination ioDestination;
    if (auto preview = controller.previewHistoryDestination(direction, ioDestination); preview.failed())
        return preview;
    if (purePluginIoHistory(controller.project, ioDestination.destination()))
    {
        auto request = chain_workflow_detail::object();
        chain_workflow_detail::put(request, "action", "ioHistory");
        chain_workflow_detail::put(request, "mixer", 0);
        chain_workflow_detail::put(request, "projectIdentity", controller.projectIdentity);
        chain_workflow_detail::put(request, "revision", juce::int64(controller.revision));
        const juce::ScopedValueSetter<std::optional<HistoryDirection>> own(pluginIoHistoryDispatch,
                                                                           direction);
        const auto outcome = mixerChainOperation(request);
        lastMixerChainHistoryOperation = outcome["operation"].toString();
        return outcome["completionState"].toString() == "failed"
                   ? juce::Result::fail(outcome["code"].toString() + ": " + outcome["message"].toString())
                   : juce::Result::ok();
    }
    if (controller.isNativeScalarHistory(direction))
    {
        auto request = chain_workflow_detail::object();
        chain_workflow_detail::put(request, "action", "scalarHistory");
        chain_workflow_detail::put(request, "mixer", 0);
        chain_workflow_detail::put(request, "projectIdentity", controller.projectIdentity);
        chain_workflow_detail::put(request, "revision", juce::int64(controller.revision));
        const juce::ScopedValueSetter<std::optional<HistoryDirection>> own(nativeScalarHistoryDispatch, direction);
        const auto outcome = mixerChainOperation(request);
        lastMixerChainHistoryOperation = outcome["operation"].toString();
        return outcome["completionState"].toString() == "failed"
                   ? juce::Result::fail(outcome["code"].toString() + ": " + outcome["message"].toString())
                   : juce::Result::ok();
    }
    HistoryDestination destination;
    if (auto preview = controller.previewHistoryDestination(direction, destination); preview.failed())
        return preview;
    const auto& target = destination.destination();
    bool boundedRouting = false;
    if (mixerRoutingOnlyHistory(controller.project, target))
    {
        CommandArgs route;
        boundedRouting = requiresBoundedMixerStructural("channel.mixer", route);
        // An older history edge can introduce a delayed/auxiliary destination;
        // any detector is sufficient; actual final engine validation still owns it.
        for (const auto &row : target.mixer)
            for (const auto &slot : row.effects)
                boundedRouting |= slot.detector.has_value();
    }
    if (boundedRouting)
    {
        auto request = chain_workflow_detail::object();
        chain_workflow_detail::put(request, "action", "routingHistory");
        chain_workflow_detail::put(request, "mixer", 0);
        chain_workflow_detail::put(request, "projectIdentity", controller.projectIdentity);
        chain_workflow_detail::put(request, "revision", juce::int64(controller.revision));
        const juce::ScopedValueSetter<std::optional<HistoryDirection>> own(mixerRoutingHistoryDispatch,
                                                                           direction);
        const auto outcome = mixerChainOperation(request);
        lastStructuralOperation = outcome["operation"].toString();
        lastMixerChainHistoryOperation = lastStructuralOperation;
        return outcome["completionState"].toString() == "failed"
                   ? juce::Result::fail(outcome["code"].toString() + ": " + outcome["message"].toString())
                   : juce::Result::ok();
    }
    if (mixerInsertOnlyHistory(controller.project, target))
    {
        auto request = chain_workflow_detail::object();
        chain_workflow_detail::put(request, "action", "insertHistory");
        chain_workflow_detail::put(request, "mixer", 0); // Master is an existing admission context only.
        chain_workflow_detail::put(request, "projectIdentity", controller.projectIdentity);
        chain_workflow_detail::put(request, "revision", juce::int64(controller.revision));
        const juce::ScopedValueSetter<std::optional<HistoryDirection>> own(mixerInsertHistoryDispatch,
                                                                           direction);
        const auto outcome = mixerChainOperation(request);
        lastLifecycleOperation = outcome["operation"].toString();
        return outcome["completionState"].toString() == "failed"
                   ? juce::Result::fail(outcome["code"].toString() + ": " + outcome["message"].toString())
                   : juce::Result::ok();
    }
    PreparedPluginTransport::Ptr effectTransport;
    if (!audio.playing() && !routingHasNativeEffects(controller.project) &&
        !routingHasNativeEffects(target) && audio.stoppedEffectWorkerTransport(effectTransport).wasOk())
        for (const auto &insert : controller.project.mixer)
        {
            StoppedEffectChangePlan effectPlan;
            const auto hasDetector = [](const Project &project)
            {
                return std::any_of(project.mixer.begin(), project.mixer.end(),
                                   [](const auto &row)
                                   {
                                       return std::any_of(row.effects.begin(), row.effects.end(),
                                                          [](const auto &slot)
                                                          { return slot.detector.has_value(); });
                                   });
            };
            if (insert.index != 0 && !hasDetector(controller.project) && !hasDetector(target) &&
                planStoppedEffectChange(controller.project, target, insert.index, effectPlan).wasOk())
            {
                if (auto admitted = routingSourceAdmission(); admitted.failed())
                    return admitted;
                return captureRoutingEffect(routingCoordinator.effectHistory(
                    direction, insert.index, {patternMode, selectedPattern}, false));
            }
        }
    if (mixerEffectsOnlyHistory(controller.project, target))
    {
        const auto admission = admitMixerChainCapture();
        if (admission.failed())
            return admission;
        const auto changed = std::find_if(controller.project.mixer.begin(), controller.project.mixer.end(),
                                          [&](const auto &old)
                                          {
                                              const auto *next = mixerInsert(target, old.index);
                                              return next && (old.effects != next->effects ||
                                                              old.effectsBypassed != next->effectsBypassed);
                                          });
        auto args = chain_workflow_detail::object();
        chain_workflow_detail::put(args, "action", "history");
        chain_workflow_detail::put(args, "mixer", changed->index);
        chain_workflow_detail::put(args, "projectIdentity", controller.projectIdentity);
        chain_workflow_detail::put(args, "revision", juce::int64(controller.revision));
        const juce::ScopedValueSetter<std::optional<HistoryDirection>> own(mixerChainHistoryDispatch,
                                                                           direction);
        const auto outcome = mixerChainOperation(args);
        lastMixerChainHistoryOperation = outcome["operation"].toString();
        return outcome["completionState"].toString() == "failed"
                   ? juce::Result::fail(outcome["code"].toString() + ": " + outcome["message"].toString())
                   : juce::Result::ok();
    }
    const bool assignment = std::any_of(controller.project.channels.begin(), controller.project.channels.end(),
        [&](const auto& channel)
        { const auto* next = target.channel(channel.id); return next && next->mixer != channel.mixer; });
    if (assignment)
    {
        if (auto admitted = routingSourceAdmission(); admitted.failed()) return admitted;
        // Mixed history destinations fail whole at actual audio preparation;
        // do not move the real cursor and fall back to a later failed rebuild.
        return routingResult(routingCoordinator.history(direction, {patternMode, selectedPattern}, false));
    }
    // A history destination with no assignment change may still replace
    // actual native-plugin topology. Only that positive classification acquires
    // the owned capture/Apply adapter; unrelated history keeps its prior path.
    if (pluginNativeTopologyDifference(controller.project, target))
    {
        // A structural history operation must OWN preparation through terminal
        // publication. Synchronously restoring CURRENT music here starts async
        // workers but loses their operation and never prepares the destination.
        auto request = chain_workflow_detail::object();
        chain_workflow_detail::put(request, "action", "structuralHistory");
        chain_workflow_detail::put(request, "mixer", 0);
        chain_workflow_detail::put(request, "projectIdentity", controller.projectIdentity);
        chain_workflow_detail::put(request, "revision", juce::int64(controller.revision));
        const juce::ScopedValueSetter<std::optional<HistoryDirection>> own(structuralHistoryDispatch,
                                                                           direction);
        const auto outcome = mixerChainOperation(request);
        lastStructuralOperation = outcome["operation"].toString();
        lastMixerChainHistoryOperation = lastStructuralOperation;
        return outcome["completionState"].toString() == "failed"
                   ? juce::Result::fail(outcome["code"].toString() + ": " + outcome["message"].toString())
                   : juce::Result::ok();
    }
    // Preserve the pre-existing guarded restoration behavior for unrelated
    // source/tempo/AudioClip history. It does NOT acquire the new capture job.
    if ((effective || audio.documentProjectionDeferred()) && routingStructuralDifference(controller.project, target))
        if (auto restored = restoreRoutingBeforeStructural(); restored.failed()) return restored;
    return std::nullopt;
}
juce::var Studio::channelRoutingState(std::uint64_t operation)
{
    routingCoordinator.poll({patternMode, selectedPattern});
    auto value = std::make_unique<juce::DynamicObject>();
    if (!operation) operation = lastRoutingOperation;
    const auto* job = routingCoordinator.find(operation);
    value->setProperty("operation", juce::int64(operation));
    value->setProperty("projectIdentity", controller.projectIdentity);
    value->setProperty("revision", juce::int64(controller.revision));
    value->setProperty("currentProjectIdentity", controller.projectIdentity);
    value->setProperty("currentRevision", juce::int64(controller.revision));
    value->setProperty("retention", "Latest32 terminal operations; expired IDs are unavailable");
    value->setProperty("pendingAudioUpdate", devicePreparationBusy() || routingPending() || needsRebuild);
    value->setProperty("audioProjectionDeferred", audio.documentProjectionDeferred());
    if (!job)
    {
        value->setProperty("completionState", operation ? "failed" : "completed");
        value->setProperty("state", "none");
        value->setProperty("errorCode", operation ? "routing.unknownOperation" : "none");
        value->setProperty("documentChanged", false);
        if (operation)
        {
            auto* error = new juce::DynamicObject;
            error->setProperty("code", "routing.unknownOperation");
            error->setProperty("message", "Operation is unknown or expired from the latest32 results");
            value->setProperty("error", juce::var(error));
        }
        return value.release();
    }
    using Phase = ChannelRoutingCoordinator::Phase;
    const auto *state = job->phase == Phase::completed   ? "completed"
                        : job->phase == Phase::failed    ? "failed"
                        : job->phase == Phase::cancelled ? "cancelled"
                                                         : "pending";
    value->setProperty("completionState", state);
    value->setProperty("state", state);
    value->setProperty("projectIdentity", job->project);
    value->setProperty("committed", job->committed);
    value->setProperty("documentCommitted", job->committed);
    using Disposition = ChannelRoutingCoordinator::AudioDisposition;
    value->setProperty("audioDisposition", job->audioDisposition == Disposition::deferredNoDevice
        ? "deferredNoDevice" : job->audioDisposition == Disposition::noAudioWork ? "noAudioWork" : "liveGraph");
    value->setProperty("completionScope", job->audioDisposition == Disposition::deferredNoDevice
        ? "document" : job->noOp ? "noChange" : "documentAndAudio");
    value->setProperty("audioPosted", job->audioPosted);
    value->setProperty("documentChanged", job->committed);
    value->setProperty("noOp", job->noOp);
    value->setProperty("nativeCompletionKnown", job->nativeCompletionKnown);
    value->setProperty("sourceRevision", juce::int64(job->sourceRevision));
    value->setProperty("destinationRevision", juce::int64(job->destinationRevision));
    value->setProperty("postedSerial", juce::int64(job->postedSerial));
    value->setProperty("selectedSerial", juce::int64(job->selectedSerial));
    value->setProperty("nativePostedSerial", juce::int64(job->nativePostedSerial));
    value->setProperty("referenceStart", juce::int64(job->referenceStart));
    value->setProperty("referenceEnd", juce::int64(job->referenceEnd));
    value->setProperty("stage", job->stage);
    const auto errorCode =
        job->errorCode.isNotEmpty() ? job->errorCode : juce::String(routingErrorCode(job->error));
    value->setProperty("errorCode", errorCode);
    value->setProperty("message", job->detail);
    if (job->error != ChannelRoutingCoordinator::Error::none)
    {
        auto* error = new juce::DynamicObject;
        error->setProperty("code", errorCode);
        error->setProperty("message", job->detail.isEmpty() ? "Inspect native state and restore interrupted audio" : job->detail);
        value->setProperty("error", juce::var(error));
    }
    value->setProperty("cancelAvailability", job->effect && job->phase == Phase::pending && !job->audioPosted
                                                 ? "before_publication"
                                                 : "too_late_after_publication");
    return value.release();
}
juce::var Studio::channelRoutingSchema()
{
    return channelRoutingResultSchema();
}
