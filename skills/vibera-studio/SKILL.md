---
name: vibera-studio
description: Operate VibeRa Studio through its local semantic MCP interface to compose, edit, arrange, mix, save and export projects. Use capability discovery to identify supported operations and remaining gaps.
metadata:
  version: 0.25.0
  contract-version: 1
---

# VibeRa Studio

For the bundled native instrument, follow [VibeRa Synthesizer](references/synthesizer.md).
Discover `vibera_synth` before use; list genre/family/preset metadata and parameter
ranges, then use exact plugin/instance/generation identities for sound edits.
Observe real job outcomes and captured Undo. Factory content is bundled; no VST
scan, network provider, account or manual sample path is required. Source guidance
alone does not qualify an older executable.

This provider-neutral skill source includes unreleased module guidance. Packaged
references qualify only the executable identified by their manifest; discover
actual operations before using a feature described by the current source.
For stereo buses, post-fader sends, source selection and exclusive continuous
levels, follow [Mixer routing](references/mixer-routing.md). Check release
qualification before treating a development capability as shipped.
For named buses, pan, mute/solo and actual meters, use [Mixer controls](references/mixer-controls.md).
For separation, polarity and left/right exchange, read
[Mixer stereo controls](references/mixer-stereo.md). Discover support first and
distinguish saved targets from DSP settling.
For production read-only controller state and atomic single-binding edits, use
[Controller bindings](references/controller-bindings.md). MIDI injection, learn,
port setup and whole-profile diagnostics retain their separate owned-test limits.
For local sample cutoff/resonance/type, dry audition boundaries and continuous
canonical editing, read [Sampler filter](references/sampler-filter.md).
For normalized per-note cutoff/resonance, neutral midpoint and shared Graph
gestures, read [Sampler Mod X/Y](references/sampler-modulation.md).
For full sampler envelope/LFO destinations, tempo units, phase, Automation Clips,
physical ranges, saved recipes, fixed project MIDI CC links/Learn and verification,
read [Sampler envelopes](references/sampler-envelopes.md). New source operations
require current discovery; they are not automatically present in the qualified package.
For typed gain/pan/separation and exact-route send Automation Clips, initialization/manual fallback,
units and paginated readback, see [Mixer automation](references/mixer-automation.md).
For bounded Song MIDI touch recording, follow [Mixer automation recording](references/mixer-automation-recording.md). Native scalar Undo/Redo and recording-time ACK-only writes use [Native parameter history](references/native-parameter-history.md). Discover actual support; source guidance and synthetic checks alone do not establish release or hardware qualification.
For stable-ID insert display order, shortcuts/wheel/context, history/cancel and
readback, use [Mixer insert order](references/mixer-insert-order.md).
For targeted effect-chain Copy, atomic preset Save, Load to clipboard, destination
Paste and Load-and-apply, read
[Effect-chain reuse](references/effect-chains.md). Discover vibera_mixerchain before
use. Copy/Save/Load-to-clipboard preserve the musical document. Paste/Load-and-apply
are structural history transactions and may briefly interrupt audio; disarm
recording first. Poll the exact session job to an actual terminal outcome and
inspect documentChanged/audioState/nativeRecoveryRequired. Distinguish native
capture from retained saved/recovery bytes and initial device-free completion
from live audio recovery. This source guidance does not qualify an older binary.
For ordered effect rows, independent bypass/editor targets and project-bound
Add navigation, read [Mixer effects](references/mixer-effects.md).
For native main input/output bus selection and explicit mono mapping, read
[Plugin audio I/O](references/plugin-audio-io.md). Discover `plugin.io` and
`vibera_plugin action=buses`; read requested and negotiated layouts separately.
This structural operation needs its own async outcome, not a document batch/macro.
For multiple selection, UUID display groups, group solo and actual persisted
recipes, use [Mixer organisation](references/mixer-organisation.md).
Coverage is partial. Discover the connected executable's capabilities; the
older releases may not have the loaded-plugin/catalogue services. Do not infer
readiness from this file or from a registered command.

