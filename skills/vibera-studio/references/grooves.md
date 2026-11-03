# Reuse timing and dynamics between parts

Requires0.0.55. Discover vibera_groove and notes.quantize schemas. Grooves store
Pattern-zero phase, duration and normalized velocity; pitch is ignored. Templates
use original VibeRa.Groove version1 JSON (*.vgroove), not FL FSC compatibility.

Capture with vibera_groove(action="capture",pattern=...,channel=...,name=...);
empty ids means that Channel part, explicit stable note IDs can include ghosts.
cycleBeats defaults to the source Pattern length. Selected onsets outside the
cycle fail; unselected whole-part capture excludes notes outside its loop. Chords
collapse deterministically to the lowest stable note ID at each exact onset.
Capture does not edit the project or create Undo. source="previewBaseline" reads
the original active Quantizer preview, matching its Capture button.

Use presets to discover original built-ins. Save/load/validate use the same
template validation as the UI. Save needs an absolute path and explicit overwrite
for an existing file; directories are refused. Loads are bounded to1MiB, including
Windows UTF-8 BOM text. A completed save means actual file I/O finished. These
small operations are synchronous on the control thread, never DSP callbacks.
Failure returns a structured error and retains project/last-good preview state.

Begin a document vibera_gesture, retain its token, then update notes.quantize with
pattern/channel/ids, groove=<full validated template>, text=<timing mode>,
strength, sensitivity, durationStrength and velocityStrength. Strengths are
normalized0..1, UI percentages0..100. Velocity defaults0 to retain source levels.
The uniform length grid is inactive when a groove defines timing. Quick actions
remain uniform-grid operations; groove arguments require advanced notes.quantize.

Starts repeat the template cycle and attract to nearest starts; duration mode
uses the matched template length, end mode attracts the absolute end to repeated
template ends. Sensitivity is an independent local half-gap policy, not verified
FL numerical parity. Guides share the actual onset data; dense displays decimate
to2048 real positions. Read bounded guide metadata in vibera_state and full active
data via vibera_groove(action="preview").

zeroVelocityDeletes defaults true: eligible zero-level points are rests and remove
matching notes, independently of velocity strength. With rests disabled, velocity
strength1 can still set those notes to0: silent, but retained in the score. This is destructive only on
acceptance; preview updates restore baseline IDs/order when rests are turned off.
Read notes and selectedNotes before/after. During preview, selection may include
temporarily removed source IDs; cancel/expiry restores them. Commit prunes deleted
IDs and creates one Undo. Reset is uniform grid, the opening snap, duration mode,
start/sensitivity/duration strength1, velocity0 and rests enabled. Cancel/expiry
restores the original project. Never use a chain of Undo calls to simulate preview.

After commit, save/reopen and wait for actual project job completion. Applied
grooves are ordinary score notes and do not depend on the template file remaining
at its old path. WAV includes groove timing plus any later global/Channel Swing;
do not accidentally apply swing twice. Verify resulting notes and actual export.

If a transformed note extends beyond the Pattern, the canonical note-edit policy
grows its length to the next bar. Other Channels' notes keep their own durations;
they are not stretched to fill that longer loop. Read the Pattern length as well
as its notes before judging an audible gap, and use Undo to restore both exactly.

Groove-bearing macro profiles use version2 so older applications refuse them;
ordinary profiles remain version1. Both load in0.0.55. Macro profile/recording UI
coverage is a retained wider backlog, not a new macro-management MCP promise.
AI workflows reuse the same template payload through canonical notes.quantize.
Per-note pan, pitch offset, release and modulation levels remain unsupported in
this groove workflow until the actual musical model supports them. No fake knobs.
