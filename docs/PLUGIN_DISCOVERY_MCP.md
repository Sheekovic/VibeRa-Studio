# VST3 discovery: shared Settings and MCP service

Delivered0.0.45 subset; delivery/qualification authority is STATUS/VERIFICATION.
No other plugin formats are enabled. Native manual Undo remains open.

`vibera_catalogue` uses PluginCatalog, the same service as Settings -> Plugins.
Global catalogue/configuration changes do not enter project Undo. The project
commands for instrument/effect creation still provide canonical document history.
Registered IDs: plugins.discover, plugins.scan, plugins.cancelScan. UI drafts of
the folder editor/standard-location checkbox commit with Scan, not on each key.

State exposes configured and requested scan scope separately, standard Windows
folders, progress/counts, current job identity and the settings path. `list` pages
plugins, blacklist or failure diagnostics; plugin kind can filter instruments/
effects. Class IDs are opaque JUCE identifiers, not rows or project plugin IDs.
`descriptor` refuses unknown/ambiguous IDs and returns the current VST3 XML used
by existing canonical plugin.instrument.create/replace/plugin.effect.insert.
Metadata timestamps can change through rescan without changing class identity.

Scan returns pending with an exact process-local operation ID. Read `job` until
completed/failed/cancelled. Up to16 historical results plus the current receipt
are retained; restart/expiry requires rediscovery, not assumed completion.
Completion means scanner work and atomic catalogue persistence finished. Individual
failed bundles are reported and blacklisted while successful classes remain
available; a completed scan can have failures. A settings writer failure is a
failed job with effectMayHaveOccurred=true because memory results may have changed.
Read-only state/list exposes that last outcome as scanError without failing the
read itself. A failed job remains failed after a later successful operation.

Cancellation is a request, not immediate completion. The background scanner
observes it, gracefully stops its exact owned child and uses the approved
retained-handle timeout fallback if necessary. A cancelled scan preserves prior
catalogue/blacklist/configuration on disk and never blacklists cancellation.
Once atomic settings commit starts, cancellation is explicitly refused; observe
the actual job instead of claiming rollback. No catalogue lock is held across
disk I/O, and scanner work/locks/allocation/lifetime stay outside audio callbacks.

Standard locations remain enabled by default. An optional checkbox/API flag
allows a targeted custom-folder scan for diagnostics or a smaller user scope;
this is an optional improvement, not a silent change to familiar defaults.
Absolute custom paths are normalized/deduplicated; at most128 paths of4096 chars.
Empty/missing folders can discover no classes. Results describe the selected
scope; a completed narrower scan replaces the cached catalogue for that scope.
The additive standardLocations setting is backward compatible with version1 XML.

Test-mode Studio owns a TemporaryFile declared before PluginCatalog: scanner
shutdown finishes before that exact temporary settings file is removed. Normal
interactive MCP retains the real user catalogue. Synthetic scanner fixtures,
filesystem fault injection and actual installed-provider runs are labelled apart.
The stdlib client remains vendor-neutral; the optional native desktop verifier
uses local Pillow for PNG capture and is not a runtime/skill dependency.

## Current verification and retained negatives

- Scanner focused26 assertions pass: crash/timeout containment, persisted blacklist,
  rescan skip, exact receipt history, cancellation preserving XML and asynchronous
  failed atomic write with a retained sentinel. Original scanner fault fixture,
  not third-party compatibility proof.
- Actual Analog Lab V MCP workflow32 checks passes discovery/paging/class resolution,
  blacklist/skip/retry/cancellation, actual filesystem failure/recovery, canonical
  creation/Undo/Redo, exact Channel runtime, project save/reopen and nonzero hosted
  WAV. Source run30.91s at app56dc33bc9799c0ba0a44b086a2e2f31ca681b8f6bef24cd629c3c34609169c91.
  Final canonical package18/18 passes; catalogue workflow32 checks29.88s.
- Actual Settings component callbacks pass7 case groups: exact scope, busy/idle,
  persistence, skip/retry, cancellation, fast same-count identity refresh, stable
  selection and reopen excluding cancelled drafts. This is component execution,
  not OS mouse input. Three callback reviews require current passed receipts and
  all native/dependency hashes. Failed/pending evidence or drift invalidates them.
- Traced fast-refresh red retained the old UI path after discovery completed with
  a new class path. Current green refreshes both. Scoped retry red lost two
  out-of-scope blacklist entries; green retains both and removes only a recovered
  in-scope candidate. See build/catalogue-fast-refresh-traced-failure and
  build/catalogue-scoped-retry-failure. No fault deadlines were relaxed.
- Seven known non-callback UI controls remain gaps in
  [the explicit inventory](mcp/SURFACE_INVENTORY.json); whole-app inventory is
  explicitly incomplete. Full readiness remains false.
- First final package rolled back to44 after installed-plugins runner PID52348
  crashed0xc00000fd, not a timeout. Local debugger reproduced _alloca_probe called
  by LiveMidi::reset. Unwind metadata: main286504 + outer actualPluginTests282904
  + nested midiInputTests443320 =1012728 bytes before deeper calls, against the
  unchanged1MiB reserve. Outer fixture-only heap ownership reduces its frame to
  4056 bytes (278848 bytes freed); production application/DSP is unchanged. The
  original installed suite passes137.29s; final source19/19 and package18/18 pass. Evidence:
  build/catalogue-package-stack-failure and build/catalogue-stack-debug.
- A later debugger run reached rapid-edit coverage and failed that original
  predicate. It is retained separately at build/catalogue-stack-debug-full;
  debugger scheduling is not normal-run performance qualification. Diagnostics
  now record actual edit/block counts; no assertion/pacing/deadline was weakened.
- First helper failure at build/catalogue-mcp-first-failure: expected failed job
  was incorrectly consumed through a success-only client method. Fixed polling
  explicitly accepts errors and verifies failure. Read-only state error nesting
  was also corrected; no product/failure predicate or deadline was relaxed.
- Initial targeted Settings messages did not activate Scan; retained at
  build/catalogue-settings-message-failure. Exact cause is unestablished; this is
  not a passing mouse test. Subsequent OS test paused before input on recent user
  activity at build/catalogue-settings-os-source, with graceful exit0.
- Actual Settings dark caption image and Browser1080p150 component image opened/
  inspected. Mouse callback qualification remains pending. Full custom-surface
  readiness stays red; a source mapping or screenshot is not a native workflow pass.

Native-caption colours use the supported
[Windows DWM attributes](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwmwindowattribute).
Older OS versions retain their fallback. Main custom JUCE chrome is unchanged;
theme changes update owned auxiliary captions. No mixed-monitor or listening claim.
