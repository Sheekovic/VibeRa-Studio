# MIDI take ownership and recovery

Unreleased Mixer-module source guidance. Discover `vibera_recording` in tools/list;
older qualified binaries need not expose it. This uses shared application take
ownership and canonical history, not pointer simulation or a second MIDI engine.
No MCP note injection, actual hardware input, listening or crash-persistent take
recovery is established by this document.

1. Read `vibera_state.recording` or `vibera_recording {"action":"state"}`.
   `recordArmed` is an arm request, `capturing` is actual take ownership, and
   `pending` means a retained envelope after capture stopped. A completed state
   query means only that the query finished. Capture acceptance is not note commit.
2. Arm/disarm through `vibera_command {"command":"transport.record"}`. It shares
   Record's actual application behavior. While stopped this toggles arm only;
   playback and the shared clock must be available before capture can start.
   While retained, Record retries the old take; it never silently replaces it.
3. Finish via `vibera_recording {"action":"finish"}`. Stop also stops/seeks the
   transport even if applying notes fails. Finish without any take is a no-op;
   arming alone does not create a take or force capture. Read actual result/state.
4. When retained, preserve its positive `serial`, `projectIdentity`, Pattern,
   original Song placement and note Channels. Retry with
   `{"action":"retry","serial":<exact current serial>}`. Cancel with
   `{"action":"cancel","serial":<exact current serial>}` only when the user
   intends to discard that unapplied take. Tokens are instance-owned exact JSON
   integers1..9007199254740991;0, missing, stale and consumed tokens refuse.
5. `completionState=failed` has structured `error.code/message` plus actual take
   readback. A failed retry leaves the same owner available for recovery. Common
   codes are take.preparation_failed, take.busy, take.source_changed,
   take.target_missing, take.placement_changed, take.invalid_capture and take.stale.
   Capture invalidity retains diagnostics and requires explicit cancellation;
   partial data is never silently applied as a complete take. `canRetry` reports
   current admission, not a guarantee against later resource failure.
6. `pending` is never success. Only a terminal completed finish/retry plus phase
   completed and actual canonical note readback establishes application. Cancellation
   returns cancelled and does not create musical history. Cancel/retry are refused
   during the short synchronous applying phase. A completed/consumed token cannot
   append the take twice.

Read-only `state` accepts `offset>=0`, `limit=1..512` (default128) for currently
retained complete note segments. `notesTotal`, `notesOffset`, `notesPartial` and
recording.heldCount/pendingInput distinguish a page from an entire valid take.
These temporary note IDs are0; Channel IDs are stable canonical targets. Beat and
length are quarter-note Pattern-source beats, pitch0..127 and velocity0..1.
Development Release support adds normalized `releaseVelocity`0..1 and rounded
`releaseMidiVelocity`0..127. Explicit physical MIDI off bytes survive; synthetic
closes use0. These are retained score data, not ADSR duration or native-delivery
acknowledgement. Discover the connected schema first. While
capturing/sealing/invalid, segments are partial diagnostics. Held notes do not
appear until closed. Terminal take noteCount is historical; terminal notes pages
are empty because ownership has moved into the canonical Project or was cancelled.
Never use temporary id0 with note-edit commands.

After retry, inspect the original Pattern with `vibera_inspect entity=notes`,
verify actual stable IDs and musical values, then verify one Undo/Redo where
applicable. Save/reopen and inspect again. Pending takes block new/open/save/WAV
and MIDI export; discardChanges does not discard an unapplied take. Seek,
Pattern/Channel switches and Undo/Redo must respect the shared finish result.
Safe one-shot Mixer controls can remain available; new owned batch/gesture starts
refuse retained take ownership. Finish those edits before retrying.

A timeout/disconnect may occur after actual completion. Read state before repeating
anything; consumed-token refusal and canonical notes/history outrank a lost reply.
In-memory retained envelopes do not survive process crash/power loss. Third-party
native widgets and MIDI hardware are separate qualification; do not infer them
from synthetic/controller/MCP checks.

Controllers in takes: notes released while the sustain pedal (CC64) is down end when
the pedal goes up (or when the take stops). Other CC, pitch bend and channel pressure
become Pattern controller events played to plugin instruments; read them with
`vibera_inspect {"entity":"controllers","pattern":id}` and clear a Channel's with
`vibera_command notes.clearControllers {pattern, channel}`. The local sampler ignores them.
