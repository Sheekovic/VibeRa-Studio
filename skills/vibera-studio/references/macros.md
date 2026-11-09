# Macro knobs

A macro knob is one 0..1 value that drives several targets at once, each through its own
range (value at knob 0 and at knob 1; inverted ranges allowed). Targets are Mixer and
Channel controls (the Mixer automation target fields) and plugin parameters (binding).
It plays through the automation program in Song, Pattern and export. A target cannot
also have an Automation Clip, a modulator or another macro. Schema 57; one Undo each.

- `automation.macro.add` (`text` name), `automation.macro.set` (`target` macro, `value`
  0..1), `automation.macro.rename`, `automation.macro.delete`.
- `automation.macro.link`: `ids` [macro] plus `mixer`/`automationControl`/... ; `text`
  "range" with `beat` (value at 0) and `length` (value at 1), otherwise the full range.
- `automation.plugin.macro.link`: `target` plugin, `binding`, `ids` [macro].
- `automation.macro.unlink`: `target` macro, `value` target index.
- Readback: `vibera_inspect {"entity":"macros"}` with each target's compiled `currentValue`.

UI: Mixer control right-click > Macro knob (link / new macro), Tools > Macro knobs...
