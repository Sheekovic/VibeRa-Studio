// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PluginProtocol.h"
#include "PreparedPluginTransport.h"
#include "PluginParameters.h"
#include "PluginBinding.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <memory>
#include <functional>
#include <array>
#include <span>
#include <optional>

namespace vibera
{
class PluginDryHistory;
struct PluginDryHistoryHostFixture;
// Persisted AUDIO dependency, not a native bus/worker capability. The application
// validates the complete Project and negotiates the requested layout privately.
struct PluginDetectorAssignment
{
    int sourceMixer = -1, inputBus = 1, channels = 2;
    constexpr bool valid() const noexcept
    {
        return sourceMixer > 0 && inputBus >= 1 && inputBus <= 31 && (channels == 1 || channels == 2);
    }
    bool operator==(const PluginDetectorAssignment &) const = default;
};
// Descriptor/state are portable project data; workers and handles never enter the model.
struct PluginSpec
{
    std::int64_t id = 0;
    juce::String name, descriptor, state;
    bool instrument = false, bypass = false;
    std::vector<PluginBinding> automationBindings;
    float wet = 1; // Host effect mix, not a native parameter; instruments require1.
    std::optional<PluginDetectorAssignment> detector; // Effects only; absent preserves old stereo layout.
    int mainInputBus = 0, mainOutputBus = 0, mainInputChannels = 2, mainInputMonoSource = 0;
    // Multi-output instruments: Mixer insert (0 Master) for each extra stereo output bus, in the
    // plugin's ascending bus order after the main one. Empty keeps the single main output.
    std::vector<int> extraOutputMixers;
    bool operator==(const PluginSpec &) const = default;
};
// Requires an independently validated Project. Invalid bus/channel declarations
// remain invalid requests, never silently converted into a disconnected layout.
inline PluginBusRequest pluginBusRequestFor(const PluginSpec &spec) noexcept
{
    PluginBusRequest request;
    if (spec.detector)
    {
        request.auxiliaryInputBus = spec.detector->inputBus;
        request.auxiliaryChannels = spec.detector->channels;
    }
    request.mainInputBus = spec.mainInputBus;
    request.mainOutputBus = spec.mainOutputBus;
    request.mainInputChannels = spec.mainInputChannels;
    request.mainInputMonoSource = spec.mainInputMonoSource;
    request.extraOutputBuses = int(spec.extraOutputMixers.size());
    return request;
}
// Control-thread snapshot only. Identity is independent of worker IPC/pairing tokens.
struct PluginRuntimeState
{
    std::int64_t plugin = 0;
    juce::String instance;
    unsigned parameterRevision = 0;
    bool available = false;
    bool operator==(const PluginRuntimeState &) const = default;
};
// Caller-prepared, SINGLE PROCESSING OWNER capture. No Host-owned mutable
// snapshot, dynamic storage, callback string conversion or owning handle copies.
// CONTROL reads only after callback exclusion / explicit caller-owned transfer.
// Tokens are runtime diagnostics, not persistent model/MCP IDs. UUID bytes keep
// copied values distinguishable after an address is reused; no token is a handle.
struct PluginPcmIdentity
{
    std::array<std::uint8_t, 16> instance{};
    std::uintptr_t host = 0, preparedTransport = 0;
    int quantum = 0, transportSamples = 0;
    bool operator==(const PluginPcmIdentity &) const = default;
};
enum class PluginPcmReceiptStatus
{
    empty,
    complete, // All emitted spans described; NOT worker/playback/PDC success.
    invalidSpan,
    capacityExceeded,
    invalidMix,
    workerUnavailable,
    transportRefused,
    identityMismatch,
    invalidStorage,
    historyDetached // Explicit noncompletion; the separate continuation owns the actual dry ring.
};
enum class PluginPcmPacketKind
{
    startup,
    completed,
    missing,
    invalidLatency
};
enum class PluginPcmMixKind
{
    wet,
    delayedDry,
    mixed
};
struct PluginPcmSegment
{
    int offset = 0, count = 0, packetFrame = 0; // Relative to THIS process call.
    std::uint64_t outputPacket = 0;             // Processing-owner logical packet, NOT graph/reference time.
    PluginPcmPacketKind packet = PluginPcmPacketKind::startup;
    // Present ONLY for the valid completed wet packet backing this span.
    // Missing/startup never reuses a stale last-consumed scalar receipt.
    std::optional<PluginConsumedLatency> consumed;
    int dryDelaySamples = 0;
    bool forcedDry = false;
    PluginPcmMixKind mix = PluginPcmMixKind::wet;
    // Exact coefficients of first/last emitted sample. Interior ramps may reach
    // their endpoint within a segment: endpoints are not a linear interpolation contract.
    float firstWet = 1, lastWet = 1, firstBypass = 0, lastBypass = 0;
};
struct PluginPcmCapture
{
    std::span<PluginPcmSegment> storage;
    PluginPcmIdentity identity;
    PluginPcmReceiptStatus status = PluginPcmReceiptStatus::empty;
    std::size_t count = 0, required = 0;
    // Optional immutable caller expectation. identity above is always output;
    // reusing a receipt never silently turns its prior output into authority.
    std::optional<PluginPcmIdentity> expectedIdentity;
    explicit PluginPcmCapture(std::span<PluginPcmSegment> prepared,
                              std::optional<PluginPcmIdentity> expected = std::nullopt) noexcept
        : storage(prepared), expectedIdentity(expected)
    {
    }
    std::span<const PluginPcmSegment> segments() const noexcept
    {
        return storage.first(count);
    }
};
struct PluginPcmPreflight
{
    // Readiness only, never a completed PCM receipt. No processing state changes.
    bool admitted = false;
    PluginPcmReceiptStatus status = PluginPcmReceiptStatus::empty;
    PluginPcmIdentity identity;
    std::size_t required = 0;
    PluginTransportOutcome transport;
};
class PluginHost
{
    struct Impl;
    std::unique_ptr<Impl> impl;
    friend class AudioEngine;
    friend struct PluginDryHistoryHostFixture; // Original SDK retention acceptance only.
    // CONTROL pin only: never inspects the live ring/cursor/witness. The actual
    // selected-root authority must validate native/source continuity on DSP
    // before calling claim. This method alone grants NO graph/membership permit.
    struct DryHistoryPreparation
    {
        std::shared_ptr<PluginDryHistory> history;
        PluginPcmIdentity identity;
        const void *writer = nullptr;
    };
    std::optional<DryHistoryPreparation> prepareDryHistoryRetention() const;
  public:
    // Multi-output instruments: extra stereo output buses this host transfers, and the samples of
    // the latest processed callback block for one of them (indexed like that callback's buffer).
    // Read only by an output tap that the graph orders after this host's track.
    int extraOutputBuses() const noexcept;
    const float *extraOutput(int bus, int channel) const noexcept;

