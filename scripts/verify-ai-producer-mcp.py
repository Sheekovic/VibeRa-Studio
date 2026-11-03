"""Actual production-stdio AI Producer workflow against local fake providers.

A loopback HTTP server stands in for an OpenAI-compatible model server, the
Anthropic Messages API and the ACE-Step 1.5 REST API. The real application runs
the agent loop, executes canonical MCP tools, generates "music" through the
ACE-Step client and places the clip. No real model, network or GPU is used;
this proves integration and protocol handling, not model or audio quality.
"""
import http.server
import importlib.util
import io
import json
from pathlib import Path
import struct
import sys
import threading
import time
import uuid
import wave

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("ai_client", ROOT / "skills/vibera-studio/scripts/stdio_client.py")
client_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(client_module)
app = Path(sys.argv[1]).resolve()
output = Path(sys.argv[2]).resolve() / ("attempt-" + uuid.uuid4().hex)
output.mkdir(parents=True, exist_ok=False)
checks = []
seen = {"openai": [], "anthropic": [], "ace": []}


def check(ok, label):
    if not ok:
        raise AssertionError(label)
    checks.append(label)


def wav_bytes(seconds=10):
    buffer = io.BytesIO()
    with wave.open(buffer, "wb") as w:
        w.setparams((2, 2, 48000, 0, "NONE", "not compressed"))
        w.writeframes(b"".join(struct.pack("<hh", 3000 if (i // 50) % 2 else -3000, 0) for i in range(48000 * seconds)))
    return buffer.getvalue()


def openai_reply(messages):
    tool_results = [m for m in messages if m.get("role") == "tool"]
    if not tool_results:
        return {"choices": [{"finish_reason": "tool_calls", "message": {"role": "assistant", "content": None,
                "tool_calls": [{"id": "call_gain", "type": "function", "function": {"name": "vibera_command",
                "arguments": json.dumps({"command": "channel.gain", "args": {"channel": 1, "value": 0.33}})}}]}}]}
    if len(tool_results) == 1:
        return {"choices": [{"finish_reason": "tool_calls", "message": {"role": "assistant", "content": "Adding a texture.",
                "tool_calls": [{"id": "call_music", "type": "function", "function": {"name": "vibera_generate_music",
                "arguments": json.dumps({"engine": "ace-step", "prompt": "lofi pad", "seconds": 10,
                                         "instrumental": True, "beat": 0})}}]}}]}
    return {"choices": [{"finish_reason": "stop", "message": {"role": "assistant", "content": "Done: kick quieter and a pad."}}]}


def anthropic_reply(body):
    messages = body["messages"]
    results = [b for m in messages if m["role"] == "user" and isinstance(m["content"], list)
               for b in m["content"] if b.get("type") == "tool_result"]
    if not results:
        return {"stop_reason": "tool_use", "content": [
            {"type": "thinking", "thinking": "", "signature": "sig"},
            {"type": "tool_use", "id": "toolu_pan", "name": "vibera_command",
             "input": {"command": "channel.pan", "args": {"channel": 2, "value": -0.5}}}]}
    return {"stop_reason": "end_turn", "content": [{"type": "text", "text": "Panned the snare left."}]}


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def send_json(self, value, status=200):
        data = json.dumps(value).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        if self.path.startswith("/oa/v1/models"):
            return self.send_json({"data": [{"id": "fake-model"}, {"id": "fake-model-2"}]})
        if self.path.startswith("/an/v1/models"):
            seen["anthropic"].append({"headers": dict(self.headers), "path": self.path})
            return self.send_json({"data": [{"id": "claude-opus-5-5"}]})
        if self.path == "/ace/health":
            return self.send_json({"status": "ok"})
        if self.path.startswith("/ace/v1/audio"):
            data = wav_bytes()
            self.send_response(200)
            self.send_header("Content-Type", "audio/wav")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return
        self.send_json({"error": "not found"}, 404)

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers["Content-Length"])) or b"{}")
        if self.path == "/oa/v1/chat/completions":
            seen["openai"].append(body)
            return self.send_json(openai_reply(body["messages"]))
        if self.path == "/an/v1/messages":
            seen["anthropic"].append({"headers": dict(self.headers), "body": body})
            return self.send_json(anthropic_reply(body))
        if self.path == "/ace/release_task":
            seen["ace"].append(body)
            return self.send_json({"code": 200, "data": {"task_id": "task-1"}})
        if self.path == "/ace/query_result":
            return self.send_json({"code": 200, "data": [{"task_id": "task-1", "status": 1,
                                   "result": json.dumps([{"file": "/v1/audio?path=%2Ftmp%2Fa.wav", "status": 1}])}]})
        self.send_json({"error": "not found"}, 404)


server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
port = server.server_address[1]
threading.Thread(target=server.serve_forever, daemon=True).start()
base = f"http://127.0.0.1:{port}"
client = None
passed = False
try:
    client = client_module.VibeRaClient(app)
    client.process.stdin.reconfigure(newline="\n")
    data = client.data
    ai = lambda **args: data("vibera_ai", args)
    channels = lambda: data("vibera_state")["channels"]
    client.wait_job(data("vibera_project", dict(action="new", discardChanges=True)))

    def wait(predicate, label, seconds=60):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            value = predicate()
            if value:
                return value
            time.sleep(.2)
        raise AssertionError("Timed out: " + label)

    state = ai(action="state")
    check(state["provider"]["preset"] == "anthropic" and state["provider"]["model"] == "claude-opus-5-5" and
          "hasApiKey" in state["provider"] and not any("key" in k.lower() and k != "hasApiKey" for k in state["provider"]),
          "Default provider is Claude Opus 5.5 and state never exposes keys")
    ai(action="configure", provider={"preset": "localcustom", "baseUrl": base + "/oa/v1", "model": "fake-model"},
       music={"aceStepUrl": base + "/ace", "aceStepAutoStart": False})
    ai(action="testConnection")
    connection = wait(lambda: (lambda c: c if not c["running"] and c["message"] != "Testing..." else None)(
        ai(action="state")["connection"]), "connection test")
    check(connection["ok"] and "fake-model" in connection["models"], "Test connection lists the server's models")

    before = {c["id"]: c for c in channels()}
    ai(action="chat", message="Make the kick quieter and add a lofi pad")
    wait(lambda: not ai(action="events")["busy"], "OpenAI-compatible producer run", 120)
    events = ai(action="events")["events"]
    check(any(e["kind"] == "assistant" and "Done" in e["text"] for e in events), "Producer final reply received")
    first = seen["openai"][0]
    names = {t["function"]["name"] for t in first["tools"]}
    check({"vibera_command", "vibera_state", "vibera_generate_music"} <= names and "vibera_ai" not in names and
          "vibera_pointer" not in names and first["messages"][0]["role"] == "system",
          "Agent offers canonical tools plus music generation, never vibera_ai or pointer simulation")
    second = seen["openai"][1]["messages"]
    check(any(m.get("role") == "tool" and m.get("tool_call_id") == "call_gain" for m in second),
          "Tool result returned to the model by call id")
    gain = next(c for c in channels() if c["id"] == 1)["gain"]
    check(abs(gain - 0.33) < 1e-6, "Agent tool call edited the project (Channel 1 gain)")
    check(seen["ace"] and seen["ace"][0]["audio_format"] == "wav" and seen["ace"][0]["lyrics"] == "[Instrumental]" and
          seen["ace"][0]["audio_duration"] == 10, "ACE-Step release_task received the generation request")
    added = wait(lambda: [c for c in channels() if c["id"] not in before], "AI audio clip placement")
    check(len(added) == 1, "Generated audio placed as a new Audio Clip Channel")
    music = ai(action="state")["music"]
    check(music["jobState"] == "succeeded" and music["audio"].endswith(".wav") and Path(music["audio"]).is_file(),
          "Music job succeeded and kept the WAV")

    # Direct MCP music generation (no agent).
    count = len(channels())
    ai(action="generateMusic", engine="ace-step", prompt="ambient", seconds=10, instrumental=True, beat=4)
    wait(lambda: len(channels()) == count + 1, "direct generateMusic placement")
    check(True, "vibera_ai generateMusic places a clip without the agent")
    refused = None
    try:
        ai(action="generateMusic", engine="ace-step", prompt="x", seconds=5)
    except RuntimeError as error:
        refused = str(error)
    check(refused and "seconds" in refused, "Invalid music duration refused with a structured error")

    # Anthropic dialect through a proxy URL (no first-party fallback), key in x-api-key.
    ai(action="configure", provider={"preset": "anthropic", "baseUrl": base + "/an", "model": "claude-opus-5-5",
                                     "effort": "medium"}, apiKey="sk-ant-test-key")
    check(ai(action="state")["provider"]["hasApiKey"] is True, "API key stored (write-only)")
    ai(action="resetChat")
    ai(action="chat", message="Pan the snare left")
    wait(lambda: not ai(action="events")["busy"], "Anthropic producer run", 120)
    request = next(r for r in seen["anthropic"] if "body" in r)
    headers = {k.lower(): v for k, v in request["headers"].items()}
    check(headers.get("x-api-key") == "sk-ant-test-key" and headers.get("anthropic-version") == "2026-10-21" and
          request["body"]["output_config"]["effort"] == "medium" and "fallbacks" not in request["body"] and
          request["body"]["tools"][0]["input_schema"],
          "Anthropic request headers, effort and tool schema")
    replay = [r for r in seen["anthropic"] if "body" in r][1]["body"]["messages"]
    check(replay[1]["content"][0]["type"] == "thinking" and replay[2]["content"][0]["type"] == "tool_result",
          "Assistant turn replayed unchanged (thinking kept) before the tool result")
    pan = next(c for c in channels() if c["id"] == 2)["pan"]
    check(abs(pan + 0.5) < 1e-6, "Anthropic agent edited the project (Channel 2 pan)")

    # Read-only mode: edits are refused even if the model calls a mutating tool.
    ai(action="configure", agentMayEditProject=False)
    ai(action="resetChat")
    seen["anthropic"].clear()
    ai(action="chat", message="Pan it again")
    wait(lambda: not ai(action="events")["busy"], "read-only run", 120)
    tools = [r for r in seen["anthropic"] if "body" in r][0]["body"]["tools"]
    check(all(t["name"] in ("vibera_state", "vibera_inspect", "vibera_commands", "vibera_snapshot", "vibera_selection")
              for t in tools), "Read-only mode offers only read tools")
    results = [b for m in [r for r in seen["anthropic"] if "body" in r][1]["body"]["messages"]
               if isinstance(m["content"], list) for b in m["content"] if b.get("type") == "tool_result"]
    check(results and results[0].get("is_error") is True and "Read-only" in results[0]["content"],
          "Read-only mode refuses a mutating call with an error result")
    passed = True
finally:
    server.shutdown()
    receipt = dict(passed=passed, checks=checks, output=str(output), network="loopback fakes only",
                   realModels=False, listening=False)
    (output / "verification.json").write_text(json.dumps(receipt, indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks))))
    if client:
        client.process.terminate()
sys.exit(0 if passed else 1)
