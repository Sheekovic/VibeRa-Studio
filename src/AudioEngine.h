// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "MixerMeter.h"
#include "CompensationGraphState.h"
#include "Project.h"
#include "StoppedEffectWorkers.h"
#include "PreparedSourceHosts.h"
#include "SampleCache.h"
#include "SamplePreview.h"
#include "PreviewLoader.h"
#include "LiveTimeline.h"
#include "MidiInput.h"
#include "Guide.h"
#include "AudioInputCapture.h"
#include <tracktion_engine/tracktion_engine.h>
#include <atomic>
#include <map>

namespace vibera
{
namespace te = tracktion;
class CompensationGraphBinding;
class Controller;
class PreparedControllerChange;
struct ProductionMembershipSdkFixture;
struct RetainedContextFixture;
struct RetainedInstrumentContextFixture;
struct RetainedWaveContextFixture;
struct FiniteEffectBaselineFixture; // ORIGINAL internal acceptance access, not a provider-registration API.
namespace experimental
{
class AppMixerAudioPorts;
}
class AudioEngine
{
    friend struct FiniteEffectBaselineFixture;
    friend struct ProductionMembershipSdkFixture;
    void withProductionMembershipSdkFixture(Controller &, PreparedControllerChange &, const PluginSpec &,
                                            const std::function<void(ProductionMembershipSdkFixture &)> &);
    struct FiniteEffectBaselineState;
    std::shared_ptr<FiniteEffectBaselineState> finiteEffectBaseline;
    juce::Result prepareFiniteEffectBaseline(const Controller &, Id, const juce::File &,
                                             const juce::String &);
    juce::Result validateFiniteEffectBaseline(const Project &) const;
    bool refuseFiniteEffectDeviceReprepare() noexcept;
    void withFiniteEffectBaselineContext(const Controller &,
                                         const std::function<void(te::Edit &, te::EditPlaybackContext &,
                                                                  experimental::AppMixerAudioPorts &)> &);
    struct EffectiveRoutingLists;
    struct ChannelRoutingState;
    struct StoppedEffectDelta;
    struct StoppedEffectMembership;
    friend class ChannelRoutingCoordinator;
    Controller *routingController = nullptr; // Borrowed canonical owner; never read by DSP.
    Controller *fullProjectionController = nullptr; // Sole CONTROL rebuild call scope only.
    PreparedControllerChange *fullProjectionChange = nullptr;
    const std::function<juce::Result()> *fullProjectionAdmission = nullptr;
    void bindRoutingController(Controller &);
    void serviceStoppedEffectMembership();
    void abandonStoppedEffectMembership(const std::shared_ptr<StoppedEffectMembership> &) noexcept;
    struct InitialSourceTransportState;
    struct InitialSourceRoutePermit;
    std::unique_ptr<InitialSourceTransportState> initialSourceTransport;
    struct ProductionPluginProjection;
    PreparedPluginTransport::Ptr productionPluginPlan;
    std::shared_ptr<ProductionPluginProjection> productionPluginProjection;
    const int explicitPhysicalProviderFrames;
    std::uint64_t productionSessionGeneration = 0;
    bool productionDeviceInvalidated = false;
    bool productionDeviceRetryQueued = false;
    juce::Result prepareProductionPluginPlan(const Project &, PreparedPluginTransport::Ptr &);
    juce::Result stageProductionPluginProjection(te::Edit &, const Project &,
                                                 const std::map<Id, std::shared_ptr<PluginHost>> &,
                                                 std::shared_ptr<ProductionPluginProjection> &);
    bool invalidateProductionPluginDevice() noexcept;
    bool currentProductionPluginContext(const te::EditPlaybackContext *) const noexcept;
    std::shared_ptr<SamplerCutClock> samplerCutClock = std::make_shared<SamplerCutClock>(0);

