# Permanent MCP and AI skill acceptance

Authorized2026-10-15: every existing/future functional button, menu, tool, custom
control, gesture, selection, setting and workflow requires a discoverable semantic
MCP equivalent. Equivalent entry points can share an operation. Pointer/key
simulation is diagnostic only. This supplements the full DAW scope.

Use shared canonical application behavior, validation, state and Undo/Redo.
Expose stable IDs, typed parameters, descriptions, ranges/defaults/units,
prerequisites, availability/readback and structured errors. Asynchronous acceptance
is not completion: expose actual completion/failure/cancellation. Keep all schema,
I/O, UI, allocation and lifetime work outside audio callbacks. Native third-party
plugin internals without supported APIs are explicit limits, not claimed coverage.

## Execution priority (reconfirmed2026-10-16)

Build coherent music workflows first. Preserve inherited MCP functionality,
tests, inventories and skill material; defer its standalone completeness campaign
until main music workflows are substantially complete. Fix inherited regressions,
data risks and active-feature blockers immediately. Every new/materially changed
feature still needs canonical UI/MCP validation/state/history, readback/errors,
real async outcomes and synchronized provider-neutral guidance. An unrelated
pre-existing gap does not block a qualified incremental release; the complete
coverage gate stays honestly failing and the full DAW roadmap stays active.

## Implementation and gates
[Explicit inherited backlog](mcp/DEFERRED_CONSOLIDATION.md).

Execution sequencing revised 2026-10-15: the standalone inherited-coverage
campaign is deferred until the main music-production workflows are substantially
complete. Preserve existing tools, tests, inventories and skill material. New or
materially changed features still require complete scoped semantic coverage and
verification. Unrelated inherited gaps do not block a qualified feature preview;
the full gate must continue to fail honestly. Fix inherited issues immediately
when they break functionality, risk project data or directly block active work.


`CommandInfo.mcp` is the explicit mapping authority; its default `unmapped` fails
coverage admission. Registry-based document admission replaces the old whitelist.
Message-thread schemas validate requests. Paginated `vibera_commands` exposes
mappings, gaps and optional argument schemas. The shared field schema is an
initial contract; full operation-specific range/prerequisite auditing remains.

`python scripts/mcp-coverage.py --write` inventories registry operations and
custom callback candidates, writes [COVERAGE.md](mcp/COVERAGE.md)/
[COVERAGE.json](mcp/COVERAGE.json) and **returns nonzero while gaps remain**.
There is no grandfathered exception baseline. Custom callback candidates require
manual expansion into individual controls/menu options/gestures; heuristics do
not prove a complete UI audit. New/changed source fingerprints invalidate review.

Full product readiness is separate from preview-subset checks. New CTest
`mcp_semantics` uses the real application over stdio to create a beat/melody,
batch/rollback/Undo/Redo, group/place Patterns, read Mixer state, save/reopen,
render WAV and verify asynchronous writer failure. Test presence is not a pass.
Exact binary/result evidence belongs in STATUS/VERIFICATION.

CTest `mcp_coverage_detector` proves that missing/default mappings and changed
custom-control fingerprints are detected; its pass is **not** a full-coverage
pass. The separate full gate still fails with37 registered gaps and226 unreviewed
of229 callback candidates. Seven explicitly inventoried non-callback UI controls
remain gaps, and inventoryComplete=false independently blocks readiness. The
explicit inventory is docs/mcp/SURFACE_INVENTORY.json. Current0.0.49 source declares192/229 mappings;7/9 explicit UI gaps remain. Both real installed
Analog Lab V and MVibratoMB were also exercised through semantic MCP: creation,
four actual workers, ordered effects/bypass, exact Undo, state capture/reopen and
nonzero hosted export with built-ins muted. This is not native-widget, listening
or hardware qualification; the MVibrato demo expiry remains a limitation.

The reusable [skill package](../skills/vibera-studio/SKILL.md), source version0.8.0/
contract1, is provider-neutral. It includes setup/configuration, targeting,
units/context/Undo, async/recovery and musical workflow recipes.
`scripts/export-mcp-reference.py` exports tool/command references and binary
provenance from actual application discovery. Packaging includes the matching
committed skill package in the sole runnable distribution. Schema export alone
does not establish musical qualification or universal AI-client compatibility.

