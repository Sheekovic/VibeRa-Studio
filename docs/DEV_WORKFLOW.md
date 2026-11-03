# Affected development checks

Verification notes are UTF-8 summaries with links to raw receipts. The dev,
build and package preflights reject `docs/VERIFICATION.md` over1MiB, lines over
8192bytes, invalid UTF-8 or embedded NULs before expensive work. Run
`python -B scripts/dev.py document-check` independently. Specify UTF-8 for both
reads and writes; locale-default Windows decoding followed by UTF-8 output can
grow corrupted punctuation exponentially. Never auto-truncate failed evidence.

Windows PowerShell5 can encode native-pipeline script input as ASCII. Prefer
`apply_patch` or ASCII-only Python source with Unicode escapes for text edits;
explicit UTF-8 file reads alone do not protect a non-ASCII search literal piped
to Python. Retain and inspect failures rather than applying broad substitutions.

For individually compiled-in native effects, `python scripts/dev-native-effects.py`
uses the same engineering-owner lease and `build/windows` cache. It incrementally
builds Studio/worker, DSP and fixed-instance acceptance, then runs native
discovery/control/state/history/WAV and affected picker/editor checks. Receipts
remain in `build/windows/native-effects-dev-*`. This focused loop does not replace
the full source regression required after shared host/dispatcher changes and
never packages or changes the qualified distribution.

`powershell -ExecutionPolicy Bypass -File scripts/dev.ps1` selects, incrementally
builds and runs affected tests. `-Plan` explains without building, `-Full` selects
all source tests and `-AdditionalTest <exact-name>` adds a test. It never packages
or qualifies a release. Timestamped `build/dev/*/verification.json` files retain
timings, selection, failures and exact tested binary hashes. Tests run serially.

Native Synthesizer content copies are declared CMake outputs for each engineering
host/configuration. Retiring generated runtime content preserves compilation
caches; the next affected-host build restores missing maps/samples automatically.
Do not clean/relink or manually delete stamps to recover these files. The bounded
current-stamp/missing-sample proof and unchanged-binary identities are retained in
`build/windows/step-memory-integration/content-restoration/verification.json`.

`scripts/dev-select.py` is the underlying read-only selector. It
collects staged, unstaged, renamed/deleted and relevant untracked paths from
Git, then explains affected coverage categories, incremental build targets and
exact CTest names. It reads current registrations from `CMakeLists.txt`;
missing required registrations or misspelled explicit overrides fail closed.
It does not build, modify source, launch providers or touch the distribution.

```powershell
python scripts/dev-select.py --format text
python scripts/dev-select.py --files src/StudioMixerControls.cpp
python scripts/dev-select.py --full
python scripts/dev-select.py --additional-test isolated_parameters
python -m unittest discover -s tests -p test_dev_selection.py
```

The default JSON contract (`schemaVersion: 1`) exposes `changedFiles`, `fileStatus`,
`categories`, per-path `reasons`, `buildTargets`, `testNames`, `ctestRegex`,
`nativeSuggested`, `timingSensitiveTests`, `full` and `requestedFull`.
`releaseQualification` is always false. Consumers must skip build/CTest when
the corresponding lists are empty. The empty POSIX regex `a^` cannot match tests;
nonempty selections are exact anchored expressions. `--files` deliberately
overrides Git discovery for controlled experiments; normal development should
omit it. `--additional-test` accepts exact registered names and can be repeated.

Shared model/dispatcher/audio-engine/editor infrastructure, build configuration,
dependency patches and unknown paths select all registered source tests.
Known feature-local paths select cross-cutting model/audio/UI/semantic/file
baselines and that feature's registered test families. This intentionally errs
toward broader checks when dependencies are uncertain. Changing a registered
verifier also selects that exact test. New unrecognized subsystems select full
regression until their dependency category is reviewed.

Untracked `Reference`, `evidance` and the protected Browser research tree are
reported as directories and preserved without recursively reading their files.
Generated `build`/`dist` outputs are not source changes. Ordinary documentation
does not compile the application; skill changes select semantic contract tests.
The wrapper still performs applicable cheap preflight/consistency checks.

