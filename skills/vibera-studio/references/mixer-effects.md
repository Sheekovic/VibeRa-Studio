# Mixer effect slots

Unreleased Mixer-module source guidance. Discover the connected application's
tools and command contracts; the qualified0.0.63 package predates these rows.
This increment does not complete MX-E. Unreleased controls require connected-binary discovery and qualification.

Continuity qualification is narrower than document-command availability. Current
unreleased source verifies actual stopped held-sample removal/Undo with an
original SDK effect, real dry-history transfer and worker retirement. Fresh Redo,
playing-transport transitions and general installed-effect continuity remain
unfinished. A removed model row or completed graph publication does not prove
uninterrupted audio, worker exit or resource destruction; verify each result at
its actual scope. Keep asynchronous operations pending until their terminal
readback, and preserve explicit failure results rather than reporting acceptance
as completion. The qualified distribution does not acquire these source changes
until the complete module release transaction passes.

Select a Mixer insert with `vibera_mixer action=select insert=<existing index>`.
Read `vibera_plugin action=list` and `vibera_inspect entity=mixer` for its ordered
effect list. Target the stable plugin `id`; a zero-based `slot` is current order,
never a persistent identity. An empty Add row is not a plugin.

In the Mixer, single-click an effect's name to request its actual native editor.
An existing editor is reused/brought forward by the shared worker service.
The separate power target toggles bypass. The ellipsis/right-click menu offers
native open/close, shared Controls, bypass, Remove and Move up/down for that
specific stable effect. The strip context menu retains the full existing effects menu.

Equivalent semantic operations are:

- `vibera_plugin action=openEditor/closeEditor plugin=<id> instance=<current instance>`;
  poll `action=job operation=<token>` to a real terminal result. See
  [native editors](plugin-editors.md). UI request acceptance is not completion.
- `vibera_select plugin=<id> pluginView=controls` for supported host parameters.
- `vibera_command command=plugin.bypass args={"target":<id>,"enabled":true/false}`.
  `enabled=true` means bypassed. Read `bypassed` from `vibera_plugin action=list`;
  DSP bypass settling is separate from the committed model target.
- `vibera_command command=plugin.remove args={"target":<id>}` removes the effect.
  Bypass and Remove share canonical document validation and Undo/Redo.

Header **+** and the empty **Add VST3 effect** row open Settings > Plugins with
the exact insert selected. `vibera_mixer action=browseEffects insert=<index>`
uses that same synchronous navigation service. It preserves the existing class
selection and scan configuration, starts no scan, inserts nothing and creates
no Undo entry. `completionState=completed` acknowledges browser navigation only.
Read `effectsBrowser.open/targetCurrent/insert/projectIdentity` in its result or
`vibera_mixer action=state`; `vibera_state.mixerEffectsBrowser` is equivalent.
Unknown inserts, active gestures and a project replacement during opening refuse
explicitly. An existing Settings window is retargeted; do not infer its target
from an old selected insert. Then choose an actual effect and **Insert effect**.

For semantic insertion, use [catalogue discovery](catalogue.md) to resolve a
class descriptor, then canonical `plugin.effect.insert` with the explicit Mixer
index and descriptor. Merely opening the browser is not plugin hosting evidence.

The pane scrolls independently with its scrollbar or mouse wheel.
`vibera_mixer action=scrollEffects offset=<zero-based slot>` shares its bounded
navigation state and refuses values above `effectsPane.maximumOffset`.
`effectsPane` reports actual logical pane geometry availability, first visible
slot, maximum offset, visible rows and effect count. Navigation is transient,
does not edit the project, and never opens a native editor. Physical visibility,
native OS input, playback and listening require separate qualification.

For ordering, use the separate six-dot grip or Move up/down in the slot menu.
The name keeps its direct editor action; power and menu remain separate targets.
Dragging previews only an owned UI intent. It changes no project/history/audio
until release over a row. The destination is that row's final zero-based slot;
the Add row means the last existing slot. Wheel-scroll the pane to reach hidden
rows. Release outside or Escape cancels only this intent. Existing active edits,
armed/retained recording and file operations refuse the shared final move
admission for UI and MCP. An unrelated gesture refuses with `gesture_owned`;
retained/capturing MIDI uses `take_pending`, and armed/file/recovery work uses
`busy`. Legitimate updates inside an owned document gesture remain supported.
Macros containing an effect move use the same admission before finalizing edits.
The menu and drag bind exact project identity, revision, insert, source ID/descriptor and
chain order; changed context refuses rather than moving another effect.

