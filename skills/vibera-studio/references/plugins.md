# Loaded VST3 operations (application 0.0.44+, contract 1)

1. `vibera_plugin action=list` returns stable project plugin IDs, Channel/Mixer/
   ordered-slot context, availability, runtime instance and latency samples.
   Paginate using offset/limit/nextOffset. This lists loaded project specifications,
   including missing/unavailable placeholders; it does not scan the OS database.
2. `action=parameters, plugin=<ID>` reads native controller catalogue snapshots.
   Provide instance to reject an old worker. Native parameter IDs are unsigned
   32-bit values including zero; they are not JUCE dropdown positions. Paginate
   the total. Normalized/default/initial values are 0..1 or null when unavailable.
   Flags/steps/unit/label and hidden/writable/native automatable/application
   automationSupported are distinct. An application-supported declaration still
   needs fresh runtime admission and project validation before it can be applied.
3. To refresh, call `action=refresh, plugin=<ID>, instance=<exact instance>`.
   The response is pending with operation. Poll `action=job, operation=<token>`
   until completed/failed/cancelled. Completion includes actual catalogue serial/
   generation. Do not substitute receipt acceptance for metadata readiness.
4. `action=controls` opens VibeRa's shared JUCE Controls window; optional parameter
   selects a visible native ID once controls.ready is true. It does not operate
   the third-party native editor. `action=closeControls` hides that target inspector.
   Existing `vibera_select pluginView=native` remains a separate request with
   pending visibility acknowledgement; arbitrary vendor widgets have no API.
5. `action=notifications, enabled=<bool>` sets the same worker-session option as
   the Controls checkbox, and returns its actual value. It changes neither project
   Undo nor the saved preset. The native Last tweaked epoch is reset by this option.
6. For an automation clip, read fresh capabilities, retain projectIdentity,
   revision, instance and generation, then call `action=automate` with plugin,
   instance, parameter, generation and automation=create. Init/removeInit are
   also supported. They prepare fresh metadata asynchronously and invoke the same
   canonical application method as the UI. Poll the job to completion, inspect
   automationSources/clips, then verify edit.undo/edit.redo. A no-op adds no Undo.
   Failed preparation is not a successful document edit; read structured errors.
7. Save/reopen with vibera_project and verify target bindings/clips. Export and
   poll the export job; verify actual WAV bytes/duration, not only file creation.

Worker readiness includes completion of native editor-capability probing and
CONTROL setup before the DSP loop announces ready. Some VST3 providers create a
view during that probe. Keep observing the original preparation job until its
actual terminal outcome; do not send duplicate controls to compensate for startup.
Startup work retains its existing preparation budget; parameter requests retain
their ordinary control deadline. A timeout remains a failure, never success.

`action=recover` requests the same last-captured/startup-state recovery as the UI.
It requires the current instance (empty string only for a missing-runtime
placeholder) and refuses healthy workers. Its pending job completes only after
an actual different ready worker and completed graph preparation. It can restart
voices and lose uncaptured changes. Recovery cancellation is unsupported once
admitted; report that error and observe actual runtime state. Recovery timeout
sets effectMayHaveOccurred=true; rediscover before retrying. Refresh observation
can be cancelled; the read-only refresh may still finish in the worker.

At most64 process-local receipts are retained; oldest terminal receipts may expire.
Unknown/expired tokens fail explicitly. Project/plugin replacement cancels pending
observations; instance/schema changes fail stale requests. An active document
gesture prevents conflicting automation commits. Optional revision guards refuse
interleaved project edits; no other request may consume a token's result.

The initial snapshot is taken after native state restoration/preparation and before
DSP starts. It is immutable and invalidated by schema generation changes. It is
neither a live DSP value nor a canonical manual fallback after later edits/presets.
In unreleased source, manualWriteSupported enables action=set for supported
writable scalar targets; read manualHistoryScope and the
[native scalar guide](native-parameter-history.md), then poll actual ACK/history.
manualParameterUndo remains false for arbitrary native UI/state reversal;
scalarParameterUndo identifies the bounded supported path. Never use a catalogue
snapshot as the BEFORE witness or pointer/key diagnostics as semantic coverage.
Native current controller snapshots may differ from instantaneous automated DSP.

Real provider tests cover Analog Lab V/MVibratoMB catalogue/notifications/Controls,
automation preparation/Undo/persistence/WAV and a real asynchronous validation
failure. This does not qualify successful crash recovery through MCP, arbitrary
native widgets, changing-curve musical accuracy, physical MIDI or listening.
MVibrato demo expiry limits quality/duration claims. Check STATUS for exact binaries.

## Unified wrapper (development source; discover before use)

