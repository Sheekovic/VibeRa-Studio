# Removed-step memory (development sources)

This guide accompanies unreleased development sources. It is not part of the
qualified0.0.65 application. The structural helper's checks do not qualify application,
UI, MCP, playback, profile migration or listening behavior. Discover the actual
running executable's schemas before using any operation below.

The intended familiar workflow is right-click/erase a step, then left-click it
again to recover that source chord's pitch, velocity, duration and IDs. With
`muteRemovedSteps=true`, removal changes existing source notes to `muted=true`;
reactivation unmutes every member of that same-cell step chord. Zero velocity is
retained, not replaced with a newly audible default. With the preference false,
removal deletes that chord. Switching the preference does not purge previously
muted notes. Empty activation creates one ordinary default step; repeated on/off
requests with no change must not consume IDs or create history.

UI access is Settings > Workflow > “Mute removed steps
instead of deleting them”. The semantic equivalent is:

```json
{"action":"configure","muteRemovedSteps":true}
```

Send it to `vibera_rack` only if discovery advertises that property. Read
`muteRemovedSteps` from Rack state and the actual canonical note `muted` flags
from `vibera_inspect entity=notes pattern=<id>`. Preferences save before live
publication and are separate from musical Undo. A failed save leaves the old
policy intact. An unreadable or mismatched saved shortcut/macro profile refuses
the change, including same-value requests; reconcile it through Settings first.
Generation is rechecked immediately before save, following the existing profile
service. This is optimistic detection, not an atomic comparison against a
noncooperating external writer after that final check. Finish an active macro/
document gesture before changing it. Do not combine it with another persistent
preference or graph-view change.

Use `vibera_command` with `command=steps.set` and stable Pattern/Channel IDs,
`beat` in zero-based quarter-note beats, and boolean `enabled`. One sixteenth is
0.25 beats. Optional `retainSteps` overrides the Workflow policy for that action;
omission follows it. An explicit false override differs from omission. New-mode
activation requires an in-window on-grid cell. Legacy deletion keeps its earlier
non-looped future/off-grid admission; manual extent leaves future data dormant.

For a continuous stroke, begin `vibera_gesture` with `domain=stepActivation`.
The lease captures the active Pattern/Channel and permits only canonical
`steps.set` updates there. It preserves recording arm/capture, while a retained
pending take still refuses a new lease. Other commands or targets return
`gesture_domain` without changing earlier valid previews. Updates remain pending;
commit completes one Undo transaction, and cancel restores the starting score.
Selection changes, local preemption or lease expiry cancel through the same
application history implementation. This is a proposed semantic contract;
actual held-input and audio qualification are still required. General document
leases continue to refuse armed recording.

Activation addresses the absolute source beat, independently of loop playback.
Deleting a displayed repetition never silently deletes an earlier source chord.
Left-clicking a source-empty later column creates a note at that later position;
an existing retained chord at that position is recalled with its original IDs.
Automatic loops may shrink when notes are muted and grow on reactivation; the
source address is never remapped through the new period. A fixed loop keeps its
explicit playback extent, so notes beyond that extent stay dormant until the
extent changes or looping is disabled. The keyboard and semantic step operation
use the same source addressing. This supersedes the earlier private modulo/
automatic-refusal proposal, based on the independently inspected installed grid
dispatch/writer path. This development behavior is not in qualified0.0.65;
static evidence does not establish complete interaction parity.

`steps.fill`, named fills and `steps.compose` accept the same typed policy when
advertised. Retained mode mutes off-mask steps, unmutes on-mask steps and keeps
existing chord properties. Basic retained fills require on-grid step notes;
use Advanced Fill's explicit `convertMelodic` consent for melodic/off-grid data.
Opening/unchanged interactive Accept applies no changed recipes and must remain
an exact no-op. Default deletion retains the earlier fill/composition behavior.

The profile uses version10 only when storing this preference or an explicit
macro override. Genuine older profiles migrate to deletion; payloads cannot
hide new fields behind an older version. Macros resolve omission through the
same preference and produce one shared musical Undo. Read the actual macro
service generation/profileVersion and result after save or execution.

The existing step Graph Editor accepts in-window on-grid muted and zero-velocity
notes as property targets. Readback exposes `inactivePropertyEditing=true`, each
visible row's `muted` flag and retained inactive source-note count. Muted bars use
a dimmer treatment. Pitch/velocity edits retain activation; reactivation restores
the updated properties. Off-grid/dormant and automatic-loop repeated graph views
remain outside that existing graph target rule. Ordinary playback, held takes,
cancellation/leases, exact Undo/Redo, save/reopen and PCM qualification remain
required before this proposal can be delivered. Model completion never
establishes audio-thread adoption or a successful file job.
