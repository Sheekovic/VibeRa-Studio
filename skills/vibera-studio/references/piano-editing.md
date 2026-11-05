# Piano roll editing: clipboard, flip, invert

- **Copy / cut / paste** (Ctrl+C / Ctrl+X / Ctrl+V): the selection, or the whole Channel part
  without one. In Pattern mode the earliest copied note lands at the playhead (snapped); in
  Song mode notes paste at their copied positions. Pasted notes go to the current Channel and
  become the selection. MCP: `notes.paste` with `args.pattern`, `args.channel`, `args.ids`
  (notes to copy from that Pattern) and `args.beat`. One Undo step.
- **Flip** (`notes.flip`, `args.text` = `horizontal` | `vertical`; Piano roll menu): mirrors
  the selection or Channel part in time within its span, or in pitch around its range.
- **Invert selection** (`selection.invert`, Shift+I in the Piano roll and Playlist).
