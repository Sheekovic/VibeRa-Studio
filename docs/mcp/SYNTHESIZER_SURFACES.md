# VibeRa Synthesizer surfaces and evidence boundaries

This is the expanded inventory for the native instrument in
`src/synth/SynthEditor.cpp`. Current aggregate semantic evidence passes in
`SYNTHESIZER_EVIDENCE.json`:18 callback reviews and76 explicit UI operations,
807 named actual cases and1111 current source/content/dependency bindings
(1095 project inputs plus16 reviewed pinned JUCE cache files). Registration
preserves unrelated reviews and inherited gaps; full-product coverage is still
incomplete. Changed callback/backend/content inputs invalidate these reviews.

The processor/component receipt at
`build/windows/synthesizer-acceptance/verification.json` reports a passed run with
79 named component cases, including all 45 exposed parameter widgets: 36 sliders,
four ComboBoxes and five FX toggles. Each widget was changed through its actual
callback, compared with native scalar readback and every neighbouring scalar,
then restored. The receipt also names row/body/star/Enter/previous/next branches,
composed filters, FX pages, actual octave key ranges, small geometry/focus/scroll,
shared preference adoption, pitch return and GUI/Host note ownership cleanup.
It is component/adapter evidence, with a real peer for keyboard hit testing.

The exact current source suite passes208/208 at
`build/dev/20261020-115419/`. Application MCP passes491 checks, including all45
setters, full patch/user-file operations, real PCM and existing-Channel assignment
with exact history. Separate guarded native controls/DPI and preset Save/Load/
overwrite/cancel workflows pass on the same binaries, as do six actual native
musical workflows. Current raw paths/hashes are in the aggregate. Earlier failed
preparation, history and peer-visibility attempts remain in
[VERIFICATION](../VERIFICATION.md); failed attempts do not qualify any review.

The instrument is built into VibeRa, rather than distributed as a separate VST3.
It needs no external plugin scan. The catalogue currently contains 56 original
patches: 20 recorded-source patches, one recorded-source/oscillator layer and 35
oscillator patches, using 20 recorded instrument maps. These counts and metadata
are verified against current discovery, real PCM and content checks. Separate
installed/moved/staging/final distribution qualification remains required. Sound provenance is in
`../SYNTHESIZER_SOUND_SOURCES.md`.

## Shared service and result contracts

`vibera_synth list` discovers stable factory IDs, genre tags, families, source
kind, recorded-map identity, description, macro mapping, all actual patch values
and parameter metadata. It also reports user-library rows/errors and the managed
user directory. `list` overrides filters for that response; `filter` persists the
browser filters. Discovery values, rather than display names or list positions,
are the client contract. `descriptor` returns the fixed native descriptor;
instrument creation uses `plugin.instrument.create` through `create`.

Instrument operations require the existing native Synth plugin ID. Mutation
requires the current runtime `instance`; scalar/patch edits also use the current
parameter `generation`. Refresh through `vibera_plugin refresh`, observe its job,
then read `vibera_synth parameters` before editing. `set` takes discovered native
units; the shared plugin service uses normalized scalar values. Never pass a
normalized value as though it were the native value.

Scalar and full-patch edits use the existing native scalar/patch admission, worker
acknowledgement, captured project state and history service. Factory selection
applies all 46 actual patch values, including the final source-identity selector.
The selector scalar alone does not load a complete patch; `vibera_synth set`
refuses its `factory-preset` key. Native slider drag, typed entry and reset use
hosted parameter gestures; the full UI patch callback must remain one gesture
batch. Current component, native and application MCP receipts verify exact
scalar/patch Undo and Redo through these shared services.

An admitted asynchronous edit is pending until its returned operation reaches a
terminal `vibera_plugin job` result. Inspect `nativeApplied`, `historyCommitted`,
actual values and errors; admission alone is not completion. Use `vibera_plugin
cancel` for that exact operation. A dispatched job can truthfully refuse with
`too_late`; observe its original terminal result. Creation can use the established
structural coordinator and its returned poll tool. Editor open/close uses the
existing worker acknowledgement, rather than a local visibility guess. Save
captures current native state and reports the actual file result through a job;
its cancellation outcome must be tested separately from parameter cancellation.

