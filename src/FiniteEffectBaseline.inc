// SPDX-License-Identifier: AGPL-3.0-or-later
// ORIGINAL internal finite-fixture baseline prerequisite. Included only after
// AudioEngine's real native/plugin/preparation types are complete. CONTROL only.
// ORIGINAL full-Q sample authority; no Host membership/transport API is used.
// The actual native Session authenticates the physical manager/serial before
// begin. These callbacks authenticate the one real fixed sample source and its
// exact ProcessState AFTER processing. Mutable fields have one DSP writer only.
class FiniteSampleObserver final : public te::SourceBurstSession::Observer
{
  public:
    enum class Fault : std::uint32_t
    {
        none,
        invalidSpan,
        sourceClock,
        sourceCount,
        nativeAbort,
        ownership,
        exhausted
    };
    const te::SourceBurstSession::Configuration configuration;
    explicit FiniteSampleObserver(te::SourceBurstSession::Configuration c) : configuration(c) {}
    void bindScope(te::ProcessState &actual)
    {
        if (scope)
        {
            if (scope != &actual)
                throw std::invalid_argument("finite.local.scope: Exact original ProcessState required");
            return; // NO same-value CONTROL store while DSP reads this immutable binding.
        }
        scope = &actual;
    }
    void seal(FixedSampler &actual, std::int64_t reference,
              std::shared_ptr<experimental::SourceExchangeContinuity> root,
              std::shared_ptr<experimental::NativeMidiFaultPublisher> midi)
    {
        if (sealed || !scope || actual.calls() != 0 || actual.runtimeEpochControl() == 0 ||
            !actual.runtimeAttachmentsControl() || !actual.preparedSource() || reference < 0 || !root ||
            !midi)
            throw std::invalid_argument(
                "finite.local.source: Exact fresh prepared sample Runtime/PCM required");
        source = &actual;
        runtime = actual.runtimeOwnerControl();
        sample = actual.preparedSource();
        sourceEpoch = actual.runtimeEpochControl();
        nextReference = reference;
        continuity = std::move(root);
        nativeMidi = std::move(midi);
        sealed = true;
    }
    bool matchesControl(const FixedSampler &actual) const
    {
        return !sealed || (source == &actual && runtime == actual.runtimeOwnerControl() &&
                           sample == actual.preparedSource() && sourceEpoch == actual.runtimeEpochControl());
    }
    bool begin(const te::SourceBurstSession::Outcome &out) noexcept override
    {
        if (fault != Fault::none)
            return false;
        if (!sealed || !source || !scope)
            return fail(Fault::ownership);
        // configured/max256 alone does NOT admit tiny physical callbacks. This
        // ordinary1Q finite proof accepts one actual full256 slice ONLY.
        if (open || out.generation != configuration.generation ||
            out.deviceEpoch != configuration.deviceEpoch || !out.metadataKnown || !out.offsetKnown ||
            !out.scopeOpen || out.failed || out.retired || !out.physicalSerial ||
            out.physicalSerial <= serial || out.physicalFrames != 256 || out.sourceQuota != 256 ||
            out.deviceOffset != 0 || out.sourceProcessed != 0 || out.sliceDeviceFrames != 256 ||
            out.sliceSourceFrames != 256)
            return fail(Fault::invalidSpan); // BEFORE native nextBlock/input/reference/Host work.
        if (begins == std::numeric_limits<std::uint64_t>::max())
            return fail(Fault::exhausted);
        serial = out.physicalSerial;
        beforeCalls = source->calls();
        open = true;
        ++begins;
        publish();
        return true;
    }
    bool end(const te::SourceBurstSession::Outcome &out) noexcept override
    {
        if (fault != Fault::none)
            return false;
        if (!open || out.generation != configuration.generation || out.physicalSerial != serial ||
            out.physicalFrames != 256 || out.deviceOffset != 256 || out.sourceProcessed != 256 ||
            out.failed || out.retired || !out.scopeOpen)
            return fail(Fault::invalidSpan);
        if (beforeCalls == std::numeric_limits<std::uint64_t>::max() || source->calls() != beforeCalls + 1)
            return fail(Fault::sourceCount);
        // Read only the SAME ProcessState's plain DSP-owned current fields.
        // No PlayHead getter, seq-lock load/store or CONTROL metadata read.
        const bool playing = source->playingSeen();
        if (scope->sampleRate != 48000 || scope->playbackSpeedRatio != 1 || scope->numSamples != 256 ||
            scope->referenceSampleRange.getStart() != nextReference ||
            scope->referenceSampleRange.getLength() != 256 ||
            scope->timelineSampleRange.getStart() != std::int64_t(timelineFrames) ||
            scope->timelineSampleRange.getLength() != (playing ? 256 : 0) ||
            source->timeSeen() != scope->editTimeRange.getStart().inSeconds() ||
            scope->editTimeRange.getStart().inSeconds() != double(timelineFrames) / 48000. ||
            continuity->observation() != experimental::SourceExchangeContinuity::Fault::none ||
            !continuity->hasSuccessfulBaseline() ||
            nativeMidi->observation() != experimental::NativeMidiFaultPublisher::Fault::none)
            return fail(Fault::sourceClock);
        if (ends == std::numeric_limits<std::uint64_t>::max() ||
            frames > std::uint64_t(std::numeric_limits<std::int64_t>::max()) - 256 ||
            nextReference > std::numeric_limits<std::int64_t>::max() - 256)
            return fail(Fault::exhausted);
        lastReference = nextReference;
        nextReference += 256;
        frames += 256;
        if (playing)
            timelineFrames += 256;
        ++ends;
        open = false;
        publish();
        return true;
    }
    void abort(const te::SourceBurstSession::Outcome &) noexcept override
    {
        if (aborts)
            return; // Sticky/idempotent; later failed callbacks cannot wrap publication.
        if (fault == Fault::none)
            fault = Fault::nativeAbort;
        open = false;
        aborts = 1;
        publish();
    }
    bool read(experimental::AppMixerAudioPorts::FiniteSourceObservation &out) const noexcept
    {
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            const auto version = publication.load(std::memory_order_seq_cst);
            if (version & 1)
                continue;
            std::array<std::uint64_t, 8> copy{};
            for (std::size_t i = 0; i < copy.size(); ++i)
                copy[i] = fields[i].load(std::memory_order_seq_cst);
            if (publication.load(std::memory_order_seq_cst) != version)
                continue;
            out.generation = configuration.generation;
            out.deviceEpoch = configuration.deviceEpoch;
            out.fault = std::uint32_t(copy[0]);
            out.open = bool(copy[1]);
            out.begins = copy[2];
            out.ends = copy[3];
            out.aborts = copy[4];
            out.frames = copy[5];
            out.serial = copy[6];
            out.referenceStart = std::int64_t(copy[7]);
            out.runtime = runtime.get(); // Immutable CONTROL seal; read never copies a Runtime owner.
            return true;
        }
        return false;
    }

  private:
    // The actual fixed source Plugin lives in its Edit/cache and every prepared
    // native graph pins it. Only Gate shells change; source membership is fixed.
    // No PluginPtr survives the Edit. These raw source/scope pointers are used
    // ONLY from the actual installed context's callbacks, never CONTROL readback.
    FixedSampler *source = nullptr;
    te::ProcessState *scope = nullptr;
    std::shared_ptr<const void> runtime;
    std::shared_ptr<const Sample> sample;
    std::uint64_t sourceEpoch = 0;
    std::shared_ptr<experimental::SourceExchangeContinuity> continuity;
    std::shared_ptr<experimental::NativeMidiFaultPublisher> nativeMidi;
    bool sealed = false, open = false;
    Fault fault = Fault::none;
    std::uint64_t serial = 0, begins = 0, ends = 0, aborts = 0, frames = 0, timelineFrames = 0,
                  beforeCalls = 0;
    std::int64_t nextReference = 0, lastReference = 0;
    std::atomic<std::uint64_t> publication{0};
    std::array<std::atomic<std::uint64_t>, 8> fields{};
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    bool fail(Fault reason) noexcept
    {
        if (fault == Fault::none)
            fault = reason;
        publish();
        return false;
    }
    void publish() noexcept
    {
        publication.fetch_add(1, std::memory_order_seq_cst);
        const std::uint64_t values[]{
            std::uint64_t(fault),        std::uint64_t(open), begins, ends, aborts, frames, serial,
            std::uint64_t(lastReference)};
        for (std::size_t i = 0; i < fields.size(); ++i)
            fields[i].store(values[i], std::memory_order_seq_cst);
        publication.fetch_add(1, std::memory_order_seq_cst);
    }
};