One owner must serialize `build/windows`, package promotion and native input.
Delegate only bounded independent work with explicit file ownership and handoff
evidence; use source-only drafts or isolated worktrees where needed. Messages to
completed agents do not resume execution: use `followup_task` for every new task
or correction and reserve `send_message` for coordination only. Explicitly resume the task
and confirm it is running once. Use messages for active coordination, never wait
for an unstarted queued correction. Review and test through the integration owner.
Run callback timing and timing-sensitive audio without other heavy tests; a
selected GUI component test does not establish native Windows interaction.
`nativeSuggested` requests targeted human/scoped desktop review, rather than
implicitly moving the cursor. Provider branches not registered in CTest still
require their focused direct checks when changed, before the module candidate.
Listening and physical hardware acceptance remain separate.

Selection does not certify a source/package or waive active failures. The
complete Mixer ledger, exact module source/staging/final gates and sole
`dist/VibeRa Studio` policy remain authoritative. Keep skill sources current
during implementation; generate executable-bound references only at the module
release boundary. Internal changes do not allocate a version or package.

Recorded baseline costs are 339.702743 seconds for the qualified 0.0.63 full
source suite and 1031.256004 seconds for its staging/final transaction
(`build/mixer-routing-*-timing.json`). They justify avoiding repeated release
transactions during internal development. The later paired Tracktion compilation
experiment is recorded below; no selector wall-clock speedup is claimed. The retained
sampler callback-overrun failure is why timing runs remain isolated, rather
than being parallelized for throughput.

Cache preflight reads the actual Windows preset and CMake cache source/generator/
architecture/cache variables. Missing cache or changed variables requests configure;
a foreign generator/source fails closed. CMake owns normal ZERO_CHECK regeneration.
`refresh-inputs` records content invalidation separately from full compilation:
selected-target builds never mark every target compiled. LF-normalized source
comparison avoids forced invalidation for EOL-only differences; binaries stay exact.
`--no-build` verifies configuration, source snapshot, target set and every selected
executable/fixture/worker identity. It reruns tests; it does not reuse their results.

The ownership job never terminates descendants automatically. Verified compiler
telemetry at the exact configured MSVC path/hash is recorded as nonblocking;
MSBuild/compiler/tests/native/plugin/unknown surviving jobs block takeover.
No executable-name cleanup or global toolchain/security setting is used.

Measured examples are receipts, not an equivalent-workload speedup claim:
shared full incremental compile200.867s and initial46-source-tests355.381s;
focused no-change compile4.190s plus checks2.629s; exact no-build rerun2.587s.
See `build/dev/20261016-170915`, `172449`, `172541`. Source gates remain mandatory
at the complete module boundary, alongside exact staging/final package gates.

Measured shared engine-header fanout at `build/dev/20261016-181857` compiled263.083s;
engine units were built for Core, ScannerFixture and Tests. The containment fixture
now links only headless JUCE and compiles the unchanged shared WorkerProcess source;
it needs PluginDescription/Windows ownership, not the DAW graph or GUI. Broader
source49/49 verification passed at `build/dev/20261016-184802`, including actual
containment and parent-crash cases (compile473.454s/tests373.153s). There is
no equivalent-workload before/after timing or claimed numeric speedup. The graph
fix's later focused compile26.233s/tests21.426s are a different workload.

Further measured shared-header/protocol/publication checkpoint:
`build/dev/20261016-201529` compile456.509s/source53tests388.886s. Exact generated
projects show Tracktion/JUCE INTERFACE_SOURCES compiled independently for Core,
app and test targets. Core/GUI/console flags differ (notably JUCE initialization),
so blindly sharing all module objects would be unsafe. The later bounded Tracktion
single-owner change preserves JUCE ownership and is measured and checked below.
No compiler/LTO/PCH/cache tuning was introduced.

Failed development runs now retain exact candidate/worker/fixture identities
before CTest executes. A later build cannot erase the failed candidate identity
by replacing build/dev-compiled.json. Actual red run212959 includes these hashes;
development_workflow passed in the subsequent focused5/5 run213326. Compilation
238.011s for the shared overload header versus43.641s/25.663s for narrower repairs
and8.650s for unchanged incremental targets are different workloads, not a claimed
numeric speedup. The latter run's five focused tests cost26.712s; module release
gates remain mandatory and the qualified distribution was never touched.

