"""Expression map acceptance through MCP (isolated --mcp-test instance).

channel.articulations sets a Channel's articulations (note names, C5 = 60, or numbers);
notes.articulation assigns one to notes; playback (vibera_inspect scheduledNotes) sends each
articulation's keyswitch just before its notes, once per chord, and nothing for plain notes. Bad
entries and unmapped articulations are refused, Undo restores, and the map and note articulations
survive save/reopen (schema 57).

usage: python scripts/verify-expression-maps-mcp.py [executable] [output folder]
"""
import importlib.util
import json
from pathlib import Path
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe").resolve()
base = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else ROOT / "build/module-qualification/expression-maps"
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
    def call(tool, args=None):
        return client.call(tool, args or {}, allow_failure=True).get("structuredContent", {})

    def command(name, **args):
        return call("vibera_command", {"command": name, "args": args})

    def wait(job, seconds=60):
        deadline = time.monotonic() + seconds
        while job.get("completionState") == "pending" and time.monotonic() < deadline:
            time.sleep(.1)
            job = call("vibera_project", {"action": "job", "operation": job["operation"]})
        return job

    def channel_map(channel):
        row = next(c for c in call("vibera_inspect", {"entity": "channels"})["items"] if c["id"] == channel)
        return [(a["name"], a["keyswitch"]) for a in row["articulations"]]

    call("vibera_project", {"action": "new", "template": "empty", "discardChanges": True})
    state = call("vibera_state")
    channel, pattern = state["rack"]["rows"][0]["id"], state["selectedPattern"]
    result = command("channel.articulations", channel=channel, text="Sustain=C1; Staccato=C#1\nPizzicato=24")
    check(result.get("completionState") == "completed", "channel.articulations sets a map: " + str(result.get("error"))[:120])
    check(channel_map(channel) == [("Sustain", 12), ("Staccato", 13), ("Pizzicato", 24)],
          f"Note names and numbers read back as keyswitches ({channel_map(channel)})")
    check(command("channel.articulations", channel=channel, text="Legato=H9").get("completionState") == "failed",
          "A bad keyswitch is refused")

    for beat, pitch in ((0, 60), (0, 64), (1, 67), (2, 72)):
        command("notes.add", channel=channel, pattern=pattern, pitch=pitch, beat=beat, length=.5, value=.8)
    notes = sorted(call("vibera_inspect", {"entity": "notes", "limit": 50})["items"], key=lambda n: (n["beat"], n["pitch"]))
    chord, single = [notes[0]["id"], notes[1]["id"]], [notes[2]["id"]]
    check(command("notes.articulation", channel=channel, pattern=pattern, ids=chord, value=2).get("completionState") == "completed",
          "notes.articulation marks a chord Staccato")
    command("notes.articulation", channel=channel, pattern=pattern, ids=single, value=3)
    check(command("notes.articulation", channel=channel, pattern=pattern, ids=single, value=9).get("completionState") == "failed",
          "An articulation outside the map is refused")
    by_id = {n["id"]: n["articulation"] for n in call("vibera_inspect", {"entity": "notes", "limit": 50})["items"]}
    check([by_id[i] for i in chord + single] == [2, 2, 3] and by_id[notes[3]["id"]] == 0, "Notes read back their articulation")

    events = call("vibera_inspect", {"entity": "scheduledNotes", "pattern": pattern, "limit": 50})["items"]
    plan = [(e["beat"], e["pitch"], e["keyswitch"]) for e in events]
    check(plan[0] == (0, 13, True) and sorted(plan[1:3]) == [(0, 60, False), (0, 64, False)],
          f"One Staccato keyswitch precedes the chord ({plan})")
    check(plan[3] == (1, 24, True) and plan[4] == (1, 67, False), "The Pizzicato keyswitch precedes its note")
    check(plan[5:] == [(2, 72, False)], "A plain note sends no keyswitch")
    check(all(e["length"] <= 1 / 64 for e in events if e["keyswitch"]), "Keyswitches are short notes")

    command("edit.undo")
    by_id = {n["id"]: n["articulation"] for n in call("vibera_inspect", {"entity": "notes", "limit": 50})["items"]}
    check(by_id[single[0]] == 0, "Undo removes the last articulation")
    command("edit.redo")

    project = output / "expression.vibra"
    job = wait(call("vibera_project", {"action": "save", "path": str(project), "overwrite": True}))
    check(job.get("completionState") == "completed" and b'VIBERA version="57"' in project.read_bytes(), "Saves at schema 57")
    call("vibera_project", {"action": "new", "template": "empty", "discardChanges": True})
    wait(call("vibera_project", {"action": "open", "path": str(project), "discardChanges": True}))
    state = call("vibera_state")
    reopened = state["rack"]["rows"][0]["id"]
    check(channel_map(reopened) == [("Sustain", 12), ("Staccato", 13), ("Pizzicato", 24)], "The map survives reopen")
    articulations = sorted(n["articulation"] for n in call("vibera_inspect", {"entity": "notes", "limit": 50})["items"])
    check(articulations == [0, 2, 2, 3], f"Note articulations survive reopen ({articulations})")
    passed = True
finally:
    client.close()
    (output / "verification.json").write_text(json.dumps(dict(passed=passed, checks=checks), indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks), last=checks[-1] if checks else None)))
sys.exit(0 if passed else 1)
