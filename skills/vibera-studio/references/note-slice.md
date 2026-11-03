# Manual note slicing

Requires0.0.61; discover the connected app rather than trusting a version label.
`notes.slice` and `piano.toolSlice` are stable registered operations. `vibera_slice`
shares the actual UI preview/session/commit service; it never simulates a pointer.

1. Inspect projectIdentity, revision, selectedPattern/selectedChannel and paginated
   source notes. Select an existing playable Channel and Pattern. Scope2 is Piano;
   editor index1 is its window. `vibera_command command=piano.toolSlice` selects
   the visible C tool; semantic editing does not require a visible editor.
2. Begin `vibera_slice action=begin leaseMs=10000` and retain its operation token.
   Do not take over a local UI preview. All beats below are quarter notes in the
   source Pattern, not Song position. Pitches are continuous MIDI semitones: an
   integer is a row centre, edges are +/-0.5. Example update:

   ```json
   {"action":"update","operation":"<returned token>","slice":{"startBeat":0.75,"endBeat":1.25,"startPitch":59.5,"endPitch":64.5,"grid":0.25,"allChannels":false}}
   ```

   `grid=0` bypasses snapping; nonzero valid range is1/960..64 beats. Endpoint
   beats are0..1000000 and pitches-0.5..127.5. A reversed line is valid. Empty
   IDs process the active Channel part; allChannels=true explicitly includes
   ghosts. Explicit IDs bound targets regardless of that flag. The UI knife
   processes editable line intersections independently of current selection.
3. Updates are pending and nondestructive: score, revision, dirty state, history
   and audio projection remain unchanged. Inspect planned left IDs/beat; right=0
   means not created yet. State supports offset/limit pagination (max512), count
   and nextOffset. Failed input leaves the prior valid preview intact. Refresh
   before the idle lease expires; expiry cancels without committing.
4. `action=commit operation=<token>` applies canonical notes.slice once and
   returns completed with actual left/right IDs. Read source notes and
   pendingAudioUpdate separately: completed is a document edit, not a claim that
   a device has played it. `cancel` leaves source/history untouched. Unknown,
   stale or foreign ownership tokens fail. Inapplicable action parameters fail;
   do not send update fields to begin/commit. Local edits, tool/context changes,
   hiding the Piano Roll and project replacement can cancel a preview; poll its
   actual terminal state. Never retry a commit blindly after losing the response.
5. Each retained left note keeps its ID and selection; fresh right halves are
   unselected. Pitch, velocity and mute flags remain. Sliced step notes become
   score notes. Pattern/Playlist extent stays unchanged; linked placements share
   their source. Right halves retrigger instruments: slicing score is not seamless
   audio-file chopping. Cuts at edges or yielding less than1/960 beat are no-ops;
   repeating a cut has count0 and no phantom Undo. Horizontal/point lines use the
   start beat in their row; vertical/diagonal intersections use row-centred time,
   clipped to the line's overlap with the row. Corner-only touches do not cut.
6. Verify one `edit.undo` restores the exact source, `edit.redo` restores IDs, then
   save/reopen and inspect again. WAV/SMF export uses real asynchronous project
   jobs; poll completed/failed and inspect actual files. Muted halves remain in
   the project and are omitted from standard MIDI. Reselect the source part after
   reopening before exportMidi; workspace selection may reset.

For a one-shot edit, call `vibera_command command=notes.slice args={pattern,channel,
slice,ids}`. For a macro-like stopped-recording workflow use `vibera_batch` with
Slice followed by canonical note edits: shared validation and one Undo, with
atomic rollback on failure. The UI macro builder creates exact unsnapped vertical
cuts from its numeric amount; recording note actions retains the complete line.
Profile3 preserves geometry, rejects modern data under old labels and migrates
real older profiles/bindings. Broad remote saved-macro/shortcut management remains
an inherited coverage gap; do not claim the batch API manages those definitions.

UI: C tool, drag preview/release commit, Shift click pointed row, Shift+Alt bypass
snap, Ctrl selection, right erase, Escape cancel, P/B Draw/Paint. Musical start
anchor stays fixed while scrolling during a drag. These are original implemented
policies guided by the public manual; exact installed FL boundary/selection/short
note semantics and human familiarity remain unqualified. No arbitrary native
plugin UI control, listening or hardware claims follow from these operations.
