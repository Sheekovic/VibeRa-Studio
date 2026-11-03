# Owned synthetic callback stack diagnosis

## Later continuation: timing phases and graph scheduling

Synthetic driver now records four scalar clock boundaries around setup,
dispatch and teardown. Sampler live/preview wrappers retain their own two
boundaries, yielding five nested durations plus raw QPC values. These are
prepared fixed-size records; formatting and CSV I/O happen after processing.
The original full wrapper deadline remains authoritative: no duration is
subtracted or excluded to pass. Four test-driver clock reads are additional
measurement overhead; production device callbacks do not use this fixture.

`callback_timing` is a new independent CTest gate. Deliberate30ms pauses before
the driver call, inside dispatch and afterward are attributed correctly;
standalone11 assertions pass. Source `CallbackTimingTests.h` deliberately blocks
only its fake callback. It is not a real-time qualification. The Core watchdog
therefore now sees four intended captures: this one plus the three pre-existing
120ms overload injections. All four were symbolized at their own source files;
no unexplained stall reproduced. `build/callback-phases-core`:11216 callbacks,
32610 assertions, max observer wait2.4132ms and no long observer waits.

The supervisor writes bounded `observer-waits.csv`: up to128 long waits,
explicit omitted count, raw QPC, active/previous token and callback counts.
Wait timing starts immediately before its requested1ms timer wait and excludes
preceding stack/log work. It includes the requested timer delay and observation
overhead, so it does not prove a system-wide scheduler cause. New
`ObserverSelfTest` permits an environment-controlled pause only when the child
is the exact same helper binary's `--fixture` target. Its40ms pause is measured
as56.9263ms; native stack verification passes. Exact-owned timeout again exits124
as injected. Receipts: `build/callback-phases-{observer,timeout}-selftest`.

Core sampler traces retain3191 callbacks (1952 live/1239 preview), no measured
overruns, largest dispatch0.263ms. The subsequent ordinary all-CPU-policy source
run fails15/16: musical timeout120.02s at renderTests, despite sampler maxima
0.3029ms. `build/callback-phases-source-timeout` preserves the full log, captured
input manifest and phase files. Both this timeout and the earlier5241.0083ms
sampler callback remain historical failures, not retroactively explained.

Source inspection then establishes an avoidable dependency: inherited
EngineBehaviour uses32 CPUs on this machine; playback and NodeRenderContext
subtract one to configure31 auxiliary graph threads. The graph's parallel
branch waits for final-node readiness. Current VibeRa EnginePolicy returns1,
selecting the supported zero-worker calling-thread branch. Hosted VST3 DSP
stays isolated and asynchronous. No device buffer, deadline, priority, mute or
transport policy changed. This removes graph-worker completion dependency;
it does not prove the cause of the earlier5s callback or render timeout.

Isolated rendering before/after:32->1 configured CPUs,47->16 peak observed OS
process threads, five retained WAV formats/PCM byte-identical. Elapsed5323.698
versus6125.0014ms, so no speedup claim. Lower parallel throughput is an explicit
tradeoff; qualify large sessions and a future bounded parallel design separately.
`build/render-policy-{all-cpus,calling-thread}` and
`build/render-policy-pcm-comparison.json`. The older baseline input manifest was
reconstructed after execution from the retained pre-CLI manifest and reversed
policy-assertion hunk, exactly matching its recorded hash; this is not a prelaunch
capture. Pinned upstream Renderer/NodeRenderContext files match their archive;
no vendor patch or dependency/license change.

Current calling-thread source passes16/16 in174.23s, musical45.05s, with
diagnostics disabled. Actual app MCP448 passes and exposes configured CPU/worker
counts. Current sampler live maxima0.2650/0.3133ms; all3191 retained live/preview
spans within the original budget. Staging and14/14 packaged checks subsequently
pass for source42ab1b8; sole delivered application is now0.0.40. Native144DPI
menu/Save/Undo toolbar screenshots were inspected on the identical delivered
application hash; Ctrl+S/Z remain unqualified. No listening/hardware pass.
Exact binary tuple and final package receipt: [STATUS.md](STATUS.md).

```powershell
# Build once in the canonical cache, then use fresh evidence names.
powershell -ExecutionPolicy Bypass -File scripts/build.ps1 -CompileOnly
build/windows/VibeRaTests_artefacts/Release/VibeRaTests.exe --callback-timing-check <absolute-output-folder>
powershell -ExecutionPolicy Bypass -File scripts/diagnose-callback-stall.ps1 -Mode ObserverSelfTest -Evidence observer-new
python scripts/analyze-callback-phases.py build/callback-phases-core/target-evidence --output build/phase-analysis-new.json
powershell -ExecutionPolicy Bypass -File scripts/verify-render-policy.ps1 -Evidence render-policy-new
```