Read [setup](references/setup.md) to connect a stdio client, and
[workflows](references/workflows.md) for composition, plugins, mixing, recording,
save/export and failure recovery. Machine references are generated from actual
application discovery with `scripts/export-mcp-reference.py` in the repository.
For per-note Slide/Porta,16-color MIDI groups and atomic Note Properties, use
[Piano Roll note modes](references/piano-note-modes.md). Discover the actual executable;
source guidance does not establish native transition or file-export qualification.
For proportional phrase timing, use [selection stretching](references/note-stretch.md).
It preserves source identity and previews the absolute ratio without editing the
project. Commit/cancel/lease ownership and source-dependent bounds matter.
For manual Piano Roll cuts, use [note slicing](references/note-slice.md): a
musical cut-line preview remains pending without changing source/audio/history;
commit returns actual split identities. Preserve session ownership and distinguish
project completion from pending audio publication. UI macros store geometry in
profile3; persistent macro/shortcut management still has inherited MCP gaps.
For source-preserving note mute, semantic strokes, linked Patterns and MIDI/WAV
export, read [note mute](references/note-mute.md). Muted notes remain in the
project; score MIDI exports omit them. Poll file jobs until a terminal result.
For arranging mixed Pattern/Audio/Automation sections together, read
[Playlist groups](references/playlist-groups.md). Distinguish placement membership
from linked musical sources, explicit selection from effective targets, and the
workspace grouping switch from document history.
For independent Automation variations, read [source cloning](references/automation-source-clone.md).
Distinguish clear/preserve target links from linked placement duplication and
settings-only instrument cloning; verify new IDs and actual file-job completion.
For Channel Rack bulk selection, named groups and display filtering, read
[Rack groups](references/rack.md). Filtering does not select a MIDI recipient;
use stable Channel IDs and distinguish bulk selection from the active instrument.
For rhythmic step fills, melodic conversion and long-Pattern editing, read
[step sequencing](references/steps.md). Fill replaces the chosen part's rhythm;
inspect its notes before deciding whether conversion matches the requested music.
For multi-Channel rhythm recipes, retained drafts and owned audible preview,
read [Advanced Fill](references/advanced-fill.md). Discover actual support;
source guidance is not release qualification.
For retained inactive step properties and automatic-loop source recall, read
[removed-step memory](references/step-memory.md). Discover typed support first;
this development guide is not evidence that a connected release implements it.

For live Pattern length, arrangement extent and explicit whole-Pattern/Song
Loop control, read [looping](references/looping.md). Read requested and applied
bounds separately; a missing audio context is never audio verification.

For MIDI take ownership, exact retry/cancel tokens and honest pending/terminal
results, read [take recovery](references/take-recovery.md). Arm through canonical
transport.record; arming and query completion never prove captured or committed
notes. Retained takes block replacement/save/export until retry or explicit cancel.
This is unreleased source guidance; discover the connected executable first.

For open/closed-hat choking, step-only Cut itself, shared Channel polyphony and
development Mono/Porta/Slide/Gate/Full-porta controls with global MIDI mappings,
read [sampler voices](references/sampler-voices.md). Discover instrument support;
these controls do not govern arbitrary native VST3 voices.

For per-step dynamics and pitch, read
[step Graph Editor](references/velocity-graph.md). Discover actual support for
canonical steps.velocity/steps.pitch and leased semantic gestures; pitch is an
unreleased source extension. Use semantic edits, never coordinate simulation.

For reusable captured/preset timing and velocity, file import/export, rest deletion
and preview guides, read [grooves](references/grooves.md). These are original
VibeRa templates; inspect actual source notes and distinguish preview from commit.

For reversible Piano Roll timing and the distinct quick actions, read
[Quantizer](references/quantizer.md). Use baseline semantic gestures, explicit
modes and actual note readback; uniform grid is not groove-template support.

For global/per-Channel Swing, bulk Mix and note truncation, read
[Swing](references/swing.md). Preserve source notes, distinguish normalized values
from UI percent, and verify played timing through readback and actual export.

For score mute, solo/restore, bulk Channel muting and the Workflow preference,
read [Channel solo](references/solo.md). Channel mute leaves live MIDI playable;
Mixer mute still affects the routed output.

For actual filter-group solo, persisted mute locks, scope restoration and profile8
macros, read [Channel scoped solo](references/channel-scoped-solo.md). This is
unreleased development behavior: discover support from the running executable.
Ctrl remains global; Alt is group scope; Shift locks. Exact combined FL modifier
runtime parity is unobserved. Protect locked peers and the active live recipient.

