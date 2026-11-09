# Sampler envelopes and LFOs

Development-source capability; inspect actual `vibera_commands` discovery before
use. Qualified executable-bound references are generated at module release.
The full Rack/Pattern/Sampler module and exact FL audio equivalence remain open.

Use `vibera_state.channels[].sampler.modulation` to identify a stable local sample
Channel and destination (`volume`, `pan`, `cutoff`, `resonance`, `pitch`). Native
Synth/VST3/Audio Clips do not expose this sampler DSP. Pan provides an LFO only.
Do not infer arbitrary native plugin UI authority from these commands.

Each `channel.sampler.modulation.<parameter>` operation has its own registry-
derived closed schema. Always supply `channel` and `text` (destination). Supply
`enabled` for flag parameters and `value` for numeric parameters. Discover ranges,
defaults and prerequisites; do not send a numeric1 in place of boolean true.

For example, use `vibera_command` with these successive arguments:

```json
{"command":"channel.sampler.modulation.attack","args":{"channel":12,"text":"volume","value":0.05}}
{"command":"channel.sampler.modulation.sustain","args":{"channel":12,"text":"volume","value":0.6}}
{"command":"channel.sampler.modulation.amount","args":{"channel":12,"text":"volume","value":1}}
{"command":"channel.sampler.modulation.enabled","args":{"channel":12,"text":"volume","enabled":true}}
```

Replace12 with the actual stable Channel ID. Enabling with Amount0 is neutral.
Full Volume modulation replaces legacy volume ADSR only while enabled with a
nonzero amount. Delay/attack/hold/decay/sustain/release and attack/decay/release
tension are independent controls. Existing source trim/loop and source-end limits
still apply; a filter release cannot extend the sample or amplitude lifetime.

Times are seconds with Tempo false; quarter-note beats with Tempo true. Changing
the flag deliberately reinterprets stored values in the new units. LFO period is
a whole cycle, not Hertz. For an eighth-note wobble set `lfoPeriod` to0.5,
`lfoTempo` true, a nonzero `lfoAmount`, and `lfoEnabled` true. Shapes0/1/2 are
sine/triangle/square. Negative depth inverts modulation. Global shares phase
across score/MIDI/typing/preview for that Channel; delay/fade stays per-note.
The clock advances with processed Channel audio, including silent processing;
graph/device recreation resets it. No FL stop/seek/global-clock parity is claimed.

Cutoff and resonance add signed envelope/LFO to manual and note Mod values, then
clamp once to0..1. Pitch depth1 is24 semitones; both sources can sum to48. Pan
uses balance, not source movement. Volume positive depth1 follows the envelope;
partial depth blends dry gain and negative depth inverts it. Volume LFO applies
a bounded0..2 gain multiplier. Destination changes use bounded smoothing and do
not restart voices/transport. New curves and units are original VibeRa contracts.

Continuous edits: select the intended Pattern/Channel, begin `vibera_gesture`
with `domain="samplerModulation"`, update the canonical commands with its token,
then commit (one Undo) or cancel (exact baseline). The captured Channel remains
authoritative; local edits, target selection changes or expiry cancel/preempt
through existing ownership. Read actual terminal states. Inspect requested
destination state and `pendingAudioUpdate`; `modulationAcknowledged:null` is
deliberate and is not a false active-voice acknowledgement.

UI navigation equivalents are `sampler.modulation.volume/pan/cutoff/resonance/pitch`
on the selected local Channel; read `samplerWindows[].modulationTarget` afterward.
Existing windows are reused. Navigation is not a musical/history edit. Parameter
commands are Undoable; use `edit.undo`/`edit.redo`, then compare state.

Saved macros accept the same explicit target and typed values via `vibera_macros`.
The Settings macro builder accepts `destination:value`, e.g. `cutoff:-0.25`.
Direct continuous MIDI CC learning lacks an explicit destination binding and is
not exposed yet; do not claim it works. Existing general controller macros remain.
Project37 and later preserve all five targets, compacting untouched defaults while keeping
nondefault and earlier expanded37 settings recoverable. Older genuine projects migrate neutral. A
new-format payload relabelled as an older project refuses. Save/export use real
asynchronous jobs: accepted/pending is not completion. Poll `vibera_project`
`action=job`, verify terminal success and output, reopen and inspect all settings.
On failure retain the original project and inspect the structured error; never
blindly overwrite or retry a failed save/export. WAV contains the actual DSP;
MIDI1 cannot encode these sampler settings.

For one-shot samples, cutoff/resonance/pitch envelopes release their modulation
without ending source playback at key-off. Volume envelopes control amplitude
gating. Enabling Volume after an earlier key-off must honor that off; forced
delete/cut/panic still release the source. Verify actual WAV and live state.

## Sampler Automation Clips

