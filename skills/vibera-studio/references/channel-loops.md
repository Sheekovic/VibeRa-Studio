# Channel loops and editable repetitions

Development-source capability; discover the connected executable first. The
qualified distribution may not contain this increment. Core commands and
musical units are provider-neutral. Full RPS-E/module acceptance remains open.

1. Read `vibera_state` for stable Pattern/Channel IDs, current meter/length and
   transport/recording state. IDs are not display row numbers. Discover command
   schemas for `pattern.channelLoop`, `pattern.burnChannelLoop` and `steps.set`.
2. Build source notes normally. Call `vibera_command` with
   `{"command":"pattern.channelLoop","args":{"pattern":5,"channel":1,"text":"fixed","length":0.75}}`
   replacing IDs with the discovered targets. Check connected schemas for
   availability and the exact supported contract.
3. Read the targeted Pattern's `channelLoops` entry: stable `channel`, `mode`,
   stored `length`, and computed `resolvedLength`. Rack rows also expose
   `loopMode` and `loopLength`. Confirm only the intended part changed. Setter
   completion is canonical state/history acceptance, not DSP settling; inspect
   `pendingAudioUpdate` separately. Source notes remain unchanged.
4. Use `text=step|beat|bar` for automatic end rounding. Units are quarter-note
   beats; Step=.25, Beat=4/denominator, Bar=numerator*4/denominator. Auto uses
   nonmuted positive-velocity note ends within the Pattern horizon; empty parts
   use one unit. Meter is this Pattern's effective signature (custom, or inherited from the document). Use `text=off` to stop
   repetition without deleting source notes. Fixed lengths are .25 multiples.
5. `steps.set` addresses the absolute source beat, never modulo playback
   length. A later displayed repetition therefore does not erase an earlier
   source chord. Repeating activation of the same raw cell is a no-op; a different
   raw cell has its own identity/history. Fixed extent remains explicit, so source
   beyond it is dormant until the extent changes or looping is disabled. Automatic
   shrink/recall preserves the same source IDs. Discover `retainSteps` and read
   [removed-step memory](step-memory.md) before using the optional retention policy.
   Edit melodic source notes normally. Crossing durations, linked-placement loop
   phase and once-only Swing stay unchanged. No arbitrary vendor-widget control
   is implied by score looping.
6. Stop recording and disarm before `pattern.burnChannelLoop` (playback may keep
   running; a repeat sounding at that instant may retrigger once). It replaces
   this part inside the Pattern with repetitions, retaining first-cycle IDs and
   notes beyond Pattern end, allocating fresh IDs for new copies, and removing
   only its loop setting. Dormant notes inside the window are replaced. Inspect
   source IDs/count, then verify one Undo restores the original score+loop and
   one Redo restores Burn. Refused Burn must preserve state/history/take.
7. Save through the normal project operation, await its actual terminal result,
   reopen and check the connected project schema, loop settings and source IDs. Export WAV through the
   normal asynchronous render job; poll completed/failed/cancelled and inspect
   the actual file. The independent loop/Burn test compares full rendered PCM.

Macros use the same commands, contextual Pattern/Channel and explicit mode. A
document batch is atomic; failed preview rollback advances revision while exact
document/dirty/history state is restored. Re-read revision before retrying. Loop
expansion capacity errors do not permit shortening/dropping score silently.

## Piano Roll occurrences and source editing

With a Channel loop enabled, the Piano Roll displays dim outlined repetitions.
Solid originals remain directly editable, including muted/zero-velocity notes and
dormant source notes beyond the Channel cycle or Pattern horizon. Repeated notes
are views of their original source: selecting, dragging, resizing, muting, erasing
or changing velocity edits that source once and updates all linked repetitions.
Dragging uses the pointer delta, never the repeated absolute onset. A clipped
horizon edge is not an editable source-length handle. Raw originals take hit
priority if they overlap a repetition. Other-Channel ghosts retain their separate
visibility/editability settings. Ordinary source history and cancellation apply.

Read `vibera_inspect` with `entity=noteOccurrences` and the discovered Pattern ID.
Paginate through `items`, `total`, `nextOffset`; verify `projectIdentity` and
`revision`. Rows expose `sourceId` (also `id`), `sourceBeat`, `sourceLength`,
`beat`/`length` (display geometry), `iteration`, `repeated`, `dormant`, `clippedEnd`
and original musical properties. Units are unswung quarter-note beats and MIDI
pitch. Inspection allocates no persistent note IDs and never changes the score.
Only the original source IDs are editable command targets.

For display-space selection, call `vibera_selection` with `action=range`,
`target=notes`, `loopPreview=true`, beat/pitch bounds and optional current
`revision`. `criterion=starts` (default) matches ruler selection; `overlaps`
matches the marquee. Beat windows are half-open; pitch limits inclusive.
Repeated hits deduplicate the real source IDs. Default false retains raw-source
selection. Apply `notes.move` with the selected source IDs and a relative delta;
read the source and appearances again, then verify one Undo/Redo. Never send a
repeat's absolute display beat as a source move delta.

Slice's typed `slice.loopPreview=true` maps visible cuts back to source beats.
Use the same flag with `vibera_slice` preview/update/commit/cancel or canonical
`notes.slice`. Preview reports **source** cut positions and no allocated right
IDs; commit must actually complete. If overlapping appearances imply different
cuts of one source, `loopPreview.ambiguousSlice` refuses the operation atomically.
Refine the line or use explicit source-space slicing (`loopPreview=false`, the
legacy default). The UI uses display-space slicing while Channel loops exist.
True display-space Slice recipes require customization profile11; raw recipes
remain profile3, retention-only10 and send-only6. Discover connected profile
support rather than assuming older builds accept these semantics.

Save stores raw source notes and loop metadata, never occurrence copies. Reopen,
inspect both views and verify actual asynchronous export completion. This guide
is development behavior; check the connected build before relying on it.

Limits: live Burn and the complete RPS module remain unfinished. No physical
MIDI, listening, native OS input or full FL equivalence claim.
