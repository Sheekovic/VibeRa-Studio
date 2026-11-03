# Piano Roll phrase articulation (application 0.0.46+, skill 0.5.0)

Discover `notes.legato` / `notes.chop` with `vibera_commands`, requesting their
schemas. Use `vibera_inspect entity=notes,pattern=<stable ID>` first. Units are
quarter-note beats, independent of meter. These synchronous document commands
use the same Controller and history as the Piano Roll Menu and shortcuts.

```json
{"command":"notes.legato","args":{"pattern":5,"channel":4,"ids":[10,11,12]}}
```

Use IDs from the current project, never these illustrative numbers. Each selected
note ends at the next selected strictly later onset in its own Channel. Chord
members at the same onset share that endpoint. The last onset group keeps its
lengths. Unselected notes and other Channels are unchanged. This edits note
lengths, not synth portamento/slide or guaranteed seamless plugin articulation.

```json
{"command":"notes.chop","args":{"pattern":5,"channel":4,"ids":[12],"length":0.25}}
```

Chop interval is 0.03125..64 beats, default0.25. Slices start relative to each
note's original onset; the final remainder retains its exact duration. Pitch,
velocity and Channel are preserved; first slice keeps its ID, later slices get
new IDs. Selected results include all new slices in `vibera_state.selectedNotes`.
Both commands accept explicit IDs across editable ghost Channels. Empty/omitted
IDs means the whole selected/explicit Channel part, **not** the UI selection or
all Channels. For a selection workflow, read selection and pass its IDs.

Each successful changed invocation adds one document Undo step. Re-read notes
after completion; verify starts/lengths/IDs and unaffected notes. Test `edit.undo`
and `edit.redo` when needed. No-op legato adds no Undo. Save, poll the actual
project job to completion, reopen and verify, then export and poll the WAV job.
Existing async failure/cancellation limits in setup/workflows still apply.

Unknown IDs, invalid/nonfinite intervals, more than4096 additional slices or a
Pattern total above100000 notes fail atomically with structured MCP errors.
Use a coarser interval/smaller selection rather than retrying unchanged input.
Read the error code/message and current revision before repeating. There is no
worker job for these synchronous edits; musical publication remains incremental.

The GUI uses current snap, or0.25 when snap is None; Ctrl+L/Ctrl+U are Piano-only.
Rack/Mixer Ctrl+L still assigns a Mixer insert. User shortcut profiles may override
defaults. Arbitrary native-plugin widgets and plugin-specific slide behavior are
outside this tool contract. These quick tools do not implement Articulate/Chopper
dialogs, groove templates, strum, note expression or the remaining PR-03 family.

## Composition tools

All take `ids` (empty = the whole selected/explicit Channel part) plus `pattern`/
`channel`, run as one Undo step and are read back with `vibera_inspect notes`:

- `notes.chordify {text: chord, value: inversion}`: every selected note becomes a
  chord root (major, minor, dim, aug, sus2, sus4, maj7, min7, dom7, dim7, m7b5, 6, m6,
  add9, maj9, min9, 9, power). Roots keep their IDs.
- `notes.arpeggiate {text: up|down|updown|random, length: step beats, value: gate, seed}`:
  each chord (same Channel and start) becomes single notes across its longest length.
- `notes.strum {text: up|down, length: beats per note, value: velocity taper}`: chord
  notes start one after another; ends stay.
- `notes.humanize {beat: timing +/-, value: velocity +/-, offset: length fraction, seed}`.
- `notes.scaleSnap {text: scale, pitch: root (pitch class of any MIDI note)}`: nearest
  scale pitch, ties down. hijaz/nahawand/kurd/ajam are 12-tone approximations only.
- `notes.maqam {text: maqam, pitch: tonic}`: real quarter tones (24-TET) for rast,
  bayati, saba, sikah, huzam, hijaz, nahawand, kurd, ajam, nawaAthar, jiharkah. Notes
  become MIDI pitch + `fineCents` (-50 for half-flat degrees). Sikah/Huzam tonic is a
  half flat below the given pitch. The local sampler plays fine cents; MIDI 1 plugin
  instruments (VST3) ignore them.
- Channel tuning: `vibera_command channel.tuning {channel, tuning: [12 cents, C..B]}` (all
  zero = equal temperament); read back as `tuning` in `vibera_inspect channels`. The UI
  offers maqam presets on any tonic and 12-note Scala (.scl) files (Channel menu > Tuning).
