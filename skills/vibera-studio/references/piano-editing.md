# Piano roll editing: clipboard, flip, invert

- **Copy / cut / paste** (Ctrl+C / Ctrl+X / Ctrl+V): the selection, or the whole Channel part
  without one. In Pattern mode the earliest copied note lands at the playhead (snapped); in
  Song mode notes paste at their copied positions. Pasted notes go to the current Channel and
  become the selection. MCP: `notes.paste` with `args.pattern`, `args.channel`, `args.ids`
  (notes to copy from that Pattern) and `args.beat`. One Undo step.
- **Flip** (`notes.flip`, `args.text` = `horizontal` | `vertical`; Piano roll menu): mirrors
  the selection or Channel part in time within its span, or in pitch around its range.
- **Invert selection** (`selection.invert`, Shift+I in the Piano roll and Playlist).
- **Chord stamp** (Piano roll menu > Chord stamp; MCP `vibera_editor`
  `{"editor":1,"action":"stamp","chord":"minor"}`, `""` turns it off, read `state.pianoStamp`):
  drawing a note places that chord with the clicked note as its root, as one Undo step.
  Shapes are the Chordify list. Without the UI, use `notes.add` then `notes.chordify`.
- **Paste position:** Ctrl+V pastes from the start of the leftmost visible bar.
- **Edit tools** (selection, or the Channel part): `notes.rotate` (`args.beat` step; Shift+Ctrl+
  Left/Right wraps notes within their bars), `notes.discardLengths` (`args.length`; Shift+D uses
  the snap), `notes.limit` (`args.pitch` low, `args.value` high, at least an octave: folds notes
  by octaves into the range), `notes.flam` (`args.length` offset, `args.value` velocity factor:
  adds a softer grace note before each note). Alt+M / Alt+Shift+M mute / unmute.
- **Select:** Shift+R one more at random, Shift+O odd (off-beat) steps, and menu Select muted,
  overlapping (all but the lowest), stacked (exact duplicates).
- **Insert / delete space** (Ctrl+Ins, Ctrl+Alt+Ins slices crossing notes, Ctrl+Del; uses the
  Piano roll ruler selection): `notes.insertSpace` / `notes.deleteSpace` with `args.beat`,
  `args.length` (and `args.enabled` to slice on insert) act on the Channel's notes and
  controller events in the Pattern.