Semantic equivalent: `vibera_command command=plugin.effect.move
args={"target":<stable effect id>,"lane":<final zero-based index>}`. Include
current `projectIdentity` and `revision` guards. Destination must exist in the
same chain; no clamping or cross-insert transfer. Omitted lane retains its
existing0 default. Same-slot is a successful no-op with unchanged history,
revision and dirty state. Other moves preserve the whole existing plugin value,
including ID, descriptor, opaque state, bypass and automation bindings, and form
one Undo. Read actual ordered IDs/slots from `vibera_plugin list` and verify
Undo/Redo and saved/reopened state. Instrument/Channel IDs are refused.

`effectsPane.reorder` reports a pending local grip intent with owner/context,
source/destination and current validity. Null means no active intent; it never
proves a move completed. Issue the guarded canonical final move and inspect actual
order. The drag hint is transient presentation; a canonical edit invalidates
an older local intent. Existing `selection.cancel` clears the focused Mixer hint
when no other semantic ownership blocks that action. Contextual selection of a
new insert or project replacement clears it. Do not infer plugin worker/editor,
held-voice, latency or uninterrupted audio adoption from the document result.
Current structural graph updates retain their separate audio qualification.

No arbitrary third-party widgets, replacement, chain copy/presets
or new plugin format support is promised by this increment. Native interaction,
provider compatibility and broader MX-E/module acceptance remain separate.

## Development-only structural effect transitions

The unreleased source connects stopped empty-chain insertion and single-effect
removal to shared Controller UI/MCP transactions when an ordinary prepared worker
source is healthy. Use existing `vibera_command` with `plugin.effect.insert`
(`mixer` stable insert and `text` scanned VST3 descriptor), or `plugin.remove`
(`target` stable effect ID). Checked `edit.undo`/`edit.redo` use the same history.
The currently qualified distribution predates this integration; discover the
connected binary. Integration qualification is still in progress.

When a numeric `operation` is returned, acceptance is pending. Poll
`vibera_mixer action=routingJob operation=<id>` and inspect `routing`.
`stage` distinguishes capture, worker startup, membership, settling and terminal
states. Require `completionState=completed`, `documentCommitted=true` and
`nativeCompletionKnown=true` before claiming joint completion. A failure after
publication may leave the document committed: inspect current project/revision
and the exact historical receipt before retrying.

`vibera_mixer action=cancelRouting operation=<id>` can cancel before graph
publication. Accepted cancellation remains pending until actual capture/startup
or native membership cancellation drains; require `cancelled`. After publication
the cancellation request fails `routing.tooLate`; keep polling the original job,
then use checked Undo. Do not replay an accepted operation blindly. Recent32
terminal numeric receipts are retained; unknown/expired IDs fail explicitly.

Close native editors and finish parameter work, gestures, takes, save/export and
recovery before structural effects. Development structural transactions reuse actual acknowledged unchanged
closed-worker native state only while runtime/revision/schema still match;
modified workers still capture and Save/export always request fresh state. This
avoids pausing every unrelated worker for every structural edit. Qualification
of this capture policy is pending; discover the connected executable.
Missing/unavailable workers produce actual
failure, not a fake effect. First-ever insertion in a sample-only source, occupied
chain edits and playing transport retain existing structural behavior and are
separate required module work. This does not enable chain Paste or qualify
arbitrary tails, installed-provider transitions, hardware or listening. Existing
focused SDK/native held-input PCM/retirement tests remain separate from actual
application workflow qualification.

## Host wet/dry and chain bypass

The lower line of each effect slot has a separate wet knob and readable percentage.
Drag vertically for the full0..100% range over120 logical pixels. Shift gives10x
finer movement. Mouse wheel changes10 percentage points per unit, Shift1 point.
Double-click restores100%; the slot context menu also offers full wet and full dry.
The name, power, reorder grip and menu remain independent. The header power toggle
bypasses the selected chain while retaining each slot setting; orange means chain
bypass is active. Add remains a separate header button.

