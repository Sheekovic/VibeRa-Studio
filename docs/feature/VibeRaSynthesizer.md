# VibeRa Synthesizer

This is the original bundled native instrument in development source. It uses
the existing instrument host, worker, parameter catalogue, project state and
history services. It is not a separately installed or exported VST3 plugin.
Source implementation and the checks described here do not qualify a release;
current acceptance authority remains [STATUS](../STATUS.md) and
[VERIFICATION](../VERIFICATION.md).

## Source architecture and native patch path

The native registry creates `SynthPlugin` inside the existing owned Plugin Worker.
It implements the shared `NativeProcessor` bridge used by VibeRa-owned processors
for parameter metadata/readback, timed events, native state envelopes, metering
and patch boundaries. External VST3 providers keep their separate loader branch;
the Synthesizer has no separately installed/exported VST3 wrapper.

The current source pair uses IPC20. A complete preset request supplies all 64
normalized scalar values (the bounded patch ACK holds exactly 64) to the existing native patch/history job. The worker
validates the entire bounded batch and source availability before mutation,
captures actual previous values and returns actual result values in its ACK.
The processor brackets the batch with a sequence counter so DSP retains a
coherent previous snapshot until the complete new patch is available. UI native
parameter gestures and semantic `loadPreset` use the same hosted/native state
model; current-state capture remains required for history, project save and
export. Admission/pending status is not an acknowledged completed patch.

Each processor eagerly hashes/decodes and validates the 20 immutable recorded
maps during CONTROL construction, before the worker starts DSP. The retained
PCM/maps remain owned by that instance; audio voices borrow the selected map.
Only `Content/Synthesizer` beside the executable is read. The manifest's
`pathBase` describes source provenance and does not add a checkout search path.
The 123 original WAV/FLAC files occupy about 170 MiB. JUCE provides runtime
decoding; acquisition tools, NumPy, ffmpeg and network access are developer-only.
Source offsets/tuning and explicit end-exclusive authored loops are prepared
before playback. Source/preset changes have an explicit voice-reset/effect-reset
policy; this source design is not a claim of seamless arbitrary-tail retention.

## Instrument and content

The catalogue declares 77 factory patches: 20 recorded-sample patches, one
recorded/synthesized layer and 56 oscillator patches. The 35 original oscillator
IDs keep their names, genres, families and macro limits but carry hand-voiced
production settings; 21 further synthesis patches cover supersaw, reese, acid,
808-style sine and sub basses, FM keys and bells, synth brass, string ensemble,
warm/dark/FM pads, plucks and leads. The 20 sample instrument
IDs resolve through `assets/synthesizer/library.json`; file provenance and
licenses belong to that library. Factory recordings remain separate from
writable user presets. Missing, unsupported or unreadable content must produce
an explicit load failure rather than an invented successful acoustic preset.

The recorded sources cover upright piano, hardware FM electric keys, violin,
cello, pizzicato contrabass, harp, flute, clarinet, trumpet, horn, trombone,
tuba, marimba, kalimba, balafon, strumstick, six-string clean electric guitar,
conga, bass drum and snare. Labels distinguish the upright from a grand, FM
electric keys from a sampled Rhodes/Wurlitzer, strumstick from a six-string
guitar, orchestral flute from a ney, and acoustic bass drum from an 808.
The hybrid Glass Electric Keys patch layers the upright recording with original
sine/triangle synthesis. Oscillator strings, pads, plucks and percussion are
identified as synthesis. Genre tags suggest uses; they do not establish cultural
origin, alternate tuning or authentic performance technique.

`SynthPresets.h/.cpp` are authoritative for stable preset IDs, 64 stable
parameter IDs/keys, units, bounds, defaults, intervals, source identities and
per-patch macro limits. The shared `presetHasGenre` helper admits pipe/comma
separated tags. Families cover Piano, Electric Piano, Violin, Strings, Bass,
Guitar, Brass, Woodwinds, Synth Leads, Pads, Plucks, Drums / Percussion and
Arabic / World. Both native UI and semantic discovery consume this catalogue.

## Production voice engine

Each voice runs two PolyBLEP oscillators (sine, saw, pulse, triangle, noise) with
optional 1..7 voice unison (symmetric detune, alternating stereo spread, random
start phases), a sine sub octave, white noise and two-operator phase modulation.
Pre-filter saturation feeds either the original 12 dB TPT state-variable filter or
a zero-delay-feedback four-pole ladder with a soft-clipped core; high/band pass on
the 24 dB setting cascade two 12 dB stages. Cutoff follows the amplitude envelope
or a separate modulation envelope, plus key tracking, velocity and per-voice
analogue drift. Both envelopes blend linear and analogue-style curved segments.
Mono and legato modes keep a bounded 32-key stack with last-note priority and
glide. All state is fixed-size per voice; nothing allocates, locks or performs I/O
on the audio thread. Schema 1 states without the appended 18 scalars load with
neutral defaults that reproduce the earlier voice exactly (the engine test asserts
the single-unison voice remains bit-identical mono).

