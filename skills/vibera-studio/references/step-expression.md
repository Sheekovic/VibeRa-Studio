# Step expression, timing and MIDI release velocity

Sampler per-note cutoff/resonance now has its own [Mod X/Y workflow](sampler-modulation.md).
Those normalized0..1 controls reset to.5; do not apply Pan/Fine's signed/reset0 rules.

Development-source workflow. Discover the connected build before use; this is not
in the qualified0.0.65 package. The whole Rack/Pattern/sampler module is unfinished.

Open Channel Rack, select a local sample Channel, show Graph Editor (Ctrl+K), and
choose Pan or Fine Pitch. Native VibeRa Synthesizer and third-party instruments
retain ordinary Pitch/Velocity support; these sampler per-note controls are
unavailable for them. Stored expression can survive replacing an instrument, but
that never means the native/VST3 plugin applied it. No arbitrary vendor UI control.

Pan is normalized -1..1, neutral0: negative moves left, positive right. It balances
the original stereo sides without fold-down or extra center attenuation. Fine
Pitch is cents -100..100, neutral0, separate from integral note pitch and global
Channel tuning. Signed lanes show a center line. Existing gestures remain:
left-drag paints, Shift adjusts finely, Alt resets to neutral, Ctrl adjusts the
opening baseline by an additive offset, and right-drag makes a linear ramp.
Chords, zero-velocity and muted retained steps keep their original IDs/activation.
Raw aligned in-window step cells are targets, including cells dormant under a
short Channel loop. Melodic/off-grid/outside-window/other-Channel notes are not
silently retargeted. Escape restores the owned preview exactly; one completed
stroke is one Undo/Redo. Changing graph property is view state, not musical Undo.

1. Read `vibera_state` and `vibera_rack action=state`; select exact Pattern/Channel
   IDs. Discover `steps.pan` / `steps.fine` with `vibera_commands includeSchemas`.
   Configure `graphProperty=pan|fine` and inspect `supportedProperties`, `supported`,
   `localSamplerTarget`, `minimum`, `maximum`, `defaultValue` and current notes.
   `appliedEligible` is type eligibility, not a DSP acknowledgement;
   `applicationAcknowledged=null` deliberately makes no adoption claim.
2. Call `vibera_command` with `command=steps.pan|steps.fine`, discovered Pattern /
   Channel and explicit raw note `ids` (empty means every eligible source step).
   `text=set|reset|scale|ramp`; `value` and ramp `offset` use the property units.
   Scale requires `target` to identify a targeted baseline anchor. Ramps use
   quarter-note `beat`/`length`, quarter-step aligned; reverse and zero spans are
   supported. Shrinking a preview ramp restores baseline values outside its span.
3. For continuous changes use `vibera_gesture domain=stepGraph`. Updates must
   target the captured Pattern/Channel, and share the canonical validation,
   recording, history and cancellation policy. `stepVelocity` remains velocity
   only. Update is pending until actual commit; cancellation/expiry restores the
   complete baseline. Invalid ranges, duplicate/foreign IDs and unsupported
   native/Audio Clip targets refuse atomically with structured errors.
4. Inspect raw `vibera_inspect entity=notes`: `pan`, `fineCents`, stable IDs and
   `noteExpressionSupported` / `noteExpressionScope`. `noteOccurrences` gives
   the same source expression on Piano loop appearances. Verify every unaffected
   field, then one Undo/Redo. Check `pendingAudioUpdate` separately: canonical
   model completion never asserts that a callback already consumed the update.
5. Save/reopen a disposable project through actual job completion and compare
   raw fields/source IDs. Schema31 stores nonneutral expression sparsely; missing
   fields mean0. Older schemas cannot smuggle these attributes, even0. Older
   readers refuse the newer format rather than drop expression. New steps and
   ordinary MIDI capture default neutral. Supported step macros reuse typed
   numeric arguments and existing profile/known-command compatibility.
6. WAV renders the expression; await completed/failed and inspect the file.
   MIDI1 score export deliberately omits per-note Pan/Fine Pitch, not substituted
   channel-wide CC/bend. The native chooser/status warns, and a completed MCP job
   reports `expressionNotesOmitted` (counts exported occurrences). Pending/failed
   jobs report an unknown count. Use project/WAV when expression must survive.