A drag owns one checked canonical Undo transaction. Escape cancels its preview;
Mixer hide also uses that cancellation path. Other gestures, file/export/recovery
or active non-editor native control work refuse without taking ownership. Armed,
capturing or retained MIDI is preserved. Invalid target/project/revision/owner
contexts cannot redirect a late drag to another slot. A failed history/cancel
operation retains the original UI owner for recovery rather than claiming completion.
`effectsPane.wetGesture` exposes a pending local owner/context validity, distinct
from saved target and DSP progress. A null UI owner never acknowledges audio.

The continuous semantic equivalent is existing `vibera_gesture` with
`action=begin domain=mixerEffects sourceInsert=<index>`, followed by updates of
`plugin.effect.mix` for a stable effect in that chain, then checked commit/cancel.
It also permits `mixer.effects.bypass` on the same insert. This domain alone may
start with a retained MIDI take; document/source editing guards are unchanged.
Use the returned operation token and poll terminal state/expiry. Commands on a
foreign insert or unrelated source/parameter domain refuse. Single wheel/reset/
header changes use the same canonical commands directly, with ordinary Undo.


Unreleased source contract; discover the connected binary before use. These are
host controls, independent of a plugin's native wet/dry parameters. They neither
suspend native processing nor promise reduced CPU usage or arbitrary native UI
control.

- `vibera_command command=plugin.effect.mix args={"target":<effect id>,"value":0.5}`
  sets the linear host wet fraction.0 is latency-aligned dry;1 is fully processed.
  Required finite value is0..1. Instruments/Channel IDs are refused. The default
  is1. Healthy changes ramp over five milliseconds; saved initial values apply
  immediately. Native latency remains present at0.
- `vibera_command command=mixer.effects.bypass args={"mixer":<index>,"enabled":true}`
  overrides every slot's effective bypass. Existing slot bypass flags, wet values,
  native state and automation bindings remain untouched. False restores those
  per-slot targets. Master0 and empty existing chains are supported. Each slot's
  existing five-millisecond bypass transition applies; this is not one equal-power
  whole-chain crossfade. Workers keep processing, including tails.

Include current projectIdentity/revision guards. Both operations share canonical
Controller validation, history, no-op and owned document-gesture cancellation.
They are independent of MIDI take placement and do not finalize a captured take.
An unrelated Controller owner still refuses mutation. Read `wet`, `chainBypassed`
and `effectiveBypass` in `vibera_plugin action=list`; inspect Mixer rows for
`effectsBypassed` and per-slot wet/effectiveBypass. Verify actual order/state with
Undo/Redo and saved/reopened projects. Chain bypass does not mute the Mixer.

Host wet/dry fields were introduced in schema20; genuine versions1..19 migrate with wet1 and chain
bypassfalse. Older-version files containing new fields are rejected. Missing
plugins retain these values and opaque state for recovery. Schema20 omits default
fields; explicit malformed fields are errors, not silently corrected values.

Model command completion is not DSP settling. `vibera_state audio.pluginWorkers`
exposes `wetBlend` (actual last processing-owner wet ramp, null before processing)
and the existing `bypassBlend` (0 wet,1 dry). These separate scalar snapshots are
not a coherent combined audio-frame receipt. Do not infer heard output or graph
adoption. Host mixing, actual graph live/offline/native latency, chain transitions,
installed providers, native desktop interaction and listening require their own acceptance.

## Effect chain spare state

Insert menu > Store effect chain in spare state (`mixer.spareStore`, selected insert) saves the
insert's whole chain (every effect's native state, wet/dry and bypass) as a chain preset file
in the configured chain preset directory. Flip effect chain with spare state (`mixer.spareFlip`)
saves the current chain, loads and applies the spare (one Undo step) and keeps the saved one as
the new spare. Needs a chain preset directory (insert menu > Chain preset directory...). Read
`state.spareStateInserts` and `state.spareStateBusy`.

Insert menu > Add insert from chain preset... creates a new insert and opens the chain preset
chooser to load and apply a saved chain onto it (familiar "Add from state"). MCP: run
`mixer.insert.create`, then `vibera_mixerchain` `loadApply` on the new insert.
