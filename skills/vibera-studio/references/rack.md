# Channel Rack selection, groups and filters

Requires application0.0.48 and `vibera_rack` in tools/list. Discover command
schemas with vibera_commands(command=...,includeSchemas=true); machine schemas
under generated/ are exported from the registry rather than maintained here.

Read `vibera_rack {"action":"state"}`. It returns stable groups, member counts,
visible Channel IDs/rows, independent selectedChannels, activeChannel,
activeChannelVisible and the current filter. IDs belong to projectIdentity.
Viewport readback reports firstRow/firstStep, page sizes and maximum offsets.
Use action=scroll with firstRow and/or firstStep to navigate the same clamped
viewport as mouse wheel/Shift-wheel and the horizontal scrollbar. Offsets are
zero-based visible rows/step columns, not Channel IDs; filtering resets firstRow.

Example: inspect vibera_state.channels, identify the drum IDs by their real names,
then call:

```json
{"action":"select","ids":[2,3],"mode":"replace"}
{"action":"group","name":"Drums"}
{"action":"filter","kind":"unsorted"}
{"action":"filter","kind":"group","group":42}
```

The numbers are examples; substitute discovered IDs. Group Selected uses shared
bulk selection unless explicit ids are supplied, reuses a case-insensitive name
or creates one atomically, then displays that group. All and Unsorted are reserved
view filters, never group names/IDs. Filter state is local navigation, resets to
All on project replacement, and is not saved in the musical project/history.
Membership and empty groups are saved in schema11; earlier application versions
cannot open schema11 projects. Preserve originals when testing a migration.

Use canonical rack.group.create/rename/delete/assign through vibera_command.
Create takes text; rename takes target stable group ID and text. Delete takes
target and unassigns members without removing instruments, notes or routing.
Assign requires nonempty ids plus target group, or text to create/reuse a name.
Assign target0 with empty text moves members to Unsorted. Each actual document
edit is one Undo step; verify with rack state after edit.undo/edit.redo. Macros
with current context use the shared Channel bulk selection for group assignment.

`select` supports replace/add/remove/toggle; it does not redirect MIDI unless
activeChannel is explicitly provided. `all` defaults to visible Channels;
allChannels=true targets the complete project. Bulk selection does not layer
MIDI input. A hidden active Channel remains the single MIDI target, reported in
state and the Rack footer. UI selector left click changes both single selection
and active target; right drag changes bulk selection only; double click selects
all visible. Alt+G groups, PgUp/PgDn change filters, the filter/Menu controls add,
rename/delete and unassign. Channel names still open instruments; PR is separate.

New Channels created through shared application commands inherit the displayed
named group when target is omitted/zero; explicit nonzero target overrides it.
Switch to All/Unsorted first to create an unassigned Channel. Replacing an
instrument retains its membership. Filtering does not mute hidden Channels or
change Pattern/Song playback. Address steps/notes by real Channel ID, not visible
row index. Invalid/stale IDs, reserved names and schema errors return structured
failure without partial edits; use projectIdentity/revision guards where needed.

Rack services are synchronous and return completionState=completed only after
shared application state is updated. Group-name UI dialogs can cancel without
editing; semantic clients supply a name directly rather than manipulating dialogs.
Save/export are separate asynchronous vibera_project jobs: poll actual terminal
results, then reopen/check membership and inspect the actual WAV. A request being
accepted never proves file completion. Third-party editor widgets remain outside
arbitrary semantic control. Legacy MCP gaps are deferred, not waived.
- Transpose selected Channels: `rack.transpose` (`ids` Channel IDs, `value` whole semitones
  -127..127, nonzero; one Undo) moves their notes in every Pattern; refuses when a note would
  leave 0..127 or the Channels have no notes. UI: Channel Rack options menu > Transpose selected
  Channels... (semitones dialog, `vibera_dialog` field `semitones`).
- Assign selected to free Mixer inserts: action `rack.assignFreeInserts` (select Channels first
  with `vibera_rack` select) gives each selected Channel, in Rack order, its own unused insert
  (lowest index first) as one routing plan and one Undo; refuses with
  `rack.assignFree.insufficient` when there are fewer free inserts than Channels. UI: Channel
  Rack options menu.
- Row knobs: every Channel row has Pan (bipolar) and Volume (0..200%) knobs before "PR". Drag
  up/down (150 px full range, Shift fine) is one Undo step (`channel.pan` / `channel.gain`);
  Alt+click resets (volume 0.7, pan centre); right-click offers Create automation clip (same as
  `channel.automateVolume` / `channel.automatePan` for that Channel) and Reset. The Rack label
  column is 230..250 px wide to fit them.
- Channel button width (Channel Rack menu > Channel button width): actions `view.rackButtonsNarrow`,
  `view.rackButtonsNormal` (default) and `view.rackButtonsWide`; read `state.rackButtonWidth`.
