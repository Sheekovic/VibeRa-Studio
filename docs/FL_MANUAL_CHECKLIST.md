# FL manual item checklist

Familiar-workflow items taken from the FL Studio manual pages for the Piano roll, Channel
Rack, Playlist, Mixer and Browser, checked against the VibeRa source. Status: **done** (command,
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
| Background waveform of audio clips | done | `view.pianoBackgroundWaveform` |
| Preview notes during playback | done | `view.previewDuringPlayback` |

## Channel Rack

| Item | Status | Notes |
| --- | --- | --- |
| Step sequencer, graph editor, swing, fill each N | done | |
| Volume / pan knobs, channel automation clips | done | `channel.automateVolume/Pan` |
| Clone, layer, transpose, filter groups | done | |
| Channel button width | done | `view.rackButtons*` |
| Focus selected channel | done | `view.rackFocusSelected` |
| Patcherize, DirectWave, Slicex | out of scope | FL-only plugins |

## Playlist

| Item | Status | Notes |
| --- | --- | --- |
| Clips, slicing, slip, consolidate, make unique, merge similar | done | |
| Markers (add every N, pairs), time signatures | done | `marker.*` |
| Clip grouping, track grouping, performance mode | done | |
| Track and track-group export | done | `exportTracks`, `exportTrackGroups` |
| Auto-scroll, centred follow | done | `playlist.follow`, `view.followCenter` |
| Mini playlist (song overview) | done | overview in the Playlist scrollbar |

## Mixer

| Item | Status | Notes |
| --- | --- | --- |
| Inserts, sends, sidechain, route selected, submix, trim unused | done | `mixer.*` |
| Dock, reset, track state save/open | done | |
| Touch recording of fader/pan/send | done | |
| Polarity, stereo separation, swap L/R | done | |
| Multichannel routing | done | `channel.outputRoutes`, multi-output instruments to Mixer tracks |
| Disk recording (render armed tracks) | done | `mixer.renderArm`, `mixer.renderArmed` |
| Smart disable | done | VibeRa effects; `effectSmartDisable` |
| Link all parameters | done | `mixer.linkAll` (track volume, pan, separation to MIDI CCs) |

## Browser

| Item | Status | Notes |
| --- | --- | --- |
| Folder tree with user-added folders, back / forward / up history | done | `browser.back/forward/up` |
| Search with next / previous result, clear filter, case-insensitive | done | `browser.find`, `browser.nextMatch/previousMatch`, `browser.clearSearch` |
| Save search result as tab, rename / delete tab | done | Browser menu |
| Starred tab, add / remove starred | done | `browser.showStarred`, `browser.toggleFavorite` |
| Tags: add, edit, filter, Any / All matching | done | `browser.addTags/editTags/matchAllTags/clearTagFilters` |
| Preview: short and full sample preview, play to end, stop, preview volume | done | `browser.playPreview/fullPreview/stopPreview/previewLouder/previewSofter` |
| Reread folders, indexing | done | `browser.reindex` |
| Current project view (samples, instruments, effects) | done | Browser Project view |
| Send to selected Channel, open in new Channel, drag to Channel / Playlist / Mixer | done | file right-click menu |
| Show in folder (Explorer), delete file | done | right-click: Show in folder, Move to Recycle Bin (confirmed, never permanent) |
| Wildcard and AND / OR / NOT search | done | `*`, `?`, AND / OR / NOT, parentheses and quoted text (BrowserQuery) |
| Sort results by name / type / date | done | Browser tabs menu > Sort results |
| Alternative view layout (columns: name, type, size, date) | done | Browser tabs menu > Show results as columns |
| Folder colours / icons, frozen tab layouts, FL Cloud and online content | out of scope | FL-service and cosmetic items |

## Second pass (2026-11-09, pianoroll_menu / pianoroll / channelrack / chansettings / playlist pages)

Each manual item was compared with the command registry. Rows below are the ones the first pass
did not list.

| Item | Status | Notes |
| --- | --- | --- |
| Playlist: Automation Clip articulator tools (flip, normalize, smooth, scale levels) | done | `automation.source.flip/normalize/smooth/scale` |
| Playlist: Merge automation clips | done | `automation.merge` (keeps the first clip's links) |
| Playlist: ripple editing (close the gap) | done | `clip.rippleDelete` |
| Playlist: marker actions (loop, skip, pause, start) | done | `marker.action`; real-device check: Start 4, Loop 8..16, Skip 20 to 28, Pause at 32. Skip/Pause act on the 20 Hz timer. Quantized song jumps: missing |
| Piano roll: snap incoming MIDI to scale | done | Piano roll menu, `view.snapIncomingMidi`, `vibera_editor` guide; hardware and typing keyboard |
| Piano roll: Interpolate tool (event data) | missing | next |
| Piano roll / Playlist: nudge with mouse wheel, Center (Shift+0) | done | Shift+Alt+wheel nudges selected notes/clips (pointer only, not MCP-tested); `view.centerPlayhead` Shift+0 |
| Piano roll: Magic lasso selection | missing | |
| Piano roll: time / key markers in Pattern mode | missing | |
| Channel Rack: Mute removed steps | done | existing Workflow setting (`retainSteps`, muted notes kept) |
| Channel Rack: Receive notes from (channel to channel) | missing | |
| Channel Rack: declicking mode, Restretch all, name alignment, auto resize | out of scope | FL sampler/display specifics |
| Loop Starter, FL Cloud, Patcherize, DirectWave, NewTone/NewTime, stem extraction, Deverb | out of scope | FL-only plugins and services |
| View cosmetics (grid colour/contrast, keyboard styles, note shadows, Behind clips) | out of scope | theme handles appearance |
