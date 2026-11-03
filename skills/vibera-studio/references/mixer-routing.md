# Mixer buses and post-fader sends

Base routing requires0.0.63. Discover the connected application and authoritative schemas.
This is stereo Mixer routing, independent of Playlist lanes and Channel Rack
display groups. It does not establish sidechains, arbitrary multichannel layouts,
hardware routing, arbitrary third-party UI access or full dynamic PDC support.
Send-level Automation Clips are not implemented by this increment; these
commands and gestures edit manual route levels.

Development source additionally implements persisted detector assignments. This
is not available merely because a connected binary is named VibeRa; require
discovery of `plugin.effect.sidechain`. The sole qualified0.0.63 distribution
does not gain unreleased commands from this guide.

For an effect with a supported auxiliary input, inspect its exact stable plugin
ID and source insert. Close its native editor, finish parameter work and disarm
recording. Use the slot's **Sidechain detector (layout request)** menu, or:

```json
{"command":"plugin.effect.sidechain","args":{"target":123,"detector":{"sourceMixer":4,"inputBus":1,"channels":2}}}
```

`channels` is1 or2; bus1..31 is a request, not provider capability. Source must
be an existing non-Master insert distinct from the effect owner. Combined audio
and detector feedback refuses. An active prepared audio device/source is required
to negotiate the layout before publication. The tap follows source effects,
fader, pan and mute, independently of its audible Master send.

Poll the returned string operation with `vibera_mixerchain action=job`; pending
never means audible completion. Use that tool's cancel action before publication.
A brief structural interruption is permitted; require completed state and
`nativeRecoveryRequired=false`, then verify the saved detector and actual worker
auxiliary layout separately via plugin/state readback. To disconnect, pass
`detector:null`; omitting detector is invalid. One Undo restores the assignment
and current captured plugin state; inspect/poll its own history receipt.

Missing providers retain saved assignments and recoverable state; their presence
in a project does not establish a working detector. Unsupported layouts must fail
without changing the incumbent model. Whole-project Save/export captures current
native state. Portable effect-chain presets do not transfer source insert IDs or
detector routes. Full delayed/aux routing transitions, dynamic PDC and arbitrary
multichannel buses remain under development; discover availability and retain
actual failures. Original SDK signal tests are not arbitrary-plugin qualification.

1. Inspect `vibera_inspect entity=mixer` for stable insert indices, names, ordered
   effects, routes and routingTargets. Route targets expose availability/reasons.
   `vibera_mixer action=select insert=1` uses the same application selection as
   the UI. Selection is workspace state; it does not edit the document or Undo.
   `action=state` exposes the selected source and controls. Master0 is an output
   destination and may be selected; it cannot send to other inserts.
2. Assign a Channel with `channel.mixer`, then enable an insert-to-insert route
   using `vibera_command command=mixer.route args={mixer:1,target:3,enabled:true}`.
   Other sends and the default Master path remain. New routes start at unity;
   enabling an existing route retains its level. False removes only that route.
3. `mixer.routeOnly args={mixer:1,target:3}` retains only that destination,
   preserving an existing level or starting at unity. Source1 now feeds bus3;
   bus3's existing Master route carries it to the output. Do not leave the direct
   Master route enabled if an unintended dry parallel path would duplicate audio.
4. `mixer.send args={mixer:1,target:3,value:0.5}` changes an existing send's linear
   gain to50%. Range0..2;1=unity;2=200%. Zero retains an enabled graph edge; use
   mixer.route enabled=false to remove it. Taps follow effects, fader, mute and
   pan; destination effects/fader then process the received signal. Self/feedback
   routes are rejected, including zero-level graph edges. Missing inserts/routes,
   nonfinite/out-of-range levels and inapplicable fields produce structured errors.
5. For a continuous level, begin `vibera_gesture action=begin domain=mixerSend
   sourceInsert=1 targetInsert=3`. Retain its token. Update only mixer.send with
   those explicit indices through the gesture tool; changes actually affect the
   document/audio, while history is grouped. Commit creates one Undo transaction.
   Cancel or idle expiry restores the captured baseline. Idle lease250..120000ms,
   default30000. Foreign tokens, wrong commands/destinations and competing writers
   fail. This domain supports a held MIDI recording take; it never changes its
   Channel recipient. Selection cannot steal the lease. Target fields apply only
   at begin. Check actual completionState, then read routes/history/audio status.
