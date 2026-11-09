"""Envelope follower modulator through MCP (isolated --mcp-test instance), checked in exported audio.

A 220 Hz tone on Insert 1 starts at 2 s. An 880 Hz tone on Insert 2 plays throughout but Insert 2's
volume is 0; a follower modulator (source Insert 1, depth 1) on Insert 2's volume lifts it with
Insert 1's level. In the export the 880 Hz component is silent before 2 s and present after.
The follower reads back as shape follower with its source and survives save/reopen.

usage: python scripts/verify-envelope-follower-mcp.py [executable] [output folder]
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
base = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else ROOT / "build/module-qualification/envelope-follower"
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


def write_tone(path, hz, seconds, silence=0.0):
    frames = [0] * int(silence * 48000) + [int(6000 * math.sin(2 * math.pi * hz * i / 48000)) for i in range(int(seconds * 48000))]
    with wave.open(str(path), "wb") as w:
        w.setparams((1, 2, 48000, 0, "NONE", "not compressed"))
        w.writeframes(array.array("h", frames).tobytes())


low, high = output / "original-follower-low.wav", output / "original-follower-high.wav"
write_tone(low, 220, 2, silence=2)
write_tone(high, 880, 4)

client = module.VibeRaClient(exe)
client.process.stdin.reconfigure(newline="\n")
passed = False
try:
    call = lambda tool, args=None: client.call(tool, args or {}, allow_failure=True).get("structuredContent", {})
    command = lambda name, **args: call("vibera_command", {"command": name, "args": args})

    def wait(job, seconds=120):
        deadline = time.monotonic() + seconds
        while job.get("completionState") == "pending" and time.monotonic() < deadline:
            time.sleep(.1)
            job = call("vibera_project", {"action": "job", "operation": job["operation"]})
        return job

    call("vibera_project", {"action": "new", "template": "empty", "discardChanges": True})
    command("project.tempo", value=120)
    command("audio.add", channel=0, text=str(low), length=4, lane=0, mixer=1)
    command("audio.add", channel=0, text=str(high), length=4, lane=1, mixer=2)
    clips = sorted((c for c in call("vibera_inspect", {"entity": "clips"})["items"] if c["kind"] == "audio"),
                   key=lambda c: c["lane"])
    for clip, insert in zip(clips, (1, 2)):
        command("channel.mixer", channel=clip["source"], mixer=insert)
    command("mixer.gain", mixer=2, value=0)
    result = command("automation.modulator", mixer=2, automationControl="gain", text="follower", target=1, strength=1)
    check(result.get("completionState") == "completed", "A follower modulator on Insert 2 volume: " + str(result.get("error"))[:120])
    row = call("vibera_inspect", {"entity": "modulators"})["items"][0]
    check(row["shape"] == "follower" and row["active"], "The follower reads back active")
    check(command("automation.modulator", mixer=3, automationControl="gain", text="follower", target=999)
          .get("completionState") == "failed", "A missing source insert is refused")

    wav = output / "follower.wav"
    job = wait(call("vibera_project", {"action": "export", "path": str(wav), "overwrite": True}))
    check(job.get("completionState") == "completed", "Exports")
    with wave.open(str(wav)) as w:
        raw = np.frombuffer(w.readframes(w.getnframes()), dtype=np.uint8).reshape(-1, 3 * w.getnchannels())[:, :3]
        rate = w.getframerate()
    values = (raw[:, 0].astype(np.int32) | (raw[:, 1].astype(np.int32) << 8) | (raw[:, 2].astype(np.int32) << 16))
    values = np.where(values >= 1 << 23, values - (1 << 24), values).astype(float)

    def band(start, end, hz):
        part = values[int(start * rate):int(end * rate)] * np.hanning(int(end * rate) - int(start * rate))
        spectrum = np.abs(np.fft.rfft(part))
        freqs = np.fft.rfftfreq(len(part), 1 / rate)
        return float(spectrum[(freqs > hz - 20) & (freqs < hz + 20)].max())

    before, after = band(.5, 1.5, 880), band(2.5, 3.5, 880)
    (output / "audio.json").write_text(json.dumps(dict(before=before, after=after, low_after=band(2.5, 3.5, 220))), encoding="utf-8")
    check(before < after / 100, f"880 Hz is silent while Insert 1 is silent ({before:.1f})")
    check(after > 1000, f"880 Hz rises once Insert 1 plays ({after:.1f})")

    project = output / "follower.vibra"
    wait(call("vibera_project", {"action": "save", "path": str(project), "overwrite": True}))
    call("vibera_project", {"action": "new", "template": "empty", "discardChanges": True})
    wait(call("vibera_project", {"action": "open", "path": str(project), "discardChanges": True}))
    row = call("vibera_inspect", {"entity": "modulators"})["items"][0]
    check(row["shape"] == "follower", "The follower survives save and reopen")
    passed = True
finally:
    client.close()
    (output / "verification.json").write_text(json.dumps(dict(passed=passed, checks=checks), indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks), last=checks[-1] if checks else None)))
sys.exit(0 if passed else 1)
