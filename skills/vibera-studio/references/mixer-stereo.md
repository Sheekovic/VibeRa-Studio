# Mixer stereo controls

Development guidance, not a claim about the last qualified executable. Discover
each command with `vibera_commands(command=..., includeSchemas=true)` first.
Inspect `vibera_inspect(entity="mixer")`; target its stable `index`, not its
display position. Master is index0. Re-read after project replacement.

| Canonical command | Required arguments | Saved default |
| --- | --- | --- |
| `mixer.stereoSeparation` | `mixer`, finite `value` in[-1,1] | 0 |
| `mixer.phaseInvert` | `mixer`, boolean `enabled` | false |
| `mixer.stereoSwap` | `mixer`, boolean `enabled` | false |

Use `vibera_command` for a discrete change. Read back `stereoSeparation`,
`phaseInverted` and `stereoSwapped` on the same insert. `completed` with
`completionScope="canonicalMixerStereoTarget"` acknowledges the document target;
`dspSettlingAcknowledged=false` explicitly does not acknowledge audio settlement.
Unavailable audio is not a successful listening test.

Separation0 preserves the original stereo signal. Positive values crossmix to
the average of L/R at+1. Negative values subtract the opposite channel, reaching
L-R/R-L at-1; common centre content can cancel. This is an original linear curve.
Polarity negates both channels; Swap exchanges L/R. Processing follows effects
and precedes the existing pan/fader; pan law and graph latency are unchanged.
Live coefficient changes ramp over five milliseconds. Saved targets initialize
new/offline graphs directly. These controls do not expose arbitrary plugin UI.

For continuous separation, begin `vibera_gesture` with
`domain="mixerControl", sourceInsert=<stable index>`. Update with the returned
operation token, `command="mixer.stereoSeparation"`, and the same `args.mixer`.
Commit creates one Undo step; cancel restores the exact baseline. A foreign
target, stale token or invalid value must fail without editing another insert.
Keep the token after a failed history admission and retry or cancel explicitly.
Discrete switches use ordinary commands; `edit.undo`/`edit.redo` restore targets.
Batching uses the same canonical validation and history.

In the Mixer, sufficiently tall strips expose a separation control plus distinct
polarity and L/R buttons. Drag/wheel changes separation; Shift is fine adjustment,
double-click resets, and right-click opens exact entry. Compact strips retain
these actions in their context menu. Existing MIDI command mapping and macros
use the same IDs. This guidance does not qualify physical controller behavior.

Save/open preserve all three targets (project schema21); genuine older projects
default to identity. Continue polling save/export jobs until completed or failed.
On an intentional export failure, use a client API that permits an MCP error
response and inspect its structured terminal outcome; do not treat a raised
client exception as cancellation. Stereo automation-clip targeting, comprehensive
controller recording, mid-ramp graph transitions and human listening remain
separate unfinished Mixer requirements.


Stereo edits and leased separation also admit an armed, capturing or retained
MIDI take, keeping its source and placement context. Commit/cancel the control
gesture before Stop; Stop records a separate note-history transaction. Do not
substitute a new project or retarget the source during capture. Software take
acceptance does not establish physical keyboard or listening qualification.

Existing Studio macros can sequence these canonical commands and group them in
one Undo transaction; selected-context resolution still applies. A failed later
step restores the original targets/history. Public MCP macro creation/setup has
an inherited gap: use supported semantic batch operations for a grouped command
sequence and label it a batch. Do not claim that a batch created a saved macro.
The synchronous Studio macro action has no public cancellation callback; internal
runner cancellation tests do not create a new UI/MCP cancellation capability.
