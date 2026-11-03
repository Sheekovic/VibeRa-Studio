# Settings Plugins: expanded operation inventory

This expands the custom-control candidates in src/PluginPage.h. It does not
waive the full coverage gate or turn source wiring into passed native acceptance.
Read STATUS/VERIFICATION for exact executable evidence. Recheck fingerprints after
code changes. Actual scanner/MCP cases: scripts/verify-catalogue-mcp.py and scanner
containment; native input cases: scripts/verify-catalogue-desktop.py.

| UI operation | Canonical path / MCP equivalence | State/result readback | Verification / remaining gap |
| --- | --- | --- | --- |
| Edit folder paths | PluginCatalog.scan customFolders / vibera_catalogue scan | configured customFolders versus requestedFolders | Typed normalized scope and persistence tested; idle local text draft is not independently exposed or remotely selectable. Partial. |
| Standard locations checkbox | Same scan option / standardLocations | configured versus requestedStandardLocations, standardFolders | Defaults/persistence tested; local pre-scan draft equivalence remains to audit. |
| Scan / rescan | PluginCatalog.scan / catalogue scan | exact operation/job, progress/counts/list | Actual component callback, SDK/backend and installed-provider MCP pass; native OS mouse pending. |
| Retry blacklist | Same service retry=true / retryBlacklisted | failure/blacklist pages and completed job | Component callback distinguishes skip versus actual retry; installed-provider MCP passes; OS mouse pending. |
| Cancel scan | PluginCatalog.cancel / catalogue cancel | cancelRequested, actual terminal job | Component callback, fixture and installed-provider MCP cancellation pass; OS mouse pending. No fake immediate cancellation. |
| Inspect class list/details | PluginCatalog.snapshot / list + descriptor | stable class IDs, names/version/manufacturer/path/type/buses | Actual installed factory discovery/paging/class resolution tested. |
| Select catalogue row | PluginPage ListBox local selection | No semantic UI selection readback | Missing shared catalogue-selection model and endpoint. A descriptor lookup does not select the UI row. |
| Select Channel target | PluginPage selectedChannelId then Studio.selectedChannel / vibera_select channel | selectedChannel and channels | Explicit target API exists; shared canonical selector/UI dropdown equivalence needs audit. |
| Select Mixer target | PluginPage local ComboBox / explicit mixer command argument | Mixer/project readback | Local dropdown selection is not exposed; target-selection equivalence remains partial. |
| New Channel | Studio.command plugin.instrument.create / vibera_command | project Channel/plugin IDs, pending graph and actual worker state | Actual scanned descriptor -> create -> Undo/Redo -> save/reopen/WAV tested. UI button binding inspected; native activation pending. |
| Replace Channel instrument | Studio.command plugin.instrument.replace / vibera_command | Channel/plugin/runtime state | Existing command mapping; current discovery-specific replace/UI scenario pending. |
| Insert effect | Studio.command plugin.effect.insert / vibera_command | ordered Mixer effects, runtime state | Existing actual effect insert/order/bypass/state cases; discovery-specific effect/UI case pending. |
| Channel plugin menu | Studio.pluginMenu / loaded-plugin select, Controls, notifications, recovery and canonical bypass/remove | vibera_plugin + project/worker state | Equivalent target operations share services. Menu expansion/native visibility acknowledgement and successful MCP crash recovery remain open. |
| Mixer slots menu | Studio.effectMenu / same project/plugin services | ordered slots, plugin/runtime state | Individual order/bypass/removal/Controls APIs exist; complete contextual-menu audit remains open. |
| List/outer page scrolling | JUCE ListBox/ScrollingSettingsPage | No remote scroll/viewport readback for this page | Gap; paged discovery does not by itself operate the visible UI scrollbars. |
| Progress/error/blacklist display | PluginCatalog.snapshot / state, job, list failures/blacklist | actual terminal state, scanError versus failed job error | Async filesystem-failure/readback and later-success retention tested; no schema-only completion claim. |

Third-party native editor widgets without supported APIs are a capability limit.
The local UI/selection/scrolling gaps above are VibeRa work, not justified exceptions.
Three callback mappings are reviewed for actual component execution plus installed
MCP cases, with current handler/backend/test input hashes and a passed receipt:
PLUGIN_DISCOVERY_EVIDENCE.json / REVIEWED_SURFACES.json. This does not qualify
native OS mouse acceptance or this entire Settings page. Local draft/selection/
scrolling gaps remain unreviewed. The targeted-message failure and OS input guard
pause remain retained; neither is relabelled as successful mouse interaction.