Imported-score macros use profile12 only when they contain `recordedNotes`.
Discover the canonical `notes.import` schema, then create/update through
`vibera_macros` with its current configuration generation. Payload rows preserve
source ID, Channel, beat, duration, pitch, velocity, step, mute, pan and fine cents
across profile save/load. Sparse input defaults neutral expression/unmuted state.
Import execution allocates fresh runtime note IDs, targets the requested Channel
and converts the imported part to melodic notes; source IDs are metadata, not
an instruction to overwrite existing IDs. No pre-existing note selection is
required. Read the saved recipe, execute, inspect notes and verify one Undo/Redo.
Malformed payloads and older profiles carrying the new payload must refuse
without changing the active configuration. `notes.record` remains excluded from
macros. Profiles without imported payload keep their earlier compatible version.

## Shift timing (development source)

Choose Shift in the Rack Graph Editor. It supports all score instruments,
including native Synth/VST3, unlike sampler-only Pan/Fine. Display percent0..100
defaults0; delay is `shiftPercent * 0.002475` quarter-note beats. At100 the hit is
delayed0.2475 beats. The source step cell, note ID and source duration stay fixed.
The Piano Roll shows logical unswung/unshifted source geometry; inspect
`shiftPercent`, `shiftDelayBeats` and projected `playbackBeat` separately.

Discover `steps.shift`, configure `graphProperty=shift`, then use the same
`stepGraph` lease/history/cancellation workflow. `value` and ramp `offset` are
percent0..100; `text=set|reset|scale|ramp`. Ctrl scale uses the opening baseline:
a positive anchor scales proportionally; a zero anchor adds the requested value.
Left paint, right ramp, Shift fine adjustment, Alt reset and Escape retain the
existing gestures. Only raw aligned in-window step sources are Graph targets.
Changing a sounding note's onset reconciles its score gate; unaffected and held
input voices retain ownership. No blanket transport/device restart is required.

Scheduling evaluates Swing on each raw occurrence, then adds Shift once. Shift
moves both edges; existing truncate-Swing subtracts only Swing from duration.
Pattern/Playlist windows crop or omit late notes without wrapping. Channel-loop
periods and Auto extent use raw sources. Burn materializes raw repetitions with
Shift intact. Melodic conversion, movement, Slice and imports retain Shift, so
conversion never silently disables timing. These boundary/stretch policies are
explicit VibeRa choices; exact FL edge behavior is not runtime-qualified.

Project schema32 stores nonzero Shift sparsely; genuine older notes default0 and
older-schema explicit presence refuses even0. Imported-note macros with nonzero
Shift require profile13; neutral ten-field payloads remain profile12. Discover
the current schema and validate actual saved recipe readback. Older profiles
cannot smuggle the new field. Ordinary numeric `steps.shift` recipes use the
existing arguments rather than requiring a new profile solely for the command.

WAV uses scheduled Shift timing. MIDI score export encodes it in note event ticks
(960PPQ), preserving the existing unswung, uncropped source-part export contract;
it is not an arrangement rendering. For a raw beat0/duration0.25 note,50% produces
ticks119/359 and100% ticks238/478. Pan/Fine omission reporting is unchanged.
Await actual terminal job state and inspect output; do not infer success from
acceptance. Record native input/listening/physical-device checks separately.

## Repeat rates (development source)

Choose Repeat in the Rack Graph Editor. Like Shift, it supports local samplers,
native Synth and VST3 score instruments. Discover `steps.repeat` and configure
`graphProperty=repeat`. `value` and ramp `offset` are integer rate indices0..14;
0 is Off,7 is1/16,10 is1/32,13 is1/64 and14 is1/64 triplet. Read the current
`repeatRate` label and `repeatIntervalBeats`; the index is not a hit count.
The lane shows the last edited step's musical rate; hovering a column inspects
its rate without changing notes. Chords with different rates display Mixed rates.
Left paint, right ramp, Shift fine, Alt Off, Ctrl baseline translation and Escape
use the same `stepGraph` lease. Scale translates the opening rate indices with
clamping; ramps round interpolated indices. Only eligible raw step sources are
Graph targets. Melodic conversion retains the property and its playback behavior.

Repeat does not persist generated note copies. Each raw Channel-loop occurrence
evaluates Swing and Shift once, then subdivides its realized duration at the
selected interval. No hit starts at the exclusive source end; each gate ends at
the next subdivision or source end. Pattern/placement windows admit and crop each
hit independently without retiming the remaining subdivisions. Raw Auto extent,
Channel-loop periods and source IDs stay fixed. Burn retains the metadata. Glue
with equal modes deliberately joins the first source's repeat phase; this edit
is not promised to preserve the old PCM. Unequal modes never silently disappear.
Existing one-shot tails, envelopes, choke and polyphony remain authoritative:
an exported note-off is not a promise that a one-shot sample tail stops there.
Hosted MIDI1 keeps its existing overlapping same-pitch ownership limits; separate
prepared ordinals do not add independent vendor note IDs or arbitrary articulation.
These boundary and gate choices are original; exact FL runtime parity is unproven.

