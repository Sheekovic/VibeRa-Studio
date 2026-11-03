"""CoProducer chat through the local Claude Code CLI and the loopback MCP bridge.

Runs the real application with VIBERA_CLAUDE_EXECUTABLE pointing at a fake Claude Code
(scripts/fake-claude.py) and Codex hidden. The fake launches the real `--mcp-bridge`
relay from the MCP config VibeRa writes, so detection, automatic provider choice,
argument policy (no built-in tools, only VibeRa's server pre-approved), model/effort,
stream-json parsing, session resume, config cleanup and read-only mode are exercised
for real. No Claude account or network is used.
"""
import importlib.util
import json
import os
from pathlib import Path
import sys
import time
import uuid

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("claude_client", ROOT / "skills/vibera-studio/scripts/stdio_client.py")
client_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(client_module)
app = Path(sys.argv[1]).resolve()
output = Path(sys.argv[2]).resolve() / ("attempt-" + uuid.uuid4().hex)
output.mkdir(parents=True, exist_ok=False)
wrapper = output / "claude.cmd"
wrapper.write_text(f'@"{sys.executable}" -I "{ROOT / "scripts/fake-claude.py"}" %*\r\n', encoding="ascii")
calls = output / "fake-claude.jsonl"
os.environ["VIBERA_CLAUDE_EXECUTABLE"] = str(wrapper)
os.environ["VIBERA_CODEX_EXECUTABLE"] = str(output / "missing-codex.cmd")
os.environ["VIBERA_FAKE_CLAUDE_LOG"] = str(calls)
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

    state = wait(lambda: (lambda s: s if s["claudeCli"]["loggedIn"] and s["provider"]["preset"] == "claude-cli"
                          else None)(ai(action="state")), "Claude Code detection and automatic provider switch")
    check(state["claudeCli"]["subscription"] == "max" and "email" not in json.dumps(state["claudeCli"]) and
          "should-not-leak" not in json.dumps(state), "Signed-in Claude subscription detected without the email")
    check(not state["codex"]["loggedIn"], "Codex hidden for this test")
    check(state["claudeCli"]["models"] == ["opus", "sonnet", "haiku", "fable"], "Claude model aliases offered")

    ai(action="configure", provider={"model": "sonnet", "effort": "xhigh"})
    ai(action="chat", message="Set the tempo to 103")
    wait(lambda: not ai(action="events")["busy"], "first Claude turn")
    events = ai(action="events")["events"]
    check(any(e["kind"] == "assistant" and "103 BPM (new session)" in e["text"] for e in events),
          "stream-json assistant text shown")
    check(any(e["kind"] == "tool" and e["text"].startswith("vibera_command") for e in events),
          "Tool call shown without the mcp__vibera__ prefix")
    check(data("vibera_state")["tempo"] == 103, "Claude edited the live project through the bridge")
    first = [r for r in records() if "argv" in r][0]
    argv = first["argv"]
    check(argv[0] == "-p" and argv[argv.index("--output-format") + 1] == "stream-json" and "--verbose" in argv,
          "Non-interactive stream-json mode")
    check("--strict-mcp-config" in argv and argv[argv.index("--tools") + 1] == "" and
          argv[argv.index("--allowedTools") + 1] == "mcp__vibera" and
          argv[argv.index("--permission-mode") + 1] == "dontAsk",
          "No built-in tools; only VibeRa's MCP server is loaded and pre-approved")
    check(argv[argv.index("--model") + 1] == "sonnet" and argv[argv.index("--effort") + 1] == "xhigh",
          "Chosen model and effort reach the CLI")
    check(first["config"]["mcpServers"]["vibera"]["args"][0] == "--mcp-bridge" and first["toolTimeout"] == "3600000",
          "MCP config points at the bridge with a long tool timeout")
    check("CoProducer" in first["prompt"] and "Set the tempo to 103" in first["prompt"],
          "First turn carries the producer brief and the request via stdin")
    tools = [r["tools"] for r in records() if "tools" in r][0]
    check("vibera_command" in tools and "vibera_generate_music" in tools and "vibera_ai" not in tools,
          "Bridge exposes the producer tool set")
    work = Path(first["cwd"])
    check(work.name == "claude-work" and not (work / "vibera-mcp.json").exists(),
          "Per-session MCP config (bridge token) removed after the turn")

    ai(action="configure", provider={"effort": "ultra"})
    ai(action="chat", message="Now 104")
    wait(lambda: not ai(action="events")["busy"], "resumed Claude turn")
    second = [r for r in records() if "argv" in r][1]
    check(second["argv"][second["argv"].index("--resume") + 1] == "fake-session-1" and
          second["prompt"].strip() == "Now 104", "Follow-up resumes the same session with just the new message")
    check("--effort" not in second["argv"], "Effort Claude Code does not accept is not sent")
    check(data("vibera_state")["tempo"] == 104, "Resumed turn edited the project")

    ai(action="configure", agentMayEditProject=False)
    ai(action="resetChat")
    ai(action="chat", message="Try 120")
    wait(lambda: not ai(action="events")["busy"], "read-only Claude turn")
    check(data("vibera_state")["tempo"] == 104, "Read-only mode blocks edits from Claude Code too")
    ai(action="configure", agentMayEditProject=True, provider={"model": "", "effort": "high"})
    passed = True
finally:
    receipt = dict(passed=passed, checks=checks, output=str(output), realClaude=False, network=False)
    (output / "verification.json").write_text(json.dumps(receipt, indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks))))
    if client:
        client.process.terminate()
sys.exit(0 if passed else 1)