struct AudioEngine::FiniteEffectBaselineState
{
    const Controller *controller;
    const juce::String controllerIdentity;
    const std::uint64_t revision;
    const Project project;
    const Id anchor;
    const PluginSpec specification;
    const juce::File module;
    const juce::String moduleHash;
    const juce::AudioIODevice *device;
    const juce::AudioDeviceManager::PhysicalCallbackReader physical;
    const std::uint64_t deviceEpoch;
    const te::SourceBurstSession::Configuration sourceConfiguration;
    const std::shared_ptr<FiniteSampleObserver> observer;
    te::SourceBurstSession::Ptr session;
    std::shared_ptr<experimental::AppMixerAudioPorts> ports;
    std::shared_ptr<PluginHost> host; // CONTROL pin, no callback copies or releases.
    PluginLatencyStamp native;
    unsigned parameterRevision = 0;
    juce::String instance;
    juce::uint32 workerPid = 0;
    unsigned resetGeneration = 0;
    bool editCreated = false;
    std::atomic<bool> invalidated{false};
    static_assert(std::atomic<bool>::is_always_lock_free);
    FiniteEffectBaselineState(const Controller &c, Id id, juce::File path, juce::String hash,
                              const juce::AudioIODevice *d,
                              juce::AudioDeviceManager::PhysicalCallbackReader reader, std::uint64_t epoch,
                              std::uint64_t generation)
        : controller(&c), controllerIdentity(c.projectIdentity), revision(c.revision), project(c.project),
          anchor(id), specification(*c.project.plugin(id)), module(std::move(path)),
          moduleHash(std::move(hash)), device(d), physical(std::move(reader)), deviceEpoch(epoch),
          sourceConfiguration{epoch, generation, 48000, 256, 256, 256, 1000000},
          observer(std::make_shared<FiniteSampleObserver>(sourceConfiguration))
    {
    }

