# Global Swing and Channel Swing Mix

Requires0.0.53; use connected capability discovery, not this document as proof.
Swing modifies compiled score playback in Pattern and Song mode; it does not
rewrite note IDs, positions, lengths or velocities. Audio Clips and live MIDI
monitoring/recorded source data are not delayed by these controls.

Read `vibera_state`: global `swing`, each Channel's `swingMix` and
`truncateSwing`, plus shared Rack selection/active target. All values are
normalized0-1; UI percentages divide by100. New/legacy project defaults are
global0, Mix1, truncationfalse. Schema13 saves all three; old files migrate
without changing their sound. Older VibeRa executables cannot load schema13.

Canonical commands:

- `project.swing` with `value`: global multiplier, all Patterns/Channels.
- `channel.swingMix` with stable `channel,value`: per-instrument multiplier.
- `rack.swingMix` with unique `ids,value`: bulk equivalent; omitted/empty IDs
  use shared Rack selection, including hidden selected Channels. Never row IDs.
- `channel.truncateSwing` with `channel,enabled`, or `rack.truncateSwing` with
  `ids,enabled`: retain original note end when its onset is delayed. One Undo
  per bulk operation. Audio Clip targets/duplicate/stale IDs refuse atomically.

At maximum global and Mix values, alternate sixteenths move0.125 quarter-note
beats; even sixteenths stay fixed. The independent off-grid policy smoothly
warps each0.5-beat pair: delay rises linearly from0 at the pair start to0.125
at its midpoint, then falls to0 at its end. Multiply by global and Channel Mix.
This preserves onset order. Exact installed-FL off-grid math is unobserved.
Truncation preserves unswung note end with a minimum length0.000001 beats;
playback is cropped by Pattern/Playlist source windows after timing. The note
readback's `playbackBeat`, `playbackLength`, `swingDelayBeats` precede clip cropping.
Original fields remain source data. MIDI score-file export remains source timing;
WAV export uses played timing. Swing automation/Burn-to-score are not implemented.

Example: discover target IDs, then exclude a bass from global Swing:

```json
{"command":"project.swing","args":{"value":0.6}}
{"command":"channel.swingMix","args":{"channel":4,"value":0}}
```

Pass each to `vibera_command`;4 is illustrative, replace it from current state.
Use `vibera_inspect entity=notes,pattern=<id>` to verify effective playback times,
and read source data to verify source notes were retained.

Continuous controls use `vibera_gesture action=begin,domain=swing`; retain the
returned operation token. `update` admits only `project.swing` and active
`channel.swingMix` on the original selected Pattern/Channel. This narrow domain
can retain an armed/held MIDI take. Bulk/truncation edits are atomic commands;
generic document leases can batch other edits while not recording. An update is
pending; only commit is completed. Cancel/idle expiry restore the exact baseline.
Unrelated commands fail with `gesture_domain`; target/project change or local
user edit cancels ownership. Poll state after interruption; do not blindly retry.

Save/export require a completed gesture, return pending jobs, and need polling
to terminal completion/failure. Reopen the saved project and read settings/source
notes; verify the actual rendered WAV. Offline success, screenshots and virtual
callback measurements do not establish listening or hardware behavior. Discover
full coverage gaps; this feature does not qualify unrelated legacy operations.
