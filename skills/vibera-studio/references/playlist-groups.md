# Group an arrangement section

Requires VibeRa0.0.59 or newer. Discover actual `clip.group`, `clip.ungroup`,
`clip.move`, `clip.duplicate`, `clip.delete` and `playlist.grouping` contracts
through `vibera_commands` with `includeSchemas=true` before editing.

Groups contain **placement IDs**, across Pattern, Audio and Automation Clips.
They are separate from Pattern source groups, Rack groups, lanes and Mixer buses.
All musical times in `clip.move` are signed quarter-note beat deltas. `lane` is
a signed lane delta; `length` defaults to0. Audio source windows in inspection
retain seconds, while `vibera_state.clips.lengthBeats` is always beats.

1. Inspect `vibera_inspect entity=clips`; identify the placements representing
   the section. Group two or more distinct IDs with
   `vibera_command command=clip.group args={"ids":[... ]}`.
2. Read `vibera_state.clipGroups`, `clipGroupingEnabled`, `selectedClips`
   (explicit anchors) and `effectiveSelectedClips` (actual selected members).
   State previews are bounded. Use paginated `vibera_inspect entity=clipGroups`,
   then `entity=clipGroupMembers group=<stable ID>` for complete membership.
3. Move one member with `clip.move`, for example
   `{"ids":[101],"beat":4,"lane":1}`. Enabled grouping expands all members;
   the whole selection clamps at boundaries without collapsing relative spacing.
   A continuous move uses `vibera_gesture`: begin, update, commit one Undo, or
   cancel to restore the exact baseline. These document leases refuse armed
   recording. Membership-only local commands can preserve a MIDI take; moving
   or deleting its arrangement context follows the existing take-finalization rules.
4. Duplicate with `clip.duplicate ids=[member] beat=8`. Copies receive a fresh
   group, while source Pattern/Audio/Automation IDs remain linked. Inspect actual
   new placement/group IDs; never derive IDs from ordering or arithmetic.
5. Temporarily edit individually with `vibera_editor editor=2 action=grouping
   enabled=false`; re-enable explicitly afterward. Membership remains intact.
   The canonical `playlist.grouping` action toggles; prefer the idempotent editor
   operation when setting a known state. An optional command `grouping=false`
   bypasses expansion for that one operation; `true` forces it. Grouping behavior
   is a workspace preference, not a musical Undo transaction. Changing it during
   a gesture is refused. Do not substitute it for gesture cancellation.
6. Ungroup from any member using `clip.ungroup ids=[member]`. This dissolves
   the intersected group without deleting placements. Delete while enabled acts
   on all members. Individual deletion prunes membership and dissolves singletons.

Use shared `edit.undo`/`edit.redo` and read the actual groups/placements afterward.
Validation refuses duplicate/stale/mixed-invalid targets atomically. No arbitrary
native VST3 interface control is promised; grouping operates on placements, not
plugin interiors. Grouping/ungrouping are available to contextual user macros;
the broader saved-macro MCP management gap remains inherited.

Save, reopen and export through `vibera_project`. A pending operation is accepted,
not complete: poll `action=job` to terminal completion/failure/cancellation. Verify
the reopened membership and export artifact. A failed save must retain the
working document and previous good project. Real-time projection may still be
pending after a document command; inspect `pendingAudioUpdate` separately.

UI defaults: Playlist Shift+G groups, Alt+G ungroups; the chain glyph toggles
behavior. Ctrl+G is reserved for a separate Pattern merge workflow. Exact FL
regroup/duplicate/resize/slice policies are not claimed from documentation alone.