A matched native preparation-fault workload (220 reached allocations plus first
non-hit, same provider and worker SHA256, unchanged assertions/45s timeout) ran
43.62s at build/dev/20261016-222851 and12.82s at223639 after adding the application's
normal removed-worker reclaimer tick to the fixture loop. The earlier fixture
retained removed workers until destruction; those unused processes no longer
accumulate throughout the sweep. This one matched observation is a test-cost
improvement, not a general compiler/audio throughput benchmark. Native meter
iteration can explicitly use --case overload; receipts mark it as development
only. Default full native acceptance remains unchanged and required for release.

A newly added selected target triggers configuration only when its generated
`.vcxproj` is absent; ordinary source changes retain CMake ZERO_CHECK/object reuse.
The corrected recovery fixture-only rebuild compiled11.885s; unchanged full-target
compilation13.380s is a different workload, not a paired speedup benchmark.
Generated History/Compensation/ClockWaveform helper outputs are included in the
existing exact-name, process-use-guarded release cleanup; a disposable-file host
check preserved source/cache/distribution sentinels. No real cleanup is part of
ordinary affected development. Full release gates remain mandatory.

New changed-workflow review receipts can explicitly select `text-lf-v1`, using the
same content_identity helper as development provenance. Both review and receipt
must declare the mode. Only declared text suffixes normalize CRLF; binary bytes
stay exact. Legacy raw receipts are unchanged. Detector tests cover EOL-only text,
substantive edits, binary EOL changes, unknown modes and mismatched receipt modes.
This prevents checkout-format churn; it does not qualify stale product behavior.

Verifier receipts now separately snapshot runner/selection/helper implementations,
actual selected CTest scripts, copied skill clients and tool binaries at startup
and before/after tests. `verificationInputsStable` requires complete listed inputs
and equal normalized source/exact binary identities. This is conservative local
provenance, not full dynamic-import/environment capture or automatic result reuse.
Compilation identity remains separate; verifier-only changes do not invalidate
C++ objects. Authoritative selected main verifier paths are checked before build.
Python unittest discovery patterns are recorded as arguments, while actual test
and helper scripts are hashed. Missing literal main scripts still fail preflight.

Direct `python scripts/dev.py owned <purpose> -- <executable> <args...>` also
retains fresh `build/dev/owned/*/operation.log` and `verification.json`, elapsed
process cost and nonzero/launcher failure. A successful process exit does not
establish a completed semantic/audio/native workflow: inspect its operation's
actual acceptance receipt. Ownership/descendant guards remain unchanged.

The runner now retains the actual pre-compilation source inventory through the
test phase and checks it again on every exit, alongside exact selected binary
and adjacent worker bytes. Text comparison normalizes line endings; binaries
remain exact. A passing test cannot return success with changed/unreadable
source, binaries or verifier inputs. When a test already failed, that original
failure stays primary and identity drift remains additional evidence. The
no-build path admits only the unchanged recorded compilation baseline.
Development workflow unit tests include actual runner-finally execution with
mocked build/process boundaries; an actual framework-leaf run at
`build/dev/20261017-063837` passes2/2 with compiledInputsStable and
verificationInputsStable true (compile5.950s/tests1.609s). No general build
speedup is inferred from this different workload; final release gates remain.

Interpreter-wrapped acceptance also binds every explicit executable argument,
including the real app passed to a Python verifier. It is retained in compilation
proof, final exact-byte checks and verifier provenance, with CTest working-directory
resolution. Missing no-build candidate proof refuses before CTest.71workflow
unit tests pass, including replacement/primary-failure cases. Actual stdio runner
`build/dev/20261017-071147` passes2/2 (compile1.800s/tests5.963s), explicitly
records unchanged app/central worker/copied worker/Python bytes and both stability
gates. This is development evidence, not qualification of the incomplete Mixer.

The093340 development-workflow checkpoint passed78 units, including seven strict
LOCAL-CLOSED dependency guard cases. Its native-header incremental compile costs
187.837s; the log actually compiles ten Tracktion engine translation units twice
through PUBLIC module source propagation. Generated project/actual command-log
review at build/mixer-compile-reuse-review proves console/standalone definitions
differ and affect JUCE ApplicationBase behavior. Do not blanket-share those36
module objects or make links PRIVATE as a shortcut. Tracktion-only reuse needs
an explicit common contract and comparable benchmark before adoption; no build
settings or general speedup is claimed. Different target sets/header fanouts are
not comparable compilation benchmarks. Continue module work on the valid cache.