Browser filters, favorites and navigation use `SynthBrowserState` and
`readSynthBrowserState`/`writeSynthBrowserState` on CONTROL threads. They do not
change DSP parameters or create score Undo edges. Filesystem errors are surfaced.
The UI periodically adopts externally changed preferences; search updates retain
the caret/selection where the new string permits it. Workspace scrolling is
clamped to the editor's real current viewport on adoption.

## Expanded UI operations

Rows below describe the reviewed shared mappings; the aggregate records the
specific actual cases for each accepted review. Registry
IDs are the actual `CommandInfo` IDs; the action after the slash is the semantic
`vibera_synth` action, not a second command registry.

| UI operation or read callback | Registry ID / semantic action and canonical service | Readback and acceptance detail |
| --- | --- | --- |
| Factory row names, tags, family, source and description | `synth.discover` / `list`; `factoryPresets`, source metadata | Exact stable IDs, truthful sources, all nine genres and thirteen families; no inferred sampled acoustic identity from a synthesis patch name. |
| Genre buttons, All Genres and Favourites | `synth.browser.filter` / `filter`; shared browser predicate | `browser.genre`, `favoritesOnly`, resulting IDs. Genre tags may be multi-valued. Favorites is a filter, not a separate sound bank. |
| Family dropdown | `synth.browser.filter` / `filter family` | `browser.family` plus exact result IDs. Unknown genre/family is refused without persisting a replacement. |
| Search text edit/clear | `synth.browser.filter` / `filter search` | Search over name/family/genres/description/source-kind; maximum 512 characters. Both UI and MCP use `synthPresetMatchesBrowser`. |
| Row star and current-preset star | `synth.browser.favorite` / `favorite preset enabled` | Stable ID appears/is absent in `browser.favorites`; repeated add/remove is idempotent, no patch load or score Undo. |
| Arrow-key list selection | `synth.editor.navigate` / `navigate selectedPresetId` | Shared highlighted stable factory ID; empty clears selection. Selecting a row alone does not load it. A filtered-out saved ID has no visible highlighted row. |
| Row click and Enter on selected row | `synth.preset.load` / `loadPreset preset`; full patch service | Exact source identity, every scalar, native job and single history result. Failed admission preserves the previous sound. |
| Previous/Next patch | `synth.preset.previous`, `synth.preset.next` / `previous`, `next` | `adjacentSynthPresetId` applies identical genre/family/search/favorites filters in factory order, wraps, and chooses first/last when current is hidden. Client supplies current stable `preset` or uses shared highlighted ID. Actual operation loads the full patch. |
| Tone, Warmth, Space, Motion rotary controls | `synth.parameter.set` / `set`; shared hosted scalars | Actual scalar values and selected base's macro mapping. Tone modifies cutoff; Warmth groups low EQ boost/output trim; Space adjusts delay/reverb mix; Motion affects vibrato/chorus rate. Enabled effect paths still govern the result. |
| ADSR knobs and displayed envelope curve | `synth.parameter.set`, `synth.parameters` / `set`, `parameters` | Attack/decay/sustain/release scalars; the curve is a read-only view, not a second draggable envelope implementation. |
| Filter dropdown, cutoff and resonance | `synth.parameter.set` / `set` | Discrete filter choice0..3, cutoffHz and normalized resonance0..1; the adapter maps resonance to engine Q. |
| Five FX tabs | `synth.editor.navigate` / `navigate effectPage` | Page0 EQ,1 Compressor,2 Chorus,3 Delay,4 Reverb. Changes visible controls only; no audio/history mutation. |
| Five FX enabled toggles | `synth.parameter.set` / `set` | Independent0/1 native scalars. Disabling preserves control settings; tail/reset policy is separate DSP behavior. |
| EQ Low/Mid/High gain controls | `synth.parameter.set` / `set` | Three groups of seven-band EQ gains, not a claim of independent shelf/parametric filters. |
| Compressor threshold/ratio; chorus mix; delay time/feedback/mix; reverb size/decay/mix | `synth.parameter.set` / `set` | Each discovered native key and unit below; changes retain shared history and actual scalar readback. |
| Source/oscillator dropdowns; blend/tuning/detune/pulse/sample controls | `synth.parameter.set` / `set` | Native enums and scalar values. Sample/hybrid source requires a selected base with a real prepared recorded map; UI disables mapless choices. Sound source identity is not a substitute for loading a patch. |
| Filter-envelope amount, pan, bend range, vibrato rate/depth and polyphony | `synth.parameter.set` / `set` | Exact stable keys, interval/range validation and actual post-ACK readback. |
| Master knob | `synth.parameter.set` / `set master-db` | Master applies to complete output including already-running effect tails; PCM acceptance is separate from scalar readback. |
| Slider drag, typed value, double-click reset | `synth.parameter.set` / `set` with value or discovered default | Gesture begin/change/end, one history unit per gesture, defaults in native units; no extra bespoke reset command. Pitch resets to0 at end of its drag. |
| On-screen and computer-key keyboard notes | `synth.preview.on`, `synth.preview.off` / `previewOn`, `previewOff` | Note0..127; velocity1..127/default100. Uses the prepared Channel preview path and returns `previewLaneOnly`; it does not insert Pattern notes. Actual keyboard-layout shortcuts are input conveniences, not a substitute for the semantic note API. |
| Pitch and modulation sliders | `synth.parameter.set` / `set pitch-bend`, `set mod-wheel` | Pitch-1..1/default0; modulation0..1/default0. Hosted parameters remain exposed to history/automation. |
| Keyboard octave buttons | `synth.editor.navigate` / `navigate octave` | Octave-1..4/default0; actual available/displayed range begins at36+12*octave and ends at127. Independent built-in keyboard scroll buttons are disabled. Changing the range releases held GUI keys; it does not transpose stored notes or create Undo. |
| Outer workspace scrollbars | `synth.editor.navigate` / `navigate scrollX scrollY` | Logical coordinates0..10000/default0, clamped to actual layout; `browser` reports adopted coordinates. Native resize/DPI changes are layout, not audio edits. |
| Preset list's own scrollbar | `synth.editor.navigate` / `navigate presetScrollY` | Logical vertical position0..10000/default0. UI polls the actual ListBox viewport and adopts/clamps externally requested offsets. This is separate from the outer workspace scroll and highlighted ID. |
| Save preset chooser and overwrite | `synth.user.save` / `saveUserPreset`; strict wrapper codec and atomic file replacement | Managed root directory, plain basename1..100 without dots/separators, .vrsynth extension; actual terminal file result, stable user UUID retained on overwrite, native confirmation and explicit MCP `overwrite=true`. |
| Load preset chooser | `synth.user.list`, `synth.user.load` / `listUserPresets`, `loadUserPreset` | UI accepts managed directory children only. MCP resolves a listed stable UUID (including nested files), or a plain root basename; shared strict parser validates the full patch before application. Choosing/cancelling a file is not patch completion. |
| Editor open/close, hide/focus loss and keyboard capture cleanup | `synth.editor.open`, `synth.editor.close`, `synth.preview.panic` / `openEditor`, `closeEditor`, `panic` | Exact editor visibility ACK; release held GUI notes, reset held pitch and end pending parameter gestures. Explicit cleanup also clears JUCE private held-key bookkeeping before allNotesOff, permitting a later identical key press. Native focus/window placement remains OS acceptance. |
| `currentPresetId` callback and modified marker | `synth.parameters` / `parameters`; selector plus all actual scalars | Factory identity and comparison with full patch values; user files may retain a base factory identity while modified. No claim that source identity proves a pristine patch. |
| `peak` callback and stereo meters | Existing native output-meter service / `vibera_plugin` readback | Must compare actual plugin meter status/values with the Synth view; quiet/not-processed/unsupported must stay distinguishable. |
| `activeVoiceCount`, `loadStatus`, local error/result labels | `synth.parameters` / `status`, existing native meter transport and operation results | `synthesizer` reports availability, activeVoices, presetId, sourceKind, contentReady, sampleAvailable, parameterError and loadStatus. It describes the last processed block (initial prepared patch before processing), not a device deadline ACK. Local feedback corresponds to the actual operation result; preparation failure reports missing content. Current executable correspondence is bound by the aggregate receipt. |
| Browser state callbacks and timer adoption | `synth.browser.state` / `browser`; shared preference file | All filters/favorites/navigation read back without selecting or loading a patch. Test external changes while search has focus and viewport clamp after resize. |

