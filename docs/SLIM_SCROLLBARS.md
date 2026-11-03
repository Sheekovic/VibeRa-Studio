# Slim scrollbars and Channel Rack wheel navigation

Delivered0.0.41,2026-10-15. This implements the user's requested appearance and
wheel behavior; it is an intentional preference, not an inferred FL interaction.
The sole distribution is `dist/VibeRa Studio/VibeRa Studio.exe`.

All VibeRa custom viewport bars and standard JUCE viewports use a12 logical-pixel
gutter, with a6-pixel rounded visual thumb and the full gutter accepting input.
Windows scaling scales those logical dimensions. Workspace and auxiliary-window
looks share this rule; standard lists/trees, Browser, Settings, plugin controls
and Channel instrument pages inherit it. Waveform navigation uses the same
height and keeps a gap between waveform/handles and the bar. Third-party native
plugin editor internals and OS file dialogs are outside VibeRa's visual control.

Playlist, Piano roll and Mixer retain their functional bars. Scrollbar geometry,
view/zoom calculations, pointer exclusion and paint clips reserve their gutters;
notes, waveforms, step rows and selection overlays cannot draw underneath them.
H/V gutters do not intersect. Horizontal thumb-edge zoom remains available in
the existing Piano roll/Playlist contexts.

The Channel Rack's vertical bar is hidden, including after resize. Plain wheel
scrolls through Channels, clamped at top/bottom. Shift-wheel or a horizontal
wheel scrolls steps; the slim horizontal bar remains available. Zero wheel
delta is inert. Scrolling does not select/mute a Channel, alter steps, edit the
project, touch transport or rebuild the graph. Name, mute, Piano roll and steps
retain separate targets; clicked rows use the current scroll offset.

## Verification

Build and final source16/16 pass in180.16s; musical45.72s. Packaged14/14 and current MCP472 pass. GUI acceptance now
checks reserved gutter bounds/no H/V overlap, hidden Rack bar, plain/Shift wheel
axes, last-Channel reach/clamping, reverse/zero delta, resize and exact musical
state retention on an overflowing34-Channel project.

Actual OS desktop check passes on the final candidate hash
`895468a6fc27752620d5ad23fc05ac4fee609799cb51e5aaf1210a2505e6d79d`:
`build/slim-scrollbars-native/native-pattern-verification.txt`. With28 Channels,
eight plain OS wheel ticks down reach Channel9, verified by opening its actual
WHEEL5 instrument; eight up return to KICK. Save preserves exact baseline XML.
The real owned-window1920x1080/144DPI Rack and990x1020 Channel-instrument PNGs were
opened/inspected: no Rack vertical bar, slim separate Playlist/Piano/Browser/
instrument bars. Only owned test windows were raised/closed; no user editor was
changed. Native Transpose/typing/Save/Fit/Undo and Ctrl+Y also pass on that same
hash in `build/slim-scrollbars-transpose-native`.

Fresh1080p100% and4K150% component captures, Mixer Insert17 and Settings Colours
were opened/inspected; the smaller bars remain separate from musical/control
content. Installed Analog Lab V retained-host Pattern/Song regression passes
767/1517 callbacks, maxima0.3479/0.3680ms with zero measured silent-right blocks/
overruns. Current full source includes sampler/musical/parameter/Browser/MIDI/
automation regressions. Final packaging now passes; direct canonical receipts are recorded below.
Component scale simulations, actual native desktop input, user physical input,
hardware and listening remain distinct. User no-stutter evidence applies to
0.0.40 only. Retain historical timing failures and the unresolved native
Ctrl+Shift+Z delivery issue in [PATTERN_TRANSPOSE.md](PATTERN_TRANSPOSE.md).

Final source16/16 and packaged14/14 pass; MCP472/musical32631. Direct canonical
package native repeats pass with graceful exit0/no forced close, unchanged hash:
`build/slim-scrollbars-packaged-native` and `build/slim-scrollbars-packaged-transpose-native`.
Fresh PNGs from both were opened/inspected. Audio Recovery413 callbacks in each
Stopped/Pattern/Song mode has zero overruns, maxima0.1544/0.2046/0.1722ms. These
are virtual measurements, not new listening. Prior0.0.40 and all temporary
runtimes are retired; one application folder remains. Exact tuple in STATUS.