Development-source workflow: discover the actual executable first. The qualified
distribution may not contain these commands. Query `vibera_commands` for
`automation.sampler.create` with `includeSchemas=true`. Its
`samplerAutomationContract` describes destinations, stable field codes, physical
units, ranges, defaults, prerequisites and the discrete interpolation policy.

Use explicit `channel` and `samplerParameter` arguments. The parameter is
`destinationCode * 32 + fieldCode`, never the selected page or a plugin parameter.
Code0 is valid. Volume/cutoff/resonance/pitch support envelopes and LFOs; Pan has
LFO only. Reserved/unsupported codes refuse. Discover codes instead of inferring
them from a UI index. New assignments require a local sample Channel. Existing
links remain dormant and recoverable if that Channel becomes a plugin or Audio
Clip; this does not grant access to the replacement instrument's parameters.

For example, after discovering a local Channel12, create a four-quarter-note
Volume sustain curve (parameter6), then set its physical output range:

```json
{"command":"automation.sampler.create","args":{"channel":12,"samplerParameter":6,"beat":0,"length":4,"lane":2}}
{"command":"automation.sampler.range","args":{"channel":12,"samplerParameter":6,"target":44,"minimum":1,"maximum":0.25}}
```

Replace12 and44 with actual Channel and Automation source IDs from state/readback.
The source ID is not the Playlist placement ID. Creation captures the manual
position, creates one source/placement/link and initializes the Song target.
Source points remain normalized0..1. Range endpoints use the parameter's physical
units; inverted and constant ranges are valid. Boolean endpoints/initial values
must be0 or1, and shape0/1/2. Interpolated flags switch at0.5; shapes round nearest,
with ties upward. Times are seconds or quarter-note beats according to the
corresponding Tempo flag; LFO period is a complete cycle, not Hertz.

Use existing `automation.point.add/move/shape/delete` and Playlist automation
placement commands to edit the curve. `automation.sampler.init` requires an
explicit physical `value`; `.init.remove` removes the Song initialization.
`.link`/`.unlink` use an explicit Automation source `target`. Unlinking does not
implicitly delete the source, placement or retained initialization. Discover and
read those separately. Song playback/export evaluates these curves; Pattern and
Stopped playback use manual sampler settings. Existing Song overlap, initial
value and last-ended-curve rules apply. Stop restores manual values without
resetting the independently owned held-input voices or shared phase clock.

UI: click the Channel name, find Envelopes / LFO and right-click a parameter.
Create, initialize, link, select linked source, unlink and output range use the
same canonical commands. General Automation source properties also identify the
full linked sampler target. Dormant links allow name-only changes and explicit
unlink; no silently redirected Master or plugin edit is allowed.

For semantic linked-source navigation, inspect `vibera_state.editors` and invoke
`view.playlist` through `vibera_command` only if the Playlist is hidden (the
familiar action toggles). Set `vibera_picker` `action=visible, visible=true`, then
`action=type, kind=automation`. Clear `filter` and `groupFilter` with empty `text`,
and use `action=select, ids=[sourceId]`. Read the returned `selection` and Playlist
visibility in `vibera_state.editors`; this must not change notes or history.

Read `vibera_inspect` entities `automationSources`, `automationPoints`,
`automationLinks` and `automationInitialValues`, using the actual source ID where
required. Link kind7 identifies sampler modulation; owner is the stable Channel
and parameter is the discovered code. `channels[].sampler.modulationAutomation`
reports availability. A command's `completionScope=canonicalAutomationTarget`
means document completion, not physical-output acknowledgement.
`dspSettlingAcknowledged=false`, `dspAcknowledged:null` and `pendingAudioUpdate`
must not be relabelled as heard audio. Existing audio Channel diagnostics retain
sampler automation failure count/reason even after automatic recovery.

Every committed curve operation uses shared history. Use `edit.undo`/`edit.redo`
and compare complete link/point/initial state. For a grouped edit begin a document
`vibera_gesture`, pass its token to updates, then commit once or cancel exactly.
Do not steal another gesture. Invalid, stale or unavailable targets must produce
a structured error without partial state/history changes.

Saved recipes containing these typed targets use customization profile17. Keep
`samplerParameter` even when it equals0. Range recipes use public
`minimum`/`maximum`, never internal beat/length fields. Explicit target identities
must be exact numeric integers; no string, boolean or fractional coercion.
Current-context recipes still resolve the actual current Channel through the
existing macro context. Inspect committed configuration and run outcome, then
verify one grouped Undo/Redo. Fixed-target absolute MIDI Learn is described below;
relative/formula/smoothing and Omni/roving remain unfinished.