## Parameter groups and validation

`SynthPresets.h/.cpp` is authoritative. `list.parameters` publishes each stable
key, integer native ID, minimum, maximum, default, unit, interval, skew and enum
choices. Runtime `parameters.items` publishes the actual normalized scalars and
current generation. IDs are `0x10000 + ParameterId` ordinal; the final selector is
appended to preserve earlier IDs. Factory patches intentionally differ from
global parameter defaults. Clients should discover rather than cache this table.

| Controls / stable keys | Native range and global defaults |
| --- | --- |
| `tone`, `warmth`, `space`, `motion` |0..1; defaults .65/.15/.2/.1 |
| `attack-ms`, `decay-ms`, `sustain`, `release-ms` |1..10000ms/12;1..10000ms/420;0..1/.78;5..15000ms/1200 |
| `filter-type`, `cutoff-hz`, `resonance` |Integer0..3/default0 LP/HP/BP/Bypass;20..20000Hz/8000;0..1/.15 |
| `eq-enabled`, `compressor-enabled`, `chorus-enabled`, `delay-enabled`, `reverb-enabled` |Integer0/1; defaults1/0/0/0/1 |
| `eq-low-db`, `eq-mid-db`, `eq-high-db` |Each-18..18dB/default0 |
| `compressor-threshold-db`, `compressor-ratio` |-60..0dB/-18;1..20:1/3 |
| `chorus-mix` |0..1/.25 |
| `delay-time-ms`, `delay-feedback`, `delay-mix` |1..2000ms/300;0..0.9/.3;0..1/.2 |
| `reverb-size`, `reverb-decay-seconds`, `reverb-mix` |0..1/.6;.1..12s/2;0..1/.2 |
| `master-db` |-60..6dB/default-6 |
| `source-type` |Integer0..2/default0: Synthesis/Sample/Sample+synthesis |
| `oscillator-type`, `oscillator-2-type` |Integer0..4/default1: Sine/Saw/Pulse/Triangle/Noise |
| `oscillator-blend`, `detune-cents`, `oscillator-2-semitones` |0..1/.35;-50..50cents/7;integer-24..24st/0 |
| `pulse-width`, `sample-level` |0.05..0.95/.5;0..1/1 |
| `pitch-bend`, `mod-wheel` |-1..1/0;0..1/0 |
| `filter-envelope-octaves`, `pan`, `bend-range-semitones` |-4..4oct/0;-1..1/0;integer0..24st/2 |
| `vibrato-rate-hz`, `vibrato-depth-cents`, `polyphony` |.05..12Hz/5;0..100cents/0;integer1..64voices/64 |
| `factory-preset` |Integer0..55/default0, source/base identity only; complete factory loading uses `loadPreset` |

