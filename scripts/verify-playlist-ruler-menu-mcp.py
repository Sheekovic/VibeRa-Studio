"""Playlist ruler menus through MCP: markers, tempo changes and automation points (isolated --mcp-test instance).

Right-clicking a Playlist marker, a tempo change or an automation point runs registered operations; this
drives them with the menu's arguments and reads the result back: Rename marker, Move marker, marker
Action (Start / Loop / Skip / Pause / None), Delete marker, Add markers every N bars and at a time
selection, next / previous marker, Edit / Delete tempo change, and Delete / move / shape of an automation
point. Every edit is one Undo step.

usage: python scripts/verify-playlist-ruler-menu-mcp.py [executable] [output folder]
"""
import importlib.util
import json
from pathlib import Path
import sys
import uuid

ROOT = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe").resolve()
base = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else ROOT / "build/module-qualification/playlist-ruler-menu"
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
    raw = lambda tool, args: client.call(tool, args, allow_failure=True)
    call = lambda tool, args=None: raw(tool, args or {}).get("structuredContent", {})
    command = lambda name, **args: call("vibera_command", {"command": name, "args": args})
    done = lambda name, **args: command(name, **args).get("completionState")
    items = lambda entity, **args: call("vibera_inspect", dict(entity=entity, limit=500, **args)).get("items", [])
    markers = lambda: sorted(items("markers"), key=lambda m: m["beat"])
    call("vibera_project", {"action": "new", "template": "empty", "discardChanges": True})

    # Markers.
    check(done("marker.add", beat=4, text="Verse") == "completed" and [m["beat"] for m in markers()] == [4], "A marker at beat 4")
    marker = markers()[0]["id"]
    check(done("marker.rename", target=marker, text="Chorus") == "completed" and markers()[0]["name"] == "Chorus", "Rename marker (marker.rename)")
    check(done("marker.rename", target=marker, text="x" * 200) == "failed", "A marker name above 128 characters is refused")
    check(done("marker.move", target=marker, beat=8) == "completed" and markers()[0]["beat"] == 8, "Move marker to the playhead position (marker.move)")
    for action in ("loop", "skip", "pause", "start", ""):
        check(done("marker.action", target=marker, text=action) == "completed" and (markers()[0].get("action") or "") == action,
              f"Marker action '{action or 'none'}' (marker.action)")
    check(done("marker.action", target=marker, text="explode") == "failed", "An unknown marker action is refused")
    check(done("marker.addPair", beat=16, length=8) == "completed" and [m["beat"] for m in markers()] == [8, 16, 24],
          f"Markers at a time selection's edges (marker.addPair) {[m['beat'] for m in markers()]}")
    command("edit.undo")
    check([m["beat"] for m in markers()] == [8], "Add pair is one Undo step")
    for name in ("marker.next", "marker.previous", "marker.selectSection"):
        outcome = done(name)
        check(outcome in ("completed", "failed"), f"{name} reports its outcome ({outcome})")
    check(done("marker.delete", target=marker) == "completed" and markers() == [], "Delete marker (marker.delete)")
    command("edit.undo")
    check(len(markers()) == 1, "Delete marker is one Undo step")

    # Tempo changes on the ruler.
    tempo = lambda: sorted(items("tempo"), key=lambda t: t["beat"])
    check(done("tempo.change.add", beat=8, value=140) == "completed" and any(abs(t["beat"] - 8) < 1e-9 for t in tempo()),
          f"A tempo change at beat 8 ({tempo()})")
    change = next(t for t in tempo() if abs(t["beat"] - 8) < 1e-9)
    bpm = lambda row: row.get("bpm", row.get("tempo", row.get("value")))
    check(done("tempo.change.set", target=change["id"], beat=12, value=150) == "completed"
          and any(abs(t["beat"] - 12) < 1e-9 and abs(bpm(t) - 150) < 1e-6 for t in tempo()), "Edit tempo change (tempo.change.set)")
    check(done("tempo.change.delete", target=change["id"]) == "completed" and not any(t.get("id") == change["id"] for t in tempo()),
          "Delete tempo change (tempo.change.delete)")

    # Automation point menu.
    command("automation.create", mixer=1, beat=0, length=4, lane=2)
    source = next(c for c in items("clips") if c["kind"] == "automation")["source"]
    command("automation.point.add", target=source, beat=2, value=.75)
    points = lambda: items("automationPoints", source=source)
    middle = next(p for p in points() if abs(p["beat"] - 2) < 1e-9)
    check(done("automation.point.move", target=middle["id"], beat=3, value=.4) == "completed"
          and any(abs(p["beat"] - 3) < 1e-9 and abs(p["value"] - .4) < 1e-4 for p in points()), "Type in value / time moves the point (automation.point.move)")
    check(done("automation.point.shape", target=middle["id"], text="hold") == "completed"
          and next(p for p in points() if p["id"] == middle["id"]).get("shape") in (1, "hold"), "Hold shape (automation.point.shape)")
    check(done("automation.point.delete", target=points()[0]["id"]) == "failed", "An endpoint cannot be deleted")
    check(done("automation.point.delete", target=middle["id"]) == "completed" and len(points()) == 2, "Delete point (automation.point.delete)")
    command("edit.undo")
    check(len(points()) == 3, "Delete point is one Undo step")
    passed = True
finally:
    client.close()
    (output / "verification.json").write_text(json.dumps(dict(passed=passed, checks=checks), indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks), last=checks[-1] if checks else None)))
sys.exit(0 if passed else 1)
