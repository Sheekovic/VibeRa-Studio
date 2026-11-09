"""Master pitch through MCP, checked in exported audio (isolated --mcp-test instance).

A sampler Channel plays a 440 Hz sine at its root note. project.masterPitch 0 renders ~440 Hz,
+1200 cents ~880 Hz and +100 cents ~466 Hz (one semitone); the value reads back in
vibera_state, survives save/reopen, is one Undo step, and out-of-range values are refused.

usage: python scripts/verify-master-pitch-mcp.py [executable] [output folder]
"""
import array
import importlib.util
import json
import math
from pathlib import Path
import sys
import time
import uuid
import wave

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe").resolve()
base = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else ROOT / "build/module-qualification/master-pitch"
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


source = output / "sine-440.wav"
with wave.open(str(source), "wb") as w:
    w.setparams((1, 2, 48000, 0, "NONE", "not compressed"))
    w.writeframes(array.array("h", [int(8000 * math.sin(2 * math.pi * 440 * i / 48000)) for i in range(96000)]).tobytes())

client = module.VibeRaClient(exe)
client.process.stdin.reconfigure(newline="\n")
passed = False
try:
    raw = lambda tool, args=None: client.call(tool, args or {}, allow_failure=True)
    call = lambda tool, args=None: raw(tool, args).get("structuredContent", {})
    command = lambda name, **args: call("vibera_command", {"command": name, "args": args})

    def ready():
        end = time.monotonic() + 15
        while call("vibera_state")["pendingAudioUpdate"] and time.monotonic() < end:
            time.sleep(.02)

    def peak_hz(name):
        path = output / name
        job = call("vibera_project", {"action": "export", "path": str(path), "overwrite": True})
        client.wait_job(job)
        with wave.open(str(path)) as w:
            data = np.frombuffer(w.readframes(w.getnframes()), dtype=np.uint8).reshape(-1, 3 * w.getnchannels())[:, :3]
            rate = w.getframerate()
        values = (data[:, 0].astype(np.int32) | (data[:, 1].astype(np.int32) << 8) | (data[:, 2].astype(np.int32) << 16))
        values = np.where(values >= 1 << 23, values - (1 << 24), values).astype(float)
        part = values[int(.1 * rate):int(.9 * rate)] * np.hanning(int(.8 * rate))
        spectrum = np.abs(np.fft.rfft(part))
        freqs = np.fft.rfftfreq(len(part), 1 / rate)
        return float(freqs[int(np.argmax(spectrum))])

    call("vibera_project", {"action": "new", "template": "empty", "discardChanges": True})
    state = call("vibera_state")
    channel, pattern = state["channels"][0]["id"], state["selectedPattern"]
    command("channel.source", channel=channel, text=str(source))
    command("notes.add", pattern=pattern, channel=channel, beat=0, length=2, pitch=60, value=1)
    command("clip.add", pattern=pattern, beat=0, length=4)
    ready()
    check(call("vibera_state").get("masterPitch") == 0, "Master pitch defaults to 0")
    base_hz = peak_hz("pitch-0.wav")
    check(abs(base_hz - 440) < 3, f"0 cents renders ~440 Hz ({base_hz:.1f})")

    command("project.masterPitch", value=1200)
    ready()
    check(call("vibera_state").get("masterPitch") == 1200, "+1200 reads back")
    octave = peak_hz("pitch-1200.wav")
    check(abs(octave - 880) < 4, f"+1200 cents renders ~880 Hz ({octave:.1f})")

    command("project.masterPitch", value=100)
    ready()
    semitone = peak_hz("pitch-100.wav")
    check(abs(semitone - 440 * 2 ** (1 / 12)) < 3, f"+100 cents renders ~466 Hz ({semitone:.1f})")

    check(command("project.masterPitch", value=1300).get("completionState") != "completed", "1300 cents is refused")
    check(call("vibera_state").get("masterPitch") == 100, "A refusal leaves the value")
    command("edit.undo")
    check(call("vibera_state").get("masterPitch") == 1200, "Undo restores the previous value")
    command("edit.redo")

    project = output / "master-pitch.vibra"
    client.wait_job(call("vibera_project", {"action": "save", "path": str(project), "overwrite": True}))
    call("vibera_project", {"action": "new", "template": "empty", "discardChanges": True})
    client.wait_job(call("vibera_project", {"action": "open", "path": str(project), "discardChanges": True}))
    check(call("vibera_state").get("masterPitch") == 100, "Master pitch survives save and reopen")
    passed = True
finally:
    client.close()
    (output / "verification.json").write_text(json.dumps(dict(passed=passed, checks=checks), indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks), last=checks[-1] if checks else None)))
sys.exit(0 if passed else 1)
