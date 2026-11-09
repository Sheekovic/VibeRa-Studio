"""Sampler envelope presets through MCP (isolated --mcp-test instance; owned preset folder).

saveEnvelopePreset stores a Channel's volume envelope (on, attack, decay, sustain, release) by
name; loadEnvelopePreset applies it to another Channel as one Undo step through
channel.sampler.envelopeApply. Missing presets, bad names and malformed preset text are refused.

usage: python scripts/verify-envelope-presets-mcp.py [executable] [output folder]
"""
import importlib.util
import json
from pathlib import Path
import sys
import uuid

ROOT = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe").resolve()
base = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else ROOT / "build/module-qualification/envelope-presets"
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
    raw = lambda tool, args=None: client.call(tool, args or {}, allow_failure=True)
    call = lambda tool, args=None: raw(tool, args).get("structuredContent", {})
    command = lambda name, **args: call("vibera_command", {"command": name, "args": args})
    project = lambda action, **args: raw("vibera_project", dict(action=action, **args))
    call("vibera_project", {"action": "new", "template": "demo", "discardChanges": True})
    channels = [c for c in call("vibera_state")["channels"] if not c.get("audioClip")]
    source, target = channels[0]["id"], channels[1]["id"]
    envelope = lambda cid: {k: next(c for c in call("vibera_state")["channels"] if c["id"] == cid)["sampler"][k]
                            for k in ("envelope", "attack", "decay", "sustain", "release")}

    command("channel.sampler.envelope", channel=source, enabled=True)
    for name, value in (("attack", .5), ("decay", .3), ("sustain", .6), ("release", 1.2)):
        command("channel.sampler." + name, channel=source, value=value)
    wanted = {"envelope": True, "attack": .5, "decay": .3, "sustain": .6, "release": 1.2}
    check(envelope(source) == wanted, f"Source envelope set ({envelope(source)})")

    saved = project("saveEnvelopePreset", channel=source, name="Pad")
    check(not saved.get("isError") and "Pad" in saved["structuredContent"]["envelopePresets"], "saveEnvelopePreset lists Pad")
    before = envelope(target)
    check(before != wanted, "The target starts with a different envelope")
    loaded = project("loadEnvelopePreset", channel=target, name="Pad")
    check(not loaded.get("isError") and envelope(target) == wanted, f"loadEnvelopePreset applies all five values ({envelope(target)})")
    call("vibera_command", {"command": "edit.undo"})
    check(envelope(target) == before, "Loading a preset is one Undo step")

    check(project("loadEnvelopePreset", channel=target, name="Nope").get("isError"), "A missing preset is refused")
    check(project("saveEnvelopePreset", channel=source, name="  ").get("isError"), "An empty name is refused")
    bad = command("channel.sampler.envelopeApply", channel=target, text=json.dumps({"envelope": True, "attack": 99}))
    check(bad.get("completionState") != "completed" and envelope(target) == before, "Malformed preset text is refused unchanged")
    passed = True
finally:
    client.close()
    (output / "verification.json").write_text(json.dumps(dict(passed=passed, checks=checks), indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks), last=checks[-1] if checks else None)))
sys.exit(0 if passed else 1)
