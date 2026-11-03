# Piano Roll note modes and properties

Development source guidance. Discover the connected executable before use;
this document does not qualify an older package or establish FL runtime parity.
Native OS input, physical MIDI, listening and hardware need separate evidence.

Use `vibera_commands` with `includeSchemas=true` for `notes.add`, `notes.slide`,
`notes.portamento`, `notes.colourGroup` and `notes.properties`. These share the
actual application commands, validation, model and Undo/Redo. Read raw source
fields through `vibera_inspect entity=notes` or `noteOccurrences`; repeated Piano
previews retain source IDs and do not become separate editable source notes.
Source mode fields and `transitionAcknowledged=null` are not a DSP acknowledgement.

`colourGroup` is an integer0..15, displayed as colors/MIDI channels1..16. It is
independent of the DAW instrument Channel ID and ordinary selection-group IDs.
Legacy notes default to0. Slide and Porta flags default false. Colors can be
used on supported hosted instruments; they encode note MIDI channels. VibeRa
does not promise arbitrary native plugin widget control or translate these
sampler Slide/Porta transitions into arbitrary vendor per-note pitch bends.

For a local sampler, a Porta-marked ordinary note inverts the Channel Porta flag.
A Slide is a silent control event, not an additional sounding note; Repeat plus
Slide refuses rather than erasing the saved Repeat property. Matching colors
define independent Mono/Slide domains. Latest Slide start wins, with stable
placement/note IDs for ties; offs/cuts run before ordinary ons and Slide admission.
Only matching-color voices held at admission participate. One multiplier based
on the highest pitch preserves chord intervals; velocity/pan/Mod X/Y interpolate
to explicit targets and retain their endpoints. A later-born voice does not join.
Endpoint edits rebase over remaining time; edits after completion use5ms smoothing.
Deleting a newer control cannot revive an older superseded one. These are explicit
independent policies; source documentation alone does not establish actual sound
or exact FL behavior. Raw flags remain recoverable
if a Channel later changes instrument role, but unsupported transitions stay dormant.

UI: Piano Roll has a16-color selector, Slide/Porta insertion switches, Properties,
Recolor and By color actions. Familiar contextual defaults are S/O, Alt+C and
Shift+C. Single-letter defaults respect Typing keyboard-to-piano and text focus;
all bindings remain customizable. Draw/Paint consume the current effective
insertion flags and color atomically. Existing notes use canonical edits instead.

Discover `vibera_note_modes`. `state` reads requested/effective insertion settings,
target IDs, support and any captured properties operation. `configure` changes
session insertion defaults only; it creates no score history. Supply one or more
explicit `slide`, `portamento`, `colourGroup` values. Enabling native transitions
requires a local sampler; switching to a hosted instrument disables their effective
insertion without erasing the requested settings.

`changeColour` applies the current color to explicit `ids` or current selected
source notes; no selection means all source notes in the target Channel.
`selectColour` replaces selection with that color in the selected Pattern.
Selection is session state, not musical project history. Explicit stable Pattern
and Channel targets are supported where the discovered schema allows them.

Double-click a note or use Properties to open the shared Note Properties window.
For MCP, use `propertiesOpen` with `pattern`, `channel`, `ids`, then retain the
exact returned `operation`. Opening/staging is pending, not completed editing.
`propertiesUpdate` takes a sparse `noteProperties` object and merges it into the
captured draft. Read `propertiesState` to see original values, all captured source
IDs, staged fields, support, visibility and stale context. Only changed fields
apply, preserving unrelated and mixed-selection values.

The patch supports Slide/Porta, color, velocity, signed pan, fine pitch in cents,
Mod X/Y, release velocity, mute, and single-note absolute start/length/pitch.
Start/length use quarter-note beats; a multi-note selection refuses absolute
timing/pitch edits. Sampler expression controls are unavailable for hosted
instrument notes. Obtain exact ranges and prerequisites from discovery.

`propertiesReset` clears the staged patch to the opening values. `propertiesCancel`
or Cancel/Escape/window Close ends only that exact operation and leaves score/history intact.
`propertiesApply` uses one atomic canonical `notes.properties` command and one
Undo step; the dialog's Return key shares Apply. An unchanged draft creates no history. A stale token cannot affect a
newer window. Project/revision changes refuse Apply; cancel and reopen instead
of overwriting newer work. Failed validation retains the draft, existing notes
and Repeat metadata. Check structured failure and actual readback before retrying.

Example using discovered stable IDs:

```json
{"tool":"vibera_note_modes","arguments":{"action":"propertiesOpen","pattern":5,"channel":1,"ids":[12]}}
{"tool":"vibera_note_modes","arguments":{"action":"propertiesUpdate","operation":"<returned token>","noteProperties":{"portamento":true,"colourGroup":2,"velocity":0.7}}}
{"tool":"vibera_note_modes","arguments":{"action":"propertiesApply","operation":"<same token>"}}
```

Replace all example IDs/tokens with current readback. Inspect the actual source
note, Undo and Redo the accepted change, then Save/Open through `vibera_project`.
File acceptance stays pending until the exact project job completes or fails.
WAV preserves supported musical expression. MIDI1 export preserves color channels,
omits silent Slide events and cannot emulate Porta or other unsupported note
expression. Completed MIDI jobs report the legacy union `expressionNotesOmitted`
and detailed `midiExpressionNotesOmitted`, `midiSlideNotesOmitted`,
`midiPortamentoNotesOmitted`; pending/failed counts are unknown/null. Use project
files for complete recoverable metadata and inspect exported files independently.

Stdio accepts at most65,536 UTF-8 bytes per JSON request frame, excluding LF.
Array-count limits also remain subject to this transport byte limit. Batch large
notes.import payloads (for example128 notes per call); read every generated source
ID through paginated inspection before sending one atomic edit that fits the
frame. Oversized frames are rejected with JSON-RPC id:null, code-32600 and
error.data{code:request_too_large,maximumRequestBytes:65536}; they are never
dispatched and the connection remains usable. Read current state and reduce the
payload instead of interpreting rejection as an accepted or completed edit.
