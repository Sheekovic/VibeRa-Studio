# Mixer organisation: isolated implementation draft

Unbuilt original code/test draft, 2026-10-16. No live source, CMake, tests,
application process, distribution, version or executable-bound references were
changed. Root is the sole integration/build/native owner. This is not MX-A
completion or a release. `base-inputs.json` records exact bases/draft hashes;
`integration.patch` applies the proposed source changes without replacing whole
live files. Review any concurrent change before applying.

Final patch SHA256 `06bab28993fde4d0fc59674826e9f9cbd067a5a7a1c46e251907dc350956a40d`.
Read-only live `git apply --check --whitespace=error` passed; applying only into
`reproduction-final` reproduced all seven draft files exactly after LF text
normalization. All four recorded live bases still matched exact raw hashes at
that check. `draft-verification.json` retains the earlier patch-line-ending failure
and correction. Preliminary `reproduction` predates the capacity compatibility
refinement and is not final integration input. No C++ compile/test passed claim.

## Implemented in the draft

- Companion Project schema20 fields: `nextMixerIndex` separate from global
  project IDs, `mixerOrder` stable-index permutation with Master first, and named,
  coloured, exclusive non-Master display groups. Physical `project.mixer` order
  is untouched by view reorder; dense native bus numbers therefore stay fixed.
- Existing default18 IDs0..17 remain unchanged. Maximum128 non-Master plus
  Master (129 total), preserving the existing valid schema19 scope. A normal deletion never lowers/recycles `nextMixerIndex`; INT_MAX+1 is
  the explicit exhausted sentinel. Insert creation appends physical topology,
  defaults unity/centre/unmuted and a unity Master send, and can place its display
  entry before an existing non-Master.
- Atomic canonical create/delete/view-reorder/group create/membership/name/
  colour/remove validation. Group membership changes have no routing meaning;
  reassignment removes selected inserts from their old groups and removes newly
  empty groups. Names are trimmed1..64 characters without tabs/newlines, unique
  case-insensitively for groups. Theme0 or opaque ARGB only. Groups use the existing
  global Project allocator and global ID collision validation.
- Explicit deletion policy `rerouteMixerToMaster`: assigned Channels refuse
  unless true. Remove incoming routes, deleted view/membership entries and
  affected automation links/initializations; remove a source/its placements only
  when no live link remains. Preserve shared sources/remaining targets. Clip
  group pruning follows removed placements. Removing a solo owner restores its
  recorded original mask; otherwise reconcile the surviving solo.
- Original transient `MixerSelection`: stable IDs, replace/add/toggle/range,
  group-member union, current view-order range, focus/anchor and deletion
  reconciliation. No Project mutation/persistence/Undo or audio operation.
- Typed proposed argument/selection contracts and actual organisation state
  readback in `MixerOrganisationContracts.h`. These are **not registered live**.
- Existing Controller StateAction adapter for canonical model operations. New
  `CommandArgs::rerouteMixerToMaster` is explicit optional policy, not an overloaded
  unrelated argument. Plugin-bearing structural operations always refuse with
  `requiresPreparedProjection`, even if the deleted insert itself has no effect.
  Unchanged remote voices elsewhere must not be silently reset by structural DSP
  replacement. Direct Controller/MCP cannot bypass this guard in this draft.

## History, native state and graph boundary

The existing exact whole-Project Undo policy is retained. Delete Undo/Redo restores
the musical document, routes, group/selection metadata as appropriate and allocator
snapshot. A new Undo branch can reuse an index from an undone creation, just as
other Project-allocated entities do; no unbounded per-process never-reuse promise
is made. During an admitted insert lifetime and view reorders its index is stable.
Normal delete/create does not reuse deleted IDs. Structural async requests must
guard exact projectIdentity + source revision before commit, preventing a stale
plan from targeting a reused index on a different history branch. Root explicitly
confirmed this existing policy; no global watermark/Controller rewrite is added.

Plugin-bearing structure is deliberately not committed here. Integration needs
one app-owned asynchronous transaction: guard context; capture every affected
native instrument/effect's fresh state with real nonzero serials; build before
snapshot using captured states; compute candidate; prepare/validate the complete
replacement graph/worker routing; revalidate context; publish and commit one
canonical history transaction with correct failure/cancellation semantics. Failure
must keep original model/audio/state and reclaim only owned prepared resources.
Plugin deletion Undo must restore the freshly captured opaque state, not startup
state or stale Project.state. Existing `capturedInstruments` accepts Channel clone/
delete only and is insufficient for effect deletion. This draft does not add a
forgeable external `prepared=true` flag or claim safe direct plugin deletion.

For plain sample-only create/delete, the draft model command is complete but an
actual prepared graph/voice transition and failed preparation behavior still need
root integration and real musical acceptance. No voice-preservation claim is made
for these structural edits. Metadata reorder/group/selection must qualify without
Edit/context/device/worker/voice replacement. Existing LiveDocument compilation
sorts Mixer controls/sends by stable identities and does not inspect view order or
groups (`LiveTimeline.cpp38..45`). AudioEngine's topology comparison/aux bus
assignment still uses physical vector ordinals; this is why physical reorder was
rejected.

## Schema compatibility

Propose organisation **schema20** after current19. Sidechain draft20 is not live;
coordinate it as21 rather than independently claiming the same version. Do not
overwrite or omit its eventual fields when merging that later change.

