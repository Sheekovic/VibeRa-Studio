# Bounded sidechain implementation proposal

Internal design,2026-10-16. The IPC9 auxiliary backend and original-worker transport
probes are implemented; preparation/layout/span safety evidence is recorded in
STATUS/VERIFICATION. Native SDK arrangement checks are required because JUCE's
public layout may be cached after native preparation refuses arrangements.
The prepared pre-Master tap, mono/stereo RemoteEffect adapter and pinned receiver/
prepared SummingNode cache hook now pass original-worker graph PCM at
`build/dev/20261016-184548`, including fixed unequal/both-delayed paths. Failures,
fixture overwrite correction and exact limits are in VERIFICATION. Connection
model/UI/MCP, actual bus capability catalogue and full live sidechain acceptance
below remain open. This is not a completed application sidechain feature.

## Small coherent slice

Support a stereo main input/output and one explicitly selected auxiliary input
bus, mono or stereo. Keep other buses disabled with explicit unsupported reasons.
This does not claim arbitrary surround, multiple simultaneous aux buses, multi-out
instruments or arbitrary native plugin UI control. Keep MX-D's wider requirements
open. Deliver this as internal Mixer work, never a separate preview.

Persist a sidechain connection on its stable effect instance ID, not its slot
ordinal: source insert ID, native auxiliary input bus index, requested channels1/2,
mapping enum left/right/average/stereo, finite linear gain0..2, enabled. Default is
no connection and preserves genuine older projects exactly. Mapping is explicit:
mono average=(L+R)*0.5, left=L, right=R; stereo=L/R identity. A source's normal
audible routing is unchanged. Source tap is post-FX/fader/pan/mute, BEFORE its
direct-only Master gate, matching the existing send tap. Never silently sum a
sidechain into the main plugin input.

## Material graph hazard found

The pinned Tracktion Plugin API already supports input/output bus declarations,
sidechain source IDs and channel wires. EditNodeBuilder.cpp1209 creates separate
direct and sidechain channel maps, combines them using graph Return/Summing nodes,
and1338 trims sidechain channels off the output. Setting a source ID alone is
insufficient: explicit main0->0,1->1 and aux wires are required. Avoid the by-name
setter: duplicate/renamed labels are not identity.

However EditNodeBuilder.cpp1641 emits its automatic sidechain SendNode AFTER the
complete source track plugin list. VibeRa's MixerMasterGate is the last source
plugin. Directly using the source insert's native sidechain output would therefore
silence the detector when Route Only disables Master, and multiply it by Master
route gain otherwise. That is wrong for the stated send tap.

Recommended smallest adaptation: reuse current AuxSendPlugin prepared graph taps,
with one independent sidechain bus per source. Insert a dedicated prepared unity
sidechain send immediately before MixerMasterGate (after other send taps is fine).
Let RemoteEffect's graph-sidechain Return consume that bus instead of the engine's
automatic post-track bus. A narrow generic upstream hook is needed: an optional
`Plugin::getSidechainBusIDOverride()` returning no override by default; the
createSidechainInputNodeForPlugin receiver chooses that ID when present, otherwise
its current derived source-ID bus. RemoteEffect returns its prepared sidechain bus
ID; keep its source track ID valid for native dependency/channel handling. Existing
automatic post-track send can remain unused initially, with no duplicate input
because the custom bus ID differs. This is a tiny pinned/reproducible vendor patch,
not an engine replacement or new callback synchronizer. Reserve a deterministic
per-Edit bus namespace distinct from routing10000+ordinal; never persist transient
graph bus IDs in projects. Verify no collision in the prepared graph.

Prove this hook/tap in a tiny render BEFORE dependent UI. An engine-free fallback
is a hidden AuxReturn carrier track per source routed into a shared zero-output
sink track, with native sidechain source=carrier. It adds more graph nodes and
ownership/bookkeeping; prefer the narrow hook if the bounded experiment passes.
Do not simply use carrier output=None: the current offline root builder includes
tracks without destination tracks in its sum, so an otherwise silent carrier can
leak detector audio into exports. Runtime and render builders differ here.

## Worker/host contract: extend, do not replace

The qualified IPC8 Block input/output arrays have exactly2 channels. Unreleased
IPC9 extends the existing fixed shared packet to input[4], output[2], preserving
sequence tags/slots/events/ownership, and bump its exact host-worker signature.
The structure change makes old/new pairs incompatible; reject mismatches, never
attempt a best-effort mixed-version run. No IPC transport/worker redesign needed.

Add immutable startup request fields for main stereo plus selected auxiliary bus
index and channels. Worker validates bus existence/layout, sets main stereo,
enables only selected aux mono/stereo, disables other buses, and revalidates after
state restore and after prepareToPlay/restored controls, including actual native
SDK arrangements before DSP readiness. Publish a bounded actual bus catalogue
(indices, names, directions, current/default channel counts, supported mono/stereo,
selected actual layout) before ready. Report unsupported layout precisely. Probe
bus support only on worker control/preparation thread. Native snapshots may not
silently override project routing. Include layout in PluginHost::matches; current
descriptor/rate/block/alive comparison would otherwise reuse an incompatible host.

Worker can process a fixed external pointer list with output0/1 as main writable
channels and input2/3 as worker-owned aux scratch after packet acquire. No callback
AudioBuffer resizing/allocation. Publish only main output channels. Host packet
fill accepts independent aux buffer/channel mapping; clear unused aux samples
for every packet. A missing aux buffer means zero detector input, never stale PCM
or copied main audio. Keep dry bypass delay stereo-only and latency unchanged.

