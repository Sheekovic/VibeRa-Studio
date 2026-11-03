# VibeRa Synthesizer

Discover the connected executable's tools first and consult STATUS for its
exact qualification. VibeRa Synthesizer is a bundled native instrument; it needs no
external plugin scan, installation, account or network. A separate VST3 wrapper
is optional roadmap scope and is not delivered here.

## Discover and create

1. Call `vibera_synth` with `action=list`, `genre="All Genres"`, `family="All"`,
   `search=""`, `favoritesOnly=false` for the complete transient factory view.
   Omitting filters inherits saved browser preferences. Read stable IDs, genre tags,
   families, recorded/synthesis/hybrid source descriptions, macro mappings and
   parameter keys/units/ranges/defaults. Do not guess IDs from display names.
2. Use `action=descriptor` when a canonical native descriptor is needed. Use
   `action=create` to create an instrument Channel through the ordinary shared
   command. A direct completion returns Channel/plugin IDs; a pending structural
   response has an operation/pollTool. Observe that exact operation to terminal,
   then inspect Channels to identify the created instrument. Do not retry create
   while its admitted structural operation is still pending.
3. Creation acknowledgement is not audio readiness. Read `vibera_plugin
   action=list` until that plugin has a current ready instance, then read
   `vibera_plugin action=refresh, plugin=<ID>, instance=<exact instance>`, poll
   its plugin job, and read `vibera_synth action=parameters` with that plugin.
   Retain the current instance and actual catalogue generation for guarded edits.
   Parameter readback is a catalogue snapshot; repeat explicit refresh before
   asserting values after a mutation. Missing content
   or a failed worker is a failure, not a compatible silent instrument.

To assign Synth to an **existing Channel**, discover `vibera_commands` with
`command="plugin.instrument.replace", includeSchemas=true`, get the canonical
XML from `vibera_synth action=descriptor`, and read the stable Channel ID from
`vibera_inspect entity=channels`. Execute `vibera_command` with
`command="plugin.instrument.replace"` and `args={"channel":<existing Channel ID>,
"text":<returned descriptor XML>}`. The Channel ID goes in `channel`; `text`
contains the returned XML. Observe the returned exact structural operation
through `vibera_mixerchain action=job` when pending, then wait for
`pendingAudioUpdate=false`. Rediscover that Channel's new `plugin` ID and its
ready instance and refresh parameters before editing. Assignment shares project
Undo/Redo and replaces the preceding instrument; rediscover plugin/instance
identities after assignment or history. The bundled native descriptor needs no scan.

`action=list` accepts genre, family, search and favoritesOnly for a transient
view. `action=browser` reads persisted browser state. `action=filter` writes
the same genre/family/search/favoritesOnly preference service used by the
native editor. `action=favorite, preset=<stable ID>, enabled=true|false`
changes a favourite. Read back the returned state and matching rows. Browser
preferences do not edit the musical document or add project Undo. UI labels
use Favourites; the semantic fields are `favoritesOnly` and `favorites`.

The catalogue currently contains 77 stable patches: 20 sampled, one sample/synth
layer and 56 synthesized, across 20 recorded maps, nine genre groups and thirteen
families. There are 64 native parameters; 63 support individual `set`, while the
factory source-identity parameter belongs to complete patch loading. Always read
counts and keys from discovery rather than relying on these numbers. Source labels
are explicit. Genre tags do not promise acoustic authenticity or alternate
tuning. Use the returned source/description rather than calling every patch
a sampled piano, guitar, string instrument or drum kit. In particular, recorded
FM Electric Keys is an FM-keyboard recording; Strumstick is not a six-string
guitar or oud. Genre labels do not establish Arabic tuning/articulation coverage.

`action=navigate` shares `effectPage`0..4 (EQ, Compressor, Chorus, Delay,
Reverb), `octave`-1..4, `scrollX`/`scrollY`/`presetScrollY`0..10000 and
`selectedPresetId` (a factory stable ID or empty string). Read `browser` to verify
saved values; a live editor clamps scroll positions to its actual layout.
Selecting/highlighting a row does not load it. Octave changes the GUI keyboard's
available range beginning at36+12*octave, releases held GUI notes and leaves
stored score pitches unchanged. These are preferences without project Undo.