  public:
    explicit AudioEngine(bool enableDevice = true, juce::File guidePreferences = {},
                         int explicitProviderFrames = 0);
    ~AudioEngine();
    te::Engine engine;
    MidiInputRouter midiInput;
    MidiInputRouter typingInput{false};
    // Audio-input takes and monitoring; registered after Tracktion so it runs first
    // in each device cycle and reads the block-start stream time.
    AudioInputCapture inputCapture{[this] { return engine.getDeviceManager().getCurrentStreamTime(); },
                                   [this](TimelineStamp::Value &value) { return guide->timeline.read(value); }};
    void setMidiTarget(Id);
    bool previewChannelNote(Id, int pitch, bool on, int velocity = 100);
    void stopChannelPreview(Id); // Releases only this Channel's independent preview lane.
    std::shared_ptr<const Sample> channelSample(Id) const;
    bool channelSampleMatches(Id, const juce::String &source) const; // CONTROL projection witness.
    juce::Result rebuild(const Project &, bool patternMode, Id selectedPattern);
    // ORIGINAL explicit initial-only engineering experiment. No UI/default
    // switch or incumbent policy migration. One exact frozen project/Host bank.
    struct InitialSourceTransportOptions
    {
        int quantum = 256, maximumSourceScalePpm = 1000000, explicitProviderFrames = 0;
        std::uint64_t sessionGeneration = 0, membershipGeneration = 0;
        bool audioOnlyMixerPorts = false; // Internal baseline-only experiment, never a default/UI switch.
        bool audioZeroHistoryProofs = false; // Requires ports; no joint-stage/route authorization.
        bool zeroStageSourceRoutes = false; // NEW internal initial-only experiment, never a default/UI switch.
        const Controller* routingController = nullptr; // Same canonical owner bound BEFORE first graph.
    };
    enum class InitialSourceTransportStatus
    {
        none, configured, pending, ready, invalidOwner, invalidProject, invalidBudget,
        unsupported, preparationFailed, sourceFault, readbackUnavailable
    };
    struct InitialSourceTransportOutcome
    {
        InitialSourceTransportStatus status = InitialSourceTransportStatus::none;
        juce::String message;
    };
    struct InitialSourceTransportReadback
    {
        InitialSourceTransportStatus status = InitialSourceTransportStatus::none;
        int quantum = 0, packets = 0, transportSamples = 0, physicalFrames = 0, maximumSourceFrames = 0;
        std::size_t memberCount = 0;
        std::uint64_t deviceEpoch = 0, sessionGeneration = 0, membershipGeneration = 0;
        bool nativeKnown = false, membershipKnown = false, pairedClosed = false;
        bool physicalKnown = false;
        transport::BurstStatus physicalStatus = transport::BurstStatus::telemetryUnavailable;
        te::SourceBurstSession::Outcome native;
        source_membership::Receipt membership;
        bool fixedEpochConfigured=false,fixedEpochHealthy=true; // Source health, never graph/worker completion.
        bool sourceExchangeFaulted=false;
        std::uint64_t completedExchangeGeneration=0; // Actual final root completion, not posted/selected.
    };
    InitialSourceTransportOutcome prepareInitialSourceTransport(const Project&, InitialSourceTransportOptions);
    InitialSourceTransportReadback initialSourceTransportReadback() const;
    struct InitialSourceEpochObservation
    {
        Id plugin=0;const void* runtime=nullptr;
        FixedSourcePcmEpochGuard::Expected expected;
        std::optional<initial_source_epoch::Fence::Failure> failure;
    };
    std::vector<InitialSourceEpochObservation> initialSourceEpochObservations() const;
    struct InitialAudioPortReadback
    {
        bool configured=false,terminalFault=false;
        std::size_t owners=0,runtimeOwners=0,nativeEdges=0,tracks=0;
        std::uint64_t builds=0,portsPrepared=0,audioBlocks=0,nonemptyAudioMidi=0;
        std::uint64_t nativeOn=0,nativeOff=0,nativePanic=0;
        unsigned nativeHeld=0;
    };
    InitialAudioPortReadback initialAudioPortReadback() const; // CONTROL-only experimental evidence.
    struct InitialZeroQueueObservation
    {
        juce::uint64 nativeOwner = 0, build = 0;
        const void* processor = nullptr; // Latest PREPARED queue identity only; no owning handle/PCM.
        bool certified = false, createdHere = false;
        bool preparationObserved = false, memorySharing = false, outputZero = false;
        juce::String proofTrace; // Latest CONTROL preparation only; no DSP writes/I/O.
    };
    std::vector<InitialZeroQueueObservation> initialAudioZeroQueues() const;
    struct InitialMixerTrackMidiObservation
    {
        juce::uint64 nativeId=0;int mixer=-1;const void* scope=nullptr;const void* runtime=nullptr;
        unsigned readers=0;bool enabled=false,registered=false;
        std::uint32_t ingressFault=0,terminalFault=0;
        std::uint64_t accepted=0,delivered=0,on=0,off=0,panics=0,panicFlags=0;unsigned held=0;
    };
    std::vector<InitialMixerTrackMidiObservation> initialMixerTrackMidiObservations() const;
    // Scoped CONTROL fixture only, valid solely during driver. Does not expose
    // a mutable sampler/runtime or grant arbitrary graph/plugin publication.
    void withInitialSourceTransportContext(const std::function<void(te::Edit&, te::EditPlaybackContext&)>&);
    // Internal original-fixture driver only; no product command or transaction.
    void withRetainedContextFixture(const PluginSpec &, const std::function<void(PluginHost &)> &,
                                    const std::function<void(RetainedContextFixture &)> &);
    // Closed same-Edit external-instrument experiment only; no product operation.
    void
    withRetainedInstrumentContextFixture(Id, const std::function<void(RetainedInstrumentContextFixture &)> &);
    // Closed native Wave decoder/routing experiment only; no product operation.
    void withRetainedWaveContextFixture(Id, const std::function<void(RetainedWaveContextFixture &)> &);
    // Internal CONTROL transaction. UI/MCP/history share their existing canonical
    // Controller preparation; this API prepares only its exact audio destination.
    enum class ChannelRoutingStatus
    {
        none,
        ready,
        noOp,
        posted,
        selected,
        readbackUnavailable,
        unavailable,
        busy,
        invalidProject,
        unsupportedDelta,
        unsupportedCompensation,
        serialExhausted,
        staleProjection,
        staleContext,
        ownerGone,
        deviceChanged,
        workerChanged,
        preparationFailed,
        graphRefused
    };
    struct ChannelRoutingOutcome
    {
        ChannelRoutingStatus status = ChannelRoutingStatus::none;
        juce::String message;
    };
    struct ChannelRoutingReadback
    {
        ChannelRoutingStatus status = ChannelRoutingStatus::none;
        bool overlayActive = false, nativeSelected = false;
        std::uint64_t generation = 0, postedSerial = 0, selectedSerial = 0, nativePostedSerial = 0;
        std::int64_t referenceStart = 0, referenceEnd = 0;
    };
    class PreparedChannelRouting
    {
      public:
        ~PreparedChannelRouting(); // CONTROL only; may retire a private native graph.
        PreparedChannelRouting(const PreparedChannelRouting &) = delete;
        PreparedChannelRouting &operator=(const PreparedChannelRouting &) = delete;
        bool wasPosted() const noexcept;
        std::uint64_t publicationSerial() const noexcept; // Exact prepared identity, not acceptance.

