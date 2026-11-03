# Glue a Piano Roll phrase (application0.0.47+, skill0.6.0)

Discover `notes.glue` through `vibera_commands includeSchemas=true`. Read actual
Pattern/Channel/note IDs with `vibera_state` and paginated `vibera_inspect`.
Times are zero-based quarter-note beats, independent of meter. Empty `ids` means
all notes of the selected/explicit Channel part; it does not consume the current
UI selection. For that workflow, read `vibera_selection action=state,target=notes`
and pass its IDs (existing explicit command request limit1024 IDs).

```json
{"command":"notes.glue","args":{"pattern":5,"channel":4,"ids":[10,11,12]}}
```

Use IDs from the connected project, never these example numbers. Glue joins
touching/overlapping notes independently per Channel/pitch. Real gaps remain.
Earliest onset survives; lowest ID resolves simultaneous ties. It keeps its ID,
pitch and velocity and covers the combined span. Joined later IDs disappear.
Editable ghost IDs can target several Channel parts independently. The UI Menu
and contextual Ctrl+G use the same canonical Controller command.

Read notes and selection again after success: explicit-target selection contains
survivors, and `vibera_inspect` establishes the actual result. Glue is synchronous,
not an accepted worker job. It adds one Undo when changed; repeated no-op adds
none. Missing/deleted IDs, Pattern or all-part Channel produce structured atomic
errors. Read fresh state instead of retrying stale IDs. `edit.undo` restores the
original notes/IDs/order, and `edit.redo` restores the merge. Selection/navigation
are not document Undo.

To reverse Quick chop: inspect slices, Glue their IDs, check original onset/end,
pitch/velocity, then save with `vibera_project action=save`. Poll its actual job
to terminal completion; reopen and compare, then export WAV and poll. Linked
Playlist clips keep the same Pattern; Glue does not Make Unique.

Tolerance absorbs only double round-off (eight epsilon units scaled by beat
magnitude), never snap-sized gaps. Different velocities collapse to the leftmost
value: Glue joins a held note rather than preserving retrigger/accent events.
Different pitches/Channels never merge. It does not provide plugin glide or
arbitrary native-editor control.

The FL-style click-and-hold boundary gesture/general gesture preference is not
implemented by this increment. Use Menu/Ctrl+G or the semantic command; pointer
simulation is not an MCP substitute. Full Piano Roll parity remains incomplete.
