// SPDX-License-Identifier: AGPL-3.0-or-later
#include "AudioEngine.h"
#include "WorkerProcess.h"
#include <juce_cryptography/juce_cryptography.h>
#include "Commands.h"
#include "PatternExtent.h"
#include "StoppedEffectChangePlan.h"
#include "effects/NativeEffectsRegistry.h"
#include "PreparedSourceMembership.h"
#include "../tests/ProductionMembershipSdkFixture.h"
#include "PreparedTimeline.h"
#include "RetainedContextFixture.h"
#include "RetainedInstrumentContextFixture.h"
#include "RetainedWaveContextFixture.h"
#include <stdexcept>
#include "Sampler.h"
#include "LiveMidi.h"
#include "RepeatMidiBudget.h"
#include "MidiNoteOwners.h"
#include "AutomationGain.h"
#include "ChannelBalance.h"
#include "MixerRoutePlugins.h"
#include "MultiOutputPlugins.h"
#include "MixerRouting.h"
#include "MixerNativeIdentity.h"
#include "PluginAutomationStream.h"
#include "RemoteEffect.h"
#include "PreparedAppMixerAudioPorts.h"
#include "OrdinarySampleAudioPorts.h"
#include "CompensationGraph.h"
#include "PluginAutomationPreparation.h"
#include <tracktion_graph/tracktion_graph.h>
#include <tracktion_engine/playback/graph/tracktion_TracktionEngineNode.h>
#include <tracktion_engine/playback/graph/tracktion_EditNodeBuilder.h>
#include <algorithm>
#include <limits>

namespace vibera
{
class EnginePolicy final : public te::EngineBehaviour
{
  public:
    bool autoInitialiseDeviceManager() override
    {
        return false;
    }
    bool shouldOpenAudioInputByDefault() override
    {
        return false;
    }
    int getNumberOfCPUsToUseForAudio() override
    {
        // A graph quantum must not depend on another graph worker finishing.
        // The supported upstream policy maps1 CPU to zero auxiliary graph
        // threads, executing its prepared nodes on the calling thread. Hosted
        // plugin DSP remains in its existing isolated asynchronous workers.
        // Revisit parallel graph throughput only with bounded deadline evidence.
        return 1;
    }
};
class EngineStorage final : public te::PropertyStorage
{
    bool offline;

  public:
    explicit EngineStorage(bool enableDevice)
        : PropertyStorage(enableDevice ? "VibeRa Studio" : "VibeRa Offline"), offline(!enableDevice)
    {
    }
    juce::File getAppPrefsFolder() override
    {
        if (offline)
            if (const auto testStorage =
                    juce::SystemStats::getEnvironmentVariable("VIBERA_NATIVE_METER_TEST_STORAGE", {});
                juce::File::isAbsolutePath(testStorage))
                return juce::File(testStorage).getChildFile("prefs");
        return offline ? juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("VibeRa-offline/prefs")
                       : PropertyStorage::getAppPrefsFolder();
    }
    juce::File getAppCacheFolder() override
    {
        if (offline)
            if (const auto testStorage =
                    juce::SystemStats::getEnvironmentVariable("VIBERA_NATIVE_METER_TEST_STORAGE", {});
                juce::File::isAbsolutePath(testStorage))
                return juce::File(testStorage).getChildFile("cache");
        return offline ? juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("VibeRa-offline/cache")
                       : PropertyStorage::getAppCacheFolder();
    }
};
class FixedSampler final : public te::Plugin
{
    // Nonmoving processing owner. Control metadata below is never read/written
    // by DSP. Reference-bound bank budget is constructed ONCE at this address.
    struct Runtime
    {
        VoiceBank voices;
        VoiceBank inputVoices, typingVoices, previewVoices;
        SamplerVoiceBudget voiceBudget{voices, inputVoices, typingVoices, previewVoices};
        SamplerCutMonitor cutMonitor;
        MidiMonitor input, typing, previewInput;
        MidiNoteOwners noteOwners;
        LiveMidi midi;
        std::unique_ptr<PluginAutomationStream> nativeStream;
        initial_source_epoch::Fence::Ptr sourceEpochFence; // Created ONCE before this fresh Host processes; retained with Runtime.
        // Same sole DSP owner as pending MIDI/automation; never copied by CONTROL.
        double clockTempo = 120;
        int clockNumerator = 4, clockDenominator = 4;
        unsigned midiReset = 0;
        std::atomic<int> observedInputKeys{0};
        LiveSequencer sequencer;
        double inputClockSeconds = 0;
        GuideGate guideGate;
        // Sole CONTROL owner; initialized shell attachments, NOT shared_ptr use_count.
        unsigned attachedShells = 0;
        unsigned dormantLeases = 0; // CONTROL-only immutable-configuration seals, including the source.
        bool prepared = false;
        double preparedRate = 0;
        int preparedBlock = 0;
        std::uint64_t epoch = 0;
        const Sample *source = nullptr;
        const PluginHost *worker = nullptr;
        juce::String workerInstance;
        PluginBusRequest workerBuses;
        int workerQuantum = 0;
        const MidiLane *inputSource = nullptr, *typingSource = nullptr, *previewSource = nullptr;
        const SamplerCutClock *cutSource = nullptr;
        const GuideControl *guideSource = nullptr;
        Id channel = 0, instrument = 0;
        bool external = false;
        Runtime() = default;
        Runtime(const Runtime &) = delete;
        Runtime &operator=(const Runtime &) = delete;
        Runtime(Runtime &&) = delete;
        Runtime &operator=(Runtime &&) = delete;
    };
    std::shared_ptr<Runtime> runtime = std::make_shared<Runtime>();
    bool runtimeAttached = false, sharingAdmissionPending = false, dormantSharingAdmissionPending = false;
    std::uint64_t sharingEpoch = 0;
    std::shared_ptr<const Sample> sample;
    std::shared_ptr<SamplerCutClock> cutClock;
    std::uint64_t samplerSettingsGeneration = 0;
    std::shared_ptr<PluginHost> remote;
    std::atomic<int> graphLatencySamples{0};
    bool external = false;
    std::atomic<bool> bypass{false};
    std::shared_ptr<LiveTimeline> timeline;
    LiveTimeline::Lease lastDocument;
    Id remoteId = 0;
    std::atomic<std::uint64_t> timelineMisses{0}, renderCalls{0};
    std::atomic<std::uint64_t> samplerAutomationFailures{0};
    std::atomic<unsigned> samplerAutomationFailureReason{0};
    void failedSamplerAutomation() noexcept
    {
        samplerAutomationFailureReason.store(unsigned(runtime->voiceBudget.modulationStatus()),
                                             std::memory_order_relaxed);
        samplerAutomationFailures.fetch_add(1, std::memory_order_relaxed);
    }
    Id channelId = 0;
    std::shared_ptr<GuideControl> guide;
    std::atomic<double> lastTime{0};
    std::atomic<int> lastLeadSkip{0};
    std::atomic<bool> lastPlaying{false};
    std::atomic<std::uint64_t> graphInitialisations{0};

    void requirePrivateRuntimeConfiguration() const
    {
        if (runtimeAttached || runtime->attachedShells != 0 || runtime->dormantLeases != 0 ||
            sharingAdmissionPending || dormantSharingAdmissionPending)
            throw std::logic_error("sampler.runtime.configurationInUse: Configure immutable source/lane "
                                   "identity before graph attachment");
    }
    bool runtimeConfigurationMatches(double rate, int block) const noexcept
    {
        return runtime->prepared && runtime->preparedRate == rate && runtime->preparedBlock == block &&
               runtime->source == sample.get() && runtime->worker == remote.get() &&
               (!remote || (runtime->workerInstance == remote->runtimeIdentity() &&
                            runtime->workerBuses == remote->busLayout() &&
                            runtime->workerQuantum == remote->processingQuantum())) &&
               runtime->external == external && runtime->instrument == remoteId &&
               runtime->channel == channelId && runtime->inputSource == runtime->input.inputLane() &&
               runtime->typingSource == runtime->typing.inputLane() &&
               runtime->previewSource == runtime->previewInput.inputLane() &&
               runtime->cutSource == cutClock.get() && runtime->guideSource == guide.get();
    }
    void detachRuntime() noexcept
    {
        if (!runtimeAttached)
            return;
        runtimeAttached = false;
        jassert(runtime->attachedShells != 0);
        if (runtime->attachedShells == 0 || --runtime->attachedShells != 0)
            return;
        // Last initialized shell ONLY, after native graph readers quiesce.
        // No reset when an incumbent shell retires while a successor is attached.
        if (remote)
            remote->resetNotes();
        runtime->voices.reset();
        runtime->inputVoices.reset();
        runtime->typingVoices.reset();
        runtime->previewVoices.reset();
        runtime->voiceBudget.resetTransitions();
        runtime->typing.reset();
        runtime->previewInput.reset();
        runtime->noteOwners.reset();
        runtime->observedInputKeys.store(0, std::memory_order_relaxed);
        runtime->sequencer.reset();
        runtime->midi.reset();
        runtime->prepared = false;
    }

  public:
    static constexpr const char *xmlTypeName = "vibera_fixed_sampler";
    static const char *getPluginName()
    {
        return "VibeRa Sampler";
    }
    FixedSampler(te::PluginCreationInfo info) : te::Plugin(info) {}
    // We schedule our own score/automation before the worker adds its real delay.
    // Native effect automation time would subtract that delay a second time.
    bool usesUnadjustedSourceRenderTime() const noexcept override
    {
        return true;
    }
    // Internal preparation only: caller owns a private, already identified tree.
    static tracktion::Plugin::Ptr createDetachedGraph(tracktion::Edit &edit, const juce::ValueTree &state)
    {
        if (state[tracktion::IDs::type].toString() != xmlTypeName)
            throw std::invalid_argument("Detached adapter type mismatch");
        return new FixedSampler({edit, state, false, tracktion::PluginCreationInfo::Mode::detachedGraph});
    }

    // Internal CONTROL preparation, not a user command. New detached shell
    // shares a compatible sample or exact same-worker instrument DSP owner;
    // nothing mutable is copied and only one adopted graph may process it.
    juce::Result shareCompatibleRuntimeFrom(const FixedSampler &source)
    {
        auto fail = [](const char *message) { return juce::Result::fail(message); };
        if (!isDetachedGraph() || &source == this || &source.edit != &edit || itemID != source.itemID ||
            runtimeAttached || sharingAdmissionPending || dormantSharingAdmissionPending || runtime->attachedShells || runtime->prepared ||
            renderCalls.load() != 0)
            return fail(
                "sampler.runtime.invalidCandidate: Requires a fresh same-ID detached shell in the same Edit");
        if (source.external)
        {
            // A genuine missing instrument is the exact null-Host/null-stream
            // external placeholder. A dead/non-null worker is NOT equivalent.
            if (source.remoteId <= 0 ||
                (source.remote && (!source.remote->alive() || !source.runtime->nativeStream ||
                                   source.remote->busLayout().auxiliaryChannels != 0)) ||
                (!source.remote && source.runtime->nativeStream) ||
                (external && (remote != source.remote || remoteId != source.remoteId)) ||
                (!external && remote))
                return fail("sampler.runtime.workerMismatch: Exact live initialized instrument Host/ID and "
                            "no auxiliary input required");
        }
        else if (external || remote || source.remote || source.runtime->nativeStream)
            return fail("sampler.runtime.sourceKindMismatch: Sample and external instrument owners cannot "
                        "replace each other");
        if (!source.runtimeAttached || !source.runtime->prepared || source.runtime->attachedShells == 0 ||
            (!source.external && !source.sample) ||
            source.runtime->epoch == std::numeric_limits<std::uint64_t>::max() ||
            !source.runtimeConfigurationMatches(source.runtime->preparedRate, source.runtime->preparedBlock))
            return juce::Result::fail(
                "sampler.runtime.inactiveSource: Source needs compatible initialized fixed-configuration ownership "
                "[attached=" + juce::String(int(source.runtimeAttached)) +
                ",prepared=" + juce::String(int(source.runtime->prepared)) +
                ",owners=" + juce::String(source.runtime->attachedShells) +
                ",sample=" + juce::String(int(bool(source.sample))) +
                ",external=" + juce::String(int(source.external)) +
                ",configuration=" + juce::String(int(source.runtimeConfigurationMatches(
                    source.runtime->preparedRate, source.runtime->preparedBlock))) + "]");
        if ((sample && sample != source.sample) || (channelId && channelId != source.channelId) ||
            (cutClock && cutClock != source.cutClock) || (guide && guide != source.guide) ||
            (runtime->input.inputLane() && runtime->input.inputLane() != source.runtime->inputSource) ||
            (runtime->typing.inputLane() && runtime->typing.inputLane() != source.runtime->typingSource) ||
            (runtime->previewInput.inputLane() &&
             runtime->previewInput.inputLane() != source.runtime->previewSource))
            return fail("sampler.runtime.incompatibleSource: Immutable Sample, Channel, lane, cut-clock and "
                        "guide identities must match");
        // All assignments are pre-publication CONTROL ownership. The private
        // empty Runtime can retire here; never in canonical commit or DSP.
        sample = source.sample;
        if (!external)
            bypass.store(source.bypass.load(std::memory_order_relaxed), std::memory_order_relaxed);
        remote = source.remote;
        remoteId = source.remoteId;
        external = source.external;
        cutClock = source.cutClock;
        guide = source.guide;
        channelId = source.channelId;
        if (!timeline)
            timeline = source.timeline; // A caller may supply a private graph scope beforehand.
        graphLatencySamples.store(source.graphLatencySamples.load(std::memory_order_relaxed),
                                  std::memory_order_relaxed);
        sharingEpoch = source.runtime->epoch;
        runtime = source.runtime;
        sharingAdmissionPending = true;
        return juce::Result::ok();
    }
    // CONTROL lifetime pin; Runtime contains no Edit/plugin ownership.
    std::shared_ptr<const void> runtimeOwnerControl() const { return runtime; }
    std::uintptr_t runtimeIdentity() const noexcept
    {
        return reinterpret_cast<std::uintptr_t>(runtime.get());
    }
    unsigned runtimeAttachmentsControl() const noexcept
    {
        return runtime->attachedShells;
    }
    std::uint64_t runtimeEpochControl() const noexcept { return runtime->epoch; }

    // A pruned/muted sample has no initialized DSP owner to retain. Reuse its
    // immutable PCM/lanes and dormant Runtime, then let the private graph perform
    // its exception-safe first initialization. Never relax active-owner sharing
    // or use this path for an external worker's packet stream.
    juce::Result shareDormantSampleRuntimeFrom(const FixedSampler &source)
    {
        if (!isDetachedGraph() || &source == this || &source.edit != &edit || itemID != source.itemID ||
            runtimeAttached || sharingAdmissionPending || dormantSharingAdmissionPending || runtime->attachedShells || runtime->prepared ||
            renderCalls.load() != 0 || external || remote || source.external || source.remote ||
            source.runtime->nativeStream || !source.sample || !source.baseClassNeedsInitialising() ||
            source.runtimeAttached || source.sharingAdmissionPending || source.dormantSharingAdmissionPending || source.runtime->prepared ||
            source.runtime->attachedShells || source.runtime->epoch == std::numeric_limits<std::uint64_t>::max() ||
            source.runtime->dormantLeases == std::numeric_limits<unsigned>::max())
            return juce::Result::fail("sampler.runtime.dormantRefused: Exact uninitialized sample with no DSP owner required");
        if ((sample && sample != source.sample) || (channelId && channelId != source.channelId) ||
            (cutClock && cutClock != source.cutClock) || (guide && guide != source.guide) ||
            (runtime->input.inputLane() && runtime->input.inputLane() != source.runtime->input.inputLane()) ||
            (runtime->typing.inputLane() && runtime->typing.inputLane() != source.runtime->typing.inputLane()) ||
            (runtime->previewInput.inputLane() && runtime->previewInput.inputLane() != source.runtime->previewInput.inputLane()))
            return juce::Result::fail("sampler.runtime.incompatibleSource: Dormant immutable sample, Channel and lanes must match");
        sample = source.sample;
        bypass.store(source.bypass.load(std::memory_order_relaxed), std::memory_order_relaxed);
        cutClock = source.cutClock;
        guide = source.guide;
        channelId = source.channelId;
        if (!timeline) timeline = source.timeline;
        sharingEpoch = source.runtime->epoch;
        runtime = source.runtime; // CONTROL ownership only; no mutable DSP state is copied.
        ++runtime->dormantLeases;
        dormantSharingAdmissionPending = true;
        return juce::Result::ok();
    }