The strict Summing source matrix is independently repeatable from the integrated
checkout: `python scripts/verify-summing-properties-guard.py --output-root
"<absolute-owned-project-build-output>"`. It reconstructs only exact pinned
predecessor bytes in disposable mirrors, not live vendor. Run standalone while
root is idle: each mirror acquires its normal fixture lease; do not wrap it in an
inherited lease for a different repository. Its17source cases are separate from
83workflow units. Both baseline failures and actual post-integration receipts remain.

Subsequent diagnostic successors make historical source stages nonterminal.
Disposable fixture reconstruction now replays only the validated complete chain,
checking every intermediate raw/payload hash. LOCAL preparation batches one fresh
preflight and rechecks every requested actual input before its first write;
it never reuses passed inputs after mutations or admits unknown vendor finals.
Combined34/52 source guards passed27 isolated units and two17-case matrices.
Observed27-name times15.965s then11.547s include fixture-branch corrections and
are not a clean A/B benchmark. The complete development_workflow15s gate still
must pass; native DSP/compiler qualification remains separate.

The normal development runner compares CTest's actual inventory with the source
registry. It reconfigures the existing cache once only on a real mismatch, then
requires exact agreement. `--no-build` never refreshes or admits stale inventory.
Unit cases cover matching/no-configure, mismatch/one-refresh, persistent mismatch
and forbidden refresh. The actual refreshed workflow gate passed13.30s against
its unchanged15s deadline (build/dev/20261017-134714).

Retained-MIDI and Ready-Graph patch adapters now skip an empty `git apply` only
when the complete current source guard proves every target is an exact supported
successor and pending count is zero. Final raw hashes are still checked. Earlier
empty-list failures134417/134523 remain retained; unknown sources still refuse.

For a cheap syntax/API check before a broader affected build, the actual MSBuild
project supports `/t:ClCompile /p:SelectedFiles=<absolute-original-source>` through
`dev.py owned`. Keep Release/x64, `/m:1 /nr:false`, and existing project flags.
StudioMixerControls.cpp passed10.498s at owned/20261017-140250-9c5653373caa;
Acceptance.cpp passed at owned/20261017-140945-780aafc3a147 after the namespace
correction. This does not link, run tests, seal candidate identity or replace the
normal development/release command. Different workloads prove no general speedup.

At143651, `development_workflow` ran `scripts/run-dev-workflow.py` under15s
CTest using two isolated processes (current bounded policy is below). Their
standard-discovery IDs must match, and selected/executed IDs must form the exact
disjoint whole. Original failure/skip/expected-failure/launch or receipt mismatch
cannot become success. Both workers finish or the existing outer deadline fails;
there are no retries or in-process concurrent monkeypatches. Future modules join
the complete default partition. Fixtures mutate only unique disposable roots.
Detailed per-process receipts/logs stay in build/development-workflow; outer
EngineeringOwner tracks descendants on interruption. All116cases pass current
actual CTest12.30s (143651); retain previous unbalanced timeout143525. One local
equivalent sequential116 diagnostic15.93s is useful evidence, not a universal
speedup or compiler benchmark. Native audio/provider/input tests stay serialized.


Installed-effect readiness uses `python scripts/verify-installed-effect-bus.py
"<absolute-vst3>" "<absolute-cached-metadata>" "<fresh-build/windows-output>"`.
`--preflight-only` checks read-only loader/desktop prerequisites without launching
providers. Known unsuitable sandbox configurations refuse with structured exit2:
an environment failure, not plugin incompatibility. Use the authorized host
execution path when required. Never switch desktops or use global input here.
Existing outputs, traversal/reparse paths and missing inputs refuse; the normal
engineering lease serializes ownership. Actual sandbox preflight cost0.549s versus
retained103.520s unsuccessful six-case launches. This is early failure detection,
not a comparable successful-provider speedup or an audio/build improvement.
Host MVibratoMB/Ozone probes completed18.940s/18.029s; exact limits and identities
are in VERIFICATION. At112921 development_workflow passed88 units. No compiler
settings changed; valid incremental objects/cache remain in use.

Use the checked owned helper for a single original translation unit:

```powershell
python -B scripts/dev.py owned "C++ probe" -- python -B scripts/probe-cpp.py --target VibeRa src/StudioMidiTakeRecoveryAcceptance.cpp
```

