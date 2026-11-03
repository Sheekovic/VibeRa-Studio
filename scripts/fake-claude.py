"""Test double for the Claude Code CLI (`claude -p --output-format stream-json`), used by
verify-ai-claude-mcp.py.

Implements `--version`, `auth status` and `-p ...`: reads the prompt from stdin, launches
the MCP server named in the `--mcp-config` file (VibeRa's --mcp-bridge), performs the MCP
handshake, lists tools, sets the tempo through vibera_command and prints Claude-style
stream-json events. Every invocation's argv, prompt, config and environment are appended
to $VIBERA_FAKE_CLAUDE_LOG for assertions.
"""
import json
import os
import subprocess
import sys


def log(record):
    path = os.environ.get("VIBERA_FAKE_CLAUDE_LOG")
    if path:
        with open(path, "a", encoding="utf-8") as f:
            f.write(json.dumps(record) + "\n")


def emit(event):
    sys.stdout.write(json.dumps(event) + "\n")
    sys.stdout.flush()


def main():
    argv = sys.argv[1:]
    if argv == ["--version"]:
        print("9.9.9 (Claude Code)")
        return 0
    if argv[:2] == ["auth", "status"]:
        print(json.dumps({"loggedIn": True, "authMethod": "claude.ai", "subscriptionType": "max",
                          "email": "should-not-leak@example.com"}))
        return 0
    if "-p" not in argv:
        print("unsupported", file=sys.stderr)
        return 2
    prompt = sys.stdin.read()
    config_path = argv[argv.index("--mcp-config") + 1]
    with open(config_path, encoding="utf-8") as f:
        config = json.load(f)
    log({"argv": argv, "prompt": prompt, "config": config, "cwd": os.getcwd(),
         "toolTimeout": os.environ.get("MCP_TOOL_TIMEOUT")})
    session = argv[argv.index("--resume") + 1] if "--resume" in argv else "fake-session-1"
    emit({"type": "system", "subtype": "init", "session_id": session, "tools": []})
    server = config["mcpServers"]["vibera"]
    bridge = subprocess.Popen([server["command"]] + server["args"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                              text=True, encoding="utf-8")
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

    rpc("initialize", {"protocolVersion": "2025-06-18", "capabilities": {}, "clientInfo": {"name": "fake-claude"}})
    rpc("notifications/initialized", notify=True)
    tools = [t["name"] for t in rpc("tools/list")["result"]["tools"]]
    log({"tools": tools})
    tempo = 103 if "--resume" not in argv else 104
    arguments = {"command": "project.tempo", "args": {"value": tempo}}
    emit({"type": "assistant", "session_id": session, "message": {"content": [
        {"type": "text", "text": "Setting the tempo."},
        {"type": "tool_use", "id": "t1", "name": "mcp__vibera__vibera_command", "input": arguments}]}})
    result = rpc("tools/call", {"name": "vibera_command", "arguments": arguments})
    failed = "error" in result or result.get("result", {}).get("isError")
    emit({"type": "user", "session_id": session, "message": {"content": [
        {"type": "tool_result", "tool_use_id": "t1", "is_error": bool(failed),
         "content": json.dumps(result.get("error") or result.get("result"))[:200]}]}})
    bridge.stdin.close()
    bridge.wait(timeout=20)
    text = f"Tempo set to {tempo} BPM ({'resumed ' + session if '--resume' in argv else 'new session'})."
    emit({"type": "assistant", "session_id": session, "message": {"content": [{"type": "text", "text": text}]}})
    emit({"type": "result", "subtype": "success", "is_error": False, "result": text, "session_id": session})
    return 0


if __name__ == "__main__":
    sys.exit(main())