    juce::Result install(te::Edit &candidate, te::DeviceManager &manager)
    {
        const auto fail = [&](const juce::String &detail)
        {
            invalidated.store(true, std::memory_order_release);
            return juce::Result::fail(detail);
        };
        try
        {
            if (candidate.initialSourceGeneration != sourceConfiguration.generation ||
                !candidate.closedNativeIO || !session)
                return fail("finite.local.owner: Exact actual prepared source authority required");
            candidate.getTransport().ensureContextAllocated();
            auto *context = candidate.getTransport().getCurrentPlaybackContext();
            if (!context || context->nativeIOAttachmentRefused() || context->sourceLifecycleInvalidated() ||
                context->initialSourcePhase() !=
                    te::EditPlaybackContext::InitialSourcePhase::awaitingSession ||
                !context->getNodePlayHead())
                return fail("finite.local.context: Fresh pending LOCAL context required");
            FixedSampler *source = nullptr;
            for (auto *track : te::getAudioTracks(candidate))
                for (auto *plugin : track->pluginList)
                    if (auto *found = dynamic_cast<FixedSampler *>(plugin))
                    {
                        if (source)
                            return fail("finite.local.source: Unique original sample owner required");
                        source = found;
                    }
            if (!source)
                return fail("finite.local.source: Actual prepared sample is absent");
            observer->seal(*source, context->getNodePlayHead()->getReferenceSampleRange().getEnd(),
                           ports->continuity, ports->nativeMidiFault);
            if (manager.installInitialSourceBurstSession(*context, session) !=
                te::SourceBurstSession::Status::pendingFirstSerial)
                return fail("finite.local.installation: Actual source Session refused before first callback");
            return juce::Result::ok();
        }
        catch (...)
        {
            invalidated.store(true, std::memory_order_release); // BEFORE diagnostic allocation.
            return juce::Result::fail("finite.local.preparationFailed: No active initial sample authority");
        }
    }
    juce::Result bindFreshHost(const std::map<Id, std::shared_ptr<PluginHost>> &hosts)
    {
        const auto found = hosts.find(anchor);
        if (host || hosts.size() != 1 || found == hosts.end() || !found->second || !found->second->alive() ||
            found->second->offline() || !found->second->matches(specification, 48000, 256) ||
            found->second->processingQuantum() != 256 || found->second->transportLatencySamples() != 256 ||
            found->second->latencySamples() != 287 || found->second->declaredLatency().nativeSamples != 31 ||
            !found->second->declaredLatency().valid(256) ||
            found->second->busLayout() != PluginBusRequest{} || found->second->statistics().submitted != 0 ||
            found->second->packetFrame() != 0 || found->second->recoveryState() != specification.state)
            return juce::Result::fail(
                "finite.baseline.host: Fresh ordinary1Q restored stereo native31 Host required");
        PluginParameterCatalogue catalogue;
        if (!found->second->readInitialParameterCatalogue(catalogue) || catalogue.error ||
            catalogue.parameters.size() != 3)
            return juce::Result::fail(
                "finite.baseline.state: Complete original immutable restored catalogue required");
        const auto value = [&](std::uint32_t id) -> std::optional<float>
        {
            for (const auto &p : catalogue.parameters)
                if (p.id == id)
                    return p.value;
            return {};
        };
        // This is the known ORIGINAL finite FIFO fixture's exact fixed mode,
        // not an inference that all plugins advertising31 latency have finite tails.
        if (value(17) != std::optional<float>(1.f / 3.f) || value(71) != std::optional<float>(0.f) ||
            value(113) != std::optional<float>(0.f))
            return juce::Result::fail(
                "finite.baseline.state: Original31/unity/no-sidechain/no-prepare-mutation state required");
        auto workerInstance = found->second->runtimeIdentity();
        const auto pid = found->second->processId();
        if (workerInstance.isEmpty() || !pid)
            return juce::Result::fail("finite.baseline.host: Live exact worker instance required");
        instance.swapWith(workerInstance);
        workerPid = pid;
        resetGeneration = found->second->midiResetGeneration();
        host = found->second;
        native = host->declaredLatency();
        parameterRevision = host->parameterRevision();
        return juce::Result::ok();
    }
    bool exactHost() const
    {
        return host && host->alive() && !host->offline() && host->matches(specification, 48000, 256) &&
               host->runtimeIdentity() == instance && host->processId() == workerPid &&
               host->midiResetGeneration() == resetGeneration && host->busLayout() == PluginBusRequest{} &&
               host->processingQuantum() == 256 && host->transportLatencySamples() == 256 &&
               host->latencySamples() == 287 && host->declaredLatency() == native &&
               host->parameterRevision() == parameterRevision &&
               host->recoveryState() == specification.state && !host->pendingParameterRequests() &&
               !host->editorBusy() && !host->editorWindowState().exists;
    }
};

