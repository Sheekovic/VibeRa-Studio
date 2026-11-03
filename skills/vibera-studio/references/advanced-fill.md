# Advanced Fill (unreleased source guidance)

Discover `vibera_stepCompose` and canonical `steps.compose` before use. The
qualified0.0.64 distribution predates them. Stable Pattern/Channel IDs are not
display positions. Channel selection within this dialog never changes the live
MIDI instrument. UI access: Channel name context menu or the Rack's selected-
Channel Fill menu > Advanced Fill; `steps.composeDialog` is shortcut-bindable.
The native dropdown includes every playable Rack Channel, initially focusing
the invoking Channel. MCP callers explicitly choose the session target list.

Use `vibera_stepCompose action=begin` with explicit `pattern`, `channels` and
optional `leaseMs` (250..120000, default30000). It returns a pending `operation`
token, active draft, recipes and final masks. Opening does not alter the score,
loops, allocator, history or dirty state. Finish recording/other document leases
first. A remote session expires after inactivity; query does not renew it.

Submit `action=update`, the exact token and `recipes`. Each row replaces that
Channel's whole draft; omitted recipe fields use discovery defaults, so use the
returned canonical fields for editing an existing draft. Strip read-only `name`
and `mask`. Other Channel drafts remain. Select a draft with `action=select` and
`channel`. Reset current/all restores its opening draft, not a fabricated rhythm.

Recipe units and musical policy:

- `sequenceSteps`1..4096; one sixteenth step is0.25 quarter-note beats.
- Manual needs exactly that many0/1 values. Every-N starts at cell0. Euclidean
  places `pulses` hits at floor(k*steps/pulses), including zero hits. This is an
  independently defined algorithm; exact FL internal algorithm parity is unproven.
- Deterministic `dice` redistributes the generated hit count with unsigned32
  `seed`, then `rotation` shifts cells (positive later, modulo sequence).
- `loop=true` sets a fixed per-Pattern Channel cycle. `false` removes that loop;
  retained notes beyond the replacement window may become audible. It does not
  toggle transport Loop. Existing automatic loop modes stay exact if unchanged;
  changing that Channel's recipe explicitly chooses fixed/off behavior.
  Opening draft defaults to true, so newly shortened rhythms repeat automatically;
  unchanged drafts are never applied and preserve existing loop metadata exactly.
- Replace only source notes starting in `[0,sequenceSteps*0.25)`. Outside notes
  and unrelated Channels survive. Surviving on-grid steps retain IDs, chords,
  pitch, length, velocity and mute. New hits use pitch60/velocity0.8/length0.25.
- In-cycle melodic/off-grid notes require `convertMelodic=true`; conversion floors
  onset to a cell and sets step length0.25. No silent conversion. Manual Pattern
  windows stay fixed; Auto extent follows the existing musical extent contract.
- Initial drafts display at most4096 cells. Longer source windows remain intact
  outside that range; opening/accepting unchanged drafts never truncates them.

`action=preview,enabled=true` auditions changed recipes relative to the immutable
opening score. Repeating an update is absolute, not additive. Preview off restores
the opening score but keeps drafts and any temporary Solo. `play` toggles the current Pattern/Song
transport, without changing playback mode, target or musical position policy.

`solo,enabled=true` auditions the selected draft Channel through canonical mute
and lock rules, independently of score Preview. `select` transfers Solo to that
Channel; a locked target refuses atomically. `solo,enabled=false`, Cancel, expiry
and Accept restore the opening mute/solo state. Temporary audition is never part
of the committed history. Read `solo` and actual Channel mute state to verify.

`dice` advances the active draft seed modulo2^32 and enables deterministic Dice;
repeated calls can produce the same mask when the rhythm has few possibilities.
`manual` freezes its final post-Dice/rotation mask, clearing those transforms
without changing hits. UI offers these same actions plus a circular step view
and a long-sequence grid. Left paints, right erases, including drag; numeric
rotation and the center knob update the same recipe. No pointer simulation is
needed for equivalent semantic edits: submit the resulting Manual recipe.

`commit` applies changed recipes and completes one canonical Undo transaction;
unchanged drafts are a no-op, including unconverted melodies. `cancel`, Escape or
window close restores the exact opening state without a history entry. Failed
validation leaves the previous draft/preview unchanged. A failed history commit
must stay pending/retryable/cancellable; never infer success from acceptance.
Project/owner replacement cancels without restoring a stale project baseline.

Read `completionState`, `operation`, `reason`, `preview`, `dirtyRecipes` and the
actual score/Channel loops. Completion acknowledges document/history, not DSP
adoption: `dspSettlingAcknowledged` is false; use global `pendingAudioUpdate` and
audio diagnostics separately. Save/export only after Accept/Cancel; poll actual
file jobs to terminal outcomes and verify the persisted/rendered result.

For one atomic non-dialog edit use `vibera_command` with
`{"command":"steps.compose","args":{"pattern":123,"recipes":[...]}}`.
Canonical UI/MCP validation/history are shared. Macros retain explicit recipe
Channel IDs; current context may retarget the Pattern only. Recipe profiles need
version9; earlier profiles remain readable and do not gain recipes silently.
No arbitrary native-plugin widgets are controlled by this tool.
