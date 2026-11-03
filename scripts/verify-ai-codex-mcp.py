"""CoProducer chat through a local agent CLI (Codex) and the loopback MCP bridge.

Runs the real application with VIBERA_CODEX_EXECUTABLE pointing at a fake Codex
(scripts/fake-codex.py). The fake launches the real `--mcp-bridge` relay, so the
loopback host, token check, tool catalog, canonical edits, JSONL parsing, resume
and argument policy are exercised for real. No ChatGPT account or network is used.
"""
import importlib.util
import json
import os
from pathlib import Path
import socket
import sys
import time
import uuid

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("codex_client", ROOT / "skills/vibera-studio/scripts/stdio_client.py")
client_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(client_module)
app = Path(sys.argv[1]).resolve()
output = Path(sys.argv[2]).resolve() / ("attempt-" + uuid.uuid4().hex)
output.mkdir(parents=True, exist_ok=False)
wrapper = output / "codex.cmd"
wrapper.write_text(f'@"{sys.executable}" -I "{ROOT / "scripts/fake-codex.py"}" %*\r\n', encoding="ascii")
calls = output / "fake-codex.jsonl"
os.environ["VIBERA_CODEX_EXECUTABLE"] = str(wrapper)
os.environ["VIBERA_FAKE_CODEX_LOG"] = str(calls)
os.environ["VIBERA_CLAUDE_EXECUTABLE"] = str(output / "missing-claude.cmd")
checks = []


def check(ok, label):
    if not ok:
        raise AssertionError(label)
    checks.append(label)


def records():
    return [json.loads(line) for line in calls.read_text(encoding="utf-8").splitlines()] if calls.exists() else []


client = None
passed = False
try:
    client = client_module.VibeRaClient(app)
    client.process.stdin.reconfigure(newline="\n")
    data = client.data
    ai = lambda **args: data("vibera_ai", args)
    client.wait_job(data("vibera_project", dict(action="new", discardChanges=True)))

    def wait(predicate, label, seconds=90):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            value = predicate()
            if value:
                return value
            time.sleep(.25)
        raise AssertionError("Timed out: " + label)

    state = wait(lambda: (lambda s: s if s["codex"]["loggedIn"] and s["provider"]["preset"] == "codex-cli" else None)(
        ai(action="state")), "Codex detection and automatic provider switch")
    check(state["codex"]["chatgpt"] and state["codex"]["version"] == "codex-cli 9.9.9-fake",
          "Signed-in ChatGPT Codex CLI detected")
    check(state["provider"]["preset"] == "codex-cli",
          "Unconfigured key provider switches automatically to the ChatGPT subscription")
    ai(action="chat", message="Set the tempo to 97")
    wait(lambda: not ai(action="events")["busy"], "first Codex turn")
    events = ai(action="events")["events"]
    check(any(e["kind"] == "assistant" and "97 BPM (new thread)" in e["text"] for e in events),
          "Codex JSONL agent message shown")
    check(any(e["kind"] == "tool" and e["text"].startswith("vibera_command") for e in events), "Tool call shown")
    check(data("vibera_state")["tempo"] == 97, "Codex edited the live project through the bridge")
    first = [r for r in records() if "argv" in r][0]
    argv = first["argv"]
    joined = " ".join(argv)
    check(argv[0] == "exec" and "--json" in argv and "-" == argv[-1] and "sandbox_mode='read-only'" in joined and
          "approval_policy='never'" in joined and "mcp_servers.vibera.default_tools_approval_mode='approve'" in joined
          and "-C" in argv, "Codex runs read-only in a VibeRa folder with only VibeRa tools pre-approved")
    check("CoProducer" in first["prompt"] and "Set the tempo to 97" in first["prompt"],
          "First turn carries the producer brief and the request via stdin")
    tools = [r["tools"] for r in records() if "tools" in r][0]
    check("vibera_command" in tools and "vibera_generate_music" in tools and "vibera_ai" not in tools and
          "vibera_pointer" not in tools, "Bridge exposes the producer tool set")

    ai(action="chat", message="Now 98")
    wait(lambda: not ai(action="events")["busy"], "resumed Codex turn")
    second = [r for r in records() if "argv" in r][1]
    check(second["argv"][:3] == ["exec", "resume", "fake-thread-1"] and second["prompt"].strip() == "Now 98",
          "Follow-up resumes the same Codex thread with just the new message")
    check(data("vibera_state")["tempo"] == 98, "Resumed turn edited the project")

    # The bridge refuses a connection with a wrong token.
    config = {a.partition("=")[0]: a.partition("=")[2] for a in second["argv"] if "=" in a}
    port = int(config["mcp_servers.vibera.args"].strip("[]").split(",")[1].strip(" '"))
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=5) as s:
            s.sendall(b'{"vibera_bridge_token":"wrong"}\n' + json.dumps({"jsonrpc": "2.0", "id": 1, "method": "tools/list"}).encode() + b"\n")
            s.settimeout(5)
            reply = s.recv(4096)
        check(reply == b"", "Wrong bridge token is dropped without a reply")
    except ConnectionRefusedError:
        check(True, "Bridge closed after the turn (no listener to attack)")

    models = ai(action="state")["codex"]["models"]
    check([m["slug"] for m in models] == ["fake-pro", "fake-mini"] and models[0]["efforts"][-1] == "ultra" and
          models[1]["defaultEffort"] == "medium", "Codex model catalog listed in priority order, hidden models omitted")
    check("model_reasoning_effort='high'" in " ".join(first["argv"]),
          "Default effort (high) is passed because every listed model accepts it")
    ai(action="configure", provider={"model": "fake-pro", "effort": "xhigh"})
    ai(action="chat", message="Now 99")
    wait(lambda: not ai(action="events")["busy"], "Codex turn with a chosen model and effort")
    third = " ".join([r for r in records() if "argv" in r][2]["argv"])
    check("model='fake-pro'" in third and "model_reasoning_effort='xhigh'" in third,
          "Chosen Codex model and reasoning effort reach the CLI")
    ai(action="configure", provider={"model": "fake-mini", "effort": "ultra"})
    check(ai(action="state")["codex"]["effectiveEffort"] == "", "Unsupported effort falls back to the model default")
    ai(action="chat", message="Now 98")
    wait(lambda: not ai(action="events")["busy"], "Codex turn with an unsupported effort")
    fourth = " ".join([r for r in records() if "argv" in r][3]["argv"])
    check("model='fake-mini'" in fourth and "model_reasoning_effort" not in fourth,
          "Effort the model does not accept is not sent")
    rejected = client.call("vibera_ai", dict(action="configure", provider={"effort": "turbo"}), allow_failure=True)
    check(bool(rejected.get("isError")), "Invalid effort rejected")
    panel = ai(action="panel", visible=True, tab="midi")["panel"]
    check(panel["visible"] and panel["tab"] == "midi", "CoProducer side panel opens on the MIDI tab")
    check(not ai(action="panel", visible=False)["panel"]["visible"], "CoProducer side panel closes")
    ai(action="configure", provider={"model": "", "effort": "high"})
    ai(action="configure", agentMayEditProject=False)
    ai(action="resetChat")
    ai(action="chat", message="Try 120")
    wait(lambda: not ai(action="events")["busy"], "read-only Codex turn")
    check(data("vibera_state")["tempo"] == 98, "Read-only mode blocks edits from the CLI agent too")
    passed = True
finally:
    receipt = dict(passed=passed, checks=checks, output=str(output), realCodex=False, network=False)
    (output / "verification.json").write_text(json.dumps(receipt, indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks))))
    if client:
        client.process.terminate()
sys.exit(0 if passed else 1)