juce::Result AudioEngine::prepareFiniteEffectBaseline(const Controller &controller, Id anchor,
                                                      const juce::File &originalModule,
                                                      const juce::String &sourceBuiltHash)
{
    const auto *messages = juce::MessageManager::getInstanceWithoutCreating();
    // Only the ORIGINAL fixture friend calls this API with its trusted build
    // artifact identity. There is no UI/MCP/provider registration or general
    // caller assertion of finite tails. A self-supplied name/latency is insufficient.
    if (!messages || !messages->isThisTheMessageThread() || finiteEffectBaseline || initialSourceTransport ||
        edit || hasProjection || timeline || compensationGraph || !currentHosts.empty() ||
        !ownedHosts.empty() || !ownedSamples.empty() || !effectPlugins.empty() ||
        !projectedChannels.empty() || !projectedInserts.empty() || !projectedAudioClips.empty() ||
        preparation[0] || preparation[1] || !retiredPreparation.empty() || capture || worker || renderEdit ||
        deferredDocumentProjection || deferredHostProjection || deferredSampleProjection || !channelRouting ||
        channelRouting->effective || channelRouting->pendingGraph || !channelRouting->prepared.empty() ||
        auditionSample || !engine.getActiveEdits().getEdits().isEmpty() || midiInput.capture.active.load() ||
        typingInput.capture.active.load() || controller.projectIdentity.isEmpty())
        return juce::Result::fail("finite.baseline.owner: Fresh exact CONTROL owner required");
    const auto &p = controller.project;
    if (auto valid = p.validate(); valid.failed())
        return valid;
    const auto *spec = p.plugin(anchor);
    auto xml = spec ? juce::parseXML(spec->descriptor) : nullptr;
    juce::PluginDescription description;
    if (!spec || !xml || !description.loadFromXml(*xml) || !originalModule.existsAsFile() ||
        sourceBuiltHash.length() != 64 || juce::SHA256(originalModule).toHexString() != sourceBuiltHash ||
        juce::File(description.fileOrIdentifier) != originalModule ||
        description.pluginFormatName != "VST3" || description.isInstrument ||
        description.numInputChannels != 2 || description.numOutputChannels != 2 || spec->instrument ||
        spec->bypass || spec->wet != 1 || spec->state.isEmpty() || !spec->automationBindings.empty())
        return juce::Result::fail("finite.baseline.provider: Exact source-built original "
                                  "module/descriptor/captured state required");
    if (p.channels.size() != 1 || p.channels[0].instrument || p.channels[0].audioClip ||
        p.channels[0].mixer != 1 || p.channels[0].gain != 1 || p.channels[0].pan != 0 ||
        !juce::File(p.channels[0].source).existsAsFile() || !p.audioPlacements.empty() || p.solo ||
        p.mixerSolo || !p.automation.sources.empty() || !p.automation.placements.empty() ||
        !p.automation.initialValues.empty() || guide->enabled.load() || guide->leadActive.load())
        return juce::Result::fail(
            "finite.baseline.project: One genuine sample source/no automation/Guide required");
    int effects = 0;
    for (const auto &m : p.mixer)
    {
        if (m.gain != 1 || m.pan != 0 || m.muted || m.stereoSeparation != 0 || m.phaseInverted ||
            m.stereoSwapped || m.effectsBypassed ||
            (m.index && (m.routes.size() != 1 || m.routes[0].target != 0 || m.routes[0].gain != 1)))
            return juce::Result::fail("finite.baseline.routing: Unity sourceA/unused anchorB/Master only");
        for (const auto &fx : m.effects)
            if (++effects != 1 || m.index != 2 || fx.id != anchor)
                return juce::Result::fail(
                    "finite.baseline.membership: Exactly one original anchor on unused insert2");
    }
    if (effects != 1)
        return juce::Result::fail("finite.baseline.membership: Finite anchor absent");
    auto &manager = engine.getDeviceManager().deviceManager;
    auto *device = manager.getCurrentAudioDevice();
    auto reader = manager.getPhysicalCallbackReader();
    juce::AudioDeviceManager::PhysicalCallbackSnapshot physical;
    if (!device || device->getCurrentSampleRate() != 48000 || device->getCurrentBufferSizeSamples() != 256 ||
        !reader.read(physical) || !physical.ownerAlive || !physical.active || physical.writerConflict ||
        physical.countersExhausted || physical.configuredFrames != 256 || physical.maximumOuterFrames > 256)
        return juce::Result::fail("finite.baseline.device: Exact healthy full-Q256/48k owner required");
    try
    {
        // Message-thread-only monotonic reservation. The number is owned by a
        // REAL prepared native session/physical quota, never a stand-in flag.
        static std::uint64_t nextGeneration = 1;
        if (nextGeneration == std::numeric_limits<std::uint64_t>::max())
            return juce::Result::fail("finite.local.exhausted: Fresh process required");
        const auto generation = nextGeneration++;
        auto next = std::make_shared<FiniteEffectBaselineState>(controller, anchor, originalModule,
                                                                sourceBuiltHash, device, std::move(reader),
                                                                physical.deviceEpoch, generation);
        // The policy captures a stable separately owned authority. No PluginPtr
        // or Edit owning cycle; its callback is CONTROL-only during factory build.
        if (te::SourceBurstSession::prepare(manager, next->sourceConfiguration, next->observer,
                                            next->session) != te::SourceBurstSession::Status::prepared)
            return juce::Result::fail(
                "finite.local.preparationFailed: Actual physical source authority refused");
        next->ports = experimental::AppMixerAudioPorts::createFiniteSampleBaseline(
            [weak = std::weak_ptr<FiniteEffectBaselineState>(next)](te::Plugin &plugin)
            {
                using B = experimental::AppMixerAudioPorts::Binding;
                const auto authority = weak.lock(); // CONTROL only. No dangling state after owner disposal.
                auto *remote = dynamic_cast<RemoteEffect *>(&plugin);
                if (!authority || !remote || typeid(plugin) != typeid(RemoteEffect) ||
                    remote->phasePairPluginId() != authority->anchor || !authority->exactHost() ||
                    remote->phasePairHost() != authority->host.get())
                    throw std::invalid_argument("finite.baseline.provider: Foreign/stale effect Host/state");
                return B{B::Role::effect, remote->runtimeOwnerControl()};
            },
            generation,
            [weak = std::weak_ptr<FiniteEffectBaselineState>(next)](
                experimental::AppMixerAudioPorts::FiniteSourceObservation &out)
            {
                const auto owner = weak.lock();
                if (!owner || !owner->observer->read(out) || !owner->session ||
                    !owner->session->read(out.native))
                    return false;
                out.ready =
                    !owner->invalidated.load(std::memory_order_acquire) && out.fault == 0 && !out.open &&
                    out.ends > 0 && out.native.status == te::SourceBurstSession::Status::ready &&
                    !out.native.failed && !out.native.retired && !out.native.scopeOpen &&
                    out.native.generation == out.generation && out.native.physicalSerial == out.serial &&
                    out.native.sourceProcessed == 256 && out.native.deviceOffset == 256;
                return true;
            });
        finiteEffectBaseline = std::move(next);
        return juce::Result::ok();
    }
    catch (...)
    {
        return juce::Result::fail("finite.baseline.preparation: No baseline authority published");
    }
}

