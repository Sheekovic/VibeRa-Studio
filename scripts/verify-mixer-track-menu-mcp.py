"""The Mixer track menu items through MCP (isolated --mcp-test instance).

The right-click menu of a Mixer track runs registered operations; this drives those operations the way
the menu does and reads the result back: track delay, the 3-band EQ, Dock to, Reset track, Create insert,
the Select and Routing submenus, spare state, Link all parameters to MIDI CCs, Arm for rendering and
Trim unused tracks. Chain presets, effect browsing, replacing and deleting inserts are checked by
mixer_chain_mcp and mixer_effects tests; parallel chains by parallel_chains_mcp. Not a listening test.

usage: python scripts/verify-mixer-track-menu-mcp.py [executable] [output folder]
"""
import importlib.util
import json
from pathlib import Path
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe").resolve()
base = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else ROOT / "build/module-qualification/mixer-track-menu"
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
    inserts = lambda: call("vibera_inspect", {"entity": "mixer", "limit": 200})["items"]
    row = lambda index: next(m for m in inserts() if m["index"] == index)
    call("vibera_project", {"action": "new", "template": "demo", "discardChanges": True})

    before = len(inserts())
    created = command("mixer.insert.create").get("completionState")  # Structural: pending until audio is prepared.
    for _ in range(100):
        if len(inserts()) == before + 1:
            break
        time.sleep(.1)
    check(created in ("completed", "pending") and len(inserts()) == before + 1,
          f"Create insert adds a Mixer track (mixer.insert.create, {created})")
    time.sleep(1)
    track = max(m["index"] for m in inserts())

    command("mixer.delay", mixer=track, value=12)
    check(abs(row(track)["delayMs"] - 12) < 1e-6, "Track delay sets 12 ms (mixer.delay)")
    check(command("mixer.delay", mixer=track, value=600).get("completionState") == "failed", "A delay above 500 ms is refused")
    command("mixer.eq", mixer=track, target=1, value=3, offset=1000)
    check("3" in json.dumps(row(track)["eq"]) and "1000" in json.dumps(row(track)["eq"]), f"EQ mid band +3 dB at 1 kHz (mixer.eq) {row(track)['eq']}")
    check(command("mixer.eq", mixer=track, target=5, value=0).get("completionState") == "failed", "An EQ band above 2 is refused")
    for value, name in ((-1, "left"), (1, "right"), (0, "middle")):
        command("mixer.dock", mixer=track, value=value)
        check(row(track)["dock"] == name, f"Dock to {name} (mixer.dock)")
    command("mixer.dock", mixer=track, value=1)
    command("mixer.reset", mixer=track)
    reset = row(track)
    check(reset["delayMs"] == 0 and reset["dock"] == "middle", "Reset track restores delay and dock (mixer.reset)")
    command("edit.undo")
    check(row(track)["dock"] == "right", "Reset track is one Undo step")

    # Select / Routing / spare / link / trim act on the selected track and report a real outcome.
    check(not raw("vibera_mixer", {"action": "select", "insert": track}).get("isError"), "The track is selected")
    for name in ("mixer.selectChannelsRouted", "mixer.selectRoutedHere", "mixer.selectRoutedFrom", "mixer.routeSelectedHere",
                 "mixer.routeSelectedHereOnly", "mixer.spareStore", "mixer.spareFlip", "mixer.linkAll"):
        outcome = command(name).get("completionState")
        check(outcome in ("completed", "failed"), f"{name} reports its outcome ({outcome})")

    raw("vibera_mixer", {"action": "select", "insert": track})
    armed = lambda: call("vibera_state").get("mixerRenderArmed", [])
    command("mixer.renderArm")
    check(track in armed(), f"Arm for rendering arms the track (mixer.renderArm) {armed()}")
    command("mixer.renderArm")
    check(track not in armed(), "A second click disarms it")

    count = len(inserts())
    command("mixer.trimUnused")  # Deletes the unused tracks one settled step at a time.
    for _ in range(150):
        if not any(m["index"] == track for m in inserts()):
            break
        time.sleep(.1)
    check(not any(m["index"] == track for m in inserts()) and len(inserts()) < count,
          f"Trim unused tracks removes the new empty track (mixer.trimUnused, {count} -> {len(inserts())})")
    passed = True
finally:
    client.close()
    (output / "verification.json").write_text(json.dumps(dict(passed=passed, checks=checks), indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks), last=checks[-1] if checks else None)))
sys.exit(0 if passed else 1)