Inspect `repeatMode`, `repeatRate`, `repeatIntervalBeats` and unchanged raw IDs.
Model completion and `applicationAcknowledged=null` do not establish audio-thread
adoption. Await pending application work separately and verify real output. New
Repeat plans are bounded to100000 expanded Pattern/Song occurrences, counting
stored inactive sources conservatively. Hosted scores also have current-block
MIDI packet and active-gate admission. Over-capacity edits refuse before source
or history changes; rediscover and reduce the score rather than changing device
buffers or assuming dropped notes. With no open device, admission uses the host's
existing512-frame/48kHz configuration. Unrepresentable residual timing refuses
with `noteRepeat.precision` rather than publishing a collapsed on/off gate.
Neutral legacy scores retain prior admission.

Schema33 stores nonzero `repeatMode` sparsely. Older files default Off; explicit
presence in an older schema refuses even0. Nonzero imported Repeat requires macro
profile14; Shift-only imports retain13 and neutral ten-field imports retain12.
Discover and read back the saved recipe, run it, verify fresh runtime source IDs,
then Undo/Redo. Ordinary numeric `steps.repeat` recipes use existing arguments.

WAV uses the prepared subdivisions. MIDI score export encodes Shift and Repeat
once in the existing unswung, uncropped source-part contract. At960PPQ, a raw
beat0/duration0.25 note in mode14 has ons0/40/80/120/160/200 and final off240,
with off before the next on at shared ticks. Pan/Fine omission reporting still
counts exported occurrences. Poll save/export to completed/failed/cancelled and
inspect the actual file; accepted requests are not completed results.

## Release velocity (development source)

Discover `steps.release`, then choose Release velocity (MIDI) in the Rack Graph
Editor or configure `graphProperty=release`. This is note-off velocity, not ADSR
release duration. Values are normalized0..1/default0 and round to MIDI bytes0..127;
0.5 becomes64. Local sampler and current native Synth audio ignore it. Other
instruments may respond depending on their implementation and patch. All score
Channels can store it and export MIDI; Audio Clip Channels refuse.

Use `steps.release text=set|reset|scale|ramp` and the same source IDs, gestures,
`stepGraph` lease, exact cancellation and single history transaction described
above. Scale uses a positive opening anchor proportionally; a zero anchor uses
additive lift. Paint, fine drag, right ramp, Alt reset and Escape share UI/application
validation. A release-only edit does not retrigger a held score note: its future
natural off uses the new value. Each Repeat off carries it. Forced deletion,
seek, Stop and panic cleanup use zero. MIDI1 same-pitch ownership limits remain.
Hosted score events use a consistent nearest-sample playback coordinate across
callback partitions. At a shared output frame, existing gates close before new
ones open. A positive gate shorter than a sample still closes after its own on;
it must not become a stuck note. Raw beat/duration readback remains musical source
geometry, rather than a claim of fractional-sample native MIDI delivery.

Read `releaseVelocity`, `releaseMidiVelocity`, `releaseVelocityAudioResponse`,
`midiExportEligible` and `hostedMidiEligible`. Known ignored targets say stored/
export-only. `appliedEligible` is capability eligibility, never proof of audible
response; `applicationAcknowledged` and `midiDeliveryAcknowledged` remain unknown.
Inspect actual prepared/MIDI output to establish delivery, and listening separately
to establish a particular patch's response. Do not infer universal plugin support.

Schema34 stores nonzero release values sparsely; older genuine files default0.
Explicit presence under an older schema refuses even0. MIDI recording/import
preserves explicit0x80 velocity; velocity-zero NoteOn and synthetic boundaries
remain0. Loop-split recorded notes put physical release velocity only on the final
segment. MIDI score export encodes each natural off, retaining the existing
Shift/Repeat unswung, uncropped source-part contract and Pan/Fine omission reporting.

Nonzero imported release requires macro profile15. Omitted/zero release retains
Repeat profile14, Shift13 or neutral ten-field12 as applicable; old profiles cannot
smuggle explicit new fields. Ordinary numeric `steps.release` commands need no new
profile solely for the command. Settings Macros accepts only a finite normalized
number0..1. Save, reopen and export through actual terminal jobs, inspect output
and retain target bytes on failed/declined overwrites. Current STATUS/receipts,
not this source guidance, establish which software checks have actually passed.

These properties are dependencies of full Graph composition, not completion of
Mod X/Y, the whole module or the full DAW. Automated/model/component/MCP and
actual-provider refusal evidence remain distinct from native OS input, physical
hardware, listening and human FL familiarity. Consult current STATUS/receipts.