## Change sound and retain history

For a complete factory patch, call:

```json
{"action":"loadPreset","plugin":123,"instance":"CURRENT_INSTANCE","generation":1,"preset":"pop-velvet-upright"}
```

Replace example identities and generation with current discovery. The response
is pending and includes an operation. Poll `vibera_plugin action=job` with
that operation until completed/failed/cancelled; cancellation uses the existing
plugin job service. Inspect actual parameter readback and project revision,
then verify `edit.undo`/`edit.redo` for a history scenario. Read `nativeApplied`,
`historyCommitted`, `documentChanged` and the actual terminal outcome; an
identical patch may be a truthful no-op. Admission alone is not a successful
preset load or committed history. Cancel the exact operation through
`vibera_plugin action=cancel`; a dispatched request may report `too_late`, in
which case continue observing its original operation rather than assuming it
was cancelled.

Factory patch load submits every actual patch scalar through the existing
native ACK/current-state/history service. The native preset browser uses the
same hosted-parameter batch. The raw `factory-preset` parameter is only a sound
source identity; the dedicated `set` action refuses that key. Use loadPreset
for a complete patch rather than changing a selector and assuming every knob
has reset. For `previous`/`next`, supply plugin, current instance/generation and
the current factory `preset` identity. Both actions use the persisted composed
genre/family/search/favourite filter in factory order and wrap. If `preset` is
omitted, they use the shared highlighted ID, which can differ from the sounding
patch. A hidden current ID chooses the first/last matching row; an empty filter
returns `synth.emptyFilter` without changing sound.

For an individual edit, call `action=set` with plugin, exact instance,
generation, a discovered key and a value in that parameter's native units.
For example, `key=attack-ms, value=25` is milliseconds, not normalized 0..1.
Values must meet the discovered range and discrete interval. Poll the returned
plugin operation and verify actual scalar readback. The generic plugin API
still uses its native parameter ID and normalized value; do not interchange
these two value domains. Stale instance/schema, unsupported source, invalid
range and busy/recording/history refusals must remain explicit. Sample/hybrid
source requires a base with a prepared recorded map; load an actual sampled
factory patch before switching `source-type` to1 or2. Native-unit JSON values
use the discovered precision; the service permits only endpoint round-trip
precision and does not authorize general out-of-range tolerance.

### Production engine controls

The 18 appended keys extend the voice; their defaults reproduce the earlier
voice, so older projects and `.vrsynth` files load unchanged:

- `unison-voices` 1..7 (integer), `unison-detune-cents` 0..100, `unison-spread`
  0..1: stacked detuned copies per note with stereo width. CPU grows with
  voices x unison; reduce `polyphony` for 5-7 voice stacks.
- `sub-level` (sine one octave below oscillator 1), `noise-level` (white noise
  before the filter), `analog-drift` (per-note random pitch/phase/cutoff).
- `fm-amount` 0..1: oscillator 2 phase-modulates oscillator 1. Use sine
  waveforms and integer `oscillator-2-semitones` ratios for keys and bells.
- `filter-model` 0 = 12 dB state-variable, 1 = 24 dB saturating ladder (the
  ladder applies to low pass; high/band pass cascade two 12 dB stages).
  `filter-drive` 0..1 saturates before the filter. `filter-key-track` 0..1 and
  `filter-velocity-octaves` 0..4 move the cutoff with pitch and velocity.
- `mod-attack-ms`, `mod-decay-ms`, `mod-sustain`: a separate envelope that drives
  `filter-envelope-octaves` and FM depth. `mod-decay-ms=0` means cutoff and FM
  follow the amplitude envelope (the earlier behaviour); it shares the amp release.
- `envelope-curve` 0..1: 0 keeps straight ADSR segments, 1 gives analogue-style
  curved attack and exponential decay/release for both envelopes.
- `velocity-to-amp` 0..1 (1 = fully velocity-scaled), `glide-ms` 0..2000,
  `voice-mode` 0 poly, 1 mono (retrigger), 2 legato (one voice, no retrigger,
  returns to the newest still-held key). Mono/legato ignore `polyphony`.