`vibera_plugin action=wrapper, plugin=<ID>, instance=<current instance>,
view=plugin|settings|parameters` opens/focuses one worker-owned window. It returns
pending; poll its operation with action=job until terminal. Plugin displays the
actual VST3 editor; Settings and Parameters share that window. Optional exact
uint32 parameter/generation select a Parameters target (native ID0 is valid).
Stale selections refuse without changing the active target. `action=editorState`
exposes wrapperView, wrapperReady, wrapperPending and actual selected native
parameter/generation alongside native window state. Navigation is not document
history. `action=closeEditor` closes this instance's wrapper asynchronously.

Wrapper bypass and effect wet/dry reuse plugin.bypass/plugin.effect.mix; exposed
parameter/reset uses the existing supported scalar ACK/current-state history
service. Acceptance is not DSP settling or history completion. The first wet/dry
UI submits on release; continuous live wet/dry is not qualified. Instruments do
not expose effect wet/dry. Arbitrary native plugin widgets remain unsupported.
The advanced legacy Controls inspector still exists during migration; do not
claim its automation/preset/controller services are all in the wrapper yet.
This guidance describes unreleased source, not the qualified distribution.

Ordinary Channel/Mixer Open Controls now selects the worker wrapper's Parameters
view. The advanced legacy `action=controls` inspector remains a compatibility path
during the remaining migration; use `action=wrapper` for the unified window.
Parameters adds Create automation clip, Initialize and Remove init, equivalent to
`vibera_plugin action=automate` with automation=create|init|removeInit, the exact
plugin/instance/parameter/generation and optional revision guard. These controls
observe the same asynchronous catalogue validation and canonical Undo/Redo as MCP.
A missing initialization is an actual failed job, not an invented successful no-op.
Refresh parameters maps to action=refresh. Native parameter notifications maps to
action=notifications with enabled=true|false; it is session state, not project
history. `wrapperNotifications` reports that actual setting.

For Last tweaked, read wrapperLastTweakedParameter/wrapperLastTweakedGeneration
from action=editorState, then select that exact pair with action=wrapper and
view=parameters. Null means no current published gesture; zero is a valid ID.
The button and MCP select the same worker-side catalogue entry. This readback
comes from the application's sole native-gesture drain, never another DSP consumer.
`wrapperOperation`, when present, identifies the actual pending canonical job;
poll action=job until completed/failed/cancelled and verify project readback.

For a plugin that declares no native editor, explicit Settings/Parameters can
open a real host-control window with availability=controls_only and
nativeInterfaceAvailable=false. Plugin tab is disabled. Native openEditor still
fails unsupported, including when it preserves an existing controls-only window.
This is not native VST3 attachment. A plugin that claims an editor but fails to
create/attach it retains its actual failure; it is not relabelled editorless.
Empty or unsupported native catalogues cannot advertise writable parameters.
These changes require a matching IPC19 app/worker in current development source. Current verification is in
STATUS; this source guidance does not qualify the unchanged distribution.
With a plugin Channel selected, `vibera_command` command=channel.pluginControls
opens that same Parameters view. Its response is pending with an editor operation
token: poll `vibera_plugin` action=job, then verify editorState. A sample/built-in
Channel without a hosted plugin returns unavailable_target; it does not fabricate
a completed plugin window. Explicit legacy action=controls remains compatible,
including recovery of that inspector; ordinary UI entry points use the wrapper.

For a continuous supported scalar edit, `action=set` accepts gestureOpen=true.
Retain the returned operation; send updates with that same operation, plugin,
instance, native parameter and generation. End with the final value and
gestureOpen=false, then poll the original job until its actual terminal outcome.
The existing application scalar service coalesces values and owns the single
history edge. Idle gestures finish after30s; rediscover a terminal/expired owner
instead of reopening it. Cancellation before dispatch uses action=cancel; after
physical dispatch observe the actual outcome. Active recording preserves the
explicit ACK-only/no-history scope and retained-take protections.

Continuous set admissions report nativeApplied/nativeDispatched/historyCommitted
for the exact operation as a whole. An earlier desired value may already have a
real ACK while the newest value is queued. These aggregate flags do not certify
the newest value or terminal completion; end the gesture and poll its job, then
verify its final after value and actual history outcome.

Global Tools Last tweaked uses `vibera_state.audio.lastTweakedPlugin`: null means
no unambiguous completed native gesture. Its stable plugin/runtime/project,
parameter ID, generation and serial identify the target; values are normalized.
Use `vibera_command` with `tools.lastTweaked.controls`, `.create`, `.init` or
`.removeInit`. Controls opens/focuses that same worker's Parameters view with
the exact parameter selected. Every admission returns pending and an operation;
poll `vibera_plugin` action=job to its terminal state. Automation results declare
actual `historyCommitted`, `documentChanged` and `undoable`; acceptance is not a
history entry. Verify the resulting automation/initial value and use normal
Undo/Redo. Pending preparation may be cancelled with action=cancel before it
mutates the document. A newer native gesture, changed project/worker/catalogue,
or disabled notifications invalidates pending global automation instead of
retargeting it. Rediscover after a refusal; do not replay blindly. No target
returns `unavailable_target`; a captured stale UI target returns `stale_context`.
This does not expose arbitrary vendor-native widgets or unsupported controller
bindings, plugin programs or hidden state.

