# Named buses and direct Mixer controls

Unreleased Mixer-module guidance; requires discovery of these commands, not
just the application version. Discover the connected executable's
schemas before acting. Stable Mixer indices differ from Channel/Playlist IDs;
Master is0 even after rename. Read `vibera_inspect entity=mixer` and
`vibera_mixer action=state` for names, colours, gain/pan/mute, route availability,
solo ownership/baseline and actual signal meters.

Create a coherent drum bus using existing inserts: `mixer.rename
args={mixer:5,text:"Drum bus"}`; assign drums to individual inserts using
`channel.mixer`, then `mixer.routeOnly args={mixer:1,target:5}` (repeat for other
drum sources). Do not add an accidental dry Master path. Colour with
`mixer.colour args={mixer:5,value:4281515426}` (opaque ARGB integer);0 follows
the theme. Names trim whitespace,1..64 characters, no tabs/newlines. Naming and
colour are document history and persist; selection is workspace-only.

F2/middle-click opens Mixer naming with the text selected and ready for typing;
precise numeric dialogs likewise select their value. Escape cancels; Apply uses
the same canonical validation/history as semantic commands.

Unreleased host source smooths healthy runtime bypass target changes with a
five-millisecond linear blend between wet and latency-aligned dry (silence for
instruments). Initial bypass and failed-worker fallback are immediate. The model
target commits before DSP necessarily settles. Discover `vibera_state`'s output
schema and read `audio.pluginWorkers[].bypassBlend` (null before processing,0 wet,
1 bypassed) and `bypassTransitionSamples`; do not infer heard sound from readback.
`auxiliaryInputBus`/`auxiliaryInputChannels` describe only the actual negotiated
layout, and `auxiliaryRejected` is a sticky delivery error. Backend mono/stereo
detector transport and fixed graph-alignment experiments do **not** expose a
working Mixer sidechain routing workflow; project connections, canonical commands,
UI and live latency transitions remain open. Do not invent a connection command
from the low-level readback. Native prepared arrangements are independently checked
before DSP; unsupported or changed layouts fail instead of pretending readiness. These fields
are unavailable in the unchanged qualified executable until a module release.

Development IPC10 also exposes `latencyDeclared` (nativeSamples/generation) and
`latencyConsumed` (same fields plus sequence, or null). Generations belong to the
worker instance; consumed identifies actual wet PCM and its aligned bypass tap.
`latencySamples` includes one IPC quantum plus the latest native declaration.
None of these fields acknowledges graph compensation adoption or proves seamless
latency changes. The historical constant-signal gap is fixed in a bounded opt-in experiment;
production history remains off pending phase/MIDI/admission/output qualification. Poll control operations to terminal independently and
verify actual audio/graph evidence. These fields are unreleased, not available in
the qualified distribution. No arbitrary native UI or hardware control is implied.

`mixer.pan args={mixer:1,value:-0.5}` uses existing engine stereo balance -1..1;
0 centre. It does not promise cross-channel mono summing. `mixer.gain` is linear
0..2,1 unity. Automation can override playback gain, so verify audio status and
signal meters separately. `mixer.mute args={mixer:1,enabled:true}` silences the
insert's sends/output, with a short smoothing ramp. False enables it.

`mixer.solo args={mixer:1}` keeps the target/downstream outputs.
`mixer.soloConnected args={mixer:5}` additionally keeps upstream contributors.
Connected solo never traverses Master backwards or branches from an upstream
contributor into unrelated buses. Whole retained inserts, including instruments
directly assigned to them, remain audible. Master cannot be soloed. Switching
solo modes/targets preserves the original mute snapshot. `mixer.unsolo` restores
that snapshot; restore=false unmutes all. Omitted restore follows Workflow's
preference (initially true). Manual mute ends the restore session. Read back the
mask and ownership; do not infer solo from a single enabled light.

Unreleased `mixer.unmuteAll args={}` is a separate global action: enable every
existing Mixer track, including Master, and clear solo restoration even when
there is no active solo. Ctrl+right-click any Mixer enable light invokes it
without changing the selected source or multi-selection; it is also in the
Mixer context menu and available for customized shortcuts/macros/MIDI commands.
It takes no arguments; discover its empty typed schema before use. Read the
actual mute mask and `vibera_mixer action=state`'s `solo` (null after completion).
One Undo restores the complete prior mute mask and solo session; Redo reapplies
it. Repeating an already satisfied action creates no history. Ordinary playback
and held MIDI takes use prepared live updates, without device/graph replacement.
Completion acknowledges the canonical state, not completion of its DSP smoothing
ramp or audible hardware output. Extra target/enable/restore fields are errors.

For a continuous pan/gain edit begin `vibera_gesture action=begin
domain=mixerControl sourceInsert=1`. Keep the returned token and update through
the gesture tool using only mixer.pan/gain with that same explicit mixer index.
Commit produces one Undo; cancel/idle expiry restores the exact baseline.
leaseMs250..120000, default30000. Wrong targets/commands/tokens fail without
partially applying an update. The domain supports held MIDI recording without
changing its recipient. Poll actual completionState and verify document/state.
Use edit.undo/redo or a validated ordered batch for discrete changes. Save/open
and export jobs must be polled until terminal; accepted pending is not completed.

