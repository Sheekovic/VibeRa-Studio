# Detached native adapter / owning-list resolver ? unbuilt draft

Scratch only. All ten live source/vendor inputs remain untouched; exact raw and
LF-normalized base identities and exact draft identities are in provenance.json.
Root owns integration, pinned guard successor review, compilation and execution.
No complete graph transaction, app/history commit, native audio or provider
qualification follows from this source draft.

## Narrow implementation

PluginCreationInfo defaults to normal; detachedGraph is an explicit internal
constructor argument, never a project property. Only independently authored
FixedSampler, RemoteEffect and MixerSidechainSend type states are admitted.
Validation occurs before AutomatableEditItem or MacroParameterElement can register
anything. It requires a private unattached tree, already valid native ID, no
existing same-tree plugin cache owner, no master mirror identity and no native
parameter blob. Native macro/modifier/automation child state is refused; only
valid sidechain wire containers and inert empty modifier containers are allowed.
Normal behavior is unchanged.

AutomatableEditItem skips both cache registration and matching removal in detached
mode, including constructor failure. Its remap default and state flush have no
live UndoManager. Plugin Undo is null, changed does not notify Selectable/mirror,
Automap registration/removal is skipped, and deletion does not consult live
parameter mapping/record managers for these zero-parameter private adapters.
No fake IDs, pointer IDs, PluginCache insertion or live list rollback is used.

A normal AuxSend constructor silently creates a native AutomatableParameter,
MODIFIERASSIGNMENTS through live Undo and automation curve/record machinery.
Detached detector taps skip that unused gain parameter and native owner-track
initialisation. Their original overridden fixed realtime send gain remains1;
normal AuxSend behavior is unchanged. Private taps must not expose native
setGainDb/getGainDb/mute/restore automation APIs: their native gain pointer is
intentionally absent. This mode is only for the fixed app detector adapter.

Each actual app adapter has a createDetachedGraph convenience using its existing
constructor and exact declared type. It does not copy/reprocess live DSP. A
replacement worker/source must already be prepared and the caller configures
private host/timeline/connections before any graph publication.

CreateNodeParams.pluginListResolver is an optional owning Plugin::Array callback.
createPluginNodeForList calls it once for that list-build invocation, validates
same Edit/non-null/unique IDs, and uses the SAME original native factory branches
for PluginNode, AuxSend, sidechain mapping and returns. Unset mode iterates the
original PluginList without an added owning-array allocation. Resolver results
must reuse identical unaffected plugin owners; affected candidate IDs must be
unique across the WHOLE graph, not only this local list. Multiple outputs may
build the same list more than once: use an immutable mapping, never a consumptive
or mutating resolver. Graph nodes pin the candidate plugin owners as before.

## Remaining integration work (explicitly not supplied)

- Carry a scoped immutable resolver through EditPlaybackContext graph creation.
  This draft adds only the per-build native seam, not persistent app plan state.
- Scope overlay by Edit/context/device/generation and route every subsequent
  build through adopted overlay. Live native lists/caches still contain incumbent
  adapters; never promote duplicate IDs into them without a separate contract.
- Prepare/publish graph handle and app/Project/history transaction remain root's
  separate work. No dual graph processing against reused owners is permitted.
- Validate buses/global node identities/cycles and worker/native state freshness.
- Normal PluginNode still calls baseClassInitialise on reused owners; existing
  unchanged-rate app overrides retain DSP, but native automation recording status
  and arbitrary provider initialisation do not gain rollback from this draft.
- Private configuration must remain closed after validation. Generic native macro
  APIs and third-party internal Plugin subclasses are not supported by this mode.

## Meaningful acceptance design / original header

DetachedGraphPluginAcceptance.h is UNBUILT. Its required driver supplies an actual
registered app adapter in a real Edit and actual factory createDetachedGraph;
run for FixedSampler (private AudioEngine.cpp seam), RemoteEffect and detector
send. The required graph callback must build/prepare via native factory without
publication or processing. It must not return a fake graph or bypass normal
admission. Candidate success and deliberate post-preparation exception both
release the graph then candidate, checking exact live state, cache members/order,
incumbent same-ID item lookup and Undo/Redo storage. Invalid macro/modifier state
and missing ID refuse before any cache write or live ID allocation.

Add a real same-Edit plan driver: old graph remains running; replace target with
private ready aux worker and insert detector tap before Master gate through
resolver, retain every unaffected owner, force failure after Plugin/Aux nodes
initialise but before publication. Verify source processing count, host identities,
held-note PCM/playhead and native parameter revisions remain correct. For success
publish through root's prepared handle, acknowledge adoption, assert distinct
main/detector PCM with unchanged ordinary oracle. Do not process a preview graph.

Cache/Undo snapshots alone cannot observe a private queued mirror timer. In the
native fixture, add a genuine registered mirror/listener and pump supported
messages after candidate.changed/destruction, checking no mirror/callback changes;
source guard inspection is not runtime timer evidence. Missing-ID counter drift
also needs an explicit native ID allocator observation seam if exact allocator
state is required; absence of a generated ID property alone does not prove it.

## Static checks actually performed

Exact vendor and LF-normalized original-source patch reproduction passed for ten
modified inputs and the initial test header. Initial no-text fixture refused CRLF
source preimages; retained initial-patch-fixture-failure.json records it. Final
header gained a REQUIRED actual graph-preparation callback; it is source-only and
was not rerun during root's timing-sensitive audio suite. No compiler, executable,
worker, native input, listening or hardware tests were run by this agent.