20 requires exactly one MIXER_ORDER and MIXER_GROUPS plus exact integer watermark.
It rejects duplicates, missing/foreign members, non-Master-first permutation,
fractional/coerced/overflowing indices, invalid colours and global group-ID
collisions. Genuine19/older files must not contain the newer fields; migration
derives view order from original topology, puts Master first in the view, derives
max-index+1 and leaves other model fields untouched. No file is rewritten on load.
The old default-nine extension is now restricted to old schemas and updates both
new view and allocator: an intentional schema20 nine-insert project must stay9.

Existing19 validation permits129 total inserts. Root confirmed preserving that
compatibility: maximum128 non-Master plus Master (129 total) is the single
authoritative capacity, with matching typed array/state limits. Genuine19 files
at the previous maximum remain valid; never truncate or silently drop an insert.
Existing default18 projects are unaffected. Genuine older
test fixtures must omit the new fields when setting an older version, preserving
their independent old-format oracle rather than relabelling new payloads.

## Integration checklist and concrete existing gaps

1. Review/apply exact model/helper/Controller patch after freeze. Register typed
   schemas in authoritative MCP discovery, shared new argument parsing and mapping.
   Canonical structural deletion should be dispatched through the async app service
   with pending/terminal jobs, structured error codes, readback and cancellation;
   direct plugin-bearing model paths must continue to refuse until validated.
   Registry helper strings alone are not functional MCP access or completion.
2. Add one owned MixerSelection to Studio. Preserve selectedInsert as its focused
   compatibility view; synchronize once via canonical selection service. Refuse
   context-changing operations during existing gestures. Reconcile after undo/
   redo/project replacement/create/delete; do not silently retarget a stale gesture.
3. Replace all Mixer *view* physical-vector iteration in Editor paint, route/send
   geometry, hit testing, scroll content and selection reveal with the same
   `mixerOrder`→stable-index lookup. Current concrete paths: Editors.h1578 geometry,
   2352 painting,2924 hit destination. Preserve all audio/worker/plugin physical
   iterations; do not feed view order into native bus indices.
4. Replace `index+1`/`selectedId-1` UI encodings with dense item-ID→stable-index
   tables. Concrete overflow/wrong-target paths: PluginPage.h59,156..157,197;
   InstrumentWindow.h100,597..604; Editors.h2813 context Mixer assignment. INT_MAX
   is valid model index, so adding1 overflows; menu/callback IDs are not indices.
   Refresh open Settings/Instrument windows after organisation changes. Audit
   deletion of the selected/hidden insert and no-duplicate editor lifetimes.
5. Channel assignment/plugin insertion MCP generic mixer schema currently caps127
   (`McpContracts.h28`, `StudioMcp.cpp` command parsing). Stable sparse index128+
   needs maxINT_MAX while active non-Master count remains128. Use the actual existence check,
   never numeric index<count. Mixer control/send-specific schemas already support
   INT_MAX. `Studio.cpp` free-insert search chooses lowest numeric unused ID;
   retain or explicitly align its displayed behaviour with view order.
6. MIDI controller bindings currently validate -1..17, bank offset by numeric
   index addition and clamp bank0..16. ControllerPage uses corresponding encoded
   IDs. Define existing fixed bindings as stable targets, and bank slots as dense
   visible non-Master ordinals through order lookup; preserve existing saved
   profiles without reinterpreting fixed stable IDs. Never let a deletion/reorder
   route a physical knob to an arbitrary wrong insert. This is module work, not
   proof of general controller completion.
7. Group/multiselection UI must apply gain/pan/mute/etc through shared individual
   validations with a single atomic batch/gesture history where appropriate.
   Grouping never inserts audio buses or invents effect controls. Define mixed-value
   readback explicitly. Preserve solo and automation target semantics.
8. Add concrete menu/keyboard/drag/context interactions, shared MCP lifecycle,
   provider-neutral skill examples and affected source regression. Gate module
   software acceptance on actual UI/semantic/persistence/audio outcomes. No version
   or package transaction for integrating this internal draft.

## Original tests provided, not executed

`tests/MixerOrganisationAcceptance.h`: stable18/create/dense physical topology
preservation; reordered relative order and Master guards; duplicate/missing
targets atomic; group identity/colour/name/membership union; selection/range/error
atomicity; real multi-target automation pruning and Channel reroute policy;
delete/create no normal index recycling; exact20 state/XML and genuine19 migration;
old/new intentional-nine distinction; missing/fractional/duplicate encoding;
INT_MAX/sentinel;128 non-Master capacity plus Master and genuine19 maximum migration; model-only valid missing-provider descriptor refusing
unsafe native-state deletion; canonical Controller delete exact Undo/Redo.

Parent runner integration: include the header after existing `require` and model
helpers; call `mixerOrganisationAcceptance()` in focused Mixer acceptance. Compile
companion Project/Commands changes together, with the new original headers. Scratch
copies are patch inputs, not a standalone source overlay (live headers include
their own relative Project.h). No C++ compile/runtime pass is claimed.

Still required: actual Pattern/Song metadata operations with exact unchanged PCM,
held voices/resources and callback gates; real prepared structural failure/cancel
and fresh plugin-state delete/Undo; native controls; persistence migrations with
independent genuine19 fixtures; semantic invalid inputs/async outcomes; mixed
groups/selection at1080p/4K; MIDI banking and user familiarity/listening separately.
