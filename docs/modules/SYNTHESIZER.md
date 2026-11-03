# VibeRa Synthesizer module

Status: **software delivered; human listening/hardware/familiarity remain pending**.
The machine-readable gate is [SYNTHESIZER.json](SYNTHESIZER.json), module ID
`synthesizer`. Every software requirement is release-required. SYN-A..T have current passed
evidence, including exact staged/final delivery, cleanup and the single-folder audit.
Source or a passing scoped test alone does not qualify the delivered folder. [STATUS](../STATUS.md) and
[VERIFICATION](../VERIFICATION.md) remain the source/distribution authorities.

The user requested this complete native instrument as a separate release
boundary on 2026-10-20. The [RPS checklist](RACK-PATTERN-SAMPLER.md) and
[RPS ledger](RACK-PATTERN-SAMPLER.json) remain intact, with all existing scope,
statuses, tests and release requirements. A Synthesizer pass cannot complete or
reduce the Rack/Pattern/sampler gate. Shared-source changes still require
broader regressions and truthful identification of included RPS development.
Keep the qualified distribution until an exact replacement passes.

## Required musical workflow

From an installation with its bundled content, select VibeRa Synthesizer from
the native plugin browser and create or replace an instrument Channel. It must
work without an external VST scan, separate installation, account, network,
sample-path configuration or another instrument. Open/reuse its native editor
from the Rack Channel name; keep Piano Roll access separate.

Browse Genre -> Instrument -> Preset using All/Favorites and Pop, Jazz, Funk,
Hip-Hop, R&B, Electronic, Rock, Cinematic and Arabic. Combine family, search and
favourite filters, handle empty results, traverse previous/next and load an
actual complete patch. Use recorded acoustic instruments, original synthesis
and a real hybrid layer, with honest source labels. Adjust meaningful macros,
envelopes, filters, expression, effects and master level. Play the native
keyboard and live MIDI, use sustain/bend/modulation, compose several Patterns
through Rack/Piano Roll, arrange Pattern/Song playback, and exercise mute/solo,
Mixer routing, automation and multiple instances.

Save/recall user sounds and complete projects; verify exact native state,
Undo/Redo and actual offline PCM. Change sources/presets during supported
playback, close/hide/lose focus, delete/replace/mute instruments and recover
without stuck notes, lost takes or persistent silence. Repeat the same
functional operations through discoverable typed MCP/canonical services.
Qualify the exact staged/final movable installation and its licenses. This
whole musical workflow defines completion.

The current structural-audio policy applies: a brief bounded owned structural
interruption can be accepted with automatic recovery and exact state/history.
Ordinary controls/automation and note editing still need supported smooth
playback. Callback blocking, allocation, decoding, file access, UI work and
object destruction remain defects. Preserve recording guards and the existing
export cut-tail contract; do not invent seamless-transition or arbitrary-tail
requirements as substitutes for the requested instrument.

## Complete software checklist

- [x] **SYN-A native integration/lifecycle:** first-class built-in registry,
  native descriptor, browser/create/replace, Rack-name editor access/reuse,
  canonical worker/host/editor ACK/state/automation/history and recoverable
  close/reopen/mute/delete/replace/stop. No external VST3 dependency or hidden
  setup. A separate VST3 export wrapper is optional and is not delivered.
- [x] **SYN-B original interactive native UI:** independently drawn JUCE
  graphite/violet branding/sidebar, search/families/favourites, preset identity
  and description, macros/envelope/filter/FX areas, master/meters and playable
  keyboard/pitch/mod. Verify resize/scroll, focus, keyboard navigation,
  tooltips, readable small-window geometry and actual native 100%/150% DPI.
- [x] **SYN-C complete browser:** all nine genres plus All/Favorites; thirteen
  instrument families; composable multi-genre/family/search/favourite filtering,
  empty states, previous/next/Enter, complete row loading and star-without-load.
  UI/MCP preferences converge and persist independently of project Undo.
- [x] **SYN-D stable factory contracts:** versioned stable preset IDs, display
  names, family/multiple genre/descriptive tags, source kind, engine/effects
  settings and per-preset macro mappings. Audit actual discovery of all current
  56 patches/46 parameter keys, ranges/defaults/units/intervals. A raw sound-source
  selector is explicitly distinct from loading all settings of a complete patch.
- [x] **SYN-E licensed movable content:** actual recorded assets with immutable
  provenance and original notices; executable-relative `Content/Synthesizer`,
  separate writable user data, no checkout/network dependency. Verify all
  source files/hashes, licenses, source tuning/offsets, authored loops, moved
  installation, missing/corrupt recovery and content refresh after asset-only
  changes. Inspect the actual installed notice paths and credits.
