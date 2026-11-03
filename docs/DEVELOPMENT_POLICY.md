# Module milestones and release boundaries

User direction2026-10-16 supersedes the former feature-per-preview queue.
Develop substantial modules continuously; internal buttons/tools/fixes are Git
checkpoints, never automatic version bumps or distribution transactions.

User direction2026-10-18 recalibrates audio acceptance. Ordinary note/pattern/
clip edits, mix controls/automation and their history, selection/naming/colour/
navigation must remain smooth during supported playback. Heavyweight plugin
insert/delete/replace/reorder/state/preset/chain/history and device/routing/
latency reconfiguration may briefly interrupt audio. They must recover
automatically, preserve intended state/routing/PDC and accurate history, avoid
crash/deadlock/leaked workers/persistent silence/repeated loss/stuck MIDI, and
report actual outcomes. Refuse/defer incompatible structural operations while
recording; never hide capture gaps. No callback waits, allocation, locks or forced
worker termination may implement suspension. Preparing work before a bounded
CONTROL-owned suspend/reconfigure/resume is acceptable; universal unrelated-route
continuity, arbitrary tails and perfect crossfades are deferred optimizations.

Freeze the required Mixer user workflows in modules/MIXER.md. Classify remaining
work A (correctness/safety/ordinary playback), B (required missing workflow), C
(accepted structural limitation/deferred optimization). Finish A then B; do not
expand scope or build new generic frameworks/diagnostic systems for C. Retain
completed seamless paths and useful evidence without making them universal gates.
Save/export still require correct current native state. Record interruption and
recovery measurements; an accepted limitation is not a technically fixed defect.

Timing acceptance uses the actual outer physical callback and its declared
configuration. Internal graph slices have no separate device deadline. Genuine
one-frame physical fixtures must remain labelled as such; explicitly assess
supported device configurations and preserve normal playback deadlines. Changes
to tests under this user-authorized policy must retain historical evidence,
independent state/PCM/recovery oracles and explicit diagnostic coverage.

The complete musical harness combines72 model/render/persistence/recovery/live
stages. Its aggregate source allowance now matches the existing packaged300s
allowance, following the practical acceptance recalibration and a retained
complete141.44s diagnostic. Historical120s timeouts remain failures against
their original limit; no callback, operation, worker or ordinary-edit deadline
changes. This is an explicit aggregate harness correction, not an audio fix or
permission to relax individual deadlines for throughput. The complete inventory
and all assertions remain mandatory.

Before expanding a module, inspect its implementation, ROADMAP, FEATURE_PARITY,
retained failure evidence and dependencies. Define its full acceptance checklist
in docs/modules. Cover musical scenarios, controls/gestures/shortcuts, UI layout,
persistence/migration/history/cancellation, live/offline audio, failures and shared
semantic MCP/readback/skill behavior. Do not mark a module complete because an
easy subset passes. Required but unfinished dependencies stay visibly open.

The active module is **Channel Rack / Pattern / sampler** after the qualified
Mixer software milestone. [Whole checklist and implementation order](modules/RACK-PATTERN-SAMPLER.md),
[machine ledger](modules/RACK-PATTERN-SAMPLER.json). Keep the complete advanced
Channel/Pattern/loop/sample scope; tempo resampling is an internal increment.
Mixer listening/hardware/familiarity acceptance and the complete VibeRa Effects
collection remain open independently. STATUS owns current identities/actions.

During development use incremental `scripts/build.ps1` (compile only by default),
affected model/audio/component/persistence/semantic tests, and meaningful local
Git commits. `-CompileOnly` remains compatible. Run broader source regressions
after shared infrastructure changes or evidence of wider risk. Fix active
regressions, data-loss defects and audio correctness failures immediately.
Keep skill sources current; executable-bound reference generation belongs to
the module release boundary. Preserve existing generated references as historical
qualified output until then. Do not regenerate/archive/package every small edit.

`scripts/build.ps1 -ReleaseCandidate` is the explicit release entry point.
It first checks the module acceptance ledger, then builds/tests the exact final
source, checks committed identity and performs the existing staged/final package
acceptance transaction. Open software requirements prevent a module release.
Changed candidates rerun affected and mandatory release gates. No test threshold,
timeout, buffer size or acceptance requirement is relaxed to achieve batching.
Module ledger evidence must name actual passed results; documentation/source
presence alone is not evidence. External listening/hardware/familiarity checks
remain separate and are never reported passed without actual evidence.

Keep `dist/VibeRa Studio` unchanged until its qualified replacement passes.
Temporary development executables exist only under build/windows and are not
another permanent distribution. Preserve cache/objects; remove temporary runnable
test outputs when no longer active. Keep source/history/user projects, settings,
samples/plugins and ongoing Browser work. No source ZIPs/versioned bundles.

Every changed operation still requires canonical UI/MCP validation/state/history,
typed discovery, structured errors, real asynchronous outcomes and synchronized
provider-neutral skill guidance. Inherited standalone MCP consolidation remains
deferred and the full gate honestly failing. Fix inherited defects immediately
when they break functionality, risk data or block this module.

Record concrete blockers by requirement, cause/evidence, dependent work and next
action. Distinguish an unfinished internal task from an external blocker. Continue
independent work; neither silently remove a requirement nor release every blocked
subsection separately. Resume from STATUS and the module ledger, not chat memory.

Use `scripts/dev.ps1` for the affected development loop; see DEV_WORKFLOW.md.
The runner explains staged/unstaged/new-file dependency selection and supports
explicit full/additional-test overrides. Unknown/shared inputs select broader
checks. Reuse the existing CMake cache and objects; no clean/fetch or packaging.
One kernel lease serializes the development, legacy build/test/package and native
entrypoints. A non-terminating Windows job tracks only owned descendants, including
survivors of an interrupted launcher. Never bypass a busy owner or terminate an
uncertain process. Reconcile recorded PID plus creation identity first.

Cheap checks reject missing module/contract inputs, stale configured tests,
missing/non-native fixtures, and mismatched adjacent worker bytes. Current-project
test schema comes from Project::currentVersion; genuine old-schema assertions and
independent musical oracles remain fixed. Test completion requires exact JUnit
case names and no skipped/failed cases; exit0 alone is insufficient. Detailed
timings/identities/failures live in build/dev receipts. No passed-test reuse is
automatic. Explicit compilation reuse requires unchanged recorded source, selected
targets and exact executable/worker/fixture bytes. Native/provider/timing evidence
is never inferred from a schema pass or an older compilation.

The native Mixer verifier defaults to its full suite. Explicit `--case overload`
is an affected development check with identical activity/ownership/input guards;
its receipt marks fullSuiteSelected=false. It cannot qualify the full native
release gate. Preserve refusals/failures and actually inspect resulting captures.

Module software evidence requires declared current dependency hashes and exact
candidate binary hashes; changing a backend or either app/worker invalidates its
receipt. Audit the adequacy of declared coverage before marking a requirement
verified. The source/package release transaction remains mandatory. No compiler
parallelism/PCH/cache tuning without a comparable measured bottleneck.

Research starts with an unresolved implementation question and a checked target
hash. Reuse matching receipts. Record operation/target/configuration failures;
after timeout/unsupported, change method or configuration before retrying. Keep
uncertainty visible and continue independent engineering, without copying reference
implementation or claiming static findings are observed runtime behavior.
