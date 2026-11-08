"""Chord Track acceptance through MCP (isolated --mcp-test instance).

chord.fill writes one chord per bar and reads back with pitch classes; unknown symbols are
refused; chord.set / chord.add / chord.delete edit it; notes.followChords moves notes to the
nearest tone of the chord at their Song position (ties go down) as one Undo step; the Chord
Track survives save and reopen (schema 57).

usage: python scripts/verify-chord-track-mcp.py [executable] [output folder]
"""
import importlib.util
import json
from pathlib import Path
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe").resolve()
base = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else ROOT / "build/module-qualification/chord-track"
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

    def chords():
        return call("vibera_inspect", {"entity": "chords"}).get("items", [])

    call("vibera_project", {"action": "new", "template": "empty", "discardChanges": True})
    check(command("chord.fill", text="C | Am | F | G", beat=0).get("completionState") == "completed", "chord.fill writes a progression")
    rows = chords()
    check([r["symbol"] for r in rows] == ["C", "Am", "F", "G"] and [r["beat"] for r in rows] == [0, 4, 8, 12],
          "One chord per 4/4 bar is read back in order")
    check(rows[1]["pitchClasses"] == [9, 0, 4], "Am reads back as A C E pitch classes")
    check(command("chord.add", beat=16, text="Xyz").get("completionState") == "failed", "An unknown chord symbol is refused")
    check(command("chord.add", beat=16, text="Bb/D").get("completionState") == "completed" and chords()[-1]["symbol"] == "Bb/D",
          "A slash chord is added")
    check(command("chord.set", target=chords()[-1]["id"], text="Dm7").get("completionState") == "completed" and
          chords()[-1]["symbol"] == "Dm7", "chord.set changes a chord")
    check(command("chord.delete", ids=[chords()[-1]["id"]]).get("completionState") == "completed" and len(chords()) == 4,
          "chord.delete removes it")

    state = call("vibera_state")
    channel, pattern = state["rack"]["rows"][0]["id"], state["selectedPattern"]
    command("clip.add", pattern=pattern, beat=0, lane=0)
    for beat, pitch in ((0, 61), (4, 62), (8, 66), (12, 68)):
        command("notes.add", channel=channel, pattern=pattern, pitch=pitch, beat=beat, length=1, value=.8)
    before = sorted((n["beat"], n["pitch"]) for n in call("vibera_inspect", {"entity": "notes", "limit": 500})["items"])
    result = command("notes.followChords", channel=channel, pattern=pattern)
    check(result.get("completionState") == "completed", "notes.followChords applies: " + str(result.get("error"))[:120])
    after = sorted((n["beat"], n["pitch"]) for n in call("vibera_inspect", {"entity": "notes", "limit": 500})["items"])
    # C: C#->C;  Am: D->C (tie with E goes down);  F: F#->F;  G: G#->G.
    check(after == [(0, 60), (4, 60), (8, 65), (12, 67)], f"Notes move to the nearest chord tone ({after})")
    command("edit.undo")
    check(sorted((n["beat"], n["pitch"]) for n in call("vibera_inspect", {"entity": "notes", "limit": 500})["items"]) == before,
          "One Undo restores every note")

    project = output / "chords.vibra"
    job = call("vibera_project", {"action": "save", "path": str(project), "overwrite": True})
    deadline = time.monotonic() + 60
    while job.get("completionState") == "pending" and time.monotonic() < deadline:
        time.sleep(.1)
        job = call("vibera_project", {"action": "job", "operation": job["operation"]})
    check(job.get("completionState") == "completed" and b'VIBERA version="57"' in project.read_bytes(), "The project saves at schema 57")
    call("vibera_project", {"action": "new", "template": "empty", "discardChanges": True})
    check(chords() == [], "A new project has an empty Chord Track")
    job = call("vibera_project", {"action": "open", "path": str(project), "discardChanges": True})
    while job.get("completionState") == "pending" and time.monotonic() < deadline + 60:
        time.sleep(.1)
        job = call("vibera_project", {"action": "job", "operation": job["operation"]})
    check([r["symbol"] for r in chords()] == ["C", "Am", "F", "G"], "The Chord Track survives save and reopen")
    passed = True
finally:
    client.close()
    (output / "verification.json").write_text(json.dumps(dict(passed=passed, checks=checks), indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks))))
sys.exit(0 if passed else 1)
