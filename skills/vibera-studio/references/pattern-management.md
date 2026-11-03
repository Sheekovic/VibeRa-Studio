# Bulk Pattern management and Pattern render

All operations are canonical `vibera_command` edits with one Undo step each.

- `pattern.cloneSelected` with `ids`: clones every listed Pattern; each independent
  copy (new Pattern and note IDs, name + " copy") is inserted right after its source.
  `pattern.clone` keeps its single `pattern` behavior.
- `pattern.deleteSelected` with `ids`: deletes the listed Patterns, all their
  Playlist placements, and their clip-group memberships (groups under two members
  dissolve). `pattern.delete` keeps its single `pattern` behavior.
- `pattern.deleteEmpty`: deletes Patterns with no notes, no Channel loops and no
  Playlist placement. `pattern.deleteUnplaced`: deletes every Pattern with no
  Playlist placement, including its notes. Both fail with `patternPurge.none`
  when nothing matches. At least one Pattern always remains.

`ids` must be 1..1024 unique existing Pattern IDs (`patternBulk.invalidSelection`).

Pattern-only WAV: `vibera_project` `{"action":"export","path":"C:/out/beat.wav","pattern":5}`
renders that Pattern once from beat 0 at 48 kHz/24-bit, without Playlist audio or
automation clips. Poll the returned job like a Song export. The UI has the same
choices in the Pattern picker's right-click menu.

Render to audio clip: `vibera_project` `{"action":"export","pattern":5,"placeClip":true}`
(no path) renders the Pattern into the project's `Rendered` folder
(Documents/VibeRa Studio/Rendered when unsaved, unique file name), then imports it
as a new Audio Clip Channel placed at the Pattern's first Playlist position on the
first lane below all clips. The returned job `path` is the WAV. The import happens
after the job completes and is its own Undo step; poll `vibera_state` channels or
`vibera_inspect` clips to confirm. UI: Pattern picker > Render Pattern to audio clip.
