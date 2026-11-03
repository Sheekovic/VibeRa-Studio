> Historical verification record. This runnable bundle was retired. Launch only `dist/VibeRa Studio/VibeRa Studio.exe`; see [current status](STATUS.md) and [distribution policy](DISTRIBUTION.md).

# Preview0.0.21 qualification

Packaged and tested locally: artifacts/preview-0.0.21/VibeRa Studio.exe. Previous qualified preview0.0.20 is preserved. Compiled/source packaging checkpointa6b9d8daedaaa403a9211c41eb7759111e1f6566; final docs/archive revision is in provenance.json.

Application SHA2564464fd060d65960fdfc86020065580af77cdaabfa4d98b2d1aeda0a4900bec3f; runner26b9300fff25c6912a375b9cf0cac6ced31343265015788c3df02144271d69cd; worker57f03e9c1789ce23d8343668aff3bb2fb7d2a482c29a878644b43a71c80216ce.

Changes: VST3 worker readiness is published only after its DSP loop enters; failed realtime+normal launch is explicitly rejected. Atomic phase/heartbeat/processed diagnostics are included in host statistics, processing timeout messages, opt-in MCP audio.pluginWorkers and hosted Playlist baseline traces. IPC signature4 requires paired binaries; project schema remains6. No larger buffer, callback wait, edit mute, graph replacement policy or production stretch mode was added.

Source Release CTest7/7 (67.32s), MCP311 and full installed Analog Lab V/MVibratoMB regression passed, including the actual entered-loop readiness assertion. Concurrent virtual512block Pattern/Song checks max0.2731/0.2681ms, zero silent blocks/overruns, identities/device retained. Hosted app167actions/two loops/max0.2726ms/zero silent right blocks/overruns, baseline20submitted20processedheartbeat53phase waiting. Original WAV name opening max0.1171/0.1404ms; actual VST3 editor reuse0.1473/0.2203ms, zero silent measurement blocks and document/graph/device retained. Native frame/hit/system-command checks passed;1080p capture inspected.

Limits: earlier RE-03 timeout cause remains unproven; readiness fix is not evidence that all startup/timing problems are resolved. Wave graph notifications2/578 persist. Actual thread-launch failure was not forced. Listening, hardware, human familiarity, native musical input and full feature/UI parity remain open.

Stretch research is included as source/evidence only: five candidate configurations expose25failed channel pitch checks under5cents and default64stereo overloads. No pitch-preserving mode is enabled. Keep Resample limitations visible and preserve failed reports.

Bundled musical4436 assertions and MCP311 exit0; bundled smoke/context/MIDI/Playlist/live/sampler detection checks exit0. Bundled sampler976callbacks/five loops per mode, max0.1080/0.2053ms, zero overruns/unaffected silent blocks/device/graph replacements. Fresh bundled populated1080p150% and native4K/144DPI captures opened/inspected. Initial13manifest hashes/source inclusions/exclusions and previous0.0.20 app hash checked; final docs-only refresh verifies corresponding source/docs/runtime files with distinct compiledSourceCheckpoint and sourceCommit. No physical input or listening check. Continue source-aware waveform navigation, stretch/latency alternatives, persistent failures and remaining M2-M6 gates from ROADMAP.
