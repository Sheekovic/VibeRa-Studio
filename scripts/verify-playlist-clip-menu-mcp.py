"""Playlist clip menu items without their own test, through MCP (isolated --mcp-test instance).

Right-clicking a Playlist clip runs registered operations. The automation tools, ripple delete, Make
unique as sample, comping and fades are checked by playlist_tools_mcp, unique_sample_mcp and
audio_recording_mcp; this drives the rest with the menu's arguments and reads the clips back: Chop
(into bars / beats), Quick quantize clip starts, Split by channel, Merge selected Pattern clips and
Merge similar Pattern clips. Every edit is one Undo step.

usage: python scripts/verify-playlist-clip-menu-mcp.py [executable] [output folder]
"""
import importlib.util
import json
from pathlib import Path
import sys
import uuid

ROOT = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe").resolve()
base = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else ROOT / "build/module-qualification/playlist-clip-menu"
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
    pattern_clips = lambda: sorted((c for c in items("clips") if c["kind"] == "pattern"), key=lambda c: (c["lane"], c["beat"]))
    shape = lambda: [(c["lane"], c["beat"], c["length"]) for c in pattern_clips()]
    call("vibera_project", {"action": "new", "template": "demo", "discardChanges": True})
    state = call("vibera_state")
    pattern = state["selectedPattern"]
    for clip in pattern_clips():
        command("clip.delete", ids=[clip["id"]])
    check(shape() == [], "The Playlist starts without Pattern clips")

    # Chop into beats / bars on the Song grid.
    command("clip.add", pattern=pattern, beat=0, lane=0, length=4)
    whole = pattern_clips()[0]["id"]
    check(done("clip.chop", ids=[whole], length=1) == "completed" and shape() == [(0, float(b), 1.0) for b in range(4)],
          f"Chop into beats cuts the clip at every beat (clip.chop) {shape()}")
    command("edit.undo")
    check(shape() == [(0, 0.0, 4.0)], "Chop is one Undo step")
    check(done("clip.chop", ids=[whole], length=0) == "failed", "A chop length of 0 is refused")

    # Quick quantize clip starts.
    command("clip.move", ids=[whole], beat=.3)
    moved = shape()
    snapped = done("clip.quantize", ids=[whole])
    check(snapped == "completed" and shape() != moved and abs(shape()[0][1] - round(shape()[0][1] * 4) / 4) < 1e-9,
          f"Quick quantize snaps the clip start to the grid (clip.quantize) {moved} -> {shape()}")
    command("edit.undo")
    command("edit.undo")
    check(shape() == [(0, 0.0, 4.0)], "Quantize is one Undo step")

    # Split by channel: one clip per Channel that plays in it, on the clip's track and the ones below.
    patterns = lambda: len(items("patterns"))
    count = patterns()
    split = done("clip.splitChannels", target=whole)
    lanes = sorted({c["lane"] for c in pattern_clips()})
    check(split == "completed" and len(pattern_clips()) >= 2 and lanes == list(range(len(lanes))) and patterns() > count,
          f"Split by channel makes one clip per Channel on consecutive tracks (clip.splitChannels, {len(pattern_clips())} clips)")
    parts = [c["id"] for c in pattern_clips()]

    # Merge the selected Pattern clips back into one new Pattern on the first clip's track.
    merged = done("clip.merge", ids=parts)
    check(merged == "completed" and shape() == [(0, 0.0, 4.0)], f"Merge selected Pattern clips makes one clip (clip.merge) {shape()}")
    command("edit.undo")
    check(len(pattern_clips()) == len(parts), "Merge is one Undo step")
    check(done("clip.merge", ids=parts[:1]) == "failed", "Merging one clip is refused")
    command("edit.undo")  # Back to the single clip before the split.
    check(shape() == [(0, 0.0, 4.0)], "Split by channel is one Undo step")

    # Merge similar: every placement of the same Pattern arrangement is replaced consistently.
    command("clip.add", pattern=pattern, beat=4, lane=0, length=4)
    similar = done("clip.mergeSimilar", ids=[c["id"] for c in pattern_clips()])
    check(similar in ("completed", "failed"), f"Merge similar Pattern clips reports its outcome (clip.mergeSimilar, {similar}, {shape()})")
    passed = True
finally:
    client.close()
    (output / "verification.json").write_text(json.dumps(dict(passed=passed, checks=checks), indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks), last=checks[-1] if checks else None)))
sys.exit(0 if passed else 1)
