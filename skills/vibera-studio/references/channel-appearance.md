# Channel appearance and stable selected-slot sorting

Discover `channel.properties`, `rack.properties` and `rack.sort` through
`vibera_commands` with `includeSchemas=true`. These are shared undoable canonical
document edits through `vibera_command`, not pointer simulation. Discover stable
Channel IDs/order with `vibera_inspect(entity=channels)` and `vibera_rack(state)`;
both expose `name`, `colour` and `icon`. A filtered row number is never an ID.

Successful property/sort commands return `completionState=completed`,
`completionScope=canonicalChannelAppearance` and `changed` (false for a no-op).
This acknowledges the canonical document/history; `dspSettlingAcknowledged=false`
does not assert audible readiness. Read `pendingAudioUpdate` and actual audio state.
Pure appearance batches validate every target/patch before publishing any edit;
an invalid later entry cannot transiently rename an earlier Channel.

In the Rack, F2, middle-click on a Channel name and the name context menu open
Channel appearance. An ordinary single name click still opens that instrument;
Piano Roll access remains separate. Rack Menu exposes selected-Channel colour/
icon and sorting. Single-name context acts on that Channel; bulk operations use
the explicitly selected Channels, including hidden members where indicated.
Pattern and Mixer property shortcuts keep their own contexts. User remappings
take precedence over familiar defaults.

Development UI adds16 theme-derived colour swatches, a hue/saturation spectrum,
RGB fields, HTML hex entry and Theme reset. Choices preview only in the dialog;
Apply publishes one canonical properties edit, Cancel publishes none. Bulk
Keep unchanged omits colour while still allowing an icon change. Invalid HTML
disables Apply. These controls do not audition instruments or retarget MIDI.

Discover `vibera_rack(action=state).colourPicker` when present. Its `palette`
contains stable preset indices and current opaque ARGB values; these are visual
theme choices, not immutable named colours. Use the advertised `singleCommand`
or `bulkCommand` with `appearance.colour` to commit that exact value, then read
Channel state. `themeValue=0` restores inheritance; omit colour for bulk Keep.
`previewChangesProject=false` distinguishes local preview from committed state.
An older executable may omit this readback; use actual command discovery rather
than assuming the development picker or its palette shipped in the package.
Circle mode, colour history, safe/quantized choices and gradient operations are
not claimed by this increment. Broader RPS-B and full module acceptance stay open.

```json
{"command":"channel.properties","args":{"channel":7,"appearance":{"name":"Kick close","colour":4292881209,"icon":"drum"}}}
{"command":"rack.properties","args":{"ids":[7,11],"appearance":{"colour":0,"icon":"hat"}}}
{"command":"rack.sort","args":{"ids":[7,11,16],"channelSort":{"by":"name","descending":false}}}
```

The optional single-Channel name is trimmed, one line,1-256 characters. Bulk
patches accept colour/icon only; they do not silently rename every member.
Colour is an integer opaque `0xFFRRGGBB` ARGB value, or0 to follow the theme.
Nonopaque alpha is invalid. Icon IDs are original vectors: `none`, `drum`, `hat`,
`keys`, `bass`, `synth`, `mic`, `sample`, `audio`. `none` clears the icon. Omitted
fields remain unchanged; an empty patch is invalid. These are display settings,
not instrument type/routing assignments. Replacing a sample retains the existing
automatic source-name behavior; a custom name is not promised to survive it.

Sort `by` is `name`, `colour`, `mixer` or `type`; `descending` defaults false.
Only slots occupied by the explicit1-128 distinct existing IDs are rearranged.
Unselected slots remain fixed. Input-ID array order is not the tie-breaker:
equal keys preserve current project order, in either direction. Name comparison
is case-insensitive; Mixer comparison uses stable assigned insert ID, not current
Mixer display position; type order ascending is hosted instrument, sampler,
Audio Clip. Assigned-colour sorting ascending places theme-following0 first,
then explicit chromatic hue from red through violet, then greys. It does not
sample theme pixels or brightness; equal hues and all theme/grey ties stay stable.
Descending reverses key ordering while preserving ties. Global theme changes
never silently reorder the project.

Each committed patch/sort is one history action. Equivalent patches/already
sorted input add no revision, dirty or Undo/Redo edge. UI dialog Cancel changes
nothing; confirmation refuses stale project/revision/target rather than applying
to a later selection. Semantic callers can supply the current top-level
`vibera_command.revision`; re-read after a stale refusal. Invalid IDs, duplicate
members, unsupported icons/alpha, unknown fields or malformed sorts refuse
atomically. Read the actual structured error and current state before retrying.
No source file, score, automation, Mixer route or stable Channel ID is changed
by appearance/sort. Clone inherits appearance while using new Channel/plugin
identities and its existing generated unique name; score transfer remains a
separate explicit operation described in [Channel management](channels.md).

Typed appearance/sort recipes use profile7 and the existing persisted
`vibera_macros` service. Read its generation and supported command schemas before
create/update/run/remove. Explicit targets use `currentContext=false`; use
current-context forms only as advertised by discovery. Saved configuration
completion means disk and memory agree, not a musical Undo. Running the actual
saved recipe creates its musical history transaction. Unknown metadata or older
profiles carrying unsupported new fields must refuse, not silently drop them.
Arbitrary profile-file import is not implied by this tool.

Appearance was introduced in project schema27; current newer schemas preserve
name/colour/icon/order on Save/reopen. Poll actual
`vibera_project` file jobs to terminal completion; command acceptance does not
prove disk persistence or audible playback. The focused stdio verifier exercises
real canonical state/history, malformed inputs, saved recipes and project jobs;
it supplies no native OS input, physical MIDI/audio, listening or live DSP claim.
Advanced groups/locked solo/link-aware cloning and the whole RPS-B milestone
remain open until their own implementation and complete acceptance.