6. Ordinary document commands support Undo/Redo and ordered `vibera_batch` where
   admission permits. Verify complete rollback if a later command fails. A
   current-context saved macro derives the source from the selected insert and
   keeps its explicit destination; a fixed-context macro retains both indices.
   UI Macro Builder uses destination:percent for levels, destination:0/1 for
   enabling, and destination alone for Route Only. Recorded level drags are one
   final macro step. Broad remote saved-macro management remains an inherited gap.
7. Save, reopen and inspect actual routes/levels. Export WAV with real project jobs
   and poll their terminal state. Pending means accepted/running, not completed.
   Preserve failed/cancelled results and verify the actual output. Schema18 adds
   explicit nondefault routes, retaining exact implicit Master routing for genuine
   older files. An older-version file carrying routing data is rejected.

UI defaults: open Mixer with F9 or the Mixer toolbar button. Select the source
insert, then click the up arrow at the bottom of
the destination. The selected source stays selected. Right-click that row for
Route to this track only, disabling, precise percentage or unity reset. Drag an
enabled destination's level control; Shift makes fine changes, the wheel changes
its value, double-click resets100%, and Escape cancels an active drag. Shortcut
actions and macros use the same canonical commands. These are independently
implemented interactions guided by current official documentation; installed
FL24.2 runtime details and human familiarity require separate qualification.

Send-level changes reuse prepared timelines and graph resources. Enabling or
removing non-Master edges changes graph topology; inspect current release evidence
for measured continuity/latency limits rather than assuming all topology changes
are free. Plugin worker latency/readiness is exposed by existing plugin services.

Development source: application sends now use a five-ms gain ramp, prepared away
from DSP and retained across compatible graph changes. The command value and
route readback remain the requested linear target, not a measurement of each
intermediate audio sample. This is not yet in the qualified0.0.63 package, and
does not establish complete playback-safe topology editing or export equivalence.

Pointer hit tests are restricted to the visible Mixer strip area: a clipped
send never responds to wheel or unity clicks over the FX pane. Semantic routing
still targets explicit insert IDs independently of scrolling.

## Channel assignment publication (unreleased Mixer source contract)

Discover the connected executable before using this development contract.
`vibera_command` with `command="channel.mixer"` and
`args={"channel": <stable Channel ID>, "mixer": <existing insert index>}` uses
exactly the same application admission and canonical history as the Channel Rack
and Mixer assignment controls. Select/read Channel IDs first; names are display
labels. Invalid/missing targets leave project and history unchanged.

Read the returned `completionState` and `operation`. During prepared native
playback, document/history commit can succeed while audio remains `pending`.
Poll `vibera_mixer` with `action="routingJob", operation=<ID>` until terminal;
`routingState` reads the latest retained operation. `vibera_state.channelRouting`
provides the same receipt. Native completion requires an observed exact selected
publication; posting, a larger queued serial, an unchanged play button, and a
successful query never establish it. `nativePostedSerial` describes a separate
possibly newer queued graph. Sample reference ranges are half-open native samples,
not beats or elapsed wall time. Polling never advances an idle audio callback.

Initial stopped no-device composition is explicitly document-only when there is
no retained context/device lifecycle or conflicting native owner. A successful
job has `documentCommitted=true`, `audioDisposition="deferredNoDevice"`,
`completionScope="document"`, `nativeCompletionKnown=false` and no posted audio.
This is terminal document success, not a forever-pending graph or proof of sound.
A native Edit is prepared from current canonical routing before later monitoring
or Play. Prior device loss with retained ownership refuses this offline admission.
Immutable loaded-sample readback and exact spec-qualified plugin worker/editor
state remain usable without a DSP graph. Native worker state must be captured
before different-device preparation; `routing.stateCaptureRequired` preserves the
worker and refuses preparation until its editor is closed and a shared save/state
capture is current. Automatic unsaved-state restoration is not qualified by this
source guidance. Never overwrite an existing project as a recovery shortcut.

`edit.undo`/`edit.redo` use the actual history transaction and their own native
publication receipt when assignments change. Await one route before another edit,
mode change, file capture, recording, or history operation. A route-only
`vibera_batch` combines1–64 assignments into one transaction/graph; later invalid
targets refuse the complete batch. Route-only UI macros use the same aggregate.
Mixed route/content batches/macros and route changes inside preview gestures
currently refuse whole, before first mutation. They do not silently run several
native rebuilds or partially commit. Ordinary notes, steps and existing send
levels retain their incremental path. Genuinely structural changes first perform
an explicit full current-canonical projection handoff; this is distinct from a
routine live route update.

