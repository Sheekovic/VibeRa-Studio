# Channel group solo and mute locks

Discover `channel.solo`, `channel.unsolo`, `channel.muteLock`, `rack.muteLock`
through `vibera_commands(includeSchemas=true)`. Use the shared `vibera_command`
adapter with stable Channel IDs. Read actual groups/membership and mute locks
from `vibera_inspect(entity=channels)`, `vibera_state.channels` and Rack rows.
Do not substitute visible row numbers, the current display filter or bulk
selection for the target's actual group membership.

Successful mute/solo/lock commands return `completionState=completed` with
`completionScope=canonicalChannelMute`, `changed` and full current state. This
acknowledges the canonical document commit; `dspSettlingAcknowledged=false`
does not claim the asynchronous audio projection has already settled. Inspect
audio preparation/health separately. Refusals return structured code/message.

```json
{"command":"channel.solo","args":{"channel":7,"enabled":true,"soloScope":"group"}}
{"command":"channel.solo","args":{"channel":9,"enabled":true,"soloScope":"global"}}
{"command":"channel.unsolo","args":{"restore":true}}
{"command":"channel.muteLock","args":{"channel":12,"enabled":true}}
{"command":"rack.muteLock","args":{"ids":[7,12],"enabled":false}}
```

Scope defaults to `global`, retaining ordinary Ctrl-solo. Group scope uses the
target's actual assigned Channel group; group0 is the valid Unsorted group.
The captured group identity remains explicit. Filtering to a different group
does not change it, and hidden Channels can remain the active MIDI recipient.
These controls operate score mutes, not Mixer muting or arbitrary native plugin
voices; they do not silently retarget live MIDI.

The UI mute-light modifiers are Ctrl for global solo, Alt for group solo and
Shift for mute lock. Combined precedence is **Shift > Alt > Ctrl**; explicit
context-menu actions avoid modifier ambiguity. This is an independent chosen
safety policy. Retained FL documentation/static findings support familiar
solo/group/lock concepts, but exact combined-modifier runtime parity has not
been observed. No proprietary implementation is copied or implied.

A lock protects that Channel's current score mute. Locked targets cannot become
solo targets, and locked peers are excluded from the affected set without being
muted or unmuted. Direct `channel.mute` on a locked target refuses. Bulk selected
mute/unmute skips locks; an all-locked selection is an exact no-op. Lock commands
change the protection flag, not the mute itself. An actual lock change cancels
solo ownership while preserving the current audible mute mask; an equivalent
lock write preserves ownership and its first baseline.

Switching the solo target or global/group scope first restores the previous
affected Channels to the first saved baseline, then applies the new scope.
It does not capture the temporary solo mask as a new original mix. Exit with
`restore=true` restores only affected Channels to that baseline; `restore=false`
unmutes only affected Channels. Locked/out-of-scope peers retain their state.
The existing saved Workflow restore preference applies when `restore` is omitted.
An explicit manual mute ends the restoration session rather than later losing
the newer edit. Undo restores the actual prior mute/lock/scope/baseline state.

Moving a peer into/out of the captured group reconciles affected membership:
leaving peers restore their baseline, joining unlocked peers enter the scope.
Moving the target to another group, deleting its captured group or deleting the
target ends the session and restores surviving affected Channels. Group deletion
still unassigns members; Channel deletion still follows its existing explicit
score/reference cleanup and asynchronous state-capture contract. Poll that real
Channel job to terminal; solo acceptance never substitutes for deletion completion.

`vibera_state` and `vibera_rack(state)` expose `soloChannel`, `soloScope`
(`global`, `group`, or null), `soloGroup` (group ID,0 for scoped Unsorted, or null),
`soloAffected` stable IDs and `soloRestoreMuted` first-baseline muted IDs.
The owner/scope fields distinguish an actual restoration session from a mask
that happens to leave one Channel audible. Each Channel readback includes
`muteLocked`. Inspect authoritative state after every edit/refusal before retrying.

Canonical solo/lock commands are synchronous document actions: actual changes
have one Undo, repeated equivalent actions add no dirty/revision/history edge,
and invalid IDs, scope/type, duplicate/empty members or forbidden locked targets
refuse atomically. There is no invented asynchronous cancellation token for
these edits; use Undo for a committed action. UI/MCP share validation/history.

Schema28 saves locks and active scoped ownership/affected membership so reopening
can restore the original mix correctly. Poll actual `vibera_project` Save/Open
jobs; acceptance alone does not prove persistence. Explicit `soloScope` in typed
saved recipes requires profile8; existing profiles1-7 keep their compatible
global/default behavior. Use the actual `vibera_macros` generation guard and
advertised explicit/current-context command schemas; unknown new fields/scopes
must refuse rather than disappear. Macro configuration is persisted settings,
outside Project Undo; running a saved recipe uses its real musical history.

The focused verifier uses the engineering app's disposable stdio mode, genuine
built-in notes/linked placements, literal mute/lock masks, real saved recipes
and file jobs. Its three actual offline exports compare scoped solo with an
independently applied peer-mute reference, require exact PCM equality, and
require a different mix after restoring that peer. Both comparison edits undo
to the exact reopened document. This establishes offline score behavior;
it does not establish live playback PCM, OS input, physical MIDI/audio,
listening or exact FL parity. Broader RPS-B link-aware cloning/advanced management
and complete module acceptance remain open.
