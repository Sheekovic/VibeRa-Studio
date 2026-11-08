"""Snap incoming MIDI to the Piano roll scale guide, through MCP (isolated --mcp-test instance).

vibera_editor action guide sets the scale guide and snapIncoming; state.pianoGuide reads back the
mapping the MIDI input router applies (incoming keys 48..59): with C major every black key moves
down to its neighbour, with A minor pentatonic keys move to the nearest pentatonic tone (ties go
down), off leaves keys unchanged, and an unknown scale is refused.

usage: python scripts/verify-midi-scale-snap-mcp.py [executable] [output folder]
"""
import importlib.util
import json
from pathlib import Path
import sys
import uuid

ROOT = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe").resolve()
base = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else ROOT / "build/module-qualification/midi-scale-snap"
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
    def guide(**args):
        result = client.call("vibera_editor", dict(editor=1, action="guide", **args), allow_failure=True)
        return result.get("structuredContent", {}), result.get("isError", False)

    state, _ = guide(scale="major", root=0, snapIncoming=True)
    keys = state["pianoGuide"]["snappedKeys48to59"]
    check(state["pianoGuide"]["snapIncoming"] and keys == [48, 48, 50, 50, 52, 53, 53, 55, 55, 57, 57, 59],
          f"C major: black keys snap down to the scale ({keys})")
    state, _ = guide(scale="minorPentatonic", root=9, snapIncoming=True)
    keys = state["pianoGuide"]["snappedKeys48to59"]
    # A minor pentatonic = A C D E G.
    check(keys == [48, 48, 50, 50, 52, 52, 55, 55, 55, 57, 57, 60], f"A minor pentatonic: nearest tone, ties down ({keys})")
    state, _ = guide(scale="minorPentatonic", root=9, snapIncoming=False)
    check(state["pianoGuide"]["snappedKeys48to59"] == list(range(48, 60)), "Off: incoming keys are untouched")
    _, failed = guide(scale="nonsense", root=0)
    check(failed, "An unknown scale is refused")
    state, _ = guide(scale="", snapIncoming=True)
    check(state["pianoGuide"]["scale"] == "" and state["pianoGuide"]["snappedKeys48to59"] == list(range(48, 60)),
          "No guide: nothing to snap to")
    passed = True
finally:
    client.close()
    (output / "verification.json").write_text(json.dumps(dict(passed=passed, checks=checks), indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks), last=checks[-1] if checks else None)))
sys.exit(0 if passed else 1)
