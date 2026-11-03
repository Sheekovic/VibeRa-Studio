# Loop control and live extents (0.0.57)

Discover the connected app's typed contracts before editing. Target stable
Pattern/placement IDs and use projectIdentity/revision guards. Time is zero-based
quarter-note beats. Pattern/Song mode changes retain existing defaults: Pattern
loops, Song does not. Mode changes currently stop transport; do not promise a
seamless mode switch. The new Loop button is beside Pattern/Song.

`vibera_transport {"action":"loop","enabled":true}` idempotently enables looping
of the entire current Pattern or Song. False disables. The canonical
`transport.loop` command toggles the same application action and can be bound to
a shortcut/controller/macro. Loop replies explicitly complete transport configuration; applied playhead readback
is separate from audible output. Loop is transient transport state, not document Undo
or a saved region. Re-read and set it explicitly after mode/project changes.
Changing it requires prepared project audio and no active MIDI take; a busy
failure preserves the take and held input. Do not retry blindly or cancel a take
without permission. Selected-region loops and FL advanced per-Channel looping
remain unsupported, not substitutes for this whole-range control.

Use `pattern.length` with `pattern=id,length=<positive beats>` for manual length.
The model accepts positive values up to1e6; the legacy engine cycle floors at0.25
beats. Source notes beyond a shorter Pattern are retained. Existing note edits
can grow a Pattern to the next bar; this is not a complete FL Auto selector.
Structural length edits retain the existing MIDI-take finalization policy; live
retiming of an uninterrupted take remains open. Do not claim all recording edits
are noninterrupting.

For Song, move/resize actual Playlist placements through `clip.move` (length is
a signed beat delta) or its typed audio/automation equivalents. Song end follows
the last placement's extent; changing a Pattern alone does not resize linked
placements. Whole-Song Loop uses that arrangement extent. `edit.undo`/`edit.redo`
restore model and requested bounds; Loop toggles do not enter document history.

`vibera_state.audio.loopBounds` exposes `enabled`, `startBeat`, `endBeat`, plus
`appliedAvailable`, `appliedEnabled`, `appliedStartBeat`, `appliedEndBeat`.
Applied values are null without an active audio context, including the normal
owned --mcp-test no-device fixture. They are not hearing/hardware evidence.
Wait for pendingAudioUpdate to clear; inspect both model and available applied
state before interpreting success. GUI/MCP changes use the same control-thread
publisher. No callback waits, allocations or UI work were added. The upstream
engine still has sequence-lock state; this is not an entire-engine wait-free claim.

Growing a loop preserves its current phase. Shrinking while the cursor remains
inside preserves it; if it is outside, the new loop resumes at its start.
This is an explicit VibeRa/pinned-engine policy, not observed FL runtime parity.

Save/open/export use `vibera_project` jobs: accepted/pending is not completed.
Poll the operation until completed/failed/cancelled and inspect structured errors.
An enabled live Loop does not cause endless export: export writes one complete
Song. Verify the resulting file's duration and musical events. Recover through
readback after timeouts, not blind repeated mutations. Physical listening/MIDI
and full FL looping/recording familiarity remain separate acceptance work.
