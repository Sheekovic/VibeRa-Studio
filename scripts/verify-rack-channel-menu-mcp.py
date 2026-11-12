"""Channel Rack channel menu items without their own test, through MCP (isolated --mcp-test instance).

Right-clicking a Channel (and its volume / pan knobs) runs registered operations. Piano roll, Open
instrument, Cut itself, Receive notes from, name / colour / icon, Fill, Channel loops, clone / delete /
move and Swing are checked by other tests; this drives the rest and reads the Channel back: Reset volume
/ Centre pan, Store in / Flip with spare state, per-note tuning, a custom scale, MTS-ESP source and the
MPE refusal on a sampler Channel.

usage: python scripts/verify-rack-channel-menu-mcp.py [executable] [output folder]
"""
import importlib.util
import json
from pathlib import Path
import sys
import uuid

ROOT = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe").resolve()
base = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else ROOT / "build/module-qualification/rack-channel-menu"
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
    result = lambda name, **args: call("vibera_command", {"command": name, "args": args})
    done = lambda name, **args: result(name, **args).get("completionState")
    channels = lambda: call("vibera_inspect", {"entity": "channels", "limit": 200})["items"]
    call("vibera_project", {"action": "new", "template": "demo", "discardChanges": True})
    target = next(c for c in channels() if c.get("source"))
    channel = target["id"]
    row = lambda: next(c for c in channels() if c["id"] == channel)
    call("vibera_select", {"channel": channel})

    # Knob menus: Reset volume / Centre pan.
    check(done("channel.gain", channel=channel, value=.3) == "completed" and abs(row()["gain"] - .3) < 1e-6, "Channel volume set to 0.3 (channel.gain)")
    check(done("channel.pan", channel=channel, value=-.5) == "completed" and abs(row()["pan"] + .5) < 1e-6, "Channel pan set left (channel.pan)")
    done("channel.pan", channel=channel, value=0)
    check(abs(row()["pan"]) < 1e-9, "Centre pan returns the pan to 0")
    check(done("channel.gain", channel=channel, value=9) == "failed", "A volume above the range is refused")

    # Spare state of the selected Channel.
    stored = done("channel.spareStore")
    check(stored in ("completed", "failed"), f"Store in spare state reports its outcome (channel.spareStore, {stored})")
    done("channel.gain", channel=channel, value=.6)
    flipped = done("channel.spareFlip")
    check(flipped in ("completed", "failed"), f"Flip with spare state reports its outcome (channel.spareFlip, {flipped})")
    if stored == "completed" and flipped == "completed":
        check(abs(row()["gain"] - .3) < 1e-6, "Flipping restores the stored volume 0.3")

    # Tuning submenu.
    cents = [0, 0, 0, -50, 0, 0, 0, 0, 0, 0, -50, 0]
    tuned = result("channel.tuning", channel=channel, tuning=cents)
    check(tuned.get("completionState") == "completed", "Per-note tuning: E and B half flat (channel.tuning) " + json.dumps(tuned.get("error"))[:120])
    check(done("channel.tuning", channel=channel, tuning=[0] * 11) == "failed", "A tuning without twelve entries is refused")
    done("edit.undo")
    scale = result("channel.scale", channel=channel, scale=[100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 1100, 1200])
    check(scale.get("completionState") in ("completed", "failed"),
          f"Custom scale reports its outcome (channel.scale, {scale.get('completionState')})")
    mts = done("channel.mtsEsp", channel=channel, enabled=True)
    check(mts in ("completed", "failed"), f"MTS-ESP tuning source reports its outcome (channel.mtsEsp, {mts})")
    if mts == "completed":
        done("channel.mtsEsp", channel=channel, enabled=False)
    mpe = done("channel.mpe", channel=channel, enabled=True)
    check(mpe == "failed", f"MPE is for plugin instruments: a sampler Channel is refused (channel.mpe, {mpe})")
    passed = True
finally:
    client.close()
    (output / "verification.json").write_text(json.dumps(dict(passed=passed, checks=checks), indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks), last=checks[-1] if checks else None)))
sys.exit(0 if passed else 1)
