# Fill steps and edit long Patterns

Requires0.0.49. Discover steps.fill/fill2/fill4/fill8 with vibera_commands and
includeSchemas=true. They use the same Controller validation/history as the
Channel name's right-click menu and Rack Menu > Fill selected Channels. The named
actions can be bound in Settings or used in macros; no new default key is assigned.

Read vibera_state, vibera_inspect(entity=notes,pattern=...) and vibera_rack(state).
Use stable Pattern/Channel IDs, not visible row indices or note IDs for fill ids.
All Channels in ids are validated before an atomic bulk edit; otherwise channel
defaults to the active instrument. The active MIDI recipient need not be visible
and does not change when a fill command executes. Audio Clip Channels cannot be
filled. rows[].canFillSteps and fillIntervals identify supported targets/values.
rows[].committedSequenceMode reports steps/pianoRoll/audioClip from the same
explicit-loop decision used for rendering and clicks. Dormant outside-loop
melody does not hide the filled step grid; active recording can show take notes.

```json
{"command":"steps.fill2","args":{"pattern":42,"channel":7}}
{"command":"steps.fill","args":{"pattern":42,"ids":[7,9],"interval":4}}
```

Substitute discovered IDs. The generic interval is an integer2/4/8, default4; named
commands fix their interval. One step is .25 quarter-note beats. Fill starts at
beat0 and covers ceil(Pattern.length*4) cells, including a partial last cell.
It does not change explicit Pattern length or stop/reset transport. To create a
longer part, use pattern.length in quarter-note beats; the Rack header offers
1/2/4/8/16-bar presets using current meter and a custom-length action.

Fill applies a repeating mask, not an additive overdub. Committed notes inside
the chosen Pattern window are assigned to their containing sixteenth cell. Notes
in off cells are removed. Notes in on cells become step events at cell onset with
.25-beat duration and retain IDs, pitches and velocities, including chords.
Empty on cells receive the ordinary Rack defaults (MIDI60,velocity.8). Other
Channels, routing/instruments and notes at/beyond the explicit Pattern end remain
unchanged. Exact previous notes and ordering return with one edit.undo; edit.redo
restores the fill. Repeating the same fill is idempotent and keeps IDs/history.
Macro current context uses shared Rack selectedChannels; surviving note selection
is pruned after conversion. Deactivating a chord step removes all its step events.

This is independently implemented behavior. FL's documented basic fill labels
and static repeating-mask evidence inform it; no direct reference runtime was
observed. VibeRa does not keep hidden muted-step notes: removed notes remain
recoverable through Undo. Its explicit Pattern window/floor-aligned conversion
may differ from FL settings-dependent preparation. Advanced Fill is still absent.

vibera_rack(action=scroll,firstStep=...) navigates the full Pattern, without the
former64-cell limit. viewport reports totalSteps/firstStep/maximumFirstStep and
page size. Step offsets are zero-based columns. Canonical steps.set addresses
Channel ID and beat, independently of the visible page/filter. Inspect the score
after edits. Fill during an active MIDI take changes committed score only; held
input/take survives and is committed separately when recording stops.

Invalid intervals, stale targets/revisions, mixed instrument/Audio Clip bulk
targets, duplicates and100000-note capacity failures return structured errors
without partial edits. No arbitrary silent truncation is applied. Synchronous
command completion means shared application state; audio publication can remain
pending until a following callback (vibera_state exposes it). Save/export still
use actual asynchronous jobs: poll terminal results, reopen the project and check
the WAV. PCM/tests do not establish human listening or hardware qualification.
