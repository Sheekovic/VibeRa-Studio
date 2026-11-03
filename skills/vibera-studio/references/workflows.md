# Semantic workflows

These are tool calls after MCP initialization. Replace `<pattern>`, `<channel>`,
`<source>` and file paths with values read from the connected instance. Always
check `isError`, structured completion and actual state. No workflow uses
simulated mouse clicks. Current executable qualification is recorded separately
in repository STATUS/VERIFICATION; these recipes do not assert a passed run.

Selection context IDs are0 Global,1 Channel Rack,2 Piano roll,3 Playlist,
4 Mixer,5 Browser,6 Pattern selector,7 Sampler. `vibera_select` changes context;
canonical edits can target explicit IDs independently. Subset UI selection,
continuous gesture ownership and some contextual controls still have coverage
gaps; use explicit note/clip IDs for supported document operations and report
unavailable UI equivalence honestly. Optional `projectIdentity` and `revision`
guards reject stale mutations.

## Beat and melody

Read `vibera_state`, then inspect Channels and Patterns. Use `vibera_batch`:

```json
{"commands":[
  {"command":"steps.set","args":{"pattern":12,"channel":1,"beat":0,"enabled":true}},
  {"command":"steps.set","args":{"pattern":12,"channel":1,"beat":1,"enabled":true}},
  {"command":"notes.add","args":{"pattern":12,"channel":4,"beat":0.5,"length":1,"pitch":48,"value":0.75}}
]}
```

The numbers above are examples, not default IDs. Inspect notes to identify stable
note IDs. `notes.move` takes signed beat/length/pitch deltas; `notes.velocity`
uses normalized `value`. `notes.quantize` uses a beat grid. Read that operation's
current contract first. Undo once restores the entire batch; Redo restores it.

## Arrange and organize

Time signatures: `meter.change.add` {beat, numerator, denominator} starts a new meter on a
bar line of the one before (`set` with target, `delete` with target); read them with
`vibera_inspect entity=meters` (each row reports its 1-based bar).

Use `pattern.clone` to make an independent source; inspect Patterns to identify
its new ID. `pattern.group` takes `ids` and a group-name `text`; empty text
ungroups. `vibera_picker action=type kind=pattern`, then `groupFilter`, `select`
with source IDs and `place` with beat/lane. Multi-placement creates linked clips
at the same beat on consecutive lanes. Source edits affect linked placements.
`clip.unique` separates a selected placement. Audio/Automation picker types use
existing source IDs; they do not import files or create new effects implicitly.

Loop a section: `vibera_transport {"action":"loop","enabled":true,"range":[start,end]}`
sets the Playlist time selection (null clears it); in Song mode looping then plays only
that selection (`vibera_state` audio.loopBounds.timeSelection). With markers,
`vibera_command` `marker.next` / `marker.previous` move the playhead and
`marker.selectSection` selects the span between the markers around the playhead
(Song start/end at the edges). The selection is also the audio punch range.

