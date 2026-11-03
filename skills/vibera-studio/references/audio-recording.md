# Audio-input recording

Unreleased development source guidance. Discover `vibera_audioRecording` in
tools/list first; older binaries do not expose it. The in-app CoProducer agent does
not get this tool (it would open the microphone); external MCP clients do.

1. Read `vibera_audioRecording {"action":"state"}`. `readiness` is `ready`,
   `disabled`, `noDevice` or `noInput`; `device` lists input names, active inputs and
   the reported input/output latencies. State is never listening or hardware proof.
2. Configure: `{"action":"configure","enabled":true,"input":0,"channels":2,
   "monitor":false,"monitorGain":1}`. `input` is the first device input channel
   (0-based); `channels` 1 records mono from it, 2 records it and the next one.
   Enabling opens exactly those inputs, which restarts the device once; it is refused
   while playing or recording. Monitoring sends the input to the outputs (headphones:
   speakers feed back).
   Several inputs at once (a drum kit on several mics): `"tracks": n` (1..8) records n
   separate tracks from consecutive inputs, each `channels` wide. After Stop each track
   is its own 24-bit WAV (`<take>-in1.wav`, ...), Audio Clip Channel and Playlist track,
   all placed in one Undo step (`take.files`; loop passes stack per track). The same
   is in the input button menu: "Record several inputs at once".
   `"insert": n` (0..125) routes the recorded Channels into Mixer insert n (0 Master);
   several tracks feed n, n+1, ... A missing insert falls back to Master and the take
   message says so. Menu: "Record into Mixer insert".
3. Record through the transport like the toolbar: switch to Song
   (`vibera_transport {"action":"song"}`), arm with `vibera_command transport.record`,
   then `vibera_transport play`. Pattern mode refuses audio takes
   (`take.code = audioTake.patternMode`); MIDI recording is unaffected. Arming
   during playback punches in at the current position.
4. Stop with `vibera_transport stop` (or the Record button to punch out). The take is
   trimmed to the position where recording started (count-in and pre-roll removed),
   moved back by the device round trip (`latencyCompensationMs`), written as a 24-bit
   WAV in `Recorded/` next to the saved project (Documents/VibeRa Studio/Recorded when
   unsaved) and placed as an Audio Clip on the first Playlist track free over its span
   as one Undo step.
5. Poll `take`: `phase` capturing, placing, completed, empty, unplaced, failed or
   cancelled, with `completionState` pending/completed/failed/cancelled, `clip`,
   `channel`, `beat`, `lane`, `seconds`, `file`, `code` and `message`. A retained MIDI
   take keeps the audio take in `placing` until that MIDI take is retried or cancelled.
6. `droppedSeconds > 0` or `interrupted` means the take was truncated at a disk stall
   or device restart; later audio is not stitched on. `cancel` (or Esc) discards a
   capturing take and its file.

Loop takes and comping:

7. With Song Loop on (toggle before recording; it is refused during a take), every
   loop pass becomes its own take: `take.passes`, `take.clips` (recording order, last
   one plays), `take.group`. One clip per pass sits on consecutive free tracks, all
   grouped (they move and slice together) and reading their own window of one WAV;
   earlier passes are muted. Placement is one Undo step (`audio.takes`).
8. Comp with `vibera_command audio.comp {"target": clip}`: that take clip plays over
   its own span; every other audio take in its group is cut at the span edges and
   muted inside it, outside unchanged; one Undo step. Swipe comp a section in one
   step with `"range": [fromBeat, toBeat]`: the chosen take is cut at those edges and
   only that part plays (or slice the grouped takes first and comp a piece). The
   Playlist clip menu offers "Use this take here (comp)" and, with a Playlist time
   selection (Ctrl-drag the ruler), "Use this take for the time selection". Where the
   source windows allow it, comp edges get a 10 ms crossfade: the chosen piece
   starts/ends half a fade into its neighbour and both fade over the overlap.
9. Finish with `vibera_command audio.flattenComp {"target": clip}` (clip menu
   "Flatten comp"): the group's muted takes are deleted and the playing pieces move
   onto the group's top track (left in place if they overlap by more than 100 ms,
   i.e. more than comp crossfades); one Undo step.
10. Tighten a late/early take: `vibera_project {"action":"alignClip","clip":id,"beats":0.25}`
   moves the clip (and its take group) so its first transient lands on the nearest
   grid line; one Undo step (clip menu "Align first transient to the grid").

Fades: `vibera_command audio.fade {"ids": [...], "fadeIn": s, "fadeOut": s}` sets
per-clip fades in seconds (omitted keeps the current one; their sum may not exceed
the clip). They render live and in export (convex curve), survive save (project
version 43), stay on the outer edges when slicing and shrink with trims.
`vibera_inspect clips` reads them back. The clip menu has a Fades submenu.

Punch: Ctrl-drag (or double-click) the Playlist ruler to select a time range, or
`configure {"punchRange": [startBeat, endBeat]}` (null clears). A take then keeps only
what falls inside it (every loop pass too); the WAV keeps the whole recording so the
clips can be trimmed open later. Nothing inside gives `audioTake.outsidePunch`.

Crash safety: the WAV header is rewritten after every second of audio and a sidecar
(`<take>.take.json`) records the take's timeline position. After a crash the next
start lists interrupted takes in `state.recovered` (microphone menu: Recovered takes);
`{"action":"placeRecovered","file":...}` places one where it was recorded (one Undo
step), `forgetRecovered` clears the list and keeps the files. Up to the last second
of audio before the crash can be missing.

Limits: one stereo or mono input per take, no input effects or record-to-Mixer-insert, constant project tempo. The device's
reported latencies are trusted; a device that misreports them shifts the take.

Stretching (any Audio Clip): `vibera_project {"action":"stretchClip","clip":id,"ratio":r}`
(new length / old, 0.25..4) or `"beats": n` to fit the clip to n beats. A job renders a
pitch-preserving stretched WAV (Stretched/ next to the project), then the clip plays it
from its start in one Undo step; the original file is untouched.
Sample edits: `{"action":"editClip","clip":id,"edit":"reverse|normalize|gain|consolidate",
"value":...}` writes a new file (Edited/) and swaps the clip source in one Undo step.
normalize takes a target dBFS, gain dB; consolidate bakes the clip window and its fades.
