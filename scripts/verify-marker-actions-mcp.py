"""Marker actions (Start, Loop, Skip, Pause) through MCP (isolated --mcp-test instance).

marker.action sets a marker's playback action; only one Start marker exists; invalid actions are
refused; the action reads back on vibera_inspect markers, takes one Undo step and survives
save/reopen (schema 57). Playback behaviour is checked only when this instance's transport
actually advances (a device is available); otherwise the receipt records it as not run.

usage: python scripts/verify-marker-actions-mcp.py [executable] [output folder]
"""
import importlib.util
import json
from pathlib import Path
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe").resolve()
base = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else ROOT / "build/module-qualification/marker-actions"
output = base / ("attempt-" + uuid.uuid4().hex)
output.mkdir(parents=True)
spec = importlib.util.spec_from_file_location("client", ROOT / "skills/vibera-studio/scripts/stdio_client.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
checks, notes = [], []


def check(ok, label):
    if not ok:
        raise AssertionError(label)
    checks.append(label)


client = module.VibeRaClient(exe)
client.process.stdin.reconfigure(newline="\n")
passed = False
try:
    def call(tool, args=None):
        return client.call(tool, args or {}, allow_failure=True).get("structuredContent", {})

    def command(name, **args):
        return call("vibera_command", {"command": name, "args": args})

    def markers():
        return {m["name"]: m for m in call("vibera_inspect", {"entity": "markers"})["items"]}

    def wait(job, seconds=60):
        deadline = time.monotonic() + seconds
        while job.get("completionState") == "pending" and time.monotonic() < deadline:
            time.sleep(.1)
            job = call("vibera_project", {"action": "job", "operation": job["operation"]})
        return job

    call("vibera_project", {"action": "new", "template": "demo", "discardChanges": True})
    for name, beat in (("Intro", 4), ("Loop", 8), ("Out", 16), ("Skip", 20), ("End", 24)):
        command("marker.add", beat=beat, text=name)
    m = markers()
    check(command("marker.action", target=m["Intro"]["id"], text="start").get("completionState") == "completed",
          "marker.action sets Start")
    command("marker.action", target=m["Loop"]["id"], text="loop")
    command("marker.action", target=m["Skip"]["id"], text="skip")
    command("marker.action", target=m["End"]["id"], text="pause")
    m = markers()
    check([m[n]["action"] for n in ("Intro", "Loop", "Out", "Skip", "End")] == ["start", "loop", "", "skip", "pause"],
          "Actions read back on markers")
    command("marker.action", target=m["Out"]["id"], text="start")
    m = markers()
    check(m["Out"]["action"] == "start" and m["Intro"]["action"] == "", "Only one Start marker")
    command("edit.undo")
    m = markers()
    check(m["Intro"]["action"] == "start" and m["Out"]["action"] == "", "One Undo restores the previous Start marker")
    check(command("marker.action", target=m["Out"]["id"], text="jump").get("completionState") == "failed",
          "An unknown action is refused")

    project = output / "markers.vibra"
    job = wait(call("vibera_project", {"action": "save", "path": str(project), "overwrite": True}))
    check(job.get("completionState") == "completed", "Saves")
    call("vibera_project", {"action": "new", "template": "empty", "discardChanges": True})
    wait(call("vibera_project", {"action": "open", "path": str(project), "discardChanges": True}))
    m = markers()
    check([m[n]["action"] for n in ("Intro", "Loop", "Out", "Skip", "End")] == ["start", "loop", "", "skip", "pause"],
          "Marker actions survive save and reopen")

    call("vibera_transport", {"action": "song"})
    call("vibera_transport", {"action": "seek", "beat": 0})
    call("vibera_transport", {"action": "play"})
    time.sleep(1.0)
    first = call("vibera_state").get("positionBeat", 0)
    if first > .1:
        check(first >= 4, f"Play from the Song start begins at the Start marker ({first})")
        call("vibera_transport", {"action": "seek", "beat": 9})
        positions = []
        for _ in range(60):
            time.sleep(.1)
            positions.append(call("vibera_state").get("positionBeat", 0))
        check(max(positions) < 16 and any(p < 9 for p in positions[10:]),
              f"The Loop marker repeats 8..16 ({min(positions):.2f}..{max(positions):.2f})")
        call("vibera_transport", {"action": "stop"})
    else:
        notes.append("Transport did not advance in this instance (no audio device): playback actions not run here.")
    passed = True
finally:
    client.close()
    (output / "verification.json").write_text(json.dumps(dict(passed=passed, checks=checks, notes=notes), indent=2),
                                              encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks), notes=notes, last=checks[-1] if checks else None)))
sys.exit(0 if passed else 1)
