# VibeRa Tune: corrected-audio candidate comparison

## Latest streaming and allocator qualification (2026-10-15)

This continues the same research; the earlier failures below remain historical
evidence. The native app and sole packaged0.0.39 distribution are unchanged.
No Tune effect/UI or future standalone plugin is enabled.

| Experiment (`build/` prefix) | Measured result / decision |
| --- | --- |
| tune-output-source-20261015 | Preparing one additional real input sample when outRatio>1 removes438 previous underflow messages; stereo8 still fails. No fallback padding substituted for success. |
| tune-stereo-apart-20261015 | Separate-channel processing worsens stereo failures to34; rejected. |
| tune-linked-phase-fixed-input-20261015 | Shared reference-channel pitch phase plus retained input phase difference removes measured stereo failures;36 pitch cases still pass. Actual child exits0; outer wrapper falsely reports Git stderr notice, subsequently corrected. |
| tune-streaming-expanded-20261015 |66 frequency cases,29753 checks; all then-defined gates pass. Predates new impulse gates, so not current full qualification. |
| tune-startup-latency-20261015 / tune-live-alignment-20261015 | Early partial-block ratio requests deferred until prepared first shift; cached declared latency and reset are stable, but actual startup/live impulse timing fails9/15 and12/15. |
| tune-resource-audit-red-20261015 | Ordinary-new hooks miss360 aligned/C allocate/free pairs. Instrumented generic allocator plus128 retained stack records identifies resampler scratch growth and temporary vector storage replacement;592 records omitted, counts retained. |
| tune-resource-reuse-20261015 | Prepare scratch for the existing resampled request; replace temporary buffer assignment with capacity-reusing assign. All instrumented allocate/free calls0. All19 WAVs byte-identical to allocator-red run. Latency9/12 remains red. |
| tune-qualified-scope-20261015 | Latest33796 checks/66 cases; max0.383925cents; pitch/stereo/overruns/provider/ordinary-new-delete/instrumented allocator counts0. Max shifting1.5396ms; separate analysis2.4739ms. Initial impulse failures9, live12, reset0; exit1. |

Expanded coverage uses44.1/48/96kHz, ratios0.5..2:30 direct three-second
pure/harmonic transpositions (maximum0.223411cents),1536 silent plus1536 harmonic
ratio-transition blocks and early127-frame requests long enough to emit actual
non-silent output at every rate. Stereo checks retain the1e-4 criterion across
startup and steady output. No device-buffer, processing-block, analysis-window
increase or relaxation of the existing pitch/stereo criteria is used.

The new impulse criterion requires exact unity alignment and at most one frame
of fractional rounding elsewhere. It is an engineering transient-alignment
diagnostic, not a universal definition of nonlinear-shifter latency. At48kHz,
prepared ratio0.5 declares4160 frames but peaks3715;0.75 declares3114/peaks2987;
1 declares/peaks2624;1.5 declares3050/peaks3135;2 declares/peaks3392. With neutral
startup followed by live changes,12/15 peaks disagree with cached delay. Largest
initial discrepancy1469 frames at96kHz. Reset renders are identical15/15.
CSV: `streaming-impulses.csv`, `streaming-live-impulses.csv`. Opened plot:
`build/tune-latency-review/startup-impulse-alignment.png`, using the earlier
`tune-alignment-final-20261015` CSV/WAVs; synthetic, not desktop/listening evidence.

The pinned public LiveShifter header describes startup delay and preparing the
initial pitch scale before querying it. That contract alone does not prove live
wet/dry compensation. No assertion of Auto-Tune timing or DSP equivalence follows.

Resource evidence covers ordinary C++ new/delete and instrumented Rubber Band
generic allocate/deallocate entrypoints, not all CRT/backend operations, blocking
locks, the full application graph or real-world callback scheduling. Reset and
construction remain control-side work. Analysis is separately measured and not
qualified for execution inside the callback.

Source changes are explicitly fingerprinted in four `experiments/tune/`
manifests: `rubberband-preparation.json` (capacity/storage reuse),
`rubberband-output-source.json` (real input reserve/scratch preparation),
`rubberband-linked-phase.json` (stereo), `rubberband-resource-observer.json`
(compile-gated measurement). The applicator accepts only exact original/current
or recorded legacy hashes, preserves unknown work and never resets the checkout.
Actual source/binary hashes are retained per run. No enabled dependency pin or
license changed. Local CRLF/LF bytes differ from Git blobs; validation uses the
recorded actual-file hashes rather than equating them with normalized blobs.

