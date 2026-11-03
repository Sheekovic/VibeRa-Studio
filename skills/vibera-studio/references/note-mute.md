# Note mute and score export

Discover `notes.mute`, `notes.unmute`, `piano.toolMute` and `piano.exportMidi`.
Read the connected application's schemas; registration is not qualification.

1. Read `vibera_state` for projectIdentity, selectedPattern/selectedChannel and
   revision. Page `vibera_inspect entity=notes pattern=<id>` for stable note IDs,
   source properties and Boolean `muted`. Never infer mute from velocity zero.
2. Call `vibera_command command=notes.mute args={pattern,channel,ids:[...]}`.
   Omitted/empty IDs target the whole active Channel part, not all ghost notes.
   Explicit IDs may include editable ghost parts within that Pattern. Unknown,
   duplicate or malformed IDs fail atomically; unrecognized properties fail.
   No time/pitch/velocity change is implied. Repeating the desired state is inert.
3. For a stroke, `vibera_gesture action=begin domain=noteMute leaseMs=10000`.
   Retain its operation token; update with additive canonical notes.mute/unmute
   commands and explicit target IDs. Updates are pending, commit is one Undo,
   cancel/expiry restores the exact baseline. Remain in the original Pattern and
   Channel context. This specialized lease preserves an existing MIDI take;
   arbitrary document commands are refused in it. Renew before lease expiry.
4. Inspect actual flags and pendingAudioUpdate after edits. Playback publication
   is separate from document completion and does not establish heard sound.
   Linked Playlist placements play the same muted Pattern source. Undo/Redo use
   `edit.undo`/`edit.redo`; undo during a take follows existing take-finalization.
5. Save with `vibera_project action=save path=<absolute .vibra>`; poll `action=job`
   until completed/failed. Reopen and inspect IDs/flags to verify persistence.
   Existing files require overwrite=true; dirty replacement requires explicit
   discardChanges. Save and export do not create musical Undo steps.
6. `vibera_project action=exportMidi path=<absolute .mid or .midi>` snapshots the
   selected Pattern's selected Channel part, tempo and meter at admission. It
   writes 960 PPQ and 7-bit velocities; projectIdentity/sourceRevision in the job
   identify its snapshot. Muted notes are omitted because SMF has no equivalent
   flag; all-muted parts yield valid silent SMF, truly empty parts fail the job.
   WAV export remains Song/48kHz/24bit/cut-tail. File jobs are pending until real
   worker completion, including filesystem failure. Inspect fileExists/fileBytes
   and musical file contents. No cancellation endpoint is advertised for these
   admitted file jobs. Closing a UI file chooser before admission creates no job.

`piano.toolMute` is a contextual navigation action (Piano scope2), not a document
edit. Actual UI defaults: T mute tool; P draw, B paint; click toggles the first
note's desired state and drag paints that state across newly touched notes;
Ctrl selects, right-click erases. Tool/selection state is not musical Undo.
Original mouse policies are independently implemented; exact FL pointer/runtime
parity and human familiarity are unqualified. Arbitrary plugin UI is not exposed.
