"""Export output channels and dither through MCP (isolated --mcp-test instance), checked in the WAVs.

A sampler Channel plays a sine panned hard left for half the Song. Exports: stereo keeps 2
channels; mono writes 1 channel at about half the left level; left keeps the signal; right is near
silence. 16-bit exports of the silent second half hold exact zeros without dither and +/-1 LSB
noise with dither. The options read back in vibera_state and invalid values are refused.

usage: python scripts/verify-export-options-mcp.py [executable] [output folder]
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
base = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else ROOT / "build/module-qualification/export-options"
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


source = output / "sine.wav"
with wave.open(str(source), "wb") as w:
    w.setparams((1, 2, 48000, 0, "NONE", "not compressed"))
    w.writeframes(array.array("h", [int(8000 * math.sin(2 * math.pi * 440 * i / 48000)) for i in range(48000)]).tobytes())


def read(path):
    with wave.open(str(path)) as w:
        channels, width, frames = w.getnchannels(), w.getsampwidth(), w.getnframes()
        raw = np.frombuffer(w.readframes(frames), dtype=np.uint8)
    if width == 2:
        data = raw.view("<i2").astype(float).reshape(-1, channels)
    else:
        b = raw.reshape(-1, channels, 3).astype(np.int32)
        v = b[..., 0] | (b[..., 1] << 8) | (b[..., 2] << 16)
        data = np.where(v >= 1 << 23, v - (1 << 24), v).astype(float)
    return channels, width, data


client = module.VibeRaClient(exe)
client.process.stdin.reconfigure(newline="\n")
passed = False
try:
    raw = lambda tool, args=None: client.call(tool, args or {}, allow_failure=True)
    call = lambda tool, args=None: raw(tool, args).get("structuredContent", {})
    command = lambda name, **args: call("vibera_command", {"command": name, "args": args})

    def export(name, **options):
        path = output / name
        client.wait_job(call("vibera_project", dict(action="export", path=str(path), overwrite=True, **options)))
        return read(path)

    call("vibera_project", {"action": "new", "template": "empty", "discardChanges": True})
    state = call("vibera_state")
    channel, pattern = state["channels"][0]["id"], state["selectedPattern"]
    command("channel.source", channel=channel, text=str(source))
    command("channel.pan", channel=channel, value=-1)
    command("notes.add", pattern=pattern, channel=channel, beat=0, length=1, pitch=60, value=1)
    command("clip.add", pattern=pattern, beat=0, length=4)
    end = time.monotonic() + 15
    while call("vibera_state")["pendingAudioUpdate"] and time.monotonic() < end:
        time.sleep(.02)

    ch, width, stereo = export("stereo.wav", outputChannels="stereo", dither=False, bitDepth=24)
    left_level, right_level = np.abs(stereo[:20000, 0]).max(), np.abs(stereo[:20000, 1]).max()
    check(ch == 2 and left_level > 100 * max(right_level, 1), f"Stereo: hard-left signal ({left_level:.0f} vs {right_level:.0f})")
    check(call("vibera_state").get("exportOutputChannels") == "stereo", "outputChannels reads back")

    ch, _, mono = export("mono.wav", outputChannels="mono")
    check(ch == 1 and abs(np.abs(mono[:20000, 0]).max() / left_level - .5) < .05, "Mono: one channel at half the left level")
    ch, _, left = export("left.wav", outputChannels="left")
    check(ch == 1 and abs(np.abs(left[:20000, 0]).max() / left_level - 1) < .02, "Left only keeps the signal")
    ch, _, right = export("right.wav", outputChannels="right")
    check(ch == 1 and np.abs(right[:20000, 0]).max() < left_level / 100, "Right only is near silence")

    _, width, plain = export("plain16.wav", outputChannels="stereo", bitDepth=16, dither=False)
    tail = plain[len(plain) // 2 + 4800:]
    check(width == 2 and np.abs(tail).max() == 0, "16-bit without dither: the silent half is exact zeros")
    _, _, dithered = export("dither16.wav", bitDepth=16, dither=True)
    noise = dithered[len(dithered) // 2 + 4800:]
    check(call("vibera_state").get("exportDither") is True and 0 < np.abs(noise).max() <= 2,
          f"16-bit with dither: +/-1 LSB noise in silence (max {np.abs(noise).max():.0f})")
    check(raw("vibera_project", {"action": "export", "path": str(output / "bad.wav"), "overwrite": True,
                                 "outputChannels": "surround"}).get("isError"), "An unknown output mode is refused")
    call("vibera_project", {"action": "export", "path": str(output / "reset.wav"), "overwrite": True,
                            "outputChannels": "stereo", "dither": False, "bitDepth": 24})
    passed = True
finally:
    client.close()
    (output / "verification.json").write_text(json.dumps(dict(passed=passed, checks=checks), indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks), last=checks[-1] if checks else None)))
sys.exit(0 if passed else 1)