After the effects, Master feeds a fixed +8 dB output make-up and a stereo-linked
peak limiter (instant attack, 80 ms release, -1 dBFS ceiling), so factory sounds
arrive at a usable mixing level without clipping the Channel.

## Native editor

Settings > Plugins includes VibeRa Synthesizer without an external scan. Select
it and choose New Channel or replace an instrument Channel, then open the
Channel's native instrument editor. The instrument has its own visual identity
(graphite panels, amber accent, one colour per module, Bahnschrift display face)
rather than the host workspace theme. A header holds the logo, preset display
with previous/next/favourite, Save/Load, Master and stereo meters. Below it a
fixed module grid (Macros; Oscillators and Unison/Sub/Noise; Filter and Amp
Envelope; Mod Envelope, Voice and Effects) fits without scrolling at 1280x720 and
larger; menus sit in each module header and envelope shapes are drawn there.
Every control has a plain-language tooltip with its default.

The browser column selects All Genres, Favourites or one genre from compact chips.
Search and the instrument-family menu narrow the factory list. Clicking a row
loads its complete patch; arrow keys select a row and Enter loads it. The star
changes a favourite without loading that sound. Previous/next traverse the
current filtered list. Genre, family, search and favourites use the shared user
preference service, independently of project Undo. The editor polls that service
to reflect semantic changes while preserving an active search caret.

Four macro knobs control Tone, Warmth, Space and Motion with per-patch limits:
cutoff range, low-EQ boost/output trim, ambience amount and vibrato depth.
Warmth is not advertised as an additional saturation algorithm. ADSR, filter
type/cutoff/resonance, EQ gains, compressor threshold/ratio, chorus amount,
delay time/feedback/amount and reverb size/decay/amount edit actual hosted
parameters. Each effect has an independent enable switch and settings page.
The oscillator section exposes source, waveforms, blend, tuning, pulse width,
FM, sample level, unison, sub, noise and drift; the voice section exposes voice
mode, glide, polyphony, bend range, vibrato and pan. Master level, pitch/mod
wheels and the playable keyboard remain outside the workspace. Windows smaller
than the module grid scroll it instead of shrinking controls; drawing and
controls use JUCE logical coordinates for DPI scaling. The on-screen keyboard
also lights keys held by host-routed external MIDI (for example a hardware
keyboard); the processor publishes that held-key mask with lock-free atomics.

The source badge reports the current source mode. A modified patch has an
asterisk; its factory name identifies its starting sound rather than claiming
unchanged factory settings. The raw `factory-preset` parameter is labelled
Sound source identity: changing that scalar alone selects the source identity,
not every dependent patch setting. A complete preset load uses all actual
scalar values through the shared host gesture batch. The dedicated preset
browser and `vibera_synth loadPreset` perform that complete operation.

Every knob, menu and enable switch uses native parameter gestures and live
parameter readback. Focus loss, hiding or closing releases held editor notes,
centres the pitch wheel and ends open gestures. Keyboard messages reach a
bounded processor queue; DSP must not call locking keyboard-state methods.
Source/sample preparation and user-file operations run outside audio callbacks.

## User presets and semantic access

Save preset and Load preset use asynchronous file pickers and `.vrsynth` files.
The shared user directory is `%APPDATA%/VibeRa Studio/Synthesizer` by default;
owned tests may use the validated directory override. Save is restricted to
that writable user tree. Errors are displayed, cancelled pickers make no edit,
and successful writes preserve the factory library. Patch loading uses the
existing parameter/history path; opaque state capture remains authoritative
for project save/export and semantic user-preset save.

`vibera_synth` discovers presets and parameters, shares browser preferences,
creates instruments, loads complete patches, changes individual native-unit
values, saves/loads user presets, previews notes and opens/closes the native
editor. Runtime instance/schema guards and existing plugin jobs remain in
force. Pending requests require terminal job readback. See the task-oriented
[synthesizer skill reference](../../skills/vibera-studio/references/synthesizer.md).

Required qualification includes actual native UI interaction/rendering, semantic
and UI state equivalence, parameter/preset Undo/Redo, current-state save/reopen,
recorded/oscillator/hybrid PCM, complete content/license checks, keyboard release,
callback allocation/lock safety, timing and exact package/source provenance.
Human listening, hardware MIDI/audio and familiarity remain separate evidence.
The complete software checklist and exact installed delivery now pass; current
identities, source/native/musical/MCP/moved/staging/final receipts and retained
failures are indexed in [STATUS](../STATUS.md) and [VERIFICATION](../VERIFICATION.md).
The curated library's exact acoustic-content gaps remain in the sound-source
manifest. Human listening and physical hardware are unqualified. The separate
Synthesizer milestone does not qualify or reduce the whole Rack/Pattern/sampler
module or inherited full-product MCP backlog.
