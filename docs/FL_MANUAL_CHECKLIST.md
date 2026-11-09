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
| Playlist: marker actions (loop, skip, pause, start) | done | `marker.action`; real-device check: Start 4, Loop 8..16, Skip 20 to 28, Pause at 32. Skip/Pause act on the 20 Hz timer |
| Piano roll: snap incoming MIDI to scale | done | Piano roll menu, `view.snapIncomingMidi`, `vibera_editor` guide; hardware and typing keyboard |
| Piano roll: Interpolate tool (event data) | done | `notes.controller.interpolate`; Piano roll Edit > Interpolate events in the time selection (menu, not the drag tool) |
| Piano roll / Playlist: nudge with mouse wheel, Center (Shift+0) | done | Shift+Alt+wheel nudges selected notes/clips (pointer only, not MCP-tested); `view.centerPlayhead` Shift+0 |
| Piano roll: Magic lasso selection | done | Piano roll Menu > Magic lasso (draw around notes; Shift includes ghost Channels); `vibera_selection` action lasso. Drawing is pointer-only, the selection function is MCP-tested |
| Piano roll: time markers in Pattern mode | done | `pattern.marker.add/rename/delete`, Piano roll Menu > Pattern markers; drawn on the ruler. Key markers: use the scale guide; time signature: Pattern meter |
| Channel Rack: Mute removed steps | done | existing Workflow setting (`retainSteps`, muted notes kept) |
| Channel Rack: Receive notes from | done | `channel.receiveMidi` (MIDI channel 1..16 per Channel, multitimbral live input); right-click > Receive notes from. Cut self already existed (Cut itself) |
| Channel Rack: declicking mode, Restretch all, name alignment, auto resize | out of scope | FL sampler/display specifics |
| Loop Starter, FL Cloud, Patcherize, DirectWave, NewTone/NewTime, stem extraction, Deverb | out of scope | FL-only plugins and services |
| View cosmetics (grid colour/contrast, keyboard styles, note shadows, Behind clips) | out of scope | theme handles appearance |
| Playlist: Quantize song jumps (Off, beat, 1/2/4/8 bars) | done | Playlist menu, `vibera_transport` jumpQuantize/jump; real-device check: jump at 1.06 waits for 4, lands at 16 |
| Expression maps: CC / program change articulations | done | `Name=CC32:64`, `Name=PC5`; sent 0.1 ms before the note |

## Third pass (2026-11-09, playlist_patterns / playlist_audioclip / playlist_automationclip / playlist_performance / menu_patterns / stepsequencer_advancedfilltool / pianoroll_chordprogression / pianoroll_riff pages)

| Item | Status | Notes |
| --- | --- | --- |
| Automation Clip options: Decimate points | done | `automation.source.decimate`, Playlist menu > Decimate points; playlist_tools_mcp |
| Audio Clip: Make unique as sample (copy the file on disk) | done | `clip.uniqueSample`, clip menu > Make unique as sample; unique_sample_mcp |

## Fourth pass (2026-11-09, chansettings_misc / chansettings_sampler / chansettings_ins / chansettings_main / menu_tools / menu_add / menu_edit / toolbar_panels pages)

Most items already exist under VibeRa names (arpeggiator, echo, cut groups, trackers, gate, porta/mono,
ping-pong loop, reverse, smart disable, score log, half speed, split by channel, last tweaked). Gaps:

| Item | Status | Notes |
| --- | --- | --- |
| Recording: Wait for input (start on the first MIDI note, keeping that note) | done | `transport.waitForInput`, Tools > Wait for input; the triggering note is captured at the take start (musical_acceptance capture checks); not hardware-qualified |
| Master pitch (global tuning offset for every instrument) | done | `project.masterPitch` -1200..1200 cents, Tools > Master pitch; sampler, MPE, MTS-ESP; master_pitch_mcp checks 440/880/466 Hz in exports |
| Audio logger (rolling master-mix buffer, save the last minutes) | missing | real-time-safe preallocated ring buffer on the master output |
| Recording filter (choose automation / notes / audio / clips) | done | Tools > Recording filter: Notes (`transport.recordNotes`), Audio (audio input switch), Automation (armed on its control); record_filter_mcp |
| Sampler envelope presets | missing | Channel presets (.vibch) already save full settings |
| Keep on disk / declicking modes | out of scope | FL sampler streaming and declick specifics |
