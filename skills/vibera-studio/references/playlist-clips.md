# Playlist clips: repeat, split, merge and the ruler

- **Repeat:** a Pattern clip longer than its Pattern repeats the Pattern until the clip
  ends (notes, controllers and Pattern automation). A clip inside its Pattern plays its
  window once. Resize with `clip.move` as usual; read occurrences with `vibera_inspect`.
- **Split by channel** (`clip.splitChannels`, `args.target` = Pattern clip): one clip per
  Channel with notes, each a new Pattern, on the clip's track and the ones below, same
  position and window. The source Pattern stays for its other clips. One Undo.
- **Merge** (`clip.merge`, `args.ids` = two or more Pattern clips; Ctrl+G with a clip
  selection): one new Pattern holding what plays in those clips (Channel loops, repeats and
  windows become plain notes and controllers), placed once from the earliest clip on its
  track; the merged clips are removed. One Undo.
- **Ruler:** left-drag scrubs the playhead; Ctrl+drag or double-click starts a time
  selection; dragging an edge of the selection resizes it; right-click offers Play from
  here, Add marker here, Select this bar, Select to song end, Clear time selection.
  Read `state.playlistRange` and `editors[2].timeline` (ruler band, grid left, pixels per
  beat, scroll) for pointer tests.
- **Copy / cut / paste** (Ctrl+C / Ctrl+X / Ctrl+V): pasted clips keep their tracks and
  spacing; the earliest lands at the playhead (snapped). MCP: `clip.paste` with `args.ids`
  (clips to copy) and `args.beat`.
- **Insert space** (`time.insert`, Ctrl+Insert; Ctrl+Alt+Insert also slices crossing clips,
  `args.enabled`) and **Delete space** (`time.delete`, Ctrl+Delete) use the ruler time
  selection (`args.beat`, `args.length`). Clips, markers, tempo and time-signature changes
  after it move; Delete cuts crossing clips at both edges and removes what is inside.
- **Chop** (`clip.chop`, `args.length` = piece in beats; clip menu > Chop into) cuts the
  selected clips on the Song grid. **Quick quantize** (`clip.quantize`, Ctrl+Q,
  `args.length` = grid) snaps clip starts, keeping lengths. Clip menu > Select all similar
  clips selects clips of the same Pattern, audio source or automation source. One Undo each.
- **Trim start** (`clip.trimStart`, `args.target`, `args.beat` = new start): dragging a
  Pattern clip's left edge moves its start and source window while the end stays; it cannot
  start before the Pattern start. Clip edges show a resize cursor. Clip menu > Rename /
  colour Pattern opens the Pattern properties.
- **Render to audio clip** (`clip.render`, a UI action over the current clip selection; clip
  menu > Render to audio clip): the selected Pattern and audio clips render together (their
  windows, repeats, mixer and the tempo in force at their start) into `Rendered/` beside the
  project, the WAV is placed at their start on the first free track and the originals are
  muted. Poll `vibera_state` until the audio clip appears.
- **Auto-scroll** (`playlist.follow` toggles; Playlist menu > Auto-scroll with the playhead;
  on by default, read `state.playlistFollowPlayhead`): during Song playback the Playlist
  page-scrolls when the playhead leaves the view, never during a pointer gesture.
- **Track height:** drag the bottom edge of a track header (or `track.height`, `args.lane`,
  `args.value` .5..3). `state.playlistTracks` lists tracks with non-default settings (name,
  colour, mute, lock, group, height, instrument Channel, audio).
- **Snap:** the Playlist offers None, 1/16, 1/8, 1/6 T, 1/4 (step), 1/3 T, 1/2, Beat, Bar and
  2 bars and Line (pointer edits follow the visible grid lines, finer when zoomed in;
  `{"editor":2,"action":"snap","line":true}`, read `state.snapLine`) and Markers (a moved clip's start, a resized end or a placed clip lands
  on the nearest marker or the Song start, bars without markers; `"markers":true`, read
  `state.snapMarkers`); Alt bypasses snap while dragging. Set it with `vibera_editor`
  `{"editor":2,"action":"snap","snap":<beats>}` (shared with the Piano roll) and read
  `state.snap`.
