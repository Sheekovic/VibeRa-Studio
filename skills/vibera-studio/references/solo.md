# Channel solo, score mute and live input

Requires0.0.50. Discover channel.solo/channel.unsolo/channel.mute and
rack.muteSelected/rack.unmuteSelected with vibera_commands(includeSchemas=true).
Use stable Channel IDs from vibera_state, never visible row positions. Read
vibera_rack(state) for soloChannel, hasSoloSnapshot, soloRestoreMuted, audibleSoloChannel,
restoreMuteAfterSolo, selectedChannels, activeChannel and per-row muted/soloed.
The snapshot lists originally muted IDs; missing IDs in that list were unmuted.
Filtering never limits the default global solo scope: all project Channels are
included. Unreleased group scope and mute locks extend this historical baseline;
discover support first and follow [scoped solo](channel-scoped-solo.md). Locked
Channels are excluded from automatic changes and direct locked mute/solo refuses.

```json
{"command":"channel.solo","args":{"channel":7,"enabled":true}}
{"command":"channel.solo","args":{"channel":7,"enabled":false}}
{"command":"channel.unsolo","args":{"restore":true}}
{"command":"channel.mute","args":{"ids":[7,9],"enabled":true}}
{"command":"rack.muteSelected"}
```

These are vibera_command arguments. True sets a solo target idempotently;
false leaves that target's solo. UI Ctrl-click toggles using the same state;
right-click the mute light offers the same commands. Switching solo targets
retains the first baseline. Unsolo normally restores it. A manually isolated
single audible Channel can unsolo even without a saved baseline, unmuting all.
Manual mute/bulk edits end the explicit restore session while retaining the
current mask before applying the requested edit. Undo recovers that session.
This manual-edit rule is an explicit VibeRa behavior, not verified FL runtime parity.

```json
{"action":"configure","restoreMuteAfterSolo":false}
```

Send that to vibera_rack. Discover the stable operation rack.soloRestore with
vibera_commands(includeSchemas=true) for its typed service invocation and readback.
It is the Settings > Workflow checkbox's canonical service; a remapped shortcut
toggles the same persisted preference. Read the actual returned preference; persistence completes before
in-memory settings change. The preference is global to this app profile,
initially true; false makes Unsolo unmute all. An explicit restore argument on
solo exit overrides it. Preferences do not consume musical Undo or revision.
Test-mode instances use owned disposable profiles and never edit user settings.
A persistence_failed error leaves both current/saved preferences unchanged.

Channel mute filters committed score, not the instrument's complete output:
live MIDI/typing stay playable, including a hidden active recipient. Existing
Channel volume/pan and Mixer routing still affect live input; Mixer mute silences
its complete routed output. Muting sends normal score note-offs; release/effect
tails may decay. Plugin autonomous audio is not universally silenced by score mute.

The hosted MIDI1 transport shares pitch/channel articulation between score,
physical and typing sources. Source releases cannot cancel another held owner.
It does not promise independent same-key note IDs, per-source sustain/pitch-bend
or arbitrary native-plugin widget control. Real installed-provider tests are
bounded evidence, not universal compatibility or listening/hardware acceptance.

Read vibera_state.audio.channels[].heldInputKeys for callback-observed MIDI/typing
keys (including pedal-held keys), and scheduledNotes for score counts. These
snapshots require callbacks to advance. They do not establish physical input,
sounding plugin voices, device health or successful listening. Do not confuse
scheduled counts with held keys.

Solo target/baseline persist in project schema12; schema11 and older migrate in
memory without rewriting their files on load. Older applications reject new
projects. Save original projects deliberately. Canonical edits and macros have
normal one-step Undo/Redo; document replacement invalidates old IDs/menus.
Invalid/stale/duplicate targets reject atomically with structured errors. Use
projectIdentity/revision guards and verify actual masks before saving/exporting.
Saving/exporting remain async: poll vibera_project(action=job) until completed,
failed or supported cancellation. Accepted/pending is never completed. Group/Alt
solo, mute locks, Playlist/Mixer solo and broader FL parity remain unfinished.
