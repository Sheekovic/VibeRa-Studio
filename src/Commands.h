// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Project.h"
#include "NativeScalarHistory.h"
#include "MixerAutomationTarget.h"
#include "SamplerAutomationTarget.h"
#include "Groove.h"
#include "NoteSlice.h"
#include "NoteStretch.h"
#include "StepCompose.h"
#include <functional>
#include <cstdint>
#include <optional>
#include <memory>

namespace vibera
{
namespace effect_chain
{
struct Chain;
}
enum class Scope
{
    global,
    rack,
    piano,
    playlist,
    mixer,
    browser,
    patterns, // Pattern selector context; existing serialized scope values stay unchanged.
    sampler   // Channel instrument waveform context; appended for profile compatibility.
};
struct CommandInfo
{
    juce::String id, name;
    Scope scope;
    bool documentEdit;
    bool userAction = true; // Internal typed helpers are not standalone shortcut/MIDI/menu actions.
    const char *mcp =
        "unmapped"; // Explicit semantic mapping; new operations fail the coverage gate by default.
};
const std::vector<CommandInfo> &commandRegistry();
// One explicit family for retention arguments; unrelated commands refuse them.
inline bool isStepRetentionCommand(const juce::String &id)
{
    return id == "steps.set" || id == "steps.fill" || id == "steps.fill2" || id == "steps.fill4" ||
           id == "steps.fill8" || id == "steps.compose";
}
// Exactly the original five local-sampler voice-mode controls; no per-note Slide/Porta semantics.
inline bool isSamplerVoiceModeCommand(const juce::String &id)
{
    return id == "channel.sampler.mono" || id == "channel.sampler.portamento" ||
           id == "channel.sampler.slideTime" || id == "channel.sampler.gateBeats" ||
           id == "channel.sampler.fullPortamento";
}
// Positive plugin graph topology classification only. Excludes opaque state,
// routine clip/note/tempo/control edits and display-only Mixer/Channel order.
bool pluginNativeTopologyDifference(const Project &, const Project &);

juce::Result validateRackGroupName(const Project &, const juce::String &, Id except = 0, bool reuse = false);
// Explicit whole-insert deletion policy. Omitted policy is strict refusal of
// dependencies; there is never an implicit Channel/send target or link pruning.
struct MixerInsertDeletePolicy
{
    std::optional<int> replacement;
    bool removeEffects = false, removeAutomation = false, removeGroupMembership = false;
};
// Sparse typed Note Properties. Presence is an edit, including false/zero.
struct NotePropertiesPatch
{
    std::optional<bool> slide, portamento, muted;
    std::optional<int> colourGroup, pitch;
    std::optional<double> velocity, pan, fineCents, modX, modY, releaseVelocity, beat, length;
    bool empty() const noexcept
    {
        return !slide && !portamento && !muted && !colourGroup && !pitch && !velocity && !pan && !fineCents &&
               !modX && !modY && !releaseVelocity && !beat && !length;
    }
};
juce::Result validateNotePropertiesPatch(const NotePropertiesPatch &);
juce::Result decodeNotePropertiesPatch(const juce::var &, NotePropertiesPatch &);
juce::var notePropertiesState(const NotePropertiesPatch &);
struct CommandArgs
{
    Id target = 0, channel = 0, pattern = 0;
    double beat = 0, length = .25, value = 0;
    int pitch = 60, lane = 0, mixer = 0;
    // Typed Mixer target family for automation.create/link/range/init/remove only.
    // Omitted retains legacy gain; `mixer` remains the sole stable owner ID.
    std::optional<MixerAutomationControl> automationControl;
    // Stable destination/field code, only automation.sampler.*; channel is its sole owner.
    std::optional<std::uint32_t> samplerAutomationParameter;
    std::optional<juce::String> mixerGroup; // Canonical UUID; empty only for group unassignment.
    std::optional<MixerInsertDeletePolicy> mixerDelete;
    // Outer presence is required only for plugin.effect.sidechain; inner null disconnects.
    std::optional<std::optional<PluginDetectorAssignment>> detector;
    std::optional<int> mixerPosition; // Final zero-based Mixer DISPLAY row; never a stable ID.
    std::optional<ChannelAppearancePatch> appearance;
    std::optional<ChannelSortRequest> channelSort;
    int numerator = 4, denominator = 4;
    bool enabled = true, step = false;
    juce::String text;
    std::vector<Id> ids;
    std::vector<Note> recordedNotes;
    std::vector<ControllerEvent> recordedControllers; // notes.record: take controllers (ids allocated).
    // notes.import: optional file tempo map (beat, BPM) and meter applied in the same Undo step.
    struct ImportedTempo
    {
        std::vector<std::pair<double, double>> tempos;
        int numerator = 0, denominator = 0;
    };
    std::optional<ImportedTempo> importTempo;
    std::vector<juce::String> importTrackNames; // notes.importTracks: MIDI track names by track index.
    std::vector<std::pair<Id, juce::String>> relinks; // media.relink: Channel -> new absolute file path.
    juce::String generation;
    double offset = 0;
    std::optional<PluginBinding> binding;
    int interval = 4; // Sixteenth-note interval for steps.fill; shared UI/MCP/macro default.
    std::vector<StepComposeRecipe> composition;
    Id compositionSolo = 0; // Internal owned Advanced Fill audition; never JSON/profile input.
    double strength = 1, sensitivity = 1, durationStrength = 1; // Quantizer normalized strengths.
    double velocityStrength = 0;
    bool zeroVelocityDeletes = true;
    std::optional<GrooveTemplate> groove;
    std::optional<NoteSliceLine> slice;
    std::optional<bool>
        grouping; // Omitted follows runtime grouping; false explicitly edits members independently.
    std::optional<bool> retainSteps; // Omitted resolves through Workflow; core default is legacy deletion.
    std::optional<bool> restore; // Solo exit: omitted uses the application preference (core default true).
    std::optional<juce::String> soloScope; // channel.solo only: global (default) or group.
    // Internal control-thread capture; never accepted from MCP/profile JSON.
    // Clone/delete Undo baselines retain the selected instruments' fresh state.
    std::vector<PluginSpec> capturedInstruments;
    // audio.takes only: one window of the recorded source per take (beat, lane, offset,
    // duration, muted); `id`/`channel` are allocated by the command.
    std::vector<AudioPlacement> audioTakes;
    // audio.takes from several input tracks at once: one source file each (`text` is the
    // first); each window's `channel` is the 0-based index of its file.
    std::vector<juce::String> takeFiles;
    // audio.fade only: new fade lengths in seconds; omitted keeps the clip's value.
    std::optional<double> fadeIn, fadeOut;
    // Score tools with randomness (notes.arpeggiate random, notes.humanize); default 1.
    std::optional<std::uint32_t> seed;
    std::optional<std::array<double, 12>> tuning; // channel.tuning: cents per pitch class C..B.
    std::optional<std::vector<double>> scale;      // channel.scale: degree cents, last = period.
    // channel.sliceToChannels: slice starts as fractions of the source (first is 0);
    // `length` is the source duration in seconds.
    std::vector<double> slicePoints;
    // audio.comp: [fromBeat, toBeat] swipe range on the chosen take (whole clip when absent).
    std::optional<std::array<double, 2>> compRange;
    // Exact send destination; required only for Mixer automationControl=send.
    // `target` remains the Automation source ID and `mixer` the stable source insert.
    std::optional<int> automationSendTarget;
    std::optional<PluginMainIo> pluginIo; // Required only for plugin.io; all four fields explicit.
    // Project-only mapping metadata. Create id0; update id==target; remove has no payload.
    std::optional<SamplerControllerLink> samplerControllerLink;
    std::vector<Id> samplerControllerReplacements; // Exact conflicting PROJECT IDs, never global rows.
    // notes.add creation fields; never create an audible ordinary note before a Slide edit.
    bool slide = false, portamento = false;
    int colourGroup = 0;                               // Exact0..15; omission preserves neutral creation.
    std::optional<NotePropertiesPatch> noteProperties; // notes.properties only; sparse atomic selection edit.
};
enum class HistoryDirection
{
    undo,
    redo
};
class MixerAutomationTake;
class Controller;
// Control-thread preparation input. No graph, host or Undo action is owned here.
// Default construction is invalid until previewHistoryDestination succeeds.
class HistoryDestination
{
  public:
    const Project &destination() const noexcept
    {
        return destinationProject;
    }
    HistoryDirection direction() const noexcept
    {
        return requestedDirection;
    }
    const juce::String &transactionName() const noexcept
    {
        return name;
    }