Bounded MIT Signalsmith comparisons also fail: Dense120/5ms has pitch3/stereo35,
DenseDouble pitch3/stereo36, Short40/2ms pitch10/stereo0. An independent
phase-frequency-map experiment improves Short to pitch9/stereo0, max7.71235cents,
still red. Evidence folders: `tune-signalsmith-dense-20261015`,
`tune-signalsmith-dense-double-20261015`, `tune-signalsmith-short-20261015`,
`tune-signal-phase-short-20261015`. `signalsmith-phase-frequency.json` pins that
research-only modification; engine vendor source is untouched. Rubber-specific
streaming/impulse/provider tests are NOT RUN for these profiles; their zero fields
do not mean pass. Latest reports expose coverage/availability fields explicitly.

Retained orchestration failures: initial linked-phase manifest generation failed
before target launch; initial storage-upgrade generation compared normalized Git
bytes with a CRLF fingerprint and refused changes. The subsequent
`tune-resource-audit-prepared-20261015` remained an unchanged red run, not a fix.
The corrected exact-byte upgrade is `tune-resource-reuse-20261015`.

Reproduce the current candidate (fresh evidence name; expected exit1 for latency):

```powershell
powershell -ExecutionPolicy Bypass -File scripts/verify-tune-correction.ps1 -RubberBand build/tune-rubberband-1d95888 -PreparedResampler -PreparedOutput -LinkedPhase -ResourceAudit -BuildDirectory tune-rubber-experiment -Evidence tune-next-streaming
```

Next: distinguish measured transient displacement from declared pipeline delay
with marked multi-impulse/voiced-onset cases across live ratio transitions; choose
and verify an explicit alignment policy before integrating dry/bypass paths.
Retain existing tight diagnostics. Then qualify voiced/unvoiced/vibrato/formant
material and full callback resource lifetime before a typed original Mixer
effect/state/UI/export implementation. Real vocals, listening and hardware remain
unperformed; this research does not explain unrelated native sampler stalls.

## Earlier 2026-10-15: allocation and processing diagnostics

Current application remains0.0.39. The0.0.24 identity below is historical.
New proprietary-reference evidence is reviewed in AUTOTUNE_EVIDENCE_REVIEW.md;
this continuation uses original experiment code and public GPL Rubber Band
source, with no Antares algorithm/assets or enabled DAW dependency.

The optional fixed-storage trace captures23 actual C++ new stacks, all44.1kHz
phase-table growth in BQResampler::phase_data_for. Prepared capacity was88200
entries while this quality's rational limit is96000. Startup temporary buffers
do not explain these records. `experiments/tune/rubberband-preparation.json`
pins upstream1d95888 and a one-line preparation change: at least rational_max+2
entries for the tested half-to-double range. Original source SHA256
058efe3179a80188050f69d67e865f96f2a33119a4a06929368532bbddd6ccab; prepared
7107bd4b83c4a41ccc9cfaf4566a9eae621a6a49fd4b1e6b8e9674745e031451.
The applicator refuses other revisions, unexpected bytes/paths or other tracked
candidate changes; it never resets work. GPL notices and production pins remain.

