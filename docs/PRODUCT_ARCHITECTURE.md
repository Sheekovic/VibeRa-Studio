# Product and architecture decisions

## Approved product
Free public Windows x64 distribution with corresponding source. Familiarity for experienced FL Studio users is as important as reliability. Default workflow: Browser -> Channel Rack -> Piano roll -> Pattern/Song -> Playlist -> Mixer -> save/reopen/export. Multi-instrument patterns share project-wide Channels; Playlist lanes and Mixer inserts are independent.

## Foundation
C++20 + JUCE + Tracktion Engine + CMake/MSVC, subject to musical M1 qualification. LMMS offers existing composition UI but inherited behavior and recording/plugin gaps; Ardour offers recording/mixing but substantial pattern-workflow and Windows build changes; lower-level miniaudio would require most DAW engineering. Tracktion supplies reusable sequencing/audio/routing/rendering, not FL interaction parity.

Initial baseline: Tracktion 4536d8a21664fe6ec2aa34b25abc87fa2a0d3b86 and its exact JUCE submodule. Any integration adjustment must be recorded with evidence.

## Subsystems
- Canonical document: Channels, Patterns, placements, Mixer inserts, audio assets, automation sources/placements, stable parameter targets, tempo, meter, tuning.
- Commands: stable IDs, typed parameters, scope, eligibility/preconditions, undo transactions, completion/failure. Menus, shortcuts and later macros invoke the same operations.
  Internal typed document helpers can participate in compound commands/macros while `userAction=false` keeps them out of shortcut, MIDI and generic action pickers. A data declaration is not a musical action or permission to write a runtime plugin.
- UI: original visual assets and contextual gestures/window navigation. Familiar terminology and defaults. The authorized 0.0.2 redesign uses docked native JUCE editors and message-thread splitters; these hold no musical data. UI scaling changes viewport density and panel bounds, not engine timing or project serialization. See UI_REDESIGN.md.
- Engine adapter: derived Tracktion Edit; Channels render separately from Mixer inserts. Repeated pattern placements expand to per-Channel clips. Generated engine state is not another editable document.
- Audio: prepared graph and built-in DSP. No VibeRa callback I/O/allocations/locks/UI/destruction.
- Workers: media preparation, thumbnails, persistence, rendering and plugin control. Message thread owns document edits and engine operations that require it.
- External plugins (M3): worker-process containment, shared-memory buffers, asynchronous control/state, explicit extra-block latency. Never wait on workers from the audio callback.
- Bundled native processors: registered VibeRa effects and Synthesizer use the
  same owned plugin worker, parameter/state/editor services and audio packet
  transport through `NativeProcessor`. Their DSP and UI are compiled into the
  worker; external VST3 loading remains a separate branch.

## Bundled Synthesizer architecture

`src/synth` separates the pure voice/sample engine, immutable prepared sample
maps, preset/parameter catalogue, JUCE processor adapter and original native
editor. The current source catalogue has 56 patches: 20 recorded, one real
recorded/synthesis layer and 35 synthesis, with 46 stable hosted parameters.
Native discovery and semantic MCP use the same catalogue and browser/user-preset
services. A separate exported VST3 wrapper is not part of this implementation.

Each `SynthPlugin` preloads and validates the bundled sample maps on CONTROL
during worker construction. PCM decoding, hashing and map allocation finish
before DSP starts. The processor retains the immutable maps; callback voices
borrow the selected map. The factory library currently contains 20 maps and
123 original WAV/FLAC files, about 170 MiB on disk. This is eager per-instance
preparation, not a streaming or downloaded-on-demand library. Only executable-
relative `Content/Synthesizer` is searched. AppData holds user presets and browser
preferences separately. No external scan, account, network or manual sample
path is required for the bundled instrument.

The current source pair uses IPC20. Complete patch loads submit all 46 normalized
parameters through the bounded native patch mailbox, validate the whole batch,
return actual previous/result values in one worker ACK and feed the existing
captured-state/history job. A sequence-protected processor snapshot prevents
DSP from adopting a partial patch. Single-parameter controls and timed automation
retain their existing services. Current native state uses the versioned
`VIBERA_SYNTHESIZER` envelope; project save/export still captures current worker
state through the shared host.

These are source-level architecture facts. Initial engine/processor/content
checks do not establish application, native UI/MCP, full-source or packaged
qualification. The complete [Synthesizer module](modules/SYNTHESIZER.md) remains
open independently of the preserved [RPS module](modules/RACK-PATTERN-SAMPLER.md).
See [feature behavior](feature/VibeRaSynthesizer.md) and
[recorded-source provenance](SYNTHESIZER_SOUND_SOURCES.md).

## Graph execution

