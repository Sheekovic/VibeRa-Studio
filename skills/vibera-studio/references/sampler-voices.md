# Sampler cut groups and polyphony (0.0.56)

Use discovery from the connected executable as authority. These commands apply
only to sample/built-in Channels, not Audio Clips or native VST3 instruments.
Read `vibera_state.channels[].sampler`: `voiceControlsAvailable` is explicit;
`voiceCeiling` is 128 for supported Channels, null otherwise. Target stable
Channel IDs with a current project identity, not display rows or names.

## Practical hat workflow

1. Load your open and closed samples using `channel.source` (discover arguments).
2. `vibera_command` with `command="channel.sampler.cut"`,
   `args={"channel":closedId,"value":1}`.
3. `channel.sampler.cutBy` with `channel=openId,value=1`.
4. Enter steps through `steps.set`, or Piano notes through `notes.add`.
   Playback/export should release the open hat when the closed hat triggers.
5. Read actual state, save using `vibera_project`, wait for its real terminal
   job result, reopen, and export a WAV. An accepted/pending job is not success.

`channel.sampler.cutItself` with `channel=id,enabled=true` assigns a matching
nonzero pair unique among other Channels; repeated invocation is idempotent.
`enabled=false` clears both. Allocation of the smallest free group is VibeRa's
original policy. Failure when all 99 are occupied is atomic. Same-Channel
self-cut originates from steps; Piano notes do not self-cut. Cross-Channel
Piano and live notes can trigger another Channel's Cut-by group. Cutting uses
the voice's captured release envelope; it is not a hard mute.

`channel.sampler.maxPoly` takes `channel=id,value=0..99` (integer).
Zero means Auto, bounded to 128 combined score/MIDI/typing/preview voices per
Channel. Values 1..99 force the oldest voice out when necessary, including
release tails. Max poly 1 is not a Mono/portamento synthesizer mode.
Cut/Cut-by take integer 0..99; 0 means None. Defaults are zero. Unsupported
instruments, malformed values or stale targets fail through structured errors.

## History, continuous controls and timing

Each command uses the same validation/state and musical Undo/Redo as the UI.
Use `vibera_batch` for one atomic multi-Channel edit. For a continuous preview,
select the intended Channel through `vibera_rack` then begin `vibera_gesture`
with `domain="samplerVoices"`. Update only the four voice commands; target is
bound at begin. Commit creates one Undo step; cancel/real lease expiry restores
baseline. Keep the operation token and verify completionState. Recording-safe
parameter previews do not authorize unrelated score/file changes.

Score cuts are scheduled at sample offsets and honor linked Song placements
and offsets. Simultaneous score events apply old-voice cuts before new note-ons;
this deterministic tie rule is independently designed. Live input cues are
coalesced at callback boundaries, ordered by global trigger ordinal; later
same-quantum live notes survive older cues. Do not promise sample-accurate
cross-device live input. Ordinary parameter changes reuse sample/graph state.

Controller profiles can map Cut/Cut-by/Max-poly as integer parameters: range
0..99, with existing pickup/relative modes. Cut itself is a button action.
Use `vibera_controller` discovery/configure/learn and read state after changes;
virtual injected tests are distinct from physical keyboard qualification.

Projects write schema14 and migrate actual older files to zero defaults.
Never relabel modern files as older versions; the parser rejects new audible
fields under old schemas to prevent silent loss. Native third-party plugin UI,
voices and internal polyphony remain under that plugin's supported interface.

## Development sampler voice modes

The Channel-name instrument window exposes Mono, Porta, Full porta, Slide time
and Gate beneath the existing Cut/Cut-by/Max-poly controls. These are local
sampler controls; they do not control voices inside native/VST3 instruments or
Audio Clips. The qualified distribution remains identified by STATUS.md.

Discover `channel.sampler.mono`, `.portamento`, `.fullPortamento`, `.slideTime`
and `.gateBeats` with `vibera_commands` and `includeSchemas=true`. Execute through
`vibera_command` with an explicit stable `channel` and `enabled` for the flags,
or `value` in seconds for Slide time and quarter-note beats for Gate. Read the
requested values under `vibera_state.channels[].sampler`; completion scope
`canonicalSamplerVoice` proves document application, not a sounding voice or
settled DSP. `voiceModeAcknowledged=null` is intentionally not an audio ACK.

- Mono, Porta and Full porta are independent flags, initially off. Mono releases
  logically held predecessors across score, MIDI, typing and preview through
  captured tails; it overrides the user Max-poly number while retaining the128
  active-voice safety ceiling. It never resurrects an older suppressed key.
- Overlapping Mono notes slide pitch, signed pan and note Mod X/Y bases over
  Slide time. Porta also uses the current foreground or remembered scalar target
  after a gap. Slide defaults to0.06s, range0..10s; the UI displays milliseconds.
  Zero is immediate. VibeRa uses a finite linear playback-rate transition and a
  fresh incoming sample/envelope. Mid-glide edits preserve the original end time.
- Gate is the maximum logical note duration, range0..64 quarter-note beats;
  zero means Off. Current tempo determines remaining musical duration. A normal
  Gate key-off uses the active volume/release behavior: an envelope-disabled
  one-shot can continue sounding after its logical key-off.
- Full porta exempts notes admitted with Porta enabled from Gate, including an
  initial note without an earlier seed. Gate/Full-porta and Slide duration are
  captured for a new note/transition; ordinary edits do not reset already-held
  voices or restart an existing transition.

Each accepted command uses shared canonical validation and Undo/Redo. Continuous
edits use the existing `vibera_gesture` domain `samplerVoices`; capture the returned
operation token, update only the original Channel, then commit or cancel. Updates
remain pending; commit creates one Undo action. Expiry cancels and restores the
baseline. An armed MIDI take is retained; unrelated gesture-domain edits refuse.
Never interpret armed recording or state readback as physical MIDI evidence.

Existing scalar macros and global Controller mappings can target these commands.
Boolean CCs use0..1 and threshold0.5; Slide/Gate mappings use seconds/beats. These
five controls are not destinations in the separate project modulation-CC or
Automation Clip registry in this increment. The destination-aware envelope/LFO
Learn workflow remains available for its documented targets.

Example: discover the target Channel, set Mono enabled, set Slide time `value:0.12`,
then place overlapping ordinary notes through the existing note commands. Set
Gate `value:0.5` for half a quarter-note and observe state. Save via
`vibera_project`, poll the returned operation until actual completed/failed, then
reopen and inspect the same stable Channel. Export a disposable WAV and await its
actual terminal job outcome to verify audible settings; no model ACK proves sound.

These are independently implemented VibeRa policies. FL's documented Mono/Porta,
Gate and Full porta controls inform the workflow; exact FL defaults, glide curve,
phase/retrigger/key-return behavior have not been observed. FL Gate's STEPS:TICKS
hint and fully-clockwise-Off convention differ from the explicit beat/Off value
here. Per-note Porta inversion, silent Slide events and color-group Mono remain
open parts of the complete module, not capabilities supplied by these five controls.
