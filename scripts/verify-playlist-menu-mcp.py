"""The Playlist menu items through MCP (isolated --mcp-test instance).

The Playlist's menu button runs registered operations; this drives them with the menu's arguments and
reads the result back: Undo / Redo, Duplicate / Delete selected clips, Enable Clip grouping, Auto-scroll,
Center the playhead, Stretch audio clips by dragging, Quantize song jumps, arrangements (new, clone,
rename, select, delete), Add marker at playhead, Add markers every N bars, Add tempo change, Tempo curve
over a range, Time signature from a bar and the Picker panel. Group / Ungroup, Make Unique and track
export are checked by clip_groups_mcp, unique_sample_mcp and export_formats_mcp.

usage: python scripts/verify-playlist-menu-mcp.py [executable] [output folder]
"""
import importlib.util
import json
from pathlib import Path
import sys
import uuid

ROOT = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe").resolve()
base = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else ROOT / "build/module-qualification/playlist-menu"
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
    state = lambda: call("vibera_state")
    items = lambda entity, **args: call("vibera_inspect", dict(entity=entity, limit=500, **args)).get("items", [])
    clips = lambda: sorted((c["lane"], c["beat"]) for c in items("clips"))
    call("vibera_project", {"action": "new", "template": "empty", "discardChanges": True})
    pattern = state()["selectedPattern"]

    # Clip edits and history.
    command("clip.add", pattern=pattern, beat=0, lane=0, length=4)
    first = items("clips")[0]["id"]
    check(done("clip.duplicate", ids=[first]) == "completed" and len(clips()) == 2, f"Duplicate selected clips (clip.duplicate) {clips()}")
    command("edit.undo")
    check(len(clips()) == 1, "Undo (edit.undo)")
    command("edit.redo")
    check(len(clips()) == 2, "Redo (edit.redo)")
    newest = [c["id"] for c in items("clips") if c["id"] != first]
    check(done("clip.delete", ids=newest) == "completed" and len(clips()) == 1, "Delete selected clips (clip.delete)")

    patterns = lambda: len(items("patterns"))
    count_before = patterns()
    made = done("clip.unique", target=first)
    unique_pattern = next(c for c in items("clips") if c["id"] == first)["source"]  # A Pattern clip's source is its Pattern.
    check(made == "completed" and patterns() == count_before + 1 and unique_pattern != pattern,
          f"Make Unique gives the clip its own copy of the Pattern (clip.unique, {count_before} -> {patterns()} Patterns)")
    command("edit.undo")
    check(patterns() == count_before, "Make Unique is one Undo step")

    # View and behaviour toggles.
    for name, key in (("playlist.grouping", "clipGroupingEnabled"), ("playlist.follow", "playlistFollowPlayhead"),
                      ("view.followCenter", "followCenter"), ("playlist.stretchMode", "audioStretchMode")):
        before = state().get(key)
        command(name)
        check(state().get(key) != before, f"{name} toggles {key} ({before} -> {state().get(key)})")
        command(name)
        check(state().get(key) == before, f"{name} toggles back")
    raw("vibera_transport", {"action": "jumpQuantize", "quantize": 4})
    check(state().get("songJumpQuantize") == 4, "Quantize song jumps > next 4 bars")
    raw("vibera_transport", {"action": "jumpQuantize", "quantize": 0})
    check(done("playlist.picker") in ("completed", "failed"), "Picker Panel reports its outcome (playlist.picker)")

    # Arrangements.
    arrangements = lambda: items("arrangements")
    count = len(arrangements())
    check(done("arrangement.add") == "completed" and len(arrangements()) == count + 1, f"New arrangement (arrangement.add, {count} -> {len(arrangements())})")
    current = next(a for a in arrangements() if a.get("current"))
    check(done("arrangement.rename", target=current["id"], text="Verse idea") == "completed"
          and any(a["name"] == "Verse idea" for a in arrangements()), "Rename arrangement (arrangement.rename)")
    check(done("arrangement.clone") == "completed" and len(arrangements()) == count + 2, "Clone arrangement (arrangement.clone)")
    other = next(a for a in arrangements() if not a.get("current"))
    check(done("arrangement.select", target=other["id"]) == "completed"
          and next(a for a in arrangements() if a.get("current"))["id"] == other["id"], "Select arrangement (arrangement.select)")
    check(done("arrangement.delete") == "completed" and len(arrangements()) == count + 1, "Delete arrangement (arrangement.delete)")

    # Markers, tempo and time signature.
    markers = lambda: sorted(m["beat"] for m in items("markers"))
    check(done("marker.add", beat=0, text="") == "completed" and markers() == [0], "Add marker at playhead (marker.add)")
    command("clip.add", pattern=pattern, beat=28, lane=1, length=4)  # The Song runs to bar 8.
    every = done("marker.addEvery", value=4)
    check(every == "completed" and len(markers()) >= 2 and all(abs(b % 16) < 1e-9 for b in markers()),
          f"Add markers every 4 bars up to the Song end (marker.addEvery) {markers()}")
    check(done("marker.addEvery", value=0) == "failed", "Every 0 bars is refused")
    tempo = lambda: items("tempo")
    before = len(tempo())
    check(done("tempo.change.add", beat=8, value=132) == "completed" and len(tempo()) == before + 1, "Add tempo change at playhead (tempo.change.add)")
    curve = done("tempo.curve", beat=16, length=8, value=160, text="linear")
    check(curve == "completed" and len(tempo()) > before + 1, f"Tempo curve over the time selection (tempo.curve, {len(tempo())} changes)")
    check(done("tempo.curve", beat=16, length=8, value=160, text="zigzag") == "failed", "An unknown tempo curve shape is refused")
    meters = lambda: items("meters")
    base_count = len(meters())
    check(done("meter.change.add", beat=16, numerator=3, denominator=4) == "completed" and len(meters()) == base_count + 1,
          f"Time signature 3/4 from a bar (meter.change.add) {meters()}")
    change = max(meters(), key=lambda m: m.get("beat", 0))
    check(done("meter.change.delete", target=change["id"]) == "completed" and len(meters()) == base_count, "Remove the change at this bar (meter.change.delete)")
    passed = True
finally:
    client.close()
    (output / "verification.json").write_text(json.dumps(dict(passed=passed, checks=checks), indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks), last=checks[-1] if checks else None)))
sys.exit(0 if passed else 1)
