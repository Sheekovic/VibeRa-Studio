# Modulators (LFO / random)

A modulator moves one control continuously without an Automation Clip: a tempo-synced
LFO or random source centred on the control's manual value (or its initialized value)
and clamped to the control's range. It plays in Song and Pattern mode and in exports.
Project schema 57; one Undo step per command.

## Commands (vibera_command)

- `automation.modulator`: Mixer or Channel control. Target fields as in Mixer automation:
  `mixer`, `automationControl` (gain, pan, stereoSeparation, send with
  `automationSendTarget`, channelGain / channelPan with `channel`).
- `automation.plugin.modulator`: plugin parameter. `target` = plugin ID, `binding` = the
  parameter binding (same object as native plugin automation); a missing binding is declared.
- Shared fields: `text` shape (sine, triangle, sawUp, sawDown, square, random,
  smoothRandom), `value` period in beats per cycle (0 = 4; 1/64..1024), `strength`
  depth 0..1 (peak-to-peak swing as a fraction of the range; default 1), `offset`
  phase 0..1.
- `automation.modulator.set` (`target` = modulator ID plus all shared fields),
  `automation.modulator.enable` (`target`, `enabled`), `automation.modulator.delete`.

Refusals: a second modulator on one control (`modulator.duplicateTarget`), a control
that an Automation Clip links (`modulator.targetAutomated`), invalid shape, period,
depth or phase. Deleting a Channel or Mixer insert removes its modulators; removing a
plugin binding or replacing an effect requires removing the modulator first.

## Readback

`vibera_inspect {"entity":"modulators"}` lists id, name, target (kind, owner,
parameter), range, shape, period, depth, phase, enabled, `active` (compiled into the
playback program) and `quarterCycleValues`: the compiled value at 0, 1/4, 1/2 and 3/4
of the first cycle. These are model values, not listening evidence.

UI: right-click a Mixer fader, pan, separation or send > Modulator (LFO); plugin
Controls window > Modulator...
