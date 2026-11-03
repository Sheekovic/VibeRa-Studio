> Historical verification record. This runnable bundle was retired. Launch only `dist/VibeRa Studio/VibeRa Studio.exe`; see [current status](STATUS.md) and [distribution policy](DISTRIBUTION.md).

# Preview 0.0.27 qualification

Development checkpoint, 2026-10-14. No qualified0.0.27 package yet; qualified0.0.26 remains available. Publication is not part of this work.

## Implemented scope

Isolated Windows x64 VST3 native-ID parameter metadata, modeless Plugin controls from Channel/Mixer menus, normalized manual control/Default, unavailable/read-only/hidden handling and explicit stale/busy outcomes. The retained worker bridge adds bounded sample-offset events, native queue merging and zero-audio stopped-state flushing. Explicitly edited normalized values supplement opaque native plugin state for normal project save/reopen. See [PLUGIN_PARAMETERS.md](PLUGIN_PARAMETERS.md) for scope, original dependency patch reconstruction and retained failures.

These controls do not establish project Automation Clip links, parameter gesture Undo/Redo, controller recording or an audible/sample-accurate response from every plugin. Mixer/Master Automation Clips retain their separately qualified0.0.26 behavior.

## Current verification

Release rebuild exited0. Three owned focused GUI repeats passed72 native controls edits and576 virtual callbacks across Pattern/Song, maximum callback0.2438ms, maximum acknowledgement58.94ms, retained Edit/context/graphs/device/worker and held MIDI takes, state save/reopen and removal guards. Fresh component screenshot was opened and inspected. Full source12/12 passed in87.22s; actual Analog Lab V and MVibratoMB parameter controls/state checks passed. Matching broader automation/controller/MCP regression subsequently passed as recorded below.

Earlier full-suite repeat failed `Controls native acknowledgement/document dirty` after a fixed70ms wait. Exact receipt, executable hash and outputs are preserved at `build/parameter-ui-ack-failure`. The verifier now waits for an actual acknowledgement with a2-second bound, checks value/status, records latency and retains the original DSP deadline. Later focused waits did not exceed70ms: the failure's exact scheduling cause is not established. This is an asynchronous verification correction, not a claimed audio or plugin bug fix.

The matching native desktop repeat paused on foreground ownership loss and closed its retained task-owned app normally (exit0, acceptance false). Receipt/output: `build/parameter-ack-native-paused`. No user input is overridden. Native input, component captures, virtual callback measurements and physical hardware/listening are separate evidence.

## Remaining release gates and continuation

Matching broad regression is now terminal:12/12 in90.59s, MCP408, six controller runs/15360callbacks/max0.8581ms and zero measured overruns. Full installed-provider processing/state/export/recording/editor opening and sample/native-instrument name-opening passed. Concurrent Pattern/Song512callbacks per mode measured maxima0.1509/0.2650ms, no silence/overruns or lifecycle changes. Hosted Playlist169actions/two loops/max0.2512ms passed with exact Undo/Redo and retained workers. Matching native frame/caption/edge/button/system-command checks passed; actual1920x1080 and3840x2088 PrintWindow images at144DPI were opened and inspected. This does not establish mixed-monitor or listening behavior.

A second native parameter repeat paused on recent user input; a bounded20-second idle gate subsequently saw no4-second idle period and started no test. Neither is a native pass. Evidence: build/parameter-ack-native-resume/process.json and build/parameter-ack-idle-gate.json. All current owned handles are terminal. Preserved runnable development tuple: build/parameter-runnable-checkpoint-0.0.27/checkpoint.json, original source archive a553446 and matching app/worker/fixture. It is explicitly not a qualified package. The live build app remains available at build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe.

Remaining: obtain a successful scoped native parameter repeat, retain the paused run and reconcile the final qualification receipt against the exact source/binary hashes. Checkpoint source, run `scripts/package.ps1 -VerificationPrefix automation -AllowReferenceInputs`, qualify relocated bundled parameter/automation/inherited workflows, MCP/SDK and native captures, inspect actual images, and write an exact-file final package receipt. Do not call an assembled but unqualified directory a tested preview.

Preserve all three unresolved earlier callback timing failures recorded in PREVIEW_0_0_26.md. Full upstream real-time safety, physical Launchkey behavior, listening, human familiarity, complete AU-01/M2-M6 and the full FL feature/UI objective remain open. Next engineering after the release checkpoint: project plugin-target linking/prepared timeline delivery, parameter gesture Undo/Redo, remaining curve/event/controller recording, original effects and recovery.