The analyzer validates per-row and cross-row clock ordering, finite nonnegative
durations and interval sums. Its6-decimal CSV classification can differ at a
deadline rounding boundary; the in-process full-precision gate wins. It does not
infer source identity/freshness or release qualification from a CSV. Controlled
post-processing tests detect one injected25ms wrapper gap and refuse an invalid
raw span, preserving original evidence:
`build/callback-phase-analyzer-injections/verification.json`.

## Earlier watchdog qualification

2026-10-15. This is a test-only diagnostic, not an audio-stutter fix. The retained
5081.2646ms,16.2725ms and115.2395ms callback failures remain unresolved. No
buffer, deadline, target priority, transport, worker policy or production DSP
path changed in this continuation. The sole delivered application remains0.0.39.

`SyntheticDevice` optionally opens a prepared shared beacon during construction.
Its callback boundary publishes only fixed scalar/atomic fields and a QPC tick.
Without `VIBERA_CALLBACK_PROBE`, it has no mapping and takes a disabled branch.
It never performs mapping setup, file I/O, symbol lookup or waits in processing.
This driver is an in-memory acceptance fixture, not a physical audio device.

`VibeRaCallbackProbe` creates a unique mapping and launches its own target
suspended, records its retained process handle/PID/creation identity, assigns
mapping ownership, then resumes it. A callback can use the mapping only when
its own PID matches. The supervisor polls a private high-resolution timer;
after the threshold it opens the reported thread, verifies its process ID,
suspends it, revalidates the active callback token and reads its context/stack.
An RAII guard resumes the thread. Symbol resolution uses local DbgHelp/PDB files
only. A live child is gracefully closed on timeout/error, then terminated through
the retained exact owned process handle if needed. No executable-name cleanup,
attachment to user processes, global input or system timer changes are used.

**Capturing suspends the thread and therefore changes timing.** Logs record the
pre-capture elapsed time and capture-operation duration separately. A stack from
a different callback does not explain the earlier failures. The observation
window covers the actual synthetic callback, not every instruction in an outer
test timing wrapper. The shared beacon is for the serialized synthetic driver;
it is not a general profiler for overlapping physical device callbacks.

## Actual checks

| Check | Evidence / outcome |
| --- | --- |
| Initial self-test | `build/callback-watchdog-selftest-1/`: target exits0, but symbol initialization before loader startup fails; wrapper correctly fails. Raw unresolved frames are not trusted. |
| Corrected capture | `build/callback-watchdog-selftest-2/`: deferred symbols resolve owned `fixtureStall`, SleepEx/ZwDelayExecution and source lines. |
| Final hardened self-test | `build/callback-watchdog-selftest-final/`: source/PDB symbols resolve, expected target exit0, process receipt includes helper/target/source hashes. |
| Exact-owned timeout | `build/callback-watchdog-timeout-final/`: intentional five-second wait, one-second supervisor timeout, graceful attempt then exact-owned exit124; wrapper verifies this expected injected outcome. This tests the helper, not plugin runtime recovery. |
| Three full histories | `build/callback-watchdog-core-{1,2,3}/`: each target exits0,11213 callbacks, three candidate captures. All nine stacks identify intentional120ms injection in `CallbackOverloadRecoveryTests.h`; the unexplained stall is not reproduced. Assertion counts27380/27376/27381 are exact run outcomes, not selected release passes. |
| Diagnostics disabled | Final compiled application/tests, all15 source CTest suites pass179.54s; musical43.33s. `build/callback-watchdog-source-ctest.log`. No new packaged/native desktop/listening/hardware qualification. |

The first three history runs preceded the final helper exception/timeout
hardening; their own binary hashes are retained. The final application compilation
and ordinary source tests include the final beacon layout. Do not equate the
diagnostic histories with final-helper performance qualification.

## Reproduce and continue

Compile in the sole cache using `scripts/build.ps1 -Configuration Release
-CompileOnly`. Then, from the repository root, select a **fresh** evidence name:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/diagnose-callback-stall.ps1 -Mode SelfTest -Evidence watchdog-selftest-new
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/diagnose-callback-stall.ps1 -Mode TimeoutSelfTest -TimeoutSeconds 1 -Evidence watchdog-timeout-new
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/diagnose-callback-stall.ps1 -Mode Core -Evidence watchdog-core-new
```

`Preview` and `Live` select focused sampler cases. Core preserves preceding test
history. Threshold defaults to the existing256/48000 deadline; changing a
diagnostic trigger does not change any acceptance deadline. Capture is bounded
to eight candidates and180seconds by default. Avoid indefinite retry-to-green:
retain a non-reproduction as such, inspect any unexpected stack and correlate
its callback token/phase with the fresh CSV. A future failure needs its own
stack/phase evidence. Run ordinary regressions with diagnostics disabled before
any package transaction. `clear-build-runnables.ps1` includes the helper in
verified generated-output cleanup; it is never shipped as a second application.