It verifies the inherited engineering owner, ordinary checkout paths, existing
canonical Release/x64 cache and actual ClCompile membership. It refreshes changed
inputs BEFORE compilation so the normal development runner does not invalidate
the same newly compiled object again. It never configures/fetches/links/packages
or substitutes for the normal tests. Missing/stale owner and invalid paths refuse.
Four mocked regression cases exercise ordering and refusal. Actual take probe
11.333s was reused by155737's16.941s normal incremental build; this proves that
object reuse only, not a comparable overall compiler speedup.

The full160803source run executed all120 workflow cases in11.48s under the
unchanged15s gate. This supersedes source-only/mock-only status for the four
new helper cases, not the separate no-link probe limitation.

On Windows, cmake/TracktionImplementation.cmake makes VibeRaCore own the12
Tracktion implementation files. Consumers retain the complete original public
header/definition/link usage graph and their distinct JUCE implementations,
startup and resources. Exact upstream source inventories and unchanged usage
properties are checked during configuration. Core retains Release /Zi symbols
previously supplied by app/test module copies; optimization/runtime flags stay
unchanged. cmake/ and the changed native patch appliers participate in compiled
input identity. No extra cache, source archive or runnable distribution is used.

Actual same-five-target graph.cpp edit experiment:30.635s before (five copies)
versus17.025s after (one copy, symbols retained). Exact source/tool/script hashes,
commands and logs: build/compile-module-reuse-review/benchmark-before and
benchmark-after. This is one paired incremental workload, not a general build
or audio-performance claim. Initial symbol-preserving rebuild cost249.745s.
Fourteen representative post-link tests pass234.290s (173538), identities stable;
the full source/module/package gates remain required after the architecture work.
Metadata successor174831 passes all124workflow cases10.69s under unchanged15s
and module policy; it covers the changed-applier identity contract too.

Pure native patch transformations now use bounded per-process immutable caches.
Only validated byte-buffer parse/replay work is memoized; every preflight still
reads and verifies ordinary paths, contracts, payload hashes and actual vendor
bytes, then checks every chain endpoint. Mutable/oversize inputs stay uncached;
failed transforms never become cached successes. The full inventory/deadline and
normal mutation rechecks remain authoritative. Actual135case gate and retained
131case overbudget receipt, plus one matched guard comparison, are in VERIFICATION.
Never extend this to filesystem/target-state or asynchronous completion caching.

The earlier135-case development inventory used three isolated unittest processes
and the unchanged15s CTest deadline; the current144-case/four-worker evidence is
recorded below. This light functional-test bound is separate
from serialized build/provider/audio/native ownership. Case/module times are recorded in
each terminal receipt and copied into the parent receipt; no per-case file I/O.
Use actual module costs to balance whole independent modules without skipping
cases. The old2child policy repeatedly exceeded15s after real guard coverage
grew. One exact135case paired diagnostic measured17.001s/two versus11.187s/three;
actual unchangedCTest subsequently passes11.33s. Existing9runner cases retain
every original failure/coverage scenario and add third-worker checks. No deadline,
fixture, required case or musical assertion is waived. Failure history is in
VERIFICATION. A diagnostic run
outside CTest never proves the deadline passed.

After the whole-source native guard grew, actual222651 full135 exceeded15s
(15.04s); failure and incomplete receipt remain retained. Original unchanged
runner profile223150 completed all135 in14.246s (workflow14.139, other groups
11.809/11.809); this diagnostic did not qualify CTest. Whole-module repartition
keeps source-guards unchanged, assigns plugin-final-output/native-MIDI to native
contracts and moves summing/stage/send into workflow. All cases, fresh target
validation and the three-process bound remain unchanged. Exact two-file replay
and9 orchestration units pass; actual223518 CTest full135 passes13.69s/15 with
stable source/verifier inputs. One workload observation, not a general speedup
or permission to relax future gates. See its timestamped receipts/VERIFICATION.

020715 retained a later15.03s timeout. Diagnostic021120/attempt8cd998ce
completed the same135 in14.151s outside CTest (not qualification): groups11.390,
13.956,12.508s; send-block alone1.298s. Moving that whole independent module
from workflow to source guards preserves the same bounded three workers, all
cases and15s deadline. Nine runner cases0.037s pass with send-block explicitly
in the disjoint mock inventory; actual021312 full135 CTest passes14.75s.
This observation diagnoses balance; it is not a general throughput claim.

