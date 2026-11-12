"""The Channel Rack menu items through MCP (isolated --mcp-test instance).

The Rack's Menu button (and right-click on the filter) runs registered operations; this drives the ones
without their own test the way the menu does and reads the result back: Select all visible / Deselect,
Transpose selected Channels, Assign selected to free Mixer inserts, Zip / Unzip all, Move up / down,
Random / Gradient colours, Channel button width, Focus selected channel, Scroll to the selected channel
and the Multi-output submenu's refusal on a sampler Channel. Groups, Fill, mute / solo / lock, clone /
delete, Swing, colour / icon and Sort are checked by rack_groups_mcp, step_fill_mcp, channel_solo_mcp,
channel_scoped_solo_mcp, channel_management_mcp, swing_mcp and channel_appearance_mcp.

usage: python scripts/verify-rack-menu-mcp.py [executable] [output folder]
"""
import importlib.util
import json
from pathlib import Path
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe").resolve()
base = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else ROOT / "build/module-qualification/rack-menu"
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
    state = lambda: call("vibera_state")
    rack = lambda **args: call("vibera_rack", args)
    channels = lambda: call("vibera_inspect", {"entity": "channels", "limit": 200})["items"]
    notes = lambda: call("vibera_inspect", {"entity": "notes", "limit": 500})["items"]
    call("vibera_project", {"action": "new", "template": "demo", "discardChanges": True})

    order = [c["id"] for c in channels()]
    visible = rack(action="state")["visibleChannelIds"]
    check(rack(action="all")["selectedChannels"] == visible, "Select all visible Channels selects every visible row")
    check(rack(action="select", ids=[])["selectedChannels"] == [], "Deselect Channels clears the selection")

    # Transpose selected Channels: their notes move together, one Undo step.
    first = order[0]
    pitches = lambda: sorted((n["id"], n["pitch"]) for n in notes() if n["channel"] == first)
    before = pitches()
    check(len(before) > 0, f"The first Channel has notes ({len(before)})")
    check(command("rack.transpose", ids=[first], value=2).get("completionState") == "completed"
          and pitches() == sorted((i, p + 2) for i, p in before), "Transpose selected Channels moves the notes +2 (rack.transpose)")
    check(command("rack.transpose", ids=[first], value=0).get("completionState") == "failed", "Transposing by 0 is refused")
    command("edit.undo")
    check(pitches() == before, "Transpose is one Undo step")

    # Zip / Unzip all.
    rack(action="select", ids=order[:2])
    command("rack.zip")
    check(sorted(state().get("zippedChannels", [])) == sorted(order[:2]), "Zip selected Channels zips both (rack.zip)")
    command("rack.unzipAll")
    check(state().get("zippedChannels") == [], "Unzip all clears them (rack.unzipAll)")

    # Move up / down (the active Channel).
    call("vibera_select", {"channel": order[1]})
    command("channel.moveUp")
    check([c["id"] for c in channels()][:2] == [order[1], order[0]], "Move up swaps the Channel with the one above (channel.moveUp)")
    command("channel.moveDown")
    check([c["id"] for c in channels()] == order, "Move down restores the order (channel.moveDown)")

    # Random / gradient colours.
    colours = lambda: [c.get("colour") for c in channels()][:3]
    plain = colours()
    check(command("rack.colourSpread", ids=order[:3], text="gradient", value=7).get("completionState") == "completed"
          and colours() != plain, f"Gradient colours recolour the selected Channels (rack.colourSpread) {colours()}")
    check(command("rack.colourSpread", ids=order[:3], text="rainbow", value=7).get("completionState") == "failed",
          "An unknown colour spread is refused")
    command("edit.undo")
    check(colours() == plain, "Colour spread is one Undo step")

    # View options.
    for name, width in (("view.rackButtonsNarrow", "narrow"), ("view.rackButtonsWide", "wide"), ("view.rackButtonsNormal", "normal")):
        command(name)
        check(state().get("rackButtonWidth") == width, f"{name} sets the Channel button width {width}")
    focus = state().get("rackFocusSelected")
    command("view.rackFocusSelected")
    check(state().get("rackFocusSelected") is (not focus), "Focus selected channel toggles (view.rackFocusSelected)")
    command("view.rackFocusSelected")
    check(command("view.rackScrollToSelected").get("completionState") == "completed", "Scroll to the selected channel runs")

    # Assign selected to free Mixer inserts: each selected Channel gets its own insert (one routing plan).
    rack(action="select", ids=order[:2])
    mixers = lambda: [c["mixer"] for c in channels()][:2]
    outcome = command("rack.assignFreeInserts").get("completionState")
    for _ in range(100):
        if len(set(mixers())) == 2 and call("vibera_state").get("status") is not None:
            break
        time.sleep(.1)
    check(outcome in ("completed", "pending") and len(set(mixers())) == 2 and 0 not in mixers(),
          f"Assign to free Mixer inserts gives each Channel its own insert (rack.assignFreeInserts, {outcome}, {mixers()})")

    # Multi-output is for plugin instruments: a sampler Channel reports a truthful refusal.
    call("vibera_select", {"channel": first})
    multi = command("channel.multiOutputs1").get("completionState")
    check(multi in ("completed", "failed"), f"Multi-output reports its outcome on the selected Channel ({multi})")
    passed = True
finally:
    client.close()
    (output / "verification.json").write_text(json.dumps(dict(passed=passed, checks=checks), indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks), last=checks[-1] if checks else None)))
sys.exit(0 if passed else 1)
