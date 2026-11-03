# Stretch selected score notes

Requires0.0.62. Discover the actual connected application and operation schema;
version labels or registration do not establish qualification. This transforms
note onsets/durations, not audio files or a third-party plugin's native interface.

1. Inspect projectIdentity/revision, source Pattern, active playable Channel and
   paginated note IDs. Select targets explicitly. Empty targets fail; they never
   mean the whole part. Explicit IDs may include other Channel parts. UI handles
   operate only on selected editable notes, preserving the user's selection.
2. Call `vibera_stretch` with `action=begin`, `ids=[...]` and optional idle lease
   (250..120000ms; default30000). Retain operation token. Anchor is the earliest
   captured onset in quarter-note source Pattern beats, independent of Song
   position. Read sourceEnd, minimumRatio, maximumRatio and count.
3. `action=update operation=<token> ratio=2` previews twice the phrase duration;
   ratio0.5 halves it. Every update derives from the unchanged captured source,
   not the preceding preview. Project/dirty/history/audio remain unchanged. Read
   planned notes using state offset/limit (max512). Ratio range0.000001..1000000
   is further constrained by source lengths and maximum musical time1000000beats.
   Minimum normal note duration1/960beat; a shorter legacy note cannot shrink
   further. Invalid updates preserve the last valid plan. Refresh before expiry.
4. Commit or cancel with the same token. Completed means the document edit
   actually succeeded; inspect pendingAudioUpdate separately. Cancel/expiry
   leaves the exact source untouched. Local actions/context changes can cancel.
   Other remote writes/files must not steal the preview. Unknown/foreign tokens
   fail. State reports actual completed/cancelled outcomes; do not retry commits
   blindly after losing a reply.
5. Verify retained IDs/order/pitch/velocity/mute and untouched unrelated notes.
   Onsets use `anchor+(onset-anchor)*ratio`; durations use `duration*ratio`.
   Changed step notes become score notes. Ratio1 is an exact no-op. A changed
   score can grow Pattern length by bars; existing Playlist placement lengths
   stay unchanged and linked placements keep their shared source. One Undo/Redo
   restores exact geometry/properties/extent; no new note identities are created.
6. Save/reopen and inspect the actual file; export WAV/SMF through project jobs
   and poll real terminal outcomes. A failed job may return structuredContent
   with completionState=failed and a structured error: preserve that failure
   rather than reporting completion or retrying blindly. MIDI omits muted notes.

For an immediate transformation use `vibera_command` command=`notes.stretch`,
args={pattern,channel,ids,beat:<anchor>,value:<ratio>}. Anchor must precede or equal
all target onsets. `vibera_batch` can combine canonical edits with one Undo where
its recording/admission rules permit. Recorded UI macros retain the ratio;
current-context macros derive the earliest current selected onset. Fixed-context
macros keep their explicit anchor/IDs. Existing profile fields store this action;
Slice-bearing profiles still require3. Broad remote saved-macro management remains
an inherited gap and is not established by batch support.

UI: select notes, drag the double-arrow handle located right of the selection; Alt
bypasses grid snapping. Alternatively hold Right-Shift on a selected note's right
edge; Alt with that shortcut quantizes ratios to25% increments. Escape cancels.
Piano Menu > Stretch selected notes opens ratio entry; it validates the captured
source before applying. Settings > Shortcuts & Macros can bind/record/build the
same action. Source anchor/limits/default snapping are explicit VibeRa policies;
installed FL24.2 exact parity and physical familiarity remain unqualified.