    ~FixedSampler() override
    {
        if (dormantSharingAdmissionPending)
            --runtime->dormantLeases;
        // Constructor unwind can leave native initialisation bookkeeping without
        // a completed PluginNode. This shell still releases its own attachment.
        detachRuntime();
        notifyListenersOfDeletion();
    }
    // Native per-adapter CPU telemetry is unused; overall callback health is independent.
    bool shouldMeasureCpuUsage() const noexcept override
    {
        return false;
    }
    juce::String getName() const override
    {
        return getPluginName();
    }
    juce::String getPluginType() override
    {
        return xmlTypeName;
    }
    juce::String getSelectableDescription() override
    {
        return getName();
    }
    bool isSynth() override
    {
        return true;
    }
    bool takesMidiInput() override
    {
        return true;
    }
    bool takesAudioInput() override
    {
        return false;
    }
    bool producesAudioWhenNoAudioInput() override
    {
        return true;
    }
    int getNumOutputChannelsGivenInputs(int) override
    {
        return 2;
    }
    BusLayout getBusses() const override
    {
        return BusLayout::singleStereoInOut();
    }
    void setRemote(std::shared_ptr<PluginHost> worker, bool isBypassed, Id plugin)
    {
        requirePrivateRuntimeConfiguration();
        external = true;
        remote = std::move(worker);
        graphLatencySamples = remote ? remote->latencySamples() : 0;
        remoteId = plugin;
        if (remote && !runtime->nativeStream)
            runtime->nativeStream =
                std::make_unique<PluginAutomationStream>(); // Before graph insertion only.
        bypass = isBypassed;
    }
    void prepareInitialSourceEpochFence(const PreparedPluginTransport::Ptr& plan,
                                         std::shared_ptr<initial_source_epoch::Health> health)
    {
        requirePrivateRuntimeConfiguration();
        if(!external||!remote||remoteId<=0||!runtime->nativeStream||runtime->sourceEpochFence||
           runtime->prepared||runtime->attachedShells||renderCalls.load()!=0||!health)
            throw std::invalid_argument("sourceEpoch.initialOnly: Fresh actual instrument Runtime/Host required");
        auto prepared=std::make_shared<initial_source_epoch::Fence>(remoteId,runtime.get(),*remote,plan,std::move(health));
        const auto& expected=prepared->expectation();
        if(graphLatencySamples.load(std::memory_order_relaxed)!=
            expected.identity.transportSamples+expected.native.nativeSamples)
            throw std::invalid_argument("sourceEpoch.graphLatency: Initial graph/declaration changed before attachment");
        runtime->sourceEpochFence=std::move(prepared); // CONTROL, before graph/context or any Host source sample.
    }
    const initial_source_epoch::Fence::Ptr& initialSourceEpochFenceControl()const noexcept
    {return runtime->sourceEpochFence;}
    void setBypassed(bool b)
    {
        bypass = b;
    }
    double getLatencySeconds() override
    {
        return remote ? graphLatencySamples.load(std::memory_order_relaxed) / sampleRate : 0.;
    }
    bool setGraphLatencySamples(int samples)
    {
        return graphLatencySamples.exchange(samples, std::memory_order_relaxed) != samples;
    }
    int preparedGraphLatencySamples() const noexcept
    {
        return graphLatencySamples.load(std::memory_order_relaxed);
    }
    void setPreparedSource(std::shared_ptr<const Sample> prepared)
    {
        requirePrivateRuntimeConfiguration();
        // Called only before insertion into the graph. Never mutate an active source.
        sample = std::move(prepared);
    }
    const std::shared_ptr<const Sample> &preparedSource() const
    {
        return sample;
    }
    void setTimeline(std::shared_ptr<LiveTimeline> data, Id channel)
    {
        requirePrivateRuntimeConfiguration();
        timeline = std::move(data);
        channelId = channel;
    }
    void setInput(std::shared_ptr<MidiLane> lane)
    {
        requirePrivateRuntimeConfiguration();
        lane->channel = channelId;
        lane->cutSignal->clock = cutClock;
        runtime->input.bind(std::move(lane));
    }
    // Detached diagnostic clone only. Shared lane metadata is immutable after
    // source preparation: bind the candidate-local monitor without rewriting it.
    void bindPreparedInputForDetachedGraph(std::shared_ptr<MidiLane> lane)
    {
        requirePrivateRuntimeConfiguration();
        if (!isDetachedGraph() || !lane || lane->channel != channelId || !lane->cutSignal ||
            lane->cutSignal->clock != cutClock)
            throw std::logic_error("sampler.runtime.inputMismatch: Detached input requires matching prepared "
                                   "Channel/cut-clock identity");
        runtime->input.bind(std::move(lane));
    }
    void setTypingInput(std::shared_ptr<MidiLane> lane)
    {
        requirePrivateRuntimeConfiguration();
        lane->channel = channelId;
        lane->cutSignal->clock = cutClock;
        runtime->typing.bind(std::move(lane));
    }
    void preparePreviewInput()
    {
        requirePrivateRuntimeConfiguration();
        auto lane = std::make_shared<MidiLane>();
        lane->channel = channelId;
        lane->cutSignal->clock = cutClock;
        lane->enabled = true;
        runtime->previewInput.bind(std::move(lane));
    }
    MidiLane *previewLane() const
    {
        return runtime->previewInput.inputLane();
    }
    void setCutClock(std::shared_ptr<SamplerCutClock> clock)
    {
        requirePrivateRuntimeConfiguration();
        cutClock = std::move(clock);
    }
    void appendCutInputs(std::vector<SamplerCutInput> &result) const
    {
        int kind = 0;
        for (auto *lane :
             {runtime->input.inputLane(), runtime->typing.inputLane(), runtime->previewInput.inputLane()})
        {
            if (lane)
                result.push_back({channelId, lane->cutSignal, 0, kind});
            ++kind;
        }
    }
    MidiLane *typingLane() const
    {
        return runtime->typing.inputLane();
    }
    void setGuide(std::shared_ptr<GuideControl> value)
    {
        requirePrivateRuntimeConfiguration();
        guide = std::move(value);
    }
    MidiLane *inputLane() const
    {
        return runtime->input.inputLane();
    }
    const std::shared_ptr<LiveTimeline> &timelineReference() const
    {
        return timeline;
    }
    std::uint64_t graphCount() const
    {
        return graphInitialisations.load();
    }
    int heldInputKeys() const noexcept
    {
        return runtime->observedInputKeys.load(std::memory_order_relaxed);
    }
    std::uint64_t misses() const
    {
        return timelineMisses.load();
    }
    std::uint64_t calls() const
    {
        return renderCalls.load();
    }
    double timeSeen() const
    {
        return lastTime.load();
    }
    std::uint64_t samplerAutomationFailureCount() const noexcept
    {
        return samplerAutomationFailures.load(std::memory_order_relaxed);
    }
    unsigned samplerAutomationLastFailure() const noexcept
    {
        return samplerAutomationFailureReason.load(std::memory_order_relaxed);
    }
    int skipSeen() const
    {
        return lastLeadSkip.load();
    }
    bool playingSeen() const
    {
        return lastPlaying.load();
    }
    void initialiseWithoutStopping(const te::PluginInitialisationInfo &) override
    {
        ++graphInitialisations;
    }
    void initialise(const te::PluginInitialisationInfo &info) override
    {
        if (dormantSharingAdmissionPending &&
            (runtime->prepared || runtime->attachedShells != 0 || runtime->epoch != sharingEpoch))
            throw std::logic_error("sampler.runtime.staleDormantSource: Dormant ownership changed before first initialization");
        // First preparation/device changes retain their original reset semantics.
        // Shared fixed-config attachment performs ONLY control bookkeeping.
        if (sharingAdmissionPending &&
            (!runtime->prepared || runtime->attachedShells == 0 || runtime->epoch != sharingEpoch))
            throw std::logic_error(
                "sampler.runtime.staleSource: Source preparation changed before graph attachment");
        if ((!runtimeAttached && runtime->attachedShells > 0) ||
            (runtimeAttached && runtime->attachedShells > 1))
        {
            if (!runtimeConfigurationMatches(info.sampleRate, info.blockSizeSamples) ||
                (external && ((remote && (!remote->alive() || !runtime->nativeStream)) ||
                              (!remote && runtime->nativeStream))) ||
                runtime->attachedShells == std::numeric_limits<unsigned>::max())
                throw std::logic_error("sampler.runtime.configurationInUse: Shared source/lane/rate/buffer "
                                       "cannot change while attached");
            if (!runtimeAttached)
            {
                ++runtime->attachedShells;
                runtimeAttached = true;
            }
            sharingAdmissionPending = false;
            ++graphInitialisations;
            lastDocument = {};
            samplerSettingsGeneration =
                0; // Graph-local immutable control selection, applied only by adopted DSP.
            return;
        }
        ++graphInitialisations;
        lastDocument = {};
        auto preparedWorkerInstance = remote ? remote->runtimeIdentity() : juce::String{};
        const auto preparedWorkerBuses = remote ? remote->busLayout() : PluginBusRequest{};
        const auto preparedWorkerQuantum = remote ? remote->processingQuantum() : 0;
        if (runtime->nativeStream)
            runtime->nativeStream->reset();
        samplerSettingsGeneration = 0;
        if (sample)
        {
            runtime->voiceBudget.resetTransitions();
            runtime->voices.prepare(*sample, info.sampleRate);
            runtime->inputVoices.prepare(*sample, info.sampleRate);
            runtime->typingVoices.prepare(*sample, info.sampleRate);
            runtime->previewVoices.prepare(*sample, info.sampleRate);
            if (!runtime->voiceBudget.prepareModulationAutomation(info.blockSizeSamples))
                throw std::runtime_error(
                    "sampler.automation.preparationFailed: Exact block capacity unavailable");
        }
        runtime->input.reset();
        runtime->typing.reset();
        runtime->previewInput.reset();
        runtime->noteOwners.reset();
        runtime->observedInputKeys.store(0, std::memory_order_relaxed);
        runtime->sequencer.reset();
        runtime->midi.reset();
        runtime->preparedRate = info.sampleRate;
        runtime->preparedBlock = info.blockSizeSamples;
        runtime->source = sample.get();
        runtime->worker = remote.get();
        runtime->workerInstance.swapWith(preparedWorkerInstance);
        runtime->workerBuses = preparedWorkerBuses;
        runtime->workerQuantum = preparedWorkerQuantum;
        runtime->external = external;
        runtime->instrument = remoteId;
        runtime->channel = channelId;
        runtime->inputSource = runtime->input.inputLane();
        runtime->typingSource = runtime->typing.inputLane();
        runtime->previewSource = runtime->previewInput.inputLane();
        runtime->cutSource = cutClock.get();
        runtime->guideSource = guide.get();
        if (runtime->epoch != std::numeric_limits<std::uint64_t>::max())
            ++runtime->epoch;
        runtime->prepared = true;
        if (!runtimeAttached)
        {
            ++runtime->attachedShells;
            runtimeAttached = true;
        }
        sharingAdmissionPending = false;
        if (dormantSharingAdmissionPending)
            --runtime->dormantLeases;
        dormantSharingAdmissionPending = false;
    }
    void deinitialise() override
    {
        lastDocument = {}; // This shell's reader lease only.
        detachRuntime();
    }
    void restorePluginStateFromValueTree(const juce::ValueTree &) override {}
    void applyToBuffer(const te::PluginRenderContext &fc) override
    {
        if (!fc.destBuffer)
            return;
        // A source instrument owns this frame. A stopped/empty upstream MIDI node
        // may leave reused storage; add-only voice mixing must start from silence.
        fc.destBuffer->clear(fc.bufferStartSample, fc.bufferNumSamples);
        if(runtime->sourceEpochFence &&
           !runtime->sourceEpochFence->beforeInput(remote.get(),fc.bufferNumSamples))
            return; // Sticky source-domain failure BEFORE sampler input/clock/Guide/MIDI work.
        ++renderCalls;
        lastTime = fc.editTime.getStart().inSeconds();
        lastPlaying = fc.isPlaying;
        int skip = fc.isRendering ? 0
                   : !fc.isPlaying && guide && guide->leadActive.load()
                       ? fc.bufferNumSamples
                       : runtime->guideGate.skip(guide.get(), fc.editTime.getStart().inSeconds(),
                                                 fc.bufferNumSamples, sampleRate);
        if (!fc.isRendering)
        {
            lastLeadSkip = skip;
            const bool clockPlaying =
                fc.isPlaying && (!guide || !guide->leadActive.load() || guide->ready.load());
            if (auto *lane = runtime->input.inputLane())
                lane->clock.publish(runtime->inputClockSeconds,
                                    juce::Time::getMillisecondCounterHiRes() / 1000.,
                                    fc.bufferNumSamples / sampleRate, clockPlaying);
            if (clockPlaying)
                runtime->inputClockSeconds += fc.bufferNumSamples / sampleRate;
            if (auto *lane = runtime->typing.inputLane())
                lane->clock.publish(runtime->inputClockSeconds -
                                        (clockPlaying ? fc.bufferNumSamples / sampleRate : 0.),
                                    juce::Time::getMillisecondCounterHiRes() / 1000.,
                                    fc.bufferNumSamples / sampleRate, clockPlaying);
            if (external || !timeline)
            {
                runtime->input.read();
                runtime->typing.read();
                runtime->previewInput.read();
                if (runtime->input.count != 0 || runtime->typing.count != 0 || runtime->input.panicked ||
                    runtime->typing.panicked)
                    runtime->observedInputKeys.store(runtime->input.liveKeyCountRT() +
                                                         runtime->typing.liveKeyCountRT(),
                                                     std::memory_order_relaxed);
            }
        }
        if (external)
        {
            fc.destBuffer->clear(fc.bufferStartSample, fc.bufferNumSamples);
            int events = 0;
            auto next = timeline->acquire();
            if (next.document)
                lastDocument = std::move(next);
            else
                ++timelineMisses;
            auto &lease = lastDocument;
            if (runtime->input.globalPanic || (remote && remote->midiResetGeneration() != runtime->midiReset))
            {
                if (remote)
                    runtime->midiReset = remote->midiResetGeneration();
                runtime->midi.reset();
                runtime->noteOwners.reset();
            }
            if (lease.document)
            {
                auto &document = *lease.document;
                if (skip < fc.bufferNumSamples)
                {
                    events = runtime->midi.process(
                        document, channelId, fc.editTime.getStart().inSeconds() + skip / sampleRate,
                        fc.bufferNumSamples - skip, sampleRate, fc.isPlaying || fc.isRendering);
                    for (int i = 0; i < events; ++i)
                        runtime->midi.events[size_t(i)].frame += skip;
                }
                else
                    events = runtime->midi.process(document, channelId, fc.editTime.getStart().inSeconds(),
                                                   fc.bufferNumSamples, sampleRate, false);
                runtime->clockTempo =
                    document.tempoMap.bpmAt(document.tempoMap.beatAt(fc.editTime.getStart().inSeconds()));
                runtime->clockNumerator = document.numerator;
                runtime->clockDenominator = document.denominator;
            }
            std::array<ipc::MidiEvent, ipc::events> merged;
            int mergedCount = 0;
            bool overflow = false;
            auto append = [&](const ipc::MidiEvent &event)
            {
                if (mergedCount < ipc::events)
                    merged[size_t(mergedCount++)] = event;
                else
                    overflow = true;
            };
            auto appendInput = [&](const ipc::MidiEvent &event, int source)
            { runtime->noteOwners.process(source + 1, event, append); };
            int panicCount = !fc.isRendering ? runtime->input.releaseCount : 0;
            for (int i = 0; i < panicCount; ++i)
                appendInput(runtime->input.events[size_t(i)], 0);
            for (int i = 0; i < events; ++i)
                runtime->noteOwners.process(0, runtime->midi.events[size_t(i)], append);
            if (!fc.isRendering)
                for (int i = panicCount; i < runtime->input.count; ++i)
                    appendInput(runtime->input.events[size_t(i)], 0);
            if (!fc.isRendering)
                for (int i = 0; i < runtime->typing.count; ++i)
                    appendInput(runtime->typing.events[size_t(i)], 1);
            // Preview is owner3. Its epoch releases only that lane's held keys.
            if (!fc.isRendering)
                for (int i = 0; i < runtime->previewInput.count; ++i)
                    appendInput(runtime->previewInput.events[size_t(i)], 2);
            if (overflow)
            {
                for (int i = 0; i < 16; ++i)
                    merged[size_t(ipc::events - 16 + i)] = {
                        fc.bufferNumSamples - 1, {std::uint8_t(0xb0 + i), 123, 0}, 3};
                runtime->midi.reset();
                runtime->noteOwners.reset();
            }
            if (remote)
            {
                auto* fixed=runtime->sourceEpochFence.get(); // Nonowning, immutable Runtime pin; no refcount traffic.
                const auto processed=runtime->nativeStream->process(
                    *remote, lease.document, remoteId, *fc.destBuffer, fc.bufferStartSample,
                    fc.bufferNumSamples, {merged.data(), std::size_t(mergedCount)},
                    fc.editTime.getStart().inSeconds(), sampleRate, runtime->clockTempo,
                    fc.isPlaying || fc.isRendering, bypass.load(), runtime->clockNumerator,
                    runtime->clockDenominator,nullptr,0,1,fixed?fixed->captureForProcessing():nullptr);
                if(fixed&&!fixed->emitted(fc.bufferNumSamples,processed))
                    fc.destBuffer->clear(fc.bufferStartSample,fc.bufferNumSamples);
                // A partial/cross-epoch call never returns mismatched PCM to
                // PluginNode/PDC. Host/native/MIDI work already done is NOT rolled back.
            }
            return;
        }
        if (!sample)
            return;
        if (timeline)
        {
            auto next = timeline->acquire();
            if (next.document)
                lastDocument = std::move(next);
            else
                ++timelineMisses;
            auto &lease = lastDocument;
            const std::array<VoiceBank *, 3> extra{&runtime->inputVoices, &runtime->typingVoices,
                                                   &runtime->previewVoices};
            const LiveChannel *liveTuning = nullptr; // Set once the document lease is known.
            auto applyInputs = [&]
            {
                if (!fc.isRendering)
                {
                    // LOCAL monitor consumption is after curve/capacity admission.
                    // Native and no-timeline compatibility retain their earlier read point.
                    runtime->input.read();
                    runtime->typing.read();
                    runtime->previewInput.read();
                    if (runtime->input.globalPanic || runtime->typing.globalPanic ||
                        runtime->previewInput.globalPanic)
                        runtime->voiceBudget.resetTransitions();
                    if (runtime->input.count != 0 || runtime->typing.count != 0 || runtime->input.panicked ||
                        runtime->typing.panicked)
                        runtime->observedInputKeys.store(runtime->input.liveKeyCountRT() +
                                                             runtime->typing.liveKeyCountRT(),
                                                         std::memory_order_relaxed);
                    const auto *keys = liveTuning ? liveTuning->liveKey.data() : nullptr;
                    const auto *expressions = liveTuning ? liveTuning->liveExpression.data() : nullptr;
                    runtime->input.apply(runtime->inputVoices, keys, expressions);
                    runtime->typing.apply(runtime->typingVoices, keys, expressions);
                    if (runtime->previewInput.panicked)
                        runtime->previewVoices.releaseAllQuickly();
                    runtime->previewInput.apply(runtime->previewVoices);
                }
            };
            if (lease.document)
            {
                const auto *selectedChannel = lease.document->find(channelId);
                const bool local = selectedChannel && selectedChannel->sampleInstrument;
                const bool changed = local && samplerSettingsGeneration != lease.document->generation;
                const auto &map = lease.document->tempoMap;
                const double beat = map.beatAt(fc.editTime.getStart().inSeconds());
                const double beatStep = (fc.isPlaying || fc.isRendering) ? map.bpmAt(beat) / (60. * sampleRate) : 0.;
                const auto activeLanes =
                    (fc.isPlaying || fc.isRendering) && local
                        ? std::span<const SamplerModulationAutomationLane>(selectedChannel->modulationLanes)
                        : std::span<const SamplerModulationAutomationLane>{};
                const bool admitted = local ? runtime->voiceBudget.beginAutomatedModulationBlock(
                                                  selectedChannel->sampler, activeLanes, beat, beatStep,
                                                  fc.bufferStartSample, fc.bufferNumSamples, changed)
                                            : runtime->voiceBudget.beginModulationBlock(fc.bufferStartSample,
                                                                                        fc.bufferNumSamples);
                if (!admitted)
                {
                    failedSamplerAutomation();
                    return; // No phase/generation ACK, local monitor consumption or input application.
                }
                if (changed)
                {
                    runtime->voiceBudget.configure(selectedChannel->sampler.values.maxPoly);
                    samplerSettingsGeneration = lease.document->generation;
                }
                liveTuning = local && selectedChannel->liveTuned ? selectedChannel : nullptr;
                applyInputs();
                std::uint64_t liveCut = 0;
                if (const auto *channel = lease.document->find(channelId); channel && !fc.isRendering)
                    liveCut = runtime->cutMonitor.poll(channel->sampler.values.cutBy, channel->inputCuts);
                if (liveCut)
                {
                    runtime->voices.releaseConfigured(liveCut);
                    for (auto *bank : extra)
                        bank->releaseConfigured(liveCut);
                }
                // Pitch bend (+/-2 semitones) in force at the block start, from the compiled
                // controller events; block-rate, no allocation.
                double bendRatio = 1;
                if (const auto *channel = lease.document->find(channelId);
                    channel && (fc.isPlaying || fc.isRendering) && !channel->controls.empty())
                {
                    const double now = fc.editTime.getStart().inSeconds();
                    auto end = std::upper_bound(channel->controls.begin(), channel->controls.end(), now,
                                                [](double t, const auto &c) { return t < c.time; });
                    while (end != channel->controls.begin())
                    {
                        --end;
                        if ((end->bytes[0] & 0xf0) == 0xe0)
                        {
                            const int raw = end->bytes[1] | (end->bytes[2] << 7);
                            bendRatio = std::pow(2., (raw - 8192) / 8192. * 2. / 12.);
                            break;
                        }
                    }
                }
                runtime->voices.setBend(bendRatio);
                if (skip < fc.bufferNumSamples)
                {
                    for (auto *bank : extra)
                        bank->render(*fc.destBuffer, fc.bufferStartSample, skip);
                    runtime->sequencer.process(
                        *lease.document, channelId, fc.editTime.getStart().inSeconds() + skip / sampleRate,
                        fc.bufferNumSamples - skip, sampleRate, fc.isPlaying || fc.isRendering,
                        runtime->voices, *fc.destBuffer, fc.bufferStartSample + skip, extra, liveCut != 0);
                }
                else
                    runtime->sequencer.process(*lease.document, channelId, fc.editTime.getStart().inSeconds(),
                                               fc.bufferNumSamples, sampleRate, false, runtime->voices,
                                               *fc.destBuffer, fc.bufferStartSample, extra, liveCut != 0);
            }
            else
            {
                if (!runtime->voiceBudget.beginModulationBlock(fc.bufferStartSample, fc.bufferNumSamples))
                {
                    failedSamplerAutomation();
                    return;
                }
                applyInputs();
                runtime->voices.render(*fc.destBuffer, fc.bufferStartSample, fc.bufferNumSamples);
                for (auto *bank : extra)
                    bank->render(*fc.destBuffer, fc.bufferStartSample, fc.bufferNumSamples);
            }
            if (!runtime->voiceBudget.endModulationBlock())
                failedSamplerAutomation();
            return;
        }
        if (!runtime->voiceBudget.beginModulationBlock(fc.bufferStartSample, fc.bufferNumSamples))
        {
            failedSamplerAutomation();
            return;
        }
        if (fc.bufferForMidiMessages && fc.bufferForMidiMessages->isAllNotesOff)
            runtime->voices.reset();
        int cursor = 0;
        if (fc.bufferForMidiMessages)
            for (auto &event : *fc.bufferForMidiMessages)
            {
                const auto point =
                    juce::jlimit(cursor, fc.bufferNumSamples,
                                 juce::roundToInt((event.getTimeStamp() + fc.midiBufferOffset) * sampleRate));
                runtime->voices.render(*fc.destBuffer, fc.bufferStartSample + cursor, point - cursor);
                cursor = point;
                if (event.isNoteOn())
                    runtime->voices.noteOn(event.getNoteNumber(), event.getFloatVelocity(), 0, 0, 0, 0, {}, 0,
                                           false, event.getChannel() - 1);
                else if (event.isNoteOff())
                    runtime->voices.noteOff(event.getNoteNumber(), event.getChannel() - 1);
                else if (event.isAllNotesOff() || event.isAllSoundOff())
                    runtime->voices.reset();
            }
        runtime->voices.render(*fc.destBuffer, fc.bufferStartSample + cursor, fc.bufferNumSamples - cursor);
        if (!runtime->voiceBudget.endModulationBlock())
            failedSamplerAutomation();
    }
};
// Original component/worker qualification seam; no UI/MCP or mutable DSP API.
#include "../tests/InstrumentRuntimeAcceptance.inc"
#include "../tests/DormantSamplerRuntimeAcceptance.inc"
void applyClipFades(te::WaveAudioClip &, const AudioPlacement &);
// Pattern mode plays at the base tempo; the Song follows the tempo map.
inline TempoMap projectMap(const Project &p, bool patternMode)
{
    return patternMode ? TempoMap(p.tempo, {}) : p.songTempo();
}
class GuidePlugin final : public te::Plugin
{
    std::shared_ptr<GuideControl> control;
    std::shared_ptr<LiveTimeline> timeline;
    LiveTimeline::Lease document;
    // Stable click/lead DSP owner; only the adopted shell may process it.
    struct Runtime
    {
        GuideDsp dsp;
        detail::GraphControlRuntimeState lifecycle;
        const GuideControl *controlIdentity = nullptr;
        Runtime() = default;
        Runtime(const Runtime &) = delete;
        Runtime &operator=(const Runtime &) = delete;
        Runtime(Runtime &&) = delete;
        Runtime &operator=(Runtime &&) = delete;
    };
    std::shared_ptr<Runtime> runtime = std::make_shared<Runtime>();
    detail::GraphControlRuntimeAttachment attachment;
    bool configured = false;
    void detachRuntime() noexcept
    {
        document = {}; // Graph-local Lease, after this graph's readers quiesce.
        if (attachment.detach(runtime->lifecycle))
            runtime->dsp.reset(); // Genuine final retirement only.
    }

