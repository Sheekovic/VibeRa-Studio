# Additional rapid dynamic-latency waveform/loop acceptance draft

Status: source-only draft, not integrated, compiled, or run. No product runtime,
provider compatibility, callback timing, native input, hardware, or listening
qualification follows from the source-patch replay check.

## Exact change and integration

`integration.patch` changes only `tests/PluginDynamicWaveformAcceptance.h` and
adds `tests/PluginRapidWaveformAcceptance.h`. `provenance.json` records raw base,
postimage, patch and relevant dependency identities. No CMake, Acceptance.cpp,
application, fixture, vendor, distribution or Git source state was changed.
The existing CTest/CLI integration remains `mixer_dynamic_waveform` /
`VibeRaTests --dynamic-waveform-check <owned-folder> <clock-fixture> <delay-fixture>`.

The four original scenarios are preserved exactly: direct clock, direct
variable-delay chain, Pattern graph and Song graph. Their analyzer, controls,
warmup, PCM oracle, values, deadlines and callback gates are unchanged. An include
and one aggregate call add the scenarios below before the existing final require.
Removing those exact additions reconstructs the original file byte-for-byte.
The two new modes are both attempted before their aggregate result is returned;
a per-mode setup exception is retained rather than silently skipping the other.

Root integration owner should first confirm the raw base SHA and inspect/apply
the patch outside any active source-frozen run. Build the affected VibeRaTests,
VibeRaPluginWorker and both existing fixture targets through the owned incremental
runner; invoke the existing `mixer_dynamic_waveform` acceptance in isolation.
`scripts/dev.ps1 -AdditionalTest mixer_dynamic_waveform` is the supported runner
entry (its changed-file selection may also select other required checks).
No timeout property changes are needed or proposed; existing CTest90s and
per-driver12000ms remain. Do not package this internal risk experiment.

## Two additional scenarios and concrete required evidence

1. Pattern playback, actual one-beat loop enabled.
2. Song playback, actual placement-defined one-beat loop enabled.

Each uses the original SDK Clock Waveform and Mixer Latency fixtures in real
live PluginHost workers, actual AudioEngine projection, exact48kHz/256-frame
synthetic device, dry+wet unity routes and independently specified Master0.5 gain.
Three bursts per mode each request native delays1024→31→257→0:24 total new
requests, acknowledgements and consumed-generation confirmations, and six
musical boundaries must all be proved. No80-callback per-change dwell is used.
Requests target unlooped musical boundary offsets[-12,-2,+2,+6]callbacks, with
acknowledgement and actual packet consumption between requests. The first two
requests must occur before the intended boundary, the last two after; first and
last consumed transitions must also straddle it. A32-callback maximum intra-burst
request gap prevents a slow control path from falsely qualifying as rapid. This
is an additional coverage requirement, not a changed callback or IPC deadline.
The next boundary with a complete12-callback leading window is scheduled from
the playhead; no audio-output-derived offset or phase fitting occurs.

Exactly one initial200-callback warmup remains; every subsequent transition,
inter-burst callback and loop sample is included. The exact original oracle is
`ClockWaveform(sourceConsumedSequence*256 + n - 256 - wetConsumedNativeLatency)`.
Native variable read-tap phase changes are expected, but the two branches must
agree with this independently defined waveform at every measured sample.
Existing2e-6 error, finite sample, whole-new-packet, zero missed/dropped packets,
zero callback overruns/pacing misses and zero observed C++new/delete probes remain.
The latter does not qualify arbitrary CRT or third-party heap behavior.

The additional preallocated callback trace uses the exact retained playback
context's PlayHead scalar accessors. No control-only projectionDiagnostics call,
string creation, allocation, lock addition or file write occurs in that probe.
The context/head pointers are test-only borrows from existing diagnostic identity,
retained by AudioEngine until Capture joins; existing no-context-replacement,
worker PID, device start/stop, graph-init, cache and sampler checks remain.
The control thread only reads frames published by Capture.complete acquire.

Actual PlayHead loop range must be0..23040samples, derived from one beat125BPM.
For every measured callback the reference and unlooped head advance exactly256;
the actual looped head equals positive-modulo(unloopedHead,23040). A monotonic
source sequence must continue across actual decreasing musical-head wraps.
Each planned boundary requires a directly recorded crossing and looped decrease.
This separates absolute source-clock continuity from score looping; rewinding the
native fixture to musical zero, seeking/restarting transport or phase-fitting
output cannot pass. Latest requested/prepared/adopted graph markers are retained
and final desired/adopted identity must match with no graph failure.

## Evidence and failure handling

New `rapid-waveform-loops.txt` reports both modes and aggregate completion.
For each mode retain original actual/expected WAVs and callback/phase/head CSVs,
plus `*-loop-clock.csv` with before/after score, unlooped and reference samples,
expected loop position and actual wrap flag. `*-controls.csv` records prescribed
boundary/target, serial, request/ack/consumed callback, unlooped positions,
generation and packet sequence. Explicit failure reasons include refused request,
ack timeout/worker failure, unconfirmed declared/consumed generation, missed
boundary side, failed rapid cadence and unconfirmed loop/clock coverage. The
aggregate fails rather than classifying such cases as unavailable providers.

The old direct-chain baseline has slower control spacing. Its pass can establish
the original source/packet contract but cannot alone isolate a rapid-only graph
failure; the new report explicitly retains that limitation.

`replay-verification.json` covers disposable patch check/application, exact
postimages and unchanged old cases/main source only. The two initial replay
attempt failures are retained in `replay-failure-1.json`: standalone Git inherited
core.autocrlf; per-command false makes the mirror match repository LF policy.
No global Git setting or main index was changed.

## Deliberate limits / remaining module requirements

This is a bounded quantum-aligned one-beat loop:23040=90×256. It does not qualify
off-grid/split-quantum, shorter-than-worker, arbitrary-tempo or all loop phases.
Those require their own per-span/packet oracle, not relaxed whole-quantum checks.
There is no rapid native-only isolation case, arbitrary MIDI/SysEx routing,
sidechain transaction admission, production history activation, real audio-device
or human listening evidence here. These remain internal qualification work;
none is described as an external blocker. Do not weaken failing musical oracles
or activate production history on the strength of this draft.
