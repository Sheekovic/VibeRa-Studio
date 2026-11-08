# Installed VST3 discovery (application0.0.45+, contract1)

The installed class catalogue and loaded project plugin list are separate.
`vibera_catalogue` identifies installed class IDs/descriptors; `vibera_plugin`
identifies project plugin IDs/runtime instances/native parameter IDs. Never use
a catalogue row or class ID as a numeric project plugin target.

1. Read `action=state`: configured customFolders/standardLocations, Windows
   standardFolders, scan state/counts and current operation. These are global
   app settings, not project edits/Undo. Test mode uses a disposable file;
   normal interactive MCP uses the user's actual persisted catalogue.
2. Start `action=scan` with explicit absolute `customFolders` and optional
   `standardLocations`. Omitted scope retains configuration; its initial default
   includes standard locations. To scan only chosen custom folders, explicitly
   set standardLocations=false. A narrower completed scan replaces the cached
   class list; it does not uninstall any plugin or remove project instances.
3. The reply is pending with operation. Poll `action=job, operation=<token>`
   until completed/failed/cancelled. A completed scan has finished discovery and
   persisted results. Inspect failureCount/blacklistCount: individual bad bundles
   can fail while the scan completes and other classes remain usable.
4. Page `action=list` with section=plugins/blacklist/failures, offset/limit and
   optional kind=all/instruments/effects. limit is1..128, default64. Each plugin
   item has an opaque stable class ID, name/manufacturer/version/path and bus
   information. Metadata timestamps can refresh without changing class identity.
5. `action=descriptor, id=<class ID>` returns the exact current VST3 descriptor.
   Pass it as args.text to canonical plugin.instrument.create/replace or
   plugin.effect.insert with explicit Channel/Mixer targets. Inspect project IDs
   afterward and wait for pendingAudioUpdate=false/actual ready worker readback.
   Those project edits have Undo/Redo. Catalogue settings do not add history.
6. Compose, save/reopen and export with the shared project tools. Poll each job,
   verify saved plugin state and actual WAV bytes; a descriptor or nonzero PCM
   alone establishes neither compatibility for every plugin nor heard audio.

Example scan arguments:

```json
{"action":"scan","customFolders":["D:/My VST3"],"standardLocations":true,"retryBlacklisted":false}
```

Custom folders accept at most128 absolute paths up to4096 characters, normalized
and deduplicated. Empty/missing folders can produce no classes. Retry blacklist
is explicit (`retryBlacklisted=true`); do not automatically retry crashing bundles.
Retry applies to candidates in the selected scan scope. Blacklist entries outside
that scope remain; an entry is removed only after its candidate scans successfully.
No unsupported plugin formats are enabled.

`action=cancel` requests cancellation and may still return pending. Observe its
job until terminal. Cancellation preserves the previous catalogue/settings/
blacklist and does not blacklist the cancelled bundle. Once settings commit has
started, unsupported_cancel is honest: observe the actual result rather than
claim rollback. Scanners stop only their exact owned worker instance.

Settings persistence failure is a failed job with structured error and
effectMayHaveOccurred=true: memory results may already have changed. Rediscover
before retry. Read-only state/list still complete successfully and expose the
last failure as scanError; they do not imply the failed scan succeeded. Retained
failed jobs stay failed after later success.
The completionState of a read-only state/list call describes that read; use
scanState or the job receipt for the asynchronous scan's outcome.

There are16 historical receipts plus the current job, process-local. Unknown/
expired tokens fail; process restart does not resume them. Project replacement
does not cancel an app-global scan. No arbitrary native plugin widget access,
manual knob Undo, physical-device or listening qualification is implied.

Known UI gaps: idle folder/checkbox drafts, visible catalogue selection, local
target dropdowns and settings/list viewport state are not all shared remotely.
Passing scan arguments commits a scan scope; descriptor lookup does not highlight
the corresponding UI row. Report those limits instead of claiming pointer-based
coverage or full Settings readiness.

## Settings > Plugins page equivalents

The page's local drafts map to canonical calls; read back after each:
- Custom folders / standard locations: `vibera_catalogue scan {customFolders, standardLocations}`, poll
  `job`, then `state.requestedFolders` / `requestedStandardLocations`.
- Row selection and scrolling: `vibera_catalogue list {offset, limit}` and `descriptor`.
- Channel target: `vibera_select {channel}` (registered as `channel.select`; a held MIDI take is
  finished first).
- Mixer target: the explicit `mixer` argument of `plugin.effect.insert`.
- Typed parameter value: `vibera_plugin set` (registered as `plugin.parameter.set`), poll to an
  acknowledged outcome; it enters project history. `busy` means structural work is still settling.