The direct MIDI guard extension retained later033838/03412315s timeouts.
Diagnostic034024 completed the same135 and measured each whole module; the
actual runner-mock cost also varied0.08s diagnostic versus2.63s in the retained
CTest attempt. Current partition uses forward+summing, final-output+send+runner
mocks, and the remaining modules. Every process still discovers the full inventory;
the parent verifies complete disjoint execution and real outcomes. Actual034302
passes135 in13.22s under the unchanged15s gate, groups11.014/13.030/11.386s.
This is one measured balancing result, not a general speedup or new timeout rule.

Offline-render policy ordering (2026-10-18): the guarded original+4/-1 native
patch sets the same requested worker count before initial setNode, avoiding
creation/join of a temporary default graph pool. Exact38-target/111-stage
closure, historical37/110 pins retained; existing engine policy remains1 CPU.
`python scripts/verify-render-worker-policy-guard.py --output-root
"<absolute-owned-project-build-output>"` reuses the17-case disposable matrix.
Run standalone while root is idle, as above; the initial inherited wrong-root
lease refusal is retained. No guard/ownership bypass. Native48.367s compile and
unchanged musical110.483s/120sPASS are owned073826/073932. Observed Pattern group
19.274s to3.885s and automation9.763s to5.030s; one comparison across differing
preceding workloads does not establish an isolated/general speedup. Exact source,
stage timings, failed startup candidates and limits are retained under
build/mixer-render-worker-runtime-20261018 and VERIFICATION. Do not rerun full
packaging for this internal change or let its passed musical subset qualify the
still-failing Mixer module.

## Fast parallel test runner

`python scripts/fast-test.py` runs the registered CTest inventory from
build/windows in parallel (default 8, longest first), then the load-sensitive
tests listed in tests/exclusive-tests.txt two at a time (`!name` = strictly
alone). Any failure outside the strict-alone phase is rerun alone: a pass there
is reported PARALLEL-SENSITIVE and exits nonzero until the list is updated, so
no failure is silently counted. Use `-R <regex>` for the affected subset while
developing (seconds to ~1 min) and the full run at checkpoints (about 10 min
versus about 26 min serial on the 32-thread development host, 2026-10-25).
Per-test logs: build/windows/fast-test/<name>.log. Development evidence only;
release qualification keeps its own gates.

## Failed candidates and unchanged source results

Successful package promotion owns generated-runnable cleanup. Failed release
attempts retain their actively investigated build/windows outputs rather than
forcing relinks that invalidate exact-binary evidence. These are temporary
engineering outputs, not additional permanent distributions. Retire them when
investigation finishes; never remove the qualified distribution on failure.

A completed dev.py --full result may supply the source-suite part of an exact
release transaction only after validating its complete CTest/JUnit inventory,
terminal pass, compiled/verifier stability, current source and exact current
binary identities. Checkpoint exact authorities, then use distribution.py record
with that actual input snapshot/count and package.ps1 for required staging/final
acceptance. This does not make development tests a packaged qualification. A
changed dependency/configuration/binary invalidates applicable reuse. The usual
build.ps1 -ReleaseCandidate entry point still performs the whole transaction.

Release tiers (owner decision, 2026-11-03). Milestone releases (major or minor
version change) need the complete regression: dev.ps1 -Full, record-rps-evidence.py,
then build.ps1 -ReleaseCandidate, which reuses that passed regression on unchanged
binaries instead of running a second suite. Patch releases (same major.minor, higher
patch) use build.ps1 -ReleaseCandidate -Patch: scripts/patch-release.py refuses a
non-patch version, runs the tests dev-select.py maps to every change since the
distribution's source commit plus a fixed core smoke set, and module-gate.py
--phase patch keeps the rows proven by the last complete regression without
re-binding them. Staging, final installed verification, rollback and the
distribution audit are unchanged for both tiers.

Owner decision (2026-11-03, supersedes per-change test runs): during development only
compile (build.ps1 -CompileOnly) and commit; run tests only when the owner asks or when a
breakage is found, and promote dist only on request. The release transaction keeps its
own gates when it is requested.

