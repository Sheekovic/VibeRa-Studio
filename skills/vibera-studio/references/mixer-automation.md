# Mixer Automation Clips — development source

Discover the connected executable first. Release support follows its actual
command registry and qualification manifest. Current development source may add
operations absent from the qualified package. UI contexts, actual native playback,
offline output and semantic workflows need their own current evidence. No arbitrary
native plugin UI control is implied. For independent curves with clear/preserved
target links, see [Automation source cloning](automation-source-clone.md).

Use `vibera_commands` with `command=automation.create, includeSchemas=true`. The
existing canonical target commands accept `mixer` (stable ID; Master0) and optional
`automationControl`: `gain` (legacy default), `pan`, `stereoSeparation`, or `send`.
Send additionally requires `automationSendTarget` (existing stable destination,0=Master);
`mixer` is the existing non-Master source. The exact route must already be enabled.
Gain is linear0..2,1 unity. Pan and separation are bipolar-1..1,0 centre/identity.
Separation<0 subtracts opposite-channel content;>0 merges toward mono. Pan retains
the project's native resolved law: the existing linear law reaches x2 at hard
pan; do not assume a unity-limited balance. Point values always remain normalized0..1.
Send uses linear0..2,1 unity. Channel targets and controller touch recording remain unsupported.
Send UI/model support does not itself qualify the actual per-frame AuxSend/MasterGate audio path.

Create a pan clip on insert3:

```json
{"command":"automation.create","args":{"mixer":3,"automationControl":"pan","beat":0,"length":4,"lane":2}}
```

Creation captures the saved manual target into a flat source and initialization;
centre pan/separation becomes normalized0.5. Read `vibera_inspect`:

- `entity=automationSources` pages stable source IDs/names/length/counts.
- `entity=automationPoints, source=<id>` pages point IDs, beats, normalized value,
  shape and tension. Edit with canonical `automation.point.*` commands.
- `entity=automationLinks, source=<id>` pages actual serialized kind/owner/parameter
  and target-domain range. Kind0 gain,1 pan,4 native parameter,5 separation,6 send;
  for6, owner is source insert and parameter is destination insert (including0).
  native parameter IDs must not be confused with Mixer owner IDs.
- `entity=automationInitialValues` pages exact initialized key/value tuples.

Every page returns total/nextOffset/projectIdentity/revision. Continue until
nextOffset is null. Current `vibera_state.automationSources` is a bounded summary;
check its truncation flags and use inspect for the full data. Mixer inspection's
`automationTargets` supplies target units, range, manual and initialized value.
These are canonical model values, not observed DSP/audible state.

Link another control with `automation.link` using source ID as `target`, exact
Mixer ID and `automationControl`; for send include the same explicit `automationSendTarget`.
`target` remains the Automation source ID, never the send destination. `automation.range` retains legacy field names:
`beat` is output at normalized0, `length` is output at normalized1. They are
target units for this operation, not musical time. Both must be in the target's
domain; inverted ranges are valid. Unlink with `automation.unlink`.

`automation.init` uses explicit `value` in target units; `automation.init.remove`
removes only that initialized key. The manual knob fields are never overwritten
by playback. Song uses initialization before clips and holds the last endpoint
after clips. Deleting/muting a placement does not delete initialization. To restore
manual behavior completely, remove/unlink remaining placements and remove that
initialization. Stop and Pattern retain existing manual-only behavior.

Canonical target mutations return `completionState=completed` with
`completionScope=canonicalAutomationTarget` and `dspSettlingAcknowledged=false`.
Read the actual project result; this does not acknowledge graph adoption,
sample consumption, smoothing completion or heard audio. File Save/export jobs
remain asynchronous: poll their job token until completed/failed/cancelled.

`vibera_batch` groups compatible canonical edits into one checked Undo and rolls
back on later validation failure. `vibera_gesture domain=document` provides owned
point/init updates with one commit or exact cancellation. Existing recording,
retained-take, file/device and gesture admission remains in force; finishing a
query or arming Record does not prove captured automation/touch recording.
Use optional command revision guards against stale state and re-read after
project replacement. Do not silently retarget IDs or manufacture success after
an error.

Pan curves are evaluated per sample without the manual centre snap/15ms ramp.
Separation curves are evaluated per sample; phase/swap during separation
automation use a separate5ms orientation-matrix transition. This is an explicit
original VibeRa automation rule, not an observed FL parity claim. The existing
manual matrix ramp remains unchanged and advances underneath automation.