  public:
    static constexpr const char *xmlTypeName = "vibera_guide";
    GuidePlugin(te::PluginCreationInfo info) : te::Plugin(info) {}
    ~GuidePlugin() override
    {
        detachRuntime();
        notifyListenersOfDeletion();
    }
    // Native per-adapter CPU telemetry is unused; overall callback health is independent.
    bool shouldMeasureCpuUsage() const noexcept override
    {
        return false;
    }
    juce::String getName() const override
    {
        return "VibeRa Metronome";
    }
    juce::String getPluginType() override
    {
        return xmlTypeName;
    }
    juce::String getSelectableDescription() override
    {
        return getName();
    }
    bool isSynth() override
    {
        return false;
    }
    bool takesAudioInput() override
    {
        return true;
    }
    bool producesAudioWhenNoAudioInput() override
    {
        return true;
    }
    int getNumOutputChannelsGivenInputs(int) override
    {
        return 2;
    }
    BusLayout getBusses() const override
    {
        return BusLayout::singleStereoInOut();
    }
    // Internal CONTROL preparation: private, already identified state only.
    static te::Plugin::Ptr createDetachedGraph(te::Edit &edit, const juce::ValueTree &state)
    {
        if (state[te::IDs::type].toString() != xmlTypeName)
            throw std::invalid_argument("Detached adapter type mismatch");
        return new GuidePlugin({edit, state, false, te::PluginCreationInfo::Mode::detachedGraph});
    }
    juce::Result shareCompatibleRuntimeFrom(const GuidePlugin &previous)
    {
        if (!isDetachedGraph() || &previous == this || &previous.edit != &edit || itemID != previous.itemID ||
            !configured || !control || !timeline || !attachment.privateConfiguration(runtime->lifecycle))
            return juce::Result::fail(
                "guide.runtime.invalidCandidate: Fresh configured same-ID detached shell required");
        if (!previous.attachment.isAttachedControl() || !previous.runtime->lifecycle.canRetain())
            return juce::Result::fail("guide.runtime.inactiveSource: Initialized click owner required");
        if (control != previous.control || control.get() != previous.runtime->controlIdentity)
            return juce::Result::fail(
                "guide.runtime.incompatibleSource: Exact GuideControl identity must match");
        runtime = previous.runtime; // Empty private click bank retires on CONTROL.
        attachment.retain(runtime->lifecycle);
        return juce::Result::ok();
    }
    // CONTROL lifetime pin; Runtime contains no Edit/plugin ownership.
    std::shared_ptr<const void> runtimeOwnerControl() const { return runtime; }
    std::uintptr_t runtimeIdentity() const noexcept
    {
        return reinterpret_cast<std::uintptr_t>(runtime.get());
    }
    unsigned runtimeAttachmentsControl() const noexcept
    {
        return runtime->lifecycle.shells;
    }
    std::uint64_t runtimeEpochControl() const noexcept
    {
        return runtime->lifecycle.epoch;
    }
    void configure(std::shared_ptr<GuideControl> c, std::shared_ptr<LiveTimeline> t)
    {
        attachment.requirePrivateConfiguration(runtime->lifecycle);
        control = std::move(c);
        timeline = std::move(t);
        configured = true;
    }
    const std::shared_ptr<LiveTimeline> &timelineReference() const
    {
        return timeline;
    }
    void initialiseWithoutStopping(const te::PluginInitialisationInfo &) override {}
    void initialise(const te::PluginInitialisationInfo &info) override
    {
        const auto mode = attachment.preflight(runtime->lifecycle, info.sampleRate, info.blockSizeSamples,
                                               runtime->controlIdentity == control.get());
        if (mode == detail::GraphControlRuntimeAttachment::Preparation::retained)
        {
            // Pre-own THIS graph's fallback. No target application, DSP read,
            // cursor copy or GuideControl mutation during candidate prepare.
            auto ownDocument = timeline ? timeline->acquire() : LiveTimeline::Lease{};
            if (!ownDocument.document)
                throw std::logic_error(
                    "guide.runtime.documentUnavailable: Candidate guide document required");
            document = std::move(ownDocument);
            attachment.complete(runtime->lifecycle, info.sampleRate, info.blockSizeSamples, mode);
            return;
        }
        runtime->dsp.reset(); // Existing first/device initialization semantics.
        document = {};
        runtime->controlIdentity = control.get();
        attachment.complete(runtime->lifecycle, info.sampleRate, info.blockSizeSamples, mode);
    }
    void deinitialise() override
    {
        detachRuntime();
    }
    void restorePluginStateFromValueTree(const juce::ValueTree &) override {}
    void applyToBuffer(const te::PluginRenderContext &fc) override
    {
        if (!fc.destBuffer || fc.isRendering || !control || !timeline)
            return;
        // Block-start stream time: Tracktion advances it only after the whole graph ran.
        control->timeline.publish(engine.getDeviceManager().getCurrentStreamTime(),
                                  fc.editTime.getStart().inSeconds(), fc.isPlaying);
        auto next = timeline->acquire();
        if (next.document)
            document = std::move(next);
        if (document.document)
            runtime->dsp.process(*fc.destBuffer, fc.bufferStartSample, fc.bufferNumSamples,
                                 fc.editTime.getStart().inSeconds(), sampleRate, fc.isPlaying,
                                 document.document->tempo, document.document->numerator,
                                 document.document->denominator, *control, &document.document->tempoMap,
                                 &document.document->meters);
    }
};
namespace
{
juce::Result prepareMixerNativeBindings(te::Edit& edit, const Project& project,
                                       std::map<int,te::AudioTrack*>& output)
{
    MixerNativeIdentity layout;
    if (MixerNativeIdentity::prepare(project,layout) != MixerNativeIdentity::Error::none)
        return juce::Result::fail("mixer.nativeIdentity.invalidUniverse: Valid fixed Mixer IDs required");
    const auto tracks = te::getAudioTracks(edit);
    if (size_t(tracks.size()) !=
        project.channels.size() + project.mixer.size() - 1 + size_t(multiOutputTrackCount(project)))
        return juce::Result::fail("mixer.nativeIdentity.trackCount: Native universe does not match Project");
    std::map<int,te::AudioTrack*> next;
    for (int slot = 1; slot < layout.size(); ++slot)
    {
        const int id = *layout.idAtSlot(slot), bus = *layout.bus(id);
        auto* track = tracks[int(project.channels.size()) + slot - 1];
        if (!track->state.hasProperty("viberaMixerIndex") ||
            !track->state.hasProperty("viberaMixerBus") ||
            int(track->state["viberaMixerIndex"]) != id || int(track->state["viberaMixerBus"]) != bus)
            return juce::Result::fail("mixer.nativeIdentity.trackMismatch: Native identity stamp changed");
        int returns = 0;
        for (auto* plugin : track->pluginList)
        {
            if (auto* receive = dynamic_cast<te::AuxReturnPlugin*>(plugin))
            {
                ++returns;
                if (int(receive->busNumber) != bus)
                    return juce::Result::fail("mixer.nativeIdentity.returnMismatch: Native bus changed");
            }
            if (auto* send = dynamic_cast<MixerSendPlugin*>(plugin))
            {
                const auto destination = layout.bus(send->destination());
                if (!destination || send->destination() == 0 ||
                    int(send->state["viberaSource"]) != id || int(send->busNumber) != *destination)
                    return juce::Result::fail("mixer.nativeIdentity.sendMismatch: Native send identity changed");
            }
        }
        if (returns != 1)
            return juce::Result::fail("mixer.nativeIdentity.returnCount: Exactly one original bus receiver required");
        next.emplace(id,track);
    }
    output.swap(next); // Candidate resources acquired before incumbent retirement.
    return juce::Result::ok();
}
}
#include "ChannelRouting.inc"
// Original actual-Guide component lifecycle acceptance; no mutable DSP access API.
#include "../tests/GuideRuntimeAcceptance.inc"
class AudioEngine::PluginPreparation final : public juce::Thread
{
  public:
    std::vector<PluginSpec> specs;
    double rate;
    int block;
    bool offline;
    struct Prepared
    {
        std::shared_ptr<PluginHost> host;
        juce::String error;
    };
    std::map<Id, Prepared> results;
    std::map<Id, std::shared_ptr<PluginHost>> reuse;
    PreparedPluginTransport::Ptr transportPlan;
    std::optional<Project> fullProjectionDestination; // Immutable before worker thread starts.
    bool fullProjectionPublished = false;             // CONTROL only; never cancel a published projection.
    bool deferredNativeAccess = false;                // Ordinary deferred rebuild owns consumption.
    const Controller *fullProjectionOwner = nullptr;  // Compared only while this CONTROL job is pinned.
    const PreparedControllerChange *fullProjectionTicket =
        nullptr; // Actual caller pins until joined/drained.
    juce::String fullProjectionIdentity;
    std::uint64_t fullProjectionRevision = 0;
    std::atomic<bool> done{false};
    PluginPreparation(std::vector<PluginSpec> s, double r, int b, bool render)
        : Thread("VST3 preparation"), specs(std::move(s)), rate(r), block(b), offline(render)
    {
    }
    ~PluginPreparation() override
    {
        signalThreadShouldExit();
        stopThread(17000);
    }
    void run() override
    {
        for (auto &spec : specs)
        {
            if (threadShouldExit())
                break;
            if (auto it = reuse.find(spec.id);
                it != reuse.end() &&
                (transportPlan ? it->second->matchesPrepared(spec, rate, block, transportPlan,
                                                             pluginBusRequestFor(spec))
                               : it->second->matches(spec, rate, block, pluginBusRequestFor(spec))))
            {
                results[spec.id] = {it->second, {}};
                continue;
            }
            auto host = transportPlan ? std::make_shared<PluginHost>(transportPlan) : std::make_shared<PluginHost>();
            auto result = host->start(
                spec, rate, block, offline, [this] { return threadShouldExit(); }, pluginBusRequestFor(spec));
            if (result.failed())
                results[spec.id] = {nullptr, result.getErrorMessage()};
            else
                results[spec.id] = {std::move(host), {}};
        }
        done.store(true, std::memory_order_release);
    }
};
#include "DeferredDeviceProjection.inc"
#include "InitialSourceTransport.inc"
#include "FiniteEffectBaseline.inc"
#include "ProductionPluginTransport.inc"
#include "../tests/ProductionMembershipSdkFixture.inc"
juce::Result AudioEngine::preparePlugins(const Project &project, bool offline)
{
    if (!offline && preparation[0] && (fullProjectionController || preparation[0]->fullProjectionDestination))
    {
        const auto &target = preparation[0]->fullProjectionDestination;
        if (!fullProjectionController || !target || !target->state().isEquivalentTo(project.state()))
            return juce::Result::fail(
                "routing.foreignFullProjectionPreparation: Another exact worker preparation owns this slot");
    }
    PreparedPluginTransport::Ptr sourcePlan;
    if (!offline && initialSourceTransport)
        sourcePlan = initialSourceTransport->plan;
    else if (!offline && !finiteEffectBaseline)
        if (auto ready = prepareProductionPluginPlan(project, sourcePlan); ready.failed())
            return ready;
    if (initialSourceTransport && sourcePlan)
        if (auto admitted = validateInitialSourceProject(project); admitted.failed()) return admitted;
    std::vector<PluginSpec> specs;
    for (auto &channel : project.channels)
        if (channel.instrument)
            specs.push_back(*channel.instrument);
    for (auto &insert : project.mixer)
        for (auto &fx : insert.effects)
            specs.push_back(fx);
    if (!offline && recoveryPlugin)
        for (auto &spec : specs)
            if (spec.id == recoveryPlugin->id && spec.descriptor == recoveryPlugin->descriptor)
                spec.state = recoveryPlugin->state;
    double rate = offline ? renderRate : engine.getDeviceManager().getSampleRate();
    int block = offline ? renderBlock : engine.getDeviceManager().getBlockSize();
    if (rate <= 0)
        rate = 48000;
    if (block <= 0)
        block = 256;
    auto &job = preparation[size_t(offline)];
    if (job)
    {
        // A pending structural preparation may hold mutable workers reused from
        // the previous live project. Invalidation must not publish those workers
        // into a new project merely because its IDs/descriptors happen to match.
        const bool staleLiveReuse = !offline && !hasProjection && !deferredHostProjection && !job->reuse.empty();
        const bool sameDeferredOwner =
            !job->deferredNativeAccess ||
            (preparingDeferredNativeAccess && job->fullProjectionOwner == routingController &&
             (!routingController || job->fullProjectionIdentity == routingController->projectIdentity));
        bool matches = sameDeferredOwner && !staleLiveReuse && job->transportPlan == sourcePlan &&
                       job->rate == rate && job->block == block && job->specs == specs &&
                       !job->threadShouldExit();
        if (!job->done.load(std::memory_order_acquire))
        {
            if (!matches)
                job->signalThreadShouldExit();
            return juce::Result::fail("Preparing VST3 workers...");
        }
        if (job->isThreadRunning() || !job->waitForThreadToExit(0))
            return juce::Result::fail("Preparing VST3 workers...");
        if (matches)
        {
            // An explicit I/O change is a native negotiation, not a missing-
            // plugin load. Refuse the private candidate before canonical commit
            // when its requested buses could not actually be prepared. Existing
            // missing-provider project loading retains its placeholder policy.
            if (!offline && fullProjectionController && fullProjectionChange)
                for (const auto &spec : specs)
                    if (const auto *old = fullProjectionController->project.plugin(spec.id);
                        old && pluginMainIoFor(*old) != pluginMainIoFor(spec))
                    {
                        const auto found = job->results.find(spec.id);
                        if (found == job->results.end() || !found->second.host ||
                            !found->second.host->alive() || found->second.host->offline() ||
                            found->second.host->busLayout() != pluginBusRequestFor(spec))
                            return juce::Result::fail("pluginIo.preparationFailed: " +
                                                      (found == job->results.end()
                                                           ? juce::String("Native result unavailable")
                                                           : found->second.error));
                    }
            return juce::Result::ok();
        }
        job.reset();
    }
    if (specs.empty())
        return juce::Result::ok(); // Any obsolete job has actually drained before empty-bank publication.
    auto candidate = std::make_unique<PluginPreparation>(std::move(specs), rate, block, offline);
    candidate->transportPlan = sourcePlan;
    candidate->deferredNativeAccess = !offline && preparingDeferredNativeAccess;
    if (candidate->deferredNativeAccess)
    {
        candidate->fullProjectionOwner = routingController;
        if (routingController)
            candidate->fullProjectionIdentity = routingController->projectIdentity;
    }
    if (!offline && fullProjectionController)
    {
        candidate->fullProjectionDestination = project;
        candidate->fullProjectionOwner = fullProjectionController;
        candidate->fullProjectionTicket = fullProjectionChange;
        candidate->fullProjectionIdentity = fullProjectionController->projectIdentity;
        candidate->fullProjectionRevision = fullProjectionController->revision;
    }
    if (!offline && (hasProjection || deferredHostProjection))
        candidate->reuse = currentHosts;
    // A requested restart (recovery or a state reload) never reuses the running worker.
    if (!offline && recoveryPlugin)
        candidate->reuse.erase(recoveryPlugin->id);
    if (!offline && (fullProjectionController || preparingDeferredNativeAccess))
        for (const auto &spec : candidate->specs)
        {
            // Ordinary capture/save refreshes metadata without reloading sound.
            // An owned structural restoration, however, must actually install a
            // DIFFERENT requested native state. Keep unchanged workers, prepare
            // changed ones privately; never report reuse as state restoration.
            const auto old = candidate->reuse.find(spec.id);
            if (old != candidate->reuse.end() && old->second &&
                old->second->recoveryState() != spec.state)
                candidate->reuse.erase(old);
        }
    if (!candidate->startThread())
        return juce::Result::fail("Cannot start VST3 preparation thread; previous playback retained");
    // Publish only a fully constructed, actually started job. Failed reuse-map
    // copies or thread admission must never leave an unstarted pending job.
    job = std::move(candidate);
    return juce::Result::fail("Preparing VST3 workers...");
}
juce::Result AudioEngine::prepareDeferredNativeHosts(const Project &target, PreparedDeferredRouting &next)
{
    const bool full = fullProjectionController && fullProjectionChange;
    if ((!full && !preparingDeferredNativeAccess) || !canDeferChannelRouting())
        return juce::Result::fail(
            "routing.deferredNativeOwner: Exact initial full-projection owner required");
    if (auto ready = preparePlugins(target, false); ready.failed())
        return ready; // Existing exact target-stamped async owner; cancellation drains it.
    auto &job = preparation[0];
    if (job && (!job->done.load(std::memory_order_acquire) || job->isThreadRunning() ||
                !job->waitForThreadToExit(0)))
        return juce::Result::fail("Preparing VST3 workers...");
    const bool exactFull =
        !job ||
        (full && job->fullProjectionDestination && job->fullProjectionOwner == fullProjectionController &&
         job->fullProjectionTicket == fullProjectionChange &&
         job->fullProjectionIdentity == fullProjectionController->projectIdentity &&
         job->fullProjectionRevision == fullProjectionController->revision &&
         job->fullProjectionDestination->state().isEquivalentTo(target.state()));
    const bool exactOrdinary =
        !job || (!full && job->deferredNativeAccess && !job->fullProjectionDestination &&
                 job->fullProjectionOwner == routingController &&
                 (!routingController || job->fullProjectionIdentity == routingController->projectIdentity));
    if (job && ((!exactFull && !exactOrdinary) || job->fullProjectionPublished || job->transportPlan))
        return juce::Result::fail("routing.deferredNativeOwner: Exact device-free worker target changed");
    if (preparingDeferredNativeAccess)
        for (const auto &channel : target.channels)
            if (!channel.audioClip)
            {
                std::shared_ptr<const Sample> sample;
                if (auto ready = samples.prepare(SampleCache::preparationKey(target, channel), sample);
                    ready.failed())
                    return ready;
                next.samples.emplace(channel.id, std::move(sample));
                next.sampleSources.emplace(channel.id, channel.source);
            }
    next.nativeProject = target;
    next.nativeRate = job ? job->rate : engine.getDeviceManager().getSampleRate();
    next.nativeBlock = job ? job->block : engine.getDeviceManager().getBlockSize();
    if (next.nativeRate <= 0)
        next.nativeRate = 48000;
    if (next.nativeBlock <= 0)
        next.nativeBlock = 256;
    next.nativeOwners = ownedHosts;
    const auto collect = [&](const PluginSpec &spec) -> juce::Result
    {
        if (!job)
            return juce::Result::fail("routing.deferredNativeMissing: Native preparation result absent");
        const auto found = job->results.find(spec.id);
        if (found == job->results.end())
            return juce::Result::fail("routing.deferredNativeMissing: Native preparation was superseded");
        const auto &host = found->second.host;
        if (!host)
        {
            next.nativeErrors[spec.id] = found->second.error;
            return juce::Result::ok(); // Genuine existing missing-plugin/state placeholder policy.
        }
        if (!host->alive() || host->offline() ||
            !host->matches(spec, next.nativeRate, next.nativeBlock, pluginBusRequestFor(spec)))
            return juce::Result::fail("routing.deferredNativeChanged: Prepared native owner changed");
        next.nativeHosts.emplace(spec.id, host);
        if (std::find(next.nativeOwners.begin(), next.nativeOwners.end(), host) == next.nativeOwners.end())
            next.nativeOwners.push_back(host);
        return juce::Result::ok();
    };
    for (const auto &channel : target.channels)
        if (channel.instrument)
            if (auto ready = collect(*channel.instrument); ready.failed())
                return ready;
    for (const auto &insert : target.mixer)
        for (const auto &effect : insert.effects)
            if (auto ready = collect(effect); ready.failed())
                return ready;
    return juce::Result::ok(); // All maps/metadata prepared BEFORE commit; completed job already joined.
}
void AudioEngine::publishDeferredNativeHosts(PreparedDeferredRouting &next) noexcept
{
    // CONTROL only, immediately after commit + ordinary context retirement. No
    // Edit, graph or DSP gate is constructed; all publication uses preowned swaps.
    currentHosts.swap(next.nativeHosts); // Empty destination removes ALL old eligibility.
    pluginErrors.swap(next.nativeErrors);
    ownedHosts.swap(next.nativeOwners);
    using std::swap;
    swap(projectedProject, next.nativeProject);
    if (preparingDeferredNativeAccess)
    {
        // Ordinary no-device source edits refresh immutable PCM and provenance
        // together. Native metadata alone must never relabel an old sample bank.
        deferredSamples.swap(next.samples);
        deferredSampleSources.swap(next.sampleSources);
        deferredSampleProjection = true;
    }
    projectedRate = next.nativeRate;
    projectedBlock = next.nativeBlock;
    deferredHostProjection = !currentHosts.empty() || !pluginErrors.empty();
    if (preparation[0])
    {
        preparation[0]->fullProjectionPublished = true;
        // Actual startup exit/join was checked before publication. Every live
        // result and reuse owner is pinned in nativeOwners/currentHosts. Retire
        // the completed CONTROL job now so an immediate Open is not mistaken
        // for a foreign pending preparation and routed into another rebuild.
        preparation[0].reset();
    }
}
juce::Result AudioEngine::drainFullProjectionPreparation(const Project &exactDestination, bool requestCancel,
                                                         bool &drained)
{
    drained = false;
    const auto *messages = juce::MessageManager::getInstanceWithoutCreating();
    if (!messages || !messages->isThisTheMessageThread() || fullProjectionController)
        return juce::Result::fail("routing.preparationDrainOwner: Idle CONTROL owner required");
    auto &job = preparation[0];
    if (!job)
    {
        drained = true;
        return juce::Result::ok(); // Nothing is signalled or changed.
    }
    try
    {
        if (!job->fullProjectionDestination || job->fullProjectionPublished ||
            !job->fullProjectionDestination->state().isEquivalentTo(exactDestination.state()))
            return juce::Result::fail(
                "routing.foreignFullProjectionPreparation: Exact unpublished owned target required");
        if (requestCancel)
            job->signalThreadShouldExit();
        if (!job->done.load(std::memory_order_acquire) || job->isThreadRunning())
            return juce::Result::ok(); // Cancellation admission is not actual startup completion.
        if (!job->waitForThreadToExit(0))
            return juce::Result::ok();
        // Reserve before changing ownership. Reused incumbent workers must not
        // be retired; only this job's genuinely private results need rehoming.
        ownedHosts.reserve(ownedHosts.size() + job->results.size());
        for (const auto &[id, result] : job->results)
            if (result.host &&
                std::none_of(job->reuse.begin(), job->reuse.end(),
                             [&](const auto &entry) { return entry.second == result.host; }) &&
                std::none_of(currentHosts.begin(), currentHosts.end(),
                             [&](const auto &entry) { return entry.second == result.host; }) &&
                std::find(ownedHosts.begin(), ownedHosts.end(), result.host) == ownedHosts.end())
                ownedHosts.push_back(result.host);
        job.reset();               // Thread actually exited; shared workers now have CONTROL owners.
        retireUnusedPluginHosts(); // Existing graceful/owned async process-retirement service.
        drained = true;
        return juce::Result::ok();
    }
    catch (...)
    {
        return juce::Result::fail(
            "routing.preparationDrainFailed: Private ownership retained; retry CONTROL drain");
    }
}

bool AudioEngine::pluginDeviceConfigurationChanged() const
{
    // A device-free native-access bank has no physical configuration to drift.
    // Device appearance still enters the existing promotion/capture/rebuild path.
    if (deferredDocumentProjection && !engine.getDeviceManager().deviceManager.getCurrentAudioDevice())
        return false;
    if (currentHosts.empty())
        return false;
    if (productionDeviceInvalidated || engine.getDeviceManager().getSampleRate() != projectedRate ||
        engine.getDeviceManager().getBlockSize() != projectedBlock)
        return true;
    if (productionPluginProjection)
    {
        juce::AudioDeviceManager::PhysicalCallbackSnapshot physical;
        const auto &plan = productionPluginProjection->plan;
        return !plan->read(physical) || !physical.ownerAlive || !physical.active || physical.writerConflict ||
               physical.countersExhausted || physical.deviceEpoch != plan->budget().deviceEpoch ||
               physical.maximumOuterFrames > plan->budget().physicalFrames;
    }
    return false;
}
AudioEngine::AudioEngine(bool enableDevice, juce::File guidePreferences, int explicitProviderFrames)
    : explicitPhysicalProviderFrames(explicitProviderFrames),
      engine(std::make_unique<EngineStorage>(enableDevice), nullptr, std::make_unique<EnginePolicy>()),
      midiInput(enableDevice)
{
    if (explicitProviderFrames < 0)
        throw std::invalid_argument("Negative physical provider frame bound");
    disableProcessPowerThrottling(); // Hidden test/MCP hosts are otherwise throttled like background apps.
    channelRouting = std::make_unique<ChannelRoutingState>();
    // Keep rendering/MIDI advancing after an overrun. The upstream cached CPU
    // guard otherwise inserts whole silent callbacks during meter recovery.
    // CPU/xrun measurements remain available; this cannot repair a missed deadline.
    engine.getDeviceManager().setCpuLimitBeforeMuting(std::numeric_limits<double>::max());
    // VibeRa owns MIDI input/device selection through MidiInputRouter.
    // Tracktion's unused periodic hardware-MIDI scan clears active audio nodes
    // when its device list changes, interrupting our retained live projection.
    engine.getDeviceManager().setMidiDeviceScanIntervalSeconds(0);
    engine.getPluginManager().createBuiltInType<FixedSampler>();
    engine.getPluginManager().createBuiltInType<RemoteEffect>();
    engine.getPluginManager().createBuiltInType<GuidePlugin>();
    engine.getPluginManager().createBuiltInType<AutomationGain>();
    engine.getPluginManager().createBuiltInType<ChannelBalance>();
    engine.getPluginManager().createBuiltInType<MixerSendPlugin>();
    engine.getPluginManager().createBuiltInType<MixerSidechainSend>();
    engine.getPluginManager().createBuiltInType<MixerMasterGate>();
    engine.getPluginManager().createBuiltInType<MultiOutputDependencySend>();
    engine.getPluginManager().createBuiltInType<MultiOutputTap>();
    persistentGuide = enableDevice || guidePreferences != juce::File{};
    guideFile = guidePreferences != juce::File{}
                    ? guidePreferences
                    : juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                          .getChildFile("VibeRa Studio/guide-settings.json");
    if (persistentGuide)
    {
        auto prefs = juce::JSON::parse(guideFile);
        guide->enabled = bool(prefs["metronome"]);
        guide->sound = juce::jlimit(0, 2, int(prefs["sound"]));
        guide->gain = prefs.hasProperty("gain") && std::isfinite(float(prefs["gain"]))
                          ? juce::jlimit(0.f, 1.f, float(prefs["gain"]))
                          : .65f;
        guideBars = juce::jlimit(0, 2, int(prefs["countInBars"]));
        lastGuideBars = prefs.hasProperty("lastBars") ? juce::jlimit(1, 2, int(prefs["lastBars"])) : 1;
    }
    engine.getDeviceManager().deviceManager.addAudioCallback(&preview);
    if (enableDevice)
    {
        engine.getDeviceManager().initialise(0, 2);
        engine.getDeviceManager().deviceManager.addAudioCallback(&inputCapture);
    }
    deviceEverActive.store(engine.getDeviceManager().deviceManager.getCurrentAudioDevice() != nullptr, std::memory_order_release);
    if (!engine.getDeviceManager().installPreparationFence(this, &AudioEngine::beforeDeviceReprepare))
        throw std::logic_error("routing.deviceFenceOwner: Fresh engine preparation owner required");
}
AudioEngine::~AudioEngine()
{
    const bool removedDeviceFence = engine.getDeviceManager().removePreparationFence(this);
    jassert(removedDeviceFence); // Sole CONTROL teardown cannot reenter its own fence.
    (void)removedDeviceFence;
    for (auto &job : preparation)
        if (job)
            job->signalThreadShouldExit();
    for (auto &job : retiredPreparation)
        job->signalThreadShouldExit();
    midiInput.close();
    midiInput.select(nullptr);
    typingInput.close();
    typingInput.select(nullptr);
    if (worker)
        finishRender();
    stop();
    engine.getDeviceManager().deviceManager.removeAudioCallback(&inputCapture);
    inputCapture.cancel();
    engine.getDeviceManager().deviceManager.removeAudioCallback(&preview);
    projectedChannels.clear();
    projectedAudioClips.clear();
    projectedInserts.clear();
    effectPlugins.clear();
    compensationGraph.reset(); // Its listener/callback must not outlive the owning Edit.
    releaseChannelRoutingForEdit();
    edit.reset(); // Native context exclusion retires its session/observer first.
    productionPluginProjection.reset();
    productionPluginPlan.reset();
    initialSourceTransport.reset(); // CONTROL, no last worker/plan owner released in DSP.
    finiteEffectBaseline.reset();   // Track/Runtime views AFTER native Edit destruction, CONTROL only.
    currentHosts.clear();
    capture.reset();
    ownedHosts.clear();
}
static te::TimePosition seconds(double value)
{
    return te::TimePosition::fromSeconds(value);
}
te::TimeRange AudioEngine::loopRange(const Project &p, bool patternMode, double duration) const
{
    const auto map = projectMap(p, patternMode);
    if (!patternMode && songLoop && songLoop->second > songLoop->first)
        return {seconds(map.secondsAt(songLoop->first)), seconds(map.secondsAt(songLoop->second))};
    return {seconds(0), seconds(map.secondsAt(duration))};
}
std::unique_ptr<te::Edit>
AudioEngine::buildEdit(const Project &project, bool patternMode, Id selectedPattern, te::Edit::EditRole role,
                       juce::String &error, std::shared_ptr<ProductionPluginProjection> *productionOutput)
{
    auto valid = project.validate();
    if (valid.failed())
    {
        error = valid.getErrorMessage();
        return {};
    }
    auto *repeatDevice = engine.getDeviceManager().deviceManager.getCurrentAudioDevice();
    const int repeatBlock = role == te::Edit::forRendering ? renderBlock
                            : repeatDevice                 ? repeatDevice->getCurrentBufferSizeSamples()
                                                           : 512;
    const double repeatRate = role == te::Edit::forRendering ? renderRate
                              : repeatDevice                 ? repeatDevice->getCurrentSampleRate()
                                                             : 48000.;
    if (auto admitted =
            validateRepeatMidiBudget(project, patternMode, selectedPattern, repeatBlock, repeatRate);
        admitted.failed())
    {
        error = admitted.getErrorMessage();
        return {};
    }
    MixerNativeIdentity mixerIdentity;
    if (MixerNativeIdentity::prepare(project, mixerIdentity) != MixerNativeIdentity::Error::none)
    {
        error = "mixer.nativeIdentity.invalidUniverse: Cannot prepare native Mixer identity";
        return {};
    }
    const bool ordinaryLive =
        role != te::Edit::forRendering && !initialSourceTransport && !finiteEffectBaseline;
    PreparedPluginTransport::Ptr sourcePlan;
    if (role != te::Edit::forRendering && initialSourceTransport)
        sourcePlan = initialSourceTransport->plan;
    else if (ordinaryLive)
        if (auto ready = prepareProductionPluginPlan(project, sourcePlan); ready.failed())
        {
            error = ready.getErrorMessage();
            return {};
        }
    if (initialSourceTransport && sourcePlan)
    {
        if (auto admitted = validateInitialSourceProject(project); admitted.failed())
        { error = admitted.getErrorMessage(); return {}; }
        if (initialSourceTransport->installationAttempted)
        { error = "source.initialOnly: A new context needs separately qualified lifecycle/membership"; return {}; }
    }
    te::Edit::Options options{engine, te::createEmptyEdit(engine), {}};
    options.role = role;
    // Even a device-free editor is dormant: a later device must first prepare
    // its actual transport and native state before this worker can process.
    options.requireSourceBurstSession =
        ordinaryLive && (std::any_of(project.channels.begin(), project.channels.end(),
                                     [](const auto &c) { return bool(c.instrument); }) ||
                         std::any_of(project.mixer.begin(), project.mixer.end(),
                                     [](const auto &m) { return !m.effects.empty(); }));
    // Ordinary baseline only: no LOCAL privilege or fixed source-epoch fence.
    // Options owns this policy before the first native context can be created.
    ordinary_sample_ports::Policy::Ptr ordinaryPorts;
    const bool hasDetectorAssignments =
        std::any_of(project.mixer.begin(), project.mixer.end(),
                    [](const auto &row)
                    {
                        return std::any_of(row.effects.begin(), row.effects.end(),
                                           [](const auto &effect) { return effect.detector.has_value(); });
                    });
    // The optimized ordinary sampler policy intentionally has no detector/send
    // ownership contract. Select the existing general graph before construction.
    const bool hasBusSends =
        std::any_of(project.mixer.begin(), project.mixer.end(),
                    [](const auto &row)
                    {
                        return std::any_of(row.routes.begin(), row.routes.end(),
                                           [](const auto &route) { return route.target != 0; });
                    });
    // Multi-output instruments add output tracks and order sends: the general graph owns them.
    if (ordinaryLive && sourcePlan && !hasDetectorAssignments && !hasBusSends && multiOutputTrackCount(project) == 0)
    {
        const auto *device = engine.getDeviceManager().deviceManager.getCurrentAudioDevice();
        ordinaryPorts = ordinary_sample_ports::Policy::prepare(
            project, sourcePlan, device && device->getActiveOutputChannels().countNumberOfSetBits() == 2);
        if (ordinaryPorts)
            options.configureNodeGraphForPreparation =
                [ordinaryPorts](te::Edit &candidate, te::CreateNodeParams &params)
            {
                ordinaryPorts->install(
                    candidate, params,
                    [](te::Plugin &plugin)
                    {
                        using B = ordinary_sample_ports::Policy::Binding;
                        if (auto *p = dynamic_cast<FixedSampler *>(&plugin))
                            return B{B::Role::sampler, p->runtimeOwnerControl(), p->runtimeEpochControl()};
                        if (auto *p = dynamic_cast<ChannelBalance *>(&plugin))
                            return B{B::Role::channelBalance, p->runtimeOwnerControl()};
                        if (auto *p = dynamic_cast<AutomationGain *>(&plugin))
                            return B{B::Role::mixerGain, p->runtimeOwnerControl()};
                        if (auto *p = dynamic_cast<GuidePlugin *>(&plugin))
                            return B{B::Role::guide, p->runtimeOwnerControl()};
                        if (auto *p = dynamic_cast<RemoteEffect *>(&plugin))
                            return B{B::Role::effect, p->runtimeOwnerControl()};
                        if (auto *p = dynamic_cast<MixerMasterGate *>(&plugin))
                            return B{B::Role::masterGate, p->runtimeOwnerControl()};
                        if (typeid(plugin) == typeid(te::AuxReturnPlugin))
                            return B{B::Role::auxReturn, {}};
                        if (typeid(plugin) == typeid(te::VolumeAndPanPlugin) && !plugin.isEnabled())
                            return B{B::Role::disabledBalance, {}};
                        return B{};
                    });
            };
    }
    if (initialSourceTransport && sourcePlan)
    {
        options.initialSourceGeneration = initialSourceTransport->options.sessionGeneration;
        options.closedNativeIO = true; // Does not alter any Node hasMidi flag.
        options.disableNodeMemorySharing = initialSourceTransport->options.audioZeroHistoryProofs;
        if (initialSourceTransport->audioPorts)
            options.configureNodeGraphForPreparation = [weak = std::weak_ptr<experimental::AppMixerAudioPorts>(initialSourceTransport->audioPorts)](te::Edit& candidate, te::CreateNodeParams& params)
            {
                const auto policy = weak.lock();
                if (!policy) throw std::invalid_argument("appAudioPort.ownerGone: CONTROL policy retired");
                policy->install(candidate, params, [](te::Plugin& plugin)
                {
                    using B = experimental::AppMixerAudioPorts::Binding;
                    if (auto* p = dynamic_cast<FixedSampler*>(&plugin)) return B{B::Role::sampler,p->runtimeOwnerControl(),p->runtimeEpochControl()};
                    if (auto* p = dynamic_cast<ChannelBalance*>(&plugin)) return B{B::Role::channelBalance,p->runtimeOwnerControl()};
                    if (auto* p = dynamic_cast<AutomationGain*>(&plugin)) return B{B::Role::mixerGain,p->runtimeOwnerControl()};
                    if (auto* p = dynamic_cast<GuidePlugin*>(&plugin)) return B{B::Role::guide,p->runtimeOwnerControl()};
                    if (auto* p = dynamic_cast<RemoteEffect*>(&plugin)) return B{B::Role::effect,p->runtimeOwnerControl()};
                    if (auto* p = dynamic_cast<MixerMasterGate*>(&plugin)) return B{B::Role::masterGate,p->runtimeOwnerControl()};
                    if (dynamic_cast<MixerSendPlugin*>(&plugin)) return B{B::Role::send,{}};
                    if (dynamic_cast<MixerSidechainSend*>(&plugin)) return B{B::Role::sidechain,{}};
                    if (dynamic_cast<te::AuxReturnPlugin*>(&plugin)) return B{B::Role::auxReturn,{}};
                    if (auto* p = dynamic_cast<te::VolumeAndPanPlugin*>(&plugin); p && !p->isEnabled()) return B{B::Role::disabledBalance,{}};
                    return B{}; // Unknown/frozen/external/native processing refuses before factory.
                });
            };
    }
    if (finiteEffectBaseline && role != te::Edit::forRendering)
    {
        if (auto valid = validateFiniteEffectBaseline(project); valid.failed())
        {
            error = valid.getErrorMessage();
            return {};
        }
        if (finiteEffectBaseline->editCreated)
        {
            error = "finite.baseline.initialOnly: No new context/owner migration";
            return {};
        }
        options.closedNativeIO = true;
        options.disableNodeMemorySharing = true;
        // Genuine preowned LOCAL source authority, not a guessed generation:
        // SourceBurstSession/configuration already exist and must be installed
        // before the first callback. Anchor Host remains ordinary1Q.
        options.initialSourceGeneration = finiteEffectBaseline->session->configuration().generation;
        options.configureNodeGraphForPreparation =
            [weak = std::weak_ptr<FiniteEffectBaselineState>(finiteEffectBaseline)](
                te::Edit &candidate, te::CreateNodeParams &params)
        {
            const auto owner = weak.lock();
            if (!owner || owner->invalidated.load(std::memory_order_acquire))
                throw std::invalid_argument("finite.local.ownerGone: CONTROL sample authority retired");
            owner->observer->bindScope(params.processState);
            for (auto *track : te::getAudioTracks(candidate))
                for (auto *plugin : track->pluginList)
                    if (auto *source = dynamic_cast<FixedSampler *>(plugin);
                        source && !owner->observer->matchesControl(*source))
                        throw std::invalid_argument(
                            "finite.local.sourceChanged: Actual fixed sample Runtime/epoch changed");
            const auto &policy = owner->ports;
            policy->install(
                candidate, params,
                [](te::Plugin &plugin)
                {
                    using B = experimental::AppMixerAudioPorts::Binding;
                    if (auto *p = dynamic_cast<FixedSampler *>(&plugin))
                        return B{B::Role::sampler, p->runtimeOwnerControl(), p->runtimeEpochControl()};
                    if (auto *p = dynamic_cast<ChannelBalance *>(&plugin))
                        return B{B::Role::channelBalance, p->runtimeOwnerControl()};
                    if (auto *p = dynamic_cast<AutomationGain *>(&plugin))
                        return B{B::Role::mixerGain, p->runtimeOwnerControl()};
                    if (auto *p = dynamic_cast<GuidePlugin *>(&plugin))
                        return B{B::Role::guide, p->runtimeOwnerControl()};
                    if (auto *p = dynamic_cast<RemoteEffect *>(&plugin))
                        return B{B::Role::effect, p->runtimeOwnerControl()};
                    if (auto *p = dynamic_cast<MixerMasterGate *>(&plugin))
                        return B{B::Role::masterGate, p->runtimeOwnerControl()};
                    if (dynamic_cast<te::AuxReturnPlugin *>(&plugin))
                        return B{B::Role::auxReturn, {}};
                    if (auto *p = dynamic_cast<te::VolumeAndPanPlugin *>(&plugin); p && !p->isEnabled())
                        return B{B::Role::disabledBalance, {}};
                    return B{};
                });
        };
    }
    // Channel tracks, then non-Master insert tracks, then multi-output instrument output tracks.
    options.numAudioTracks =
        juce::uint32(project.channels.size() + project.mixer.size() - 1 + size_t(multiOutputTrackCount(project)));
    options.defaultMasterVolumedB = 0;
    auto result = te::Edit::createEdit(options);
    if (!result)
    {
        error = "Engine could not create Edit";
        return {};
    }
    // Atomic MixerMeter supplies UI/MCP. Unused native meters register clients
    // and take callback locks. The pinned adapter honours this app-owned opt-in.
    result->state.setProperty("viberaOwnMetering", true, nullptr);
    for (auto *track : te::getAudioTracks(*result))
    {
        const auto plugins = track->pluginList.getPlugins();
        for (auto *plugin : plugins)
            if (dynamic_cast<te::LevelMeterPlugin *>(plugin))
                plugin->deleteFromParent();
    }
    result->tempoSequence.getTempo(0)->setBpm(project.tempo);
    result->clickTrackEnabled = false;
    const auto clockMeter = playbackMeter(project, patternMode, selectedPattern);
    result->tempoSequence.getTimeSig(0)->setStringTimeSig(juce::String(clockMeter.first) + "/" +
                                                          juce::String(clockMeter.second));
    auto tracks = te::getAudioTracks(*result);
    std::map<int, te::AudioTrack *> inserts;
    for (auto &m : project.mixer)
    {
        if (m.index == 0)
        {
            result->getMasterVolumePlugin()->setEnabled(false);
            continue;
        }
        const int slot = *mixerIdentity.slot(m.index);
        auto *t = tracks[int(project.channels.size()) + slot - 1];
        t->state.setProperty("viberaMixerIndex", m.index, nullptr);
        t->state.setProperty("viberaMixerBus", *mixerIdentity.bus(m.index), nullptr);
        t->setName(m.name);
        inserts[m.index] = t;
        t->getOutput().setOutputToDefaultDevice(false);
        t->getVolumePlugin()->setEnabled(false);
    }
    auto liveTimeline = std::make_shared<LiveTimeline>();
    ownedTimelines.push_back(liveTimeline);
    auto document = compileLiveDocument(project, patternMode, selectedPattern, ++timelineGeneration);
    if (!document)
    {
        error = "Cannot prepare automation";
        return {};
    }
    std::map<Id, std::shared_ptr<PluginHost>> projectionHosts;
    // These references must be released while their owning temporary Edit is
    // still alive on failure. Commit them only after the complete graph succeeds.
    std::map<Id, te::Plugin::Ptr> projectionEffects;
    auto prepareHost = [&](const PluginSpec &spec) -> std::shared_ptr<PluginHost>
    {
        auto host = sourcePlan ? std::make_shared<PluginHost>(sourcePlan) : std::make_shared<PluginHost>();
        const bool offline = role == te::Edit::forRendering;
        auto rate = offline ? renderRate : engine.getDeviceManager().getSampleRate();
        auto block = offline ? renderBlock : engine.getDeviceManager().getBlockSize();
        if (rate <= 0)
            rate = 48000;
        if (block <= 0)
            block = 256;
        auto &job = preparation[size_t(offline)];
        auto prepared = juce::Result::ok();
        if (job && job->done.load(std::memory_order_acquire))
        {
            auto it = job->results.find(spec.id);
            if (it != job->results.end())
            {
                host = it->second.host;
                if (!host)
                    prepared = juce::Result::fail(it->second.error);
            }
            else
                prepared = juce::Result::fail("Plugin preparation was superseded");
        }
        else
            prepared = host->start(spec, rate, block, offline, {}, pluginBusRequestFor(spec));
        if (prepared.wasOk() && host &&
            (host->offline() != offline ||
             (sourcePlan ? !host->matchesPrepared(spec, rate, block, sourcePlan, pluginBusRequestFor(spec))
                         : !host->matches(spec, rate, block, pluginBusRequestFor(spec)))))
            prepared = juce::Result::fail("plugin.preparedConfigurationChanged: Candidate "
                                          "descriptor/device/layout does not match the prepared worker");
        if (prepared.failed())
        {
            if (offline)
            {
                error = prepared.getErrorMessage();
                return {};
            }
            pluginErrors[spec.id] = prepared.getErrorMessage();
            return {}; // project retains descriptor/state as a visible recoverable placeholder
        }
        if (std::find(ownedHosts.begin(), ownedHosts.end(), host) == ownedHosts.end())
            ownedHosts.push_back(host); // control-thread lifetime pin
        projectionHosts[spec.id] = host;
        if (!offline)
        {
            currentHosts[spec.id] = host;
            auto it = observedParameterRevisions.find(spec.id);
            if (it == observedParameterRevisions.end() || it->second.host != host)
                observedParameterRevisions[spec.id] = {host, host->parameterRevision()};
        }
        return host;
    };
    // Private per-Edit detector buses do not enter project data. One tap can
    // supply multiple detectors; audible-send buses occupy the preceding range.
    std::map<int, int> detectorBuses;
    for (const auto &row : project.mixer)
        for (const auto &effect : row.effects)
            if (effect.detector)
                detectorBuses.emplace(effect.detector->sourceMixer, 0);
    int detectorBus = MixerNativeIdentity::firstBus + MixerNativeIdentity::capacity;
    for (auto &[source, bus] : detectorBuses)
        bus = detectorBus++;
    int multiOutputBus = detectorBus; // Private order buses for multi-output instruments.
    struct MultiOutput
    {
        std::shared_ptr<PluginHost> host;
        juce::String name;
        int bus, orderBus, target;
    };
    std::vector<MultiOutput> multiOutputs;
    for (auto &m : project.mixer)
    {
        auto &list = m.index == 0 ? result->getMasterPluginList() : inserts.at(m.index)->pluginList;
        int position = 0;
        if (m.index != 0)
        {
            auto receive = result->getPluginCache().createNewPlugin(te::AuxReturnPlugin::xmlTypeName, {});
            auto *aux = dynamic_cast<te::AuxReturnPlugin *>(receive.get());
            aux->busNumber = *mixerIdentity.bus(m.index);
            list.insertPlugin(receive, position++, nullptr);
        }
        for (auto &spec : m.effects)
        {
            auto remote = result->getPluginCache().createNewPlugin(RemoteEffect::xmlTypeName, {});
            auto host = prepareHost(spec);
            if (role == te::Edit::forRendering && !host)
                return {};
            auto *effect = dynamic_cast<RemoteEffect *>(remote.get());
            effect->configure(host, spec.bypass || m.effectsBypassed, liveTimeline, spec.id, spec.wet);
            if (spec.detector)
            {
                const auto *before =
                    fullProjectionController ? fullProjectionController->project.plugin(spec.id) : nullptr;
                const bool assigning =
                    fullProjectionController &&
                    (!before || before->detector != spec.detector || before->descriptor != spec.descriptor ||
                     before->instrument != spec.instrument);
                if (!host && (assigning || role == te::Edit::forRendering))
                {
                    error = "sidechain.layoutUnavailable: Detector worker preparation failed: " +
                            pluginErrors[spec.id];
                    return {};
                }
                // Existing missing-plugin documents retain a recoverable dry
                // placeholder. A NEW layout assignment cannot silently succeed.
                if (host)
                    if (auto connected =
                            effect->connectSidechain(inserts.at(spec.detector->sourceMixer)->itemID,
                                                     detectorBuses.at(spec.detector->sourceMixer));
                        connected.failed())
                    {
                        error = connected.getErrorMessage();
                        return {};
                    }
            }
            if (ordinaryPorts && host)
            {
                const auto history = host->prepareDryHistoryRetention();
                if (!history)
                {
                    error = "effects.historyUnavailable: Actual prepared Host history required";
                    return {};
                }
                const auto observed =
                    effect->prepareOutputObservation(history->history, history->identity, history->writer);
                if (observed.failed())
                {
                    error = observed.getErrorMessage();
                    return {};
                }
            }
            list.insertPlugin(remote, position++, nullptr);
            if (role != te::Edit::forRendering)
                projectionEffects[spec.id] = remote;
        }
        auto gain = result->getPluginCache().createNewPlugin(AutomationGain::xmlTypeName, {});
        const auto *nativeVolume =
            m.index == 0 ? result->getMasterVolumePlugin().get() : inserts.at(m.index)->getVolumePlugin();
        dynamic_cast<AutomationGain *>(gain.get())->configure(liveTimeline, m, nativeVolume->getPanLaw());
        list.insertPlugin(gain, position, nullptr);
        if (m.index != 0)
        {
            if (const auto detector = detectorBuses.find(m.index); detector != detectorBuses.end())
            {
                auto tap = result->getPluginCache().createNewPlugin(MixerSidechainSend::xmlTypeName, {});
                dynamic_cast<MixerSidechainSend *>(tap.get())->configure(detector->second);
                list.insertPlugin(tap, list.getPlugins().size(), nullptr);
            }
            for (const auto &route : m.routes)
                if (route.target != 0)
                {
                    auto send = result->getPluginCache().createNewPlugin(MixerSendPlugin::xmlTypeName, {});
                    dynamic_cast<MixerSendPlugin *>(send.get())
                        ->configure(liveTimeline, m.index, route.target,
                                    *mixerIdentity.bus(route.target), route.gain);
                    list.insertPlugin(send, list.getPlugins().size(), nullptr);
                }
            auto gate = result->getPluginCache().createNewPlugin(MixerMasterGate::xmlTypeName, {});
            const auto *master = mixerRoute(project, m.index, 0);
            dynamic_cast<MixerMasterGate *>(gate.get())
                ->configure(liveTimeline, m.index, master ? master->gain : 0);
            list.insertPlugin(gate, list.getPlugins().size(), nullptr);
        }
        if (m.index == 0 && role == te::Edit::forEditing)
        {
            auto plugin = result->getPluginCache().createNewPlugin(GuidePlugin::xmlTypeName, {});
            dynamic_cast<GuidePlugin *>(plugin.get())->configure(guide, liveTimeline);
            list.insertPlugin(plugin, 0, nullptr);
        }
    }
    auto *selected = project.pattern(selectedPattern);
    const double duration = std::max(.25, patternMode && selected ? selected->length : project.endBeat());
    const auto songMap = patternMode ? TempoMap(project.tempo, {}) : project.songTempo();
    std::vector<SamplerCutInput> cutInputs;
    for (size_t i = 0; i < project.channels.size(); ++i)
    {
        auto &channel = project.channels[i];
        auto *track = tracks[int(i)];
        track->setName(channel.name);
        // Resolve settings off the callback. Even unity/centre native balance
        // takes a VCA lock, so bypass its DSP and retain only the engine object.
        auto *nativeVolume = track->getVolumePlugin();
        auto balance = result->getPluginCache().createNewPlugin(ChannelBalance::xmlTypeName, {});
        dynamic_cast<ChannelBalance *>(balance.get())
            ->configure(liveTimeline, channel, nativeVolume->getPanLaw());
        nativeVolume->setEnabled(false);
        track->pluginList.insertPlugin(balance, 0, nullptr);
        if (channel.audioClip)
        {
            te::AudioFile source(engine, juce::File(channel.source));
            if (!source.isValid())
            {
                error = "Unreadable audio source: " + channel.name;
                return {};
            }
            if (channel.mixer == 0)
                track->getOutput().setOutputToDefaultDevice(false);
            else
                track->getOutput().setOutputToTrack(inserts.at(channel.mixer));
            if (!patternMode)
                for (const auto &placement : project.audioPlacements)
                    if (placement.channel == channel.id)
                    {
                        const double start = songMap.secondsAt(placement.beat);
                        auto wave =
                            track->insertWaveClip(channel.name, juce::File(channel.source),
                                                  {{seconds(start), seconds(start + placement.duration)},
                                                   te::TimeDuration::fromSeconds(placement.offset)},
                                                  false);
                        if (!wave)
                        {
                            error = "Cannot create audio clip";
                            return {};
                        }
                        wave->setAutoTempo(false);
                        wave->setAutoPitch(false);
                        wave->setUsesProxy(false);
                        wave->setLoopRange({});
                        wave->setMuted(placement.muted || project.laneMuted(placement.lane));
                        applyClipFades(*wave, placement);
                        wave->state.setProperty("viberaPlacement", juce::int64(placement.id), nullptr);
                    }
            continue;
        }
        auto plugin = result->getPluginCache().createNewPlugin(FixedSampler::xmlTypeName, {});
        auto *sampler = dynamic_cast<FixedSampler *>(plugin.get());
        if (!sampler)
        {
            error = "Sampler creation failed";
            return {};
        }
        std::shared_ptr<const Sample> source;
        auto prepared =
            samples.prepare(SampleCache::preparationKey(project, channel), source);
        if (prepared.failed())
        {
            error = prepared.getErrorMessage();
            return {};
        }
        if (std::find(ownedSamples.begin(), ownedSamples.end(), source) == ownedSamples.end())
            ownedSamples.push_back(source);
        sampler->setPreparedSource(std::move(source));
        sampler->setTimeline(liveTimeline, channel.id);
        sampler->setCutClock(samplerCutClock);
        if (role == te::Edit::forEditing)
        {
            sampler->setInput(midiInput.createLane());
            sampler->setTypingInput(typingInput.createLane());
            sampler->preparePreviewInput();
            sampler->setGuide(guide);
        }
        if (channel.instrument)
        {
            auto host = prepareHost(*channel.instrument);
            if (role == te::Edit::forRendering && !host)
                return {};
            if (!channel.instrument->extraOutputMixers.empty())
            {
                const int orderBus = multiOutputBus++;
                for (size_t k = 0; k < channel.instrument->extraOutputMixers.size(); ++k)
                    multiOutputs.push_back({host, channel.name, int(k), orderBus,
                                            channel.instrument->extraOutputMixers[k]});
                auto order = result->getPluginCache().createNewPlugin(MultiOutputDependencySend::xmlTypeName, {});
                dynamic_cast<MultiOutputDependencySend *>(order.get())->configure(orderBus);
                track->pluginList.insertPlugin(order, track->pluginList.getPlugins().size(), nullptr);
            }
            sampler->setRemote(std::move(host), channel.instrument->bypass, channel.instrument->id);
            if (initialSourceTransport && sourcePlan && initialSourceTransport->options.zeroStageSourceRoutes)
            {
                try {sampler->prepareInitialSourceEpochFence(sourcePlan,initialSourceTransport->sourceEpochHealth);}
                catch(...)
                {
                    // Fresh private setup only: latch terminal refusal before diagnostic allocation.
                    initialSourceTransport->status=InitialSourceTransportStatus::preparationFailed;
                    error="sourceEpoch.preparationFailed: Fresh source epoch/graph preparation refused";
                    return {};
                }
            }
        }
        ++samplerCreations;
        sampler->appendCutInputs(cutInputs);
        track->pluginList.insertPlugin(plugin, 0, nullptr);
        if (channel.mixer == 0)
            track->getOutput().setOutputToDefaultDevice(false);
        else
            track->getOutput().setOutputToTrack(inserts.at(channel.mixer));
        auto clip =
            track->insertMIDIClip(channel.name, {seconds(0), seconds(songMap.secondsAt(duration))}, nullptr);
        clip->setUsesProxy(false);
        // Empty anchor clips define the initial display/range, never the live note sequence.
        // Routine notes are published through LiveTimeline without touching Edit ValueTrees.
    }
    // Output tracks after the insert tracks, one per extra output bus, in Channel order.
    for (size_t i = 0; i < multiOutputs.size(); ++i)
    {
        const auto &output = multiOutputs[i];
        auto *outputTrack = tracks[int(project.channels.size() + project.mixer.size() - 1 + i)];
        outputTrack->setName(output.name + " out " + juce::String(output.bus + 2));
        outputTrack->state.setProperty("viberaMultiOutputTarget", output.target, nullptr);
        outputTrack->getVolumePlugin()->setEnabled(false);
        auto receive = result->getPluginCache().createNewPlugin(te::AuxReturnPlugin::xmlTypeName, {});
        dynamic_cast<te::AuxReturnPlugin *>(receive.get())->busNumber = output.orderBus;
        outputTrack->pluginList.insertPlugin(receive, 0, nullptr);
        auto tap = result->getPluginCache().createNewPlugin(MultiOutputTap::xmlTypeName, {});
        dynamic_cast<MultiOutputTap *>(tap.get())->configure(output.host, output.bus);
        outputTrack->pluginList.insertPlugin(tap, 1, nullptr);
        if (output.target == 0)
            outputTrack->getOutput().setOutputToDefaultDevice(false);
        else
            outputTrack->getOutput().setOutputToTrack(inserts.at(output.target));
    }
    std::stable_sort(cutInputs.begin(), cutInputs.end(),
                     [](const auto &a, const auto &b) { return a.channel < b.channel; });
    bindLiveCutInputs(*document, cutInputs);
    const auto automationReady = prepareNativeAutomationRoutes(
        project, *document, projectionHosts,
        role == te::Edit::forRendering ? renderRate : engine.getDeviceManager().getSampleRate(),
        role == te::Edit::forEditing);
    if (automationReady.failed())
    {
        error = automationReady.getErrorMessage();
        return {};
    }
    if (!liveTimeline->publish(document))
    {
        error = "Cannot publish prepared native automation";
        return {};
    }
    auto &transport = result->getTransport();
    transport.setLoopRange(loopRange(project, patternMode, duration));
    // Hosted plugins read tempo from the Edit: mirror the Song map into its tempo sequence.
    if (!patternMode && !project.tempoChanges.empty())
    {
        auto &sequence = result->tempoSequence;
        for (const auto &change : project.tempoChanges)
            if (auto tempoSetting = sequence.insertTempo(te::BeatPosition::fromBeats(change.beat), change.bpm, 0.f))
                tempoSetting->setCurve(change.ramp ? 1.f : 0.f);
    }
    transport.looping = patternMode;
    if (initialSourceTransport && sourcePlan)
    {
        const auto installed = installInitialSourceTransport(*result, project);
        if (installed.failed()) { error = installed.getErrorMessage(); return {}; }
    }
    if (finiteEffectBaseline && role != te::Edit::forRendering)
    {
        try
        {
            const auto admitted = finiteEffectBaseline->bindFreshHost(projectionHosts);
            if (admitted.failed())
            {
                finiteEffectBaseline->invalidated.store(true, std::memory_order_release);
                error = admitted.getErrorMessage();
                return {};
            }
        }
        catch (...)
        {
            // Only this fresh finite capability arm; existing Edit/PluginPtr
            // unwind order and rebuild host-map restoration stay authoritative.
            finiteEffectBaseline->invalidated.store(true, std::memory_order_release);
            error = "finite.baseline.preparationFailed: Fresh finite owner/state validation failed";
            return {};
        }
        finiteEffectBaseline->editCreated = true;
        const auto installed = finiteEffectBaseline->install(*result, engine.getDeviceManager());
        if (installed.failed())
        {
            error = installed.getErrorMessage();
            return {};
        }
    }
    if (role != te::Edit::forRendering)
    {
        if (ordinaryLive && sourcePlan)
        {
            if (!productionOutput)
            {
                error = "source.projectionOwnerRequired: Live worker projection requires its CONTROL owner";
                return {};
            }
            const auto staged =
                stageProductionPluginProjection(*result, project, projectionHosts, *productionOutput);
            if (staged.failed())
            {
                error = staged.getErrorMessage();
                return {};
            }
            (*productionOutput)->ports = std::move(ordinaryPorts);
        }
        effectPlugins = std::move(projectionEffects);
    }
    return result;
}
static double durationBeats(const Project &p, bool patternMode, Id selected)
{
    auto *pattern = p.pattern(selected);
    return std::max(.25, patternMode && pattern ? pattern->length : p.endBeat());
}
std::optional<float> AudioEngine::mixerDisplayGain(int insert) const
{
    if (const auto *gain = dynamic_cast<AutomationGain *>(effectiveMixerGain(insert)))
        return gain->displayGain();
    return {};
}
MixerMeterReading AudioEngine::mixerMeter(int insert) const
{
    if (const auto *gain = dynamic_cast<AutomationGain *>(effectiveMixerGain(insert)))
        return gain->meterReading();
    return {};
}
juce::Result AudioEngine::clearMixerOverload(int insert, std::uint64_t &block)
{
    if (auto *gain = dynamic_cast<AutomationGain *>(effectiveMixerGain(insert)))
    {
        block = gain->clearOverload();
        return juce::Result::ok();
    }
    return juce::Result::fail("Mixer meter is not prepared for this insert");
}
std::vector<std::pair<int, std::uint64_t>> AudioEngine::mixerGraphInitialisations() const
{
    std::vector<std::pair<int, std::uint64_t>> result;
    if (!edit)
        return result;
    for (const auto &insert : projectedProject.mixer)
        if (const auto *gain = dynamic_cast<AutomationGain *>(effectiveMixerGain(insert.index)))
            result.emplace_back(insert.index, gain->graphInitialisations());
    return result;
}
static std::vector<ScheduledNote> channelNotes(const std::vector<ScheduledNote> &events, Id channel)
{
    std::vector<ScheduledNote> result;
    for (auto &n : events)
        if (n.channel == channel)
            result.push_back(n);
    return result;
}
static int midiVelocity(float v)
{
    return juce::jlimit(1, 127, juce::roundToInt(v * 127));
}
static bool sameNotes(const std::vector<ScheduledNote> &a, const std::vector<ScheduledNote> &b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].note != b[i].note || a[i].placement != b[i].placement || a[i].beat != b[i].beat ||
            a[i].length != b[i].length || a[i].pitch != b[i].pitch ||
            a[i].repeatOrdinal != b[i].repeatOrdinal || a[i].releaseVelocity != b[i].releaseVelocity ||
            a[i].slide != b[i].slide || a[i].portamento != b[i].portamento ||
            a[i].colourGroup != b[i].colourGroup ||
            midiVelocity(a[i].velocity) != midiVelocity(b[i].velocity))
            return false;
    return true;
}
struct AudioEngine::ProjectionBinding
{
    std::map<Id, ProjectedChannel> channels;
    decltype(projectedAudioClips) clips;
    std::map<int, te::AudioTrack *> inserts;
    std::shared_ptr<LiveTimeline> timeline;
    Project document;
    bool patternMode = true;
    Id pattern = 0;
    double rate = 0;
    int block = 0;
};
std::unique_ptr<AudioEngine::ProjectionBinding>
AudioEngine::prepareProjectionBinding(te::Edit &candidate, const Project &p, bool patternMode, Id selected,
                                      std::map<int, te::AudioTrack *> &&preparedInserts)
{
    auto prepared = std::make_unique<ProjectionBinding>();
    prepared->document = p;
    prepared->patternMode = patternMode;
    prepared->pattern = selected;
    prepared->rate = engine.getDeviceManager().getSampleRate();
    prepared->block = engine.getDeviceManager().getBlockSize();
    for (auto &plugin : candidate.getMasterPluginList())
        if (auto *g = dynamic_cast<GuidePlugin *>(plugin))
            prepared->timeline = g->timelineReference();
    auto tracks = te::getAudioTracks(candidate);
    if (tracks.size() < int(p.channels.size()))
        throw std::invalid_argument("source.bindingTracks: Candidate Channel mapping is incomplete");
    auto events = sequence(p, patternMode, selected);
    for (size_t i = 0; i < p.channels.size(); ++i)
    {
        auto *track = tracks[int(i)];
        if (p.channels[i].audioClip)
        {
            prepared->channels[p.channels[i].id].track = track;
            for (auto *clip : track->getClips())
                if (auto *wave = dynamic_cast<te::WaveAudioClip *>(clip))
                    prepared->clips[juce::int64(wave->state["viberaPlacement"])] = wave;
            continue;
        }
        const auto clips = track->getClips();
        const auto plugins = track->pluginList.getPlugins();
        if (clips.isEmpty() || plugins.isEmpty())
            throw std::invalid_argument("source.bindingSource: Candidate source mapping is incomplete");
        auto *clip = dynamic_cast<te::MidiClip *>(clips[0]);
        auto plugin = plugins[0];
        auto *sampler = dynamic_cast<FixedSampler *>(plugin.get());
        if (!clip || !sampler)
            throw std::invalid_argument("source.bindingSource: Exact prepared sampler/anchor required");
        prepared->timeline = sampler->timelineReference();
        prepared->channels[p.channels[i].id] = {track, clip, plugin, sampler->preparedSource(),
                                                channelNotes(events, p.channels[i].id)};
    }
    prepared->inserts.swap(preparedInserts);
    return prepared;
}
void AudioEngine::bindProjection(ProjectionBinding &prepared) noexcept
{
    static_assert(std::is_nothrow_swappable_v<Project>);
    projectedChannels.swap(prepared.channels);
    projectedAudioClips.swap(prepared.clips);
    projectedInserts.swap(prepared.inserts);
    timeline.swap(prepared.timeline);
    using std::swap;
    swap(projectedProject, prepared.document);
    projectedPatternMode = prepared.patternMode;
    projectedPattern = prepared.pattern;
    projectedRate = prepared.rate;
    projectedBlock = prepared.block;
    hasProjection = true;
}
// Clip fades are rendered by Tracktion's fade node, live and offline alike.
void applyClipFades(te::WaveAudioClip &wave, const AudioPlacement &placement)
{
    const auto in = te::TimeDuration::fromSeconds(placement.fadeIn),
               out = te::TimeDuration::fromSeconds(placement.fadeOut);
    if (wave.getFadeInType() != te::AudioFadeCurve::convex)
        wave.setFadeInType(te::AudioFadeCurve::convex);
    if (wave.getFadeOutType() != te::AudioFadeCurve::convex)
        wave.setFadeOutType(te::AudioFadeCurve::convex);
    if (wave.getFadeIn() != in)
        wave.setFadeIn(in);
    if (wave.getFadeOut() != out)
        wave.setFadeOut(out);
}
juce::Result AudioEngine::updateAudioPlacements(const Project &project, bool patternMode)
{
    std::set<Id> retained;
    if (!patternMode)
        for (const auto &placement : project.audioPlacements)
        {
            const auto &channel = *project.channel(placement.channel);
            auto &wave = projectedAudioClips[placement.id];
            const auto *oldChannel = projectedProject.channel(channel.id);
            if (wave && oldChannel && oldChannel->source != channel.source)
            {
                wave->removeFromParent();
                wave = nullptr;
            }
            const double start = projectMap(project, patternMode).secondsAt(placement.beat);
            te::ClipPosition position{{seconds(start), seconds(start + placement.duration)},
                                      te::TimeDuration::fromSeconds(placement.offset)};
            if (!wave)
            {
                wave = projectedChannels.at(channel.id)
                           .track->insertWaveClip(channel.name, juce::File(channel.source), position, false);
                if (!wave)
                    return juce::Result::fail("Cannot create audio clip");
                wave->setAutoTempo(false);
                wave->setAutoPitch(false);
                wave->setUsesProxy(false);
                wave->setLoopRange({});
                wave->setMuted(placement.muted || project.laneMuted(placement.lane));
                wave->state.setProperty("viberaPlacement", juce::int64(placement.id), nullptr);
            }
            else
            {
                if (wave->getPosition() != position)
                    wave->setPosition(position);
                if (wave->getName() != channel.name)
                    wave->setName(channel.name);
            }
            if (wave->isMuted() != (placement.muted || project.laneMuted(placement.lane)))
                wave->setMuted(placement.muted || project.laneMuted(placement.lane));
            applyClipFades(*wave, placement);
            retained.insert(placement.id);
        }
    for (auto it = projectedAudioClips.begin(); it != projectedAudioClips.end();)
        if (!retained.count(it->first))
        {
            if (it->second)
                it->second->removeFromParent();
            it = projectedAudioClips.erase(it);
        }
        else
            ++it;
    return juce::Result::ok();
}
void AudioEngine::updateMixerRoutes(const Project &p)
{
    // Caller validated the unchanged ID universe and native source bindings
    // before publishing the routine live document.
    MixerNativeIdentity mixerIdentity;
    const auto prepared = MixerNativeIdentity::prepare(p, mixerIdentity);
    if (prepared != MixerNativeIdentity::Error::none)
        throw std::logic_error("mixer.nativeIdentity.invalidUniverse: Invalid route source");
    for (const auto &m : p.mixer)
        if (m.index != 0)
        {
            auto &list = projectedInserts.at(m.index)->pluginList;
            std::set<int> retained;
            std::vector<te::Plugin::Ptr> existingPlugins;
            for (auto *plugin : list)
                existingPlugins.emplace_back(plugin);
            for (const auto &plugin : existingPlugins)
            {
                auto *routePlugin = dynamic_cast<MixerSendPlugin *>(plugin.get());
                if (!routePlugin)
                    continue;
                if (!mixerRoute(p, m.index, routePlugin->destination()))
                    plugin->deleteFromParent();
                else
                    retained.insert(routePlugin->destination());
            }
            for (const auto &route : m.routes)
                if (route.target != 0 && !retained.contains(route.target))
                {
                    auto plugin = edit->getPluginCache().createNewPlugin(MixerSendPlugin::xmlTypeName, {});
                    dynamic_cast<MixerSendPlugin *>(plugin.get())
                        ->configure(timeline, m.index, route.target,
                                    *mixerIdentity.bus(route.target), route.gain);
                    int position = 0;
                    for (auto *existing : list)
                    {
                        if (dynamic_cast<MixerMasterGate *>(existing))
                            break;
                        ++position;
                    }
                    list.insertPlugin(plugin, position, nullptr);
                }
        }
}
bool AudioEngine::hasAudioDevice() const
{
    return engine.getDeviceManager().deviceManager.getCurrentAudioDevice() != nullptr;
}
bool AudioEngine::canDeferChannelRouting() const noexcept
{
    const auto* manager = juce::MessageManager::getInstanceWithoutCreating();
    const auto& devices = engine.getDeviceManager();
    using Fence = te::DeviceManager::PreparationFenceStatus;
    const bool ownedPreparation =
        fullProjectionController && fullProjectionChange && preparation[0] &&
        !preparation[0]->fullProjectionPublished &&
        preparation[0]->fullProjectionOwner == fullProjectionController &&
        preparation[0]->fullProjectionTicket == fullProjectionChange &&
        preparation[0]->fullProjectionIdentity == fullProjectionController->projectIdentity &&
        (preparation[0]->fullProjectionRevision == fullProjectionController->revision ||
         (fullProjectionChange->wasCommitted() && !fullProjectionChange->isNoOp() &&
          preparation[0]->fullProjectionRevision != std::numeric_limits<std::uint64_t>::max() &&
          preparation[0]->fullProjectionRevision + 1 == fullProjectionController->revision));
    const bool ownedNativeAccess =
        preparingDeferredNativeAccess && preparation[0] && preparation[0]->deferredNativeAccess &&
        !preparation[0]->fullProjectionDestination && !preparation[0]->fullProjectionPublished &&
        preparation[0]->fullProjectionOwner == routingController &&
        (!routingController || preparation[0]->fullProjectionIdentity == routingController->projectIdentity);
    return manager && manager->isThisTheMessageThread() &&
           !deviceEverActive.load(std::memory_order_acquire) && !preview.deviceEverStarted() &&
           !devices.deviceManager.getCurrentAudioDevice() &&
           devices.getPreparationFenceStatus() == Fence::none && !playing() &&
           (!edit || !edit->getTransport().getCurrentPlaybackContext()) && channelRouting &&
           !channelRouting->effective && !channelRouting->pendingGraph && channelRouting->prepared.empty() &&
           !channelRouting->deviceInvalidated && !capture && !recoveryPlugin && !worker && !renderEdit &&
           !midiInput.capture.active.load() && !typingInput.capture.active.load() &&
           (!preparation[0] || ownedPreparation || ownedNativeAccess) && !preparation[1] &&
           retiredPreparation.empty();
}
juce::Result AudioEngine::prepareDeferredRouting(const Project& target,
    std::unique_ptr<PreparedDeferredRouting>& output)
{
    if (!canDeferChannelRouting()) return juce::Result::fail("routing.audioRefused: No initial offline admission");
    auto prepared = std::make_unique<PreparedDeferredRouting>();
    for (const auto& channel : target.channels)
        if (!channel.audioClip)
        {
            std::shared_ptr<const Sample> sample;
            if (auto ready = samples.prepare(SampleCache::preparationKey(target, channel), sample);
                ready.failed()) return ready;
            prepared->samples.emplace(channel.id, std::move(sample));
            prepared->sampleSources.emplace(channel.id, channel.source);
        }
    output = std::move(prepared);
    return juce::Result::ok();
}
void AudioEngine::retireNoDeviceProjection(PreparedDeferredRouting& prepared) noexcept
{
    // Caller performed final admission immediately before the actual canonical
    // swap, with no message pumping. All destruction occurs on CONTROL after
    // that swap and BEFORE any observer can allocate an obsolete context.
    jassert(canDeferChannelRouting());
    deferredPositionBeat = positionBeat();
    deferredLooping = looping();
    deferredDocumentProjection = true;
    deferredHostProjection = hasProjection || deferredHostProjection;
    deferredSampleProjection = true;
    deferredSamples.swap(prepared.samples);
    deferredSampleSources.swap(prepared.sampleSources);
    // MidiInputRouter owns its lane bank independently of native Plugins.
    // Keep those lanes/history targets alive: select(nullptr) would invoke
    // fallible historyInterrupted observers after the canonical commit.
    // bindProjection/setMidiTarget normally retarget on later preparation.
    compensationGraph.reset();
    releaseChannelRoutingForEdit();
    projectedChannels.clear();
    projectedAudioClips.clear();
    projectedInserts.clear();
    effectPlugins.clear();
    edit.reset();
    productionPluginProjection.reset();
    hasProjection = false;
    // currentHosts/ownedHosts, sample bank, guide settings and document-owned
    // plugin state remain recoverable. Normal rebuild handles their reuse.
}
void AudioEngine::retireDeferredProjectReplacement(PreparedDeferredRouting &prepared,
                                                   Project &preparedMetadata) noexcept
{
    jassert(canDeferChannelRouting());
    retireNoDeviceProjection(prepared);
    // Old workers remain in ownedHosts for the existing asynchronous graceful
    // retirement service. IDs may recur in the NEW project session: none of
    // their opaque state, editor eligibility or native witnesses may follow.
    currentHosts.clear();
    observedParameterRevisions.clear();
    lastCapturedRevisions.clear();
    observedPluginLatencies.clear();
    refusedCompensationLatencies.clear();
    pluginErrors.clear();
    recoveryPlugin.reset();
    recoveryStates.clear();
    deferredHostProjection = false;
    deferredPositionBeat = 0;
    projectedRate = 0;
    projectedBlock = 0;
    using std::swap;
    swap(projectedProject, preparedMetadata); // Prepared no-throw value swap, no postcommit copy.
}
juce::Result AudioEngine::rebuild(const Project &p, bool patternMode, Id selected)
{
    if (fullProjectionController)
    {
        if (!fullProjectionChange || !fullProjectionChange->destinationBeforeCommit() ||
            !fullProjectionChange->destinationBeforeCommit()->state().isEquivalentTo(p.state()))
            return juce::Result::fail(
                "routing.fullProjectionDestinationChanged: Owned rebuild destination changed");
        if (auto valid = fullProjectionController->validatePreparedChange(*fullProjectionChange);
            valid.failed())
            return valid;
    }
    if (finiteEffectBaseline)
        if (auto valid = validateFiniteEffectBaseline(p); valid.failed())
            return valid;
    if (initialSourceTransport)
        if (auto admitted = validateInitialSourceProject(p); admitted.failed()) return admitted;
    // All mutation and preparation belongs to the message thread, never an audio callback.
    auto valid = p.validate();
    if (valid.failed())
    {
        recoveryPlugin.reset();
        return valid;
    }
    if (patternMode && !p.pattern(selected))
    {
        recoveryPlugin.reset();
        return juce::Result::fail("Selected Pattern is unavailable");
    }
    if (auto *repeatDevice = engine.getDeviceManager().deviceManager.getCurrentAudioDevice())
        if (auto admitted = validateRepeatMidiBudget(p, patternMode, selected,
                                                     repeatDevice->getCurrentBufferSizeSamples(),
                                                     repeatDevice->getCurrentSampleRate());
            admitted.failed())
            return admitted;
    // A genuine new-device configuration must not erase unsaved native state
    // merely because the offline routing Edit was retired. Keep workers alive
    // until existing shared capture/save owns an exact current snapshot. Close
    // native editors before reprepare; arbitrary editor mutation is independent.
    if (engine.getDeviceManager().deviceManager.getCurrentAudioDevice())
        for (const auto& [id, host] : currentHosts)
        {
            if (recoveryPlugin && recoveryPlugin->id == id)
                continue;
            const auto* requested = p.plugin(id);
            const auto* native = projectedProject.plugin(id);
            if (!requested || !native || requested->descriptor != native->descriptor) continue;
            // Proven exact reuse never reloads opaque state: PluginPreparation
            // returns this host and buildEdit consumes that result, without start.
            if (pluginHostConfigurationCurrent(*requested, host, engine.getDeviceManager().getSampleRate(),
                                               engine.getDeviceManager().getBlockSize()))
                continue;
            const auto captured = lastCapturedRevisions.find(id);
            if (capture || host->editorBusy() || host->editorWindowState().exists ||
                host->pendingParameterRequests() || captured == lastCapturedRevisions.end() ||
                captured->second.host != host || captured->second.value != host->parameterRevision() ||
                requested->state != host->recoveryState())
                return juce::Result::fail("routing.stateCaptureRequired: Capture current native state before "
                                          "replacing its device transport");
        }
    // Keep offline document editing/export available without constructing an
    // Edit that a later native device timer could play with obsolete routing.
    if (deferredDocumentProjection && !channelRouting->explicitFullReplacement &&
        !engine.getDeviceManager().deviceManager.getCurrentAudioDevice())
    {
        const juce::ScopedValueSetter<bool> access(preparingDeferredNativeAccess, true);
        PreparedDeferredRouting next;
        if (auto ready = prepareDeferredNativeHosts(p, next); ready.failed())
            return ready;
        if (!canDeferChannelRouting())
            return juce::Result::fail(
                "routing.deferredNativeChanged: Device/context changed before native access publication");
        publishDeferredNativeHosts(next);
        projectedPatternMode = patternMode;
        projectedPattern = selected;
        return juce::Result::ok(); // Real native controls/state access; native Edit remains deferred.
    }
    bool sameTopology = hasProjection && edit && p.channels.size() == projectedProject.channels.size() &&
                        p.mixer.size() == projectedProject.mixer.size();
    if (sameTopology)
    {
        for (size_t i = 0; i < p.channels.size(); ++i)
        {
            auto &a = p.channels[i];
            const auto *previous = projectedProject.channel(a.id);
            if (!previous)
            {
                sameTopology = false;
                break;
            }
            const auto &b = *previous;
            sameTopology =
                sameTopology && a.audioClip == b.audioClip && bool(a.instrument) == bool(b.instrument);
            if (a.instrument && b.instrument)
                sameTopology = sameTopology && a.instrument->id == b.instrument->id &&
                               a.instrument->descriptor == b.instrument->descriptor &&
                               a.instrument->extraOutputMixers == b.instrument->extraOutputMixers; // Output tracks.
        }
        for (size_t i = 0; i < p.mixer.size(); ++i)
        {
            auto &a = p.mixer[i];
            const auto *previous = mixerInsert(projectedProject, a.index);
            if (!previous)
            {
                sameTopology = false;
                break;
            }
            const auto &b = *previous;
            sameTopology = sameTopology && a.effects.size() == b.effects.size();
            if (a.effects.size() == b.effects.size())
                for (size_t j = 0; j < a.effects.size(); ++j)
                    sameTopology = sameTopology && a.effects[j].id == b.effects[j].id &&
                                   a.effects[j].descriptor == b.effects[j].descriptor &&
                                   a.effects[j].detector == b.effects[j].detector;
        }
    }
    if (recoveryPlugin)
    {
        const auto *target = p.plugin(recoveryPlugin->id);
        if (!target || target->descriptor != recoveryPlugin->descriptor)
            recoveryPlugin.reset(); // A removed/replaced target must never be resurrected.
    }
    sameTopology = sameTopology && !recoveryPlugin && !pluginDeviceConfigurationChanged();
    if (channelRouting->effective && !channelRouting->explicitFullReplacement)
    {
        const auto routing = channelRoutingReadback();
        if (routing.status == ChannelRoutingStatus::deviceChanged ||
            routing.status == ChannelRoutingStatus::staleContext ||
            routing.status == ChannelRoutingStatus::staleProjection)
            return juce::Result::fail(
                "routing.staleContext: Restore the full projection explicitly before editing");
        bool nativeChange = !sameTopology || p.tempo != projectedProject.tempo ||
                            p.numerator != projectedProject.numerator ||
                            p.denominator != projectedProject.denominator;
        if (p.channels.size() == projectedProject.channels.size())
            for (const auto &channel : p.channels)
            {
                const auto *old = projectedProject.channel(channel.id);
                nativeChange = nativeChange || !old ||
                               (old && (old->source != channel.source || old->mixer != channel.mixer));
                if (channel.audioClip && patternMode != projectedPatternMode)
                    nativeChange = true;
            }
        if (p.mixer.size() == projectedProject.mixer.size())
            for (std::size_t i = 0; i < p.mixer.size(); ++i)
            {
                const auto &a = p.mixer[i].routes;
                const auto *previous = mixerInsert(projectedProject, p.mixer[i].index);
                if (!previous)
                {
                    nativeChange = true;
                    break;
                }
                const auto &b = previous->routes;
                nativeChange = nativeChange || a.size() != b.size();
                if (a.size() == b.size())
                    for (std::size_t r = 0; r < a.size(); ++r)
                        nativeChange = nativeChange || a[r].target != b[r].target;
            }
        if (!p.state()
                 .getChildWithName("AUDIO_PLACEMENTS")
                 .isEquivalentTo(projectedProject.state().getChildWithName("AUDIO_PLACEMENTS")))
            nativeChange = true;
        if (nativeChange)
            return juce::Result::fail("routing.requiresPreparedProjection: Native source/clip/tempo/routing "
                                      "edits require their own prepared plan or explicit full replacement");
    }
    if (channelRouting->explicitFullReplacement)
        sameTopology = false;
    if (sameTopology && preparation[0] && !fullProjectionController &&
        !preparation[0]->fullProjectionDestination)
    {
        // Cancellation can restore the exact incumbent while its previous
        // structural preview is still preparing. No replacement needs this job.
        // Transfer ownership before returning the ordinary incremental path;
        // existing CONTROL reclamation waits for actual thread exit.
        retiredPreparation.reserve(retiredPreparation.size() + 1);
        preparation[0]->signalThreadShouldExit();
        retiredPreparation.push_back(std::move(preparation[0]));
    }
    if (!sameTopology)
    {
        auto ready = preparePlugins(p, false);
        if (ready.failed())
            return ready;
        // Prepare/validate the replacement first. A missing source must not stop the working Edit.
        bool resume = playing();
        double beat = positionBeat();
        const bool restoreDeferred = deferredDocumentProjection, restoreLoop = deferredLooping;
        juce::String error;
        auto oldHosts = currentHosts;
        auto oldEffects = effectPlugins;
        auto oldErrors = pluginErrors;
        auto oldParameterRevisions = observedParameterRevisions;
        currentHosts.clear();
        effectPlugins.clear();
        pluginErrors.clear();
        std::shared_ptr<ProductionPluginProjection> nextSource;
        std::unique_ptr<te::Edit> next;
        try
        {
            next = buildEdit(p, patternMode, selected, te::Edit::forEditing, error, &nextSource);
        }
        catch (...)
        {
            effectPlugins.clear();
            currentHosts.swap(oldHosts);
            effectPlugins.swap(oldEffects);
            pluginErrors.swap(oldErrors);
            observedParameterRevisions.swap(oldParameterRevisions);
            recoveryPlugin.reset();
            // Restore incumbent ownership before constructing any diagnostic.
            // Keep the stable error code while retaining the actual CONTROL
            // preparation exception, rather than hiding every native refusal.
            try
            {
                throw;
            }
            catch (const std::exception &failure)
            {
                return juce::Result::fail(
                    "source.preparationFailed: Candidate preparation failed; previous playback retained: " +
                    juce::String(failure.what()));
            }
            catch (...)
            {
            }
            return juce::Result::fail(
                "source.preparationFailed: Candidate preparation failed; previous playback retained");
        }
        if (!next)
        {
            currentHosts = std::move(oldHosts);
            effectPlugins = std::move(oldEffects);
            pluginErrors = std::move(oldErrors);
            observedParameterRevisions = std::move(oldParameterRevisions);
            recoveryPlugin.reset();
            return juce::Result::fail(error);
        }
        // Candidate PluginPtrs are mirrored outside `next`. Any exception
        // before publication must clear those mirrors BEFORE next's Edit dies.
        // Declared after next, so this guard unwinds first. Default/native
        // ordinary replacement semantics are untouched outside this owner.
        const auto retireDeferredCandidateReferences = [&]() noexcept
        {
            if (!next)
                return;
            effectPlugins.clear();
            currentHosts.swap(oldHosts);
            pluginErrors.swap(oldErrors);
            observedParameterRevisions.swap(oldParameterRevisions);
            effectPlugins.swap(oldEffects);
        };
        struct DeferredCandidateReferences
        {
            decltype(retireDeferredCandidateReferences)& retire;
            ~DeferredCandidateReferences() noexcept { retire(); }
        } deferredCandidateReferences{retireDeferredCandidateReferences};
        std::map<int, te::AudioTrack *> nextInserts;
        std::unique_ptr<ProjectionBinding> nextBinding;
        decltype(recoveryStates) nextRecoveryStates;
        juce::Result nativeBindings = juce::Result::ok();
        const auto discardIdentityCandidate = [&]() noexcept
        {
            effectPlugins.clear();
            currentHosts = std::move(oldHosts); pluginErrors = std::move(oldErrors);
            observedParameterRevisions = std::move(oldParameterRevisions);
            effectPlugins = std::move(oldEffects);
            nextBinding.reset(); // Candidate PluginPtrs retire before their Edit.
            next.reset(); // Native references released before their candidate Edit.
        };
        try
        {
            nextRecoveryStates = recoveryStates;
            nativeBindings = prepareMixerNativeBindings(*next, p, nextInserts);
            if (nativeBindings.wasOk())
                nextBinding =
                    prepareProjectionBinding(*next, p, patternMode, selected, std::move(nextInserts));
            if (recoveryPlugin)
            {
                if (currentHosts.contains(recoveryPlugin->id))
                    nextRecoveryStates.erase(recoveryPlugin->id);
                else
                    nextRecoveryStates[recoveryPlugin->id] = *recoveryPlugin;
            }
            std::erase_if(nextRecoveryStates,
                          [&](const auto &entry)
                          {
                              const auto *spec = p.plugin(entry.first);
                              return !spec || spec->descriptor != entry.second.descriptor;
                          });
            if (preparation[0])
                retiredPreparation.reserve(retiredPreparation.size() + 1);
        }
        catch (...)
        {
            // Restore before constructing the diagnostic, which itself may
            // allocate. A failure cannot strand native candidate mirrors.
            discardIdentityCandidate();
            return juce::Result::fail(
                "source.bindingPreparationFailed: Private mapping failed; previous playback retained");
        }
        if (nativeBindings.failed())
        {
            discardIdentityCandidate();
            return nativeBindings;
        }
        if (deferredDeviceFinalAdmission)
        {
            const auto admitted = (*deferredDeviceFinalAdmission)(true);
            if (admitted.failed())
            {
                discardIdentityCandidate();
                return admitted;
            }
        }
        if (nextSource && engine.getDeviceManager().validateStagedSourceBurstSession(*nextSource->context,
                                                                                     nextSource->session) !=
                              te::SourceBurstSession::Status::staged)
        {
            discardIdentityCandidate();
            return juce::Result::fail(
                "source.admissionChanged: Candidate remains dormant; previous playback retained");
        }
        if (fullProjectionController)
            if (auto valid = fullProjectionController->validatePreparedChange(*fullProjectionChange);
                valid.failed())
            {
                discardIdentityCandidate();
                return valid;
            }
        stop();
        if (fullProjectionAdmission && *fullProjectionAdmission)
            if (auto admitted = (*fullProjectionAdmission)(); admitted.failed())
            {
                discardIdentityCandidate();
                return admitted;
            }
        if (fullProjectionController)
            if (auto valid = fullProjectionController->validatePreparedChange(*fullProjectionChange);
                valid.failed())
            {
                discardIdentityCandidate();
                return valid; // Owned wrapper resumes the still-current incumbent.
            }
        compensationGraph.reset();
        releaseChannelRoutingForEdit();
        projectedChannels.clear();
        projectedAudioClips.clear();
        projectedInserts.clear();
        oldEffects.clear(); // plugin references must not outlive their owning Edit
        refusedCompensationLatencies.clear();
        observedPluginLatencies.clear(); // New graph already declares actual new Host latencies.
        edit = std::move(next);
        if (fullProjectionController && preparation[0])
            preparation[0]->fullProjectionPublished = true;
        productionPluginProjection =
            std::move(nextSource); // Old context is excluded before warm Host handoff.
        tempo = p.tempo;
        tempoMap = projectMap(p, patternMode);
        bindProjection(*nextBinding); // Only prepared no-throw swaps after incumbent retirement.
        if (fullProjectionController &&
            !fullProjectionController->commitPreparedChange(*fullProjectionChange))
        {
            productionDeviceInvalidated = true;
            return juce::Result::fail("routing.fullProjectionCommitInvariant: Destination graph published "
                                      "dormant; canonical commit refused");
        }
        try
        {
            deferredDocumentProjection = deferredHostProjection = deferredSampleProjection = false;
            recoveryStates.swap(nextRecoveryStates);
            recoveryPlugin.reset();
            if (preparation[0])
            {
                if (preparation[0]->isThreadRunning())
                {
                    preparation[0]->signalThreadShouldExit();
                    retiredPreparation.push_back(std::move(preparation[0]));
                }
                else
                    preparation[0].reset();
            }
            ++replacements;
            // Retire preparation/account the committed replacement BEFORE any
            // fallible transport or input observer; recovery must not stay busy.
            if (restoreDeferred)
                edit->getTransport().looping = restoreLoop;
            setMidiTarget(midiTarget);
            seekBeat(beat);
            if (resume)
                edit->getTransport().play(false);
            publishLoopBounds();
            if (productionPluginProjection)
            {
                auto &source = *productionPluginProjection;
                // Mandatory immediate seek is consumed only after the cold context
                // binds the ACTUAL next full callback's global reference clock.
                source.context->postPosition(seconds(tempoMap.secondsAt(std::max(0., beat))));
                if (engine.getDeviceManager().armStagedSourceBurstSession(*source.context, source.session) !=
                    te::SourceBurstSession::Status::pendingFirstSerial)
                {
                    productionDeviceInvalidated = true;
                    edit->getTransport().stop(false, false);
                    return juce::Result::fail("source.activationRefused: Replacement committed but audio "
                                              "activation failed; context "
                                              "dormant, transport stopped, restore required");
                }
            }
            productionDeviceInvalidated = false;
            return juce::Result::ok();
        }
        catch (...)
        {
            // The old Edit is gone. Never report rollback or leave the dormant
            // candidate as successful playback after a CONTROL listener throws.
            productionDeviceInvalidated = true;
            try
            {
                edit->getTransport().stop(false, false);
            }
            catch (...)
            {
            }
            return juce::Result::fail("source.activationFailed: Replacement committed but activation threw; "
                                      "transport stopped, restore required");
        }
    }

    // Preflight ALL sources and replacement plugins before touching the retained live projection.
    std::map<Id, std::shared_ptr<const Sample>> prepared;
    std::map<Id, te::Plugin::Ptr> replacementPlugins;
    for (auto &channel : p.channels)
    {
        if (channel.audioClip)
        {
            te::AudioFile source(engine, juce::File(channel.source));
            if (!source.isValid())
                return juce::Result::fail("Unreadable audio source: " + channel.name);
            continue;
        }
        auto result = samples.prepare(SampleCache::preparationKey(p, channel), prepared[channel.id]);
        if (result.failed())
            return result;
        if (std::find(ownedSamples.begin(), ownedSamples.end(), prepared[channel.id]) == ownedSamples.end())
            ownedSamples.push_back(prepared[channel.id]);
        auto &projected = projectedChannels.at(channel.id);
        if (projected.sample != prepared[channel.id])
        {
            auto plugin = edit->getPluginCache().createNewPlugin(FixedSampler::xmlTypeName, {});
            auto *sampler = dynamic_cast<FixedSampler *>(plugin.get());
            if (!sampler)
                return juce::Result::fail("Sampler creation failed");
            sampler->setPreparedSource(prepared[channel.id]);
            sampler->setTimeline(timeline, channel.id);
            sampler->setCutClock(samplerCutClock);
            sampler->setInput(midiInput.createLane());
            sampler->setTypingInput(typingInput.createLane());
            sampler->preparePreviewInput();
            sampler->setGuide(guide);
            if (channel.instrument)
                sampler->setRemote(currentHosts[channel.instrument->id], channel.instrument->bypass,
                                   channel.instrument->id);
            replacementPlugins[channel.id] = plugin;
            ++samplerCreations;
        }
    }
    auto events = sequence(p, patternMode, selected);
    const bool tempoChanged = p.tempo != projectedProject.tempo;
    const double beat = positionBeat();
    const auto duration = durationBeats(p, patternMode, selected);
    const bool rangeChanged =
        tempoChanged || duration != durationBeats(projectedProject, projectedPatternMode, projectedPattern);
    if (timeline)
    {
        auto document = compileLiveDocument(p, patternMode, selected, ++timelineGeneration);
        if (!document)
            return juce::Result::fail("Cannot prepare automation; current playback retained");
        std::vector<SamplerCutInput> cutInputs;
        for (const auto &[id, projected] : projectedChannels)
        {
            const auto replacement = replacementPlugins.find(id);
            auto *sampler = dynamic_cast<FixedSampler *>(replacement == replacementPlugins.end()
                                                             ? projected.sampler.get()
                                                             : replacement->second.get());
            if (sampler)
                sampler->appendCutInputs(cutInputs);
        }
        bindLiveCutInputs(*document, cutInputs);
        const auto automationReady =
            prepareNativeAutomationRoutes(p, *document, currentHosts, projectedRate, true);
        if (automationReady.failed())
            return automationReady;
        if (!timeline->publish(document))
            return juce::Result::fail("Audio edit queue busy; retrying");
    }
    {
        te::TransportControl::ReallocationInhibitor batch(edit->getTransport());
        if (tempoChanged)
            edit->tempoSequence.getTempo(0)->setBpm(p.tempo);
        const auto clockMeter = playbackMeter(p, patternMode, selected);
        if (clockMeter != playbackMeter(projectedProject, projectedPatternMode, projectedPattern))
            edit->tempoSequence.getTimeSig(0)->setStringTimeSig(juce::String(clockMeter.first) + "/" +
                                                                juce::String(clockMeter.second));
        for (auto &channel : p.channels)
        {
            auto &projected = projectedChannels.at(channel.id);
            auto *old = projectedProject.channel(channel.id);
            if (channel.instrument)
                dynamic_cast<FixedSampler *>(projected.sampler.get())
                    ->setBypassed(channel.instrument->bypass);
            if (auto replacement = replacementPlugins.find(channel.id);
                replacement != replacementPlugins.end())
            {
                projected.sampler->deleteFromParent();
                projected.track->pluginList.insertPlugin(replacement->second, 0, nullptr);
                projected.sampler = replacement->second;
                projected.sample = prepared[channel.id];
            }
            if (old->name != channel.name)
            {
                projected.track->setName(channel.name);
                if (projected.clip)
                    projected.clip->setName(channel.name);
            }
            // Gain/pan use immutable live targets, without native parameter or
            // graph writes. Audio-clip mute is part of that prepared target too.
            if (old->mixer != channel.mixer)
            {
                if (channel.mixer == 0)
                    projected.track->getOutput().setOutputToDefaultDevice(false);
                else
                    projected.track->getOutput().setOutputToTrack(projectedInserts.at(channel.mixer));
            }
            auto notes = channelNotes(events, channel.id);
            if (!sameNotes(projected.notes, notes))
            {
                projected.notes = std::move(notes);
                ++midiUpdates;
            }
        }
        for (size_t i = 0; i < p.mixer.size(); ++i)
        {
            auto &m = p.mixer[i];
            const auto &old = *mixerInsert(projectedProject, m.index);
            for (auto &spec : m.effects)
                if (auto it = effectPlugins.find(spec.id); it != effectPlugins.end())
                    dynamic_cast<RemoteEffect *>(it->second.get())
                        ->setBypassed(spec.bypass || m.effectsBypassed);
            // Gain/mute/pan are published together before each strip's meter.
            if (m.index != 0 && m.name != old.name)
                projectedInserts.at(m.index)->setName(m.name);
        }
        if (!channelRouting->effective)
        {
            updateMixerRoutes(p);
            const auto audioUpdated = updateAudioPlacements(p, patternMode);
            if (audioUpdated.failed())
                return audioUpdated;
        }
        if (rangeChanged)
            edit->getTransport().setLoopRange(loopRange(p, patternMode, duration));
        if (projectedPatternMode != patternMode)
            edit->getTransport().looping = patternMode;
    }
    tempo = p.tempo;
    tempoMap = projectMap(p, patternMode);
    if (tempoChanged)
        seekBeat(beat);
    projectedProject = p;
    projectedPatternMode = patternMode;
    projectedPattern = selected;
    ++updates;
    setMidiTarget(midiTarget);
    publishLoopBounds();
    return juce::Result::ok();
}
void AudioEngine::publishLoopBounds()
{
    // The pinned transport copies its ValueTree range to the playhead only on
    // every tenth timer tick. Publish the same prepared range synchronously on
    // the control thread. Its existing setter preserves the current timeline
    // position when extending or still in range; an out-of-range cursor resumes
    // at the shortened loop start. This is an explicit VibeRa policy.
    if (!edit)
        return;
    auto &transport = edit->getTransport();
    if (auto *context = transport.getCurrentPlaybackContext())
        if (auto *head = context->getNodePlayHead())
        {
            const auto rate = context->getSampleRate();
            if (!std::isfinite(rate) || rate <= 0)
                return;
            const bool enabled = transport.looping;
            auto range = transport.getLoopRange();
            range = range.withEnd(
                std::max(range.getEnd(), range.getStart() + te::TimeDuration::fromSeconds(.001)));
            head->setLoopRange(enabled, enabled ? te::toSamples(range, rate) : juce::Range<std::int64_t>{});
        }
}
void AudioEngine::setMidiTarget(Id channel)
{
    midiTarget = channel;
    MidiLane *lane = nullptr;
    if (auto it = projectedChannels.find(channel); it != projectedChannels.end() && it->second.sampler)
        lane = dynamic_cast<FixedSampler *>(it->second.sampler.get())->inputLane();
    midiInput.select(lane);
    MidiLane *typingLane = nullptr;
    if (auto it = projectedChannels.find(channel); it != projectedChannels.end() && it->second.sampler)
        typingLane = dynamic_cast<FixedSampler *>(it->second.sampler.get())->typingLane();
    typingInput.select(typingLane);
    midiInput.setHistoryTarget(channel);
    typingInput.setHistoryTarget(channel);
    std::array<MidiLane *, 17> receiving{};
    for (const auto &c : projectedProject.channels)
        if (c.receiveMidiChannel >= 1 && c.receiveMidiChannel <= 16)
            if (auto it = projectedChannels.find(c.id); it != projectedChannels.end() && it->second.sampler)
                receiving[size_t(c.receiveMidiChannel)] =
                    dynamic_cast<FixedSampler *>(it->second.sampler.get())->inputLane();
    midiInput.setReceivers(receiving);
    if (!deferredDocumentProjection && lane && edit && engine.getDeviceManager().deviceManager.getCurrentAudioDevice())
        edit->getTransport().ensureContextAllocated();
}
bool AudioEngine::previewChannelNote(Id channel, int pitch, bool on, int velocity)
{
    // Single message-thread producer. Does not retarget physical MIDI/typing,
    // record notes, mutate the project, reload PCM or change transport.
    auto *source = projectedProject.channel(channel);
    auto found = projectedChannels.find(channel);
    if (deferredDocumentProjection || !hasProjection || !source || source->audioClip || pitch < 0 ||
        pitch > 127 || found == projectedChannels.end() || !found->second.sampler)
        return false;
    auto *sampler = dynamic_cast<FixedSampler *>(found->second.sampler.get());
    auto *lane = sampler ? sampler->previewLane() : nullptr;
    if (!lane || !edit || !engine.getDeviceManager().deviceManager.getCurrentAudioDevice())
        return false;
    edit->getTransport().ensureContextAllocated();
    const unsigned char bytes[]{static_cast<unsigned char>(on ? 0x90 : 0x80),
                                static_cast<unsigned char>(pitch),
                                static_cast<unsigned char>(on ? juce::jlimit(1, 127, velocity) : 0)};
    lane->push(bytes, 3);
    return true;
}
void AudioEngine::stopChannelPreview(Id channel)
{
    if (auto found = projectedChannels.find(channel);
        found != projectedChannels.end() && found->second.sampler)
        if (auto *sampler = dynamic_cast<FixedSampler *>(found->second.sampler.get()))
            if (auto *lane = sampler->previewLane())
                ++lane->epoch; // Audio consumer releases only this lane; no callback destruction.
}
std::shared_ptr<const Sample> AudioEngine::channelSample(Id id) const
{
    if (deferredDocumentProjection && deferredSampleProjection)
    {
        const auto sample = deferredSamples.find(id);
        return sample == deferredSamples.end() ? nullptr : sample->second;
    }
    auto it = projectedChannels.find(id);
    return it == projectedChannels.end() ? nullptr : it->second.sample;
}
bool AudioEngine::channelSampleMatches(Id id, const juce::String &source) const
{
    if (deferredDocumentProjection && deferredSampleProjection)
    {
        const auto found = deferredSampleSources.find(id);
        return found != deferredSampleSources.end() && found->second == source && bool(channelSample(id));
    }
    const auto *channel = projectedProject.channel(id);
    return channel && channel->source == source && bool(channelSample(id));
}
AudioEngine::ProjectionDiagnostics AudioEngine::projectionDiagnostics() const
{
    // Control-thread diagnostic readback; never called by DSP.

    ProjectionDiagnostics d;
    if (productionPluginProjection)
    {
        const auto &source = *productionPluginProjection;
        const auto &budget = source.plan->budget();
        d.pluginTransportPackets = budget.packets;
        d.maximumPhysicalFrames = budget.physicalFrames;
        d.maximumSourceFrames = budget.sourceFrames;
        d.pluginDeviceEpoch = budget.deviceEpoch;
        te::SourceBurstSession::Outcome native;
        source_membership::Receipt members;
        d.pluginSourceState = !source.session->read(native) || !source.members->read(members) ? "unavailable"
                              : productionDeviceInvalidated || native.failed || native.retired ||
                                      members.status != source_membership::Status::ready
                                  ? "fault"
                              : source.context->sourceSessionGatePhase() ==
                                      te::EditPlaybackContext::SourceSessionGatePhase::active
                                  ? "active"
                                  : "pending";
    }
    else if (!initialSourceTransport && !finiteEffectBaseline && !currentHosts.empty())
        d.pluginSourceState = "controlOnly";
    if (edit)
    {
        d.transportPlaying = edit->getTransport().isPlaying();
        d.loopEnabled = edit->getTransport().looping;
        const auto requestedLoop = edit->getTransport().getLoopRange();
        d.loopStartBeat = tempoMap.beatAt(requestedLoop.getStart().inSeconds());
        d.loopEndBeat = tempoMap.beatAt(requestedLoop.getEnd().inSeconds());
        if (auto *context = edit->getTransport().getCurrentPlaybackContext())
        {
            d.playPending = context->isPlayPending();
            if (auto *head = context->getNodePlayHead())
            {
                d.headPlaying = head->isPlaying();
                d.headSeconds = head->getPosition() / context->getSampleRate();
                d.headLoopAvailable = true;
                d.headLoopEnabled = head->isLooping();
                const auto range = head->getLoopRange();
                d.headLoopStartBeat = tempoMap.beatAt(double(range.getStart()) / context->getSampleRate());
                d.headLoopEndBeat = tempoMap.beatAt(double(range.getEnd()) / context->getSampleRate());
            }
        }
    }
    d.edit = reinterpret_cast<std::uintptr_t>(edit.get());
    d.playbackContext =
        edit ? reinterpret_cast<std::uintptr_t>(edit->getTransport().getCurrentPlaybackContext()) : 0;
    d.replacements = replacements;
    d.updates = updates;
    d.samplerCreations = samplerCreations;
    d.midiUpdates = midiUpdates;
    d.cache = samples.statistics();
    for (auto &[id, p] : projectedChannels)
        if (p.sampler)
            d.channels.push_back(
                {id, reinterpret_cast<std::uintptr_t>(p.track),
                 reinterpret_cast<std::uintptr_t>(p.sampler.get()),
                 reinterpret_cast<std::uintptr_t>(p.sample.get()), int(p.notes.size()),
                 dynamic_cast<FixedSampler *>(p.sampler.get())->graphCount(),
                 dynamic_cast<FixedSampler *>(p.sampler.get())->misses(),
                 dynamic_cast<FixedSampler *>(p.sampler.get())->calls(),
                 dynamic_cast<FixedSampler *>(p.sampler.get())->timeSeen(),
                 dynamic_cast<FixedSampler *>(p.sampler.get())->skipSeen(),
                 dynamic_cast<FixedSampler *>(p.sampler.get())->playingSeen(),
                 dynamic_cast<FixedSampler *>(p.sampler.get())->heldInputKeys(),
                 dynamic_cast<FixedSampler *>(p.sampler.get())->samplerAutomationFailureCount(),
                 dynamic_cast<FixedSampler *>(p.sampler.get())->samplerAutomationLastFailure()});
    return d;
}
void AudioEngine::openPluginEditor(Id id, bool open)
{
    if (auto it = currentHosts.find(id); it != currentHosts.end())
        it->second->editor(open);
}
juce::Result AudioEngine::requestPluginRecovery(const Project &project, Id id)
{
    const auto *spec = project.plugin(id);
    const auto *projected = projectedProject.plugin(id);
    if (!spec || !hasProjection || !projected || projected->descriptor != spec->descriptor)
        return juce::Result::fail("Plugin is not in the current prepared project");
    if (capture || rendering() || recoveryPlugin)
        return juce::Result::fail("Finish the pending plugin snapshot, export or recovery first");
    const auto host = parameterHost(id);
    if (host && host->alive())
        return juce::Result::fail("Plugin is already running; recovery was not needed");
    if (!host && !pluginErrors.contains(id))
        return juce::Result::fail("Plugin has not been prepared yet");
    recoveryPlugin = *spec;
    if (host)
        recoveryPlugin->state = host->recoveryState();
    else if (const auto it = recoveryStates.find(id);
             it != recoveryStates.end() && it->second.descriptor == spec->descriptor)
        recoveryPlugin->state = it->second.state;
    return juce::Result::ok();
}
juce::Result AudioEngine::requestPluginStateReload(const Project &project, Id id)
{
    const auto *spec = project.plugin(id);
    const auto *projected = projectedProject.plugin(id);
    if (!spec || !hasProjection || !projected || projected->descriptor != spec->descriptor)
        return juce::Result::fail("Plugin is not in the current prepared project");
    if (capture || rendering() || recoveryPlugin)
        return juce::Result::fail("Finish the pending plugin snapshot, export or recovery first");
    recoveryPlugin = *spec; // Same structural path as recovery, from the given state.
    return juce::Result::ok();
}
std::shared_ptr<PluginHost> AudioEngine::parameterHost(Id id) const
{
    if (auto it = currentHosts.find(id); it != currentHosts.end())
        return it->second;
    return {};
}
bool AudioEngine::pluginProjectionReady(const Project &project, Id id) const
{
    const auto *requested = project.plugin(id), *prepared = projectedProject.plugin(id);
    // An offline worker/editor can remain alive without a native Edit/DSP graph.
    // Exact last-native spec identity still guards target ownership; invalidation
    // clears this eligibility on project replacement even when IDs are reused.
    return (hasProjection || deferredHostProjection) && requested && prepared && requested->descriptor == prepared->descriptor;
}
std::shared_ptr<PluginHost> AudioEngine::preparedPluginHost(const Project &project, Id id) const
{
    return pluginProjectionReady(project, id) ? parameterHost(id) : nullptr;
}
std::vector<AudioEngine::PluginDiagnostic> AudioEngine::pluginDiagnostics() const
{
    std::vector<PluginDiagnostic> result;
    for (auto &[id, host] : currentHosts)
        result.push_back({id, host->latencySamples(), host->statistics(), host->alive(), host->processId(),
                          host->automationStatistics()});
    return result;
}
std::vector<PluginRuntimeState> AudioEngine::pluginRuntimeStates() const
{
    std::vector<PluginRuntimeState> result;
    result.reserve(currentHosts.size());
    for (const auto &[id, host] : currentHosts)
        result.push_back({id, host->runtimeIdentity(), host->parameterRevision(), host->alive()});
    return result;
}
std::optional<bool> AudioEngine::mixerSendAutomationHealthy() const
{
    if (!edit)
        return std::nullopt;
    const auto healthy = [](const te::Plugin *plugin)
    {
        if (const auto *send = dynamic_cast<const MixerSendPlugin *>(plugin))
            return send->sendAutomationHealthy();
        if (const auto *gate = dynamic_cast<const MixerMasterGate *>(plugin))
            return gate->sendAutomationHealthy();
        return true;
    };
    if (channelRouting && channelRouting->effective)
    {
        for (const auto &[list, plugins] : channelRouting->effective->lists)
            for (const auto &plugin : plugins)
                if (!healthy(plugin))
                    return false;
    }
    else
    {
        for (const auto &[id, track] : projectedInserts)
            for (const auto &plugin : track->pluginList.getPlugins())
                if (!healthy(plugin))
                    return false;
    }
    return true;
}
juce::String AudioEngine::pluginStatus(Id id) const
{
    if (auto it = pluginErrors.find(id); it != pluginErrors.end())
        return "Placeholder: " + it->second;
    auto it = currentHosts.find(id);
    if (it == currentHosts.end())
        return "Not yet prepared";
    auto &host = *it->second;
    if (!host.alive())
        return host.error().isNotEmpty() ? host.error()
                                         : "Worker unavailable; recoverable project state retained";
    auto stats = host.statistics();
    auto result = "Isolated VST3 | " + juce::String(host.latencySamples()) + " samples latency | " +
                  juce::String(juce::int64(stats.missed)) + " missed packets";
    const auto automation = host.automationStatistics();
    if (stats.parametersRejected != 0 || automation.failed())
        result += " | Automation delivery warning: " + juce::String(juce::int64(stats.parametersRejected)) +
                  " rejected points, " + juce::String(juce::int64(automation.invalidTiming)) + " timing, " +
                  juce::String(juce::int64(automation.capacityExceeded)) + " capacity, " +
                  juce::String(juce::int64(automation.unavailableTarget)) + " unavailable target, " +
                  juce::String(juce::int64(automation.snapshotUnavailable)) + " missed snapshots";
    return result;
}
juce::Result AudioEngine::configureCompensationHistory(std::uint32_t maximumDelay, std::size_t budget,
                                                       Id phaseSource, Id phaseWet)
{
    if (channelRouting->effective)
        return juce::Result::fail(
            "routing.compensationActive: Retained routing requires a jointly qualified compensation plan");
    if (!edit || playing() || compensationGraph || maximumDelay == 0 || maximumDelay > 131071 || budget == 0)
        return juce::Result::fail("Configure one bounded compensation experiment on a stopped prepared Edit");
    if (!edit->getTransport().getCurrentPlaybackContext())
        return juce::Result::fail("Compensation experiment needs an audio playback context");
    std::optional<CompensationPhasePairPlan> phase;
    if (phaseSource || phaseWet)
    {
        if (!phaseSource || !phaseWet || phaseSource == phaseWet || guide->enabled.load() ||
            projectedProject.solo || projectedProject.mixerSolo ||
            !projectedProject.audioPlacements.empty() ||
            (!projectedProject.automation.sources.empty() ||
             !projectedProject.automation.placements.empty() ||
             !projectedProject.automation.initialValues.empty() || !projectedProject.automation.modulators.empty()))
            return juce::Result::fail("Phase-pair experiment requires a closed non-automated stereo graph");
        int sourceInsert = -1, wetInsert = -1, plugins = 0;
        for (const auto &insert : projectedProject.mixer)
            for (const auto &plugin : insert.effects)
            {
                ++plugins;
                if (plugin.instrument || plugin.bypass || insert.effectsBypassed || plugin.wet != 1 ||
                    !plugin.automationBindings.empty())
                    return juce::Result::fail(
                        "Phase-pair experiment excludes instrument/bypass/automation state");
                if (plugin.id == phaseSource)
                    sourceInsert = insert.index;
                else if (plugin.id == phaseWet)
                    wetInsert = insert.index;
            }
        if (plugins != 2 || sourceInsert <= 0 || wetInsert <= 0 || sourceInsert == wetInsert)
            return juce::Result::fail(
                "Phase-pair experiment requires exactly two effects on distinct non-Master inserts");
        for (const auto &insert : projectedProject.mixer)
        {
            if (insert.pan != 0 || insert.muted || insert.stereoSeparation != 0 ||
                insert.phaseInverted || insert.stereoSwapped || (insert.index != 0 && insert.gain != 1))
                return juce::Result::fail(
                    "Phase-pair experiment excludes additional balance/solo processing");
            if (insert.index == sourceInsert)
            {
                if (insert.routes.size() != 2 || !mixerRoute(projectedProject, sourceInsert, 0) ||
                    !mixerRoute(projectedProject, sourceInsert, wetInsert))
                    return juce::Result::fail("Phase-pair source needs exactly direct Master and wet routes");
            }
            else if (insert.index != 0 && (insert.routes.size() != 1 || insert.routes.front().target != 0))
                return juce::Result::fail("Phase-pair experiment excludes other bus/send paths");
            for (const auto &route : insert.routes)
                if (route.gain != 1)
                    return juce::Result::fail("Phase-pair route gains must be unity");
        }
        for (const auto &channel : projectedProject.channels)
        {
            if (channel.instrument || channel.audioClip ||
                (channel.mixer != 0 && channel.mixer != sourceInsert))
                return juce::Result::fail("Phase-pair experiment excludes other instrument/audio branches");
            if (channel.mixer != sourceInsert)
                for (const auto &pattern : projectedProject.patterns)
                    for (const auto &note : pattern.notes)
                        if (note.channel == channel.id)
                            return juce::Result::fail(
                                "Phase-pair experiment excludes notes outside common source");
        }
        auto source = parameterHost(phaseSource), wet = parameterHost(phaseWet);
        PreparedPluginTransport::Ptr prepared;
        if (productionPluginPlan)
        {
            auto *context = edit->getTransport().getCurrentPlaybackContext();
            auto &manager = engine.getDeviceManager().deviceManager;
            auto *device = manager.getCurrentAudioDevice();
            const auto *sourceSpec = projectedProject.plugin(phaseSource);
            const auto *wetSpec = projectedProject.plugin(phaseWet);
            const auto &owner = productionPluginProjection;
            te::SourceBurstSession::Outcome native;
            source_membership::Receipt members;
            if (!owner || !owner->session || !owner->members || owner->context != context ||
                owner->plan != productionPluginPlan || productionDeviceInvalidated || !device ||
                !sourceSpec || !wetSpec || !source || !wet || source == wet || !source->alive() ||
                !wet->alive() || source->offline() || wet->offline() || !owner->session->read(native) ||
                native.failed || native.retired || !owner->members->read(members) ||
                members.status != source_membership::Status::ready)
                return juce::Result::fail("Phase-pair prepared source session is not healthy/current");
            const auto gate = context->sourceSessionGatePhase();
            const auto &config = owner->session->configuration();
            const auto &transport = productionPluginPlan->budget();
            if ((gate != te::EditPlaybackContext::SourceSessionGatePhase::waitingFirstSerial &&
                 gate != te::EditPlaybackContext::SourceSessionGatePhase::active) ||
                device->getCurrentSampleRate() != productionPluginPlan->sampleRate() ||
                device->getCurrentBufferSizeSamples() != 256 ||
                context->getSampleRate() != productionPluginPlan->sampleRate() ||
                projectedRate != productionPluginPlan->sampleRate() || projectedBlock != 256 ||
                transport.quantum != 256 || transport.packets != 3 || transport.transportSamples != 768 ||
                transport.configuredFrames != 256 || transport.physicalFrames != 256 ||
                transport.sourceFrames != 282 || transport.maximumSourceScalePpm != 1100000 ||
                config.sampleRate != productionPluginPlan->sampleRate() ||
                config.deviceEpoch != transport.deviceEpoch || config.deviceFrames != 256 ||
                config.maximumPhysicalFrames != 256 || config.maximumSourceFrames != 282 ||
                config.maximumSourceScalePpm != 1100000 || config.generation == 0 ||
                native.generation != config.generation || native.deviceEpoch != config.deviceEpoch ||
                members.sessionGeneration != config.generation || members.deviceEpoch != config.deviceEpoch ||
                members.membershipGeneration != config.generation ||
                !source->matchesPrepared(*sourceSpec, projectedRate, 256, productionPluginPlan) ||
                !wet->matchesPrepared(*wetSpec, projectedRate, 256, productionPluginPlan) ||
                source->declaredLatency().nativeSamples != 0 || source->busLayout().auxiliaryChannels != 0 ||
                wet->busLayout().auxiliaryChannels != 0 || wet->latencySamples() < 768 ||
                maximumDelay < 768u + std::uint32_t(wet->declaredLatency().nativeSamples))
                return juce::Result::fail(
                    "Phase-pair requires matching prepared3Q source ownership/configuration");
            prepared = productionPluginPlan; // CONTROL ownership only; graph retains it until retirement.
        }
        else
        {
            if (!source || !wet || source == wet || !source->alive() || !wet->alive() || source->offline() ||
                wet->offline() || source->processingQuantum() != wet->processingQuantum() ||
                source->transportLatencySamples() != source->processingQuantum() ||
                wet->transportLatencySamples() != wet->processingQuantum() ||
                source->processingQuantum() != 256 || source->declaredLatency().nativeSamples != 0 ||
                source->busLayout().auxiliaryChannels != 0 || wet->busLayout().auxiliaryChannels != 0 ||
                maximumDelay < 256u + std::uint32_t(wet->declaredLatency().nativeSamples))
                return juce::Result::fail(
                    "Phase-pair experiment needs one-quantum live matching256-frame stereo "
                    "workers and zero-native source");
        }
        phase = CompensationPhasePairPlan{
            source, wet, phaseSource, phaseWet, source->processingQuantum(), std::move(prepared)};
    }
    auto candidate =
        std::make_unique<CompensationGraphBinding>(edit->getTransport(), maximumDelay, budget, phase);
    const auto ready = candidate->refresh();
    if (ready.failed())
        return ready; // Prepared publication failed; candidate callback/state retires here on control.
    refusedCompensationLatencies.clear();
    compensationGraph = std::move(candidate);
    return juce::Result::ok(); // Marker preparation, not an audible adoption acknowledgement.
}
juce::Result AudioEngine::holdCompensationGraphPublicationForTest(bool hold)
{
    return compensationGraph ? compensationGraph->holdPublicationForTest(hold)
                             : juce::Result::fail("No compensation experiment");
}
CompensationGraphObservation AudioEngine::compensationGraphObservation() const
{
    return compensationGraph ? compensationGraph->observation() : CompensationGraphObservation{};
}
juce::String AudioEngine::compensationGraphFailure() const
{
    return compensationGraph ? compensationGraph->error() : juce::String{};
}
void AudioEngine::reclaimPluginHosts()
{
    serviceChannelRouting();
    // A no-device deferred document deliberately skips buildEdit. An ordinary
    // worker preparation started before that boundary therefore has no graph
    // consumer to retire its completed job. Keep every incumbent Host; rehome
    // private results for the existing asynchronous CONTROL retirement service.
    // A full replacement owner must drain its own exact preparation instead.
    auto &dormant = preparation[0];
    // Only the ordinary consumer can be invalidated by device appearance or a
    // replaced document. Full canonical preparations retain their exact owner's
    // existing cancellation/drain path and are never stolen here.
    const bool lostDeferredConsumer =
        dormant && dormant->deferredNativeAccess &&
        (!deferredDocumentProjection || deviceEverActive.load(std::memory_order_acquire) ||
         engine.getDeviceManager().deviceManager.getCurrentAudioDevice() ||
         dormant->fullProjectionOwner != routingController ||
         (routingController && dormant->fullProjectionIdentity != routingController->projectIdentity));
    const bool oldOrphan = deferredDocumentProjection &&
                           !engine.getDeviceManager().deviceManager.getCurrentAudioDevice() && dormant &&
                           !dormant->deferredNativeAccess;
    if (!fullProjectionController && !channelRouting->explicitFullReplacement && dormant &&
        !dormant->fullProjectionDestination && lostDeferredConsumer)
        dormant->signalThreadShouldExit();
    if (!fullProjectionController && !channelRouting->explicitFullReplacement && dormant &&
        !dormant->fullProjectionDestination && (oldOrphan || lostDeferredConsumer) &&
        dormant->done.load(std::memory_order_acquire) && !dormant->isThreadRunning() &&
        dormant->waitForThreadToExit(0))
    {
        ownedHosts.reserve(ownedHosts.size() + dormant->results.size());
        for (const auto &[id, result] : dormant->results)
            if (result.host &&
                std::none_of(currentHosts.begin(), currentHosts.end(),
                             [&](const auto &entry) { return entry.second == result.host; }) &&
                std::find(ownedHosts.begin(), ownedHosts.end(), result.host) == ownedHosts.end())
                ownedHosts.push_back(result.host);
        dormant.reset(); // Worker thread actually exited; never a callback wait.
    }
    bool latencyChanged = channelRouting->deferredLatency && !channelRoutingPostPending();
    if (latencyChanged)
        channelRouting->deferredLatency = false;
    for (auto &[id, host] : currentHosts)
    {
        host->service();
        auto samples = host->latencySamples();
        auto [it, inserted] = observedPluginLatencies.emplace(id, samples);
        if (!inserted && it->second != samples)
        {
            it->second = samples;
            latencyChanged = true;
        }
    }
    std::erase_if(observedPluginLatencies, [&](auto &item) { return !currentHosts.contains(item.first); });
    if (channelRoutingPostPending())
    {
        channelRouting->deferredLatency = channelRouting->deferredLatency || latencyChanged;
        latencyChanged = false; // Worker service/observation above still runs.
    }
    // Failed experimental plans stay explicit until a declaration/topology
    // changes. Do not allocate/rebuild the same refused graph on every timer tick.
    if (!channelRoutingPostPending() && (!compensationGraph || !compensationGraph->publicationHeld()) &&
        (!compensationGraph || refusedCompensationLatencies.empty() ||
         refusedCompensationLatencies != observedPluginLatencies))
    {
        // Freeze all declarations before a graph build. A moving worker atomic is
        // not a coherent graph-wide latency plan. These writes are control-only;
        // existing PluginNodes keep their already prepared latency properties.
        std::vector<std::pair<te::Plugin *, int>> previousLatencies;
        for (auto &[id, plugin] : effectPlugins)
            if (auto found = observedPluginLatencies.find(id); found != observedPluginLatencies.end())
                if (auto *effect = dynamic_cast<RemoteEffect *>(plugin.get()))
                {
                    if (compensationGraph && effect->preparedGraphLatencySamples() != found->second)
                        previousLatencies.emplace_back(effect, effect->preparedGraphLatencySamples());
                    latencyChanged = effect->setGraphLatencySamples(found->second) || latencyChanged;
                }
        for (const auto &channel : projectedProject.channels)
            if (channel.instrument)
                if (auto found = observedPluginLatencies.find(channel.instrument->id);
                    found != observedPluginLatencies.end())
                    if (auto projected = projectedChannels.find(channel.id);
                        projected != projectedChannels.end())
                        if (auto *sampler = dynamic_cast<FixedSampler *>(projected->second.sampler.get()))
                        {
                            if (compensationGraph && sampler->preparedGraphLatencySamples() != found->second)
                                previousLatencies.emplace_back(sampler,
                                                               sampler->preparedGraphLatencySamples());
                            latencyChanged = sampler->setGraphLatencySamples(found->second) || latencyChanged;
                        }
        // A plugin changing its DSP latency is a graph property change, unlike a
        // routine note edit. Recalculate compensation on the control thread.
        if (latencyChanged && edit)
        {
            if (compensationGraph)
            {
                if (compensationGraph->refresh().failed())
                {
                    refusedCompensationLatencies = observedPluginLatencies;
                    for (auto [plugin, latency] : previousLatencies)
                    {
                        // Keep the old graph's control-side declarations after a
                        // refused plan. The binding retains explicit failure state;
                        // this is not rollback of the native plugin's own DSP.
                        if (auto *effect = dynamic_cast<RemoteEffect *>(plugin))
                            effect->setGraphLatencySamples(latency);
                        else if (auto *sampler = dynamic_cast<FixedSampler *>(plugin))
                            sampler->setGraphLatencySamples(latency);
                    }
                }
                else
                    refusedCompensationLatencies.clear();
            }
            else
                edit->restartPlayback();
        }
    }
    std::erase_if(
        observedParameterRevisions, [&](auto &item)
        { return !currentHosts.contains(item.first) || currentHosts.at(item.first) != item.second.host; });
    // Graph-held hosts stay pinned; the last owning reference is released here, never by DSP.
    std::erase_if(
        lastCapturedRevisions, [&](auto &item)
        { return !currentHosts.contains(item.first) || currentHosts.at(item.first) != item.second.host; });
    retireUnusedPluginHosts();
    std::erase_if(ownedSamples, [](auto &sample) { return sample.use_count() == 1; });
    std::erase_if(ownedTimelines, [](auto &clock) { return clock.use_count() == 1; });
    std::erase_if(retiredPreparation, [](auto &job)
                  { return job->done.load(std::memory_order_acquire) && !job->isThreadRunning(); });
}
void AudioEngine::retireUnusedPluginHosts()
{
    // A completed removal transaction may be replaced while an old native
    // graph still pins its Host. Keep polling that exact owned instance for
    // graceful exit/timeout recovery independently of transaction lifetime.
    for (const auto &host : ownedHosts)
        if (std::none_of(currentHosts.begin(), currentHosts.end(),
                         [&](const auto &entry) { return entry.second == host; }))
            host->service();
    // Control only: a completed offline RenderWorker and Edit must release their
    // graph/plugin references before this can initiate graceful worker shutdown.
    std::erase_if(ownedHosts,
                  [](auto &host)
                  {
                      if (host.use_count() != 1)
                          return false;
                      host->beginRetirement();
                      host->service();
                      // Both host/process destructors can wait. Keep their mapping/handles
                      // until the kernel reports exit, then release on this control thread.
                      return host->retirementComplete();
                  });
}
bool AudioEngine::consumePluginChanges()
{
    bool changed = false;
    for (auto &[id, host] : currentHosts)
    {
        auto value = host->parameterRevision();
        auto &seen = observedParameterRevisions[id];
        if (seen.host != host)
            seen = {host, value};
        else if (value != seen.value)
        {
            seen.value = value;
            changed = true;
        }
    }
    return changed;
}
void AudioEngine::markCapturedPluginStateSaved()
{
    for (auto &[id, revision] : lastCapturedRevisions)
        if (auto it = currentHosts.find(id); it != currentHosts.end() && it->second == revision.host)
            observedParameterRevisions[id] = revision;
}
juce::Result AudioEngine::requestPluginState(const Project &project, const std::vector<Id> *only,
                                             bool reuseUnchanged)
{
    if (capture)
        return juce::Result::fail("A plugin state snapshot is already pending");
    // One application control owner: preflight every participating live host
    // before issuing any capture. An unread completed editor receipt is not
    // physical work and must not block saving. Never silently skip a refused
    // live request and then save the older opaque project state as current.
    for (const auto &[id, host] : currentHosts)
    {
        const auto *plugin = project.plugin(id);
        const auto *projected = projectedProject.plugin(id);
        if ((hasProjection || deferredHostProjection) && (!only || std::find(only->begin(), only->end(), id) != only->end()) && plugin &&
            projected && plugin->descriptor == projected->descriptor && host->alive() && host->editorBusy())
            return juce::Result::fail(
                "Finish the pending native plugin editor request before capturing state");
    }
    // Finish every fallible copy/reservation before installing capture or
    // admitting any worker side effects. A failed allocation cannot strand a
    // capture with no application observer, or lose earlier request ownership.
    auto prepared = std::make_unique<StateCapture>();
    prepared->project = project;
    std::size_t capacity = project.channels.size();
    for (const auto &insert : project.mixer)
        capacity += insert.effects.size();
    prepared->pending.reserve(capacity);
    prepared->deadline = juce::Time::getMillisecondCounter() + 10000;
    lastCapturedRevisions.clear();
    capture = std::move(prepared);
    auto request = [&](const PluginSpec &plugin)
    {
        if (only && std::find(only->begin(), only->end(), plugin.id) == only->end())
            return;
        const auto *projected = projectedProject.plugin(plugin.id);
        if (auto it = currentHosts.find(plugin.id); (hasProjection || deferredHostProjection) && it != currentHosts.end() && projected &&
                                                    projected->descriptor == plugin.descriptor)
        {
            if (it->second->alive())
            {
                juce::String state;
                unsigned revision = 0;
                if (reuseUnchanged && it->second->reuseCapturedState(state, revision))
                {
                    capture->project.plugin(plugin.id)->state = std::move(state);
                    capture->revisions[plugin.id] = {it->second, revision};
                    return;
                }
                const auto serial = it->second->requestState();
                if (serial)
                    capture->pending.push_back({plugin.id, it->second, serial});
                else
                    capture->refusedAdmission = true;
                // Drain any earlier admissions through pollPluginState even
                // after a refusal. No partial snapshot can be saved as success.
            }
            else
                capture->project.plugin(plugin.id)->state = it->second->recoveryState();
        }
        else if (const auto saved = recoveryStates.find(plugin.id);
                 saved != recoveryStates.end() && saved->second.descriptor == plugin.descriptor)
            capture->project.plugin(plugin.id)->state = saved->second.state;
    };
    for (auto &channel : project.channels)
        if (channel.instrument)
            request(*channel.instrument);
    for (auto &insert : project.mixer)
        for (auto &effect : insert.effects)
            request(effect);
    return juce::Result::ok();
}
bool AudioEngine::pollPluginState(Project &project, juce::Result &result)
{
    if (!capture)
        return false;
    auto store = [&](Id id, const juce::String &state)
    {
        for (auto &c : capture->project.channels)
            if (c.instrument && c.instrument->id == id)
                c.instrument->state = state;
        for (auto &m : capture->project.mixer)
            for (auto &fx : m.effects)
                if (fx.id == id)
                    fx.state = state;
    };
    bool pending = false;
    for (auto &item : capture->pending)
    {
        if (item.serial == 0)
            continue;
        juce::String state;
        unsigned revision = 0;
        if (item.host->readState(item.serial, state, &revision))
        {
            store(item.id, state);
            capture->revisions[item.id] = {item.host, revision};
            item.serial = 0;
        }
        else
            pending = true;
    }
    if (pending && juce::Time::getMillisecondCounter() < capture->deadline)
        return false;
    result = pending ? juce::Result::fail("Plugin state snapshot timed out; existing output preserved")
             : capture->refusedAdmission
                 ? juce::Result::fail("Live plugin state request refused; existing output preserved")
                 : juce::Result::ok();
    if (result.wasOk())
    {
        lastCapturedRevisions.swap(capture->revisions);
        project = std::move(capture->project);
    }
    else
        lastCapturedRevisions.clear();
    capture.reset();
    return true;
}
void AudioEngine::playStop(int countBars)
{
    if (!edit || deferredDocumentProjection)
        return;
    if (playing())
        stop();
    else
    {
        stopAudition();
        auto &transport = edit->getTransport();
        if (countBars > 0)
        {
            bool loop = transport.looping;
            double target = transport.getPosition().inSeconds();
            double start = target - countInSeconds(countBars);
            guide->begin(start, target);
            transport.play(false);
            if (auto *context = transport.getCurrentPlaybackContext())
            {
                if (auto *head = context->getNodePlayHead())
                {
                    if (loop)
                        head->setRollInToLoop(te::toSamples(seconds(start), context->getSampleRate()));
                    else
                        head->setPosition(te::toSamples(seconds(start), context->getSampleRate()));
                    // performPlay posts its cursor for the next callback. Replace that pending
                    // cursor too, otherwise it overwrites the roll-in before the first block.
                    context->postPosition(seconds(start));
                    guide->ready = true;
                }
            }
        }
        else
        {
            guide->cancel();
            transport.play(false);
        }
    }
}
void AudioEngine::stop()
{
    guide->cancel();
    if (edit)
        edit->getTransport().stop(false, false);
    stopAudition();
}
void AudioEngine::seekBeat(double beat)
{
    // A Loop marker's section stops looping when the playhead is sent outside it, before the
    // transport can wrap back into it.
    if (songLoopFromMarker && songLoop && (beat < songLoop->first - 1.e-9 || beat >= songLoop->second))
        setSongLoopBeats(std::nullopt);
    if (deferredDocumentProjection && !edit) deferredPositionBeat = std::max(0., beat);
    guide->cancel();
    if (edit)
        edit->getTransport().setPosition(seconds(tempoMap.secondsAt(std::max(0., beat))));
}
void AudioEngine::setLooping(bool loop)
{
    if (deferredDocumentProjection) deferredLooping = loop;
    if (edit)
        edit->getTransport().looping = loop;
    publishLoopBounds();
}
void AudioEngine::setSongLoopBeats(std::optional<std::pair<double, double>> beats, bool fromMarker)
{
    songLoopFromMarker = beats && fromMarker;
    if (beats == songLoop)
        return;
    songLoop = beats;
    if (!edit)
        return;
    edit->getTransport().setLoopRange(
        loopRange(projectedProject, projectedPatternMode,
                  durationBeats(projectedProject, projectedPatternMode, projectedPattern)));
    publishLoopBounds();
}
bool AudioEngine::looping() const
{
    return edit ? bool(edit->getTransport().looping) : deferredDocumentProjection && deferredLooping;
}
std::optional<std::pair<double, double>> AudioEngine::loopSeconds() const
{
    if (!edit || !edit->getTransport().looping)
        return std::nullopt;
    const auto range = edit->getTransport().getLoopRange();
    return std::pair{range.getStart().inSeconds(), range.getEnd().inSeconds()};
}
bool AudioEngine::playing() const
{
    return edit && edit->getTransport().isPlaying();
}
double AudioEngine::countInSeconds(int bars) const
{
    const auto meter = playbackMeter(projectedProject, projectedPatternMode, projectedPattern);
    return juce::jlimit(0, 2, bars) * meter.first * 4. / meter.second * 60 / tempoMap.bpmAt(positionBeat());
}
void AudioEngine::saveGuide()
{
    if (!persistentGuide)
        return;
    auto value = std::make_unique<juce::DynamicObject>();
    value->setProperty("version", 1);
    value->setProperty("metronome", guide->enabled.load());
    value->setProperty("countInBars", guideBars);
    value->setProperty("lastBars", lastGuideBars);
    value->setProperty("sound", guide->sound.load());
    value->setProperty("gain", guide->gain.load());
    guideFile.getParentDirectory().createDirectory();
    guideFile.replaceWithText(juce::JSON::toString(juce::var(value.release())));
}
void AudioEngine::setMetronome(bool value)
{
    guide->enabled = value;
    saveGuide();
}
void AudioEngine::setGuideSound(int value)
{
    guide->sound = juce::jlimit(0, 2, value);
    saveGuide();
}
void AudioEngine::setGuideGain(float value)
{
    guide->gain = std::isfinite(value) ? juce::jlimit(0.f, 1.f, value) : .65f;
    saveGuide();
}
void AudioEngine::setCountInBars(int value)
{
    guideBars = juce::jlimit(0, 2, value);
    if (guideBars)
        lastGuideBars = guideBars;
    saveGuide();
}
void AudioEngine::cancelCountIn()
{
    if (guide->remaining.load() > 0 && edit)
    {
        const auto target = guide->leadEnd.load();
        stop();
        edit->getTransport().setPosition(seconds(target));
    }
    guide->cancel();
}
double AudioEngine::positionBeat() const
{
    return edit ? tempoMap.beatAt(edit->getTransport().getPosition().inSeconds()) :
                  deferredDocumentProjection ? deferredPositionBeat : 0;
}
juce::String AudioEngine::deviceDescription() const
{
    auto *device = engine.getDeviceManager().deviceManager.getCurrentAudioDevice();
    return device ? device->getName() + "  " + juce::String(int(device->getCurrentSampleRate())) + " Hz / " +
                        juce::String(device->getCurrentBufferSizeSamples())
                  : "No audio device - offline export available";
}
juce::Result AudioEngine::commitRenderedFile(juce::TemporaryFile &temporary,
                                             std::span<const std::shared_ptr<PluginHost>> hosts,
                                             bool completed, const juce::String &taskError,
                                             bool samplerAutomationFailed)
{
    if (samplerAutomationFailed)
        return juce::Result::fail("sampler.automation.renderFailed: Sampler automation processing failed; "
                                  "previous output preserved");
    for (const auto &host : hosts)
        if (!host || !host->alive() || host->deliveryFailed())
        {
            // Name the worker's own reason when it reported one.
            const auto reason = host ? host->error() : juce::String{};
            return juce::Result::fail(
                "VST3 audio/MIDI/automation delivery failed during export" +
                (reason.isNotEmpty() ? " (" + reason + ")" : juce::String{}) + "; previous output preserved");
        }
    if (!completed)
        return juce::Result::fail("Render cancelled; previous output preserved");
    if (taskError.isNotEmpty())
        return juce::Result::fail(taskError);
    if (!temporary.getFile().existsAsFile() || !temporary.overwriteTargetFileWithTemporary())
        return juce::Result::fail("Cannot commit WAV; previous output preserved");
    return juce::Result::ok();
}
class AudioEngine::RenderWorker final : public juce::Thread
{
  public:
    std::atomic<float> progress{0};
    std::atomic<bool> done{false};
    bool completed = false;
    juce::File target;
    juce::TemporaryFile temporary;
    std::unique_ptr<te::Renderer::RenderTask> task;
    std::vector<std::shared_ptr<PluginHost>> hosts;
    // CONTROL collects exact render sources; the owning Edit outlives this worker/task.
    std::vector<FixedSampler *> samplers;
    bool samplerFailure() const noexcept
    {
        return std::any_of(samplers.begin(), samplers.end(),
                           [](const auto *sampler) { return sampler->samplerAutomationFailureCount() != 0; });
    }
    bool pluginFailure() const
    {
        return std::any_of(hosts.begin(), hosts.end(),
                           [](auto &host) { return !host->alive() || host->deliveryFailed(); });
    }
    int tailMode = 0, bitDepth = 24;
    int outputChannels = 0; // 0 stereo, 1 mono (L+R)/2, 2 left only, 3 right only.
    bool dither = false;     // TPDF dither at the output LSB (16/24-bit integer only).
    // Pre-roll: the render starts two blocks before the Song (silence) and that part is
    // removed afterwards. A plugin's first processed block in a render comes out late by its
    // native latency and is realigned from the next block, which left a jump inside the
    // first block of every export with a latent effect; the pre-roll absorbs it.
    int prerollSamples = 0;
    double songSeconds = 0, sampleRate = 48000;
    RenderWorker(te::Edit &edit, const juce::File &file, double rate, int bits, int block, double endSeconds,
                 std::vector<std::shared_ptr<PluginHost>> plugins = {}, int tail = 0)
        : juce::Thread("VibeRa WAV render"), target(file), temporary(file), hosts(std::move(plugins))
    {
        tailMode = std::clamp(tail, 0, 2);
        bitDepth = bits;
        sampleRate = rate;
        songSeconds = endSeconds;
        if (tailMode != 0)
            endSeconds += 8.; // Render room for reverb/delay tails; trimmed or wrapped afterwards.
        for (const auto *track : te::getAudioTracks(edit))
            for (auto *plugin : track->pluginList.getPlugins())
                if (auto *sampler = dynamic_cast<FixedSampler *>(plugin))
                    samplers.push_back(sampler);
        static juce::WavAudioFormat format;
        te::Renderer::Parameters params(edit);
        params.destFile = temporary.getFile();
        params.audioFormat = &format;
        params.sampleRateForAudio = rate;
        params.bitDepth = bits;
        params.blockSizeForAudio = block;
        prerollSamples = 2 * block;
        params.time = {seconds(-prerollSamples / rate), seconds(endSeconds)};
        params.canRenderInMono = false;
        params.useMasterPlugins = true;
        params.ditheringEnabled = false;
        task = std::make_unique<te::Renderer::RenderTask>("VibeRa WAV", params, &progress, nullptr);
        task->setCancellationCheck([this]
                                   { return threadShouldExit() || pluginFailure() || samplerFailure(); });
    }
    void run() override
    {
        while (!threadShouldExit())
        {
            if (task->runJob() == juce::ThreadPoolJob::jobHasFinished)
            {
                completed = !threadShouldExit();
                break;
            }
        }
        if (completed && (tailMode != 0 || prerollSamples > 0))
            completed = applyTail();
        done = true;
    }
    // Leave: trim trailing silence (below -90 dBFS) after the Song end. Wrap: mix everything
    // past the Song end back onto the start, then cut at the Song end.
    bool applyTail()
    {
        juce::WavAudioFormat wav;
        // A render that wrote no file must fail truthfully: JUCE's WAV reader dereferences a null stream.
        auto stream = temporary.getFile().createInputStream();
        if (!stream)
            return false;
        std::unique_ptr<juce::AudioFormatReader> reader(wav.createReaderFor(stream.release(), true));
        if (!reader)
            return false;
        int frames = int(reader->lengthInSamples);
        const int channels = int(reader->numChannels);
        juce::AudioBuffer<float> pcm(channels, frames);
        reader->read(&pcm, 0, frames, 0, true, true);
        reader.reset();
        if (prerollSamples > 0)
        {
            frames = std::max(0, frames - prerollSamples);
            for (int c = 0; c < channels; ++c)
                std::memmove(pcm.getWritePointer(c), pcm.getReadPointer(c) + prerollSamples, size_t(frames) * sizeof(float));
        }
        const int songEnd = std::min(frames, int(std::lround(songSeconds * sampleRate)));
        int keep = frames;
        if (tailMode == 0)
            keep = songEnd;
        else if (tailMode == 1)
        {
            int last = songEnd;
            const float floor = juce::Decibels::decibelsToGain(-90.f);
            for (int i = frames - 1; i >= songEnd; --i)
                if (std::abs(pcm.getSample(0, i)) > floor || (channels > 1 && std::abs(pcm.getSample(1, i)) > floor))
                {
                    last = i + 1;
                    break;
                }
            keep = std::min(frames, last + int(sampleRate * .05));
        }
        else
        {
            for (int c = 0; c < channels; ++c)
                for (int i = songEnd; i < frames; ++i)
                    pcm.addSample(c, (i - songEnd) % std::max(1, songEnd), pcm.getSample(c, i));
            keep = songEnd;
        }
        int written = channels;
        if (outputChannels != 0 && channels >= 2)
        {
            // FL export Channels: Mono (merged / left / right).
            for (int i = 0; i < keep; ++i)
            {
                const float l = pcm.getSample(0, i), r = pcm.getSample(1, i);
                pcm.setSample(0, i, outputChannels == 1 ? (l + r) * .5f : outputChannels == 2 ? l : r);
            }
            written = 1;
        }
        if (dither && bitDepth < 32)
        {
            // Triangular (TPDF) dither of +/-1 LSB before integer quantisation; deterministic seed.
            const float lsb = 1.f / float(1 << (bitDepth - 1));
            juce::Random random(0x5eed);
            for (int c = 0; c < written; ++c)
                for (int i = 0; i < keep; ++i)
                    pcm.setSample(c, i, pcm.getSample(c, i) + (random.nextFloat() - random.nextFloat()) * lsb);
        }
        temporary.getFile().deleteFile();
        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(temporary.getFile().createOutputStream().release(),
                                                                           sampleRate, unsigned(written), bitDepth, {}, 0));
        if (!writer)
            return false;
        if (written == channels)
            return writer->writeFromAudioSampleBuffer(pcm, 0, keep);
        juce::AudioBuffer<float> mono(pcm.getArrayOfWritePointers(), 1, keep);
        return writer->writeFromAudioSampleBuffer(mono, 0, keep);
    }
    juce::Result commit()
    {
        return AudioEngine::commitRenderedFile(temporary, hosts, completed, task->errorMessage,
                                               samplerFailure());
    }
    ~RenderWorker() override
    {
        signalThreadShouldExit();
        stopThread(10000);
    }
};
juce::Result AudioEngine::renderSync(const Project &p, const juce::File &file, double rate, int bits,
                                     int block)
{
    if ((rate != 44100 && rate != 48000) || (bits != 16 && bits != 24 && bits != 32) || block < 32 ||
        block > 8192)
        return juce::Result::fail("Unsupported render settings");
    if (p.placements.empty() && p.audioPlacements.empty())
        return juce::Result::fail("Place a clip in the Playlist before exporting Song");
    juce::String error;
    renderRate = rate;
    renderBlock = block;
    auto render = [&]() -> juce::Result
    {
        const auto firstHost = ownedHosts.size();
        auto renderingEdit = buildEdit(p, false, p.patterns.front().id, te::Edit::forRendering, error);
        if (!renderingEdit)
            return juce::Result::fail(error);
        RenderWorker job(*renderingEdit, file, rate, bits, block, p.songSeconds(p.endBeat()),
                         {ownedHosts.begin() + std::ptrdiff_t(firstHost), ownedHosts.end()});
        while (job.task->runJob() != juce::ThreadPoolJob::jobHasFinished)
        {
        }
        job.completed = job.prerollSamples > 0 ? job.applyTail() : true;
        return job.commit();
    };
    try
    {
        auto result = render();    // Destroy task, graph and temporary Edit first.
        retireUnusedPluginHosts(); // No waits; retain mappings until kernel exit.
        return result;
    }
    catch (...)
    {
        retireUnusedPluginHosts(); // Partial failed offline preparation is unused too.
        throw;
    }
}
juce::Result AudioEngine::startRender(const Project &p, const juce::File &file, double rate, int bits, int tail)
{
    if (worker)
        return juce::Result::fail("A render is already active");
    if ((rate != 44100 && rate != 48000) || (bits != 16 && bits != 24 && bits != 32))
        return juce::Result::fail("Export is 44.1 or 48 kHz at 16, 24 or 32 (float) bits");
    if (p.placements.empty() && p.audioPlacements.empty())
        return juce::Result::fail("Place a clip in the Playlist before exporting Song");
    renderRate = rate;
    renderBlock = 512;
    auto ready = preparePlugins(p, true);
    if (ready.failed())
        return ready;
    juce::String error;
    renderRate = rate;
    renderBlock = 512;
    auto firstHost = ownedHosts.size();
    renderEdit = buildEdit(p, false, p.patterns.front().id, te::Edit::forRendering, error);
    if (!renderEdit)
        return juce::Result::fail(error);
    stop(); // Only after workers, metadata, workload and immutable timeline are prepared.
    worker =
        std::make_unique<RenderWorker>(*renderEdit, file, rate, bits, 512, p.songSeconds(p.endBeat()),
                                       std::vector<std::shared_ptr<PluginHost>>(
                                           ownedHosts.begin() + std::ptrdiff_t(firstHost), ownedHosts.end()), tail);
    worker->outputChannels = exportOutputChannels;
    worker->dither = exportDither;
    worker->startThread();
    preparation[1].reset();
    return juce::Result::ok();
}
bool AudioEngine::rendering() const
{
    return worker && !worker->done;
}
float AudioEngine::renderProgress() const
{
    return worker ? worker->progress.load() : 0;
}
void AudioEngine::cancelRender()
{
    if (worker)
        worker->signalThreadShouldExit();
}
juce::Result AudioEngine::finishRender()
{
    if (!worker)
        return juce::Result::ok();
    worker->signalThreadShouldExit();
    worker->waitForThreadToExit(10000);
    auto result = worker->commit();
    worker.reset();
    renderEdit.reset();
    return result;
}
juce::Result AudioEngine::pollAudition()
{
    std::shared_ptr<const Sample> prepared;
    PreviewWaveform waveform;
    auto result = juce::Result::ok();
    if (!previewLoader.poll(prepared, result, &waveform) || result.failed())
        return result;
    if (!preview.publish(prepared, 0, auditionToEnd))
        return juce::Result::fail("Sample preview busy; select the sample again");
    auditionSample = std::move(prepared);
    auditionWaveform = std::move(waveform);
    auditionSerial = preview.publishedSerial();
    return juce::Result::ok();
}
juce::Result AudioEngine::seekAudition(float fraction)
{
    if (!auditionSample || !std::isfinite(fraction))
        return juce::Result::fail("Select an audio sample before playing its preview");
    if (!preview.publish(auditionSample, fraction, true))
        return juce::Result::fail("Sample preview busy; try again");
    auditionSerial = preview.publishedSerial();
    return juce::Result::ok();
}
juce::Result AudioEngine::audition(const juce::String &source)
{
    previewLoader.cancel();
    std::shared_ptr<const Sample> prepared;
    auto result = samples.prepare(source, prepared);
    if (result.failed())
        return result;
    if (!preview.publish(prepared))
        return juce::Result::fail("Sample preview busy; select the sample again");
    auditionWaveform = PreviewWaveform::prepare(*prepared, source);
    auditionSample = std::move(prepared);
    auditionSerial = preview.publishedSerial();
    return juce::Result::ok();
}
#include "RetainedContextFixture.inc"
#include "RetainedInstrumentContextFixture.inc"
#include "RetainedWaveContextFixture.inc"
} // namespace vibera