      private:
        friend class AudioEngine;
        PreparedChannelRouting();
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
    ChannelRoutingOutcome ensureChannelRoutingContext(const Project &, bool patternMode, Id pattern);
    ChannelRoutingOutcome prepareChannelRouting(const Project &, std::unique_ptr<PreparedChannelRouting> &);
    // Explicit engineering entry ONLY. The SAME real canonical ticket is checked
    // before/after private prep and again immediately before post+canonical commit.
    ChannelRoutingOutcome prepareInitialSourceChannelRouting(Controller&, PreparedControllerChange&,
        std::unique_ptr<PreparedChannelRouting>&);
    ChannelRoutingOutcome validateInitialSourceChannelRouting(Controller&, PreparedControllerChange&,
        PreparedChannelRouting&) const;
    // Internal stopped-only effect append/removal prerequisite. No model/history
    // mutation. capturedSource must contain acknowledged current native state.
    // New workers are private, single-use and already restored off DSP.
    // Reuses the existing exact validate/post/selected receipt, never Stop
    // as callback exclusion. App glue must keep a sole CONTROL lease through
    // Controller validation + graph post + canonical no-throw commit.
    ChannelRoutingOutcome prepareStoppedEffectChange(const Project &capturedSource,
        const Project &destination, int insert, const StoppedEffectWorkers::Ptr &,
        std::unique_ptr<PreparedChannelRouting> &);
    // Actual application overload: newer captured removal state is admitted
    // only if owned by this SAME validated canonical history ticket. Incumbent
    // projection metadata remains unchanged on cancellation/refusal.
    ChannelRoutingOutcome prepareStoppedEffectChange(Controller &, PreparedControllerChange &,
                                                     const Project &capturedSource, int insert,
                                                     const StoppedEffectWorkers::Ptr &,
                                                     std::unique_ptr<PreparedChannelRouting> &);
    // CONTROL admission for asynchronous private worker startup. Returns the
    // actual current session plan; success is not a graph/membership commit.
    juce::Result stoppedEffectWorkerTransport(PreparedPluginTransport::Ptr &) const;
    // CONTROL-only pre-post membership is asynchronous. These internal operations
    // do not mutate history; only the same Controller ticket may publish/commit.
    enum class StoppedEffectPhase
    {
        unprepared,
        prepared,
        pending,
        expanded,
        cancelling,
        cancelled,
        retiring,
        completed,
        failed
    };
    struct StoppedEffectReadback
    {
        StoppedEffectPhase phase = StoppedEffectPhase::unprepared;
        const char *error = nullptr; // Stable static error ID, not an ephemeral message.
        std::uint64_t operation = 0, membership = 0, physicalSerial = 0;
        bool graphPosted = false, graphRetired = false;
    };
    juce::Result prepareStoppedEffectMembership(Controller &, PreparedControllerChange &,
                                                PreparedChannelRouting &);
    juce::Result requestStoppedEffectMembership(Controller &, PreparedControllerChange &,
                                                PreparedChannelRouting &);
    juce::Result cancelStoppedEffectMembership(PreparedChannelRouting &);
    StoppedEffectReadback stoppedEffectMembershipReadback(PreparedChannelRouting &);
    ChannelRoutingOutcome validateStoppedEffectChange(Controller &, PreparedControllerChange &,
                                                      PreparedChannelRouting &) const;
    ChannelRoutingOutcome validateChannelRouting(PreparedChannelRouting &) const;
    // Final fallible audio+Controller admission must just have passed under one
    // sole CONTROL/no-reentry lease. Returns posted, NEVER inferred completion.
    ChannelRoutingStatus postChannelRouting(PreparedChannelRouting &) noexcept;
    ChannelRoutingReadback channelRoutingReadback() const noexcept;
    // CONTROL observes actual selection and latches that historical witness
    // before retiring the returned holder; newer queued work is distinct.
    ChannelRoutingReadback observeChannelRoutingSelection();
    // Explicit existing full Edit replacement for unsupported structural edits
    // or genuine context/device restoration. No automatic route-Stop fallback.
    juce::Result rebuildFullProjection(const Project &, bool patternMode, Id selectedPattern);
    // Owned structural interruption: pending preparation never commits. The
    // caller retains both tickets and delivers notifications after readback.
    juce::Result rebuildFullProjection(const Project &, bool patternMode, Id selectedPattern, Controller &,
                                       PreparedControllerChange &,
                                       const std::function<juce::Result()> &finalAdmission = {});
    // Exact owned unpublished worker preparation only. Drained means startup
    // exited and private workers are handed to CONTROL retirement, not kernel exit.
    juce::Result drainFullProjectionPreparation(const Project &exactDestination, bool requestCancel,
                                                bool &drained);
    // CONTROL-only explicit genuine-device recovery; success schedules a retry,
    // never claims resumed/adopted playback. Full projection must already exist.
    juce::Result retryDevicePreparation();
    // CONTROL-only offline routing admission. Initial no-device sessions only;
    // a retained context, device transition or native/private owner refuses.
    bool canDeferChannelRouting() const noexcept;
    // Just validated on sole CONTROL lease. Retires stopped/context-free native
    // owners, retaining worker/sample banks. Never called from DSP/commit swap.
    struct PreparedDeferredRouting
    {
        std::map<Id, std::shared_ptr<const Sample>> samples; // Immutable PCM only; no native PluginPtr.
        std::map<Id, juce::String> sampleSources; // Provenance travels with this exact PCM bank.
        // Only the owned no-device full-projection path fills these CONTROL
        // resources. Ordinary routing/New/Open callers retain existing behavior.
        std::map<Id, std::shared_ptr<PluginHost>> nativeHosts;
        std::map<Id, juce::String> nativeErrors;
        std::vector<std::shared_ptr<PluginHost>> nativeOwners;
        Project nativeProject;
        double nativeRate = 0;
        int nativeBlock = 0;
    };
    juce::Result prepareDeferredRouting(const Project&, std::unique_ptr<PreparedDeferredRouting>&);
    void retireNoDeviceProjection(PreparedDeferredRouting&) noexcept;
    // Initial context-free New/Open only. Destination metadata and PCM are
    // prepared before the Controller swap; no old-session Host remains eligible.
    void retireDeferredProjectReplacement(PreparedDeferredRouting &, Project &preparedMetadata) noexcept;
    bool documentProjectionDeferred() const noexcept { return deferredDocumentProjection; }
    bool pluginStateCapturePending() const noexcept { return bool(capture); }
    bool deferredWorkerPreparationIdle() const noexcept { return !preparation[0]; }
    std::optional<unsigned> capturedPluginRevision(Id, const std::shared_ptr<PluginHost>&) const noexcept;
    // CONTROL initial-startup owner only. Final admission revalidates captured
    // source after candidate construction; exceptions restore no-Edit ownership.
    juce::Result rebuildDeferredDeviceProjection(const Project&, bool, Id,
        const std::function<juce::Result(bool candidateReady)>& finalAdmission);
    bool drainDeferredWorkerPreparation(bool requestCancel);
    void playStop(int countInBars = 0);
    void setMetronome(bool);
    bool metronome() const
    {
        return guide->enabled.load();
    }
    void setGuideSound(int);
    int guideSound() const
    {
        return guide->sound.load();
    }
    void setGuideGain(float);
    float guideGain() const
    {
        return guide->gain.load();
    }
    void setCountInBars(int);
    int countInBars() const
    {
        return guideBars;
    }
    double countInSeconds(int bars) const;
    void cancelCountIn();
    int preferredCountInBars() const
    {
        return lastGuideBars;
    }
    double countInRemaining() const
    {
        return guide->remaining.load() * tempo / 60;
    }
    std::uint64_t guideHits() const
    {
        return guide->hits.load();
    }
    void stop();
    void seekBeat(double);
    void setLooping(bool);
    bool looping() const;
    // Song mode loops the Playlist time selection (beats) when one is set, else the whole Song.
    void setSongLoopBeats(std::optional<std::pair<double, double>>);
    std::optional<std::pair<double, double>> songLoopBeats() const
    {
        return songLoop;
    }
    // Active transport loop range in edit seconds; nullopt when not looping.
    std::optional<std::pair<double, double>> loopSeconds() const;
    bool playing() const;
    double positionBeat() const;
    juce::String deviceDescription() const;
    std::unique_ptr<te::Edit> buildEdit(const Project &, bool patternMode, Id selectedPattern,
                                        te::Edit::EditRole, juce::String &error,
                                        std::shared_ptr<ProductionPluginProjection> * = nullptr);
    juce::Result renderSync(const Project &, const juce::File &, double rate = 48000, int bits = 24,
                            int block = 512);
    // Offline WAV render: 44100/48000 Hz, 16/24-bit integer or 32-bit float.
    // tail: 0 cut at the Song end, 1 leave the remainder (effect tails until silence, at most
    // 8 s), 2 wrap the remainder onto the start (seamless loops).
    juce::Result startRender(const Project &, const juce::File &, double rate = 48000, int bits = 24, int tail = 0);
    bool rendering() const;
    float renderProgress() const;
    juce::Result finishRender();
    // Asks the running offline render to stop at its next block; finishRender then reports
    // a failed (uncommitted) render and the previous output file is untouched.
    void cancelRender();
    static bool preparationPending(const juce::Result &result)
    {
        return result.failed() &&
               (result.getErrorMessage() == "Preparing VST3 workers..." ||
                result.getErrorMessage() == "Preparing native device..." ||
                result.getErrorMessage() == "Preparing time-stretched sample..." ||
                result.getErrorMessage().startsWith("Preparing native automation metadata for "));
    }
    // Renderer/control thread only. This is the shared atomic output commit boundary.
    static juce::Result commitRenderedFile(juce::TemporaryFile &,
                                           std::span<const std::shared_ptr<PluginHost>>, bool completed,
                                           const juce::String &taskError,
                                           bool samplerAutomationFailed = false);
    juce::Result audition(const juce::String &);
    void requestAudition(const juce::String &source, bool playToEnd = false)
    {
        auditionSample.reset();
        auditionWaveform = {};
        auditionToEnd = playToEnd;
        previewLoader.request(source);
    }
    juce::Result pollAudition();
    void stopAudition()
    {
        previewLoader.cancel();
        preview.stop();
    }
    const PreviewWaveform &auditionInfo() const
    {
        return auditionWaveform;
    }
    bool canReplayAudition() const
    {
        return bool(auditionSample);
    }
    juce::Result seekAudition(float fraction);
    std::uint64_t auditionRequestId() const
    {
        return auditionSerial;
    } // control thread only
    float auditionPosition() const
    {
        return preview.playingSerial() == auditionSerial ? preview.positionFraction() : 0.f;
    }
    void setAuditionVolume(float gain)
    {
        preview.setVolume(gain);
    }
    float auditionVolume() const
    {
        return preview.getVolume();
    }
    void setFullAudition(bool full)
    {
        preview.setFull(full);
    }
    bool fullAudition() const
    {
        return preview.getFull();
    }
    bool auditioning() const
    {
        return preview.isActive();
    }
    void openPluginEditor(Id, bool open = true);
    std::shared_ptr<PluginHost> parameterHost(Id) const;   // Control-thread only; retains worker ownership.
    bool pluginProjectionReady(const Project &, Id) const; // Includes a current missing-plugin placeholder.
    std::shared_ptr<PluginHost> preparedPluginHost(const Project &, Id) const;
    juce::String pluginStatus(Id) const;
    juce::Result requestPluginRecovery(const Project &, Id); // Explicit structural replacement, control only.
    // Restarts a running plugin from the project's state for it (e.g. a changed impulse file);
    // the caller has refreshed every plugin's captured state first.
    juce::Result requestPluginStateReload(const Project &, Id);
    void invalidateProjection()
    {
        hasProjection = false;
        deferredHostProjection = deferredSampleProjection = false;
        deferredSamples.clear();
        deferredSampleSources.clear();
        recoveryPlugin.reset();
        recoveryStates.clear();
    }
    juce::Result requestPluginState(const Project &, const std::vector<Id> *only = nullptr,
                                    bool reuseUnchanged = false);
    bool pollPluginState(Project &, juce::Result &);
    void reclaimPluginHosts();
    // Control-thread lifetime diagnostic; includes asynchronously retiring workers.
    std::size_t retainedPluginHostCount() const noexcept
    {
        return ownedHosts.size();
    }
    // Bounded internal experiment; no production/user opt-in until complete
    // history/adoption/MIDI/memory admission qualification. Call only while stopped.
    juce::Result configureCompensationHistory(std::uint32_t maximumDelay, std::size_t graphBudget,
                                              Id phaseSource = 0, Id phaseWet = 0);
    juce::Result holdCompensationGraphPublicationForTest(bool); // Internal deterministic risk test only.
    CompensationGraphObservation compensationGraphObservation() const;
    juce::String compensationGraphFailure() const; // Control-thread diagnostic for the internal experiment.
    bool consumePluginChanges();
    void markCapturedPluginStateSaved();
    bool pluginDeviceConfigurationChanged() const;
    bool deviceRestorationRequired() const;
    bool hasAudioDevice() const; // An open output device (live graph), not offline-only.
    // CONTROL coordinator: exact capture, retire old context, actual native retry,
    // then build/arm replacement. Pending means preparation, never audio completion.
    juce::Result prepareDeviceRestoration(const Project &captured);
    // CONTROL only: exact live transport identity, including same-format device epochs.
    bool pluginHostConfigurationCurrent(const PluginSpec &, const std::shared_ptr<PluginHost> &, double rate,
                                        int block) const;
    struct ChannelDiagnostic
    {
        Id channel = 0;
        std::uintptr_t track = 0, sampler = 0, sample = 0;
        int midiNotes = 0; // Historical field name: scheduled score-note count, not held MIDI.
        std::uint64_t graphInitialisations = 0;
        std::uint64_t timelineMisses = 0, renderCalls = 0;
        double lastTime = 0;
        int lastLeadSkip = 0;
        bool lastPlaying = false;
        int heldInputKeys = 0; // Callback-published MIDI+typing keys, including pedal-held keys.
        std::uint64_t samplerAutomationFailures = 0;
        unsigned samplerAutomationFailureReason = 0;
    };
    struct ProjectionDiagnostics
    {
        std::uintptr_t edit = 0, playbackContext = 0;
        std::uint64_t replacements = 0, updates = 0, samplerCreations = 0, midiUpdates = 0;
        bool headPlaying = false, transportPlaying = false, playPending = false;
        double headSeconds = 0;
        bool loopEnabled = false, headLoopEnabled = false, headLoopAvailable = false;
        double loopStartBeat = 0, loopEndBeat = 0, headLoopStartBeat = 0, headLoopEndBeat = 0;
        juce::String pluginSourceState = "notRequired";
        int pluginTransportPackets = 0, maximumPhysicalFrames = 0, maximumSourceFrames = 0;
        std::uint64_t pluginDeviceEpoch = 0;
        SampleCache::Statistics cache;
        std::vector<ChannelDiagnostic> channels;
    };
    ProjectionDiagnostics projectionDiagnostics() const;
    std::optional<float> mixerDisplayGain(int insert) const;
    MixerMeterReading mixerMeter(int insert) const;
    juce::Result clearMixerOverload(int insert, std::uint64_t &clearedThroughBlock);
    std::vector<std::pair<int, std::uint64_t>> mixerGraphInitialisations() const;
    struct PluginDiagnostic
    {
        Id id;
        int latency;
        PluginHost::Statistics packets;
        bool alive;
        juce::uint32 processId = 0;
        PluginHost::AutomationStatistics automation;
    };
    std::vector<PluginDiagnostic> pluginDiagnostics() const;
    std::optional<bool> mixerSendAutomationHealthy() const;      // CONTROL readback; null without an Edit.
    std::vector<PluginRuntimeState> pluginRuntimeStates() const; // Control-thread; not model context.