Typed automation macros serialize their optional control in shortcut profile4;
send macros also preserve the destination in profile6. Older profiles1..5 cannot
masquerade as send definitions. Fixed-context macros retain both IDs; current-context
macros follow the selected SOURCE insert and retain the explicit destination, refusing
a missing/self/disabled edge rather than enabling or retargeting it.
Other typed automation macros retain profile4,
while old profiles keep their original1/2/3 format unless the new field is used.
Current-context macros resolve the existing selected Mixer ID; fixed-context
macros preserve their stored ID. Core macro serialization/replay tests are
separate from semantic MCP macro setup, which still has an inherited gap. A batch
is a batch; do not label it as proof of an actual saved Customization macro.

Schema25 appends send kind6 without changing0..5; earlier schemas reject send keys.
Schema22 preserves old serialized enum IDs0..4 and adds separation5. Old projects
migrate without invented automation links/initializations. New targets under an
old schema, unknown future keys, invalid ranges and missing owners fail without
replacing the project. MIDI Learn, touch/event recording, send automation, wider
MX-G and full route-wide compensation remain required module work.

## Mixer and source-properties access

Right-click the Mixer pan knob, separation control or volume fader for Create
automation clip, Init song with this position, Delete initialized control,
Link to existing source and Select linked source in Playlist. Edit value remains
available in that menu. The insert-name context menu also provides Volume, Pan
and Stereo separation automation menus when compact strips hide inline controls.

Create selects the actual clip and its automation drawing source in the Playlist
Picker. Select linked source changes workspace selection only. Its semantic
equivalent uses existing `vibera_picker` (type automation, empty filter, select
source ID) and `vibera_editor` show Playlist. Read selectedIds/source kind back;
selection is not a musical mutation or audio-consumption acknowledgement.

Double-click an automation clip header for source properties. The target list
retains the existing volume rows, followed by explicitly named pan and separation
rows. Output-at-source0/source1 values are linear gain0..2 or bipolar-1..1.
Reverse ranges are supported. Apply combines rename/link/range into one checked
Undo; invalid values restore the whole properties edit. Remove link affects only
the exact selected control, preserving other kinds on the same insert.

Delayed menus/dialogs capture project identity, revision and stable target/source
IDs. A newer document requires reopening them; changing selection never retargets
an existing intent. Unrelated gestures or active file/device/routing work refuse
mutation. These controls change no MIDI source or capture clock; armed/active/
retained takes remain intact. This does not establish touch recording or hardware
qualification. UI and MCP both invoke the canonical automation commands listed
above; there is no separate mouse-driven semantic implementation.


Send UI access: select the SOURCE insert, then right-click the actual enabled
send LEVEL knob in the destination strip footer. The route button's separate
right-click still opens routing. The send menu offers Edit value, Create clip,
Init song, Delete initialized control, Link source and Select linked source.
Edit value reuses the existing numeric percent0..200 service and canonical
`mixer.send` on the captured pair; this does not write an automation point.
Automation source properties append actual send pairs AFTER all original
gain/pan/separation choices. Project/revision changes invalidate pending menus
and dialogs; cancellation changes neither target nor history.

Example after discovering that source1->destination3 is enabled:

```json
{"command":"automation.create","args":{"mixer":1,"automationControl":"send","automationSendTarget":3,"beat":0,"length":4,"lane":2}}
```

Read `vibera_inspect entity=mixer`: each actual send automation target includes
sourceInsert, destinationInsert, linear manual gain, range, initializedValue
(null when absent) and linkedSources. These are canonical values, not audible
completion. Send creation samples that exact route's saved manual level, never
the source fader or currently selected destination.

Disabling/removing a route (including routeOnly removing other routes) refuses
while its send automation has any link OR initialization. Explicitly unlink
that pair and remove its initialization before removing the route. Insert
delete may explicitly remove affected automation links/initial values; it never
retargets them to the replacement insert or deletes shared curves/placements.
A surviving native effect detector reference always requires explicit clearing.

Actual DSP playback/export, loop/latency correctness, held-take safety and
controller pair pickup each need current separate acceptance. Do not treat a
model completed result, private native proof or profile roundtrip as these results.

## Song MIDI recording

See [Song MIDI automation recording](mixer-automation-recording.md) for the shared
recorder, target admission, source clock, history/cancel, retained failures and
current Pattern/native-parameter/overdub limitations. This is an internal Mixer
increment, not completion of the whole automation/controller checklist.

Channel volume / pan automation: `automation.create` (and link/init) take
`automationControl` `channelGain` (linear 0..2, the Channel volume range) or `channelPan`
(-1..1) with `channel` = stable Channel ID. Playback and export follow the curve at block rate
(smoothed by the Channel balance stage) while playing or rendering; stopped, the Channel's own
value applies. Actions `channel.automateVolume` / `channel.automatePan` (and right-click Volume /
Pan in a plugin instrument's channel window > Create automation clip) create a clip over the
Song on the first free Playlist track from the current value.
