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
