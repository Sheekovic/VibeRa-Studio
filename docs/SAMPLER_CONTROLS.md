# Sampler controls and qualification

Qualified preview0.0.19, 2026-10-13. This extends SA-01; the full sampler family remains incomplete. Package/evidence: [PREVIEW_0_0_19.md](PREVIEW_0_0_19.md).

## Functional controls

Click a sample/original instrument Channel name to reuse its settings window. Volume, pan, Mixer assignment and separate Piano Roll access remain. The scrollable settings view adds:

| Control | Musical behavior |
| --- | --- |
| Root MIDI | -1 Auto retains the prepared sample root; 0-127 calibrates the key at original speed. A higher root lowers the sound of a fixed played key. |
| Pitch (cents) | -4800 to +4800; 100 cents per semitone. Resampling changes both pitch and playback duration. Sounding voices approach the new speed over approximately 5 ms without resetting their source position. |
| Source start/end | Normalized percentages of the original PCM, with start strictly below end. Non-destructive bounds apply to subsequent notes. Invalid/inverted ranges reject atomically and the view restores the accepted value. |
| Reverse | Subsequent notes read the selected source range backwards. Existing voices retain direction/range. |
| Volume envelope | Disabled preserves the legacy sample behavior and 2 ms attack/12 ms release. Enabled makes even one-shot samples respond to note release, using attack/decay/sustain/release. |
| ADSR | Attack/decay/release 0-10 seconds; sustain 0-1. Subsequent voices capture their envelope settings so editing them does not reset held voices. Zero release ends on the next rendered frame. |

These controls target that window's Channel, even if another Channel is selected. They are unavailable for streamed Audio Clip Channels or VST3 instruments. VST3 Channels still open their real native editor. Settings retained on a Channel are inactive while it hosts a VST3 instrument.

**Preview source** explicitly auditions the original source before these controls. Play notes through the Channel to hear its settings. There is no claim that this button renders the adjusted instrument. A familiar preview keyboard/right-click root selector, draggable sample handles and tabbed advanced pages remain work to do.

## Familiarity and research evidence

| FL task | Expected interaction | VibeRa equivalent | Current difference | Evidence |
| --- | --- | --- | --- | --- |
| Calibrate sample root | Right-click the preview keyboard key | Root MIDI control, same calibration direction | Numeric selector; keyboard gesture pending | Official [Miscellaneous Channel Settings](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/chansettings_misc.htm); VibeRa frequency/GUI tests |
| Channel pitch | Adjust Channel pitch in cents | Pitch slider/text entry | Fixed four-octave range; separate FL stretch/pitch modes not implemented | Official [Sampler](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/chansettings_sampler.htm), installed static PitchWheel binding; VibeRa octave render |
| Source range/reverse | Sample controls/handles and reverse process | Non-destructive percentage bounds and Reverse | Handles, precomputed processes and sample-file editing remain pending | Official Sampler; installed static SmpStartWheel/SmpEndWheel/ReverseSampleBox declarations; duration/PCM tests |
| Note-controlled sample volume | Enable volume envelope, adjust ADSR | Enable Volume envelope and edit ADSR | Delay/hold/tension/amount/tempo/LFO/filter pages remain pending; parameters captured per voice | Official [Envelope / Instrument Settings](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/chansettings_ins.htm); release/shape/GUI tests |

Documentation is D evidence and may describe a newer version. Static resource declarations are not observed handler execution. No FL interaction was launched/observed in this continuation. Raw resource/artwork/code outputs are not application or package inputs. [REFERENCE_RESEARCH.md](REFERENCE_RESEARCH.md) retains provenance and unknowns.

## Engine and project decisions

Channel `SamplerSettings` are canonical document data. Ten stable `channel.sampler.*` commands participate in normal validation, undo, remapping and document macros; MCP exposes bounded canonical edits and readable settings. Edits preserve active MIDI capture. CoProducer context fingerprints include the settings so results cannot be accepted against a retuned destination.

The control thread prepares tuning factors in immutable `LiveChannel` snapshots using the existing three-slot pin/publication mechanism. Audio consumers copy only fixed-size settings and retarget the fixed 64-voice banks. Source PCM is reused. Pitch/root tables are prepared with the sample/device configuration; no exponential calculation, allocation, I/O, blocking wait, lock, graph replacement or resource destruction is added to the callback by these controls. Source-boundary/direction/envelope changes are captured at note-on; pitch updates retain voice phase/position. This is scoped source review, not proof that every upstream Tracktion path is realtime safe.

Project schema **5** stores explicit `SAMPLER` children. Versions 1-4 migrate with legacy-sound defaults; schema5 requires every setting and validates finite/ranged values. Failed reads preserve the destination. A sampler child masquerading as schema4 is rejected. Older previews reject schema5 instead of silently discarding audible settings; keep older files/backups when changing versions. Display-only Pattern colour remains supported.

## Verification records

- Initial build and six CTest suites passed before the expanded tests. The expanded build exposed an incorrect CoProducer test field; corrected to `options.selection.destination`, with the failed build retained at `build/sampler-build-final.log`.
- `build/sampler-ctest-expanded.log` retains a failed loop-count assertion: the fixture read the UI transport cursor during a tight synchronous callback loop. Audio-node playhead sampling fixes the measurement; the complete rerun passed six suites in 62.68 s (`build/sampler-ctest-loop-clock.log`).
- Corrected virtual Pattern and Song runs each processed 976 callbacks and crossed five loops. Maximum 0.1697/0.2233 ms versus 5.3333 ms, zero overruns/unaffected silent blocks/graph replacements/device starts/stops. Exact graph/context/per-Channel PCM identity and preparation counts retained. `build/windows/acceptance/sampler-live-verification.txt` is overwritten by the final runner and remains the authoritative latest measurement.
- Musical checks cover calibrated frequency, octave pitch, root direction, selected-source duration, reversed PCM, ADSR shape/one-shot release/default compatibility, configured offline export, all legacy migrations, invalid settings/atomic rejection and exact persistence/undo.
- GUI checks exercise actual root/pitch/range/ADSR/envelope handlers, Channel targeting, undo and viewport scrolling. `build/windows/gui_acceptance/sampler-controls.png` and `sampler-controls-bottom.png` are component renders, distinct from native OS input.
- Source MCP passed 304 checks after sampler integration; `build/sampler-mcp/verification.json` records the exact app hash. Native PrintWindow/hit/system-command checks passed at DPI144, including 1920x1080 and 3840x2088 captures. A sandbox attempt reported an unavailable rounded-corner preference; the authorized host desktop check passed. Native captures were inspected; this does not qualify musical mouse gestures or listening.

Final-build, installed-provider, packaged and bundled checks passed and are recorded in [PREVIEW_0_0_19.md](PREVIEW_0_0_19.md) and STATUS/VERIFICATION. No physical MIDI, listening, actual audio-device latency, clean-machine or full FL-parity claim is made here.

## Remaining sampler scope

Keep SA-01 open: preview keyboard/key zones, direct source handles/zoom, loop/ping-pong/crossfade, normalization, filter and modulation envelopes/LFO, delay/hold/tension/tempo sync, polyphony/choke/mono/portamento, stretching/resampling-quality choices, preset workflows, broader native gesture and listening checks. Current linear interpolation and source-boundary cuts need quality/listening qualification; no stretch or preserved-duration pitch shift is implied.