Unknown keys, nonfinite/out-of-range values, fractional discrete values, stale
instances/generations, unavailable content, invalid IDs and malformed user files
must refuse before changing the patch. Structured operation codes currently
include `synth.invalidTarget`, `synth.unknownParameter`, `synth.parameterRange`,
`synth.unknownPreset`, `synth.invalidFavorite`, `synth.userIdentity`,
`synth.unknownUserPreset`, `synth.userName`, `synth.overwriteRequired`,
`synth.staleInstance` and `synth.capacity`. Shared validation failures may be
wrapped as `synth.refused` with their exact cause in the message. Do not promise a
more specific top-level code until executable tests establish it.

## User-file and real-time boundaries

Factory assets and maps are read-only. User files live under the discovered
managed Synthesizer directory, normally the user's application-data directory.
The UI rejects loads outside it; copy a compatible user file into that library
before selecting it. Nested listed files are loaded by their stable UUID over
MCP, rather than by an arbitrary external path. Wrapper schema1 includes user
UUID/name, base factory ID, family/genre, source and macro metadata plus the strict
native state. Current macro mappings reference the base patch; arbitrary imported
sample paths or changed macro definitions are unsupported.

The bounded recursive listing skips linked files, reports malformed files and
duplicate UUIDs, and enforces directory/file limits. Native state/user wrappers
are bounded to256KiB and require complete consistent scalars/source identity.
Atomic save retains the previous file on write failure. A save is a file operation
without score Undo; loading a valid patch changes sound through shared history.

The native chooser and MCP save both restrict new destinations to a plain root
basename up to100 characters without dots or separators. The native chooser may
append the .vrsynth extension when omitted; other extensions are refused. Existing
nested user files remain loadable. UI overwrite confirmation and MCP explicit
overwrite have separate actual native/MCP file-result tests and chooser
cancellation checks; admission alone never establishes a successful write.

