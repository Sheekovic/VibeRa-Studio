"""MIDI file import through MCP (isolated --mcp-test instance).

vibera_project importMidi reads a generated type-0 file with C5 (60) on MIDI channel 1 and E5 (64)
on channel 3. All channels import both notes; midiChannels [3] imports only E5, like the Import MIDI
dialog's channel tiles; an out-of-range channel is refused without changing the Pattern. Each
import is one Undo step.

usage: python scripts/verify-midi-import-mcp.py [executable] [output folder]
"""
import importlib.util
import json
from pathlib import Path
import struct
import sys
import uuid

ROOT = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe").resolve()
base = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else ROOT / "build/module-qualification/midi-import"
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


def write_midi(path):
    # Type 0, 480 PPQ: C5 on channel 1 at beat 0, E5 on channel 3 at beat 1, each one beat long.
    events = bytes([0x00, 0x90, 60, 100,  0x83, 0x60, 0x80, 60, 0,
                    0x00, 0x92, 64, 100,  0x83, 0x60, 0x82, 64, 0,
                    0x00, 0xFF, 0x2F, 0x00])
    track = b"MTrk" + struct.pack(">I", len(events)) + events
    path.write_bytes(b"MThd" + struct.pack(">IHHH", 6, 0, 1, 480) + track)


midi = output / "two-channels.mid"
write_midi(midi)
client = module.VibeRaClient(exe)
client.process.stdin.reconfigure(newline="\n")
passed = False
try:
    raw = lambda tool, args=None: client.call(tool, args or {}, allow_failure=True)
    call = lambda tool, args=None: raw(tool, args).get("structuredContent", {})
    call("vibera_project", {"action": "new", "template": "empty", "discardChanges": True})
    pitches = lambda: sorted(n["pitch"] for n in call("vibera_inspect", {"entity": "notes", "limit": 512}).get("items", []))

    result = raw("vibera_project", {"action": "importMidi", "path": str(midi)})
    check(not result.get("isError"), "importMidi with all channels: " + json.dumps(result)[:160])
    check(pitches() == [60, 64], "Both channels import")
    call("vibera_command", {"command": "edit.undo"})
    check(pitches() == [], "The import is one Undo step")

    raw("vibera_project", {"action": "importMidi", "path": str(midi), "midiChannels": [3]})
    check(pitches() == [64], "midiChannels [3] imports only channel 3")
    before = pitches()
    check(raw("vibera_project", {"action": "importMidi", "path": str(midi), "midiChannels": [17]}).get("isError"),
          "Channel 17 is refused")
    check(pitches() == before, "A refusal leaves the Pattern unchanged")
    passed = True
finally:
    client.close()
    (output / "verification.json").write_text(json.dumps(dict(passed=passed, checks=checks), indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks), last=checks[-1] if checks else None)))
sys.exit(0 if passed else 1)