  private:
    // AudioEngine CONTROL only, with NO Edit/graph/process reader able to use
    // this Host. Fresh control-only startup may finalize its transport ONCE;
    // after any admitted source sample the policy is permanently immutable.
    // No worker restart/state load/editor close, and no mutation on refusal.
    enum class FreshTransportPromotion
    {
        promoted,
        wrongThread,
        invalidPlan,
        workerUnavailable,
        configurationMismatch,
        notFresh,
        unsupportedLatency,
        sourceRefused
    };
    FreshTransportPromotion promoteFreshPreparedTransport(const PluginSpec &, double rate, int block,
                                                          const PreparedPluginTransport::Ptr &,
                                                          PluginBusRequest = {}) noexcept;
    bool refreshParameterCatalogue(); // Sole shared-mailbox consumer; control thread only.
    void serviceParameterRequests();  // Sole control-reply consumer, retains token-owned receipts.
    void serviceEditorRequests();     // Independent native-window mailbox; control thread only.

  public:
    // Internal prepared experiment; default preserves the existing1Q policy.
    // No live policy change, format enablement, worker/IPC replacement or UI setting.
    enum class TransportPolicy
    {
        preparedBurst = 0, // Only through the validated plan constructor; never an integer packet cast.
        oneQuantum = 1,
        twoQuanta = 2
    };
    explicit PluginHost(TransportPolicy = TransportPolicy::oneQuantum);
    // CONTROL only. Null/unvalidated plans cannot manufacture a new policy.
    explicit PluginHost(PreparedPluginTransport::Ptr);
    TransportPolicy transportPolicy() const noexcept;
    int transportPacketCount() const noexcept; // Finalized before DSP; exact K,2..8 for a plan.
    // SINGLE PROCESSING OWNER only, exactly once around ALL of this Host's slices
    // within an actual outer callback. sourceFrameUpperBound is the validated
    // total source quota, not the next slice. Fewer/zero processed frames is legal
    // for muted/pruned nodes. No worker wait; success means admission/scope close,
    // not DSP completion, graph adoption or audible recovery. Application MUST
    // guard total source bounds BEFORE processing any native graph node.
    PluginTransportOutcome beginPhysicalBurst(int sourceFrameUpperBound) noexcept;
    PluginTransportOutcome endPhysicalBurst() noexcept;
    // Same SINGLE processing owner; CONTROL may call only after callback exclusion.
    // Always closes only scope metadata, including after failed/partial begin/end.
    // Idempotent; preserves the first fault, last admitted outer serial, packets,
    // MIDI/parameters, cursor and DSP ramps. NOT recovery, completion or reset.
    void abortPhysicalBurst() noexcept;
    PluginTransportOutcome transportOutcome() const noexcept; // Coherent latched scalar fault, CONTROL-safe.
    ~PluginHost(); // retained/reclaimed on control thread, never in audio callback
    // Preparation owner may cancel startup; predicate is never called from DSP.
    juce::Result start(const PluginSpec &, double rate, int block, bool offline,
                       const std::function<bool()> &cancelled = {}, PluginBusRequest = {});
    PluginBusRequest busLayout() const noexcept; // Immutable actual layout after successful startup.
    bool readBusCatalogue(PluginBusCatalogue &) const; // CONTROL only; false preserves output.
    int bypassTransitionSamples() const noexcept;
    std::optional<float>
    bypassBlend() const noexcept; // null before processing;0 wet,1 delayed dry/silent synth.
    std::optional<float> wetBlend() const noexcept; // Actual host wet ramp, null before processing.
    // Compatibility fire-and-forget entrypoint. New UI/MCP callers must retain
    // requestEditor ownership and observe its real result instead of assuming completion.
    void editor(bool open);
    // Tokens are the pair(runtimeIdentity,serial); never apply another instance's serial.
    PluginEditorAdmission requestEditor(const juce::String &instance, PluginEditorAction,
                                        std::uint64_t wrapperSelection = 0);
    bool editorResultReady(const juce::String &instance, unsigned request);
    bool readEditorResult(const juce::String &instance, unsigned request, PluginEditorResult &);
    PluginEditorCancellation cancelEditorRequest(const juce::String &instance, unsigned request);
    void abandonEditorRequest(const juce::String &instance, unsigned request);
    unsigned pendingEditorRequests() const noexcept;
    bool editorBusy() const; // Physical nonterminal work, never retained-receipt count.
    PluginEditorWindowState editorWindowState() const; // Current observation, not foreground proof.
    // Worker-GUI proxy; application CONTROL only. Never called by DSP.
    bool takeWrapperRequest(PluginWrapperRequest &);
    bool readWrapperDesired(unsigned serial, float &, bool &gestureOpen) const;
    bool completeWrapperRequest(unsigned serial, PluginWrapperOutcome, const juce::String &operation,
                                const juce::String &code, const juce::String &message);
    void publishWrapperState(bool ready, bool bypassed, float mix, std::uint64_t lastTweakedSelection = 0);
    void publishWrapperChannel(const PluginWrapperChannel &channel);
    PluginWrapperState wrapperState() const;
    std::uint64_t wrapperSelection() const;
    PluginNativeMeterReading nativeOutputMeter() const noexcept; // CONTROL telemetry; no audio freshness claim.
    void resetNotes() noexcept;
    bool offline() const noexcept;
    unsigned midiResetGeneration() const noexcept;
    bool matches(const PluginSpec &, double rate, int block, PluginBusRequest = {}) const;
    bool matchesPrepared(const PluginSpec &, double rate, int block, const PreparedPluginTransport::Ptr &,
                         PluginBusRequest = {}) const;
    juce::uint32 processId() const;
    unsigned requestState(); // 0 if native editor busy; capture MUST fail/retry, never skip this target.
    unsigned parameterRevision() const noexcept;
    const juce::String &runtimeIdentity() const noexcept;
    void service();
    void beginRetirement();          // Control-thread request/poll only; no synchronous worker wait.
    bool retirementComplete() const; // True only after our retained worker process has exited.
    bool readState(unsigned serial, juce::String &base64, unsigned *parameterRevision = nullptr);
    // CONTROL only. Exact acknowledged native bytes, never the supplied startup
    // state. Closed, unchanged workers may reuse these for structural edits;
    // Save/export must still request a fresh snapshot.
    // Adopts acknowledged bytes as the recovery baseline, like readState().
    bool reuseCapturedState(juce::String &base64, unsigned &parameterRevision);
    unsigned stateCaptureSerial() const noexcept;
    const juce::String &
    recoveryState() const noexcept; // Last successful capture or startup state; control only.
    // Control-thread metadata/control. Busy requests fail explicitly; no worker wait in live DSP.
    unsigned requestParameterCatalogue(); // 0 if native editor busy; no metadata request accepted.
    // Independent control-thread readers share a cached immutable-by-copy snapshot.
    // out.serial/generation form the reader cursor; false preserves out.
    bool readParameterCatalogue(PluginParameterCatalogue &);
    // Immutable post-restore/pre-DSP snapshot. This is not a later controller
    // value or proof of an Undo baseline after presets/schema changes.
    // Control/preparation thread only; false preserves out.
    bool readInitialParameterCatalogue(PluginParameterCatalogue &) const;
    struct StartupDiagnostics
    {
        double launchMs = 0, readyMs = 0, catalogueMs = 0, totalMs = 0;
    };
    StartupDiagnostics startupDiagnostics() const noexcept; // Read after preparation; control only.
    PluginBindingAdmission validateAutomationBindings(std::span<const PluginBinding>);
    unsigned setParameter(std::uint32_t id, float normalized, unsigned catalogueGeneration);
    unsigned setParameterPatch(std::span<const std::pair<std::uint32_t,float>>, unsigned catalogueGeneration);
    bool readParameterResult(unsigned request, PluginParameterResult &);
    bool parameterResultReady(unsigned request);
    void abandonParameterRequest(unsigned request);     // Releases a receipt, does not undo an applied write.
    unsigned pendingParameterRequests() const noexcept; // Control-thread diagnostic; bounded at64.
    // Control thread only. Native editor gestures, separate from playback values.
    std::optional<PluginGesture> lastParameterGesture();
    // Sole CONTROL consumer; service/Last Tweaked retain all completions here.
    // false preserves output. Check losses before/after owning a history batch.
    bool popCompletedParameterGesture(PluginGesture &);
    // Monotonic loss/invalidation units, not a count of valid undoable gestures.
    std::uint64_t completedParameterGestureLosses() const noexcept;
    bool parameterNotificationsEnabled() const;
    void setParameterNotificationsEnabled(bool);
    std::uint64_t droppedParameterGestures() const noexcept;
    int latencySamples() const noexcept; // -1 if latest native+KQ exceeds prepared capacity.
    PluginLatencyStamp declaredLatency() const noexcept;
    // Bounded coherent scalar read; null before first wet packet or while its
    // processing owner publishes. This does not claim graph/PDC adoption.
    std::optional<PluginConsumedLatency> consumedLatency() const noexcept;
    // Quantum is immutable after single-use startup; cursor is processing-owner only.
    int processingQuantum() const noexcept;       // Logical full-Q atomic parameter/audio packet.
    int transportLatencySamples() const noexcept; // Prepared policy*Q, excludes native declaration.
    int maximumLiveSpan() const noexcept; // Plan total-source quota; legacy2Q Q/1Q admission unchanged.
    int packetFrame() const noexcept;
    juce::String error() const;
    bool alive() const;
    enum class AutomationStatus : unsigned
    {
        ready,
        invalidTiming,
        capacityExceeded,
        unavailableTarget,
        snapshotUnavailable
    };
    struct AutomationStatistics
    {
        AutomationStatus last = AutomationStatus::ready;
        std::uint64_t invalidTiming = 0, capacityExceeded = 0, unavailableTarget = 0, snapshotUnavailable = 0;
        bool failed() const noexcept
        {
            return invalidTiming != 0 || capacityExceeded != 0 || unavailableTarget != 0 ||
                   snapshotUnavailable != 0;
        }
    };
    // One processing owner records bounded outcomes; control/renderer reads atomics.
    // Counters survive recovery so a later successful slice cannot hide export loss.
    void recordAutomationStatus(AutomationStatus) noexcept;
    AutomationStatistics automationStatistics() const noexcept;
    struct Statistics
    {
        std::uint64_t submitted = 0, missed = 0, dropped = 0, midiOverflow = 0;
        std::uint64_t processed = 0, workerHeartbeat = 0;
        std::uint64_t parametersApplied = 0, parametersRejected = 0;
        std::uint64_t auxiliaryRejected = 0;
        ipc::DspPhase workerPhase = ipc::DspPhase::preparing;
    };
    Statistics statistics() const noexcept;
    bool deliveryFailed() const noexcept;
    // Finalized before DSP (fresh promotion can bind plan/K once); UUID/Host stay fixed.
    // No String conversion/owner copy. Previously captured control-only plan tokens expire.
    PluginPcmIdentity pcmIdentity() const noexcept;
    // SINGLE PROCESSING OWNER, including for prepared-plan physical metadata.
    // Nonmutating inspection shared by direct Host and partitioned Stream.
    // Capture is optional; no identity/storage work is done for the null path.
    // Caller owns valid writable storage disjoint from all other call inputs.
    // Detectable null/alignment/size/audio-alias errors refuse before processing.
    // A refused inspection does NOT latch a Host fault or shut down a worker.
    PluginPcmPreflight preflightProcess(const juce::AudioBuffer<float> &, int offset, int count, float wet,
                                        const PluginPcmCapture * = nullptr) const noexcept;
    // Nonblocking live processing. Prepared-plan hosts require an admitted
    // physical scope; cumulative samples cannot exceed its total source quota.
    // Legacy explicit2Q count<=Q and default1Q semantics stay unchanged. Invalid
    // spans refuse before mutation; offline waits remain renderer-owned only.
    // KQ is declared transport latency, never a hidden buffer/deadline increase.
    // Optional capture requires disjoint valid caller-prepared storage; insufficient
    // storage refuses BEFORE audio/cursor/MIDI/parameter/ramp mutation. Exact bound
    // for count>0 is1+(packetFrame()+count-1)/Q, calculated with checked int64 math.
    // Two entries suffice for count<=Q. Null preserves all existing span semantics.
    // Output-only metadata: no reference/source/upstream provenance is inferred.
    // Song beat at `seconds` for the next process calls (tempo maps); without it the worker
    // derives PPQ from seconds and tempo. Audio thread only.
    void setBlockPpq(double beat, double seconds) noexcept;
    void process(juce::AudioBuffer<float> &, int offset, int count, const ipc::MidiEvent *, int events,
                 double seconds, double tempo, bool playing, bool bypass, int numerator = 4,
                 int denominator = 4, const ipc::ParameterEvent *parameters = nullptr, int parameterCount = 0,
                 unsigned parameterGeneration = 0, bool parameterBatchValid = true,
                 const juce::AudioBuffer<float> *auxiliary = nullptr, int auxiliaryOffset = 0, float wet = 1,
                 PluginPcmCapture *capture = nullptr) noexcept;

  private:
    // CONTROL ring pin eligibility, not a DSP continuity/handoff certificate.
    // Shared only with the existing original acceptance friend.
    static bool dryHistoryCountersPermit(const Statistics &, const AutomationStatistics &,
                                         std::uint64_t latencyRejected,
                                         std::uint64_t transportFault) noexcept;
};
juce::File pluginWorkerExecutable();
} // namespace vibera
