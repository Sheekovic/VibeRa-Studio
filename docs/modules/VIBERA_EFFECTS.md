# VibeRa Effects

User direction2026-10-19: start independent local VibeRa Effects from the supplied
effects research before YuE2 integration. Preserve concurrent Mixer work, its
acceptance scope and the qualified0.0.63 distribution. No upload or publication.

The complete [85-reference ledger](VIBERA_EFFECTS.json) remains open. Folder
count does not equal distinct DSP algorithms: the scope also contains editors,
note processors, routing tools, controllers and visualizers. Shared foundations
do not close their distinct workflows or version families.

## Native integration checkpoint

User correction2026-10-19: every effect must be native and separately available
inside Studio. The single externally scanned VST3 suite was rejected and retired.
The reusable independent DSP moved to `src/effects/dsp`. `src/effects` compiles
fixed effect instances and original editors into the Studio worker; there is no
external effect binary, install, scan or mode selector. Existing owned worker
containment remains an internal implementation detail.

The picker always prepends23 individual entries: Balance, Stereo Width, Polarity,
Mute, Stereo Matrix, DC Block, Bass Shelf, Seven Band EQ, Filter, Soft Clip, Drive,
Crush, Compressor, Peak Limiter, Gate, Stereo Delay, Chorus, Flanger, Phaser, Room
Reverb, Tremolo, Ring Mod and Auto Pan. Each insertion creates an independent
ordered Mixer slot with only that effect's relevant parameters. Project, chain
preset, insertion/replacement and worker startup validate reserved native metadata;
external VST3 discovery remains separate and compatible.

Immutable MCP `vibera_effects` list/descriptor discovery uses the source registry.
Canonical `plugin.effect.insert/replace` and the existing slot, parameter, job,
history, automation, save/reopen and export paths serve both UI and MCP. Native
state schema1 identifies the fixed type and validates all scalar targets before
replacement; incompatible/partial state cannot silently change the effect type.
Audio buffers allocate during preparation. Sample-offset edits, processing and
reset use bounded storage/work without host/UI notifications from DSP.

Native affected checks pass. Broader regression and current
shared wrapper/source qualification remain failed as recorded below. Prior prototype passes below are
historical only; they do not qualify native integration or the complete collection.
Run `python scripts/dev-native-effects.py` for incremental development checks.
No version bump, packaging or distribution replacement occurs here.

## Actual native affected verification

Native cases4/4 and corrected MCP semantic discovery pass. Final focused run
`build/windows/native-effects-dev-1792397234565544400/verification.json` is8/9:
only the concurrent wrapper workflow times out60.05s. Its overall result is failed.
Full run `build/dev/20261019-104707` completed186/189,990.692s tests and85.797s
incremental compile. Failures: an outdated27-tool assertion (corrected to28 plus
typed native discovery checks; targeted PASS), development_workflow15.04s timeout,
and wrapper wet/dry accessibility. Concurrent wrapper/MCP/test source changes
invalidate combined-source qualification; no full/module/release pass is claimed.
Native implementation files remained unchanged during the final focused run.
Handoff: `build/windows/native-effects-native-handoff.json`.

- DSP:23 effects,1,700,944 assertions; retained analytic, impulse, extreme and
  automation oracles.
- Fixed instances:11,393 checks; relevant parameters only, stable IDs, strict
  type/state/attribute refusal, sample-offset controls versus a segmented scalar
  reference, no DSP host notifications, zero callback C++ allocation/destruction,
  scalar flush preserving delay history, all23 editor handlers, paired gestures
  and actual timer readback. Fresh EQ/Delay/Limiter renders were opened/inspected.
  Receipt: `build/windows/native-effects-acceptance/verification.json`.
- Model: all23 project/chain roundtrips, mixed native/VST3 compatibility, native
  instruments refused and14 metadata-spoof variants preserving prior outputs.
- Actual MCP:1,821 checks in27.202s. All23 types prepare and independently accept
  a relevant scalar endpoint with actual ACK/fresh readback/exact Undo/Redo;
  includes discrete controls. Empty external catalogue before/after; no scan or
  install. Separate Balance/Delay slots, actual native editor/wrapper navigation,
  scalar refusal/history, save/reopen and real attenuated/delayed nonzero WAVs.
  App/worker hashes stayed byte-identical; owned app exited0. Receipt:
  `build/windows/native-effects-mcp/attempt-1792397312645026900/verification.json`.

