# Vocal pitch editor

Note-based pitch editing of a monophonic Audio Clip (one voice or instrument), in the
style of graphical vocal editors. Original implementation: YIN pitch detection at about
12 kHz with a 10 ms curve, notes split where the pitch leaves the note for 30 ms, and
per-note transposition rendered offline with Signalsmith Stretch (length kept).

## MCP (vibera_project)

- `{"action":"vocalPitch","clip":<Audio Clip ID>}` returns `notes` (`note` index,
  `start`/`end` seconds from the clip window start, fractional MIDI `pitch`, `name`,
  `cents` from the nearest semitone), `curveHopSeconds` and `curve` (fractional MIDI
  per frame, null where unvoiced). `editor:true` also opens the graphical editor.
- `{"action":"vocalPitchApply","clip":..,"snap":0..1,"shifts":[{"note":i,"semitones":s}]}`
  renders a new source where each note moves by its shift (snap pulls every other
  note that fraction of the way to its nearest semitone). It is a job: poll
  `{"action":"job","operation":..}` until completed. The clip then plays the new file
  (Edited folder); one Undo restores the previous source. Shifts are -24..24 semitones;
  an unknown note index or no change is refused. Windows up to ten minutes.

## UI

Playlist: right-click an Audio Clip > Edit > Edit vocal pitch (notes)... Drag a note up
or down (semitone steps, Alt for fine), double-click to snap it, Snap to semitones /
Snap 50 % for all (or the selected) notes, Apply renders.

Limits: one voice at a time; timing, vibrato and formant editing are not implemented.
Analysis values are model readings, not listening evidence.
