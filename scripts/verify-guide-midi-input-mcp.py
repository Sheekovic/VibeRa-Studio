"""Metronome / count-in and MIDI input settings through MCP (isolated --mcp-test instance).

vibera_transport action guide sets count-in bars, click sound, click volume and the metronome with
the same engine setters as Settings > Count-in; action midiInput sets the MIDI input channel filter
and recording quantize like Settings > MIDI. Each reads back in vibera_state; out-of-range values
are refused and leave the state unchanged.

usage: python scripts/verify-guide-midi-input-mcp.py [executable] [output folder]
"""
import importlib.util
import json
from pathlib import Path
import sys
import uuid

ROOT = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe").resolve()
base = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else ROOT / "build/module-qualification/guide-midi-input"
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
    call("vibera_project", {"action": "new", "template": "empty", "discardChanges": True})

    result = raw("vibera_transport", {"action": "guide", "countInBars": 2, "sound": 1, "gain": 0.4, "metronome": True})
    check(not result.get("isError"), "guide applies: " + json.dumps(result)[:160])
    state = call("vibera_state")
    check(state.get("countInBars") == 2, "Count-in reads back 2 bars")
    check(state.get("guideSound") == 1, "Sound reads back Wood")
    check(abs(state.get("guideGain", -1) - 0.4) < 1e-3, "Volume reads back 0.4")
    check(state.get("metronome") is True, "Metronome reads back on")

    raw("vibera_transport", {"action": "guide", "countInBars": 0, "metronome": False})
    state = call("vibera_state")
    check(state.get("countInBars") == 0 and state.get("metronome") is False, "Count-in and metronome turn off")
    check(state.get("guideSound") == 1, "Fields left out are unchanged")
    check(raw("vibera_transport", {"action": "guide", "sound": 5}).get("isError"), "An unknown sound is refused")
    check(raw("vibera_transport", {"action": "guide", "gain": 2}).get("isError"), "A volume above 1 is refused")
    check(call("vibera_state").get("guideGain") == state.get("guideGain"), "A refusal leaves the volume")

    result = raw("vibera_transport", {"action": "midiInput", "midiChannel": 3, "quantizeStart": True, "quantizeEnd": False})
    check(not result.get("isError"), "midiInput applies: " + json.dumps(result)[:160])
    midi = call("vibera_state").get("midiInput", {})
    check(midi == {"channel": 3, "quantizeStart": True, "quantizeEnd": False}, "MIDI input reads back: " + json.dumps(midi))
    raw("vibera_transport", {"action": "midiInput", "midiChannel": 0, "quantizeEnd": True})
    midi = call("vibera_state").get("midiInput", {})
    check(midi == {"channel": 0, "quantizeStart": True, "quantizeEnd": True}, "Omni and end quantize read back")
    check(raw("vibera_transport", {"action": "midiInput", "midiChannel": 17}).get("isError"), "Channel 17 is refused")
    check(call("vibera_state").get("midiInput", {}).get("channel") == 0, "A refusal leaves the channel")
    raw("vibera_transport", {"action": "midiInput", "quantizeStart": False, "quantizeEnd": False})
    passed = True
finally:
    client.close()
    (output / "verification.json").write_text(json.dumps(dict(passed=passed, checks=checks), indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks), last=checks[-1] if checks else None)))
sys.exit(0 if passed else 1)