| Run (`build/` prefix) | Actual result |
| --- | --- |
| tune-allocation-trace-20261015 |23 symbolized allocations;36 pitch cases within0.383925cents; stereo8/allocating-block23 FAIL. Stack tracing is diagnostic. |
| tune-prepared-resampler-20261015 | Same36 cases, allocations0/overruns0/max0.9659ms; stereo8 FAIL. Corrected harmonic WAV byte-identical SHA256fce1cbd408d370f71ad2fe93e739e9ca9d467bd05ef4b79ab523d91cd7854d10. |
| tune-prepared-ratio-stress-20261015 |1536 later ratio changes at44.1/48/96kHz, ratio0.5..2: allocations0,219 overruns/max781.666ms; stereo8 FAIL. Neutral initial preparation precedes counting; non-unity startup remains unqualified. |
| tune-prepared-ratio-trace-20261015 | Fresh per-block ratio/cycle CSV:219 overruns/max806.662ms;438 provider messages. Aggregate max now includes stress, correcting the preceding report's matrix-only max field. |
| tune-prepared-ftz-20261015 | Scoped x64 FTZ/DAZ still219 overruns/max786.379ms; stereo8 FAIL. This does not fix the measured path; experimental option defaults off. |
| tune-bounded-provider-20261015 | Prepared logger replaces processing stderr I/O.1536 ratio blocks: allocations0/overruns0; rate maxima0.6804/0.7638/1.1778ms. Whole run max1.2757ms/pitch36 within0.383925cents. **ProviderMessages438/stereoFailures8 remain FAIL**, exit1/28181 checks. |

Debug level0 still sent library errors to default stderr during processing.
New64-entry/192-character storage retains text/numeric arguments, total and
dropped counts without allocation, I/O or locks. Each stress rate retains64 of146
messages and explicitly reports82 dropped records. CSV retains every block's
count delta; full drain occurs after processing. One processing owner only,
read after its call returns: not yet a concurrent UI/worker-transfer API. Provider
messages now fail acceptance, so faster timing cannot hide output underflows.

Removing default logger I/O eliminates these observed stress overruns; this is
not proof about the unrelated application sampler stalls or all scheduling
conditions. Ordinary new hooks do not cover all C/aligned allocation/destruction.
The library reports511/512 output underflows after rapid changes and has an
existing fallback fill; do not treat it as successful audio. Startup stereo,
changing wet/dry alignment, actual vocals/listening and full RT remain open.
Next inspect required output-source count/resampler history across changes,
then stereo startup and latency with non-silent transitions. No Tune Mixer
effect, functional UI or standalone VST3 is delivered.

