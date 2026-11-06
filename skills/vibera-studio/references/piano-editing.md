# Piano roll editing: clipboard, flip, invert

- **Copy / cut / paste** (Ctrl+C / Ctrl+X / Ctrl+V): the selection, or the whole Channel part
  without one. In Pattern mode the earliest copied note lands at the playhead (snapped); in
  Song mode notes paste at their copied positions. Pasted notes go to the current Channel and
  become the selection. MCP: `notes.paste` with `args.pattern`, `args.channel`, `args.ids`
  (notes to copy from that Pattern) and `args.beat`. One Undo step.
- **Flip** (`notes.flip`, `args.text` = `horizontal` | `vertical`; Piano roll menu): mirrors
  the selection or Channel part in time within its span, or in pitch around its range.
- **Invert selection** (`selection.invert`, Shift+I in the Piano roll and Playlist).
- **Chord stamp** (Piano roll menu > Chord stamp; MCP `vibera_editor`
  `{"editor":1,"action":"stamp","chord":"minor"}`, `""` turns it off, read `state.pianoStamp`):
  drawing a note places that chord with the clicked note as its root, as one Undo step.
  Shapes are the Chordify list. Without the UI, use `notes.add` then `notes.chordify`.
- **Paste position:** Ctrl+V pastes from the start of the leftmost visible bar.
- **Edit tools** (selection, or the Channel part): `notes.rotate` (`args.beat` step; Shift+Ctrl+
  Left/Right wraps notes within their bars), `notes.discardLengths` (`args.length`; Shift+D uses
  the snap), `notes.limit` (`args.pitch` low, `args.value` high, at least an octave: folds notes
  by octaves into the range), `notes.flam` (`args.length` offset, `args.value` velocity factor:
  adds a softer grace note before each note). Alt+M / Alt+Shift+M mute / unmute.
- **Select:** Shift+R one more at random, Shift+O odd (off-beat) steps, and menu Select muted,
  overlapping (all but the lowest), stacked (exact duplicates).
- **Insert / delete space** (Ctrl+Ins, Ctrl+Alt+Ins slices crossing notes, Ctrl+Del; uses the
  Piano roll ruler selection): `notes.insertSpace` / `notes.deleteSpace` with `args.beat`,
  `args.length` (and `args.enabled` to slice on insert) act on the Channel's notes and
  controller events in the Pattern.
- **Resizing from the left** (`piano.resizeLeft` toggles, Ctrl+Alt+Home; Piano roll menu > Edit):
  dragging a note's start moves it while the end stays. Note edges show a resize cursor.
- **Select time around selection** (`selection.timeAround`, Ctrl+Enter) sets the ruler time
  selection to the selected notes' span. `editors[1].timeline` also reports `topPitch` and
  `rowHeight` (row y = rulerBottom + (topPitch - pitch) * rowHeight).
- **Paint drum sequencer** (`tool.drumPaint`, N): paints notes the length of the snap; clicking
  an existing note mutes / unmutes it instead of moving it. P (Draw) and B (Paint) leave it.
- **Randomize** (`notes.randomize`: `args.text` scale name from the scale list, `args.pitch`
  root/lowest note, `args.length` step, `args.value` density 0..1, `args.seed`): replaces the
  Channel part with random in-scale notes over the Pattern (one Undo). Menu > Randomize uses
  the Piano roll scale guide, or C major.
- **Velocity lane:** drag paints the velocity of each note under the pointer; Shift+drag gives
  every crossed note the starting level; Alt+click a note's bar resets it to 0.78 (Alt+click a
  controller point still edits that point). One Undo per drag. `editors[1].timeline` reports
  `velocityBase` / `velocityScale` and `timeSelection`.
- **Time selection:** Ctrl+Left / Ctrl+Right (`selection.timePrevious` / `selection.timeNext`)
  move it by its own width.
- **Articulate** (`notes.articulate`, `args.value` 0.05..1): each note lasts that share of the gap
  to the next note start (staccato 25%, portato 50%, tenuto 90%; Quick legato is 100%).
- **Scale levels** (`notes.scaleLevels`): velocity x `args.value` (0..4) + `args.offset`
  (-1..1), inverted first when `args.enabled`. Menu > Edit > Scale levels.
- **LFO tool** (`notes.controller.lfo`: `args.text` cc / pitchBend / pressure, `args.pitch` CC
  number, `args.target` shape 0 sine / 1 triangle / 2 square / 3 saw, `args.length` period
  beats 1/4..64, `args.value` depth 0..1, `args.offset` centre): replaces the Channel's events
  of that lane over the Pattern with one point per 1/16 beat (one Undo). Menu > LFO tool writes
  CC 1 (mod wheel).
- **Claw machine** (`notes.claw`: `args.length` step 1/32..4 beats, `args.text` rhythm of `x`
  keep / `.` drop, repeating): cuts each note into steps and keeps the rhythm's steps.
- **Riff machine** (`notes.riff`: `args.text` scale, `args.pitch` root, `args.length` grid,
  `args.value` density, `args.seed`): replaces the Channel part with a riff that repeats a
  one-bar rhythm, starts each bar on the root and moves mostly by scale steps.
- **Note grid highlights** (`view.noteGridHighlights`, Alt+B): toggles the black-key row shading
  of the grid (on by default); off shows even rows with thin lines. Read `state.pianoGridHighlights`.