## Continuous edit and selection implementation (0.0.43 implementation; qualification in STATUS)

`vibera_selection` uses the shared Studio selection model for stable note/clip
IDs, set/add/remove/toggle/all/clear and musical ranges with explicit starts/
overlap semantics. Selection does not add Undo entries. UI Select All/Deselect,
ghost switching and marquee/ruler/pitch finalization share this selection path.

`vibera_gesture` uses Controller's canonical gesture snapshots, command validation
and publication. Begin returns a leased owner token, updates remain pending and
commit creates one Undo step. Cancel/idle expiry restores the starting project
and dirty state. Local UI document edits preempt remote previews; stale/foreign
owners cannot commit or edit. Other MCP mutations are refused while pending.
Invalid updates preserve earlier previews without renewing the lease. Runtime
plugin parameter transactions remain a separate open acceptance item.

The full coverage gate now also requires that reviewed callback surfaces name
real mapped registry IDs, real semantic tool readback, existing skill material and a verification
receipt/case list. An arbitrary nonempty review label cannot close a surface.
Delivered 0.0.43 passes source 18/18 and packaged 16/16; actual workflows and
retained failures are recorded in STATUS/VERIFICATION. This is a qualified subset,
not complete operation coverage.

## Loaded-plugin services (delivered subset 0.0.44; evidence in STATUS)

The shared `vibera_plugin` service adds loaded-target/parameter discovery, native
IDs, session notifications, Controls selection/close, refresh and automation jobs,
and observed recovery outcomes. [Contract, evidence and limits](PLUGIN_MCP_SERVICE.md).
Pending receipts are not completion. Initial post-restore/pre-DSP metadata is
separate from current values and never a substitute for canonical manual Undo.
Native external changes preempt remote previews and preserve the dirty flag when
a local gesture cancels or ends without document changes. They do not fabricate
document Undo. Scan/discovery of installed plugins and native manual parameter
transactions remain gaps. Actual crash-recovery success needs its own MCP case.
Final source18/18, package16/16 and actual installed-provider/shipped-client checks
pass for the stated subset. This does not close the full failing coverage gate.

## Discovery service and evidence-bound reviews (0.0.45 candidate)

Shared Settings/vibera_catalogue discovery now covers standard/custom scopes,
blacklist/retry, paged stable class IDs/descriptors, real job cancellation and
atomic persistence failures. [Contract](PLUGIN_DISCOVERY_MCP.md) and
[expanded control inventory](mcp/PLUGIN_DISCOVERY_SURFACES.md) distinguish implemented
operations from remaining local draft/selection/scrolling equivalence.

Three scan/retry/cancel callback mappings have actual JUCE component and installed
MCP evidence. Native mouse acceptance remains pending; no pointer adapter counts
as semantic coverage. Reviews now require status=passed, a passed structured
receipt, actual case lists and exact current handler/dependency hashes. Failed/
pending receipts and changed backends invalidate reviews even when callback text
is unchanged. Detector self-tests exercise those refusals. This is a scoped
coverage review, not automatic proof of all UI operations or complete readiness.

## Open acceptance

The full gate is currently red. Registered gaps and custom settings/gestures
need implementation and manual audit: catalogue draft/selection/viewport control, runtime plugin
manual parameter transactions/recovery qualification, Browser collections/assets, device/MIDI settings, appearance,
shortcut/macros, CoProducer jobs, broader continuous runtime-control transactions,
window details and durable/cancellable async recovery. A mapping is not verified
coverage. Unimplemented VibeRa behavior is a gap, never a justified exception.

No feature is complete until its UI, semantic MCP equivalence, readback/errors/
Undo/async behavior, real workflow verification and synchronized skill material
are qualified. Existing preview features with MCP gaps remain partial in the
full product roadmap. Record exact gaps/next actions across interruptions and
preserve the single-distribution policy and existing release gates.
