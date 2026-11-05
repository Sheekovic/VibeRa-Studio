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
- **Resizing from the left** (`piano.resizeLeft` toggles, Ctrl+Alt+Home; Piano roll menu > Edit):
  dragging a note's start moves it while the end stays. Note edges show a resize cursor.
- **Select time around selection** (`selection.timeAround`, Ctrl+Enter) sets the ruler time
  selection to the selected notes' span. `editors[1].timeline` also reports `topPitch` and
  `rowHeight` (row y = rulerBottom + (topPitch - pitch) * rowHeight).
- **Paint drum sequencer** (`tool.drumPaint`, N): paints notes the length of the snap; clicking
  an existing note mutes / unmutes it instead of moving it. P (Draw) and B (Paint) leave it.
- **Randomize** (`notes.randomize`: `args.text` scale name from the scale list, `args.pitch`
  root/lowest note, `args.length` step, `args.value` density 0..1, `args.seed`): replaces the
  Channel part with random in-scale notes over the Pattern (one Undo). Menu > Randomize uses
  the Piano roll scale guide, or C major.