  private:
    friend class Controller;
    const Controller *controller = nullptr;
    Project sourceProject, destinationProject;
    juce::String identity, name;
    std::uint64_t sourceRevision = 0;
    HistoryDirection requestedDirection = HistoryDirection::undo;
    // Non-owning identity only, never dereferenced. Re-read live actions instead.
    std::vector<std::uintptr_t> actions;
};
// Internal control-thread transaction; not a user-facing raw Project replacement.
// Preparation and validation may allocate. Commit is bounded/noexcept and never
// delivers observers. Destroy tickets on the control thread AFTER publication.
class PreparedControllerChange
{
  public:
    struct NativeEffectHistoryWitness
    {
        const Project* capturedSource = nullptr;
        const Project* destination = nullptr;
        int insert = 0;
        HistoryDirection direction = HistoryDirection::undo; // Meaningful only for a history traversal.
        bool isHistoryTraversal = false; // Fresh canonical commands are neither Undo nor Redo.
    };
    // Internal admission evidence only: caller must validate this SAME ticket
    // against Controller and native worker/source identities immediately before
    // joint publication. Not a public project setter or an audio-completion token.
    std::optional<NativeEffectHistoryWitness> nativeEffectHistoryBeforeCommit() const noexcept
    {
        if (committed || !history || !nativeEffectHistorySource) return {};
        return NativeEffectHistoryWitness{nativeEffectHistorySource, &nextProject, nativeEffectHistoryInsert,
                                          nativeEffectHistoryDirection, nativeEffectHistoryIsTraversal};
    }
    bool wasCommitted() const noexcept
    {
        return committed;
    }
    // A canonical structural command that changes nothing still has a real
    // ticket, but needs no audio projection/history/revision/notification work.
    bool isNoOp() const noexcept
    {
        return commandNoOp;
    }
    // The slot owns retired source after commit: never expose it as the target.
    const Project *destinationBeforeCommit() const noexcept
    {
        return committed ? nullptr : &nextProject;
    }

