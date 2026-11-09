"""Interpolate tool for Piano roll event data, through MCP (isolated --mcp-test instance).

notes.controller.interpolate keeps the first and last events of a lane inside the range and turns
everything between into a straight line every 1/16 beat; other lanes are untouched; a range with
fewer than two events is refused; one Undo restores the lane.

usage: python scripts/verify-controller-interpolate-mcp.py [executable] [output folder]
"""
import importlib.util
import json
from pathlib import Path
import sys
import uuid

ROOT = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe").resolve()
base = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else ROOT / "build/module-qualification/controller-interpolate"
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
    call("vibera_project", {"action": "new", "template": "empty", "discardChanges": True})
    state = call("vibera_state")
    channel, pattern = state["rack"]["rows"][0]["id"], state["selectedPattern"]

    def lane(text="cc", number=1):
        rows = call("vibera_inspect", {"entity": "controllers", "pattern": pattern, "limit": 512})["items"]
        kind = {"cc": 0, "pitchBend": 1, "pressure": 2}[text]
        return sorted((round(r["beat"], 4), round(r["value"], 4)) for r in rows
                      if r.get("type") in (kind, text) and (kind != 0 or r.get("number") == number))

    for beat, value in ((0, .2), (.5, .9), (1.3, .1), (2, .6)):
        command("notes.controller.add", pattern=pattern, channel=channel, text="cc", pitch=1, beat=beat, value=value)
    command("notes.controller.add", pattern=pattern, channel=channel, text="cc", pitch=7, beat=1, value=.5)
    before = lane()
    check(len(before) == 4, f"Four CC1 events ({before})")
    result = command("notes.controller.interpolate", pattern=pattern, channel=channel, text="cc", pitch=1, beat=0, length=2)
    check(result.get("completionState") == "completed", "Interpolate applies: " + str(result.get("error"))[:120])
    after = lane()
    check(len(after) == 33 and after[0] == (0, .2) and after[-1] == (2, .6), f"Endpoints kept, 1/16-beat steps ({len(after)})")
    check(all(abs(v - (.2 + .2 * b)) < 1e-3 for b, v in after), "Every value lies on the straight line .2 -> .6")
    check(lane("cc", 7) == [(1, .5)], "Other lanes are untouched")
    check(command("notes.controller.interpolate", pattern=pattern, channel=channel, text="cc", pitch=7, beat=0,
                  length=2).get("completionState") == "failed", "A lane with one event in range is refused")
    command("edit.undo")
    check(lane() == before, "One Undo restores the lane")
    passed = True
finally:
    client.close()
    (output / "verification.json").write_text(json.dumps(dict(passed=passed, checks=checks), indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks), last=checks[-1] if checks else None)))
sys.exit(0 if passed else 1)
