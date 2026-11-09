# Expression maps (articulations and keyswitches)

A Channel's expression map lists articulations (name + keyswitch note) for orchestral or
articulation-switching instruments. A note with an articulation makes playback send that
keyswitch as a short note (1/64 beat) at the same beat just before it; a chord sends it
once. Notes without an articulation send nothing. Project schema 57; one Undo each.

## Commands (vibera_command)

- `channel.articulations`: `channel`, `text` with `Name=keyswitch` entries separated by
  `;` or new lines. Keyswitch is a MIDI number or a note name in the Piano roll
  convention (C5 = 60, C0 = 0, sharps `#`, flats `b`), a control change `CCn:v`
  (e.g. `Pizz=CC32:64`) or a program change `PCn` (e.g. `Trem=PC5`). CC and program
  articulations are sent to the instrument 0.1 ms before their notes instead of a note. Empty text clears the map. At
  most 64 entries, names up to 32 characters.
- `notes.articulation`: `pattern`, `channel`, `ids` (notes; empty means the Channel's
  whole part), `value` = 1-based entry of the note's Channel map, 0 for none. A number
  beyond the map is refused.

## Readback

- `vibera_inspect channels`: `articulations` [{articulation, name, keyswitch, kind note/cc/program, value, trigger}].
- `vibera_inspect {"entity":"scheduledControls","pattern":id}`: MIDI control messages Pattern
  playback sends (status, data1, data2, seconds), including CC / program articulations.
- `vibera_inspect notes`: `articulation` per note.
- `vibera_inspect {"entity":"scheduledNotes","pattern":id}`: what Pattern playback sends,
  in order; keyswitches have `keyswitch:true` and `note:0`.

UI: Piano roll Menu > Articulation (expression map): edit the map, or choose the
articulation of the selected notes; note labels show the articulation name.
Keyswitches reach any instrument, so set maps only on Channels whose instrument uses them.
