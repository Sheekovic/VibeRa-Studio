# Mixer and Master Automation Clips

Current release authority: [STATUS](STATUS.md). The sole runnable distribution is
`dist/VibeRa Studio`; old preview evidence below is historical, not a preservation
rule. AU-01 and the full Mixer module remain incomplete.

Development schema22 extends shared automation targets to pan and stereo
separation, with bipolar output ranges and normalized source points. Right-click
the relevant Mixer control or use its insert context menu; source properties
retain gain rows and add explicit pan/separation rows. Initialization, linking,
source selection, history and MCP share canonical behavior. Current source guide:
`skills/vibera-studio/references/mixer-automation.md`. Backend/component/export/MCP
checks passed; new UI and loop-native integration are pending. Sends and touch
recording remain unfinished. Historical gain qualification:
[retained receipts and failures](PREVIEW_0_0_26.md).

Continuation qualifies right insertion/continuous drag/Shift level preservation, axis constraints, source-window trim and actual Escape while preserving a held MIDI take. It fixes queued source-target selection overwriting typed values. Current source/timing/provider/native evidence and the strict package gate are recorded in [PREVIEW_0_0_26.md](PREVIEW_0_0_26.md); earlier measurements below retain their own identities.

The [official Automation Clip manual](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/playlist_automationclip.htm) was rechecked on 2026-10-14 for right-click creation/point editing, axis modifiers, initialization and Make Unique target independence. This is documentation evidence; local FL runtime behavior and its curve mathematics remain distinct acceptance gates.

## Workflow

Right-click a Mixer fader and select **Create automation clip**. VibeRa creates a normalized source and a Playlist placement spanning at least four beats or the current Song, on the next Playlist lane. The source initially reproduces that fader's manual value, links to its persistent Mixer index with a 0–2 linear gain range, and preserves an existing initialized value. Playlist lanes remain independent of Mixer routing.

Right-click the curve body to add a point; drag a point to change its source beat and normalized value. Alt bypasses snapping, Ctrl holds time, and Shift holds value. Escape cancels the entire gesture. Endpoints cannot be deleted; moving the first endpoint preserves beat zero. Moving the final endpoint changes the source length and matching full source windows. Right-click a point for deletion, numeric time/value/tension, Single curve, Hold or Smooth. The right point selects the incoming curve. These modifier/curve details are VibeRa's documented implementation; exact local FL behavior remains unqualified.

Drag the clip header or empty curve body to move the clip; drag its right edge to resize its source window. Existing Playlist duplicate, slice, slip, mute, selection and deletion commands support pattern, audio and automation placements. Linked copies share their source points. Make Unique creates fresh source/point IDs and independent target links; explicitly relink the new source through its properties.

Double-click a clip, or use its header menu's **Automation source / targets**, to rename the source and add/edit/remove a Mixer-volume link. The dialog marks existing links and supports inverted/constant ranges. Apply/link edits are one Undo transaction; invalid values restore the complete previous state. Playlist Menu selects Draw Pattern or an automation source for placing additional linked clips. Right-click a fader also exposes Init song with this position and Delete initialized control.

## Playback and persistence

Project schema 8 stores sources, stable point IDs, placements/source windows, typed links/ranges and initialized controls. Versions 1–7 migrate with no automation. Strict automation parsing, global ID/domain/ownership checks and full Song compilation reject malformed/unsupported data before replacing a project. Active Channel/pan/plugin targets are rejected rather than silently stored as working automation.

Song playback and offline export share the prepared program. Before the first clip, an initialized value takes precedence over the manual fader; without initialization, the manual fader is the fallback. The last applicable clip value holds after its end. Pattern mode and stopped monitoring use manual Mixer values. Muting a Mixer insert/Master overrides its curve; muting a placement removes that placement from evaluation while preserving initialization and earlier held values. Removing a source does not remove initialized controls.

Retained original gain stages replace only Mixer/Master gain processing; Tracktion retains routing, plugin effects, Channel gains and pan. Existing Guide ordering is preserved. Manual gain/mute changes use 15 ms smoothing; curve samples use their exact prepared polynomial. Routine automation edits publish immutable data without rebuilding the retained Edit/device/samplers/gain stages or hosted workers. Mixer faders read the last processed gain through atomic UI readback while playing. No new dependency or vendor patch is required.

## Evidence and limits

Release build logs: `build/automation-final-build.log` and `build/automation-properties-build.log`. Initial integration suite: `build/automation-final-ctest.log`, 10/10 in 78.15 s, 25,926 musical and 12,659 automation assertions. Final source-properties suite: `build/automation-properties-ctest.log`, 9/10 in 78.23 s, with 25,929 musical/12,659 automation assertions passing. Controller GUI failed a 27.9823 ms callback against 5.333 ms; the failure is retained in `build/automation-controller-deadline-failure`. Three focused matching controller repeats passed with maxima 0.2191/0.2377, 0.2228/0.1918, 0.2682/0.2139 ms (Pattern/Song), zero overruns/lifecycle changes. Cause remains unestablished; this does not erase the failed suite or qualify a package. Owned receipts: `build/automation-controller-repeats.json`. Exact application/runner/worker identities are in `build/automation-integration-provenance.json`.