Owner decision (2026-11-03, later): after every change, compile and refresh the owner's
dist copy with `scripts/update-dist.ps1` (app, worker, Content, CoProducer, skill; refuses
while VibeRa runs from dist; writes QUICK-UPDATE.txt with the untested commit). This is a
convenience copy, not a qualified release: provenance.json/SOURCE.txt keep describing the
last qualified package, and the full release transaction still runs only on request.

###2026-10-19 complete light-test qualification

The complete144-case development inventory now uses FOUR bounded isolated
Python unittest workers, with the unchanged15s CTest deadline. This changes
only light functional tests; build/provider/audio/native work remains serialized.
Whole independent modules are balanced across four groups, with complete/disjoint
discovery and explicit failure/skip/exit checking. Existing9runner tests pass.
Actual complete repeated runs:8.99/8.82/14.43s PASS; evidence
build/dev/owned/20261019-005054-39fc3f710368. All144cases ran in each.
Earlier three-worker timeout attempts remain retained, including the quiet run.
No peak-memory measurement or universal speedup is claimed. Previous two/three
worker timings above are historical configurations. No audio/operation deadline,
buffer, guard or musical oracle changed.

Owned fixture mirrors now stay under build; every actual ancestor/reparse check
remains, with two fewer ancestors than the previous system-temp location.
Redundant post-command preflights were removed only where the actual command
already performed full validation; fresh bytes/mtimes and negative checks remain.
Pure mocked interpreter identity uses an owned stand-in instead of the machine
installation. A standalone final-output profile measured131preflights and
191667lstat calls in6.19s; existingPython3.11 took6.46s, so no interpreter switch.
Finite-baseline diagnostic I/O now publishes the SAME collected samples/rows
at completion or before rethrowing an assertion, instead of repeatedly flushing
growing files after each callback. Forced termination cannot flush RAM; prior
timeout evidence remains. Actual unchanged30s finite gate PASS20.37s after
batching. Disposable64KiB CONTROL flushes measured12-66ms, not DSP latency.

###2026-10-19 bounded negative-observation cleanup

After the retained15.05s development_workflow timeout, cProfile identified90
preflights in the source peer. Its manifest/payload tamper observation now uses
its already validated target list and freshly reads every vendor byte/mtime,
removing14 duplicate post-restoration preflights. The actual refusal operation,
all assertions, cases, ancestry checks and15s deadline remain. The149-case gate
passes two targeted runs12.09/14.80s; profile wall times7.00/9.11s show variance,
not a demonstrated speedup. No new runner/cache/parallelism setting was added.
Evidence: build/wrapper-followup-20261019/verification.json. The failed broad run
remains recorded; exact final module gates are still required.


### Disposable guard fixture seeds

Development unittests reuse an immutable, content-identified seed for repeated
source-guard fixture construction within one worker. Every caller receives new
files and a fresh guard module. All reviewed inputs are re-read/hashed; metadata
size/mtime is not sufficient. Copied bytes are checked and original local-preimage
assertions remain. Production guard preflights, mutable validation,4-worker bound
and15s CTest deadline are unchanged. Same-stat content invalidation and independent
copy behavior have a real test. This is test construction reuse, not passed-result
reuse or a production filesystem cache. Current timing/failure evidence:
`build/wrapper-chain-20261019/fixture-seeding-improvement.json` (retained15.02s
failure; final affected165-case pass12.53s). Broader source gates remain required.

### Fresh directory metadata and measured development partition

The structural source guard checks current metadata at every leaf and ancestor.
On Windows, ancestors use GetFileAttributesW directory/reparse flags; INVALID
queries use the original lstat/error path. Leaves retain lstat; validity is never
cached. Fallback and native negatives include all ancestors above ROOT, directory
types, missing/permission failures and a real owned junction. Source/payload pins
and immutable transform checks remain unchanged.

The final-output diagnostic profile measured6.503s before and5.108s after with
identical workload. The complete development run additionally balances independent
fixture/cache modules onto the measured lighter worker:166 cases, four workers,
unchanged15s deadline, affected pass12.37s. New full source acceptance is required;
these measurements do not establish runtime audio improvements or universal timing
stability. Details and retained failures:
`build/wrapper-chain-20261019/fresh-directory-guard-improvement.json`.


### Current fixture I/O and explicit result reuse

