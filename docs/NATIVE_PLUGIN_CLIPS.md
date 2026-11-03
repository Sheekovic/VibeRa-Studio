# Native plugin Automation Clips: development0.0.35

The matching-source build/package transaction passed: 15/15 source suites and
14/14 final packaged checks. This workflow is included in the sole distribution.
See STATUS.md and build/native-clips-provenance.json for exact current identity.
This work does not complete the native automation family or full FL feature/UI goal.

## User workflow and reference

Open a Channel/effect's existing Plugin controls inspector, select an automatable
native parameter, then Create automation clip. The action requests fresh metadata,
checks project/host/parameter/generation identity and creates a flat normalized
curve on a free Playlist lane. It captures the current manual fallback and creates
an initialized control in one undoable model command. Init song and Delete init
manage that parameter's initializer separately. Song playback uses Playlist curves;
Pattern playback does not inherit them. Existing point/shape/trim/mute/duplicate
and Undo/Redo workflows are reused.

The [official Automation Clip manual](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/playlist_automationclip.htm)
documents a last-tweaked route and a parameter-browser alternative, default song
coverage without a selection, and retained initialization when clips are removed.
These are documented targets, not running-FL observations. VibeRa currently uses
its parameter inspector alternative, song extent with a four-beat minimum, and a
free lane. Time-selection creation, Current project parameter browsing and native
editor last-tweaked/gesture Undo remain separate unfinished workflows. Earlier
[Wrapper static findings](WRAPPER_REFERENCE.md) do not establish handler execution.

## Model and runtime ownership

Canonical automation.plugin.create/init/init.remove commands use the existing
typed native binding payload. They are internal parameter-specific actions, not
unbound generic shortcut/MIDI targets. Native keys must resolve to an existing
PluginSpec and saved native-ID declaration. Stale capabilities/invalid values fail
before committing a partial project. Binding/initializer/curve creation is one Undo.
Saved manual fallback values feed the existing compiler; whole-plugin opaque state
continues through normal capture/persistence. Removal of a referenced binding/owner
is refused until its active references are removed; automatic orphan retargeting is
not implemented. Replacing a plugin must never silently bind by parameter name/order.

Project schema9/IPC6 need no new fields. Older applications that reject native keys
cannot open these active-clip projects. Do not downgrade-save. No plugin formats,
dependency revisions, licensing or proprietary assets change.

Control-thread preparation validates current native metadata, packet workload and
exact owned worker before publishing a document. Editing can retain a visibly missing
plugin's descriptor/state/curves without a runtime route. Offline rendering remains
strict: missing workers and failed delivery must leave an existing WAV intact. A
live worker with invalid/stale capabilities is not silently treated as a missing
placeholder. Metadata work and snapshot/resource preparation stay outside callbacks;
no new wait/allocation/I/O/UI operation is added to the audio callback.

## Qualification state and retained failures

New tests cover native-ID creation/initialization, atomic refusal, missing-plugin
project data, exact Undo/Redo, normalized manual fallback and Pattern independence.
Whole-graph tests exercise actual original SDK/installed providers through the normal
engine, live curve edits/loop boundaries, state capture, collected-media round trips
and offline WAV. The original fixture uses a steady sample and independent .25/.75
gain windows to test actual rendered PCM, beyond event counters. Actual inspector
button callbacks run during a held MIDI take with lifecycle/deadline measurements.
Both real-provider native-clip checks passed in final packaged verification.

Focused SDK whole-graph qualification now passes181125 assertions. Both modes
run448 callbacks and one loop; Pattern applies zero Playlist native events, Song
applies8609. Maxima0.2008/0.1999ms, zero lost packets and unexpected fixture silence;
the independent export ratios are exactly0.25000/0.75000. The actual missing-plugin
editing graph retains state/curves with a visible error and no native worker.
`build/native-clips-measured-core` contains callback CSVs and reports.

The owned inspector test exited0 (`build/native-clips-first-ui-driver.log`).
Actual button/Undo/Redo callbacks preserve held MIDI, transport, graph/device and
worker. Maxima0.2781/0.2032ms over107 callbacks per mode. Both fresh inspector
renders were opened/inspected. Control acknowledgement maxima54.50/109.99ms are
separate UI measurements; Song had12 acknowledgements beyond70ms. These results
do not establish physical/native mouse familiarity, hardware or listening.

First fixture run terminated with0xC00000FD. A matching optimized-symbol trace in
`build/native-clips-symbol-crash` located nativeClipGraphTests at its entry. Two
278768-byte AudioEngine test objects plus the caller frame exhausted the default
stack. Both test-only engines now allocate before callbacks on the heap; no stack
limit/buffer increase or production-DSP workaround was used. Initial unsymbolized
and symbolized failures remain. Optimized test PDBs stay in build/windows.

After that repair, the test passed its Pattern live edit/loop/lifecycle assertions
but rejected the portable save comparison because sample collection changes paths.
The corrected comparison verifies original/collected SHA256 and exact remaining
project state, matching existing collection semantics. The red receipt remains in
`build/native-clips-heap-fixed-core.log`. The first focused results above preceded the final source/provider/native UI
and in-place package qualification recorded in STATUS.md. Listening/hardware and full native
gesture/notification/PDC acceptance are not established by these tests.

The first complete759fc2c transaction passed15/15 source and14/14 packaged
checks, including actual Analog Lab V Master and MVibratoMB Dry/wet native clips.
A native-window test closed during plugin startup and exceeded its three-second
grace period; only its owned instance was terminated. The red receipt is retained
at build/native-clips-startup-close-red.{log,txt}.

Preparation formerly waited up to15 seconds in PluginHost::start without
observing its owning thread's cancellation. The correction checks a preparation-
owner predicate outside DSP, signals jobs early during engine teardown, and
prevents reuse of cancelled results after a rapid selection change/restore.
Only existing verified owned-worker graceful/timeout cleanup is used.

The original fixture's test-only initialization delay is scoped to child-process
environment and restored afterward. Focused regression passes181137 assertions:
cancel response103ms, engine teardown2157ms, exact worker exit and restored-selection
recovery; full native curve/PCM checks remain passing. These are fixture results;
final source/package and actual native-window shutdown requalification follow.

Final correction qualification: 15/15 source suites and 14/14 packaged checks
passed; actual native-project window closure is graceful with exit 0 within the
unchanged 3000 ms helper bound. Fresh populated native, inspector and actual
provider editor captures were opened/inspected. Exact latest measurements and
source/binary hashes are in build/native-clips-provenance.json; earlier focused
measurements above remain historical. No listening/hardware claim.