Retained native failures: unsupported rectangle API in the concurrent wrapper
fixture, corrected to local bounds; one70ms editor test sleep replaced by bounded
actual-value polling within500ms with unchanged value tolerance; stale MCP object
from an overlapping compile, corrected by rebuilding the affected TU. Failed
receipts remain. A prior416.842s compile correctly failed its consistency check
following concurrent edits and qualified no tests. Native component/MCP evidence
is separate from physical input, listening, hardware and the complete collection.
The workflow deadline, wrapper outcomes and full module gates remain required.

## Historical rejected-prototype verification

Historical command: `python plugins/vibera-effects/tests/develop.py` (retired).
Final stable-input receipt:
`build/windows/vibera-effects/attempt-1792393303701649400/development.json`.
Affected CTest3/3 PASS,8.98s wall time (outer command9.011s);
incremental compile14.608s, complete development operation25.932s.

- Actual DSP:23 modes,1,700,944 checks. Four sample rates, block partitions,
  analytic utility/filter/dynamics oracles, impulse/tail/reset/feedback behavior,
  invalid/extreme controls and input, zero callback new/delete, exact live
  Mute/Polarity fades, and an eight-second high-Q rapid-filter-automation regression.
- Actual processor/component:94,517 checks, including all mode branches, zero
  callback new/delete, strict state preservation/refusal/retry, designated bypass,
  first-sample recalled gain, UI-to-host and host-to-UI parameter agreement. Fresh
  Seven Band EQ/Delay PNGs were opened and inspected after numeric-display cleanup.
- Actual VST3/application MCP:361 checks, disposable catalogue scan, real insertion
  and worker preparation, actual scalar ACK/history, refreshed parameter readback,
  stale-instance/out-of-range refusal, exact Undo/Redo, native state save/reopen,
  real attenuated/restored-delay24-bit stereo WAVs, and removal/Undo restoration.
  Owned application exit0, no forced close. Receipt:
  `build/windows/vibera-effects/mcp-attempt-1792393303701649400/verification.json`.

Plugin SHA256:
`aec475b37a065e206df4b01c6b3ec0db3819598f0c2bfd422363a00ca94bc3dc`.
The main DAW source/cache/test inventory and permanent distribution were not
modified by this component build. Its tests use the existing main development
app; the MCP receipt records that exact app's hash independently.

Retained failures: the initial default Balance configuration incorrectly ramped
the first recalled native values; removed that synthetic configuration and tested
first-sample state. Initial MCP readback incorrectly treated a catalogue snapshot
as current; the corrected verifier observes an explicit refresh and its serial.
Both failed run receipts remain under this component's build directory. Earlier
filter numerical review found coefficient-dependent biquad-state instability;
the independent trapezoidal state-variable correction passes actual C++ stress.
No historical failure or assertion was erased to manufacture a pass.

## Remaining full scope

Current native-editor update2026-10-19: original responsive rotary cards and EQ
faders preserve fixed IDs, ranges, host gestures and reset behavior. Real stereo
output meters distinguish not-processed from measured silence. Shared MCP
readback is CONTROL-only, IPC16; third-party VST3 meters are explicitly unsupported.
Affected13/13 and actual Master two-slot state/history/rendering checks pass;
six native component images were opened. Receipt:
build/mixer-native-workflow-20261019/current-focused-verification.json.
This does not qualify the full85-reference collection or a new distribution.

The first effects are foundations, not completed reference counterparts. Complete
native Windows input/window and live automation/recovery qualification, user
listening and physical-device checks remain separate. No heard audio, hardware,
full release or production-readiness claim is made.

Seven Band EQ is fixed-frequency, not the required full parametric EQ workflow.
Peak Limiter is sample-peak with no lookahead/true-peak qualification. Nonlinear
effects are not oversampled. Delay has no tempo sync yet; mono, surround and
external sidechain are unsupported. Advanced multiband dynamics/enhancement,
editable waveshaping, convolution, multitap/bank delays, pitch/time/formant/vocoder
tools, modular/controller/note-processing systems, audio editors and visualizers
remain required open work in the complete ledger.

Research strings prove only string presence. The raw control heuristics include
shared sampler/editor modules, especially around multiband/enhancement targets;
they do not prove instantiated controls, bindings or DSP. Only independently
defined behavior and original implementation enter product source. No extracted
binary, resource, preset, artwork, raw inventory or recovered code is a build input.

Source integration: `src/effects`; tests: `tests/effects` and `tests/NativeEffectsTests.cpp`.
Provider-neutral operation: [Effects skill guidance](../../skills/vibera-studio/references/effects.md).