1. Call `vibera_commands` to discover IDs, mappings and gaps. Request
   `command=<id>, includeSchemas=true` for typed arguments. Use `tools/list`
   for tool schemas. Unsupported or unverified capabilities remain incomplete.
2. Read `vibera_state` and paginated `vibera_inspect` before identifying targets.
   IDs belong to the current `projectIdentity`; never substitute row numbers.
   Re-read after project replacement or worker recovery. Observe revision guards.
3. Use semantic tools and canonical commands. `vibera_pointer` and `vibera_key`
   are diagnostics and do not satisfy functional coverage. No screen coordinates
   are needed for the workflows in this package.
4. Time is zero-based quarter-note beats; MIDI pitch is 0-127, velocity 0-1.
   Audio Clip source duration/offset are seconds, independent of tempo. Mixer
   index0 is Master; Playlist lane0 is the first lane. Patterns contain multiple
   Channel parts; Playlist lanes do not determine Mixer routing.
5. `vibera_batch` groups canonical document edits into one Undo transaction and
   rolls back on validation failure. `edit.undo`/`edit.redo` affect musical
   document history. Navigation, saving, plugin runtime control and export are
   not automatically undoable. `vibera_gesture` owns a leased continuous edit:
   begin, update canonical commands, then commit one Undo step or cancel. Keep
   the operation token; pending previews are not committed. See the workflow
   reference for idle expiry and local-user preemption.
   Failed history admission returns `history_refused`; it does not complete an
   owned gesture. Keep its token and retry or cancel. Failed batches roll back
   to their owned baseline. `vibera_state.historyNotifications` distinguishes
   pending Controller work from pending asynchronous Undo notification requests;
   request acceptance does not acknowledge observer delivery. Committed edits
   are never replayed merely to retry notification.
6. Save/export return `completionState=pending` and an operation ID. Poll
   `vibera_project action=job`; only a terminal `completed` result establishes
   completion. `failed` carries a structured error. Cancellation of these jobs
   is not implemented yet: report that limitation rather than inventing success.
7. Verify actual state, reopened project and output files. A timeout/disconnect
   can leave an uncertain outcome: inspect before repeating a mutation. Jobs
   belong to one live instance; retention depends on the tool. Reconnecting to a
   newly launched instance does not resume a lost in-memory job ledger.

VST3 native editors expose only capabilities supplied through supported plugin
interfaces. Do not claim arbitrary control of proprietary editor widgets or
equate opening a window with successful parameter processing. Parameter/state
work and recovery still have semantic coverage gaps; consult discovery and the
repository coverage inventory. Virtual audio, screenshots and PCM checks never
establish heard audio, physical MIDI operation or full FL workflow parity.

Use `vibera_plugin` for loaded project plugin discovery, paginated native
parameters, refresh jobs, shared Controls, session notifications and canonical
automation preparation. Read [plugin operations](references/plugins.md) before
using them. Supported scalar writes/history use the shared Controls service:
[native parameter history](references/native-parameter-history.md). Arbitrary
native widgets, programs and hidden-state reversal remain unsupported.
Initial parameter snapshots are not current manual-before-value authority.
For acknowledged native instrument/effect windows, targeting, cancellation and
current window readback, read [native editors](references/plugin-editors.md).
This is unreleased source guidance; discover capabilities before using it.

Use `vibera_catalogue` for installed VST3 scanning, class IDs/descriptors, blacklist
and scan jobs. Read [catalogue operations](references/catalogue.md) for scope,
persistence failures, cancellation and creation from discovered classes.

For Piano Roll Quick legato / Quick chop, use the canonical `notes.legato` and
`notes.chop` commands. Read [score tools](references/score-tools.md) for targeting,
exact-tail behavior and verification. Inherited MCP gaps remain deferred during
music-feature development; coverage is still partial.

Use `notes.glue` to join touching/overlapping same-pitch selected notes; the
[Glue recipe](references/glue.md) covers surviving IDs, linked Patterns, Undo
and numerical tolerance. Read state again because merged note IDs disappear.

