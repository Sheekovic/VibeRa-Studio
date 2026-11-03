> Historical verification record. This runnable bundle was retired. Launch only `dist/VibeRa Studio/VibeRa Studio.exe`; see [current status](STATUS.md) and [distribution policy](DISTRIBUTION.md).

# Preview 0.0.25 — native MIDI controller foundation

Qualified locally on2026-10-13. Executable: `artifacts/preview-0.0.25/VibeRa Studio.exe`. Source checkpoint `1ab6b1dbfafe5c8aa947e124f68c5bda940989de`; application SHA256 `3d14a9b2b552a6da095624475e2c722b0432b3d60293c7d3174e33169311abf8`. Source ZIP, pinned JUCE/Tracktion archives, licenses and binary provenance accompany the executable. This post-qualification note records results after the source checkpoint; it does not change the archived source or executable.

## Use your Launchkey

Settings > Controllers > Launchkey MK3 preset > Connect Launchkey. Read-only Windows discovery identified LKMK3 performance and MIDIIN2/MIDIOUT2 DAW endpoints, consistent with MK3. Ordinary keys use Settings > MIDI; control input/output use Controllers. Use Session pads and supported Mixer Volume/Pan modes. The preset includes transport/record/metronome/Undo, editor and instrument opening, window arrangements, Mixer banks/faders/pots/Master and additional pad actions. Pad16 is a software modifier, distinct from physical Shift. For custom links choose Add mapping, select an action/macro and mode/range, then MIDI Learn and press/move a control. Learn captures without executing; pickup prevents absolute value jumps. [Detailed scope](MIDI_CONTROLLERS.md).

## Evidence

| Check | Actual result |
| --- | --- |
| Source Release |8/8 suites,71.99seconds;10044 musical assertions. |
| Bundled musical runner |10042 assertions, exit0; ownedPID34880, creation2026-10-13T16:36:41.5712316Z. |
| Bundled controller |1280 virtual callbacks/320 fader-pan edits per playback mode plus Undo/Redo; max1.4029/0.2751ms; zero overruns/graph-device lifecycle changes. Eight edits preserve injected MIDI capture. Pickup, Learn, final insert/Master, Stop and profile-file round trip pass. OwnedPID37628, creation2026-10-13T16:37:17.3762295Z. |
| MCP | Source and packaged389 raw protocol checks plus16 official SDK checks, matching app hash. |
| Existing bundled GUI | Context/smoke, asynchronous MIDI, Playlist/live and sampler gestures/tempo passed. |
| Actual installed plugins | Analog Lab V/MVibratoMB processing/state/export/native editor; source sample/VST3 opening during Pattern/Song; hosted Playlist168actions/two loops/max0.2499ms/zero silent right blocks-overruns, exact Undo/Redo and retained workers passed on matching bytes. |
| Native frame | DPI144,1920x1080/3840x2088 PrintWindow, caption/edge and targeted minimize/maximize/restore passed. Final native populated and packaged Controllers images opened. No global synthetic input. |

Package `provenance.json` records binary/source identities. `evidence/packaged-musical-process.json`, `packaged-controller-process.json`, `packaged-mcp/verification.json` and `packaged-sdk/verification.json` distinguish actual process/provider evidence. Main logs: `build/controllers-final-exposure-ctest.log`, `controllers-package-exposure.log`, `controllers-packaged-mcp.log`, `controllers-packaged-sdk.log`.

## Limits and continuation

Physical Launchkey input/control gestures, mode transitions, LEDs, driver behavior, unplug/reconnect, physical multi-monitor operation and listening remain unqualified. No hosted-plugin parameter automation, pressure/NRPN/SysEx, multi-device/project profiles, GUI profile import/export or controller gesture Undo coalescing yet. Macros remain canonical document operations. This is useful controller infrastructure, not complete DAW/controller/FL parity.

Failures remain archived: initial fixed-inbox Windows stack overflow was fixed by prepared heap storage; synthetic wheel fields were uninitialized and are now initialized; native keyboard hit testing required exposing only the task-owned test window above unrelated desktop windows. No production window placement change, buffer increase or assertion weakening. Historical sampler-preview deadline/budget failures remain unexplained. Failed partial previews are preserved under `build/controllers-package-*-failed`; previous previews remain unchanged.

All owned jobs are terminal. Next: recorded physical controller qualification, isolated plugin parameter control/automation foundation, output reconnect, gesture Undo and profile tools. Resume from STATUS/ROADMAP/MILESTONES; no unattended session is promised. Local packaging only; no external publication.