Reproduce with the existing candidate and sole compilation cache:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/verify-tune-correction.ps1 -RubberBand build/tune-rubberband-1d95888 -PreparedResampler -BuildDirectory tune-rubber-experiment -Evidence tune-next-fresh
```

Use a fresh evidence name. `-TraceAllocations` adds diagnostic stacks;
`-FlushDenormals` selects the retained comparison. Current commands are expected
to fail provider/stereo gates. Provenance records candidate and binary hashes;
earlier failure evidence is retained rather than reclassified.

## Initial experiment (historical)

2026-10-13. Independent original detector, target policy, correction amount/speed and prepared stereo bridge are in experiments/tune. No Antares implementation/assets are used. Application0.0.24 remains unchanged, SHA25659263e69ae36902ecc52f57e269d43cf5fbe30df4d5806ce6ef22284611ebb48. This experiment does not expose an effect in the DAW.

## Defined gates and results

36 three-second synthetic pure/harmonic stereo cases at44.1/48/96kHz: detuned110/220/440/880Hz, an excluded major-scale pitch and referenceA442. Independent Hann/spectral peak estimate checks corrected fundamental within5cents. Right input is minus half left to check stereo coherence at1e-4 throughout startup and steady output. Analysis2048/4096-frame observations is separate from256-frame prepared shifting. Ordinary C++ allocation hooks and wall-clock block deadlines are measured; neither is a complete realtime audit. Model policy additionally tests invalid settings, live amount/speed and unvoiced release. Variable127-frame chunks check exact delayed bypass; final Rubber Band run checks wet unity impulse delay too.

| Candidate / evidence | Pitch gate | Stereo / allocations | Timing / outcome |
| --- | --- | --- | --- |
| Engine-pinned Signalsmith1.3.2; build/tune-correction/verification-pinned-matrix-failure.txt |12/36 failed, max18.8197cents |24 stereo failures; prepared ordinary allocation gate passed | max shifting0.247ms, analysis2.4235ms, zero shifter overruns; failed candidate |
| Upstream Signalsmith a670068d9aeb64913331d5cc29337b19a457a7df + Linear0.6.4 de55e6a50ffcf6f8f43f649692d94691c7025151; build/tune-correction-upstream | Same12 pitch failures/max18.8197cents |24 stereo failures | shifting0.2053ms, analysis2.4106ms; failed candidate |
| Known exact source pitch instead of detector; build/tune-correction-known | Same12 pitch failures/max18.8218cents |22 stereo failures | shifting0.1745ms; this rules out detector error as the explanation for those pitch failures, not every possible integration issue |
| Rubber Band4.0.0 LiveShifter 1d95888bec3ae0a17c0c4af791810d5a63f6bc35; build/tune-correction-rubber |0 pitch failures, max0.383925cents |8 full-span stereo failures,23 allocating blocks. Steady stereo max1.83284e-6 after1second; startup differences remain | shifting1.0662ms, analysis2.5627ms, zero measured shifter overruns. Full candidate still FAIL |

Final Rubber Band binary SHA256f2433deb521b68b06dd307629ab8b53870b22d81af1209eee23aa9a8ebf5df19, ownedPID37692 creation2026-10-13T13:47:01.9969427Z terminal/exit1. Wet unity impulse peak2624frames matches reported delay at48kHz; amplitude0.670534 from0.7input. Ratio-dependent reported delays differ across cases; dry delay is currently prepared at unity, so this is NOT live bypass alignment qualification after retuning. The fixed512-frame bridge adds512frames and never waits on a worker. Stereo short initialization differences, allocation locations, dynamic ratio delay and realistic voice quality must be resolved before app integration. Original and corrected harmonic WAVs in build/tune-correction-rubber are synthetic artifacts, not DAW export or heard audio.

Initial pinned one-case error15.8418cents, initial Rubber allocation failure and preceding matrices/provenance remain retained. The upstream comparison initially failed to compile without its separately published Linear include dependency; corrected include setup is explicit in the script. Failed gates remain failures; collection continues through cases rather than masking the result. No lowered tolerance, increased device buffer or muted playback.

## Reproduce

Run scripts/fetch-tune-candidates.ps1 for exact isolated upstream revisions; it preserves an unexpected existing checkout instead of resetting it. Production dependencies.lock.json remains unchanged. Then:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/verify-tune-correction.ps1
powershell -ExecutionPolicy Bypass -File scripts/verify-tune-correction.ps1 -KnownPitch -Evidence tune-correction-known
powershell -ExecutionPolicy Bypass -File scripts/verify-tune-correction.ps1 -Candidate build/tune-upstream-a670068 -Linear build/tune-linear-de55e6a/include -Evidence tune-correction-upstream -BuildDirectory tune-upstream-experiment
powershell -ExecutionPolicy Bypass -File scripts/verify-tune-correction.ps1 -RubberBand build/tune-rubberband-1d95888 -Evidence tune-correction-rubber -BuildDirectory tune-rubber-experiment
```

Each command records build logs, actual exit, executable hash and owned-process receipt. Expect failed candidate gates, not a successful product qualification. Timing varies with machine load. Older reports cover their exact earlier binaries; reruns must not be presented as those old bytes. Future reruns should use distinct Evidence folders to retain previous results.

## Licensing and primary sources

Signalsmith/Linear MIT texts were read; notices already retained under licenses/third-party. Rubber Band README/header explicitly grants GPL2-or-later, with actual COPYING retained as RubberBand-COPYING.txt. The GPL route is used for this open-source experiment; no commercial license purchased or proprietary-source distribution promised. GPL3 combination is permitted by AGPL3 section13, retaining third-party notices/source. Public packaging/SBOM checks still apply before shipping an enabled runtime dependency.

Primary sources: [Signalsmith processing and latency](https://signalsmith-audio.com/code/stretch/), [Rubber Band licensing](https://breakfastquay.com/rubberband/license.html), [integration notes](https://www.breakfastquay.com/rubberband/integration.html), and the pinned source headers/COPYING. Documented latency/API behavior is separate from measured evidence; no Auto-Tune equivalence claim.

## Earlier continuation note (superseded by latest section)

User now prioritizes native Launchkey/controller mappings. Preserve this experiment and resume afterward: locate candidate allocation call paths during ratio changes, test bounded preparation or off-callback processing without blocking waits, qualify ratio-dependent wet/dry latency and startup coherence, add vocal/vibrato/transient transitions, then typed original Mixer effect/state/UI/export. The earlier sampler package failures remain a separate open gate. Do not enable a failed candidate just to display VibeRa Tune controls.
