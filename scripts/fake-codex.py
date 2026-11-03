"""Test double for the Codex CLI (`codex exec --json`), used by verify-ai-codex-mcp.py.

Implements `--version`, `login status`, `debug models` and `exec [resume <id>] ... -`: reads the
prompt from stdin, launches the MCP server named by `-c mcp_servers.vibera.*`
(VibeRa's --mcp-bridge), performs the MCP handshake, lists tools, sets the tempo
through vibera_command and prints Codex-style JSONL events. Every invocation's
argv and prompt are appended to $VIBERA_FAKE_CODEX_LOG for assertions.
"""
import json
import os
import subprocess
import sys


def log(record):
    path = os.environ.get("VIBERA_FAKE_CODEX_LOG")
    if path:
        with open(path, "a", encoding="utf-8") as f:
            f.write(json.dumps(record) + "\n")


def emit(event):
    sys.stdout.write(json.dumps(event) + "\n")
    sys.stdout.flush()


def toml_literal(value):
    value = value.strip()
    return value[1:-1] if value.startswith("'") and value.endswith("'") else value


def main():
    argv = sys.argv[1:]
    if argv == ["--version"]:
        print("codex-cli 9.9.9-fake")
        return 0
    if argv[:2] == ["login", "status"]:
        print("Logged in using ChatGPT")
        return 0
    if argv[:2] == ["debug", "models"]:
        levels = lambda names: [{"effort": n, "description": n} for n in names]
        print(json.dumps({"models": [
            {"slug": "fake-mini", "display_name": "Fake Mini", "visibility": "list", "priority": 2,
             "default_reasoning_level": "medium", "supported_reasoning_levels": levels(["low", "medium", "high"])},
            {"slug": "fake-pro", "display_name": "Fake Pro", "visibility": "list", "priority": 1,
             "default_reasoning_level": "low",
             "supported_reasoning_levels": levels(["low", "medium", "high", "xhigh", "max", "ultra"])},
            {"slug": "fake-hidden", "display_name": "Hidden", "visibility": "hide", "priority": 0,
             "default_reasoning_level": "low", "supported_reasoning_levels": levels(["low"])}]}))
        return 0
    if not argv or argv[0] != "exec":
        print("unsupported", file=sys.stderr)
        return 2
    prompt = sys.stdin.read()
    log({"argv": argv, "prompt": prompt})
    config = {}
    for i, a in enumerate(argv):
        if a == "-c" and i + 1 < len(argv):
            key, _, value = argv[i + 1].partition("=")
            config[key] = value
    command = toml_literal(config["mcp_servers.vibera.command"])
    args = [toml_literal(x) for x in config["mcp_servers.vibera.args"].strip("[]").split(",")]
    thread = argv[argv.index("resume") + 1] if "resume" in argv else "fake-thread-1"
    emit({"type": "thread.started", "thread_id": thread})
    emit({"type": "turn.started"})
    bridge = subprocess.Popen([command] + args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True,
                              encoding="utf-8")
    serial = [0]

    def rpc(method, params=None, notify=False):
        message = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            message["params"] = params
        if not notify:
            serial[0] += 1
            message["id"] = serial[0]
        bridge.stdin.write(json.dumps(message) + "\n")
        bridge.stdin.flush()
        return None if notify else json.loads(bridge.stdout.readline())

    rpc("initialize", {"protocolVersion": "2025-06-18", "capabilities": {}, "clientInfo": {"name": "fake-codex"}})
    rpc("notifications/initialized", notify=True)
    tools = [t["name"] for t in rpc("tools/list")["result"]["tools"]]
    log({"tools": tools})
    tempo = 97 if "resume" not in argv else 98
    emit({"type": "item.started", "item": {"id": "i1", "type": "mcp_tool_call", "server": "vibera",
                                           "tool": "vibera_command",
                                           "arguments": {"command": "project.tempo", "args": {"value": tempo}},
                                           "status": "in_progress"}})
    result = rpc("tools/call", {"name": "vibera_command",
                                "arguments": {"command": "project.tempo", "args": {"value": tempo}}})
    failed = "error" in result or result.get("result", {}).get("isError")
    log({"call": result})
    emit({"type": "item.completed", "item": {"id": "i1", "type": "mcp_tool_call", "server": "vibera",
                                             "tool": "vibera_command", "status": "failed" if failed else "completed",
                                             "error": result.get("error")}})
    bridge.stdin.close()
    bridge.wait(timeout=20)
    emit({"type": "item.completed", "item": {"id": "i2", "type": "agent_message",
                                             "text": f"Tempo set to {tempo} BPM ({'resumed ' + thread if 'resume' in argv else 'new thread'})."}})
    emit({"type": "turn.completed", "usage": {"input_tokens": 1, "output_tokens": 1}})
    return 0


if __name__ == "__main__":
    sys.exit(main())
