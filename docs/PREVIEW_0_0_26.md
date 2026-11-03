> Historical verification record. This runnable bundle was retired. Launch only `dist/VibeRa Studio/VibeRa Studio.exe`; see [current status](STATUS.md) and [distribution policy](DISTRIBUTION.md).

# Preview 0.0.26: Automation Clips

Preview0.0.26 is packaged and qualified locally for the subset below. Runnable artifact: `artifacts/preview-0.0.26/VibeRa Studio.exe`. Preview0.0.25 is preserved. This checkpoint advances AU-01; the complete automation family, full FL parity and M2–M6 remain open.

Final bundled evaluator/matrix, Automation Clip/MIDI take/properties/recorded-note round-trip/export, CoProducer context, smoke/MIDI/Playlist, sampler and controller gates passed. Bundled musical27166 assertions/MCP408 and matching native Windows creation/point gestures/Undo/Redo passed; fresh native/component captures inspected. Sampler Stopped/Pattern/Song461callbacks each measured0.1813/0.1220/0.1445ms with zero overruns; controller1280callbacks per mode measured0.2203/0.2238ms with zero overruns. Automation held-note take96callbacks per mode measured0.0739/0.1908ms. Exact application remainsbde46c1b...; implementation/source archive checkpointc1f9f5b. Final14-file SHA256/process/MCP/native audit: `artifacts/preview-0.0.26/evidence/final-package-verification.json`. Source/manifests/notices/pinned dependency archives and all three timing failures are included. No publication/hardware/listening/full RT or full feature acceptance claim.

The first finalizer looked for CoProducer manifest basenames at the package root and encountered path errors. That receipt was invalidated, and a strict namespace-aware audit verified all14 files before qualification. The corrected receipt uses full relative paths. This was an audit-path mistake, not a missing runtime or passing test substituted for the file audit.

The first assembled bundle failed sampler gesture Pattern playback at5.9334ms/one overrun; all recorded model/MIDI/graph/device flags were retained. No package receipt was produced. Its executable/source archive and failure/provenance remain in `build/automation-bundled-sampler-failure`. Diagnostic continuation adds bounded per-callback sampler gesture phase/ordinal/wall/raw-cycle/thread-context traces, preserving461callbacks per mode and the unchanged deadline. Latest diagnostic source `bde46c1b215a7cedc0979d83ed773d61236ed5b84cafef5d604b37379acc9c06` passed10/10 in79.38s (musical27162/automation12659), six controller runs/15360callbacks/max0.248ms, MCP408 and matching provider/opening/hosted/native checks. Relocated sampler trace passed with Stopped/Pattern/Song maxima0.4099/0.2066/0.1358ms. This is observation, not an explanation of the failed bundle. The later exact-byte assembly and final receipt above passed; the initial failure remains unqualified and retained.

## Included scope

Mixer/Master Automation Clips, normalized points and typed target ranges, initialized controls, source-window placements, source/target properties, exact schema8 persistence/migrations and canonical Undo/Redo. Original prepared gain stages share immutable programs between live and offline graphs. Pattern/stopped monitoring retain manual gain. See [AUTOMATION_CLIPS.md](AUTOMATION_CLIPS.md).

Continuation adds continuous right-drag after point insertion, Shift-right insertion preserving the curve's current level, Ctrl/Shift axis constraints, left-border source trimming and real Escape cancellation. Escape cancels an active edit before cancelling a MIDI take; with no active edit, the existing take-cancel behavior remains. A stale point gesture drops its IDs after Project replacement. Target selection displays that link's actual ranges; its default queued notification is flushed before callbacks are attached, preventing freshly typed values from being overwritten.

Injected MIDI take qualification uses the actual curve/shortcut handlers and real Tracktion virtual output. Pattern/Song held notes survive12 curve drags and Escape cancellation; Stop commits exactly one take, and Undo/Redo preserves prior automation changes. Saved recorded notes plus edited curves round-trip and export through the actual DAW. Native OS creation/point insertion/left drag/Undo/Redo are separate checks; numeric/axis/source-window native input, physical hardware, listening and human familiarity remain pending.

