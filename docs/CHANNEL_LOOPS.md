# Per-Pattern Channel loops

Development source; not yet a released or qualified module. Complete scope stays
in [Rack / Pattern / sampler](modules/RACK-PATTERN-SAMPLER.md), RPS-E/C/L/M.

Right-click a Channel name in the Rack, then **Channel loop (this Pattern)**.
Off keeps existing score behavior. Fixed length accepts quarter-note beats in
sixteenth-note multiples. Auto Step, Beat and Bar round the last active note end
up to the selected unit; exact boundaries add no empty unit. Empty parts use one
unit. Step is0.25 quarter-note beats; Beat is4/document denominator; Bar is
numerator times Beat. Per-Pattern meter remains a separate unfinished requirement.

Loop settings belong to a Pattern and stable Channel ID. They never modify the
instrument, another Pattern's part, transport Loop or raw source notes. Notes
whose onset lies outside the loop window are dormant. Automatic boundaries
ignore muted/zero-velocity notes and consider active ends up to the Pattern
horizon. Crossing notes keep their duration across Channel-cycle boundaries;
the existing final Pattern/placement window still clips playback. Placement
offsets retain Pattern-source phase. Swing applies once at each repeated raw
onset, so an odd number of sixteenth notes alternates its phase naturally.

Faded Rack steps/melodic marks show repeated score; accent lines mark step-grid
cycle boundaries. Clicking repeated step cells edits the original cell through
canonical `steps.set`, so UI and MCP share that mapping. Other note properties
are edited on the source score; the Piano Roll draws repeated appearances from
`pianoNoteOccurrences` (hit-testing selects the real source note), covered by the
piano navigation acceptance's loop-preview stage. Ordinary loop settings prepare
a new immutable timeline off DSP.
Unchanged occurrences retain their runtime identity through score edits.

**Burn repetitions to score** materializes the selected part inside the existing
Pattern horizon and turns off only its loop. First-cycle source IDs and all
other parts stay intact; copied repetitions get fresh IDs. Dormant notes inside
that horizon are replaced by the repeated score; notes beyond the Pattern end
remain. Undo restores exact source notes, loop settings and allocator/history.
Burn is admitted during playback (2026-10-25): the live sequencer reconciles
voices to the new note IDs, so a repetition sounding at that instant may
retrigger once; no seamless-transition claim. It still refuses while a take is
armed, capturing or retained: new persistent note identities must not silently
retrigger a recording. Source cloning/linked placements provide explicit
alternative variations.

Schema29 stores `CHANNEL_LOOP(channel,mode,length)` children on each Pattern.
Older projects migrate to loops Off; loop data under an older schema, duplicate
targets, unavailable/Audio Clip Channels, noncanonical modes/lengths and excessive
expansion fail atomically. A looped Pattern is bounded to100000 raw occurrences
before compilation/publication. Existing fixed gate/voice/worker capacities and
callback deadlines are unchanged. Runtime repetition tags occupy a negative,
checked signed64 namespace separate from real Placement IDs; they are never
persisted or exposed as editable Clip IDs. Extremely large legacy Clip IDs that
cannot fit that tag refuse loop activation without changing existing data.

Semantic commands: `pattern.channelLoop` and `pattern.burnChannelLoop` through
`vibera_command`. Discover exact connected-build schemas first. Required target
fields are `pattern` and `channel`; the setter also requires `text` as
`off|fixed|step|beat|bar`, plus `length` for a useful fixed choice (default0.25).
Read `vibera_state.patterns[].channelLoops` and Rack rows' `loopMode/loopLength`.
Synchronous completion means canonical model/history acceptance, not a DSP
acknowledgment; inspect `pendingAudioUpdate` separately. Macro recipes reuse these
commands and normal contextual targeting. Invalid actions return structured
`channelLoop.*` errors; stopped-only Burn admission also applies to macros.

## Reference provenance and limits

| FL task | Expected interaction | VibeRa equivalent | Intentional difference / gap | Evidence |
| --- | --- | --- | --- | --- |
| Repeat one Channel in one Pattern | Per-part automatic Step/Beat/Bar or fixed end | Rack name context > Channel loop; shared typed command | Max-active-end and metrical Beat are independent policies; exact FL rounding unobserved | Official Rack/update docs; bounded static handlers; own model/MCP/PCM tests |
| Edit repeated rhythm | Source-cycle changes affect repeats | Faded Rack cells reference the same source; clicking a repeat edits its original cell | Piano Roll draws repeats; editing targets source notes | Own canonical Step/history and prepared playback checks |
| Burn a loop into editable score | Materialize repetitions with recoverable history | Same Pattern/Channel, new IDs only for repetitions, one Undo | Stop required; in-window dormant notes replaced, beyond-horizon notes retained | Own model, actual MCP and three-way WAV comparison |
| Keep playing while editing loops | Timing/held input retained | Prepared immutable timeline updates, stable occurrence tags | Native OS input/listening not qualified by virtual callbacks | Actual Analog Lab V Pattern/Song processing, exact held-capture duration and lifecycle counters |

Official Image-Line FL20.1.1 notes explicitly document per-Pattern, per-Channel
looping/length. The Channel Rack manual documents automatic Step/Beat/Bar and
Burn. These establish workflow intent, not VibeRa algorithm parity. Primary links
and the bounded research receipt live in
`build/channel-loop-reference-20261020/findings.json`.

Static study of the matching installed engine distinguishes automatic modes and
fixed endpoints, Pattern+Channel target ownership, and a separate Burn action.
Its endpoint helper assigns the last matching event's end; iterator traversal is
bounded by an onset horizon, but its ordering is not proven. Metrical Beat mapping,
crossing-note runtime behavior and an exact maximum-end policy are not established
by that study. VibeRa's max-active-end, document-meter and continuing-tail rules
above are explicit independent musical policies pending further observed parity
evidence. No FL runtime was executed, settings edited, or proprietary code/assets
imported. This work uses the existing read-only saved analysis project rather than
reimporting or reanalyzing the whole binary.
