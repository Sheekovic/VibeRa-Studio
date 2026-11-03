# App-owned metering opt-in

Original VibeRa integration patch against Tracktion Engine revision
`4536d8a21664fe6ec2aa34b25abc87fa2a0d3b86`, following the existing
`tracktion-sidechain-bus` patch to `tracktion_EditNodeBuilder.cpp`. The pinned
dependency remains under its recorded GPL-3.0-or-later license; this patch adds
two independent conditional wrappers and imports no proprietary implementation.

An app-created Edit may set `edit.state["viberaOwnMetering"] = true` before graph
construction when it supplies its own audio-thread-safe metering. The opt-in
omits `LevelMeasurerProcessingNode` construction for native LevelMeterPlugin
entries and the separate output-device master `LevelMeasuringNode`. Ordinary
Edits retain the exact existing behavior because the property defaults to false.
The preview-specific `SharedLevelMeasuringNode` construction remains unchanged.
No other graph, plugin, routing, latency, or audio-processing branch changes.

This is a graph-construction option, not a real-time property mutation or an
assertion that all callbacks are safe. VibeRa's adoption of the opt-in, absence
of required native-meter clients, and actual playback/export/custom-meter results
must be verified by application tests separately.

`scripts/apply-owned-metering-patch.ps1` pins the dependency, predecessor and
single target; verifies the exact patch payload, headers, numstat and source;
refuses reparse-point paths and unknown source bytes; and checks the generated
post-image in an owned disposable copy before changing dependency source.
`-ValidateOnly` (alias `-VerifyOnly`) preserves dependency source; it may create
and remove that disposable preflight copy. Repeated application leaves a known
final target's contents and modification time unchanged. Integration into the
shared runner and the predecessor's explicit successor recognition is separate.

Exact predecessor source SHA256:
`e8720bd117ccc1d851ed6679b113cd73286cef6676cb7810c834d38e33d923ff`.
Exact final source SHA256:
`0bec425cab622973213e30423007fbb556289e7d0e74ef565132b1c6f86b1bcd`.
The authoritative patch payload hash is in `manifest.json`.

Isolated guard checks: `build/owned-metering-check/verify-guard.py`.
The checks exercise fresh application, dry validation, idempotence, unknown and
missing source, malformed pin/path/authority/hash/header contracts, altered patch
payload, incorrect declared post-image and a Windows directory junction. They
prove guard behavior and narrow source changes; they do not establish C++
compilation, runtime audio, native desktop behavior or listening results.