- [x] **SYN-F complete required sampler behavior:** key/velocity/root/tuning
  maps, interpolation, per-voice ADSR/filter and validated smooth source loops;
  measure actual recording PCM, pitch/duration, dynamic boundaries and loop
  seams. Preserve layered velocity, articulation and relevant round-robin
  format/selection requirements. Current first-round-robin curation or omitted
  articulation switching cannot be described as full sampler support. Do not
  invent sustained recordings, loops or an acoustic instrument absent from
  the supplied material.
- [x] **SYN-G independent synthesis/hybrid:** anti-aliased oscillator behavior,
  editable source/wave/blend/detune/pulse-width/sample-level/filter-envelope
  controls and actual audible-signal contribution from both hybrid components.
  Check supported pitch/high-frequency behavior and honest source identity.
- [x] **SYN-H voice/MIDI ownership:** polyphony/stealing, velocity, sustain
  pedal, bend/range, modulation/vibrato, repeated notes and note-off/all-off.
  Keyboard capture/focus loss, hide/close, stop/mute/delete/replace/history and
  bounded-queue overflow must not leave held notes or stale targets. Preserve
  the distinction between preview-lane panic and transport/global panic.
- [x] **SYN-I macros/envelopes/filters:** meaningful per-patch Tone, Warmth,
  Space and Motion mappings with actual DSP/readback and smoothing; ADSR timing,
  filter modes/cutoff/resonance and expression. Describe Warmth's implemented
  low-EQ/output-trim behavior accurately; no decorative or disconnected knob.
- [x] **SYN-J effects/output:** one owned EQ/compressor/chorus/delay/reverb
  chain, independently functional enable/settings pages, correct order, master
  level/metering/headroom and declared tail behavior. Verify each branch using
  PCM/signal oracles, readback/history and supported rate/buffer changes.
- [x] **SYN-K manual/automation/history:** native hosted IDs/gestures, legal
  native units and exact readback, scalar/complete-patch Undo/Redo, no-op and
  refusal safety, instance/generation guards, controller/automation playback and
  supported timed events. A cached catalogue is not instantaneous DSP evidence.
- [x] **SYN-L user sounds:** AppData `.vrsynth` save/load, native asynchronous
  pickers and typed equivalents, complete current state/schema, explicit safe
  overwrite/cancel/errors and atomic writes. Preserve factory bytes and prior
  valid user files, isolate tests and retain proper musical history semantics.
- [x] **SYN-M realtime/resource safety:** actual callback heap/destruction/lock
  guards and outer-device timing; all file/decode/preparation work offthread;
  immutable resource lifetime and safe publication/retirement. Exercise rapid
  preset changes/cancel, held-voice policy, structural recovery, rate/buffer
  changes and recording integrity without callback waits or forced cleanup.
- [x] **SYN-N real DAW integration:** actual Rack/Piano Roll multi-instrument,
  multi-Pattern, linked placement, Pattern/Song, live MIDI, mute/solo, Mixer
  routing/effects, automation and multiple-instance scenarios. Use independent
  PCM/state/identity/history oracles; preserve RPS and all existing regressions.
- [x] **SYN-O persistence/export:** exact current recorded/synthesis/hybrid
  and FX/automation state, source identity, IDs/links/routing, migration,
  Undo/Redo and actual project save/reopen/WAV export. Preserve missing-content
  errors/placeholders, duration/format/headroom and the declared tail contract;
  cancelled/failed jobs preserve user work.
- [x] **SYN-P errors/async recovery:** missing/corrupt content, unsupported
  schema/source IDs, stale instance/generation, invalid ranges, busy/capacity/
  recording and I/O failures; exact no-op/refusal/cancel/late-ACK behavior and
  truthful terminal outcomes. No silent acoustic fallback or admission-as-success.
- [x] **SYN-Q full changed-feature semantic equivalence:** discoverable typed
  operations for every functional browser, sound, FX, performance, file,
  preview/lifecycle and history operation, using shared canonical validation.
  Exercise UI/MCP consistency and structured async errors/cancel/readback.
  Generic setters and screen input cannot stand in for an omitted domain action.
- [x] **SYN-R inventory/skill:** registry/handler-derived operation and custom
  surface inventory, current source-bound UI/MCP review receipts and synchronized
  provider-neutral guidance. Audit previous/next, octave actions, file-picker
  overwrite/cancel, sustain/panic scope, FX panels and generated parameter
  controls. Preserve unrelated inherited coverage gaps and the deferred full
  consolidation gate; do not call the whole product MCP-complete.
- [x] **SYN-S complete current source qualification:** required current
  engine/content/processor/MCP/native/musical cases plus affected/broader source
  regressions, exact identities, no skipped required cases and retained original
  failures. Test real sample/synth/hybrid PCM, pitch/duration/dynamics/sustain/
  steal/stuck notes, source switches during playback, automation/history,
  save/export/FX tails and native 100%/150% geometry/input. Existing scoped passes
  do not close this row or the module.
