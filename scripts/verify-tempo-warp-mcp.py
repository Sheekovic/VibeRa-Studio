"""Warped Audio Clips following the Song tempo map, through MCP on the real application.

Places a 4 s tone clip at beat 4 (120 BPM), warps it, then edits the tempo map and checks
that the application re-renders the warp by itself: a jump to 60 BPM doubles the clip, a
move re-warps it again, and undoing an automatic re-warp does not loop. No audio device,
hardware or listening is involved.
"""
import importlib.util
import json
import math
import struct
import sys
import time
import uuid
import wave
from pathlib import Path

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("stdio_client", ROOT / "skills/vibera-studio/scripts/stdio_client.py")
client_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(client_module)
app = Path(sys.argv[1]).resolve()
output = Path(sys.argv[2]).resolve() / ("attempt-" + uuid.uuid4().hex)
output.mkdir(parents=True, exist_ok=False)
checks = []


def check(ok, label):
    if not ok:
        raise AssertionError(label)
    checks.append(label)


client = None
passed = False
try:
    client = client_module.VibeRaClient(app)
    client.process.stdin.reconfigure(newline="\n")
    data = client.data
    command = lambda name, **args: data("vibera_command", dict(command=name, args=args))
    client.wait_job(data("vibera_project", dict(action="new", discardChanges=True)))
    source = output / "tone.wav"
    with wave.open(str(source), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(48000)
        w.writeframes(b"".join(struct.pack("<h", int(8000 * math.sin(i * 2 * math.pi * 220 / 48000)))
                               for i in range(48000 * 4)))
    command("project.tempo", value=120)
    command("audio.takes", text=str(source), length=4, takes=[dict(beat=4, lane=1, offset=0, duration=4)])

    def clip():
        page = data("vibera_inspect", dict(entity="clips"))
        rows = next(v for v in page.values() if isinstance(v, list))
        return next(r for r in rows if r["kind"] == "audio")

    def settle(predicate, seconds=30):
        deadline = time.time() + seconds
        while time.time() < deadline:
            row = clip()
            if predicate(row):
                return row
            time.sleep(.25)
        return clip()

    first = clip()
    check(abs(first["length"] - 4) < 1e-6, "A 4 s clip at 120 BPM")
    client.wait_job(data("vibera_project", dict(action="warpClip", clip=first["id"], bpm=120)), seconds=60)
    warped = clip()
    check(abs(warped["length"] - 4) < .01, "Warping on a constant 120 BPM map keeps 4 s")
    command("tempo.change.add", beat=2, value=60, step=False)
    doubled = settle(lambda r: abs(r["length"] - 8) < .01)
    check(abs(doubled["length"] - 8) < .01, "A jump to 60 BPM before the clip re-warps it to 8 s by itself")
    command("clip.move", ids=[doubled["id"]], beat=-2)
    time.sleep(.3)
    moved = settle(lambda r: abs(r["length"] - 8) < .01 and abs(r["beat"] - 2) < 1e-6)
    check(abs(moved["beat"] - 2) < 1e-6 and abs(moved["length"] - 8) < .01,
          "Moving the clip keeps it warped to the map")
    command("tempo.curve", beat=2, length=8, value=120, text="linear")
    ramped = settle(lambda r: abs(r["length"] - 8) > .05)
    expected = 60 / 7.5 * math.log(2)
    check(abs(ramped["length"] - expected) < .05,
          f"A ramp under the clip re-warps it to the integral ({ramped['length']:.3f} s vs {expected:.3f} s)")
    command("edit.undo")
    time.sleep(2.5)
    after_undo = clip()
    check(abs(after_undo["length"] - 8) < .01,
          "Undoing the automatic re-warp restores the previous render without re-triggering it")
    passed = True
finally:
    receipt = dict(passed=passed, checks=checks, output=str(output), hardware=False, listening=False)
    (output / "verification.json").write_text(json.dumps(receipt, indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks))))
    if client:
        client.process.terminate()
sys.exit(0 if passed else 1)
