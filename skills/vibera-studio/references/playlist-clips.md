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
- **Vertical zoom:** Alt+wheel over the Playlist scales every track's height
  (`editors[2].timeline.laneHeight`); double-click a track name to rename it.
  `vibera_pointer` phase `wheel` (with `deltaY`, `alt`/`ctrl`/`shift`) runs one wheel step.
- **Keys:** Shift+Up/Down move the selected clips one track (`clip.trackUp`/`clip.trackDown`);
  Home / End put the playhead at the start / the end of the Song (Pattern end in Pattern mode)
  (`transport.home`/`transport.end`).
- **Track header:** Shift+wheel moves the track up/down; Alt+click its bottom divider resets
  its height; the right-click menu adds Clips on this track (select all, mute/unmute all,
  merge Pattern clips, consolidate to audio), Auto name (`track.autoName`), Reset name and
  colour (`track.reset`) and Clone track (`track.clone`: the track and copies of its clips
  right below). Alt+T adds a time marker at the playhead (`marker.addAtPlayhead`).
- **Stretch mode** (`playlist.stretchMode`, Shift+M, Playlist menu; `state.audioStretchMode`):
  dragging an audio clip's right edge previews the ratio and, on release, stretches the clip
  to that length keeping pitch (a new file in `Stretched/`, one Undo step when it is ready).
  With the mode off the right edge resizes (trims) as before.
- **Track LED:** click mutes, Ctrl+click solos, Shift+click locks, Alt+click mutes the whole
  track group (one Undo). Track menu > Auto color group gives the group this track's colour.
- **Merge with** (Arrangements menu; `arrangement.merge`, `args.target` stored arrangement,
  `args.beat` position, `args.enabled` insert = move later material right first): adds copies
  of that arrangement's clips and markers to the open one (the source stays). One Undo.
- **Play selected** (`tool.play`, Y in the Playlist and Piano roll): in the Playlist, holding the
  mouse plays from that point and releasing stops; in the Piano roll, holding a note (or a
  row) sounds it. P / B / other tools leave it.
- **Clip focus:** the picker type (Pat / Audio / Auto; `vibera_picker` `type`) is drawn on top of
  stacked clips and is the type a click picks where clips overlap.
- **Track selector:** click a track name to select it (Ctrl adds); Ctrl+Up/Down moves the
  selection, Ctrl+Alt+Up/Down extends it (`track.selectPrevious/Next`, `track.extendPrevious/Next`);
  Shift+Ctrl+B clones the selected tracks with their clips (`track.cloneSelected`, one Undo).
  Read `state.selectedTracks`.
- Audio Clip pitch: `vibera_project` `editClip` with `edit: "pitch"` and `value` semitones
  (-24..24, nonzero) renders a pitch-shifted copy of the clip window at the same length and
  points the clip at it (job, one Undo). Keys on the selected Audio Clip: 7 pitch down, 8 pitch
  up, 9 reverse (actions `clip.pitchDown`/`clip.pitchUp`/`clip.reverse`, first selected clip,
  one offline job at a time); also the clip menu > Edit sample.
- Merge similar Pattern clips: `clip.mergeSimilar` (`ids`: the selected stack, two or more
  Pattern clips; Shift+Ctrl+G; clip menu) merges them like `clip.merge` into one new Pattern
  ("Merged xN") and replaces every other repeat of the same stack (same Patterns, tracks,
  windows, mute and spacing, any time) with a clip of it. One Undo.
- Markers in bulk (Playlist menu > Add markers): `marker.addEvery` (`value` 1..64 bars) adds a
  marker named "Bar N" on every Nth bar up to the Song end, following time-signature changes and
  skipping positions that already have one; `marker.addPair` (`beat`, `length`) marks the start
  and end of a time selection. One Undo step each; refuses when nothing would be added.
- Export all playlist tracks (Playlist menu; `vibera_project {"action":"exportTracks","path":
  <absolute folder>}`): one WAV per Playlist track that has Pattern or Audio clips (lanes 64+
  of Performance mode excluded), named "NN Track name.wav", each rendered from the Song start
  with only that track's clips while automation and tempo stay, so the files line up. Runs one
  export after another; the job completes after the last file.
- Export all playlist track groups (Playlist menu; `vibera_project` `exportTrackGroups` with a
  folder `path`): one WAV per group of two or more tracks (a first track plus the tracks
  "Grouped with the track above") that has clips, named after the group's first track
  ("Group N" when unnamed), with the same Song-start alignment as the track export.