Playlist tracks are lanes 0..127 (Track 1 = lane 0). `track.rename` {lane,text},
`track.colour` {lane,value: 0xFFrrggbb or 0}, `track.mute` {lane,enabled} (silences the
track's clips; clip mute flags stay), `track.solo` {lane} (that track alone; soloing it
again unmutes every track), `track.insert`/`track.delete` {lane} (shift later
tracks; delete removes that track's clips) and `track.move` {lane,value:-1|1}; one Undo
each, saved in project v50. `track.group` {lane,enabled} joins the group of the track
above and `track.collapse` {lane,enabled} folds a group (view only, its clips still play;
project v51). `vibera_inspect entity=tracks` lists lanes with settings.

## Mixer and automation

Starting transport waits only a bounded CONTROL registration period; it does
not manufacture device callbacks or advance an absent device clock. Read actual
transport/engine state after Play. Command completion does not establish heard
audio or physical-device operation.

For a shared effects bus or parallel send, follow [Mixer routing](mixer-routing.md).
Select the source through `vibera_mixer`, inspect available destinations, then
use `mixer.route`, `mixer.routeOnly` and `mixer.send`. Verify routing/history and
the exported result. Continuous levels use the owned `mixerSend` gesture domain.

`vibera_inspect entity=mixer` reads gain, pan, mute and ordered effect IDs.
`mixer.gain`, `mixer.pan`, `mixer.mute` and `channel.mixer` use the canonical
document commands. `automation.create` creates a Mixer-gain source/placement;
point commands and linked placements share the same model. Discover normalized
values, source/point IDs and actual links from state before editing. Plugin
automation requires valid native metadata/bindings; declaring a binding is not
proof the current worker can process it.
For independent curves with explicit target-link handling, follow
[Automation source cloning](automation-source-clone.md). Clone creates an unused
source; place it explicitly. Duplicate placement remains linked, while Make Unique
retargets one occurrence and clears its links.

## Plugins

Use [catalogue operations](catalogue.md) to scan explicit VST3 folders or standard
locations, poll actual completion, inspect blacklist/failures and resolve a stable
class ID to a descriptor. Canonical `plugin.instrument.create/replace` and
`plugin.effect.insert/move`, bypass/removal commands use project Undo. Read their
contracts and current project/worker state; a scan result is not worker readiness.
Use [loaded-plugin operations](plugins.md) for metadata, Controls, notifications,
automation preparation and recovery observation. Native editor requests remain
separate and cannot promise arbitrary vendor widget control. Discover bounded
scalar writes/history in the [native parameter guide](native-parameter-history.md);
programs and hidden provider-state reversal remain unsupported. Do not substitute
the pointer tool or invent native parameter identities.

## Recording

`transport.record` arms the same MIDI recording behavior as the UI; read
`recordArmed`, `recordingTake`, current mode and audio-device availability.
`vibera_transport` starts/stops/seeks and switches Pattern/Song. Physical MIDI
input must already be configured. `notes.record` commits a supplied typed take
through canonical validation; it is not evidence of live hardware capture.
Device/port/settings, monitoring and long-lived capture control still need
broader semantic coverage and physical qualification.

## Import a MIDI file

`vibera_project {"action":"importMidi","path":<absolute .mid>}` imports into the
selected (or `pattern`/`channel`) Pattern and Channel as one Undo step: notes plus CC,
pitch bend and channel pressure. Options: `track` (1-based, default all), `blend`
(keep existing notes/controllers), `realign` (first note to beat 0) and `useTempo`
(replace the project tempo map and time signature with the file's). `split: true`
makes one new Channel per MIDI track with notes (a settings clone of the target
Channel, named after the track) instead of filling one Channel. Program changes are
imported as controller events (`vibera_inspect controllers` type "program") and sent to
plugin instruments in playback; SysEx is not imported (`ignoredEvents`). `exportMidi` writes the Channel's CC, pitch
bend and channel pressure with its notes. Edit single events with
`notes.controller.add` {pattern, channel, beat, text cc|pitchBend|pressure|program,
pitch (CC number), value}, `notes.controller.set` {target, value} and
`notes.controller.delete` {target}; IDs come from `vibera_inspect controllers`.

## Missing samples

A project whose sample/audio files are gone still opens and saves (the last known
path is kept); playback/export of a missing file fails truthfully instead of
playing silence. `vibera_project {"action":"missingMedia"}` lists the Channels;
`{"action":"relinkMedia","path":<absolute folder>}` searches it (by file name, and
by content for collected media/<md5> copies) and relinks them as one Undo step
(`media.relink`), keeping every other Channel setting. UI: Tools > Find missing samples.

## Save/export and recovery

`vibera_project action=save path=<absolute .vibra>` returns a pending job.
Poll `action=job operation=<id>` until completed/failed. Read `fileExists` and
`fileBytes`, inspect/reopen the project and verify values. Existing outputs
require explicit `overwrite=true`; replacing dirty work requires
`discardChanges=true`. Normal UI file dialogs and MCP use shared save/export
application methods, including plugin-state capture.

`action=export path=<absolute .wav>` exports Song at48kHz/24bit/cut-tail,
matching the current UI. Poll its job and inspect the WAV. Nonzero PCM and
correct timing are automated evidence, not listening. A failed writer/export
must report terminal failure. Cancellation is not implemented; query state,
never assume disconnect cancels work. After process loss, inspect outputs and
open/recovery state before resubmitting; the old in-memory job ID is unavailable.

## Semantic selection and continuous editing (0.0.43+)

Read `vibera_inspect entity=notes, pattern=<selected Pattern ID>` and
`vibera_selection action=state, target=notes`. `action=set` accepts stable IDs
with replace/add/remove/toggle; all/clear and musical range selection are also
available. Notes default to the selected Channel; allChannels=true includes
other Channel parts. Ranges are [start,end) in quarter-note beats, with inclusive
MIDI pitches (notes) or lanes (clips). criterion=starts matches start positions;
overlaps includes sustained notes/clips crossing the interval. Audio Clip
duration is converted from seconds using project tempo. Selection is UI state,
not an Undo transaction. Invalid IDs refuse the whole selection change.

For a velocity sweep, call `vibera_gesture action=begin, label="Phrase velocity"`
and retain operation. It returns pending. Call update with operation,
command=notes.velocity, args={pattern:<ID>,ids:[<note IDs>],value:0.6}; update
again with another value. All previews use canonical validation and publication.
They remain pending until commit. `action=commit, operation=<token>` completes
one Undo step; read notes to verify, then edit.undo/edit.redo to verify history.
`action=cancel` returns cancelled and restores the starting project/dirty state.
An invalid update preserves earlier previews, returns failed and does not renew
the lease. Inspect and either correct, cancel or commit deliberately.

Only one gesture can own the model. While pending, other MCP mutations are
refused; state/inspect/discovery remain available. Idle lease defaults to 30s,
configurable 250-120000ms at begin; successful updates renew it. Expiry, local
UI document edits or recording preempt the gesture and cancel its previews.
Local UI Save, Export and Close also cancel the remote preview before capturing
or replacing the project. MCP save/export requests refuse a pending gesture;
commit or cancel it explicitly before requesting those jobs.
Poll action=state to observe the outcome. A token cannot mutate a replacement
project or another owner's gesture. Remote owners never take over local drags.
Gestures are process-local, not durable jobs. There is no success claim after
timeout/disconnection; inspect the current project and gesture before retrying.
Supported native-plugin control protocols remain separate: this tool cannot
control arbitrary third-party editor widgets or fix missing canonical parameter
Undo integration. Save/export job cancellation remains a separate open gap.

Loaded-plugin discovery/parameters/automation preparation are available in
0.0.44+: see [plugin operations](plugins.md). This supplements descriptor-based
creation; scanning the installed-plugin database is still an MCP gap.

## Connect and repeat a Piano Roll phrase

Use the [score tools recipe](score-tools.md): discover and run `notes.legato`, then
`notes.chop` on actual inspected note IDs, verify exact tails and Undo/Redo, save,
reopen and export. Read full selection with `vibera_selection action=state` when
it exceeds the compact `vibera_state` selection array. Tools edit the current
Channel part when explicit IDs are empty; they never select the whole Rack.

To join slices back into held notes, use the [Glue recipe](glue.md) and re-read
notes/selection after later IDs disappear. Linked Playlist copies keep the same
source Pattern. Save/reopen and export only after verifying the merged result.

## Project replacement and native state

A completed Save writes its captured snapshot. A newer native plugin change can
leave the current project dirty even though that Save job completed successfully.
Read project state after completion; saving and native dirty observation do not
create a musical Undo step. Never treat unchanged musical revision as proof that
newer opaque plugin state is saved.

UI close/open/new and MCP project new/open share CONTROL admission. Pending
Save/export/native preparation, accepted Audio Clip imports and queued or unacknowledged plugin Controls work
return busy; existing accepted jobs continue to their real terminal outcomes.
New/open freshly observe native revisions before accepting a replacement.
`discardChanges=true` deliberately permits current unsaved state to be discarded
once competing work is finished; it does not cancel pending control/file work.
After a file chooser returns, UI revalidates its original project identity and
asks about currently unsaved work before replacing it. No unconditional close
is queued after Save. Arbitrary native widget control remains unsupported.

`vibera_project action=state` freshly observes available native revisions and
returns `busy`/`busyReason` from the same transition admission. Pending work is
preserved even with explicit discard. New/Open jobs return `documentChanged`;
a failed job with `error.effectMayHaveOccurred=true` means replacement committed
before a later publication failure. Rediscover project identity and current state
before retrying. `error.effectScope=document_replacement` limits this witness to
document replacement; it does not promise rollback of earlier audio/UI effects.
A failed file load with `documentChanged=false` retained the source document.

`vibera_plugin action=closeControls` uses the same checked inspector close as its
window button. Queued/unacknowledged edits return `busy` and keep the window/value
alive; switching inspectors also waits. Poll Controls/native state until the edit
finishes, verify the value, then close. A successful close returns
`completionState=completed` and `closed=true`; this window operation is not musical
Undo history and does not close an arbitrary third-party native editor.

## Performance mode

Launch a Pattern live into the Song: `vibera_project {"action":"perform","pattern":id}`
(stop it with `"on":false`; no `pattern` just reads the launched pads). Requires Song mode;
starts playback if stopped. Each launched Pattern lands on the next bar on its own lane
(64+) and is re-placed every time it ends; each placement is a normal `clip.add` Undo step.
Stopping the transport ends the performance.

Each Pattern's launch step is `pattern.launchQuantum` {pattern, value}: -1 one bar
(default), 0 immediately, or 0.0625..64 beats; one Undo step, saved with the project (v49).
Pad n is the n-th Pattern: `vibera_command {"command":"performance.pad3"}` toggles it, and
`performance.pad1..16` can be MIDI-learned on the Controller page. The perform readback
lists `pads` (pad, pattern, quantum) beside `launched`. `performance.saveScene`
{value: 1..8, ids: Pattern IDs} stores a scene (project v52); `vibera_command
performance.scene3` launches it and stops the other launched Patterns;
`performance.stopAll` stops everything.
