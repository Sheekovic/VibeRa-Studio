> Historical verification record. This runnable bundle was retired. Launch only `dist/VibeRa Studio/VibeRa Studio.exe`; see [current status](STATUS.md) and [distribution policy](DISTRIBUTION.md).

# Preview 0.0.15

## Current qualification

Includes development Slice/Slip/Mute and schema4 from 5c0461c, plus the 18-strip Mixer, expanded Playlist toolbar, editor/overflow viewports and Settings > Colours described in WORKSPACE_NAVIGATION.md. Build uses unchanged pinned dependencies; VibeRa Plugin Worker resource version follows 0.0.15. Third-party plugins/models/presets and Reference inputs are not bundled. Preview 0.0.14 remains intact.

Current source application SHA256: `06d394e71001b4938116bebb53915e0d1d294cd0b0ad879ea7da506b6ba7cde6`.
Runner: `7f47365e0852310311ef98d6b483546a2b6f4d477f04f5a45377a22bc7a3b4e1`.
Worker: `439e93aa59b115adfebfc6a00c54d334b45fccb579e430ec46074ee546aff007`.

- Release CTest 6/6: 0.05 / 29.88 / 10.96 / 1.86 / 2.76 / 11.96 seconds. Musical/GUI checks include Insert17 routing/gain/WAV, migration/round-trip/undo, actual scrollbar/shortcut handlers, appearance save/load/reset/corruption and existing DAW regressions.
- MCP 152 checks, exit0, actual stdio/component PNG, hash bound. Retained UTF-8 reply String regression remains covered.
- Installed Analog Lab V/MVibratoMB full provider suite exit0; exact identities in `build/playlist-provider-provenance.json`. Scanning/containment/native editor/state/offline WAV/live Pattern/Song, MIDI, recording and mixed audio qualification remain virtual-output evidence, not listening or hardware evidence.
- Current built-in live Playlist: 189 cycles/two loops, zero right-silent blocks/overruns, maximum0.2345ms; graph notifications2→650. Current installed instrument live Playlist:167 cycles/two loops, zero right-silent blocks/overruns, maximum0.2275ms; graph2→578, exact undo/redo, retained worker/device/Edit/context. Baseline warmed with actual signal and zero worker misses before edits. Includes mute/slice/slip/loop/sustained-note cases; does not prove frame-perfect unaffected voices or allocation/lock safety across all upstream graph paths.
- Native Windows: owned preference-isolated populated Midnight Session, actual PrintWindow captures at1080p/4K/DPI144; caption/buttons/edge hit tests and targeted minimize/maximize/restore passed. Captures opened and visually inspected. No global or physical musical input, listening, Windows Snap-menu or mixed-monitor DPI qualification follows. Component scaling views separately cover100/150/200%.

## Failures retained

An earlier pre-workspace 0.0.15 binary failed live qualification of the old schema3 hosted fixture twice:117 cycles,1527 silent right blocks, worker not retained. Added a baseline gate caught instrument worker timeout with zero warm signal *before any edits*, while the effect worker remained available. Old/fresh saved instrument state was identical; descriptor scan timestamps differed, which does not establish causality. Fresh current fixture passes. Root cause/general startup reliability is unresolved; the older-fixture recheck is recorded separately. Do not infer the Mixer or descriptor changes fixed it, increase buffers/timeouts to hide it, or discard original failures. `build/playlist-live-initial-failure.txt`, `playlist-live-diagnostic/`, `playlist-live-legacy-provenance.json` and current baseline/worker traces retain this distinction.

The initial colour GUI test used the pinned JUCE selector's synchronous RGB update and failed with a partly updated colour. The GUI fixture now uses normal asynchronous notifications and theme refresh is deferred outside picker updates; reset, exact profile recovery and musical-state preservation pass. Initial CTest failure is retained.

## Package gate and continuation

Current older schema3 fixture recheck also passed:169 cycles/two loops, zero right-silent blocks/overruns, maximum0.2865ms, retained workers, exact undo/redo, graph2→579. This is positive compatibility evidence, not an explanation or fix for the earlier startup timeout. `build/playlist-live-legacy-provenance.json` binds the current app and older fixture; raw baseline and worker receipts remain under `build/playlist-live-legacy-recheck`.

Package assembly/testing follows the exact local source checkpoint; completion evidence is appended below when actually performed. Do not mistake source qualification for a packaged release. Use `scripts/package.ps1 -VerificationPrefix playlist -AllowReferenceInputs`, then bundled runner/MCP and provenance/hash/archive checks. Reference remains untracked, excluded from source and runtime. Package publication is not authorized by local packaging.

Next engineering: reproduce instrument pre-edit startup timeout using retained state/worker traces; continue AUDIO_AUDIT upstream graph qualification and full FEATURE_PARITY roadmap. Prioritize familiar independent Playlist Select/Delete/Zoom modes and M2 acceptance scenarios after these user-requested navigation changes. All listening/hardware/human familiarity gates remain pending. No unattended work is implied after a turn ends; resume from STATUS and these exact evidence paths.

## Local package completion

`artifacts/preview-0.0.15/VibeRa Studio.exe` is assembled and tested. Bundled musical runner passed3242 assertions; packaged GUI/render, CoProducer context, asynchronous MIDI, Playlist handlers and concurrent built-in live suite passed. Packaged live189 cycles/two loops: zero isolated-right silent blocks/overruns, max0.2032ms, exact undo/redo, retained context/device; graph2?650 remains. Packaged MCP152 checks passed against the exact app SHA above. Final docs/source archive are refreshed after this documentation checkpoint; provenance retains compiled source checkpoint8445a84 separately from final documentation/source commit. No new binaries were built after qualification. Manifest hashes and corresponding source exclusions/script matches are verified separately; previous0.0.14 app hash remains0356b4b028604ea1992d2aa174cdf276b10a4622c3502564cf6e6b3b58864b40. All owned jobs terminal.
