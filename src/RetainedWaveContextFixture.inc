// SPDX-License-Identifier: AGPL-3.0-or-later
// Included inside namespace vibera. Internal original WAV routing experiment.
void AudioEngine::withRetainedWaveContextFixture(
    Id channel, const std::function<void(RetainedWaveContextFixture &)> &driver)
{
    const auto check = [](bool ok, const char *message)
    {
        if (!ok)
            throw std::runtime_error(message);
    };
    check(edit && timeline && hasProjection && !playing() && !compensationGraph &&
              projectedChannels.size() == 1 && projectedAudioClips.size() == 1 && currentHosts.empty() &&
              effectPlugins.empty() && !projectedPatternMode &&
              !edit->getTransport().getCurrentPlaybackContext(),
          "Wave fixture requires one stopped actual Song AudioClip before context allocation");
    const auto found = projectedChannels.find(channel);
    const auto *model = projectedProject.channel(channel);
    check(found != projectedChannels.end() && model && model->audioClip && !model->instrument &&
              !found->second.sampler && model->mixer == 1 && projectedInserts.contains(1) &&
              projectedInserts.contains(2),
          "Exact real Wave clip and both distinct native Mixer buses required");
    check(engine.getDeviceManager().getSampleRate() == 48000 &&
              engine.getDeviceManager().getBlockSize() == 256,
          "Original Wave clock requires actual unchanged48k256 configuration");
    auto *source = found->second.track;
    const auto clip = projectedAudioClips.begin()->second;
    check(source && clip && clip->getTrack() == source && clip->getAudioFile().isValid(),
          "One actual native WaveAudioClip must reference the independently written source");
    // Closed original16384-frame fixture admission, CONTROL only. A live
    // Wave node advertises ready before its asynchronous mmap cache is ready.
    // Establish an actual data-read condition before constructing its graph;
    // never consume/discard an audio callback or weaken the frame0 oracle.
    const auto cachedFile = clip->getAudioFile();
    check(cachedFile.getFile() == juce::File(model->source),
          "Closed Wave fixture requires original source, not an implicit cached proxy");
    const auto& fileInfo = cachedFile.getInfo();
    check(fileInfo.sampleRate == 48000. && fileInfo.numChannels == 2 &&
              fileInfo.lengthInSamples == 16384,
          "Closed Wave cache admission requires exact original48k/stereo/16384 fixture");
    auto& cache = engine.getAudioFileManager().cache;
    const bool mappedFirstBefore = cache.hasMappedReader(cachedFile, 0);
    const bool mappedLastBefore = cache.hasMappedReader(cachedFile, 16383);
    const bool cacheMissBeforePreparation = cache.hasCacheMissed(false);
    const auto preparationBegan = juce::Time::getHighResolutionTicks();
    auto preparedReader = cache.createReader(cachedFile);
    check(preparedReader && preparedReader->getSampleRate() == 48000. &&
              preparedReader->getNumChannels() == 2,
          "Actual native cache reader metadata must be valid before Wave graph preparation");
    juce::AudioBuffer<float> cachedPcm(2, 16384), directPcm(2, 16384);
    preparedReader->setReadPosition(0);
    // Bounded CONTROL resource admission, never a callback timeout/buffer change.
    // Same supported cache API as upstream's own independent reader tests.
    const bool cacheReadCompleted = preparedReader->readSamples(
        16384, cachedPcm, te::ChannelConfiguration::stereo(), 0,
        te::ChannelConfiguration::stereo(), 1000);
    const double preparationMs = 1000. * juce::Time::highResolutionTicksToSeconds(
        juce::Time::getHighResolutionTicks() - preparationBegan);
    const bool mappedFirstAfter = cache.hasMappedReader(cachedFile, 0);
    const bool mappedLastAfter = cache.hasMappedReader(cachedFile, 16383);
    const bool cacheMissAfterPreparation = cache.hasCacheMissed(false);
    if (!cacheReadCompleted || !mappedFirstAfter || !mappedLastAfter)
        throw std::runtime_error(("Wave CONTROL cache admission failed: read=" +
            juce::String(int(cacheReadCompleted)) + ";mappedFirst=" + juce::String(int(mappedFirstAfter)) +
            ";mappedLast=" + juce::String(int(mappedLastAfter)) +
            ";cacheMiss=" + juce::String(int(cacheMissAfterPreparation)) +
            ";elapsedMs=" + juce::String(preparationMs, 3)).toStdString());
    std::unique_ptr<juce::AudioFormatReader> directReader(
        te::AudioFileUtils::createReaderFor(engine, cachedFile.getFile()));
    check(directReader && directReader->sampleRate == 48000. && directReader->numChannels == 2 &&
              directReader->lengthInSamples == 16384 &&
              directReader->read(&directPcm, 0, 16384, 0, true, true),
          "Independently decode actual source for native-cache readiness identity");
    for (int channelIndex = 0; channelIndex < 2; ++channelIndex)
        for (int frame = 0; frame < 16384; ++frame)
            check(cachedPcm.getSample(channelIndex, frame) == directPcm.getSample(channelIndex, frame),
                  "Every prepared cache frame must equal the independently decoded source");
    preparedReader->setReadPosition(0); // This independent reader never advances the graph's reader.
    // preparedReader stays owned until driver returns, pinning the actual same
    // CachedFile used by all native graph reader instances across replacements.
    edit->getTransport().ensureContextAllocated();
    auto *context = edit->getTransport().getCurrentPlaybackContext();
    check(context && context->getNodePlayHead(), "Actual ordinary native AudioClip context allocated");
    const auto owners = edit->getPluginCache().getPlugins();
    const auto master = edit->getMasterVolumePlugin();
    const auto tracks = te::getAudioTracks(*edit);
    const auto inserts = projectedInserts;
    const auto admitted = [owners, master](const te::Plugin &plugin)
    {
        const auto refuse = []() -> bool
        { throw std::invalid_argument("Unadmitted Wave routing plugin owner/type"); };
        if (!owners.contains(const_cast<te::Plugin *>(&plugin)) && master.get() != &plugin)
            return refuse();
        if (dynamic_cast<const te::VolumeAndPanPlugin *>(&plugin))
            return !plugin.isEnabled() || refuse();
        return bool(dynamic_cast<const ChannelBalance *>(&plugin) ||
                    dynamic_cast<const AutomationGain *>(&plugin) ||
                    dynamic_cast<const MixerSendPlugin *>(&plugin) ||
                    dynamic_cast<const MixerMasterGate *>(&plugin) ||
                    dynamic_cast<const GuidePlugin *>(&plugin) ||
                    dynamic_cast<const te::AuxReturnPlugin *>(&plugin) ||
                    dynamic_cast<const te::LevelMeterPlugin *>(&plugin)) ||
               refuse();
    };
    const auto makeOverlay =
        [&, tracks, inserts, source,
         admitted](int target, const std::shared_ptr<RetainedWaveObservation> &observation, bool fail)
    {
        check((target == 1 || target == 2) && bool(observation),
              "Only existing distinct original Wave route buses admitted");
        using Routing = te::GraphOutputRouting;
        std::vector<Routing::Route> routes;
        for (auto *track : tracks)
        {
            if (track == source)
                routes.push_back(
                    {track->itemID, Routing::Target::destinationTrack, inserts.at(target)->itemID});
            else
            {
                check(std::any_of(inserts.begin(), inserts.end(),
                                  [track](const auto &p) { return p.second == track; }),
                      "Fixed Wave graph universe must contain only original source and insert tracks");
                routes.push_back({track->itemID, Routing::Target::defaultWaveDevice, {}});
            }
        }
        auto overlay = std::make_shared<te::EditPlaybackContext::GraphOverlay>();
        overlay->outputRouting = Routing::prepare(*edit, std::move(routes));
        overlay->fixedInitialisationAdmission = admitted;
        // No resolver or timeline replacement: unchanged ORIGINAL control owners
        // attach at exact same configuration. This qualifies routing/decoder
        // continuity only, not the private-document application transaction.
        overlay->lastStage =
            [observation, fail, clipId = clip->itemID](
                te::OutputDevice &, const te::CreateNodeParams &,
                std::unique_ptr<tracktion::graph::Node> root) -> std::unique_ptr<tracktion::graph::Node>
        {
            if (fail)
                throw std::runtime_error("Original Wave late factory refusal");
            const auto checkWaveId = [clipId](tracktion::graph::Node &node)
            {
                if (node.getNodeProperties().nodeID != std::size_t(clipId.getRawID()))
                    throw std::runtime_error("Native Wave node must retain exact actual clip identity");
            };
            std::vector<tracktion::graph::Node *> pending{root.get()}, seen;
            while (!pending.empty())
            {
                auto *node = pending.back();
                pending.pop_back();
                if (std::find(seen.begin(), seen.end(), node) != seen.end())
                    continue;
                seen.push_back(node);
                if (dynamic_cast<te::WaveNode *>(node))
                {
                    ++observation->waveNodes;
                    checkWaveId(*node);
                }
                if (dynamic_cast<te::WaveNodeRealTime *>(node))
                {
                    ++observation->realTimeWaveNodes;
                    checkWaveId(*node);
                }
                for (auto *input : node->getDirectInputNodes())
                    pending.push_back(input);
                // Native CombiningNode owns clip decoders internally rather
                // than exposing them as scheduler input edges.
                for (auto *internal : node->getInternalNodes())
                    pending.push_back(internal);
            }
            class Marker final : public tracktion::graph::Node
            {
                std::unique_ptr<Node> input;
                std::shared_ptr<RetainedWaveObservation> observation;
                bool first = true;

              public:
                Marker(std::unique_ptr<Node> value, std::shared_ptr<RetainedWaveObservation> state)
                    : input(std::move(value)), observation(std::move(state))
                {
                    ++observation->constructed;
                    setOptimisations(
                        {tracktion::graph::ClearBuffers::no, tracktion::graph::AllocateAudioBuffer::no});
                }
                tracktion::graph::NodeProperties getNodeProperties() override
                {
                    auto p = input->getNodeProperties();
                    p.hasMidi = false;
                    tracktion::hash_combine(p.nodeID, std::size_t(0x776176652d637478ULL));
                    return p;
                }
                std::vector<Node *> getDirectInputNodes() override
                {
                    return {input.get()};
                }
                bool isReadyToProcess() override
                {
                    return input->hasProcessed();
                }
                void process(ProcessContext &pc) override
                {
                    if (first)
                    {
                        observation->firstReference = pc.referenceSampleRange.getStart();
                        first = false;
                    }
                    ++observation->calls;
                    setAudioOutput(input.get(), input->getProcessedOutput().audio);
                }
            };
            return std::make_unique<Marker>(std::move(root), observation);
        };
        return overlay;
    };
    const auto pointer = [](const void *p)
    { return juce::String::toHexString(juce::int64(reinterpret_cast<std::uintptr_t>(p))); };
    auto history = edit->getUndoManager().canUndo() ? edit->getUndoManager().prepareUndoMutation() : nullptr;
    const auto capture = [&, owners, master, tracks, source, clip, pointer]
    {
        auto state = edit->state.createCopy();
        state.getChildWithName(te::IDs::TRANSPORT).removeProperty(te::IDs::position, nullptr);
        juce::String result = state.toXmlString() + "\nproject=" + projectedProject.state().toXmlString() +
                              "\nsource=" + pointer(source) + "/" + pointer(clip.get()) + "/" +
                              clip->itemID.toString() + "\nmaster=" + pointer(master.get());
        for (auto *track : tracks)
        {
            result += "\ntrack=" + pointer(track) + "/" + track->itemID.toString() + "/" +
                      pointer(track->getOutput().getDestinationTrack());
            for (auto *plugin : track->pluginList)
                result += "/" + pointer(plugin) + ":" + plugin->itemID.toString();
        }
        for (auto *plugin : edit->getMasterPluginList())
            result += "\nmasterPlugin=" + pointer(plugin);
        for (auto *plugin : edit->getPluginCache().getPlugins())
            result += "\ncache=" + pointer(plugin);
        result += "\ncachePreparation=CONTROL;frames=16384;timeoutMs=1000;completed=" +
                  juce::String(int(cacheReadCompleted)) + ";elapsedMs=" + juce::String(preparationMs, 6) +
                  ";mappedBefore=" + juce::String(int(mappedFirstBefore)) + "/" +
                  juce::String(int(mappedLastBefore)) + ";mappedAfter=" + juce::String(int(mappedFirstAfter)) +
                  "/" + juce::String(int(mappedLastAfter)) + ";missBefore=" +
                  juce::String(int(cacheMissBeforePreparation)) + ";missAfter=" +
                  juce::String(int(cacheMissAfterPreparation)) + ";reader=" + pointer(preparedReader.get());
        auto &undo = edit->getUndoManager();
        result += "\nhistory=" + juce::String(undo.getNumberOfUnitsTakenUpByStoredCommands()) + "/" +
                  undo.getUndoDescriptions().joinIntoString("|") + "/" +
                  undo.getRedoDescriptions().joinIntoString("|") + "/" +
                  juce::String(int(!history || undo.isPreparedMutationCurrent(*history)));
        result += "\ncounts=" + juce::String(juce::int64(replacements)) + "/" +
                  juce::String(juce::int64(updates)) + "/" + juce::String(juce::int64(samplerCreations)) +
                  "/" + juce::String(juce::int64(ownedHosts.size()));
        return result;
    };
    RetainedWaveContextFixture fixture{*edit, *context, *source, makeOverlay, capture};
    driver(fixture); // Original source/clip/native cache owners remain pinned.
}