RemoteEffect declares buses honestly via getBusses/getChannelNames, returns main
output count2, forwards main+aux channels to PluginAutomationStream/PluginHost,
and preserves existing parameter-event partitioning. The native layout may be
mono auxiliary; graph wires can sum L/R into one aux channel and host applies
0.5 for the explicit average mapping, or wires select left/right/stereo exactly.
Represent mapping gains in prepared immutable control data, not native CachedValue
reads/allocating containers inside DSP.

## Application, validation, lifetime and errors

Project model: append optional sidechain config to PluginSpec; versioned strict
parse/state/validate; prohibit it on instruments in this first slice; reject bad
native bus indices/channels/mappings/NaN gains/unknown sources. Deleting a source
must explicitly disconnect affected sidechains atomically with Undo, not leave
dangling IDs. Removing/reordering effects retains instance-targeted wiring.

Routing cycle validation must include sidechain dependency edges, not just audible
MixerRoute edges. For each source->effect owner's insert dependency, reject any
cycle in the union (including audible route returning to source). Master cannot
be a sidechain source in this slice. No current-project rejection because a plugin
binary is missing: keep valid recoverable config/state and expose unavailable.

Canonical commands: effect sidechain connect/disconnect/mapping/gain/enable via
the same application validation/history as UI; add typed discovery, target
availability/readback and structured errors. Changing native layout requires
async state capture/new prepared worker and graph transaction; accepted != done.
Keep old working graph until prepared replacement succeeds; cancellation/failure
keeps old project/history/audio and returns a terminal reason. Capture current
opaque state before changing layout; preserve held unrelated workers/voices.
Do not mutate live native plugin layouts from an audio callback. Prepared graph
publication/reclamation follows existing control-thread lifetime pins; no callback
worker waits/refcount-last-release, I/O or UI. Gain-only sidechain changes can use
prepared immutable publication and one gesture/history transaction.

Missing/incompatible effect: retain descriptor/state/connection as unavailable,
effect dry pass-through consistent with current placeholders; never audible aux
leak. Offline export must report unavailable/failed plugin rather than claiming a
correct sidechained mix. Owned crash/timeout recovery retains layout/connection
and revalidates on retry. Main/bypass paths keep total declared worker+native delay;
sidechain path joins graph compensation before processor execution.

## Exact touchpoints

- src/PluginProtocol.h: signature, bounded layout request/catalogue,4 input channels.
- src/PluginHost.h/.cpp: startup/layout identity/readback, aux packet arguments,
  zero-fill and current fixed packet ownership; maintain stereo bypass.
- src/PluginWorker.cpp: negotiated pre/post-state layouts, catalogue before ready,
  prepared pointer count and aux processing; current forced-disabling block614+.
- src/PluginAutomationStream.h/.cpp: preserve aux channels through exact worker
  quantum/callback partitions; existing event/generation safeguards stay intact.
- src/AudioEngine.cpp: RemoteEffect buses/channel names/connection/tap ID;
  prepared per-source sidechain tap insertion; buildEdit and incremental graph
  update; preparation reuse/rollback/state/current-host identity include layout.
- src/MixerRoutePlugins.h: dedicated fixed prepared sidechain unity tap, separate
  from document's audible MixerRoute levels; no accidental Master gate dependency.
- pinned tracktion_Plugin.h + tracktion_EditNodeBuilder.cpp through original
  reproducible patch: generic receiver bus override only, default unchanged.
- src/PluginHost.h PluginSpec + src/Project.cpp: optional connection and migration;
  src/MixerRouting.cpp: union dependency validation/deletion safeguards.
- shared commands/MCP/UI/skill: explicit effect-target connection and readback,
  async terminal receipts, one Undo/cancel; native UI remains provider-owned.

## Required bounded tests before extensive UI

1. Current original mono-sidechain fixture: main sparse impulses vs independent
   detector impulses; amount0 leaves main unchanged; amount1 adds ONLY detector
   with known declared latency. Verify silence/no-main-copy/left-right sign.
2. Extend original fixture to genuinely accept stereo aux; verify channel identity,
   mono left/right/average and stereo separately. Do not simulate stereo on a
   mono-only native bus and call it supported.
3. Host packets partitioned across callbacks and worker quantum, unused aux cleared,
   same sequence ownership/MIDI/parameter/no-drop semantics and bypass PCM.
4. Actual graph WAV: source Master enabled/disabled/Route Only and gain changes;
   detector remains post-fader/mute/pre-Master and never leaks into main sum.
   Native declared delay31/257, unequal detector vs main chain, parallel wet/dry,
   bypass, buffer/rate changes. Independent sample oracle, not waveform-presence.
5. Atomic model/history/migration/save/reopen, reordered/deleted slots, source
   deletion undo, combined audio+detector cycle, invalid bus/mapping/index/gain.
6. Async prepare cancellation, unsupported layout, restore requiring other layout,
   missing/crash/timeout worker: keep old project/graph or explicit unavailable;
   no successful export replacing an existing file after failure.
7. Pattern/Song and held MIDI, callback allocation/lifecycle/overrun evidence, worker
   identity for unaffected slots. Native meaningful controls and semantic MCP
   end-to-end results; skill sources current. Real installed sidechain-capable
   effect qualification when available, with demo restrictions explicit.

These tests qualify only this optional one-bus stereo-main slice. Dynamic latency
live graph transitions, arbitrary multichannel layouts and every other open Mixer
row remain required module work; do not turn this slice into a module release.

Connected-solo traversal and source deletion must use the same combined audible/detector dependency graph. Explicitly test the ordinary-solo detector policy.
