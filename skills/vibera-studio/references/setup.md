# Setup and compatibility

Read ../manifest.json for this package's version, minimum application version,
command contract and partial-readiness declaration. Discover the connected
application's tools; repository STATUS.md records delivered qualification.
Do not infer complete coverage from package metadata or a version number.

Use an MCP client supporting local stdio subprocesses, JSON-RPC2.0 UTF-8
newline framing, initialization, `notifications/initialized`, `tools/list` and
`tools/call`. The server negotiates2025-11-25,2025-06-18,2025-03-26 or2024-11-05.
It is not a Streamable HTTP server and does not attach to another running GUI.
Each connection launches a separate VibeRa application instance. Instance and
project IDs cannot be transferred between sessions.

`../connection.example.json` shows a common client configuration envelope.
Client settings formats differ: preserve the command/args values while adapting
the outer envelope to the client's documented stdio configuration. Replace the
absolute executable path when installing elsewhere. No vendor SDK is needed.
The package is not claimed tested with every AI client/model.

`scripts/stdio_client.py` is a standard-library-only integration client included
in the package. It launches only its own disposable `--mcp-test` instance and
provides `call`, `data`, job polling and verified graceful shutdown. It does not
attach to or terminate user applications. Regular interactive AI clients should
use the connection example instead.

`--mcp` launches the regular application with its normal audio/settings behavior.
`--mcp-test` is the hidden, isolated application test mode, with hardware ports
disabled; controller injection is test-only. Tests run against the exact binary
and do not establish the regular client's physical audio/MIDI qualification.

Do not write non-protocol messages to server stdin or stdout. Diagnostics belong
on stderr. Keep requests within the server's1MiB request limit. Fetch operation
contracts by ID or in bounded pages rather than requesting all schemas at once.

The transport/error conventions follow the primary
[stdio specification](https://modelcontextprotocol.io/specification/2025-11-25/basic/transports)
and [tools specification](https://modelcontextprotocol.io/specification/2025-11-25/server/tools).
VibeRa's polling job IDs are application operations, not an implementation of
the optional MCP Tasks extension. Cancellation is tool-specific: catalogue scans
and continuous edit gestures support explicit cancellation; project save/export
and plugin recovery do not. Read the relevant tool's result and workflow reference.
Loaded-plugin session parameter notifications are distinct from server-to-client
job-completion notifications. Poll jobs until terminal; durable cross-process
job recovery and protocol job-completion notifications remain open.

The optional standard-library Python helper was exercised with Python 3.13.1.
Other Python versions and client-specific configuration envelopes are not
qualified by that result. A normal stdio MCP client launches the native executable
directly and does not need Python or a model-vendor SDK.

Installed third-party plugin qualification uses a normal Windows desktop/user
profile. Two restricted-agent executions could not finish four-worker startup
within the unchanged 25s guard; the same candidate passed with desktop permissions.
The exact blocked resource is unidentified. Process isolation remains active;
separate plugin workers are not a guarantee that arbitrary plugins function in an
OS-restricted agent sandbox. Do not broaden permissions silently in an AI client.