Settings now contains **Native main audio I/O**. Discover the exact current
plugin/instance, then use `vibera_plugin action=buses` to read native bus names,
direction-local zero-based indices and requested/negotiated selections. Another
combination is not guaranteed by that catalogue: native preparation must succeed.
The Mixer graph remains stereo; simultaneous multi-output routing is unsupported.
No-input instruments disable input controls and retain their existing input fields.

The UI's Apply audio I/O and `vibera_plugin action=audioIo` use canonical
`plugin.io`. Supply `plugin`, exact `instance`, and complete `pluginIo`:
`{inputBus:0, outputBus:0, inputChannels:2, monoSource:0}`. Bus indices range0..31
and must exist in the actual worker. Stereo uses inputChannels2/monoSource0;
mono uses inputChannels1 and monoSource1=left,2=right,3=(left+right)/2. Optional
projectIdentity/revision guards reject stale requests. Missing, malformed,
nonexistent or detector-conflicting selections have structured failures.

Poll the returned parent token with action=job. `requestedIo` identifies the
request, `canonicalOperation` identifies its underlying structural transaction,
and `structuralOperation` preserves that child's actual outcome. It can close
editors, capture fresh state and replace workers; a bounded interruption is
accepted. The parent remains observable across that replacement. Only its real
completed terminal plus actual requested/negotiated readback proves success.
Rediscover the new instance before further plugin actions. `documentChanged`,
`historyCommitted` and `undoable` describe the actual history edge. A current-I/O
no-op completes without replacement or history; native refusal preserves the
incumbent layout. Normal Undo/Redo negotiates/restores the corresponding layout.

Use action=cancel with the parent operation. Cancellation delegates to the owned
structural job and remains pending until draining/restoration finishes; a too-late
refusal requires observing the original job, not retrying the mutation. I/O changes
refuse armed/active/retained MIDI or audio automation recording and conflicting
native work. Save/export still captures current state. This is unreleased source
guidance; STATUS identifies the qualified binary and actual verification scope.

## Containing Mixer chain and detector routing (development source)

Wrapper Settings exposes Copy chain, Paste chain, Save chain, Load chain, Chain
directory and Detector routing for **effects**. These address the entire owning
Mixer chain, including Master; instrument wrappers disable them. Individual
plugin/instrument presets and arbitrary programs remain unsupported here.

Discover the effect with `vibera_plugin action=list`. Use `action=chain` with its
exact `plugin`, `instance` and `chainAction=copy|paste|save|loadApply|
configureDirectory|sidechain`. Optional projectIdentity/revision guards reject
stale work. The effect pins its actual insert/slot; later Mixer selection never
retargets the action. Copy replaces the portable clipboard; Paste and Load replace
the whole containing chain through normal canonical history. Save captures current
native state. No snapshot/recovery cache is substituted for an actual fresh ACK.

Configure an existing absolute local `directory` first. Save/Load require an
absolute `.vibfx` `path` under that directory; Save defaults `overwrite=false`.
Only explicit `overwrite=true` permits replacement of that exact path. The UI
retains an explicit Replace/Cancel confirmation for existing files. Directory
configuration and clipboard/file jobs are session state/files, not document Undo.
Do not assume the directory or clipboard persists across process restarts.

For `chainAction=sidechain`, provide `detector=null` to disconnect or the same
typed `{sourceMixer, inputBus, channels}` as canonical plugin.effect.sidechain.
Read native buses and actual negotiated state first. Source is a non-Master
insert, auxiliary input bus1..31 differs from main, and channels is1 or2. Actual
layout preparation requires an active prepared device/source and may refuse;
catalogue names do not promise every combined arrangement. UI choices use those
same existing model/layout services and expose actual bus indices.

Every admission is pending. Poll the parent with action=job: chainAction,
mixer/slot, phase, requested path, editorClosures and canonicalOperation identify
the real work. Closing editors is a structural consequence; each receipt reports
its actual native outcome. After that, structuralOperation preserves the canonical
child through worker/slot replacement. Only the terminal result, actual file or
clipboard readback and intended model/history establish success. Rediscover new
plugin IDs/instances after Paste/Load and Undo/Redo. Native third-party widgets
remain outside semantic coverage.

Use action=cancel with the parent token. Before canonical admission, the parent
must close its owned chooser/confirmation/menu and drain any dispatched editor
requests before cancelled. After admission, cancellation delegates to the actual
child; a too-late refusal means observe the original job. Cancelled native closure
can leave an editor closed; it does not restore a window or invent a document
history edge. Recording/retained takes, stale targets and conflicting work refuse
without silently discarding capture. Brief structural interruptions remain the
accepted product boundary. This source guidance is not a release qualification;
consult STATUS and actual discovery on the executable you use.
