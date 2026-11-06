# Effect-chain capture, presets and Apply

This is development source guidance. Qualified0.0.63 remains the sole permanent
distribution. Discover the connected executable's `vibera_mixerchain` tool and
command contracts before invoking it; source/registration is not qualification.
Use the connected executable's discovery to distinguish **Load to clipboard**
from **Load chain preset and apply**. Source implementation alone does not qualify
the new Apply workflow or installed effects.

The Mixer effect menu and `vibera_mixerchain` share the same application operation.
Stable actions are `mixer.chain.copy`, `mixer.chain.save`,
`mixer.chain.loadClipboard`, `mixer.chain.directory`, `mixer.chain.cancel`,
`mixer.chain.paste` and `mixer.chain.loadApply`. Copy, Save and Load to clipboard
modify no Project, Undo/Redo, effect IDs, automation, routes or audio graph.
The clipboard is portable effect data; it is not a live native clipboard handle.

Malformed wire arguments fail before admission through the standard MCP server
error envelope: `completionState=failed,error={code,message,retryable}`. The
output schema advertises this separately from the canonical service's flat
`code/message` result. Action-specific/context refusals still come from the
shared UI/application service. Neither error admits a job or changes history.

Read `action=state` first. For Copy/Save supply the current `projectIdentity`, exact
`revision` and existing stable `mixer` index (Master0 and sparse indices supported).
Finish gestures, takes, parameter/editor work and other capture/projection/file
work first. Source capture is target-only: actual live owners must acknowledge
opaque state at their current supported parameter revision. The source Project,
revision, session and exact worker identities are validated again before
publication. A stale source fails without replacing the clipboard or file.
Unsignalled third-party native widget edits are not universally detectable.

Full structural Apply/history may briefly interrupt audio and reset sounding
instrument notes while fresh state is captured and the graph is replaced.
This is an accepted transition, not seamless playback. The operation must recover
automatically with the exact intended native state/routing/history. Observe its
terminal job and active audio state; no recording/take may be silently discarded.
Ordinary musical edits retain their separate continuity requirements.

`action=copy` returns a session operation. An accepted/pending operation is not a
copied clipboard. Poll `action=job, operation=<exact token>` until `completed`,
`failed` or `cancelled`. Receipt provenance distinguishes `nativeAcknowledgement`,
`retainedProjectBytes` and `retainedRecoveryBytes`. Missing-plugin saved or newer
matching recovery bytes remain portable but are not freshly acknowledged state.
Only the current clipboard retains opaque chain bytes; terminal job history
retains compact labels, not native handles or a copy of every old chain. IDs and
history expire with this Studio session, never silently during that session.

Configure one existing ordinary LOCAL directory with `action=configureDirectory,
directory=<absolute path>` or the menu's Chain preset directory action. There is
one worker, no implicit mkdir, network access or pool per folder. Active capture
or file work refuses reconfiguration; invalid roots preserve the prior root.
Changing a root preserves readable terminal job receipts.

Save: `action=save` plus current source guards and `path=<absolute direct child
.vibfx>`. `overwrite` defaults false. An existing output is never replaced without
explicit true/GUI Replace confirmation. After genuine source capture, background
write/flush/close and atomic commit must finish before the job says completed.
Save does not change the clipboard. Load: `action=loadClipboard,path=...` reads and
decodes background, then publishes portable clipboard data on CONTROL; it does
not load plugins, change a destination or imply Paste. Malformed data preserves
the previous clipboard. Read receipts' actual fileCommitted/nativeError and
temporaryPath/temporaryRemaining/cleanupError; cleanup failure is not hidden.
The helper reserves at most128MiB of serialized payload across its records; that
is not a bound on all process heap usage. Portable chains are limited to16 slots/64MiB.

Cancel with the exact operation. Native capture cancellation remains pending
while the owned request drains; it cannot cancel another Save. File cancellation
accepted before committing prevents publication. Once committing, `tooLate`
does not mean the write stopped; inspect the original job's actual terminal
outcome. A request-response exception while admitted work survives reports that
operation pending, not a false failed/rolled-back job.

Format1 preserves ordered Windows VST3 effect descriptors and opaque state,
names, slot wet0..1/bypass, chain bypass and default main stereo I/O. Format2
also preserves the four selected native main I/O fields; default selections
retain exact Format1 encoding. Both formats keep the same limits. Read
[Plugin audio I/O](plugin-audio-io.md) before applying a nondefault selection:
actual destination native negotiation is still required. Both exclude source/destination IDs,
runtime owners, parameter catalogues, automation links/Clips, controller mappings
and routing. Codec validation rejects malformed UTF-8, unknown flags/version,
trailing bytes and descriptor DOCTYPE/entity declarations. No arbitrary native
widget control, installed-provider compatibility, listening or hardware claim.

Paste: `action=paste` plus the CURRENT destination `projectIdentity`, exact
`revision` and existing `mixer` index. It requires a copied/loaded clipboard.
Load and apply: `action=loadApply` plus those same destination guards and the
absolute direct-child `.vibfx` `path` in the configured directory. The Mixer menu
provides **Paste effect chain**, **Load chain preset...**, and the separate
**Load chain preset to clipboard**. Apply replaces the selected insert's complete
chain, including an empty chain; it is not a single-slot Replace operation.
Malformed input, stale contexts and failed preparation preserve the clipboard.

