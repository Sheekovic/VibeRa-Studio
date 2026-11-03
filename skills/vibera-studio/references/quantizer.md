# Quantize a melody with reversible preview

Discover notes.quantize, notes.quickQuantize and notes.quantizeStarts with
vibera_commands(includeSchemas=true); inspect stable note IDs through
vibera_inspect(entity="notes",pattern=...). Empty ids means only the specified
Channel part. Explicit IDs can include editable ghost Channels. Never substitute
row indices or assume every note in the multi-instrument Pattern is a target.

For a preview, begin vibera_gesture with domain="document", label="Quantizer",
and a suitable leaseMs. Retain the operation token. Update with command
notes.quantize and explicit args: pattern, channel, ids, length (quarter-note
beats), text (duration/end/leaveDuration/leaveEnd), strength, sensitivity and
durationStrength (normalized0..1). This is the semantic equivalent of the UI
preview controls, without opening or manipulating a modal window.

Consecutive Quantizer updates use the first Quantizer operation's baseline. A
preceding non-Quantizer command starts a new baseline; Move then Quantize works
on the moved notes within one macro Undo.
Start strength blends onsets; sensitivity limits attraction to a fraction of
half-grid distance; durationStrength blends length or absolute end in its mode.
For Reset use the opening grid, text="duration", and all three values1. For an
original-score comparison use strength=0, durationStrength=0, text="duration".
Do not issue Undo to implement preview. Read notes after each update to verify
actual state. Invalid inputs leave the last good preview intact and do not renew
the lease. Commit is one Undo; cancel/expiry restores the starting project.
Pending is not completed. Renew intentionally, or inspect the terminal state.

Quick operations use the same note validation/transform/history. quickQuantize
forces duration mode; quantizeStarts forces leaveDuration. notes.quantize with
empty text retains the historical onset-only semantic default. Always supply
explicit mode for new workflows. Grid range0.03125..64 beats, default0.25.

Accept, inspect, save with vibera_project, wait for the actual terminal job, reopen
and inspect again. Export WAV is also asynchronous; an accepted export request
does not prove success. Verify its terminal state and resulting file. Refuse
recording/take conflicts rather than stopping or committing someone else's take.
These operations change source notes, so linked Playlist Pattern instances change
together; they do not move Audio Clips. From0.0.55, reusable groove timing/velocity and rest templates are supported;
see grooves.md. Unsupported note properties and exact FL numerical parity remain open.