## Source qualification

Reviewed source application SHA256 `8389a22d28dd75ae34a518a93880e490209a65eb5a20e72ecc26657857e74ae3`; runner `965b688599cc56fd22eddf7c010c609f95fcceb29766c65032c2ea1d49ba58da`; worker `3e2baa302e476c214ffe705d7c244740aac6ee3b7d78d8b0b9c80a2655054c2f`. Source CTest10/10 in79.81s, musical27165 and standalone automation12659 assertions. MIDI take maxima0.1745/0.1396ms over96 callbacks each, zero overruns and unchanged capture/device/Edit/context/sampler/gain graphs. Fresh component renders inspected.

Six controller runs cover15360 measured callbacks, maximum0.2755ms, zero measured overruns and retained lifecycle. Raw CSVs, median/p99/p999 and exact owned process identities live in `build/automation-controller-bound` and `build/automation-source-qualification.json`. Test priorities and the256-frame/48kHz/5.333ms deadline are unchanged.

Matching MCP408, installed Analog Lab V/MVibratoMB scan/processing/state/export/native editor and name-opening regression passed. Hosted Playlist168actions/two loops/max0.2701ms/no silence-overruns/retained workers and exact Undo/Redo passed. Source native fader creation/point gestures/Undo/Redo and caption/edge/system-command checks passed; captures were inspected. Provider/project state remains local and is not included in distributable evidence.

Exact commands/receipts: `scripts/verify-automation-release.ps1 -Configuration Release -ControllerRuns 6`, `scripts/verify-controller-providers.ps1 -Prefix automation`, `build/automation-source-qualification.json`, `automation-provider-provenance.json`, `automation-opening-processes.json`, `automation-live-plugin-process.json`, `automation-os-input/process.json`, and `automation-native/native-window-verification.txt`.

## Retained failures and limits

The earlier controller callback27.9823ms against5.333ms is unestablished, despite subsequent passing runs. The first continuation source suite also failed Stopped sampler preview:413callbacks, maximum5.4364ms/one overrun. Its exact suite/result/binary identity is retained in `build/automation-sampler-deadline-failure`. Per-callback wall time/raw Windows thread-cycle/thread-context diagnostics now retain all sampler modes; observations do not establish either failure's cause. No buffer increase, priority adjustment, muted playback or assertion weakening was used. Later passes qualify their stated measured scope only.

Initial new axis test incorrectly attempted to increase a saturated value; the fixture now moves into the valid range while still requiring both the constrained and changed axes. A new target-range test exposed the real queued ComboBox-selection race described above. Those failed outputs remain in `build/automation-continuity-first`, `automation-take-first`, and `automation-take-reviewed`.

Hardware/listening, whole-graph real-time guarantees, long-session performance, physical controllers, all curve families/LFO/event automation, Channel/pan/generic links, isolated VST3 parameter delivery, broader CoProducer/Tune and the remaining roadmap are not qualified. Publication is not authorized by this local package work.

## Package gate and continuation

`scripts/package.ps1 -VerificationPrefix automation -AllowReferenceInputs` inherits every controller-preview gate and adds source/executable hash matching, explicit retained timing failures, bounded timing receipts, bundled evaluator/matrix, actual Automation Clip/MIDI take/property/round-trip/export checks and the relocated full musical suite. Bundled MCP/native checks and the final matching package receipt passed as recorded above. No installed proprietary provider state or Reference inputs are shipped.

Next: stable isolated plugin parameter enumeration/delivery and remaining AU-01/original-effect work. Keep the unresolved RT diagnostics and hardware/familiarity gates active. All task-owned qualification processes are terminal; no unattended work continues after a run ends.
