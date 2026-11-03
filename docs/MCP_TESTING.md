# Local application MCP testing

The user requested MCP for development testing. The native application now has an explicitly enabled, local stdio MCP server. Normal startup has no server, listener or network dependency. A client launches one owned application instance; closing its stdin ends that instance. This first implementation does not attach to an already-running user instance, save/load arbitrary files, execute shells, control other programs, or expose CoProducer generation.

The protocol uses newline-delimited UTF-8 JSON-RPC, initialization/version negotiation, initialized notification, ping, tools/list and tools/call. Supported negotiated versions: 2025-11-25, 2025-06-18, 2025-03-26 and 2024-11-05. Only tools capabilities are advertised. Requests are bounded to 64 KiB, state to 512 selected-pattern notes/128 patterns, snapshots to 1600x1000 pixels. Truncation is explicit. Unknown commands/fields, invalid numeric/boolean arguments and stale revision guards are rejected. Tools can report execution errors separately from protocol errors.

The stdio worker owns pipe I/O and uses a duplicated handle to cancel only its own synchronous writes during shutdown. Tool execution is marshalled to JUCE's message thread. No MCP work, allocation, locks, network or file I/O is added to the audio callback. UI dispatch has a five-second bound; a timed-out edit may already have started, so inspect state before retrying. There is no asynchronous tasks/cancellation capability claim.

## Client configuration

For a stdio-compatible client, use the development executable with `--mcp` to create a visible application with its normal audio settings. Example generic MCP configuration:

```json
{
  "mcpServers": {
    "vibera": {
      "command": "E:\\Projects\\VibeRa Studio\\artifacts\\preview-0.0.12\\VibeRa Studio.exe",
      "args": ["--mcp"]
    }
  }
}
```

Use `--mcp-test` for a hidden, hardware-disabled owned test instance. It tests the real project model, canonical edit pipeline and component rendering; it does not advance a physical or virtual audio device. Its transport flags are not playback/PCM evidence. A console launch without piped stdin/stdout is rejected. The 0.0.12 local review package includes this server. Packaged 0.0.11 predates it. Version0.0.12 introduced this server; consult STATUS.md for the latest qualified package.

No global client configuration was changed and this session's tool catalog has not automatically acquired VibeRa tools. The official TypeScript MCP SDK 1.32.1 has now qualified an actual stdio application instance. Attachment to an existing instance and connection to this session's tool catalog remain unimplemented.

## Exposed tools

Development0.0.25 adds the tenth tool, `vibera_controller`, only in owned hardware-disabled `--mcp-test` instances. State/preset/configure/Learn/cancel/injected short-message operations exercise the actual inbox and command dispatcher. Hardware identifiers cannot be opened by this tool. Injected actions that would open file dialogs or perform filesystem operations are rejected before asynchronous UI dispatch. It preserves modal and pointer-gesture exclusion. Current checks:389 raw-protocol checks and16 official SDK checks on executable49a12508edf0fd1ac09e3f53ddd31738e3b40c523c886a2e90947190f21a3bf6. These do not prove MIDI hardware or playback; controller_mapping_gui separately uses a virtual device.

| Tool | Operation |
| --- | --- |
| vibera_state | Bounded project/editor/transport state, pending projection, audio device and sampler graph/render lifecycle counters; no sample paths/plugin state blobs |
| vibera_commands | Explicit whitelist of canonical document edits plus undo/redo |
| vibera_command | Undoable edit with optional expected revision; undo/redo use recording completion safeguards |
| vibera_select | Existing Pattern/Channel/context selection; pending take and performance ownership handled before a target change |
| vibera_transport | Play/stop/seek and Pattern/Song through normal application callbacks |
| vibera_editor | Show/hide/maximize Rack, Piano Roll, Playlist or Mixer |
| vibera_key | Contextual safe shortcut through the actual application key handler; file/dialog/macro actions excluded |
| vibera_pointer | Paired down/drag/up or cancel on actual editor/child component handlers, in logical coordinates |
| vibera_snapshot | Actual instance component PNG including the custom header; not native PrintWindow or OS-input evidence |

Document command arguments mirror CommandArgs: pattern/channel/target IDs; beat/length/value; pitch/lane/mixer; numerator/denominator; enabled/step booleans; ids (at most 1000). IDs are resolved against the actual project. No raw plugin descriptors/states, sample replacements, file commands or direct CoProducer acceptance are exposed. The edit tool correctly advertises potential destructive changes because it can delete notes/clips, even though they are undoable. Tool annotations are hints, not authentication or approval grants.

Example tools/call parameters after initialization:

```json
{"name":"vibera_command","arguments":{"command":"notes.add","revision":0,"args":{"beat":1,"length":0.5,"pitch":64,"value":0.8}}}
```

Read the actual current revision before using the guard; 0 above is illustrative. A component image can verify visual state, canonical commands verify application behavior, and lifecycle counters help diagnose transport/graph changes. None establishes audible quality, hardware timing, OS gesture delivery or full workflow parity. Offline export through MCP, virtual callback advancement and robust hung-client/large-project qualification remain follow-ups.