Channel cloning, reordering, deletion, real job outcomes and score transfer:
[Channel management](references/channels.md). Clone/delete do not run in the
synchronous document-macro runner; reorder does.

Save/apply portable sampler Channel sounds: [Channel presets](references/channel-presets.md).
Bulk Pattern clone/delete, unused-Pattern cleanup and Pattern-only WAV: [Pattern management](references/pattern-management.md).
CoProducer chat panel, provider/model/effort setup and local ACE-Step/YuE2 music: [CoProducer](references/ai-producer.md).

For non-destructive sample peak normalization, read [Normalize](references/normalize.md).
Use the shared typed setting, verify requested/prepared/projected state and actual
export; native third-party plugin interiors remain unsupported.

For source-bound Set/Detect/Fit tempo, cancellation and musical beat/bar fitting,
read [Sampler tempo](references/sampler-tempo.md). Discover `vibera_samplerTempo`:
completed analysis has not applied an edit. Explicit confirmation is one Undo;
Resample changes pitch and duration together. The workflow is under development;
use executable discovery to check availability in the connected build.

Sampler step Pan/Fine Pitch, score Shift/Repeat timing, MIDI release velocity, source-targeted gestures and truthful MIDI export:
[Step expression](references/step-expression.md). Development-source only; discover
target support (Shift/Repeat include native instruments; release response is instrument-dependent)
and verify actual model/audio/file outcomes. Release velocity is not ADSR duration.

Independent per-Pattern Channel loops, source-bound Piano occurrences and Burn:
[Channel loops guide](references/channel-loops.md). Development-source only;
discover availability, source IDs/display geometry, Slice ambiguity, exact command
envelopes and stopped-only Burn admission.

Automatic/manual Pattern length, dormant score windows and local time signatures:
[Pattern extent and meter](references/pattern-extent.md). Development source only;
discover the commands and verify effective meter, mode and real file-job outcomes.

Unreleased checked local previews: refused Rack velocity/Swing completion retains
its baseline; retry mouse-up/completion or Escape. Sampler controls retain their
owned edit after mouse-up: Return retries, Escape cancels. Quantizer Accept failure
keeps its dialog and typed `vibera_state.quantizerPreview.error` with pending state;
retry Accept or Cancel. Save/export/new/open/close refuse uncommitted local edits.
These UI paths share Controller history; semantic owned gestures use
`vibera_gesture` completion/cancellation and must inspect actual results.

Song MIDI Mixer automation recording: [task guide](references/mixer-automation-recording.md).

Tempo map: `vibera_command tempo.change.add {beat (> 0), value: BPM 10..999, step: ramp}`,
`tempo.change.set {target, beat, value, step}`, `tempo.change.delete {target}`; read with
`vibera_inspect {"entity":"tempo"}` (row 0 is the base tempo; `seconds` per change). Song
mode only. Shaped curves: `tempo.curve {beat, length, value, text: linear|ease|in|out}`
glides from the tempo at `beat` to `value` (16 ramps, changes inside replaced, one Undo).
Audio Clips keep their seconds unless warped: `vibera_project warpClip {clip, bpm}`.

Arrangements: `vibera_command arrangement.add|clone {text}`, `arrangement.select|rename|delete
{target}` (0 = current); `vibera_inspect {"entity":"arrangements"}`. Playback, export and
editing act on the current arrangement.

Playlist markers: `vibera_command marker.add {beat, text}`, `marker.rename|move|delete
{target}`; read them with `vibera_inspect {"entity":"markers"}`.

Export: `vibera_project export` picks WAV/FLAC/OGG/MP3 from the path extension (MP3 needs
lame.exe on PATH or VIBERA_LAME_EXECUTABLE; otherwise the job fails with that reason);
`stems: true` with a folder path writes one WAV per Mixer insert fed by a Channel.

Audio-input (microphone/instrument) takes, input/monitor settings and their placed
Audio Clips: [audio recording](references/audio-recording.md).

Channel name/colour/original icons and stable selected-slot sorting:
[Channel appearance guide](references/channel-appearance.md). Development source
only until the Rack/Pattern/sampler module qualifies; discover availability first.

For individually insertable built-in VibeRa effects, read the
[Effects guide](references/effects.md). Discover availability and current native
parameter metadata; it is not bundled in the qualified DAW distribution.