Tone/Warmth/Space/Motion use per-preset mappings. Warmth uses low-EQ boost and
output trim when EQ is enabled; it is not an additional distortion stage.
Space raises the wet levels of enabled delay/reverb. Their bypass switches
remain authoritative, so enable the desired effect before increasing Space.
EQ exposes
grouped low/mid/high gains. Changing filter/source/waveform/effect settings
does not establish that a factory patch remains unchanged; the native editor
marks modified values and reports the current source mode.

## User files, preview and editor

`action=saveUserPreset` requires plugin, exact instance and a plain `name`
basename without an extension/path. It captures current native state before
writing `<name>.vrsynth` in the shared writable Synthesizer directory. Poll the
pending capture/write job and inspect its terminal result. An existing file
requires explicit `overwrite=true`; overwrite preserves that user preset's
independent stable UUID. The factory sample library is read-only.

`action=listUserPresets` returns managed presets and per-file errors. Retain each
stable user `id`, base factory ID, source/family/macro metadata and managed path.
`action=loadUserPreset` uses the same guarded plugin/instance/generation and
either the listed stable `preset` UUID or a plain root `name`, never both. UUID
lookup supports managed nested files and survives renaming/moving within that
directory. It validates the saved state and loads actual scalar values
through the patch/history job. Missing, malformed, oversized or unsupported
files fail; do not turn a file-exists check into a successful load claim.
The strict schema1 wrapper keeps its own name/UUID, base/source/macros and all
normalized native values (files written before the 18 appended keys still load
with neutral defaults). Arbitrary imported sample paths or custom macro
definitions are unsupported. Listing is bounded, rejects linked paths and reports
malformed/duplicate identities. The editor's Save/Load buttons use that same
format and managed directory. Native chooser interaction/cancellation is not
qualified by a semantic file-service test. Cancellation is not a document edit.
Runtime state capture remains
required when saving a project or exporting audio after edits.

`action=previewOn` accepts plugin, MIDI note 0..127 and velocity 1..127.
Send the matching previewOff, or `action=panic`, when finished. These operate
the transient preview lane; they do not insert score notes or establish MIDI
recording. Preview panic is not a global transport panic. Use actual rendered
PCM or the existing export job to verify output; this does not establish human
listening or hardware playback. Every previewOn must be paired with previewOff
or panic, including failure/cleanup paths. Output passes Master, a fixed +8 dB
make-up and a -1 dBFS peak limiter, so rendered peaks never exceed about
-1 dBFS; lower Master (not the limiter) to leave mix headroom.

`action=openEditor`/`closeEditor` reuse the established asynchronous editor
service. Poll its operation and inspect `vibera_plugin action=editorState` for
actual visibility/readiness. The native keyboard, pitch/mod wheels, filter,
ADSR, macros, effects, source controls and master expose actual parameters;
pointer simulation is not their semantic equivalent. Browse/favourite actions
are settings, while sound edits use document state/history. Do not claim OS
input, audio quality, third-party compatibility or release qualification from
component presence alone.

`action=status` adds `synthesizer.available`, `activeVoices`, `presetId`,
`sourceKind`, `contentReady`, `sampleAvailable`, `parameterError`, `loadStatus`
and an explicit `scope`. This is the last processed native block, or the initial
prepared patch before processing. Ordinary `--mcp-test` has no live callback
driver: transport state or a prepared snapshot cannot prove that a newly loaded
patch has processed live audio. Use actual offline PCM for render acceptance;
live source/voice/meter checks require the separate native-meter fixture.

Factory content resolves from `Content/Synthesizer` beside the installed worker,
with bundled source licenses. User presets/preferences normally resolve to
`%APPDATA%/VibeRa Studio/Synthesizer`; read `userDirectory` rather than assuming a
developer override will work in a packaged application. Installed smoke checks
must use transient discovery and disposable projects without changing real user
favourites, browser/navigation preferences, user presets or opening the editor.
Save/open/export jobs and installation-move acceptance remain separate from
component callbacks, native OS input, hearing and physical MIDI/audio hardware.
