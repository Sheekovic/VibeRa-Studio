# Keyboard shortcuts

Shortcuts live in the same Customization profile as saved macros, so `vibera_macros` edits them,
guarded by the profile `generation` (read `state` first; every change returns the next one). These
are local preferences, not Undo steps. Same calls as Settings > Shortcuts.

- `vibera_macros {"action":"state","includeShortcuts":true}` lists every shortcut:
  `command`, `scope` (0 global, 1 Rack, 2 Piano, 3 Playlist, 4 Mixer, 5 Browser, 6 Patterns,
  7 Sampler), `key` (JUCE key code; letters upper-case), `ctrl`, `shift`, `alt`, `text`.
- `bindShortcut` with `shortcut` `{command, scope, key, ctrl, shift, alt, alternate}`: replaces the
  command's shortcut in that scope, or adds a second one with `alternate:true`. A key used by another
  command in the same scope is refused with `shortcut.conflict`; an unknown command with
  `shortcut.invalid`.
- `unbindShortcut` with `shortcut` `{command, scope}` removes the command's shortcuts in that scope.
- `resetShortcuts` restores the familiar defaults and keeps saved macros.

- `exportProfile` `{path, overwrite}` writes shortcuts and macros to an absolute .json file (an
  existing file needs `overwrite:true`); `importProfile` `{path}` replaces them from one (refused
  if the file is missing or invalid).

Shortcut actions always return `shortcuts`. Run a shortcut through the real handler with
`vibera_key` (diagnostic); prefer the canonical command.

Panels: `vibera_command view.browser` and `view.coProducer` toggle the Browser and CoProducer
panel; read back `state.browserShown` / `state.coProducerShown`.

## Appearance

`vibera_appearance {"action":"state"}` lists `font`, every colour role (`colours`, AARRGGBB) and
`presets`. `set` applies `preset` (`default` keeps the font, or a listed palette), `font` (an
installed typeface) and `colours` `{role: AARRGGBB}` live; `save:true` keeps it after restart
(`saved`). `reset` restores the default palette and font. Local preference, not Undo.
