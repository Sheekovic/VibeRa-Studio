"""Pattern markers (Piano roll ruler) through MCP (isolated --mcp-test instance).

pattern.marker.add/rename/delete edit named positions inside a Pattern; they read back on
vibera_inspect patterns in beat order, take one Undo step each, belong to their Pattern only and
survive save/reopen (schema 57).

usage: python scripts/verify-pattern-markers-mcp.py [executable] [output folder]
"""
import importlib.util
import json
from pathlib import Path
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe").resolve()
base = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else ROOT / "build/module-qualification/pattern-markers"
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

    def wait(job):
        deadline = time.monotonic() + 60
        while job.get("completionState") == "pending" and time.monotonic() < deadline:
            time.sleep(.1)
            job = call("vibera_project", {"action": "job", "operation": job["operation"]})
        return job

    def markers(pattern):
        row = next(p for p in call("vibera_inspect", {"entity": "patterns"})["items"] if p["id"] == pattern)
        return [(m["beat"], m["name"]) for m in row["markers"]]

    call("vibera_project", {"action": "new", "template": "demo", "discardChanges": True})
    command("pattern.insert")
    patterns = [p["id"] for p in call("vibera_inspect", {"entity": "patterns"})["items"]]
    first, second = patterns[0], patterns[1]
    check(command("pattern.marker.add", pattern=first, beat=8, text="Fill").get("completionState") == "completed",
          "pattern.marker.add")
    command("pattern.marker.add", pattern=first, beat=2, text="Hook")
    check(markers(first) == [(2, "Hook"), (8, "Fill")] and markers(second) == [], "Markers read back in beat order, per Pattern")
    row = next(p for p in call("vibera_inspect", {"entity": "patterns"})["items"] if p["id"] == first)
    hook = row["markers"][0]["id"]
    command("pattern.marker.rename", pattern=first, target=hook, text="Drop")
    check(markers(first)[0] == (2, "Drop"), "Rename")
    command("edit.undo")
    check(markers(first)[0] == (2, "Hook"), "One Undo restores the name")
    check(command("pattern.marker.add", pattern=first, beat=-1).get("completionState") == "failed", "A negative beat is refused")
    project = output / "markers.vibra"
    check(wait(call("vibera_project", {"action": "save", "path": str(project), "overwrite": True})).get("completionState")
          == "completed", "Saves")
    call("vibera_project", {"action": "new", "template": "empty", "discardChanges": True})
    wait(call("vibera_project", {"action": "open", "path": str(project), "discardChanges": True}))
    check(markers(first) == [(2, "Hook"), (8, "Fill")], "Pattern markers survive save and reopen")
    command("pattern.marker.delete", pattern=first, target=hook)
    check(markers(first) == [(8, "Fill")], "Delete")
    passed = True
finally:
    client.close()
    (output / "verification.json").write_text(json.dumps(dict(passed=passed, checks=checks), indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks), last=checks[-1] if checks else None)))
sys.exit(0 if passed else 1)
