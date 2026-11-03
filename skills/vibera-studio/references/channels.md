# Channel settings clone, ordering and deletion

Requires0.0.51/contract1. Discover stable Channel IDs, actual project order,
selection/group/filter and active editing/MIDI recipient with vibera_state and
vibera_rack(state). Equivalent entry points use the same services and Controller
history. Do not address visible row positions or simulate clicks for coverage.

Discover channel.clone/channel.delete with vibera_commands(includeSchemas=true).
They map to vibera_rack actions clone/delete, not vibera_command. Default targets
are the shared Rack selection; explicit ids override it. Require 1..128 unique,
current-session Channel IDs. Cloning must fit the 128-Channel project limit.

```json
{"action":"clone","ids":[7,9],"revision":42}
{"action":"job","operation":"returned-current-session-operation-id"}
{"action":"cancel","operation":"returned-current-session-operation-id"}
```

Acceptance returns pending, modelCommitted=false. Poll until completed, failed
or cancelled. Completed means the document edit actually committed after selected-
plugin state capture, not plugin activation or audio preparation. Check
audioPreparationPending, normal audio/plugin readback and actual playback/render
results before claiming an instrument works. Missing plugins can remain recoverable
placeholders. Failed jobs carry structured codes such as capture_failed,
stale_revision, stale_target and validation_failed. Terminal jobs remain readable
by their ID across project replacement; unknown/expired/foreign IDs fail. Recover
lost acknowledgements from vibera_rack(state).channelOperations before retrying.

Clone inserts settings/instrument copies immediately after each source, in global
project order. New Channel/plugin IDs are independent, with unique names. Sample,
sampler controls, gain/pan, Mixer assignment, group, bypass and captured native
plugin state are retained. No score/events/placements/external automation links
are copied. Clone selects the new Channels and makes the first the active recipient.
Use shared clipboard.copy -> clone -> wait -> clipboard.paste to transfer the
current Channel score for a variation. Other Patterns and linked placements retain
their identities; Paste is a separate Undo. Read createdChannels from the terminal
job and notes from vibera_inspect(entity=notes,pattern=...). Rendering bounds in
vibera_state are view coordinates, not musical data; do not compare them for Undo.
The first Paste into an empty part reveals its pitches in the Piano Roll without
changing horizontal zoom/time view or Rack focus. It is a view change, separate
from the document transaction and its Undo.

```json
{"command":"channel.moveDown","args":{"ids":[7,9]}}
{"command":"channel.moveUp","args":{"channel":7}}
```

These use vibera_command. IDs move selected runs one global slot, preserving
relative order/routing/score and active MIDI recipient; boundary no-op adds no
Undo. UI Alt+Up/Down and Shift+wheel over a name use the same commands. Filtered
rows still refer to global stable IDs. Reorder supports current-selection document
macros and held recording. Clone/delete require the take/gesture/macro/file
operation to finish; unsafe admission fails without committing the take. They are
explicitly unsupported in the synchronous document-macro runner, not fake macros.

Delete removes Channel-owned notes in every Pattern and owned Audio Clip placements.
Only dependent automation links/initial values are removed; reusable curves,
placements and unrelated links remain. It never deletes sample/plugin files or
Mixer effects. One Undo restores Channel/instrument state, notes, links and order;
Redo reapplies the same IDs. Undo/Redo are shared document history; selection/view
and global settings are not guaranteed to rewind with it. Save/reopen/export use
vibera_project and that tool's own job IDs. Do not poll a Channel job with the
project-job tool. Explicit cancellation before commit preserves document data;
its background/control capture drains before another writer is admitted.

Reorder keeps the existing graph/workers/voices. Structural clone/delete uses the
existing prepared graph replacement, preserves the device/cursor, and can restart
voices. Per-source MIDI1 articulation/controllers, arbitrary native-plugin widgets,
Clone with links and full FL workflow parity are not promised. Automated virtual
PCM/lifecycle checks, actual native desktop interaction, physical hardware and
listening are separate evidence categories. Consult exact current STATUS and
VERIFICATION before reporting compatibility or acceptance.

Slicing a sample: `vibera_project {"action":"sliceChannel","channel":id,"sensitivity":0.5}`
detects up to 16 hits in the Channel's sample and creates one sampler Channel per slice
(its own start/end window) plus a Pattern replaying them in place. With known points use
`vibera_command channel.sliceToChannels {channel, slicePoints:[0,...], length: seconds}`.
