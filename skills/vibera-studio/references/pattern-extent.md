# Pattern extent and meter

Development commands; discover them before use. Qualified older executables may
not expose them. Use `vibera_commands` with the command and `includeSchemas:true`.

Read `vibera_state.patterns` or paginated `vibera_inspect` entity `patterns`.
Target the stable Pattern ID, never a picker index. Readback includes `length`,
`autoLength`, stored `numerator`/`denominator` (both0 inherit),
`effectiveNumerator`, `effectiveDenominator` and `barBeats`.

Call `vibera_command` with these envelopes:

```json
{"command":"pattern.extent","args":{"pattern":1,"text":"auto"}}
{"command":"pattern.extent","args":{"pattern":1,"text":"manual","length":3.5}}
{"command":"pattern.meter","args":{"pattern":1,"enabled":true,"numerator":7,"denominator":8}}
{"command":"pattern.meter","args":{"pattern":1,"enabled":false}}
```

Use an actual discovered ID instead of the example1. Length is quarter-note
beats, not denominator beats or bars. Manual length accepts0.25..1000000 in
multiples of0.25; omitted length defaults0.25. Auto ignores length. Legacy
`pattern.length` accepts positive fractional beats and also selects Manual.
Custom numerator1..32 and denominator2/4/8/16 default4/4 when omitted.
The familiar default Ctrl+Shift+P opens the meter dialog. Existing customized
profiles are preserved and may need an explicit new binding.

Each actual change is one Undo/Redo; identical settings are no-ops. Auto follows
stored source ends (including muted/zero velocity) and fixed Channel periods,
rounded to effective bars. Generated repetitions never feed back. Manual retains
outside source notes dormant through note edits/import/take commits. Switching
to Auto can reveal them. Neither operation resizes existing Playlist windows.
Clone preserves settings; Auto split parts recompute independently.
Explicit CoProducer accepted regions use Manual to retain requested trailing
rests; select Auto deliberately if occupied-bar length is desired afterward.

Recipe: create beat notes, inspect Auto length, choose Manual14steps by requesting
3.5quarter beats, add a variation beyond the window, verify that it stays dormant,
switch Auto, verify the revealed source extent, and Undo the mode change. Save,
new/reopen and inspect IDs/length/meter before rendering the arrangement. Use
`vibera_batch`/typed macro recipes for one grouped musical change, respecting their
existing rollback/history rules.

Commands share UI validation and return structured errors. Completion scope is
`canonicalPatternExtent`: document state is committed, not proof that DSP has
settled. Inspect `pendingAudioUpdate` separately. An active Pattern take pins its
playback window and effective meter; changes require Stop.
Invalid/no-op requests do not finish the take.
An ordinary score edit that changes an active recorded Pattern's window refuses
atomically with `recording.patternExtentBusy`; preserve the take and Stop first,
or choose Manual before recording. In-range live edits remain supported.
File save/export returns its real asynchronous job; poll until completed/failed/
cancelled, then inspect actual project/WAV outcomes. Accepted is not completed.

Pattern clock/grid/Beat-Bar loops/MIDI export use local meter. Song clock remains
the document meter; changing a Pattern does not create Song meter markers. Older
projects migrate Manual/inherited to preserve exact windows. Model signed64 IDs
do not expand the inherited MCP target-range limit1..1e9. Third-party native UI
capabilities and CoProducer provider meter restrictions remain unchanged.
