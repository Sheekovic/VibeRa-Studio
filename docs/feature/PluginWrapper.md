# Unified plugin wrapper

User requirement2026-10-19: the actual VST3 editor and VibeRa host controls belong
in one reusable plugin window, with a familiar FL-style workflow. This is required
plugin/instrument workflow scope, not another per-button release. Source implementation is progressing; full native qualification remains pending.
No full Fruity Wrapper parity is claimed.

## Evidence and current gap

- Before this change: `PluginWorker.cpp`'s `EditorWindow` owns the actual native editor
  in a basic Windows-title-bar window. `StudioPluginParameters.cpp` separately
  opens `PluginControlWindow`. This is source inspection; current native desktop
  automation is refused by the owned-caption guard.
- Documented reference: the [official Plugin Wrapper manual](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/wrapper.htm)
  groups the native interface, host settings and parameter/preset entry points in
  a shared wrapper. Documentation is not observation of the installed FL version.
- User requirement: replace VibeRa's split-window host workflow with one wrapper.
  Existing isolated hosting remains useful and does not require split controls.

## Required usable behavior

- Channel name and Mixer-slot editor actions open or focus the same wrapper for
  that stable plugin instance. Distinct instances retain independent windows.
- A VibeRa charcoal header identifies the Channel or Mixer slot and plugin;
  Plugin, Settings and Parameters views share that window. Returning to Plugin
  shows the actual native editor, with its supported resize constraints intact.
- Existing Open Controls selects the wrapper's Parameters view. Do not leave a
  second host inspector as the ordinary workflow or reset native state on a tab
  switch. Plugin-created dialogs may remain separate, correctly owned dialogs.
- Expose existing supported bypass, effect wet/dry, parameter/reset/last-tweaked,
  preset/state, automation/controller and routing services in that wrapper.
  Instrument and effect controls differ where their musical models differ.
  MIDI/processing options require actual backend support; omit or clearly disable
  unavailable options. Do not advertise VST2/AU/32-bit hosting or arbitrary native
  widgets through this work.
- Preserve custom shortcuts, focus/text entry, close/hide/reopen, movable/resizable
  windows and monitor-aware sizing. Keep original branding and assets. Do not
  promise universal native-plugin key forwarding or unsupported plugin scaling.
- Ordinary opening/navigation/parameter controls preserve playback and held MIDI.
  Structural operations retain the existing bounded recovery and recording
  safeguards. Saving/exporting always captures correct current native state.

## Ownership and first experiment

Keep the visible wrapper and native editor on the plugin worker's JUCE message
thread. The worker already owns editor attachment and destruction. Do not move
the Studio-bound inspector component into another process or assume cross-process
`SetParent` is a supported solution.

Keep canonical project validation/history and native-scalar/automation admission
in the application. A small bounded CONTROL-only intent/reply seam can forward
wrapper actions to those existing services, scoped by worker instance, generation
and serial. The current editor mailbox supports only Open/Close, so this requires
an explicit compatible protocol/layout transition and stale-worker refusal.
No raw Component/Studio pointers, duplicate project model, synchronous GUI waits,
or DSP-callback work. Acceptance of an intent is not successful completion.

First prove an original SDK editor inside the worker wrapper with functional
bypass, effect mix and one exposed writable parameter/reset through the canonical
application services. Verify one wrapper, actual native attachment/resizing,
terminal acknowledgements, exact Undo/Redo, view close and stale-worker rejection.
This is an internal experiment, not completion or a separate release. Reuse the
existing broader controls only after ownership and request contracts are proved.

## Completion evidence

- Complete the required controls/views above through shared typed UI/MCP commands,
  state/readback, structured errors, history/cancel and real asynchronous outcomes.
  Keep provider-neutral skill guidance synchronized; a screenshot is not coverage.
- Test repeated Channel/Mixer opening, multiple instances, tab changes, native
  resize, parameter drags/reset, bypass/mix, save/reopen/export, Undo/Redo and
  crash/timeout/replacement without stale actions reaching another instance.
- Verify Pattern/Song playback and held MIDI, then available real VST3 instrument
  and effect branches. Original fixtures do not establish installed compatibility.
- Inspect actual wrapper renders at1080p and4K scaling; run guarded native input
  when the desktop permits it. Listening, physical controllers and mixed-monitor
  hardware checks remain distinct. Preserve refusals and failures.
- Deliver inside its coherent instrument/plugin workflow milestone, retaining the
  single qualified distribution until the exact replacement passes release gates.

## Development checkpoint2026-10-19

