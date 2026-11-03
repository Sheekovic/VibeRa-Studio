// SPDX-License-Identifier: AGPL-3.0-or-later
// Included after PluginPreparation definition. CONTROL only, initial deferred
// startup with NO incumbent Edit/context. Ordinary live replacement is unchanged.
std::optional<unsigned> AudioEngine::capturedPluginRevision(Id id,
    const std::shared_ptr<PluginHost>& host) const noexcept
{
    const auto found = lastCapturedRevisions.find(id);
    return found != lastCapturedRevisions.end() && found->second.host == host
        ? std::optional<unsigned>(found->second.value) : std::nullopt;
}
bool AudioEngine::drainDeferredWorkerPreparation(bool cancel)
{
    if (!preparation[0]) { deferredWorkerOwner = false; return true; }
    if (!deferredWorkerOwner || !deferredDocumentProjection || edit) return false;
    auto& pending = preparation[0];
    if (cancel) pending->signalThreadShouldExit();
    if (pending->isThreadRunning() || !pending->done.load(std::memory_order_acquire)) return false;
    // Candidate workers not published by buildEdit still need nonblocking owned
    // retirement. Reserve before ownership changes; failure retains the job.
    ownedHosts.reserve(ownedHosts.size() + pending->results.size());
    for (const auto& [id, value] : pending->results)
        if (value.host && std::find(ownedHosts.begin(), ownedHosts.end(), value.host) == ownedHosts.end())
            ownedHosts.push_back(value.host);
    pending.reset(); // Thread already exited; workers remain in the owned retirement bank.
    deferredWorkerOwner = false;
    return true;
}
juce::Result AudioEngine::rebuildDeferredDeviceProjection(const Project& project, bool mode, Id pattern,
    const std::function<juce::Result(bool)>& finalAdmission)
{
    const auto* manager = juce::MessageManager::getInstanceWithoutCreating();
    if (!manager || !manager->isThisTheMessageThread() || !deferredDocumentProjection || edit ||
        channelRouting->effective || !channelRouting->prepared.empty() || capture || worker ||
        !finalAdmission || deferredDeviceFinalAdmission)
        return juce::Result::fail("device.invalidOwner: Initial deferred CONTROL projection owner required");
    if (auto valid = finalAdmission(false); valid.failed()) return valid;
    if (preparation[0] && !preparation[0]->done.load(std::memory_order_acquire))
        return juce::Result::fail("Preparing VST3 workers...");
    // Every rollback owner is prepared before changing native bookkeeping. No
    // live Edit exists. Candidate startup failure therefore restores deferred
    // ownership rather than leaving a partially bound Edit that timers can run.
    auto hosts = currentHosts;
    auto errors = pluginErrors;
    auto revisions = observedParameterRevisions;
    auto captured = lastCapturedRevisions;
    auto document = projectedProject;
    auto previousTimeline = timeline;
    const auto previousRate = projectedRate;
    const auto previousBlock = projectedBlock;
    const auto previousPattern = projectedPattern;
    const auto previousMode = projectedPatternMode;
    const auto previousHostEligibility = deferredHostProjection;
    const auto previousSampleEligibility = deferredSampleProjection;
    const auto previousBeat = deferredPositionBeat;
    const auto previousLoop = deferredLooping;
    const auto restore = [&]() noexcept
    {
        compensationGraph.reset();
        releaseChannelRoutingForEdit();
        projectedChannels.clear(); projectedAudioClips.clear(); projectedInserts.clear();
        effectPlugins.clear();
        edit.reset(); // Native destruction quiesces any candidate context before its parents retire.
        productionPluginProjection.reset(); // Its borrowed context is no longer alive.
        currentHosts.swap(hosts); pluginErrors.swap(errors); observedParameterRevisions.swap(revisions);
        lastCapturedRevisions.swap(captured);
        using std::swap; swap(projectedProject, document);
        timeline.swap(previousTimeline);
        projectedRate = previousRate; projectedBlock = previousBlock;
        projectedPattern = previousPattern; projectedPatternMode = previousMode;
        hasProjection = false; deferredDocumentProjection = true;
        deferredHostProjection = previousHostEligibility; deferredSampleProjection = previousSampleEligibility;
        deferredPositionBeat = previousBeat; deferredLooping = previousLoop;
    };
    const juce::ScopedValueSetter<const std::function<juce::Result(bool)>*> guard(deferredDeviceFinalAdmission,
                                                                          &finalAdmission);
    deferredWorkerOwner = true;
    try
    {
        auto result = rebuild(project, mode, pattern);
        if (result.failed())
        {
            if (!preparationPending(result)) restore();
            return result;
        }
        deferredWorkerOwner = false;
        return result;
    }
    catch (...)
    {
        restore();
        return juce::Result::fail("device.nativePreparationFailed: Candidate startup failed; retained source state restored");
    }
}