Preset decoding, preference/user-file I/O, sample decoding and allocation belong
outside DSP. Factory maps are prepared before processing. Parameter targets and
GUI note events use bounded prepared storage/atomics; the audio callback must not
lock `MidiKeyboardState`, perform file I/O, allocate, free or call UI objects.
Native patch switching may reset voices/effect state by the documented prepared
policy; source presence alone does not establish safe callback behavior.

The pinned JUCE Windows peer routes `WM_CAPTURECHANGED` through
`doCaptureChanged` to a synthetic `mouseUp` while dragging. There is no
`Component::mouseCaptureLost` callback in this dependency. The native keyboard
uses that existing mouseUp route; explicit editor cleanup additionally calls the
base keyboard's focusLost reset and allNotesOff. The passed component test drives
release outside the keyboard and a repeated identical press through the actual
GUI note queue, then hides without mouseUp and repeats the same key. It also
tests focus-loss pitch centring and GUI/Host ownership for the same pitch. This
is component/queue evidence, with no observed OS capture event or physical-MIDI
claim.

## Evidence handoff

`scripts/mcp-coverage.py` now discovers nested Synth callback candidates and the
`SynthMcp.h` tool schema. Heuristic candidates still need manual branch expansion;
this document includes return-key selection, viewport, timer/focus cleanup and
read callbacks that a callback regex alone cannot qualify.

For each accepted callback review, retain its exact
`src/synth/SynthEditor.cpp:line:excerpt-hash-prefix`, full excerpt `sourceSha256`,
actual admitted `synth.*` command IDs, semantic readbacks, current skill path and
verification object. `verification` must have status `passed`, nonempty actual
case names, `sourceHashMode: text-lf-v1`, exact current dependency hashes and a
relative passed receipt. That receipt must have `passed: true`, the identical
`sourceInputs`, nonempty actual `checks`, application SHA-256 and worker identity.
Hash mode uses the repository's `dev_support.content_identity`, not an unrelated
text normalization. Changed callback/backend/codec inputs invalidate review.

The JSON binds actual component/MCP/native cases and exact raw receipts to the
current application/worker. Historical failed/partial attempts remain in the
verification index and are not accepted raw receipts. The processor receipt's
integer counter is not a list of
semantic cases; use its `componentCaseIds` string array.

The integration payload is
`build/windows/synthesizer-development/coverage-registration.json`. It contains
exact callback keys/fingerprints and explicit manual-operation records, all with
the original planning state. The reviewed merge uses completed current evidence,
preserving unrelated reviews and every inherited gap. Native chooser save/load/cancellation, ordinary
non-pitch drag boundaries, typed numeric entry/default reset and computer-key
note handling retain separate acceptance requirements where no named component
case proves them. Source mappings alone cannot close those branches.

Review input hashes cover runtime callback/backend/IPC/state/preset/audio sources
and the actual acceptance producers. `library.json` binds the recorded map
contract; the independent content gate checks every embedded sample and notice
hash. This scoped callback review does not replace full compiled-source,
installed-content, staging/final package or moved-installation provenance.
Evidence JSON, this document, generated coverage, registration payloads, review
tables and skill documentation are outputs, so none belongs in its own
`sourceInputs`. Including a receipt hash inside that receipt, or including a
review table that embeds the same hash set, creates a self-reference. Checker
and hash-utility identities are recorded separately as tooling metadata;
coverage bookkeeping must not fabricate runtime drift or loosen validation.
`text-lf-v1` uses `dev_support.content_identity`; binary identity remains exact.

Original project inputs belong in the corresponding-source Git checkpoint.
Pinned JUCE cache files are independently reviewed upstream dependencies, not a
partial vendor checkout to commit: their hashes remain in
`pinnedDependencyInputs` and `dependencyHashes`, checked by the module gate.
The committed dependency lock and patches retain the acquisition contract.

The accepted Synth aggregate includes actual MCP completion/history/cancellation
and separate current native attachment/layout/meter evidence. The generic
`channel.openInstrument` context adapter remains an inherited global gap; native
Synth Rack-name opening has semantic equivalence through admitted
`synth.editor.open` / `vibera_synth openEditor` and the same acknowledged plugin
editor service, with Channel/plugin discovery and exact state readback.
Native OS input, listening, hardware and FL familiarity remain distinct. No
inherited full-product coverage gap is waived.