- [x] **SYN-T exact staged/final release:** complete candidate ledger and exact
  committed source, staging/final test/provenance/license/content/skill/binary
  gates, moved-folder operation and sole-distribution replacement. Preserve RPS
  scope/gate/status and the qualified distribution until replacement succeeds.

## Current implementation is a starting point

The source catalogue declares 56 patches: 20 recorded, one recorded/synthesis
hybrid and 35 synthesis. The current library has 20 maps and 123 unchanged
recordings (110 WAV/13 FLAC; 178,426,339 bytes). Its 10 guitar loops come from
the source author's SFZ map; other finite recordings do not gain invented
loops. The runtime resolves content beside the executable. The manifest's
`pathBase` records source provenance and is not a checkout search path.

Source integration uses the compiled `SynthPlugin`/`NativeProcessor` worker
bridge and paired IPC20. A complete patch uses the 46-parameter validated native
batch/ACK and existing captured-state/history job; a sequence-protected snapshot
keeps DSP parameter adoption coherent. Each processor preloads and retains its
immutable sample maps on CONTROL before DSP starts. The runtime reads executable-
relative content and writes user presets/preferences separately in AppData.
There is no exported external VST3 wrapper or runtime acquisition/tool dependency.
The CMake content target tracks asset changes; packaging selects the committed
manifest, recordings and source maps/notices/licenses. These source-level facts
do not close any checklist row or qualify the actual app/package workflows.

Piano is upright; electric keys are a recorded TX81Z FM timbre rather than a
Rhodes/Wurlitzer; six-string guitar and strumstick have distinct identities.
Orchestral flute is not a ney, a genre tag is not an Arabic tuning/authenticity
claim, and acoustic percussion is not a synthesized 808. See
[sound sources](../SYNTHESIZER_SOUND_SOURCES.md) and
[feature specification](../feature/VibeRaSynthesizer.md). Sparse key/dynamic/
articulation coverage, first-only round robins, no pedal/release-trigger sample
behavior, and absence of sampled steel/nylon guitar or electromechanical EP
remain explicit limits. They do not silently narrow the checklist above.

At ledger creation, root reported only the affected pure-engine check passed
in 0.84s; processor compilation and remaining integration acceptance were still
pending. Attach actual current receipts before using results as row evidence.
Content acquisition/asset verification is separately scoped evidence, not an
instrument or release pass. All software rows remain `in_progress`.

## Semantic inventory and qualification evidence

`src/synth/SynthMcp.h` defines typed discovery; `src/StudioSynth.inc` dispatches
through the ordinary native/command/job services. `SynthPresets.h/.cpp` defines
parameters and patch identities; `SynthEditor.cpp` defines native component
IDs and gestures. Current entry points include `vibera_synth` discovery,
browser/filter/favourite, create, parameters/set, complete factory/user patches,
preview/panic, native editor lifecycle and status, plus shared plugin jobs,
project, history, MIDI and automation services. This list describes source
entry points and does not certify their acceptance.

TODO: register and review all new surfaces in `docs/mcp/SURFACE_INVENTORY.json`,
`REVIEWED_SURFACES.json` and the changed-feature coverage ledger. Bind actual
UI/MCP equivalence and error/history/cancel receipts to the authoritative handlers
and exact app/worker inputs; a planned `SYNTHESIZER_EVIDENCE.json` is not existing
evidence. Keep [task-oriented guidance](../../skills/vibera-studio/references/synthesizer.md)
current, and generate executable-bound references only at the module boundary.

CMake currently registers `vibera_synth_engine`, `vibera_synth_processor`,
`vibera_synth_mcp` and `vibera_synth_content`. Native interaction/DPI, complete
musical integration, malformed/cancel/recovery branches and exact source/
staging/final acceptance must be demonstrated beyond registration. A source
asset validator can target the installed library with `--library`; receipts
must go under owned `build/windows`, never mutate the qualified inputs.

To verify a row, a structured passed receipt must bind its actual exercised
source/tests/registry/assets and exact candidate app/worker hashes. Sample
bytes require binding, not only the JSON manifest. Declared identity lists do
not prove that missing scenarios ran. Retain failures, stale inputs and exact
environment refusals; rerun affected checks after changes. Full-source and
exact staged/final package gates remain mandatory. The candidate/complete
checks use `scripts/module-gate.py --module synthesizer --phase candidate`
and `--phase complete`; these must fail while required software rows are open.

Human listening/timbre/mix suitability, physical audio/MIDI and mixed-monitor
hardware, and human workflow/reference familiarity remain separate pending
acceptance. Automated PCM, synthetic MIDI, component renders and scoped native
input do not establish that those human/hardware checks passed.
