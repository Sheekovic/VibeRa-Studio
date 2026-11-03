// SPDX-License-Identifier: AGPL-3.0-or-later
// Included inside namespace vibera, after FixedSampler definition. Test-only
// original live graph fixture setup; never called by an ordinary UI operation.
void AudioEngine::withRetainedContextFixture(const PluginSpec &spec,
                                             const std::function<void(PluginHost &)> &configureWorker,
                                             const std::function<void(RetainedContextFixture &)> &driver)
{
    auto check = [](bool ok, const char *message)
    {
        if (!ok)
            throw std::runtime_error(message);
    };
    check(edit && timeline && hasProjection && !playing() && !compensationGraph &&
              projectedChannels.size() == 1 && projectedInserts.contains(1) && projectedInserts.contains(2),
          "Retained fixture needs one stopped prepared sample Channel and two existing inserts");
    check(!edit->getTransport().getCurrentPlaybackContext(),
          "Original fixture configuration must precede any native graph allocation");
    check(engine.getDeviceManager().getSampleRate() == 48000 &&
              engine.getDeviceManager().getBlockSize() == 256 && spec.name == "VibeRa Mixer Latency Fixture",
          "Retained fixture requires the actual original SDK provider and exact48k256 virtual configuration");
    auto &projected = projectedChannels.begin()->second;
    auto *sourceSampler = dynamic_cast<FixedSampler *>(projected.sampler.get());
    check(sourceSampler && projected.sample && !projectedProject.channels.front().instrument &&
              !projectedProject.channels.front().audioClip && currentHosts.empty() && effectPlugins.empty(),
          "Closed sample-source fixture must have no preexisting external host owners");
    auto host = std::make_shared<PluginHost>();
    const auto started = host->start(spec, 48000, 256, false, {}, {1, 2});
    if (started.failed())
        throw std::runtime_error(started.getErrorMessage().toStdString());
    configureWorker(*host); // Supported parameter acknowledgements BEFORE graph allocation.
    check(host->alive() && !host->offline() && host->latencySamples() == 256 &&
              host->busLayout().auxiliaryChannels == 2,
          "Fixture must acknowledge native0 delay and genuine stereo auxiliary worker");
    ownedHosts.push_back(host);
    currentHosts[spec.id] = host;
    observedParameterRevisions[spec.id] = {host, host->parameterRevision()};
    auto *sourceTrack = projectedInserts.at(1), *targetTrack = projectedInserts.at(2);
    constexpr int detectorBus = 18500;
    auto send = edit->getPluginCache().createNewPlugin(MixerSidechainSend::xmlTypeName, {});
    auto *sendAdapter = dynamic_cast<MixerSidechainSend *>(send.get());
    check(sendAdapter != nullptr, "Actual detector app adapter is registered");
    sendAdapter->configure(detectorBus);
    int gate = 0;
    for (auto *plugin : sourceTrack->pluginList)
    {
        if (dynamic_cast<MixerMasterGate *>(plugin))
            break;
        ++gate;
    }
    check(gate < sourceTrack->pluginList.getPlugins().size(), "Original pre-Master-gate tap position exists");
    sourceTrack->pluginList.insertPlugin(send, gate, nullptr);
    auto remote = edit->getPluginCache().createNewPlugin(RemoteEffect::xmlTypeName, {});
    auto *effect = dynamic_cast<RemoteEffect *>(remote.get());
    check(effect != nullptr, "Actual remote app adapter is registered");
    effect->configure(host, false, timeline, spec.id);
    const auto connected = effect->connectSidechain(sourceTrack->itemID, detectorBus);
    if (connected.failed())
        throw std::runtime_error(connected.getErrorMessage().toStdString());
    int fader = 0;
    for (auto *plugin : targetTrack->pluginList)
    {
        if (dynamic_cast<AutomationGain *>(plugin))
            break;
        ++fader;
    }
    check(fader < targetTrack->pluginList.getPlugins().size(),
          "Original target effect-before-fader position exists");
    targetTrack->pluginList.insertPlugin(remote, fader, nullptr);
    effectPlugins[spec.id] = remote;
    // A dedicated original lane is installed BEFORE any held note/graph. Both
    // candidate and incumbent can pin it, but only ONE adopted graph may consume
    // it. Fresh MidiMonitor/VoiceBanks deliberately do not gain a copied held key.
    auto inputLane = std::make_shared<MidiLane>();
    sourceSampler->setInput(inputLane);
    const auto channel = projectedChannels.begin()->first;
    setMidiTarget(channel); // Allocates the ONE ordinary actual context.
    auto *context = edit->getTransport().getCurrentPlaybackContext();
    check(context != nullptr, "Actual retained same-Edit playback context allocated");
    // Keep the exact census being compared owned for this bounded test. Native
    // cache maintenance legitimately removes otherwise unreferenced default
    // plugins on its timer; that is unrelated to candidate registration safety.
    const auto baselineCacheOwners = edit->getPluginCache().getPlugins();
    const auto baselineMasterVolume = edit->getMasterVolumePlugin();
    const std::array<te::Plugin::Ptr, 3> originals{projected.sampler, remote, send};
    auto create = [&, originals, host, inputLane, channel](unsigned kind,
                                                           const juce::ValueTree &state) -> te::Plugin::Ptr
    {
        if (kind == 0)
        {
            auto candidate = FixedSampler::createDetachedGraph(*edit, state);
            auto *sampler = dynamic_cast<FixedSampler *>(candidate.get());
            sampler->setPreparedSource(sourceSampler->preparedSource());
            sampler->setTimeline(timeline, channel); // Unchanged immutable document; never rebind incumbent.
            sampler->setCutClock(samplerCutClock);
            sampler->bindPreparedInputForDetachedGraph(inputLane);
            sampler->setGuide(guide);
            return candidate;
        }
        if (kind == 1)
        {
            auto candidate = RemoteEffect::createDetachedGraph(*edit, state);
            auto *prepared = dynamic_cast<RemoteEffect *>(candidate.get());
            prepared->configure(host, false, timeline, spec.id);
            const auto connected = prepared->connectSidechain(sourceTrack->itemID, detectorBus);
            if (connected.failed())
                throw std::runtime_error(connected.getErrorMessage().toStdString());
            return candidate;
        }
        if (kind == 2)
        {
            auto candidate = MixerSidechainSend::createDetachedGraph(*edit, state);
            dynamic_cast<MixerSidechainSend *>(candidate.get())->configure(detectorBus);
            return candidate;
        }
        throw std::invalid_argument("Unknown original retained fixture adapter kind");
    };
    auto createRetainingSampler = [&, sourceSampler](const juce::ValueTree &state) -> te::Plugin::Ptr
    {
        auto candidate = FixedSampler::createDetachedGraph(*edit, state);
        auto *sampler = dynamic_cast<FixedSampler *>(candidate.get());
        const auto admitted = sampler->shareCompatibleRuntimeFrom(*sourceSampler);
        if (admitted.failed())
            throw std::runtime_error(admitted.getErrorMessage().toStdString());
        return candidate;
    };
    auto pointer = [](const void *value)
    { return juce::String::toHexString(juce::int64(reinterpret_cast<std::uintptr_t>(value))); };
    auto &nativeHistory = edit->getUndoManager();
    // This bounded test compares ownership/history while callbacks and messages
    // run. Native Edit's350ms idle timer may otherwise close the SETUP transaction
    // midway through the comparison without any graph/document mutation.
    const te::Edit::UndoTransactionInhibitor fixtureTransaction(*edit);
    auto nativeHistoryGuard = nativeHistory.canUndo()   ? nativeHistory.prepareUndoMutation()
                              : nativeHistory.canRedo() ? nativeHistory.prepareRedoMutation()
                                                        : nullptr;
    auto capture = [&, originals, host, pointer]
    {
        // No render/cursor/callback/worker packet counters: those are verified
        // separately as changing observations. Snapshot actual registration and
        // immutable identities, not fitted PCM or synthetic mirror dictionaries.
        // TransportControl mirrors the advancing runtime cursor into this one
        // native property on its timer. It is not canonical project structure;
        // exact per-callback head advancement is independently verified.
        auto structuralState = edit->state.createCopy();
        structuralState.getChildWithName(te::IDs::TRANSPORT).removeProperty(te::IDs::position, nullptr);
        juce::String result =
            structuralState.toXmlString() + "\nproject=" + projectedProject.state().toXmlString();
        auto &history = edit->getUndoManager();
        result +=
            "\nnativeHistory=" + juce::String(history.getNumberOfUnitsTakenUpByStoredCommands()) + "/" +
            juce::String(history.getNumActionsInCurrentTransaction()) + "/" +
            history.getUndoDescriptions().joinIntoString("|") + "/" +
            history.getRedoDescriptions().joinIntoString("|") + "/" + history.getCurrentTransactionName() +
            "/" + juce::String(int(history.canUndo())) + "/" + juce::String(int(history.canRedo())) +
            "/guard=" + juce::String(int(bool(nativeHistoryGuard))) + "/" +
            juce::String(int(!nativeHistoryGuard || history.isPreparedMutationCurrent(*nativeHistoryGuard)));
        auto appendList = [&](const te::PluginList &list)
        {
            result += "\nlist=" + pointer(&list);
            for (auto *plugin : list)
                result += "/" + pointer(plugin) + ":" + plugin->itemID.toString();
        };
        appendList(edit->getMasterPluginList());
        result += "\nmasterVolume=" + pointer(baselineMasterVolume.get()) + "/" +
                  baselineMasterVolume->itemID.toString() + "/" +
                  juce::String(int(baselineMasterVolume->isEnabled()));
        for (auto *track : te::getAudioTracks(*edit))
            appendList(track->pluginList);
        result += "\ncache=";
        for (auto *plugin : edit->getPluginCache().getPlugins())
            result += "/" + pointer(plugin);
        for (const auto &plugin : originals)
            result += "\nautomatable=" + pointer(edit->automatableEditItemCache.findItem(plugin->itemID));
        for (const auto &[id, owner] : currentHosts)
            result += "\nhost=" + juce::String(juce::int64(id)) + "/" + pointer(owner.get()) + "/" +
                      owner->runtimeIdentity() + "/" + juce::String(owner->processId()) + "/" +
                      juce::String(owner->parameterRevision());
        for (const auto &[id, plugin] : effectPlugins)
            result += "\neffect=" + juce::String(juce::int64(id)) + "/" + pointer(plugin.get());
        for (const auto &[id, channel] : projectedChannels)
            result += "\nchannel=" + juce::String(juce::int64(id)) + "/" + pointer(channel.track) + "/" +
                      pointer(channel.sampler.get()) + "/" + pointer(channel.sample.get());
        for (const auto &owner : ownedHosts)
            result += "\nownedHost=" + pointer(owner.get());
        for (const auto &owner : ownedSamples)
            result += "\nownedSample=" + pointer(owner.get());
        for (const auto &owner : ownedTimelines)
            result += "\nownedTimeline=" + pointer(owner.get());
        const auto cache = samples.statistics();
        result += "\ncounts=" + juce::String(juce::int64(replacements)) + "/" +
                  juce::String(juce::int64(updates)) + "/" + juce::String(juce::int64(samplerCreations)) +
                  "/" + juce::String(juce::int64(midiUpdates)) + "/" +
                  juce::String(juce::int64(cache.preparations)) + "/" +
                  juce::String(juce::int64(cache.hits)) + "/" + juce::String(juce::int64(cache.bytes)) + "/" +
                  juce::String(juce::int64(cache.entries));
        return result;
    };
    RetainedContextFixture fixture{
        *edit,
        *context,
        originals,
        host,
        create,
        createRetainingSampler,
        [](const te::Plugin::Ptr &plugin)
        {
            auto *sampler = dynamic_cast<FixedSampler *>(plugin.get());
            if (!sampler)
                throw std::invalid_argument("Runtime identity requires the actual sample adapter");
            return sampler->runtimeIdentity();
        },
        [](const te::Plugin::Ptr &plugin)
        {
            auto *sampler = dynamic_cast<FixedSampler *>(plugin.get());
            if (!sampler)
                throw std::invalid_argument(
                    "Runtime attachment readback is CONTROL-only and requires the actual sample adapter");
            return sampler->runtimeAttachmentsControl();
        },
        [baselineCacheOwners, baselineMasterVolume](const RetainedContextFixture::Replacements &replacements)
        {
            return [baselineCacheOwners, baselineMasterVolume, replacements](const te::Plugin &plugin)
            {
                // Exact plan-owned pointers, not stable IDs or type names alone.
                bool owned = baselineCacheOwners.contains(const_cast<te::Plugin *>(&plugin)) ||
                             baselineMasterVolume.get() == &plugin;
                for (const auto &replacement : replacements)
                    owned |= replacement.get() == &plugin;
                auto refuse = [&]() -> bool
                {
                    throw std::invalid_argument(
                        ("fixedInit.unadmittedOwner: type=" + plugin.state[te::IDs::type].toString() +
                         " name=" + plugin.getName() + " id=" + plugin.itemID.toString() + " owned=" +
                         juce::String(int(owned)) + " enabled=" + juce::String(int(plugin.isEnabled())))
                            .toStdString());
                };
                if (!owned)
                    return refuse();
                if (dynamic_cast<const te::VolumeAndPanPlugin *>(&plugin))
                    return !plugin.isEnabled() || refuse(); // Native DSP is replaced by own balance.
                const bool supported = dynamic_cast<const FixedSampler *>(&plugin) ||
                                       dynamic_cast<const RemoteEffect *>(&plugin) ||
                                       dynamic_cast<const MixerSidechainSend *>(&plugin) ||
                                       dynamic_cast<const MixerSendPlugin *>(&plugin) ||
                                       dynamic_cast<const MixerMasterGate *>(&plugin) ||
                                       dynamic_cast<const ChannelBalance *>(&plugin) ||
                                       dynamic_cast<const AutomationGain *>(&plugin) ||
                                       dynamic_cast<const GuidePlugin *>(&plugin) ||
                                       dynamic_cast<const te::AuxReturnPlugin *>(&plugin) ||
                                       dynamic_cast<const te::LevelMeterPlugin *>(&plugin);
                return supported || refuse();
            };
        },
        capture,
        [](const te::Plugin::Ptr &plugin)
        {
            auto *sampler = dynamic_cast<FixedSampler *>(plugin.get());
            if (!sampler)
                throw std::invalid_argument("Sampler counter requires the actual app adapter");
            return sampler->calls();
        },
        [](const te::Plugin::Ptr &plugin)
        {
            auto *sampler = dynamic_cast<FixedSampler *>(plugin.get());
            if (!sampler)
                throw std::invalid_argument("Held-key readback requires the actual app adapter");
            return sampler->heldInputKeys();
        }};
    driver(fixture); // Source/worker/Edit owners remain pinned for the entire actual run.
}