Current graph execution policy (2026-10-15): EnginePolicy configures one calling
CPU, which the pinned Tracktion playback/render paths map to zero auxiliary graph
workers. Prepared nodes execute on their caller instead of awaiting graph jobs.
Isolated asynchronous VST3 workers retain their existing model. This is a
reliability boundary for the current graph, with reduced parallel capacity for
large sessions; future multicore graph work requires a bounded scheduling and
latency design plus measured acceptance. It does not narrow the full DAW scope
or establish that all callback locks/allocators are audited. No user buffer or
priority change is used. [Comparison and limits](CALLBACK_STALL_DIAGNOSTICS.md).

## Persistence

Current schema17 retains stable note identity/mute and versioned Channel/Pattern/
placement/automation data. Selection time stretching0.0.62 uses existing note
onset/duration fields and the canonical command/history model; no project or IPC
format change is introduced. Development evidence is in PIANO_NOTE_STRETCH.md;
STATUS/VERIFICATION distinguish qualified distributions from source candidates.

Historical schema10 added optional bounded Pattern filter-group metadata;
schemas1–9 migrate without groups and older readers reject10. This is display
organization, never routing or a playback boundary. See CLIP_PICKER.md.
Versioned project.vibra document with collected media. Atomic writes, source-aware clip offsets, copy-based migrations, missing-asset placeholders. M2: dirty autosave every 30 seconds, ten retained snapshots. User shortcut/macro profiles are separate versioned JSON settings.

## Shortcuts and macros
M1 command registry; M2 Settings > Shortcuts & Macros. All registered bindings can be changed, removed or reset; profiles support import/export. Editor context takes precedence over global fallback; validation and profile import follow that rule. Conflicts within a context are rejected, while documented Browser F2/F3 can coexist with global Pattern rename. Existing customized global bindings are protected during migration. Named user macros sequence semantic actions with parameters, not screen coordinates; optionally record supported commands. Document-only macros form one undo transaction; mixed macros have explicit undoable segments and non-undoable boundaries. Cancellation, failure reporting and recursion/concurrent-run prevention required. No arbitrary scripting in the first version.

## Semantic MCP and reusable AI skills

Every functional UI entry point needs semantic MCP equivalence through the same
canonical application commands, validation and Undo/Redo; custom gestures and
settings are included. CommandInfo carries an explicit MCP mapping, defaulting
to unmapped. Discovery exports typed contracts on demand; schema admission,
document batching, paginated readback and project save/export jobs run on the
message thread. Pipe I/O remains on the MCP server worker. No DSP callback
participation is introduced. Source/sample validation is shared in Studio.command.

Job acceptance is pending, not completion; native plugin-editor requests report
pending where visibility acknowledgement is unavailable. Unknown timeout outcomes
require readback before retry. Full coverage and cancellable/durable jobs remain
open acceptance; pointer/key diagnostics are never substitutes. The provider-neutral
skill package and generated references live in skills/vibera-studio. Full failing
coverage gate and remaining scope: MCP_PRODUCT_REQUIREMENT.md.
Semantic note/clip selections share Studio.selectItems with UI selection actions
and gesture finalization. Continuous MCP transactions lease Controller's existing
snapshots through explicit owner tokens; canonical command updates remain on the
message thread. Local document edits cancel remote previews before capturing their
own baseline. Commit is one Undo entry; cancel restores project/dirty state; idle
expiry is processed by the UI timer. Readback exposes pending/completed/cancelled
and rejects stale ownership/revision. These process-local gestures do not imply
native plugin parameter Undo, durable jobs or arbitrary third-party UI control.
Test/control-thread stage/pacing diagnostics are separate from actual DSP spans
and do not change device settings, callback budgets or the suite timeout.

## Sampler voice routing (schema14 increment)

`SamplerCuts.h` provides prepared MIDI/typing/preview trigger signals with a
lock-free global ordinal. Immutable `LiveDocument` owns routing references and
sample-offset score-cut schedules outside DSP. Callback monitors use fixed arrays,
bounded scans and raw borrowed pointers whose owners remain pinned by the document.
Score events apply off/cut/on ordering; live cues coalesce per quantum and preserve
later live notes by ordinal. Four voice banks retain separate input ownership but
share a preallocated128-voice Channel budget (user limit1..99, zero Auto). Cuts
release captured envelopes; maximum-polyphony steals oldest voices immediately.
Persistent live-cut time prevents old-note chase after fixed gate overflow and a
new publication; transport jumps reset that history. No plugin-native voice
semantics are inferred from sampler support. [Contract/evidence](SAMPLER_VOICES.md).

## Licensing

Own code AGPL-3.0-or-later; Tracktion GPLv3-or-later and JUCE AGPLv3. Preserve notices and corresponding source. MIT VST3 SDK and audited GPLv3 ASIO SDK in later milestones; WASAPI first. No FL assets or third-party plugin bundles. No paid commercial license path is assumed.

## Improvements
Docking is now the default under the user's explicit reference-based redesign request; familiar editor switching, gestures and command IDs remain. Routing inspector, recovery diagnostics and maqam tools remain separately qualified improvements. Do not silently substitute different musical workflows.