`cancelRouting` cannot undo an already published transaction; it returns typed
`routing.tooLate` (unknown/expired IDs return `routing.unknownOperation`). After
actual completion, use checked Undo. The latest32 terminal receipts are retained;
old IDs expire. Historical `projectIdentity` belongs to the operation;
`currentProjectIdentity`/`currentRevision` identify the current Studio document.
Device/context interruption is a failed audio job even if its document was
committed; inspect the receipt and restore current canonical audio explicitly,
never retry an already committed edit blindly.

Discover `channel.mixer` through `vibera_commands` with `includeSchemas=true`.
Its arguments, result schema and polling/cancellation guidance share the actual
application contract. Omitted `channel` follows the selected stable Channel ID;
explicit0 is invalid. Omitted `mixer` means Master0. Mixer indices are existing
stable nonnegative integers through2147483647, including sparse indices, rather
than visual row positions. Canonical validation rejects missing inserts. These
operations do not create an insert or change the default18-strip layout. Supply
current project identity/revision guards and inspect operation scope before
interpreting a completed document-only receipt as native playback.


### Initial device appearance after offline routing

VibeRa prepares the current document automatically when the first audio device
appears after a document-only route. Play can also request preparation followed
by playback. No file Save is required. This is a CONTROL async operation: native
editors on workers that require a different configuration must close with a real
acknowledgement; VibeRa captures actual opaque state before preparing replacements.
An exactly reusable same-config worker and its editor remain intact.

Use `vibera_mixer` with `action: devicePreparationState` to discover its current
operation. Query `operation` optionally; expired tokens fail. Retry a failed or
cancelled operation with `retryDevicePreparation` and its exact positive integer
token. Retry returns a new token. `cancelDevicePreparation` requests cancellation;
it stays pending until owned native work drains. Stop also requests cancellation.
Only the current token is retained. Wait for `completionState` completed, failed,
or cancelled and inspect its structured error before further edits.

Completion scope is `nativePreparation`, with `nativeCompletionKnown: false`.
This confirms actual native preparation, not selected graph, audible output,
hardware, or a completed musical render. `stateCaptured`, `workersReusedOnly`,
`documentCommitted`, `nativePrepared`, device rate/block and source identity are
separate readbacks. A committed opaque refresh remains recoverable even if a later
native preparation fails/cancels; cancellation does not pretend to undo that refresh.
Existing pure-route Undo/Redo preserves the current captured opaque state.

While pending, document/history/gestures, Controls writes, native editor changes,
recording and file jobs are refused through shared admission. Read-only state and
owned cancellation remain available. Retry after resolving the reported cause;
VibeRa does not endlessly reopen/capture on a failed timer. No arbitrary native
plugin widget control or universal atomic fork of self-changing plugins is promised.

Snapshot delivery errors use `device.capturePollException`: the exact owned job
drains its native capture before failing. Canonical metadata is never fabricated
from an incomplete snapshot; the retained worker remains recoverable. Resolve the
reported cause, then retry the current token. No file Save is required.

Unreleased target-selector behavior: the sampler/channel-settings Mixer choice
and the Rack target popup both dispatch canonical `channel.mixer`. UI item tokens
are private presentation ordinals; AI commands always use the actual stable
insert index returned by discovery, including sparse indices. An open sampler
refreshes target names and membership without reassigning an old token to a
different stable insert. A queued choice whose target or source Channel vanished,
or whose project session changed, is refused rather than routed elsewhere.
UI presentation refresh creates no musical history. Confirm the canonical
Channel destination and routing-job outcome after assignment; a no-device
document completion is not DSP selection. Target-menu component qualification
is separate from live playback, OS input, listening and hardware evidence.

Internal audio-domain zero-history proofs remain default-disabled engineering
infrastructure. They are neither an MCP operation nor permission to bypass a
reported routing refusal. A certified unused delay queue alone does not complete
the whole-route transfer, native selection or canonical history transaction.
Continue using discovered routing availability and actual job outcomes above.

The additional internal whole-inventory experiment is also unavailable to UI/MCP.
Its restricted actual application Pattern/Song cases now qualify canonical
route/cancel/Undo/Redo and held/score/release PCM with retained native inbox owners.
This remains initial-only, one instrument, no FX/sends or dynamic source latency,
and unavailable to UI/MCP. Preserve ordinary routing availability and reported
failures; never infer completion from accepted preparation or graph posting.
Fixed-source proof does not support a plugin changing
its latency during playback. Declared worker latency and actual processed-packet
stamps are distinct; general dynamic compensation and recovery remain incomplete.