## Verification and sources

Run `python tests/mcp_acceptance.py "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe" build/mcp-acceptance` after building. The test starts its own hardware-disabled application with real pipes and exercises lifecycle/order, tool discovery, canonical note editing, exact undo/redo, stale revisions, invalid/unsupported arguments, forbidden file commands, unknown tools, editor handler, PNG transport and EOF shutdown. Failure cleanup closes stdin first and terminates only the retained owned subprocess if graceful shutdown times out. Evidence is `build/mcp-acceptance/verification.json`, mcp-state.json, mcp-project.png and stderr.log; build/CTest logs are `build/mcp-build.log` / mcp-ctest.log. The actual MCP image was opened and inspected. Existing DAW regression suites remain separate.

Current primary specification, consulted 2026-10-12: [lifecycle](https://modelcontextprotocol.io/specification/2025-11-25/basic/lifecycle), [stdio transport](https://modelcontextprotocol.io/specification/2025-11-25/basic/transports), [tools](https://modelcontextprotocol.io/specification/2025-11-25/server/tools). No new runtime dependency or license change was introduced.

## Editor input and current qualification (2026-10-12)

State includes note rectangles, editor child-button bounds, selected notes and viewport/repeat range. Pointer input targets the actual child component under the initial coordinate; subsequent events retain that target, button and initial modifiers. Only one owned gesture may run at a time. Interleaved mutation and unpaired releases are rejected; cancel rolls back the canonical edit gesture. Project identity is checked on drag/release. Coordinates are editor-local logical pixels, not screen coordinates. Safe contextual key bindings pass through Studio.keyPressed. No global cursor or OS synthetic input is used.

Release build passed. Expanded raw subprocess acceptance passed **120 checks**, including paired note movement, exact undo, cancellation rollback, mutation exclusion, Ctrl+A/Ctrl+B, file-shortcut rejection, zoom and actual child-button clicking. Official SDK acceptance passed **16 checks**, including negotiation, ping, nine-tool discovery, canonical edit/error/undo/redo, shortcut, PNG and graceful shutdown. Both recorded application SHA256 `2c18ee386d207f49ac63ee1d81f1fe9667af4857054269b5abc1a0e434fce841`. CTest separately passed 3/3 (0.16/16.50/6.08 seconds). Both current component PNGs were opened and inspected. None establishes OS input, audio deadlines, listening, hardware or later concurrent CoProducer changes.

Evidence: `build/mcp-input-acceptance/verification.json`, `mcp-state.json`, `mcp-project.png`; `build/mcp-sdk-acceptance/verification.json`, `sdk-project.png`; `build/mcp-input-build.log`, `mcp-input-check.log`, `mcp-input-ctest.log`, `mcp-sdk-check.log`. The earlier 49-check result remains historical. All owned jobs exited; packaged 0.0.11 is unchanged.

Optional official-client reproduction after building:

```powershell
npm install --prefix build/mcp-client --ignore-scripts --no-audit --no-fund --save-exact @modelcontextprotocol/sdk@1.32.1
node tests/mcp_sdk_acceptance.mjs "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe" build/mcp-sdk-acceptance build/mcp-client/node_modules/@modelcontextprotocol/sdk
python tests/mcp_acceptance.py "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe" build/mcp-input-acceptance
```

The official SDK is a test-only MIT dependency (actual LICENSE read: copyright 2024 Anthropic, PBC), from https://github.com/modelcontextprotocol/typescript-sdk. Registry integrity: `sha512-2DdE+SJDtzLEEWzY1ZjY7Q+VcPhcV1KisD3zI4u0XZyktsjHum1mwbMI+JaulUBi2OZk+KJAi2uPXzxichPkdw==`. Local package-lock retains resolved transitive pins. No SDK code is linked into or required by the application; ordinary testing can use the Python client without it.

Browser navigation follow-up: scope 5 now means Browser (0 Global, 1 Rack, 2 Piano, 3 Playlist, 4 Mixer unchanged). This keeps selection validation consistent with state. No new tools or filesystem privileges; current 120-check regression evidence and limitations are recorded in BROWSER_WORKFLOW.md.


Development follow-up: projectVersion/512 bounded clip entries (kind, source ID, source offset units, mute, actual Playlist bounds), Channel mute/audio type and explicit clip.slice/slip/mute commands added. Existing file-operation boundary unchanged. Retained serialized String fixes dangling UTF-8 output pointer; expanded actual client152 checks include8 large Unicode IDs and clip state/undo. Older packaged0.0.14 predates this fix; new schema4 development requires next release qualification. Exact failure/evidence: PLAYLIST_MUTE.md.

Preview0.0.25 final source and packaged application3d14a9b2b552a6da095624475e2c722b0432b3d60293c7d3174e33169311abf8 each passed389 raw protocol checks and16 official SDK checks. Ten tools include test-only vibera_controller. Packaged verification lives in artifacts/preview-0.0.25/evidence/packaged-mcp and packaged-sdk. No hardware input or listening inferred.
