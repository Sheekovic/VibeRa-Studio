# Native instrument/effect windows

Unreleased Mixer-module source guidance. Discover `vibera_plugin` actions and
output schema; the qualified0.0.63 package predates acknowledged native windows.
No arbitrary plugin widget control is implied. Host parameters remain separate.

Read `vibera_plugin action=list`. Select an actual project plugin's stable `id`
and exact runtime `instance`, which changes after recovery/replacement. Then use
`action=openEditor plugin=<id> instance=<instance>`. UI equivalents are the
Channel name button and the instrument/effect menu's native editor action.
An existing native window is reused, shown/restored and brought forward. These
operations preserve project state, transport and Undo. Opening a window does
not prove audible processing or OS foreground ownership.

Acceptance returns `completionState=pending` and an `operation` token. Poll
`vibera_plugin action=job operation=<token>` to actual `completed`, `failed` or
`cancelled`. `opened`, `brought_to_front` and `closed` are successful outcomes;
unsupported/null/failed native views, timeout, worker failure and invalid replies
are explicit failures. `action=closeEditor` uses the same target rules and
acknowledges actual destruction; an already closed editor succeeds idempotently.
`vibera_select pluginView=native/closeNative` routes through the same service and
also returns the job token. Shared Controls selection completes separately.

Read `action=editorState plugin=<id> instance=<instance>` or each list row's
`nativeEditor` for known/exists/visible/minimised/availability/revision/busy.
Revision belongs to its instance. Known=false means unavailable, not proof that
a window is closed. A manual native close can hide a retained editor; reopening
reuses it. Current state can differ from `windowAtCompletion` in a job receipt.
Visible means the native component is showing and not minimised, not that it is
unoccluded on a physical monitor. `nativeWidgetControl=false` remains explicit.

Native completion has a strict5000ms budget from admission, using the same Windows
performance-counter epoch/frequency across the owned host/worker (IPC12).
Timely completion polled late remains valid.
`nativeCompletionKnown`, nullable `nativeOutcome`/`elapsedMs`, dispatched and
window-at-completion distinguish a genuinely late operation from an unknown
timeout. A failure may have native effects; do not retry blindly. Rediscover
the current worker/window state and use the normal saved-state recovery path.

`action=cancel operation=<token>` can win only before native dispatch. `too_late`
preserves the pending operation: keep polling. Project/plugin replacement never
turns an already dispatched side effect into a fictional successful cancellation.
Jobs belong to this session; at most64 are retained and old terminal jobs can
expire. Reconnecting to another application does not restore in-memory tokens.

Physical editor work refuses overlapping state capture, parameter writes and
catalogue refresh; completed unread receipts do not keep these busy. Save/export
must fail/refuse rather than silently skip a live plugin's state. Finish current
file operations before opening/closing an editor. Opening during ordinary live
playback is supported by design and still requires actual current verification;
neither these instructions nor schema presence establish a provider/native pass.

The Channel name button can retain an opening intent while its projection is
preparing or canonical admission is busy. `vibera_state.instrumentOpening` reports
`phase=waiting`, stable Channel/plugin IDs and `projectionReady`; this is not an
accepted native operation or proof of an open window. Its project/descriptor
binding is invalidated by replacement. MCP clients follow the same admission
rules: wait for the matching worker, request `openEditor`, handle explicit `busy`,
then poll an accepted job. A validated `closeEditor` also cancels a queued opening
intent for that target; physical close can still fail busy, so verify actual state.

## Instrument Channel strip in the plugin window

A plugin instrument's window shows its Channel strip in the header: ON (mute), PAN, VOL and
the Mixer TRACK with < / > arrows. Each change runs the ordinary Channel command (`channel.mute`,
`channel.pan`, `channel.gain`, `channel.mixer` through the routing coordinator) with Undo, so
the Mixer track can be set while the window stays open. Routing changes (Channel to insert,
sends) no longer refuse because a plugin window is open: idle windows close for the routing
job and reopen when it finishes.

PITCH (`channel.pitch`, `args.channel`, `args.value` -1..1) sends that MIDI pitch bend to a
plugin instrument with every note start (so it holds when playback starts mid-song; Pattern
pitch-bend events still follow). RANGE (`channel.pitchRange`, 1..48 semitones, default 2) is
the bend span shown with it; set the plugin's own bend range to match (a VST3 plugin decides its
range). Both are saved with the project and undoable; samplers keep their own pitch controls.
