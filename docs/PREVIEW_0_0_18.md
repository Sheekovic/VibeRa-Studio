> Historical verification record. This runnable bundle was retired. Launch only `dist/VibeRa Studio/VibeRa Studio.exe`; see [current status](STATUS.md) and [distribution policy](DISTRIBUTION.md).

# Preview 0.0.18 qualification

2026-10-13. Local packaged executable: `artifacts/preview-0.0.18/VibeRa Studio.exe`. Full feature/UI parity, hardware/listening, human familiarity and release gates remain incomplete. The full tracker is [ROADMAP.md](ROADMAP.md).

This checkpoint adds familiar Pattern management, stable-ID metadata/order/insert/delete and clone-after-source, F2 name/colour and F4 empty navigation, scoped/customizable Pattern bindings with profile migration, recording-safe metadata edits and owned dialog teardown. Ctrl+L reaches all actual Mixer inserts and preserves the document when none is free. Details, reference evidence, compatibility and remaining differences: [PATTERN_MANAGEMENT.md](PATTERN_MANAGEMENT.md).

## Identity

Compiled implementation/source checkpoint: `603502a8969f131e911d835f92147a78b5d4c5c2`. Its rebuild retained the qualified application/runner/worker bytes. Final documentation/source checkpoint is recorded separately in package provenance.

| Binary | SHA256 |
| --- | --- |
| Application | `99a3b6697252547c78b276f0839f575b8f6a654743b47e9a0cc39ddae5a019e2` |
| Musical runner | `9023905fd8b45a02a868d655d2ba4c3a34cda4a31219a6dfb9db09014833d912` |
| Plugin worker | `2939d400590e2ed5cbb3819e07dc799cd5cc086b40c9d4d37085283a5cec0149` |

Preview0.0.17 remains unchanged: application SHA256 `1fb2f16556ae78913d46b66e0fdefcd378f2c8a3804abfa836f39e5378cfa8f4`.

## Actual verification

- Final source Release CTest6/6:0.05/31.93/11.15/1.76/3.02/12.04seconds. Exact model/PCM/round-trip/undo/failure/profile tests, GUI/MIDI/context/Playlist/live regression.
- Source and packaged actual-application MCP297 checks each; nine tools, contextual handlers, canonical Pattern operations and dialog exclusion. This is protocol/component input, not OS physical input or listening.
- Bundled musical runner3316 assertions; bundled GUI/frame/layout, context, MIDI, Audio Playlist/live and dedicated Pattern workflow passed.
- Source and bundled Pattern tests exercise the actual F2/modal callback, note-on before and note-off after metadata/reorder, captured-note pairing, stable device/Edit/context/graph, exact Undo/Redo, F4/create and dialog owner lifetime. Actual Analog Lab112 callbacks per mode: maxima0.1339/0.2003ms, zero right-silent blocks/overruns. Bundled built-in maxima0.2423/0.1420ms.
- Full installed Analog Lab V/MVibratoMB suite passed. Subsequent UI-only fixes retained those exact qualified runner/worker bytes; final current application separately passed held-MIDI/Pattern and actual hosted Playlist requalification. Current hosted live168 cycles/two loops: maximum0.1867ms, zero right-silent blocks/overruns; workers retained, exact Undo/Redo. Wave graph notifications2->578 remain.
- Packaged live Playlist189 cycles/two loops: maximum0.2437ms, zero right-silent blocks/overruns; graph notifications2->650 remain. No whole-upstream RT or frame-perfect unaffected-voice claim.
- Current-binary native OS mouse opened the real Pattern menu; F2 opened properties; Escape cancelled. Main/menu/dialog captures at DPI144 were opened and inspected. Main1080p/4K PrintWindow/native caption/button/edge/system-command checks passed. This is scoped OS synthetic input, not human physical interaction or hardware listening.

Evidence is under `build/pattern-*` and package `evidence`. Native files include `pattern-os-input/native-pattern-menu.png`, `native-pattern-properties.png` and `pattern-native/native-dark-title-1080.png` / `native-dark-title-4k.png`. Packaged Pattern metrics are in `packaged-pattern-workflow/pattern-workflow-verification.txt`. Actual installed plugin state/presets and raw FL analysis are excluded from shipped source/provider evidence.

Initial WAV test failure is preserved: creation metadata differed, PCM was identical; the corrected test compares every decoded sample exactly. A separate native Rack repeat stopped at an obscured owned caption before activation input. That attempt is retained as not completed; prior0.0.17 native Rack qualification remains historical. Current native Pattern tests are separate passing evidence.

## Limits and next actions

Pattern palette/random-F2 cycling, unused-slot semantics, groups/picker, split-by-Channel, per-pattern meter, transpose/render variants and exact native delete/Undo selection remain incomplete. Colour is optional schema4 display metadata; older previews can lose it on re-save. Scope6 bindings need this application. No feature-family completion, FL runtime parity, production readiness, heard audio or hardware/latency claim follows from the checks.

Continue with sampler pitch/root/trim/envelopes, foundational automation/effects and broader Pattern discovery. Preserve detached Browser/native-input and restart recovery tasks. Existing plugin pre-edit startup timeout and whole-upstream RT audit remain unresolved. All owned jobs for this checkpoint are terminal. No remote push/publication occurred; resume from git status, STATUS, ROADMAP and exact evidence rather than chat memory.
