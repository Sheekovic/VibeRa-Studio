# Velocity Graph Editor

Unreleased RPS-C source also supports Pitch; the qualified0.0.64 distribution
does not include this extension. Discover the actual executable before using it.
Set `vibera_rack {"action":"configure","graphVisible":true,"graphProperty":"pitch"}`
through the same service as the graph's property dropdown. Switching properties
finishes the current local drag; an unrelated owned preview refuses the change.
This is transient view state, without musical Undo. Change persistent Solo
preferences in a separate request.

Read `graph.property`, `supportedProperties`, units/defaultValue, and each visible
note's `pitch` and `value`. Pitch uses MIDI0..127 (middle C=60, labelled C5
consistently with VibeRa's Piano Roll), reset60. Discover
canonical `steps.pitch`: `pitch` is an integer, default60; `offset` is the ramp's
ending integer MIDI pitch (supply it explicitly). Targets and quarter-beat
coordinates follow velocity. `set` assigns the target pitches; `scale` transposes
gesture-baseline pitches by requested anchor interval, clamping0..127. `ramp`
interpolates semitones and rounds nearest, restoring targeted values outside a
shrinking ramp from baseline. Chord notes are distinct IDs: explicit `ids` can
target one; a UI column targets all step notes at that onset.

For continuous pitch edits use `vibera_gesture domain=stepGraph`; it accepts only
steps.pitch/steps.velocity on its captured Pattern/Channel, retaining held input
with exact commit/cancel/expiry. Legacy stepVelocity remains velocity-only.
UI left-paint/Ctrl-transpose/right-ramp/Alt-reset and semantic commands share
validation/history. `step`/IDs/timing/velocity/mute remain unchanged by pitch.
Optional Shift-drag provides tenfold finer relative adjustment from the touched
note's current value, without an initial click jump; default gestures stay intact.
The resulting pitch/velocity uses the same semantic scalar commands. This is an
explicit VibeRa convenience, not a claim of observed FL fine-drag mathematics.
No per-note pan/fine pitch/release/Mod X/Y/Shift/Repeat support is implied yet.

```json
{"command":"steps.pitch","args":{"pattern":5,"channel":1,"text":"set","pitch":67}}
{"command":"steps.pitch","args":{"text":"scale","target":7,"pitch":72}}
{"command":"steps.pitch","args":{"text":"ramp","beat":0,"length":0.75,"pitch":60,"offset":72}}
```

Requires0.0.52/contract1. Discover steps.velocity and rack.graph with
vibera_commands(includeSchemas=true). Musical units are quarter-note beats;
one step is0.25 beats. IDs are current-session stable note IDs, never columns.
Normalized velocity0-1 is stored without forced quantization. Positive scored
velocities render through the existing MIDI7-bit rounding/clamp1-127; zero
retains an active step in the project but emits no scored hit. Zero does not
implement FL's separate cut-note behavior. Do not promise unsupported properties.

Show/hide uses the same UI service as Ctrl+K and the Rack graph button:

```json
{"action":"configure","graphVisible":true}
```

Send to vibera_rack. This is session view state, not project data or Undo.
It opens the Rack and follows the active Channel. Use shared vibera_select or
vibera_rack select(activeChannel=...) to change the active editing/MIDI target.
Display filtering alone leaves that target unchanged, even outside the filter.
Graph edits do not change routing, instrument state or Channel selection.

Read vibera_rack(state).graph: selected Pattern/Channel, normalized units,
default0.8, supported velocity-only modes, eligibility/count and visible-column
notes with IDs, beats, column numbers, exact velocity and effective midiVelocity.
Use paginated vibera_inspect(entity=notes,pattern=...) for all notes, including
offscreen ones. Rendering coordinates are not canonical musical data.

All edits use vibera_command steps.velocity, including UI drags and macros:

```json
{"command":"steps.velocity","args":{"pattern":5,"channel":1,"text":"set","ids":[7,8],"value":0.45}}
{"command":"steps.velocity","args":{"text":"reset"}}
{"command":"steps.velocity","args":{"text":"scale","target":7,"value":0.6}}
{"command":"steps.velocity","args":{"text":"ramp","beat":0,"length":0.75,"value":0.2,"offset":0.8}}
```

Omitted Pattern/Channel mean current selected targets. IDs are optional, unique,
maximum100000; omitted/empty means all existing aligned in-loop step-note IDs
in that Channel, including offscreen ones. Explicit foreign/stale/melodic/
out-of-loop/off-grid IDs fail atomically. Empty columns never create steps.
Other Channels, melodic notes, other Patterns and placements are unchanged;
linked clips naturally reference the edited Pattern. Save/reopen retains values.

set assigns value. reset assigns the actual step creation default0.8. scale
requires target to be an eligible targeted anchor note ID; positive anchor uses
the original ratio, zero anchor uses an additive lift, clamped0-1. This is an
explicit VibeRa rule, not installed-FL execution evidence. ramp uses beat as
start and signed length as endpoint span; both endpoints must be in-loop and
aligned to0.25. value/offset are start/end velocities. Reverse/zero-span ramps
are supported. Targeted notes outside a ramp restore baseline values, useful
when shrinking a preview. Untargeted notes remain unchanged.

For a continuous gesture, use vibera_gesture begin(domain=stepVelocity), update
with steps.velocity, then commit or cancel using the returned operation token.
This is the same Controller baseline/validation/history as UI drag: repeated
scale/ramp previews use gesture-start values, not compounded prior results.
Committed drag is one Undo; cancel restores exact starting project/dirty state.
Set/reset can accumulate strokes; each update has atomic validation. Without a
gesture, baseline is the current Project and one command is one Undo.

The narrow domain locks the original active Pattern/Channel and permits only
steps.velocity. It can continue while MIDI is armed or a take is held, without
committing the take. Generic document gestures retain their recording guard.
Changing domain/targets or using other commands fails. Local UI edits may
preempt remote previews; Stop/recording completion is a local document boundary.
Finish the lease before saving/exporting or switching context. Poll state for
actual completed/cancelled/expiry; acceptance of a pending preview is not commit.
Invalid updates do not renew the lease. Local Escape cancels a UI graph drag;
hide, resize, navigation or another local action commits it before the boundary.
Recording Stop keeps the graph transaction separate from the recorded-take Undo.

State capture/export/macro operations must finish before direct graph editing.
Structured errors include invalid_arguments, validation_failed, stale_revision,
gesture_owned and gesture_domain. Rediscover after project/session changes.
Settings Macros includes current-Channel value-based velocity edits; existing
macro/profile services expose the same canonical operation. Continuous mouse
stroke recording into a macro is not promised.

Verify with canonical note readback and Undo/Redo, then actual project/WAV jobs.
Project save/export returns pending until the job completes or fails; inspect
its actual terminal result and output. Audio state is separate from document
commit. Virtual callbacks/PCM do not prove physical hardware or heard audio.
Remaining graph properties, swing, remembered removed steps and full FL behavior
remain open, with inherited MCP consolidation deferred and the full gate failing.