Disposable nonrunnable source-test fixtures use standard Windows TEMP; compilation
cache and verification receipts remain in build/windows and build/dev. Controlled
identical fixture profile: project volume5.300s (unlink2.765s), system TEMP1.570s.
Do not generalize this to DAW performance. Source guards still inspect fresh
ancestry and bytes. Immutable preparation has three closed content-keyed profiles:
local-preimage, all-final and absent-only; never collapse mixed-stage semantics.
All original166 development cases remain in the15s/four-worker gate. Additional
new profile oracles run as a separate serial15s CTest. No inherited case moved
out of discovery and no deadline changed. Independent fresh copies, optional
header transitions, same-stat manifest/payload/vendor invalidation and unchanged
cache after failure are checked; read-only closure dependencies verify fresh
changed bytes, not an invented patch-target pin.

The current191-case source qualification at
`build/dev/20261019-mixer-final-composite2/verification.json` is explicit scoped
reuse:189 unchanged outcomes from retained failed full194405 plus two fresh CTests.
It checks exact binary/tool/configuration/script identities, additive-only new
CMake test registration, original commands/working directories, the reviewed
preparation-only delta and before/after stability. Every case has its real origin.
Old full189/190 remains failed. Conservative verifier snapshots never authorize
automatic reuse; validate actual dependencies and coverage for each proposal.
Existing properties here are source-derived from unchanged definitions; old
receipts did not independently snapshot all properties. Mandatory exact staged/
final package checks remain separate. Do not use this bounded receipt as a
blanket reuse mechanism for other changes.


A completed project Open is a document/file outcome. Before dependent capture or
routing, observe pendingAudioUpdate until false; follow the exact returned chain
job if routing is pending. Do not retry mutations to overcome busy admission.
The retained insert-order staging failure and corrected direct/CTest evidence are
linked in VERIFICATION. Current source authority is
`build/dev/20261019-mixer-projection-qualified/verification.json`; it retains190
unchanged qualified outcomes plus that fresh CTest, with exact original origins.


## Recorded package-output recovery2026-10-20

Installed-provider MCP acceptance requires a genuinely fresh output directory:
an older `chain-presets` directory caused a FileExistsError during0.0.65 delivery.
Preserve prior nonrunnable evidence before a new whole-package run; do not use
exist_ok to mask stale fixture state. Exact executable-derived staging/current
output guards remain enforced. Failed original receipts remain separate from
new successful attempts. The documented unchanged-input reuse policy allowed
completed source/stage/final cases to be retained while executing only missing
or failed branches with the original Invoke-Owned process runner and deadlines.
Current delivery used84 staged and117 final unique actual successful cases;
see VERIFICATION and verification-resumed.json rather than the retained failed
index. Single-use recovery helpers are under build/windows/synthesizer-development;
they do not replace the canonical build/package scripts or add an MCP subsystem.
No source208 rerun or assertion/deadline relaxation was needed for fixture-output,
receipt aggregation or generated-bytecode cleanup issues. Exact source/native/
content/binary bindings and the final one-folder audit remain mandatory.

###2026-10-21 independent final-output verifier cases

The former single final-output case is two independent complete fixtures: exact
history/current-successor checks and mutations/historical-ancestor checks. All124
original assertion sites and131 fresh preflight scenarios remain; the second
fixture adds one initialization preflight. Complete/disjoint discovery covers167
cases across the existing four processes. No production guard, source identity,
ancestry check, deadline or musical oracle changed. Eight explicit partition
expectations were corrected after the first actual run exposed stale mappings.

Focused CTest now passes development_workflow13.40s under its unchanged15s limit,
and development_fixture_profiles6.02s. The previous15.07s full-run timeout,
15.487s complete diagnostic and failed first split remain recorded. This resolves
the focused verifier defect; it is not a fresh full-source pass or a general
speedup claim. Receipt: build/dev-final-output-split-20261021/verification.json.

## Installer

`powershell -ExecutionPolicy Bypass -File scripts/make-installer.ps1 [-Source <runnable folder>] [-Output <folder>]`
builds `VibeRa-Studio-Setup.exe` (default: from `dist/VibeRa Studio` into
`build/windows/installer`) with Windows' built-in IExpress; no download or admin rights.
The setup installs per user into `%LOCALAPPDATA%\Programs\VibeRa Studio` with shortcuts
and an uninstall entry. Build it from a qualified distribution only; it is a release
artefact, not a development checkpoint. `installer_package` verifies the pipeline.