Meters expose left/right block Peak/Rms and separate Display envelope, linear
amplitude (dBFS=20log10;0=silence), post-effects/pan/fader/mute before sends;
Master after its fader. They are live transient read-only state, not Undo data.
Display decays with300ms exponential release; raw peaks/RMS can already be zero.
ageMs>250 yields stale=true and zero values. available=false means no stable
callback reading, not proof of silence. A stopped transport may still process
live MIDI. Offline renders do not drive the live Edit's meters. Meter values do
not prove audible hardware output, listening quality or complete plugin PDC.

Unreleased overload monitoring adds `leftOverload`/`rightOverload`,
`leftLastClippedBlock`/`rightLastClippedBlock` and `clearedThroughBlock`.
A finite sample peak at or above1 (0dBFS) latches its side, including negative
samples. Silence, display decay and stale levels do not clear the latch. This
is sample overload indication, not true-peak metering or hardware clipping.
Click the meter or use its context menu to clear; semantic equivalent:
`vibera_mixer action=resetOverload insert=1` (Master0 is allowed). Discover
`mixer.meterReset` for the typed target/prerequisites. Reset requires a prepared
meter and returns completed plus the completed-block fence and actual meter
readback. It excludes an in-flight/new callback, so later clipping can re-latch
immediately. No project mutation, Undo, transport reset or recording interruption.
Missing/unsupported targets return structured errors. Meter lifecycle reprepare
can clear this transient state; it is never saved as project data.

Invalid names/ARGB/ranges, missing indices, Master solo, foreign/stale leases
and inapplicable fields return structured errors. Recover by reading current
state/discovery; do not retry against guessed targets. Arbitrary third-party
native editor controls remain outside supported host parameter interfaces.

Unreleased Mixer balance correction preserves the existing linear pan law,
15ms manual coefficient ramps and centre snap at inclusive+-0.005. Gain/pan
updates retain the graph/device/voices through the canonical commands above.
Own-insert and Master meters now follow their own pan, including after actual
reactivation. Faders cap320logical pixels while keeping the same0..2 gain
mapping; dBFS scale and peak/RMS readouts are read-only views of these existing
semantic values. Clicking below a capped fader selects the insert without a
document/history edit. Use semantic commands, not screen coordinates. Overload
operations above are under development; verify exact connected discovery and
current receipts. Observable native-editor completion remains unfinished.

## Integrated EQ

Every insert has a 3-band EQ after its effect slots and before stereo, pan and fader: a low
shelf, a mid peak and a high shelf. `mixer.eq` sets one band (`args.mixer`, `args.target` band
0/1/2, `args.value` gain dB -18..18, `args.offset` frequency Hz 20..20000, 0 keeps it);
`mixer.eqReset` flattens it. Each is one Undo step; it is saved with the project and applies
in playback and export. Read `vibera_state` mixer rows `eq` (gainDb, frequencyHz per band).
UI: insert menu > EQ (3-band)... opens a curve editor (drag a band: frequency and gain;
double-click: flat band; right-click: reset all; one Undo per drag).

## Track delay

`mixer.delay` (`args.mixer`, `args.value` 0..500 ms) delays an insert's audio after its EQ
(a time offset for alignment, not an echo); saved with the project, one Undo step, applied in
playback and export. Read `delayMs` on the mixer rows. UI: insert menu > Track delay...

`mixer.insert.create` takes `args.value` 1..32 to add several inserts in one Undo step (insert
menu > Add multiple inserts...).

## Meter-wave view

`mixer.meterWave` (Alt+W in the Mixer) toggles each insert's peak meters into a scrolling level
waveform (newest at the bottom, width = peak). View only; read `state.mixerMeterWave`.
- Docking: `mixer.dock` (`mixer` insert, `value` -1 left / 0 middle / 1 right; one Undo) shows the
  track in the left, middle or right section of the Mixer (insert right-click > Dock to). Layout
  only: routing, IDs and `mixerOrder` storage order are unchanged; `vibera_inspect` mixer rows
  report `dock` as left/middle/right.
- Select (Mixer track menu > Select, acting on the selected insert): actions
  `mixer.selectChannelsRouted` (Alt+L: Channels whose Mixer track is it, Rack selection),
  `mixer.selectRoutedHere` (tracks routed to it) and `mixer.selectRoutedFrom` (tracks it routes
  to) replace the selection and refuse when there is nothing to select.
- Reset track: `mixer.reset` (`mixer`; one Undo) puts name, colour, volume, pan, mute, stereo,
  phase/swap, EQ, track delay and dock back to defaults; effects and routing stay.
- Link all parameters (Mixer track menu > File > Link all parameters to MIDI CCs; `mixer.linkAll` on
  the selected track): maps volume, pan and stereo separation to the next three free CCs (20..119,
  MIDI channel 1, absolute with pickup) through the controller mapping service; the status names the
  CCs. Remove or relearn them on the Controllers page.