juce::Result AudioEngine::validateFiniteEffectBaseline(const Project &project) const
{
    const auto &s = *finiteEffectBaseline;
    te::SourceBurstSession::Outcome native;
    if (!s.session || !s.session->read(native) || native.failed || native.retired)
        return juce::Result::fail(
            "finite.local.sourceFault: Actual sample source authority failed; dispose owner");
    juce::AudioDeviceManager::PhysicalCallbackSnapshot physical;
    if (s.invalidated.load(std::memory_order_acquire) || !s.physical.read(physical) || !physical.ownerAlive ||
        !physical.active || physical.writerConflict || physical.countersExhausted ||
        physical.deviceEpoch != s.deviceEpoch || physical.configuredFrames != 256 ||
        physical.maximumOuterFrames > 256 ||
        engine.getDeviceManager().deviceManager.getCurrentAudioDevice() != s.device)
        return juce::Result::fail("finite.baseline.deviceChanged: Terminal initial baseline, dispose owner");
    // Stored Controller address is NOT dereferenced. Only current actual fixture
    // calls prepare; later project updates must remain byte-equivalent here.
    if (!project.state().isEquivalentTo(s.project.state()) || guide->enabled.load() ||
        guide->leadActive.load())
        return juce::Result::fail(
            "finite.baseline.initialOnly: Canonical/source/effect/route universe fixed");
    if (s.editCreated &&
        (!s.exactHost() || currentHosts.size() != 1 || currentHosts.find(s.anchor) == currentHosts.end() ||
         currentHosts.at(s.anchor) != s.host))
        return juce::Result::fail("finite.baseline.providerChanged: Original Host/state/configuration fixed");
    return juce::Result::ok();
}