  private:
    friend class Controller;
    Controller *controller = nullptr;
    Project sourceProject, nextProject;
    juce::String identity, owner, retiredOwner;
    std::uint64_t sourceRevision = 0, sourceGestureSerial = 0;
    bool sourceDirty = false, sourceGestureDirty = false, sourceHasGesture = false;
    bool clearGesture = false, clearQuantizer = false, destinationDirty = true;
    bool incrementRevision = true, validated = false, committed = false, commandNoOp = false;
    std::unique_ptr<Project> retiredGesture, retiredQuantizer;
    std::unique_ptr<juce::UndoManager::PreparedHistoryMutation> history;
    const Project *historyBeforeSnapshot = nullptr;     // Actual newly owned StateAction.beforeSnapshot.
    const Project *nativeEffectHistorySource = nullptr; // Immutable StateAction owned by history ticket.
    int nativeEffectHistoryInsert = 0;
    HistoryDirection nativeEffectHistoryDirection = HistoryDirection::undo;
    bool nativeEffectHistoryIsTraversal = false;
};
class Controller
{
  public:
    Project project = Project::empty();
    juce::UndoManager undo;
    std::function<void()> changed;
    // Optional CONTROL application routing/admission. Empty preserves the
    // headless/default canonical behavior. nullopt means execute the ordinary
    // canonical implementation; a returned Result is handled/refused already.
    // Prepared internal APIs never call these hooks (no recursive mutation).
    std::function<std::optional<juce::Result>(const juce::String&, const CommandArgs&,
        const juce::String&)> applicationCommand;
    std::function<std::optional<juce::Result>(HistoryDirection)> applicationHistory;
    std::function<juce::Result()> applicationGestureAdmission;
    // CONTROL-only final musical admission, before canonical/history publication.
    // Used to protect an active recorded Pattern window; never invoked from DSP.
    std::function<juce::Result(const Project &)> applicationChangeAdmission;
    std::uint64_t revision = 0;
    juce::String projectIdentity = juce::Uuid().toString(); // Session identity changes on replacement.
    bool dirty = false;
    bool clipGroupingEnabled = true; // Runtime workspace behavior; not musical Undo or membership.
    juce::Result execute(const juce::String &, const CommandArgs & = {}, const juce::String &owner = {});
    // Internal CONTROL-only prepare path for channel.mixer, mixer.route,
    // mixer.routeOnly and mixer.send. Runs the SAME canonical mutation and
    // history preparation as execute, without commit or observer delivery.
    // Refuses ALL active gestures (including the supplied owner); never ends,
    // cancels or takes one over. On refusal output remains unchanged.
    // On success inspect isNoOp/destinationBeforeCommit, prepare audio, then
    // repeat validatePreparedChange immediately before canonical publication.
    juce::Result prepareStructuralCommand(const juce::String &, const CommandArgs &,
                                          std::unique_ptr<PreparedControllerChange> &,
                                          const juce::String &owner = {});
    // Internal CONTROL-only canonical effect command preparation. Only
    // plugin.effect.insert into an empty chain or plugin.remove of an entire
    // existing Mixer chain is admitted by the shared stopped-effect planner.
    // capturedSource may differ ONLY in existing same-ID/descriptor opaque
    // plugin states. Actual capture/worker freshness is the application's job.
    // History.before uses capturedSource; ticket freshness uses actual Project.
    // No application hook, commit, observer, gesture takeover or audio work.
    // Refusal preserves output and all canonical/history state.
    juce::Result prepareStoppedEffectCommand(const juce::String &, const CommandArgs &,
                                             const Project &capturedSource,
                                             std::unique_ptr<PreparedControllerChange> &);
    // General interrupted Mixer chain application. CONTROL preparation only:
    // capturedSource differs from actual Project only in same-ID/descriptor
    // native opaque states; freshness/worker capture is the application's job.
    // Replacement owns one real history action and reserves nonrecycling IDs
    // within this Controller session, including discarded prepared tickets.
    // No application hooks, observers, graph work, gesture takeover or commit.
    juce::Result prepareMixerChainReplacement(const Project &capturedSource, int insert,
                                              const effect_chain::Chain &,
                                              std::unique_ptr<PreparedControllerChange> &);
    // Fresh-state single-slot replacement, using the same plugin.effect.replace
    // canonical mutator as execute. Existing Mixer effect ID only. Other slots,
    // bindings and routing remain exact; referenced target automation refuses.
    // Replacement gets a nonrecycled new ID, default empty native state and no
    // copied native bindings; host slot wet/bypass and chain override remain.
    // CONTROL preparation only. No hooks, observers, gesture takeover or commit.
    juce::Result prepareMixerEffectReplacement(const Project &capturedSource, Id target,
                                               const juce::String &descriptor,
                                               std::unique_ptr<PreparedControllerChange> &);
    // Same-ID detector dependency change. Fresh opaque-only source capture, one
    // history action; actual bus negotiation/activation is the application's job.
    juce::Result prepareMixerEffectSidechain(const Project &capturedSource, Id target,
                                             std::optional<PluginDetectorAssignment>,
                                             std::unique_ptr<PreparedControllerChange> &);
    // General interrupted variable-insert path, separate from retained fixed-
    // universe routing. Only mixer.insert.create/delete; fresh opaque-only
    // capturedSource, one actual history action, session-reserved new IDs.
    // No app hooks, observers, gesture takeover, native work or commit.
    juce::Result prepareMixerInsertCommand(const juce::String &, const CommandArgs &,
                                           const Project &capturedSource,
                                           std::unique_ptr<PreparedControllerChange> &);
    juce::Result prepareMixerInsertHistory(HistoryDirection, const Project &capturedSource,
                                           std::unique_ptr<PreparedControllerChange> &);
    // One actual owned pure Mixer-effect structural action (chain replace,
    // append/remove or reorder), with newly captured current native state
    // spliced into BOTH immutable replacement history endpoints. Missing old
    // plugins retain their historical bytes. Mixed/nonstructural actions refuse.
    // Separate from stopped-effect capabilities; no stopped witness is minted.
    juce::Result prepareMixerEffectHistory(HistoryDirection, const Project &capturedSource,
                                           std::unique_ptr<PreparedControllerChange> &);
    // Captured interrupted structural operations only. Existing send LEVELS use
    // their normal LiveDocument path; plugin.remove accepts effects only.
    juce::Result prepareCapturedMixerCommand(const juce::String &, const CommandArgs &,
                                             const Project &capturedSource,
                                             std::unique_ptr<PreparedControllerChange> &);
    // One actual owned structural snapshot, with fresh current opaque state in
    // BOTH history endpoints. Application chooses only structural differences;
    // native scalar actions keep their separate witnessed-overlay contract.
    juce::Result prepareCapturedStructuralHistory(HistoryDirection, const Project &capturedSource,
                                                  std::unique_ptr<PreparedControllerChange> &);
    juce::Result preparePluginIoHistory(HistoryDirection, const Project &capturedSource,
                                        std::unique_ptr<PreparedControllerChange> &);
    // Actual owned route-only history (Channel mixer assignments / Mixer routes),
    // with same-ID opaque state refreshed in BOTH history edges before publication.
    juce::Result prepareCapturedMixerRoutingHistory(HistoryDirection, const Project &capturedSource,
                                                    std::unique_ptr<PreparedControllerChange> &);
    // Internal closed route-only macro/batch preparation. Every assignment uses
    // the same canonical mutation as channel.mixer; one real StateAction only.
    juce::Result prepareChannelRoutingSequence(const std::vector<CommandArgs> &, const juce::String &label,
                                               std::unique_ptr<PreparedControllerChange> &);
    // Internal Song touch recorder. Canonical manual mutator, no per-event history;
    // final capture is one real prepared snapshot action, cancel restores baseline.
    juce::Result prepareAutomationTouchPreview(const MixerAutomationTake &, const juce::String &,
                                               const CommandArgs &,
                                               std::unique_ptr<PreparedControllerChange> &);
    juce::Result prepareAutomationTouchFinish(const MixerAutomationTake &, bool cancel,
                                              std::unique_ptr<PreparedControllerChange> &);
    void replace(Project, bool deferNotifications = false);
    bool beginGesture(const juce::String &owner = {});
    void endGesture(const juce::String &name, const juce::String &owner = {});
    void cancelGesture(const juce::String &owner = {});
    juce::Result endGestureChecked(const juce::String &name, const juce::String &owner = {});
    juce::Result cancelGestureChecked(const juce::String &owner = {});
    const juce::String &gestureOwner() const
    {
        return editOwner;
    }
    void undoEdit();
    void redoEdit();
    juce::Result undoEditChecked();
    juce::Result redoEditChecked();
    // Internal prepared, validated canonical snapshot path. No graph publication
    // is performed here. historyBefore may carry freshly captured native state.
    juce::Result prepareSnapshotChange(Project next, const Project &historyBefore, const juce::String &name,
                                       bool recordHistory, bool finishGesture, bool destinationDirty,
                                       std::unique_ptr<PreparedControllerChange> &,
                                       std::span<const NativeScalarEdit> nativeScalars = {});
    // Fresh native AFTER plus witnessed supported scalar BEFORE. One real
    // canonical history action; first-unbound parameters remain unbound.
    juce::Result prepareNativeScalarEdit(const Project &, std::span<const NativeScalarEdit>,
                                         std::unique_ptr<PreparedControllerChange> &);
    bool isNativeScalarHistory(HistoryDirection) const;
    juce::Result prepareNativeScalarHistory(HistoryDirection, const Project &,
                                            std::unique_ptr<PreparedControllerChange> &);
    juce::Result prepareHistoryChange(HistoryDirection, std::unique_ptr<PreparedControllerChange> &);
    // Internal closed empty-chain append/removal history only, one owned action.
    // capturedSource differs from current canonical Project ONLY in opaque native
    // states. Actual native capture ownership/revision comes from app admission.
    // Success prepares actual-manager inverse snapshots; does not post/commit or
    // complete DSP selection. Output/history/model unchanged on refusal.
    juce::Result prepareStoppedEffectHistory(HistoryDirection, int insert, const Project& capturedSource,
                                             std::unique_ptr<PreparedControllerChange>&);
    // Internal captured-native metadata refresh, outside musical Undo. Only
    // opaque state may differ; same source/identity validation and swap commit.
    juce::Result prepareNativeStateRefresh(Project, std::unique_ptr<PreparedControllerChange>&);
    // Exact source checks may allocate; call immediately before the cheap commit
    // on the sole control thread, without pumping/reentering or direct model writes.
    juce::Result validatePreparedChange(PreparedControllerChange &) const;
    bool commitPreparedChange(PreparedControllerChange &) noexcept;
    // true means no pending requests remain; async JUCE request is not delivery.
    bool deliverPendingNotifications() noexcept;
    bool hasPendingControllerNotification() const noexcept
    {
        return pendingControllerNotification;
    }
    bool hasPendingUndoNotification() const noexcept
    {
        return pendingUndoNotification;
    }
    // Read-only: refuses active gestures, an empty direction or foreign actions.
    // Does not cancel/finish a gesture, execute actions, notify or move history.
    // On refusal, the supplied destination is unchanged.
    juce::Result previewHistoryDestination(HistoryDirection, HistoryDestination &) const;
    // Repeat immediately before prepared publication/commit on the sole control
    // thread. Rejects model/session/revision/cursor/action changes during prepare.
    // Success is admission only; this does not commit or consume Undo/Redo.
    juce::Result validateHistoryDestination(const HistoryDestination &) const;
    void notify();
    void markExternalChange(); // Native state is outside Project snapshots/Undo; retain unsaved ownership.
    std::uint64_t externalChangeSerial() const noexcept
    {
        return externalChangeVersion;
    }
    bool hasGesture() const
    {
        return bool(gestureStart);
    }
    bool hasQuantizerPreview() const
    {
        return bool(gestureStart) && bool(quantizerStart);
    }
    const Project *quantizerBaseline() const
    {
        return quantizerStart ? &*quantizerStart : nullptr;
    }
    std::uint64_t gestureSerial() const noexcept
    {
        return editSerial;
    }

  private:
    // Pointer ownership permits no-delete retirement in the prepared commit.
    std::unique_ptr<Project> gestureStart, quantizerStart;
    bool pendingControllerNotification = false, pendingUndoNotification = false;
    bool deliveringNotifications = false;
    juce::Result executeInternal(const juce::String &, const CommandArgs &, const juce::String &owner,
                                 std::unique_ptr<PreparedControllerChange> *preparedOutput,
                                 const Project *capturedEffectSource = nullptr, bool withoutHistory = false);
    static void applyPreparedChange(void *) noexcept;
    bool isPreparedChangeCurrent(const PreparedControllerChange &) const noexcept;
    juce::String editOwner;
    bool gestureDirty = false;
    std::uint64_t editSerial = 0;
    std::uint64_t externalChangeVersion = 0; // CONTROL-only, saturating unsaved-owner witness.
    // CONTROL session-only reservation floor for prepared chain replacements.
    // Not musical Undo; cancelled/discarded candidates never return these IDs.
    // Other historical allocation APIs are unchanged by this bounded seam.
    Id mixerChainNextId = 1;
    Id mixerInsertNextId = 1; // Same session-only reservation policy; not persisted or musical Undo.
};
} // namespace vibera