- **Key labels** (right-click a Piano roll key; `channel.keyLabel`, `args.channel`, `args.pitch`,
  `args.text` at most 32 characters, empty removes): names keys for the Channel (drum maps),
  drawn on the keyboard and saved with the project. Read `state.keyLabels` (selected Channel).
- **MIDI clipboard** (Piano roll menu; `notes.copyMidiClipboard`, `notes.pasteMidiClipboard`
  Shift+Ctrl+V): copy exports the Channel part to a .mid file and puts that file on the system
  clipboard (Windows file list) for other apps; paste imports the first .mid/.midi file on the
  clipboard into the current Channel (replacing its part, like MIDI import). Windows only.
- **Export as score sheet (PDF)** (Piano roll menu; `notes.exportScore` opens a save chooser):
  the selected Channel's unmuted notes on a grand staff (treble from middle C up), four bars
  per system, sharps, ledger lines and note values from the lengths (whole, half, quarter,
  eighth, sixteenth). A readable lead sheet, not engraved notation: no beams, rests, ties or
  key signatures.
- Test mode: `vibera_dialog` (`state`, `submit` with `fields` and `button`) reads and answers the
  open modal dialog in-process (e.g. Rename key: field `label`, button `OK`); it refuses outside
  test mode and never sends OS input.
- Trim selection: `notes.trim` (`pattern`, `channel`, `beat`, `length`; one Undo) keeps only
  `[beat, beat+length)` of the Channel part: notes outside are removed, notes crossing an edge
  are cut at it, controller events outside are removed and nothing moves. UI: Piano roll menu >
  Edit > Trim selection (needs a ruler time selection; action `notes.trimAction`), next to
  Insert space / Slice & insert space / Delete space.
- Note groups: `notes.group` (`pattern`, `channel`, `ids` of two or more notes; Shift+G) gives
  them a new group; `notes.ungroup` (Alt+G) clears it. Clicking one grouped note selects the
  whole group; duplicated or pasted grouped notes form new groups. Saved per note as `group`;
  read back as `noteGroup` (0 = none) in `vibera_inspect` notes. Piano roll menu > Edit.
- Event editor target: `vibera_editor` `{editor:1, action:"laneTarget", target}` (velocity,
  release, pan, modX, modY, fine; UI: click the lane name under the keyboard) chooses what the
  lane under the notes shows and paints; read back as `state.pianoLaneTarget`. `notes.velocity`
  takes `text` = that target and `value` = the 0..1 lane level (pan/fine centred at .5:
  pan -1..1, fine -100..100 cents). Pan, Mod X/Y and Fine pitch are refused for plugin
  instrument notes (`noteLane.unsupportedTarget`). Controller dots show on the Velocity target only.
- Turn into automation clip: `notes.toAutomation` (`pattern`, `channel`, `text` cc / pitchBend /
  pressure, `pitch` CC number, `beat` Playlist start, `lane` 0..127; one Undo) turns the
  Channel's controller lane in that Pattern into an unlinked automation clip (stepped values,
  0..1; bend -1..1 maps to 0..1). Link it with `automation.link` or a control's automation menu
  ("Link to existing source"). UI: Piano roll menu > Edit > Turn into automation clip (one item
  per lane with events; placed at the Pattern's first clip on the first free lane).
- Insert current controller value: action `notes.insertControllerValue` (Ctrl+I) adds an event
  at the playhead (Pattern time, 1/960 beat) in the lane of the Channel's most recently added
  event (pitch bend when none), holding the lane's value there (previous event, else the
  Channel's pitch bend or 0). Piano roll menu > Edit > Insert current controller value picks
  the lane (CC with events, Pitch bend, Pressure).
- Scripts (Piano roll menu > Scripts; familiar Tools > Scripting): `notes.script` (`pattern`,
  `channel`, `text` = JavaScript, `ids` = selection, `length` = snap; one Undo) runs the script
  on that Channel's part with JUCE's QuickJS engine (2 s limit, no file/network/project access).
  The script sees `notes` (objects with id, pitch, beat, length, velocity, pan, release, muted,
  selected), `patternLength`, `snap`, `selected(n)` (true for every note when none is selected),
  `addNote(pitch, beat, length, velocity)` and `clamp`; the final `notes` replaces the part
  (notes keeping their `id` keep fine pitch, mod X/Y, slide, colour...). Values are validated
  (pitch 0..127, velocity/release 0..1, pan -1..1, beat/length >= 0) before anything changes.
  Six original examples are in the menu; "Run script file (.js)..." opens
  Documents/VibeRa Studio/Scripts; `notes.scriptAgain` (Ctrl+Alt+Y) repeats the last script.
- Keyboard labels (Piano roll menu > Keyboard labels): actions `view.keyLabelsNone`,
  `view.keyLabelsRoot` (default, the Cs) and `view.keyLabelsWhite` (every white key); read back
  as `state.pianoKeyboardLabels` none/root/white. Custom key names (right-click a key) still show.
- Auto-scroll (Piano roll menu > Auto-scroll with the playhead; action `view.pianoFollow`, on by
  default, `state.pianoFollowPlayhead`) pages the Piano roll with the playhead during playback.
  `view.followCenter` (`state.followCenter`) keeps the playhead centred instead, in both editors.
- Background waveform (Piano roll menu; `view.pianoBackgroundWaveform`, off by default, read
  `state.pianoBackgroundWaveform`) draws Playlist audio clips faintly behind the notes, aligned to
  the first Playlist clip of the Pattern (song start when it has none).
