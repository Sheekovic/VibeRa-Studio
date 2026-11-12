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

## Workspace windows

`vibera_editor` places windows like dragging them: `move` `{editor, bounds:[x,y,w,h]}` floats an
editor (0 Rack, 1 Piano, 2 Playlist, 3 Mixer) or the Browser (4) in the workspace (dock a
detached window first); `detach` / `dock` also accept the Browser. `layout` sets the splitters:
`browserWidth` 180..360, `playlistRatio` 0.12..0.8, `rackWidth` 330..900, `mixerHeight` 168..360,
`coProducerWidth` 340..760 (clamped like dragging). Read back `state.editorBounds` (five
[x,y,w,h]) and `state.workspaceLayout`.

Browser folders: `vibera_editor {"editor":4,"action":"browserFolder","path":<absolute folder>,"remember":true}`
opens a folder in the Browser (remember adds it to the saved folders, like the folder chooser);
`browserForget` removes a saved folder (files untouched). Read back `state.browser.folder` and
`state.browser.libraries`.

Browser tags and preview (editor 4): `browserSetTags {"path":<absolute file>,"tags":[...]}` replaces a
file's tags like the Edit tags dialog; `browserTagFilter {"tag":"Drum"}` selects or clears one tag in
the tag filter like clicking it (unknown tags are refused, `browser.unknownTag`); read back
`state.browser.tags` and `.tagFilter`. `browserPreviewSeek {"position":0..1}` restarts the current
sample preview there like Shift+click on the preview waveform (`state.browser.previewReady`,
`.previewPosition`, `.previewSerial`; refused with nothing previewed). `browserTagSearch {"text":"dr"}`
types in the tag list's search box (`state.browser.tagSearch`, `.visibleTags`; empty shows all).

Editor scroll bars: `vibera_editor {"editor":0..3,"action":"scroll","vertical":false,"start":8,"span":16}`
moves the editor's scroll bar thumb like dragging it. Read `state.editorViews[editor].horizontal` /
`.vertical` = [start, span, domain] first: units are the bar's own (Playlist/Piano roll beats and rows,
Rack steps and Channels, Mixer pixels and effect slots). `span` zooms only where the bar zooms
(`horizontalZoom` / `verticalZoom`); start is clamped to the domain. View state, not an Undo step.
