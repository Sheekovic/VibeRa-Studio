# Reorder Mixer inserts without changing their identities

Unreleased Mixer-module guidance. Discover `mixer.move`, `mixer.moveLeft` and
`mixer.moveRight` with `vibera_commands includeSchemas=true` before using them.
Registration/source guidance is not release, listening or hardware qualification.
Discover insert lifecycle and group operations separately; this guide covers
reordering existing inserts with stable identities.

Read `vibera_inspect entity=mixer`: `index` is a stable ID; `position` is its
zero-based display row. `vibera_state mixerOrder` / `vibera_mixer action=state`
`order` report actual order, selected source ID and boundary availability. Master
has ID0 even after rename and its CURRENT display row is reserved. Never use an
ordinal as an identity or assume sparse IDs are contiguous.

Use `vibera_command command=mixer.move args={mixer:7,mixerPosition:3}` to move
existing ID7 to final row3. Other non-Master values shift; Master stays fixed,
including in a nonleading legacy row. Position must exist and cannot be the
Master row. Absolute invalid input fails without clamping. `mixer.moveLeft` and
`mixer.moveRight` take only `{mixer:7}` and skip the reserved Master row; a boundary
is a successful `changed:false` no-op without revision/dirty/history change.

A successful discrete move is one Undo. Save/reopen keeps order; Channel
assignments, send source/destination IDs, selected source, effects/opaque state,
automation and controller targets retain their stable IDs. Read actual returned
`mixerOrder` and inspect routes/effects, then verify Undo/Redo. Return
`completionState:completed` with `completionScope:canonicalProjectOrder` means
the canonical move finished; it does not acknowledge heard sound or DSP adoption.
Check existing pending-audio/status fields and relevant audio qualification.

After `vibera_project` Open completes, the document is available while audio
projection may still be preparing. Observe `vibera_state pendingAudioUpdate`
until false before a capture/routing operation; do not repeatedly submit a
mutation against a busy owner. A structural routing command may itself return
`pending`: follow that exact operation using `vibera_mixerchain action=job` until
completed/failed/cancelled before dependent work. File/document jobs and chain
jobs have different owners. A completed document load or admitted route is not
an acknowledgement of heard playback. Preserve existing job/time budgets and
report readiness failures rather than retrying until a mutation succeeds.

For multiple moves, an unarmed owned `vibera_gesture domain=document` groups
updates into one history action. Update with canonical commands and arguments;
commit finishes one Undo, cancel restores the exact baseline and dirty state.
Document leases remain unavailable during capture/retained takes. Discrete order
moves themselves retain armed/capturing/retained MIDI and never finish or cancel
the take. Shared file/device/routing ownership and unrelated gestures refuse.
Foreign/unqualified history can also refuse; preserve the take and inspect the
structured error. Do not clear user history as a recovery strategy.

The same commands are available for custom shortcuts and document macros. Macro
`parameters.mixerPosition` is an exact integer0..128 for `mixer.move` only;
relative commands omit it. `currentContext:true` targets the selected stable
insert. A macro is one Undo and cancellation restores its baseline; an invalid
later step must roll back earlier moves. This is an optional field within the
existing profile1..3 formats; the existing export-version rules remain unchanged.
Older profiles without it still import. Named macro/shortcut management retains
its inherited semantic MCP gaps; use discoverable canonical order operations or
an owned semantic gesture/batch for AI execution. No executable-bound references regenerate
until the module release boundary.

UI defaults: Mixer focus, Alt+Left/Right moves the selected insert. Right-click
a strip name for Move left/right. Shift+wheel OVER A STRIP NAME moves that hovered
insert without changing the selected source/send target; positive vertical wheel
goes left, negative right, one event/one step. Shift-wheel pan/send retains fine
control behavior; effects pane and scrollbar targets do not reorder inserts.
Menu cancellation does nothing; stale project/revision/order menu intents fail.

Reference provenance: the current official [FL Mixer manual](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/mixer.htm)
documents Alt+Left/Right and Shift+wheel track reorder preserving links. This is
documented behavior, not direct observation of installed FL24.2. VibeRa's name-row
wheel target deliberately preserves its existing fine pan/send wheel controls.
All implementation and assets are original.