  private:
    std::unique_ptr<ChannelRoutingState> channelRouting;
    void serviceChannelRouting();
    bool channelRoutingPostPending() const noexcept;
    void releaseChannelRoutingForEdit() noexcept;
    static bool beforeDeviceReprepare(void *) noexcept;
    te::Plugin *effectiveMixerGain(int) const noexcept;
    void retireUnusedPluginHosts();
    std::unique_ptr<te::Edit> edit;
    std::unique_ptr<CompensationGraphBinding> compensationGraph;
    Id midiTarget = 0;
    std::map<Id, int> observedPluginLatencies;
    std::map<Id, int> refusedCompensationLatencies; // No unchanged expensive retry after explicit refusal.
    SamplePreview preview;
    PreviewLoader previewLoader;
    std::shared_ptr<const Sample> auditionSample;
    PreviewWaveform auditionWaveform;
    std::uint64_t auditionSerial = 0;
    bool auditionToEnd = false;
    class RenderWorker;
    std::unique_ptr<RenderWorker> worker;
    std::unique_ptr<te::Edit> renderEdit;
    double tempo = 120;
    TempoMap tempoMap; // Projected Song map (base tempo in Pattern mode); CONTROL thread only.
    std::shared_ptr<GuideControl> guide = std::make_shared<GuideControl>();
    int guideBars = 0;
    int lastGuideBars = 1;
    bool persistentGuide = false;
    juce::File guideFile;
    void saveGuide();
    SampleCache samples;
    std::shared_ptr<LiveTimeline> timeline;
    std::uint64_t timelineGeneration = 0;
    double renderRate = 48000;
    int renderBlock = 512;
    std::vector<std::shared_ptr<PluginHost>> ownedHosts;
    std::vector<std::shared_ptr<const Sample>> ownedSamples;
    std::vector<std::shared_ptr<LiveTimeline>> ownedTimelines;
    std::map<Id, std::shared_ptr<PluginHost>> currentHosts;
    std::map<Id, te::Plugin::Ptr> effectPlugins;
    std::map<Id, juce::String> pluginErrors;
    std::optional<PluginSpec> recoveryPlugin;
    std::map<Id, PluginSpec> recoveryStates; // Failed retry placeholders retain their captured state.
    struct Revision
    {
        std::shared_ptr<PluginHost> host;
        unsigned value = 0;
    };
    std::map<Id, Revision> observedParameterRevisions, lastCapturedRevisions;
    struct StateCapture
    {
        Project project;
        struct Pending
        {
            Id id;
            std::shared_ptr<PluginHost> host;
            unsigned serial;
        };
        std::vector<Pending> pending;
        std::map<Id, Revision> revisions;
        juce::uint32 deadline = 0;
        bool refusedAdmission = false;
    };
    std::unique_ptr<StateCapture> capture;
    class PluginPreparation;
    std::array<std::unique_ptr<PluginPreparation>, 2> preparation;
    std::vector<std::unique_ptr<PluginPreparation>> retiredPreparation;
    juce::Result preparePlugins(const Project &, bool offline);
    bool preparingDeferredNativeAccess = false; // Scoped CONTROL only; never read by DSP.
    juce::Result prepareDeferredNativeHosts(const Project &, PreparedDeferredRouting &);
    void publishDeferredNativeHosts(PreparedDeferredRouting &) noexcept;
    juce::Result validateInitialSourceProject(const Project&) const;
    juce::Result installInitialSourceTransport(te::Edit&, const Project&);
    double projectedRate = 0;
    int projectedBlock = 0;
    struct ProjectedChannel
    {
        te::AudioTrack *track = nullptr;
        te::MidiClip *clip = nullptr;
        te::Plugin::Ptr sampler;
        std::shared_ptr<const Sample> sample;
        std::vector<ScheduledNote> notes;
    };
    std::map<Id, ProjectedChannel> projectedChannels;
    std::map<Id, juce::ReferenceCountedObjectPtr<te::WaveAudioClip>> projectedAudioClips;
    std::map<int, te::AudioTrack *> projectedInserts;
    Project projectedProject;
    bool projectedPatternMode = true, hasProjection = false;
    static_assert(std::atomic<bool>::is_always_lock_free);
    std::atomic<bool> deviceEverActive{false};
    bool deferredDocumentProjection = false, deferredHostProjection = false, deferredSampleProjection = false;
    const std::function<juce::Result(bool candidateReady)>* deferredDeviceFinalAdmission = nullptr; // CONTROL call scope only.
    bool deferredWorkerOwner = false;
    std::map<Id, std::shared_ptr<const Sample>> deferredSamples;
    std::map<Id, juce::String> deferredSampleSources;
    double deferredPositionBeat = 0;
    bool deferredLooping = false;
    std::optional<std::pair<double, double>> songLoop;
    te::TimeRange loopRange(const Project &, bool patternMode, double duration) const;
    Id projectedPattern = 0;
    std::uint64_t replacements = 0, updates = 0, samplerCreations = 0, midiUpdates = 0;
    ChannelRoutingOutcome prepareGraphProjection(const Project &, const StoppedEffectDelta *,
        std::unique_ptr<PreparedChannelRouting> &, std::shared_ptr<InitialSourceRoutePermit> = {});
    ChannelRoutingOutcome prepareStoppedEffectChangeImpl(const Project &, const Project &, int,
                                                         const StoppedEffectWorkers::Ptr &,
                                                         std::unique_ptr<PreparedChannelRouting> &,
                                                         bool historyWitness);
    bool stoppedEffectHistoryWitnessCurrent(Controller &, PreparedControllerChange &,
                                            const PreparedChannelRouting &) const;
    ChannelRoutingOutcome validateGraphProjection(PreparedChannelRouting &, bool initialSourceAdmission,
                                                  bool privateEffectPreparation = false) const;
    juce::Result validateInitialSourceRoute(const InitialSourceRoutePermit&) const;
    std::shared_ptr<void> leaseInitialSourceRoute(
        InitialSourceRoutePermit&, std::shared_ptr<const te::GraphOutputRouting>);
    void publishInitialSourceRoute(InitialSourceRoutePermit&) noexcept;
    bool initialSourceExchangeFaulted() const noexcept;
    std::uint64_t initialSourceCompletedExchangeGeneration() const noexcept;
    struct ProjectionBinding;
    std::unique_ptr<ProjectionBinding> prepareProjectionBinding(te::Edit &, const Project &, bool, Id,
                                                                std::map<int, te::AudioTrack *> &&);
    void bindProjection(ProjectionBinding &) noexcept;
    juce::Result updateAudioPlacements(const Project &, bool);
    void updateMixerRoutes(const Project &);
    void publishLoopBounds(); // Message thread only; no graph/device/transport restart.
};
} // namespace vibera