Continuation added controller callback wall-time/raw Windows thread-cycle CSVs plus explicit per-mode lifecycle/gain-stage invariants, all outside the measured callback. No deadline/buffer assertion was weakened. This diagnostic build (`build/automation-controller-diagnostics-build.log`) passed final CTest 10/10 in 79.03 s (`build/automation-diagnostics-ctest.log`), with 25,925 musical/12,659 automation assertions; controller maxima 0.1439/0.1905 ms, zero overruns and all lifecycle flags true. CSVs/invariants live in `build/windows/controller_mapping_gui`. Raw cycle counts are not converted to elapsed CPU time and do not establish the earlier outlier's cause. Final executable SHA256 is `d6a6c2a2c053a0952a71c001fba16c0408184fce02b9f4f70b1e9e5dcbb9ab65`; runner `c123f7b2e307aa81debbc51af631ddd8f4342fb11c2da3daad38cca1a7d854c7`.

The actual DAW exports Insert 17 plus Master curves at 44.1/48 kHz with 256/512-frame buffers. An independent composed-gain PCM oracle checks linear/Smooth curves, initialization, manual-zero replacement, post-clip hold, stereo output and mute; maximum measured error was 0.000000122. Live virtual processing includes point edits/Undo/Redo/seeks/looping, retained gain-stage initialization counters and 320 callback deadlines. Evidence: `build/windows/acceptance/automation-playback-verification.txt` and original `automation-*.wav` fixtures. Standalone dense evaluator checks remain distinct from whole-graph/hardware qualification.

The component GUI gate exercises actual right-click point insertion and drag, Undo/Redo/Escape, linked copies/Make Unique/relink, real source-properties rename/link/inverted range and invalid-range transaction rollback, disk round trip and DAW export. Compact and 1600-pixel snapshots were opened and inspected. Evidence: `build/windows/automation_clip_gui` and `build/automation-properties-final`. MCP final integration passed 408 checks; evidence: `build/automation-mcp-properties/verification.json`.

Matching diagnostic-executable MCP408 and native Windows creation/point gestures/Undo/Redo passed again; final native capture was opened and inspected. Exact receipts: `build/automation-mcp-diagnostics/verification.json` and `build/automation-native-diagnostics/process.json`. Earlier native evidence below retains its own executable identity.

Native owned-app acceptance passed on application SHA256 `35d9e08040427c16f75b063cbb3c3500c0ab55a322673a7ec9883bf1b0df99b7`: Windows right-click Mixer fader, left-click Create automation clip, right-click curve insertion and Windows point drag, confirmed by application state and exact Undo/Redo. Native captures were opened and inspected. Evidence: `build/automation-native-paced/process.json`, `verification.txt`, `native-mixer-automation-menu.png`, `native-automation-points.png`. Earlier foreground pauses and popup-selection failures remain in `build/automation-native-*`; separating mouse move/press/release delivery produced the passing harness run, with no production-input change. Do not relabel the failed runs as passes.

Installed Analog Lab V/MVibratoMB regression passed Pattern and Song on the preceding integration bytes: 512 virtual callbacks each, 442/435 rapid note/curve publications, maxima 0.1652/0.1584 ms, zero silent blocks/overruns, retained workers/graphs and no lost/dropped packets. Evidence: `build/automation-hosted-final`. A final matching runner repeat is recorded separately in the integration provenance. This is hosting native Mixer automation around actual plugins; it is not VST3 parameter automation or clean-output licensing/listening evidence.

Matching runner repeat (`build/automation-hosted-matching/process.json`, owned PID2696 creation2026-10-13T21:59:38.9570510Z, exit0) passed 512 callbacks per mode, 448/434 rapid publications, maxima0.1974/0.2552ms, zero silence/overruns and retained workers/graphs. Packet reports retain submitted516/missed0/dropped0 per provider/mode.

Still required: additional curve families and reference tension/overlap qualification; Channel/pan/generic links; native VST3 parameter enumeration and isolated sample-offset delivery; controller recording/touch/automation event rules; note-triggered automation generators; native numeric-dialog/source-window/axis gestures and full uninterrupted MIDI-recording qualification; large-session/upstream callback audits, hardware/listening/familiarity and exact package qualification. Built-in effects, Tune, recovery and the remaining M2–M6 roadmap stay open.

## Reproduce / resume

Build and run `scripts/test.ps1 -Configuration Release` (now 10 suites). Run `python tests/mcp_acceptance.py <absolute-app-path> <build-output-folder> 0.0.26`. Native OS input is explicitly authorized by AGENTS.md; announce foreground/cursor use, then run `python scripts/verify-automation-desktop.py <absolute-app-path> <build-output-folder>`. It guards foreground/hit ownership and recent user input, captures only owned windows and gracefully closes its retained subprocess. The hosted runner accepts `--automation-hosted-check <absolute-existing-hosted-project> <absolute-output-folder>`; keep proprietary provider state out of distributable artifacts.

Next: use the new callback/lifecycle diagnostics to resolve or explicitly bound the retained controller deadline qualification without weakening its gate. Add automation-specific package checks and qualify exact bundled bytes; continue parameter enumeration/isolated delivery and the broader AU-01/effects backlog. Preserve prior qualified previews and the failed acceptance receipts. No unattended work is implied after a run ends.