Project38 stores sampler Automation Clip targets; genuine older projects migrate
without inventing links. Save/open/export are asynchronous: poll the job to its
actual terminal state, inspect the resulting project/file and compare readback.
An invalid project must preserve the current document. A failed/cancelled export,
including sampler automation DSP failure, must preserve a pre-existing WAV.
Never call a queued job completed or automatically overwrite after a failure.

Forced tagged releases keep configured tails; explicit quick panic is separate.
# Project MIDI links and Learn (development source)

These operations are in the current development source, not the qualified0.0.65
distribution. Scoped model/runtime, real stdio persistence and actual component/
virtual-audio checks pass; module/package and physical MIDI acceptance remain
open. Right-click an envelope/LFO
control and choose **Link to controller**. The fixed Channel and destination/field
are captured when the operation starts. Moving hardware identifies one CC; it
does not change the value or complete the link. Confirm **Link**, or Cancel.
Settings > Controllers > **Project sampler links** edits/disables/removes saved
links. Link edits use project Undo/Redo; Save the project separately for durability.
Project39 stores these links; genuine schema38 Automation Clip projects remain
readable without inventing links. Never relabel document publication as DSP or
hardware acknowledgement.

Use `vibera_controller` actions `samplerLearnStart` (`targetChannel`,
`samplerParameter`), `samplerLearnState`, `samplerLearnAccept` and
`samplerLearnCancel` (exact returned `operation` supplied as `token`). Start and
capture are **pending**; only explicit acceptance can complete the project edit.
Accept optionally supplies paired physical `minimum`/`maximum`, `pickup` and the
exact `replacements` project-link IDs shown by readback. Failed/conflicting
acceptance keeps the original document. Read `samplerControllers` from
`vibera_state`/controller state to verify links, availability, effective projection
and errors. A model commit and effective runtime publication are distinct facts.

Canonical `channel.sampler.controller.create/update/remove` use typed
`controllerLink`, `replacements` and existing-link `target` arguments; discover
their current schemas with `vibera_commands`. Code0 is valid. Multiple distinct
hardware sources may address one target; overlapping project sources require
explicit replacement. Project links shadow matching global mappings without
changing global settings. A dormant native/Audio Clip target retains its link and
source reservation, with no fallback to another global action.

This increment supports absolute CC and pickup with ascending physical ranges.
Incoming MIDI channel0 means a source wildcard; it does not enable target Omni.
Performance-port sustain64 and panic120/123 remain musical controls and cannot be
learned here; Control-port CC keeps its normal mapping role. Physical MIDI devices
are not configured by these operations. Diagnostic injection remains test-only.
Project-link configuration is excluded from global macro profiles until an
explicit safe payload contract exists; linked sampler parameter changes retain
their existing canonical command/macro behavior. Relative/formula/smoothing,
roving/Omni and arbitrary third-party native widget learning are not claimed.

## Source fades (project42)

`channel.sampler.fadeIn` / `channel.sampler.fadeOut` take `value` in seconds of
source time, 0..10 (0 = off). Fade in ramps linearly from the playback start of
the trimmed region (the region end when Reverse is on); fade out ramps to zero over
the final seconds before the playback end. They behave like precomputed fades:
looped audio that passes the faded points dips too. Independent of the volume
envelope; applies to subsequent notes. Read back `fadeIn`/`fadeOut` in the Channel
sampler state. Out-of-range values fail with `samplerFade.invalidValue`.

`channel.sampler.loopCrossfade` (`value` seconds, 0..10) smooths forward and
reverse loop wraps by blending the source just outside the loop into its end.
It is limited to half the loop length and to the source available before the
loop start (after the loop end when reversed); ping-pong loops ignore it.

`channel.sampler.hqInterpolation` (`enabled`) switches pitched source reads from
linear to 4-point Hermite interpolation (lower aliasing/error, slightly more CPU).
`channel.sampler.loopPoints` sets `beat`=loopStart, `length`=loopEnd (fractions
0..1) and `enabled`=Use loop points in one Undo step. Sampler readback reports
`fileLoopStart`/`fileLoopEnd` (fractions) when the WAV has an smpl loop, else null;
pass them to loopPoints to use the file's loop (UI: Use file loop).
`vibera_transport` `{"action":"previewNote","channel":1}` plays one note through the
Channel's current settings while stopped (pitch defaults to the root; seconds
0.05..4, default 1.5), the same as the sampler window's Preview processed button.

## Envelope presets

`vibera_project {"action":"saveEnvelopePreset","channel":id,"name":"Pad"}` stores the Channel's
volume envelope (on, attack, decay, sustain, release) as a named preset;
`{"action":"loadEnvelopePreset","channel":id,"name":"Pad"}` applies it as one Undo step through
`channel.sampler.envelopeApply`; `{"action":"envelopePresets"}` lists the names. UI: Channel menu >
Envelope presets. Missing presets, bad names and out-of-range values are refused.
