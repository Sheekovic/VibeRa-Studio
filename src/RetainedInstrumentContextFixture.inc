// SPDX-License-Identifier: AGPL-3.0-or-later
// Included inside namespace vibera. Closed test setup; no normal UI/MCP path.
void AudioEngine::withRetainedInstrumentContextFixture(
    Id instrument, const std::function<void(RetainedInstrumentContextFixture &)> &driver)
{
    const auto check = [](bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    };
    check(edit && timeline && hasProjection && !playing() && !compensationGraph &&
              projectedChannels.size() == 1 && currentHosts.size() == 1 && effectPlugins.empty() &&
              (!edit->getTransport().getCurrentPlaybackContext() ||
               (productionPluginProjection &&
                productionPluginProjection->context == edit->getTransport().getCurrentPlaybackContext())),
          "Retained instrument fixture requires one stopped external Channel and its exact ordinary prepared "
          "context");
    auto &projection = projectedChannels.begin()->second;
    const auto channel = projectedChannels.begin()->first;
    auto *source = dynamic_cast<FixedSampler *>(projection.sampler.get());
    const auto *model = projectedProject.channel(channel);
    const auto found = currentHosts.find(instrument);
    check(source && model && model->instrument && model->instrument->id == instrument && !model->audioClip &&
              found != currentHosts.end() && found->second && source->inputLane() && source->typingLane(),
          "Actual projected Channel, prepared physical/typing lanes and exact instrument Host required");
    const auto host = found->second;
    check(engine.getDeviceManager().getSampleRate() == 48000 &&
              engine.getDeviceManager().getBlockSize() == 256 && host->alive() && !host->offline() &&
              host->processingQuantum() == 256 && host->busLayout() == PluginBusRequest{} &&
              pluginHostConfigurationCurrent(*model->instrument, host, 48000, 256),
          "Same exact live descriptor/worker, native bus/rate/quantum and unchanged48k256 device required");
    const auto original = projection.sampler;
    setMidiTarget(channel); // Actual app routing selects both original lanes; ONE ordinary context.
    auto *context = edit->getTransport().getCurrentPlaybackContext();
    check(context && !source->baseClassNeedsInitialising() && source->runtimeAttachmentsControl() == 1,
          "Original external instrument graph must be genuinely initialized");
    const auto initialRuntime = source->runtimeIdentity();
    const auto cacheOwners = edit->getPluginCache().getPlugins();
    const auto masterVolume = edit->getMasterVolumePlugin();
    auto &history = edit->getUndoManager();
    auto historyGuard = history.canUndo()   ? history.prepareUndoMutation()
                        : history.canRedo() ? history.prepareRedoMutation()
                                            : nullptr;
    const auto asSampler = [&](const te::Plugin::Ptr &plugin) -> FixedSampler *
    {
        auto *sampler = dynamic_cast<FixedSampler *>(plugin.get());
        check(sampler && &sampler->edit == edit.get() && sampler->itemID == original->itemID &&
                  sampler->runtimeIdentity() == initialRuntime,
              "Exact closed same-Edit/source/runtime shell required");
        return sampler;
    };
    const auto create = [&](const te::Plugin::Ptr &incumbent, const juce::ValueTree &state)
    {
        auto *active = asSampler(incumbent);
        check(!active->baseClassNeedsInitialising(),
              "Retaining factory requires actual initialized source, not retired registered shell");
        auto candidate = FixedSampler::createDetachedGraph(*edit, state);
        auto *prepared = dynamic_cast<FixedSampler *>(candidate.get());
        const auto admitted = prepared->shareCompatibleRuntimeFrom(*active);
        if (admitted.failed())
            throw std::runtime_error(admitted.getErrorMessage().toStdString());
        return candidate;
    };
    const auto fixed = [&, cacheOwners, masterVolume](const te::Plugin::Ptr &candidate)
    {
        if (candidate)
            asSampler(candidate);
        return [cacheOwners, masterVolume, candidate, original, initialRuntime](const te::Plugin &plugin)
        {
            const bool owned = cacheOwners.contains(const_cast<te::Plugin *>(&plugin)) ||
                               masterVolume.get() == &plugin || candidate.get() == &plugin;
            const auto refuse = [&]() -> bool
            { throw std::invalid_argument("Unadmitted original instrument graph owner/type"); };
            if (!owned)
                return refuse();
            if (auto *sampler = dynamic_cast<const FixedSampler *>(&plugin))
                return ((&plugin == original.get() || &plugin == candidate.get()) &&
                        sampler->runtimeIdentity() == initialRuntime) ||
                       refuse();
            if (dynamic_cast<const te::VolumeAndPanPlugin *>(&plugin))
                return !plugin.isEnabled() || refuse();
            const bool supported = dynamic_cast<const MixerSendPlugin *>(&plugin) ||
                                   dynamic_cast<const MixerMasterGate *>(&plugin) ||
                                   dynamic_cast<const ChannelBalance *>(&plugin) ||
                                   dynamic_cast<const AutomationGain *>(&plugin) ||
                                   dynamic_cast<const GuidePlugin *>(&plugin) ||
                                   dynamic_cast<const te::AuxReturnPlugin *>(&plugin) ||
                                   dynamic_cast<const te::LevelMeterPlugin *>(&plugin);
            return supported || refuse();
        };
    };
    const auto pointer = [](const void *p)
    { return juce::String::toHexString(juce::int64(reinterpret_cast<std::uintptr_t>(p))); };
    const auto capture = [&, cacheOwners, masterVolume, original, host, pointer]
    {
        auto state = edit->state.createCopy();
        state.getChildWithName(te::IDs::TRANSPORT).removeProperty(te::IDs::position, nullptr);
        auto &undo = edit->getUndoManager();
        juce::String result =
            state.toXmlString() + "\nproject=" + projectedProject.state().toXmlString() +
            "\nnativeHistory=" + juce::String(undo.getNumberOfUnitsTakenUpByStoredCommands()) + "/" +
            juce::String(undo.getNumActionsInCurrentTransaction()) + "/" +
            undo.getUndoDescriptions().joinIntoString("|") + "/" +
            undo.getRedoDescriptions().joinIntoString("|") + "/" + undo.getCurrentTransactionName() + "/" +
            juce::String(int(!historyGuard || undo.isPreparedMutationCurrent(*historyGuard)));
        const auto list = [&](const te::PluginList &plugins)
        {
            result += "\nlist=" + pointer(&plugins);
            for (auto *p : plugins)
                result += "/" + pointer(p) + ":" + p->itemID.toString();
        };
        list(edit->getMasterPluginList());
        for (auto *track : te::getAudioTracks(*edit))
            list(track->pluginList);
        result += "\ncache=";
        for (auto *p : edit->getPluginCache().getPlugins())
            result += "/" + pointer(p);
        result += "\nregistered=" + pointer(edit->automatableEditItemCache.findItem(original->itemID)) + "/" +
                  pointer(original.get());
        result +=
            "\nmaster=" + pointer(masterVolume.get()) + "/" + juce::String(int(masterVolume->isEnabled()));
        for (const auto &[id, value] : currentHosts)
            result += "\nhost=" + juce::String(juce::int64(id)) + "/" + pointer(value.get()) + "/" +
                      value->runtimeIdentity() + "/" + juce::String(value->processId());
        for (const auto &value : ownedHosts)
            result += "\nownedHost=" + pointer(value.get());
        for (const auto &value : ownedSamples)
            result += "\nownedSample=" + pointer(value.get());
        for (const auto &value : ownedTimelines)
            result += "\nownedTimeline=" + pointer(value.get());
        for (const auto &[id, value] : projectedChannels)
            result += "\nchannel=" + juce::String(juce::int64(id)) + "/" + pointer(value.track) + "/" +
                      pointer(value.sampler.get()) + "/" + pointer(value.sample.get());
        const auto cache = samples.statistics();
        result += "\ncounts=" + juce::String(juce::int64(replacements)) + "/" +
                  juce::String(juce::int64(updates)) + "/" + juce::String(juce::int64(samplerCreations)) +
                  "/" + juce::String(juce::int64(cache.preparations)) + "/" +
                  juce::String(juce::int64(cache.hits));
        // Parameter revision, cursor, meters and processing counters advance
        // legitimately. They are verified separately, never frozen as owners.
        return result;
    };
    RetainedInstrumentContextFixture fixture{
        *edit,
        *context,
        original,
        host,
        create,
        [asSampler](const te::Plugin::Ptr &p) { return asSampler(p)->runtimeIdentity(); },
        [asSampler](const te::Plugin::Ptr &p) { return asSampler(p)->runtimeAttachmentsControl(); },
        [asSampler](const te::Plugin::Ptr &p) { return asSampler(p)->calls(); },
        [asSampler](const te::Plugin::Ptr &p) { return asSampler(p)->heldInputKeys(); },
        fixed,
        capture,
        [&, asSampler]
        {
            const auto current = projectedChannels.at(channel).sampler;
            asSampler(current); // Same exact source ID/Runtime, not registered-original lookup.
            return current;
        }};
    driver(fixture); // This method pins all original cache/Host/Edit owners throughout.
}
