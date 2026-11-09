"""Receive notes from (multitimbral live MIDI) through MCP (isolated --mcp-test instance).

channel.receiveMidi sets the MIDI channel a Channel always plays from; one Channel per MIDI
channel (setting it on another clears the first), 0 turns it off, 17 is refused, one Undo each,
and it survives save/reopen (schema 57). The live routing itself is covered by the
midiReceiveAndScaleSnapTests stage of musical_acceptance.

usage: python scripts/verify-midi-receive-mcp.py [executable] [output folder]
"""
import importlib.util
import json
from pathlib import Path
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe").resolve()
base = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else ROOT / "build/module-qualification/midi-receive"
output = base / ("attempt-" + uuid.uuid4().hex)
output.mkdir(parents=True)
spec = importlib.util.spec_from_file_location("client", ROOT / "skills/vibera-studio/scripts/stdio_client.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
checks = []


def check(ok, label):
    if not ok:
        raise AssertionError(label)
    checks.append(label)


client = module.VibeRaClient(exe)
client.process.stdin.reconfigure(newline="\n")
passed = False
try:
    call = lambda tool, args=None: client.call(tool, args or {}, allow_failure=True).get("structuredContent", {})
    command = lambda name, **args: call("vibera_command", {"command": name, "args": args})

    def receiving():
        return {c["name"]: c["receiveMidiChannel"] for c in call("vibera_inspect", {"entity": "channels"})["items"]}

    def wait(job):
        deadline = time.monotonic() + 60
        while job.get("completionState") == "pending" and time.monotonic() < deadline:
            time.sleep(.1)
            job = call("vibera_project", {"action": "job", "operation": job["operation"]})
        return job

    call("vibera_project", {"action": "new", "template": "demo", "discardChanges": True})
    rows = call("vibera_inspect", {"entity": "channels"})["items"]
    a, b = rows[0], rows[1]
    check(command("channel.receiveMidi", channel=a["id"], value=2).get("completionState") == "completed" and
          receiving()[a["name"]] == 2, "A Channel receives MIDI channel 2")
    command("channel.receiveMidi", channel=b["id"], value=2)
    r = receiving()
    check(r[b["name"]] == 2 and r[a["name"]] == 0, "One Channel per MIDI channel: the first is cleared")
    command("edit.undo")
    r = receiving()
    check(r[a["name"]] == 2 and r[b["name"]] == 0, "One Undo restores the previous receiver")
    check(command("channel.receiveMidi", channel=a["id"], value=17).get("completionState") == "failed",
          "MIDI channel 17 is refused")
    project = output / "receive.vibra"
    check(wait(call("vibera_project", {"action": "save", "path": str(project), "overwrite": True}))
          .get("completionState") == "completed", "Saves")
    call("vibera_project", {"action": "new", "template": "empty", "discardChanges": True})
    wait(call("vibera_project", {"action": "open", "path": str(project), "discardChanges": True}))
    check(receiving()[a["name"]] == 2, "The receive channel survives save and reopen")
    command("channel.receiveMidi", channel=a["id"], value=0)
    check(receiving()[a["name"]] == 0, "0 turns it off")
    passed = True
finally:
    client.close()
    (output / "verification.json").write_text(json.dumps(dict(passed=passed, checks=checks), indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks), last=checks[-1] if checks else None)))
sys.exit(0 if passed else 1)