Original three-view worker component and bounded CONTROL request/reply are
implemented in source, with app-owned canonical bypass/mix/native-scalar
validation and history. The native editor remains solely worker-owned. Native
resize requests propagate; supported host resizes use its bounds constrainer.
Typed MCP wrapper navigation targets current runtime/parameter generation and
has actual window/view readback; scalar gestures retain their real operation.
The first wrapper layout usedIPC15; native-meter CONTROL readback now uses
IPC16 and matching app/worker are required. Project format is unchanged. Original
worker Pattern/Song wrapper controls, continuous semantic ACK/history, actual
structural Undo/Redo and canonical save/round-trip pass focused development
checks. Details: docs/STATUS.md and build/plugin-wrapper-handoff-20261019.json.
The current combined candidate and installed/native-input branches need fresh
qualification; no full-module/release pass is claimed. Existing broader controls and editorless fallback
remain required migration work; wet/dry currently submits on release. Keep these
gaps explicit and preserve the full behavior/acceptance checklist above.

IPC17 follow-up: ordinary Channel/Mixer Open Controls now selects the same worker
Parameters view. Automation clip/create-init/remove-init, refresh and session
notifications use canonical application jobs; per-worker Last tweaked selects
the exact published ID/generation. Explicit editorless Settings/Parameters creates
a real controls-only peer; native Open stays unsupported and real creation/
attachment failures never fall back. A genuine null-after-probes fixture caught
and fixed duplicate capability probing. Legacy explicitly requested Controls
remains compatible, including recovery of its own inspector and held MIDI take.
Actual installed Analog Lab V/MVibratoMB workflow passes; final focused results
are linked from STATUS. Remaining preset/controller/routing migration and native caption/scaling
acceptance remain open. No release or full
wrapper/FL parity is claimed.

Global Tools Last tweaked now shares canonical editor/automation jobs. Open
control selects the same worker Parameters view and exact native ID/generation.
Create/init/remove-init report pending then actual terminal outcomes, history and
errors; cancellation is supported before mutation. Newer gestures, disabled
notifications and replaced projects/workers cannot silently retarget preparation.
Pattern/Song original native fixture, held MIDI, graph/device invariants and exact
Undo/Redo pass; installed Analog Lab V/MVibratoMB state/history/export passes on
the exact candidate. Current evidence: build/wrapper-completion-20261019.
A native desktop attempt reached F2 but its VK text used the active Arabic
layout; that fixture failure is retained. A bounded guarded Unicode literal-text
repair preserves the exact oracle/shortcuts/guards/deadlines and OS keyboard
settings. The full native Mixer rerun passes34.29s. Current complete source
passes190/190 with no skips and stable compiled/verifier inputs;164 development
cases pass under unchanged15s. Fresh1080p/4K150 component renders are inspected.
These do not qualify native plugin-caption/scaling, listening or hardware.
Supported exact-target wrapper routing/containing-chain services remain required;
see build/wrapper-completion-20261019/next-services.json. No existing per-plugin/
instrument preset, arbitrary native-program or direct native-parameter MIDI
binding service can be claimed by substituting whole-chain shortcuts.


## Native main-I/O checkpoint2026-10-19 (unreleased IPC18)

Settings now exposes actual native bus names/indices and explicit stereo/mono
mapping through the same canonical plugin.io as semantic audioIo. A retained
parent observes the real child across editor close, fresh capture and worker
replacement. No-op, history, refusal and delegated cancellation are explicit.
Native peer style is stable across views; Settings/Parameters resize constraints
do not recreate the attachment. Original charcoal worker chrome is rendered.
Focused11/11 and installed MVibratoMB mono negotiation/WAV/stereo Undo pass;
actual DPI-aware client render was opened. Evidence and limits:
`build/wrapper-settings-20261019/verification.json`. Earlier full190/190 is
historical for these changed inputs. Required containing-chain and existing
sidechain wrapper entry points remain unfinished; no package promotion yet.

## Containing-chain checkpoint2026-10-19 (unreleased IPC19)

Settings now has Copy/Paste/Save/Load chain, Chain directory and Detector routing.
They resolve the containing insert from the exact effect ID, independent of the
selected Mixer strip. UI and typed semantic chain requests share retained parent
jobs over existing canonical services. Close serials are acknowledged/drained;
current native state is captured before copying/saving. Replacement outcomes stay
readable after the original worker/slot disappears. Recording safeguards remain.
Save replacement needs explicit approval; chooser cancellation drains its owner.
Instruments cannot invoke containing-effect-chain actions. These are whole Mixer
chains, not individual-plugin/instrument presets or arbitrary native programs.

Current focused12/12, exact installed Analog Lab V/MVibratoMB chain/state/WAV,
and full guarded native Mixer checks pass. Actual worker Copy and directory
cancellation pass in native/editorless Pattern and Song playback. Expanded
Settings and the real charcoal MVibratoMB client capture were opened and
inspected. Receipt: build/wrapper-chain-20261019/verification.json; semantic
inventory: docs/mcp/PLUGIN_WRAPPER_CHAIN_EVIDENCE.json. Native file selection/
overwrite, listening and mixed-monitor hardware are not claimed from these checks.
Full current-source/module/staging/final qualification remains pending.