Apply first captures the CURRENT destination's outgoing opaque states with the
same target-only acknowledgement contract, then prepares one canonical history
ticket and the full native destination. Exact original Hosts, revisions, state,
editor/control readiness and device configuration are rechecked before each
build and again at final native admission. Candidate Host maps are not mistaken
for the pinned outgoing owners. Expensive worker preparation leaves incumbent
audio running; a brief existing reconfiguration interruption is permitted.
This does not promise a seamless crossfade or preservation of arbitrary tails.

Poll the returned session operation. `pending`/`preparing` means no completion.
`documentChanged` and `undoable` become true only after actual canonical/native
publication. The receipt then remains pending until actual source activation or
explicit `audioState=deferredNoDevice` document-only completion. Audio states are
`preparing`, `pending`, `active`, `deferredNoDevice` and `fault`.
`nativeRecoveryRequired=true` means the intended document is committed but audio
restoration is unresolved; a failed receipt does not claim that commit rolled
back. Inspect its exact error and recovery state before issuing another change.
After publication, activation has a10000ms monotonic QPC bound. Missing callbacks
fail with `effectChain.activationTimeout`, retaining the intended project and
Undo history. This does not terminate published workers. A separately admitted
device recovery keeps its own owner/token (`recoveryOperation` when present);
inspect that operation and current audio state. A terminal failure is historical
and is not rewritten if callbacks subsequently recover. This bound applies to
activation, not physical callback DSP deadlines or background plugin startup.

An Apply creates one Undo step containing freshly captured outgoing state. Pure
Mixer-effect structural Undo and fresh Redo use the same prepared full-projection
workflow and return their own asynchronous chain operation. Ordinary control
history keeps its normal path. Mixed music/routing history never silently falls
back to this operation after a failed native route preparation. Removed effect
IDs are not recycled; unrelated routing/music are preserved and incompatible
automation references are rejected by canonical preparation.

During owned Apply, finish or cancel it before document/file/source/native-control
changes. Active recording, retained takes, gestures, pending controls and open
native editors refuse admission. Cancel BEFORE publication drains the actual
owned capture/file/preparation thread; it remains pending during that drain.
Preparation drain means private workers are retiring, not that every process has
already exited. AFTER publication cancellation is `effectChain.tooLate`; the
original job continues activation/recovery and the intended model stays explicit.
A fresh inverse history operation is required after completion.

To replace one slot, discover `plugin.effect.replace`, then use `vibera_command`
with exact current effect `target` ID and the scanned effect descriptor `text`.
The Mixer slot's **Replace…** submenu uses the same command. Its returned chain
job is asynchronous and has the same actual activation/cancellation contract.
The new plugin gets a fresh ID/default native state; the host wet/bypass settings
and every other slot's IDs/order/state/bindings remain. Undo restores the captured
old plugin. Target automation links/initialization must be removed explicitly
first; they are never silently attached to an unrelated replacement. Structural
Replace/Paste/Apply are unavailable in the synchronous document-macro runner.

Internal Mixer development now separates worker-bank preparation, pending source
expansion, paired native close, graph selection, predecessor DSP-reader
quiescence and final bank close. Quiescence excludes old DSP readers; it does
not promise that native cached graph/plugin owners or worker processes have
been destroyed. Final membership completion requires its own actual close ACK.
Cancellation before graph publication must acknowledge the return to the source
bank; after publication it is too late and requires a new canonical inverse.
These backend stages do not qualify general PDC transitions. The interrupted
Apply workflow uses the existing full-projection path. Continue using only the
connected executable's advertised actions and their real terminal job results.

Canonical `plugin.effect.insert` appends to an existing Mixer insert, including
Master0, up to the current16-slot limit. When its bounded structural operation
finds idle native editors open, that same pending job first requests their normal
native closure and waits for each exact acknowledgement, then captures fresh
state before preparing or committing the append. Inspect `editorClosures` in the
job receipt; native close admission alone is not completion. Cancel retains the
incumbent model and drains already admitted closes; an editor already closed is
not reopened by cancellation. Active parameter gestures or editor requests still
refuse admission. Replace, reorder and other Apply actions retain their explicit
close-editors prerequisite; this is not universal automatic editor closure.

Add insert from chain preset: action `mixer.insert.fromChainPreset` (Mixer effect menu > Add
insert from chain preset...) creates a new insert, waits for that structural change to commit
(up to about 10 s) and then opens the chain file chooser for the new insert (loadApply). It
refuses before creating anything when no chain preset directory is configured. The chooser is
native; automation can pick the file in its list with UI Automation (owned process only).

Mixer track state (Mixer track menu > File > Save / Open mixer track state; `vibera_project`
`saveTrackState` / `openTrackState` with `mixer` and an absolute `.vibfx` path directly in the
configured chain preset directory): Save is the chain save of that track plus a track-controls
block (header flag 2: name, colour, volume, pan, stereo separation, phase/swap, EQ, track delay,
dock). Open applies the chain with loadApply, then the controls with `mixer.trackControls` (JSON
in `text`); each is its own Undo step. Plain chain presets stay readable; older builds refuse
files with the controls block. Both start once earlier audio work has settled.
