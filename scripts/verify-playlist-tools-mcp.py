"""Playlist gaps from the FL manual comparison, through MCP (isolated --mcp-test instance).

Automation Clip articulator tools (flip, normalize, smooth, scale levels) edit the curve; Merge
Automation Clips joins two clips into one curve over their span keeping the first clip's links;
ripple delete removes a clip and pulls later clips on its track left. Each is one Undo step.

usage: python scripts/verify-playlist-tools-mcp.py [executable] [output folder]
"""
import importlib.util
import json
from pathlib import Path
import sys
import uuid

ROOT = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe").resolve()
base = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else ROOT / "build/module-qualification/playlist-tools"
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

    def clips(kind=None):
        return [c for c in call("vibera_inspect", {"entity": "clips", "limit": 500})["items"] if not kind or c["kind"] == kind]

    def points(source):
        return [(round(p["beat"], 4), round(p["value"], 4), p.get("shape"))
                for p in call("vibera_inspect", {"entity": "automationPoints", "source": source, "limit": 500})["items"]]

    call("vibera_project", {"action": "new", "template": "empty", "discardChanges": True})
    command("automation.create", mixer=1, beat=0, length=4, lane=2)
    first = clips("automation")[0]
    source = first["source"]
    command("automation.point.add", target=source, beat=2, value=.75)
    # Endpoints sit at the manual gain (normalized); move them to known levels.
    ids = [p["id"] for p in call("vibera_inspect", {"entity": "automationPoints", "source": source})["items"]]
    command("automation.point.move", target=ids[0], beat=0, value=.25)
    command("automation.point.move", target=ids[-1], beat=4, value=.5)
    check([v for _, v, _ in points(source)] == [.25, .75, .5], f"Curve set up ({points(source)})")
    check(command("automation.source.flip", target=source).get("completionState") == "completed" and
          [v for _, v, _ in points(source)] == [.75, .25, .5], "Flip vertically mirrors every level")
    command("automation.source.normalize", target=source)
    check([v for _, v, _ in points(source)] == [1, 0, .5], f"Normalize stretches the levels to 0..1 ({points(source)})")
    command("automation.source.scale", target=source, value=.5, offset=.25)
    check([v for _, v, _ in points(source)] == [.75, .25, .5], "Scale levels: value x 0.5 + 0.25")
    command("automation.source.smooth", target=source)
    shapes = [s for _, _, s in points(source)][1:]
    check(all(s in (2, "smooth") for s in shapes), f"Every point turns smooth ({shapes})")
    command("edit.undo")
    check([s for _, _, s in points(source)][1:] != shapes, "Undo restores the previous point shapes")
    command("edit.redo")

    # Decimate points: interior points on a straight line go; the off-line point and the ends stay.
    command("automation.create", mixer=2, beat=0, length=4, lane=3)
    line = [c for c in clips("automation") if c["lane"] == 3][0]["source"]
    ends = [p["id"] for p in call("vibera_inspect", {"entity": "automationPoints", "source": line})["items"]]
    command("automation.point.move", target=ends[0], beat=0, value=.2)
    command("automation.point.move", target=ends[-1], beat=4, value=.6)
    for beat, value in ((1, .3), (2, .4), (3, .5), (3.5, .9)):
        command("automation.point.add", target=line, beat=beat, value=value)
    before = points(line)
    check(len(before) == 6, f"Decimate test curve has six points ({before})")
    result = command("automation.source.decimate", target=line)
    after = points(line)
    check(result.get("completionState") == "completed" and [b for b, _, _ in after] == [0, 3, 3.5, 4],
          f"Decimate drops the points on the straight line ({after})")
    check(command("automation.source.decimate", target=line).get("completionState") == "failed",
          "A curve with nothing left to drop is refused")
    command("edit.undo")
    check(points(line) == before, "Decimate is one Undo step")
    command("clip.delete", ids=[c["id"] for c in clips("automation") if c["lane"] == 3])
    check(not [c for c in clips("automation") if c["lane"] == 3], "The decimate test clip is removed")

    command("automation.place", target=source, beat=6, length=4, value=0, lane=2)
    automation = sorted((c for c in clips("automation") if c["lane"] == 2), key=lambda c: c["beat"])
    check(len(automation) == 2, "A second Automation Clip of the same curve")
    merged = command("automation.merge", ids=[c["id"] for c in automation])
    check(merged.get("completionState") == "completed", "Merge Automation Clips: " + str(merged.get("error"))[:120])
    after = clips("automation")
    check(len(after) == 1 and after[0]["beat"] == 0 and after[0]["length"] == 10,
          f"One merged clip spans both (0..10) ({after})")
    merged_points = points(after[0]["source"])
    check(merged_points[0][0] == 0 and merged_points[-1][0] == 10 and any(abs(b - 6) < 1e-6 for b, _, _ in merged_points),
          f"The merged curve starts, restarts at 6 and ends at 10 ({merged_points})")
    links = call("vibera_inspect", {"entity": "automationLinks", "source": after[0]["source"]})["items"]
    check(len(links) == 1, "The merged curve keeps the first clip's target link")
    check(command("automation.merge", ids=[after[0]["id"]]).get("completionState") == "failed", "Merging one clip is refused")
    command("edit.undo")
    check(len(clips("automation")) == 2, "One Undo restores both clips")

    state = call("vibera_state")
    pattern = state["selectedPattern"]
    for beat in (0, 4, 8):
        command("clip.add", pattern=pattern, beat=beat, lane=0, length=4)
    command("clip.add", pattern=pattern, beat=8, lane=1, length=4)
    lane0 = sorted((c for c in clips("pattern") if c["lane"] == 0), key=lambda c: c["beat"])
    result = command("clip.rippleDelete", ids=[lane0[1]["id"]])
    check(result.get("completionState") == "completed", "Ripple delete: " + str(result.get("error"))[:120])
    beats = sorted((c["lane"], c["beat"]) for c in clips("pattern"))
    check(beats == [(0, 0), (0, 4), (1, 8)], f"The later clip on that track closes the gap; other tracks stay ({beats})")
    command("edit.undo")
    check(sorted((c["lane"], c["beat"]) for c in clips("pattern")) == [(0, 0), (0, 4), (0, 8), (1, 8)],
          "One Undo restores the clip and positions")
    passed = True
finally:
    client.close()
    (output / "verification.json").write_text(json.dumps(dict(passed=passed, checks=checks), indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks), last=checks[-1] if checks else None)))
sys.exit(0 if passed else 1)