bool AudioEngine::refuseFiniteEffectDeviceReprepare() noexcept
{
    if (!finiteEffectBaseline || !finiteEffectBaseline->editCreated)
        return false;
    finiteEffectBaseline->invalidated.store(true, std::memory_order_release);
    return true; // BEFORE native ScopedDeviceListReleaser/context/Track destruction.
}

void AudioEngine::withFiniteEffectBaselineContext(
    const Controller &controller,
    const std::function<void(te::Edit &, te::EditPlaybackContext &, experimental::AppMixerAudioPorts &)>
        &driver)
{
    const auto *messages = juce::MessageManager::getInstanceWithoutCreating();
    if (!messages || !messages->isThisTheMessageThread() || !finiteEffectBaseline || !edit || !driver ||
        initialSourceTransport || &controller != finiteEffectBaseline->controller ||
        controller.projectIdentity != finiteEffectBaseline->controllerIdentity ||
        controller.revision != finiteEffectBaseline->revision ||
        !controller.project.state().isEquivalentTo(finiteEffectBaseline->project.state()) ||
        edit->initialSourceGeneration != finiteEffectBaseline->sourceConfiguration.generation ||
        !edit->closedNativeIO || validateFiniteEffectBaseline(projectedProject).failed())
        throw std::invalid_argument("finite.baseline.context: Exact owned initial CONTROL context required");
    auto *context = edit->getTransport().getCurrentPlaybackContext();
    if (!context)
        throw std::invalid_argument("finite.baseline.context: Context must actually exist");
    driver(*edit, *context, *finiteEffectBaseline->ports);
}
