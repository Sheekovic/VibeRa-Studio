# Song MIDI Mixer automation recording

Discover `vibera_automationRecording` on the connected executable. It shares the
Mixer control context menu and `automation.record.arm/finish/cancel` actions.
This internal increment records one insert's gain, pan, stereo separation and
currently existing outgoing send levels from mapped MIDI or explicit semantic
value edits. It does not record arbitrary native plugin UI or native parameters.
Pattern events, loop passes and overwrite/overdub remain explicit required work.

Use Song mode with Loop off and a prepared selected input source/audio clock.
Right-click an existing Mixer gain/pan/separation/send control, then choose
**Record MIDI automation for this insert (Song)**. While stopped this arms; Play
starts capture. While already playing it begins immediately. Existing mapped
controller pickup and relative modes still apply; uncaught input is not recorded.
The recording checkbox/Keep and Cancel context actions bind the original token.
Stop keeps the automation take; Escape cancels its manual preview. UI pointer
knob gestures are refused while this MIDI recorder owns the document: use MIDI
or the shared semantic value operation. No hidden generic pointer record mode.

Read project identity/revision, then call:

```json
{"action":"arm","insert":1,"lane":3,"projectIdentity":"<current>","revision":1}
```

Existing linked automation or initialized values on ANY prepared insert control
or current send refuses before arming. It never removes earlier automation to
make recording appear successful. Maximum64 target keys and65536 accepted events
are prepared; source clock/buffer/device/tempo and mapping changes invalidate the
partial take instead of silently dropping points or retargeting stable IDs.

Read `state` for phase/token/target units, fixed source channel/epoch and accepted
input. State supports offset0..65536 and limit1..512 (default128) for active event
pagination; `pendingEvent` retains an input whose preparation failed. Values are
actual target units, event times are source-clock seconds. After completion use
`vibera_inspect` Automation sources/points/links for canonical full readback.
`targets.manual` is the value captured at arm, not a later meter/DSP value.

A semantic edit shares the canonical manual-control preview and records at the
actual backend source time; caller timestamps/point beats are not accepted:

```json
{"action":"value","token":"<active>","control":"pan","value":-0.25}
{"action":"value","token":"<active>","control":"send","sendTarget":2,"value":0.5}
```

Gain/send use linear0..2, pan/separation use-1..1. Send destination is required only
for send. Values become held-step Automation Clip points, not an estimate of a
smoothed fader's audible output. No interpolation, fitting or arbitrary point
thinning is applied. Equal-time values coalesce deterministically in the source.
Different physical ports touching the same target out of source-time order
refuse as invalid input rather than silently changing MIDI ordering.

`finish`, `retry` and `cancel` require the exact active token. Finish seals existing
producers on CONTROL, prepares one canonical history action, and retains the
complete take if history preparation refuses. An accepted Cancel stays requested (`cancelRequested=true`); retry then retries
cancellation rather than committing that take. Retry is explicit; there is no
automatic repeated history transaction. Invalid clock/loss/capacity/preparation
input cannot be committed as a complete performance: inspect partial/pending data
and cancel. Producer quiescence is bounded100ms on CONTROL; timeout retains the
clock/take and returns failure. No callback waits, allocation or locks are added.

Completed means actual Project/history application with ordinary persisted
Automation Clips, initialization and manual fallback, not audio consumption or
heard settling. `dspSettlingAcknowledged=false`. Cancel restores the owned model
and original dirty state while preserving independently newer native dirty work;
no new history action. Normal Undo uses the application's existing dirty/saved
semantics. Save/reopen retains exact clip/point/link IDs and values.

A genuine MIDI note recorder stays independent: automation never clears its
queues, session, held notes or placement. You may arm MIDI notes first or begin
automation during an existing held note take. Stop seals automation first, then
commits the note take as its own action. One Undo removes notes, the next removes
that automation take. A failed automation transaction retains both for explicit
retry/cancel; ordinary Save/project replacement/other edits refuse while owned.
No hardware, physical MIDI, listening, native parameter recording or whole MX-G
completion claim follows from component/MCP/synthetic-device evidence.

Touch recording with the mouse: while a take is recording, dragging that insert's volume
fader, pan knob or a send level in the Mixer records each value at its time through the same
admission as `value` (no gesture is opened, so nothing else can be edited meanwhile). Stopping
playback seals and commits the take as usual; the recorded source is named "<target> - Recorded".
MCP `vibera_pointer` on the Mixer (editor 3) is admitted during a take for the same reason.