Internal development now forwards complete actual PCM segment receipts through
the shared automation stream. Real SDK tests cover offline1Q/live2Q equivalence
and invalid requests preserving pending MIDI/parameters. This adds no discoverable
music operation and does not change routing availability. The initial-only
source/root fence now rejects a known changed latency before source input and
checks actual emitted segments before downstream audio; it retains a healthy
worker for explicit CONTROL disposition. This does not implement dynamic PDC.
One bounded pending-consumed race was unobserved; never infer its qualification.

The initial-only native Mixer Track MIDI boundary has passed its bounded actual
runtime cases. Its restricted admission rejects populated native launcher slots,
unknown consumers, FX and unsupported topology; it must never silently discard
queued MIDI to admit a route. AI clients must continue to use canonical routing
commands and their actual asynchronous readback. Internal test opt-ins are not
supported MCP features or proof that route-wide compensation is complete.
An unreleased source-clock correction schedules self-generated notes and native
automation on the source musical timeline; actual plugin/worker delay remains
reported separately. Its broader source regressions remain required.

Unreleased ordinary native MIDI ownership repair: compatible graph candidates
retain one exact Track/context inbox. Preparing or cancelling a candidate does
not replay queued notes or discard pending releases. A missing predecessor with
a surviving owner is an explicit preparation failure, not permission to drop
events. UI and MCP still use canonical routing commands and the same terminal job
readback above; no new endpoint or client-side queue workaround is needed. This
does not establish complete route-wide compensation, generic callback lock
safety, physical MIDI or listening qualification. Discover the connected binary;
the qualified0.0.63 package does not contain this development repair.

Development live-plugin transport prepares an immutable physical/source
callback budget for ordinary playback. Worker transport delay is included in
`latencySamples`; it is separate from the unchanged audio-device buffer. Offline
export prepares independent workers and trims declared graph latency. A pending
context is not completed playback. Inspect `audio.pluginSourceState`
(`notRequired`, `controlOnly`, `pending`, `active`, `fault`, `unavailable`),
`pluginTransportPackets`, `maximumPhysicalFrames`, `maximumSourceFrames` (frames)
and `pluginDeviceEpoch` alongside worker counters and actual operation outcomes.
Device-free plugin controls do not establish live readiness. Unknown custom
providers need an explicit callback-frame contract; observing a small callback
does not establish its maximum. Activation failure after replacement reports a
committed, dormant context with transport stopped, not successful playback or
rollback. This integration is under verification; discover the connected binary
and retain existing routing/PDC limits.

Ordinary live replacement currently supports physical callbacks no larger than
the native device quantum. Unknown custom providers have no inferred bound;
multi-quantum custom callbacks are explicitly unsupported by this handoff.
Device reconfiguration uses the existing shared `vibera_mixer`
`devicePreparationState`, `retryDevicePreparation`, `cancelDevicePreparation`
actions. Preserve the current operation token and poll its actual outcome.
Preparation closes owned editors, captures current native state, retains workers
while retiring the obsolete context, waits for native device preparation, then
prepares the replacement. Cancel after retirement retains a deferred document
and recoverable native state; it does not claim to restore the old device graph.
Even a completed preparation is separate from first successful callback evidence
in `audio.pluginSourceState`; `pendingAudioUpdate` includes its pending gate.

For a failed source session with healthy workers, explicit Play (the shared
`transport.play` command) or retry of the current preparation operation can
reconstruct the context with the same workers and their current native state.
It does not reload an older saved state or reset the audio device. Automatic
timer retry is restricted to genuine device refusals, preventing a repeated
session restart loop. Dead workers still require explicit plugin saved-state
recovery. A committed activation exception reports a stopped failure; it does
not promise rollback or uninterrupted voices during structural recovery.

First audio-device startup can retain an already-open native editor and its
existing worker when configuration matches and the worker has never processed
audio. This is a CONTROL-only preparation refinement of the same commands and
readback, not permission to migrate a warm worker between device epochs. A
processed or mismatched worker follows the capture/replacement policy above.
Poll the operation and `audio.pluginSourceState`; an open editor alone does not
prove active audio. This development behavior is not in qualified0.0.63.
