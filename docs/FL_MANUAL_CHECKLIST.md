# FL manual item checklist

Familiar-workflow items taken from the FL Studio manual pages for the Piano roll, Channel
Rack, Playlist and Mixer, checked against the VibeRa source. Status: **done** (command,
UI and readback exist), **partial**, **missing**, or **out of scope** (depends on FL-only
plugins or architecture VibeRa does not copy). Behaviour is implemented independently.

## Piano roll

| Item | Status | Notes |
| --- | --- | --- |
| Draw/paint/slice/mute/select/zoom tools | done | `piano.tool*` |
| Ghost channels, editable ghosts | done | `piano.ghosts`, `piano.editGhosts` |
| Slide and portamento notes | done | `piano.insertSlide`, `piano.insertPortamento` |
| Note colours, select by colour | done | `piano.changeColour`, `piano.selectColour` |
| Quantize, chop, strum, arpeggiate, flam, randomize, limit, LFO, scale levels, articulate, riff, glue | done | notes.* tools |
| Note grouping, trim, scripts (JavaScript) | done | `notes.group`, `notes.trim`, `notes.script` |
| Keyboard labels (none / root / white) | done | `view.keyLabels*` |
| Note grid highlights | done | `view.noteGridHighlights` |
| Auto-scroll with the playhead, centred follow | done | `view.pianoFollow`, `view.followCenter` |
| Score sheet export, MIDI export | done | |
| Background waveform of audio clips | missing | |
| Preview notes during playback | missing | |

## Channel Rack

| Item | Status | Notes |
| --- | --- | --- |
| Step sequencer, graph editor, swing, fill each N | done | |
| Volume / pan knobs, channel automation clips | done | `channel.automateVolume/Pan` |
| Clone, layer, transpose, filter groups | done | |
| Channel button width | missing | |
| Focus selected channel | missing | |
| Patcherize, DirectWave, Slicex | out of scope | FL-only plugins |

## Playlist

| Item | Status | Notes |
| --- | --- | --- |
| Clips, slicing, slip, consolidate, make unique, merge similar | done | |
| Markers (add every N, pairs), time signatures | done | `marker.*` |
| Clip grouping, track grouping, performance mode | done | |
| Track and track-group export | done | `exportTracks`, `exportTrackGroups` |
| Auto-scroll, centred follow | done | `playlist.follow`, `view.followCenter` |
| Mini playlist / track separators | missing | |

## Mixer

| Item | Status | Notes |
| --- | --- | --- |
| Inserts, sends, sidechain, route selected, submix, trim unused | done | `mixer.*` |
| Dock, reset, track state save/open | done | |
| Touch recording of fader/pan/send | done | |
| Polarity, stereo separation, swap L/R | done | |
| Multichannel routing | partial | |
| Disk recording (render armed tracks) | missing | |
| Smart disable | missing | |
| Link all parameters | missing | |
