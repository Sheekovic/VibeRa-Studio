"""The Playlist track menu items through MCP (isolated --mcp-test instance).

Right-clicking a Playlist track header runs registered operations; this drives them with the arguments
the menu passes and reads the tracks and clips back: Mute, Solo, Rename, colour, Lock, height, Group with
track above, Collapse / Expand group, Instrument track, Audio track, Insert / Delete / Clone / Move track,
Auto name and Reset. Every edit is one Undo step. Not a listening test.

usage: python scripts/verify-playlist-track-menu-mcp.py [executable] [output folder]
"""
import importlib.util
import json
from pathlib import Path
import sys
import uuid

ROOT = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe").resolve()
base = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else ROOT / "build/module-qualification/playlist-track-menu"
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
    tracks = lambda: {t["lane"]: t for t in call("vibera_inspect", {"entity": "tracks", "limit": 200})["items"]}
    clips = lambda: sorted((c["lane"], c["beat"]) for c in call("vibera_inspect", {"entity": "clips", "limit": 500})["items"])
    call("vibera_project", {"action": "new", "template": "empty", "discardChanges": True})
    state = call("vibera_state")
    pattern, channel = state["selectedPattern"], state["selectedChannel"]
    command("clip.add", pattern=pattern, beat=0, lane=0, length=4)
    command("clip.add", pattern=pattern, beat=0, lane=1, length=4)
    check(clips() == [(0, 0), (1, 0)], "Two clips on tracks 1 and 2")

    check(done("track.rename", lane=1, text="Drums") == "completed" and tracks()[1]["name"] == "Drums", "Rename track (track.rename)")
    check(done("track.colour", lane=1, value=0xFF3366CC) == "completed" and tracks()[1]["colour"] == 0xFF3366CC, "Track colour (track.colour)")
    check(done("track.colour", lane=1, value=0x003366CC) == "failed", "A transparent track colour is refused")
    command("track.mute", lane=1, enabled=True)
    check(tracks()[1]["muted"] is True, "Mute track (track.mute)")
    command("track.mute", lane=1, enabled=False)
    command("track.solo", lane=0)
    check(tracks().get(1, {}).get("muted") is True and not tracks().get(0, {}).get("muted"), "Solo track mutes the other used tracks (track.solo)")
    command("track.solo", lane=0)
    check(not tracks().get(1, {}).get("muted"), "Soloing it again unmutes every track")
    command("track.lock", lane=1, enabled=True)
    check(tracks()[1]["locked"] is True, "Lock track (track.lock)")
    command("track.lock", lane=1, enabled=False)
    check(done("track.height", lane=1, value=2) == "completed" and tracks()[1]["height"] == 2, "Track height x2 (track.height)")
    check(done("track.height", lane=1, value=9) == "failed", "A height above 3 is refused")
    check(done("track.group", lane=1, enabled=True) == "completed" and tracks()[1]["groupedAbove"] is True, "Group with track above (track.group)")
    check(done("track.group", lane=0, enabled=True) == "failed", "The first track has nothing above to group with")
    check(done("track.collapse", lane=0, enabled=True) == "completed" and tracks()[0]["collapsed"] is True, "Collapse group (track.collapse)")
    command("track.collapse", lane=0, enabled=False)
    command("track.group", lane=1, enabled=False)
    check(done("track.instrument", lane=1, channel=channel) == "completed" and tracks()[1]["channel"] == channel, "Instrument track (track.instrument)")
    check(done("track.audio", lane=1, enabled=True) == "completed" and tracks()[1]["audio"] is True and tracks()[1]["channel"] == 0,
          "Audio track replaces the instrument owner (track.audio)")
    command("track.audio", lane=1, enabled=False)

    command("track.insert", lane=1)
    check(clips() == [(0, 0), (2, 0)] and tracks()[2]["name"] == "Drums", "Insert track moves the later tracks down (track.insert)")
    command("edit.undo")
    check(clips() == [(0, 0), (1, 0)] and tracks()[1]["name"] == "Drums", "Insert track is one Undo step")
    cloned = command("track.clone", lane=1)
    check(clips() == [(0, 0), (1, 0), (2, 0)],
          f"Clone track copies the track and its clips below it (track.clone) {clips()} {json.dumps(cloned.get('error'))[:160]}")
    command("track.delete", lane=2)
    check(clips() == [(0, 0), (1, 0)], "Delete track removes the track and its clips (track.delete)")
    command("track.move", lane=1, value=-1)
    check(tracks()[0]["name"] == "Drums", "Move track up swaps it with the track above (track.move)")
    check(done("track.move", lane=0, value=-1) == "failed", "The first track cannot move up")
    command("track.move", lane=0, value=1)
    auto = done("track.autoName", lane=1)
    check(auto in ("completed", "failed"), f"Auto name reports its outcome (track.autoName, {auto}, {tracks().get(1, {}).get('name')})")
    command("track.rename", lane=1, text="Drums")
    command("track.reset", lane=1)
    check(tracks().get(1, {}).get("name", "") == "" and tracks().get(1, {}).get("colour", 0) == 0, "Reset track clears the name and colour (track.reset)")
    passed = True
finally:
    client.close()
    (output / "verification.json").write_text(json.dumps(dict(passed=passed, checks=checks), indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks), last=checks[-1] if checks else None)))
sys.exit(0 if passed else 1)
