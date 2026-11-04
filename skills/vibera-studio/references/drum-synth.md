# VibeRa Drum Synth

A built-in synthesized drum kit (no samples), listed with VibeRa Synthesizer as a native
instrument (descriptor `file="vibera.instrument.drums"`, uniqueId 0x56531002). Create it like
any instrument Channel: `vibera_command plugin.instrument.create` with that descriptor XML in
`args.text`, or the Channel Rack instrument menu.

Eight voices, each with tune (semitones), decay (seconds to about -60 dB), tone and level (dB):

| Pad | Drum | Notes | Tone changes |
|---|---|---|---|
| 1 | Kick | 35, 36, 60 | Click and drive |
| 2 | Rim | 37, 61 | Body |
| 3 | Snare | 38, 40, 62 | Snap (noise) |
| 4 | Clap | 39, 63 | Brightness |
| 5 | Closed hat | 42, 44, 64 | Brightness; chokes the open hat |
| 6 | Open hat | 46, 65 | Brightness |
| 7 | Low tom | 41, 43, 45, 66 | Skin (noise) |
| 8 | High tom | 47, 48, 50, 67 | Skin (noise) |

Plus a master level. Parameter ids are 0x30000 + index (drum * 4 + tune/decay/tone/level),
master is 0x30020; they automate and record like other native parameters. State is the
`VIBERA_DRUM_SYNTH` envelope. Notes need a velocity (`notes.add` `value`, 0..1); velocity
scales the hit. The editor's pads play the drum (higher on a pad = louder) and select it
for the four knobs; its display draws the selected drum's shape and the live output.
