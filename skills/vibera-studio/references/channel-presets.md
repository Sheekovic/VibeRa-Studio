# Channel presets

A Channel preset (`.vibch`) stores one score Channel's sound: name, sample
source (absolute path or `builtin:*`), gain, pan, all sampler settings, Swing
Mix/truncation, colour and icon, plus, for instrument Channels, the native/VST3
instrument descriptor and its state captured fresh from the running plugin when
saved from the UI. It never stores score, Mixer assignment, group, mute/lock
state, controller links, automation or plugin automation bindings. Audio Clip
Channels are refused (`channelPreset.unsupportedTarget`).

Applying an instrument preset gives the target a new plugin ID with that state
(a structural change: a brief interruption while the instrument starts; Undo
may answer `pending` and complete shortly after). Applying a sampler preset to
an instrument Channel removes its instrument. A VST3 missing on this machine
becomes the usual recoverable missing-plugin placeholder.

UI: right-click a Channel name in the Channel Rack and pick Save Channel preset...
or Load Channel preset.... The default folder is
Documents/VibeRa Studio/Channel presets. When a preset's absolute sample path is
missing, a sample with the same file name beside the preset file is used.

MCP: apply with the canonical command; `text` is the complete preset XML.

```json
{"command":"channel.preset","args":{"channel":2,"text":"<VIBERA_CHANNEL_PRESET format=\"1\">...</VIBERA_CHANNEL_PRESET>"}}
```

The payload is `<VIBERA_CHANNEL_PRESET format="1">` wrapping one complete VibeRa
project tree with exactly one sampler Channel; it gets the same validation as a
project file. Applying is one Undo step and preserves the target's ID, Mixer
insert, group and mute. Refusals leave the revision unchanged:
`channelPreset.decodeFailed`, `channelPreset.unsupportedFormat`,
`channelPreset.missingChannel`, `channelPreset.missingSample`.

MCP file jobs: `vibera_project` `{"action":"savePreset","channel":3,"path":"C:/x/pad.vibch"}`
(add `overwrite:true` to replace; instrument Channels capture fresh native state, so
poll `{"action":"job","operation":...}` while pending) and
`{"action":"loadPreset","channel":2,"path":"C:/x/pad.vibch"}` (one Undo step; a
missing sample is looked up beside the preset file).
