"""Loudness analysis through MCP (isolated --mcp-test instance).

A generated 997 Hz 0 dBFS one-channel WAV must read -3.01 LUFS (+/-0.1) with a 0 dBTP true
peak; an exported demo Song must read a finite integrated loudness below its true peak; a
silent file reports null loudness; an unreadable file is refused.

usage: python scripts/verify-loudness-mcp.py [executable] [output folder]
"""
import importlib.util
import json
import math
from pathlib import Path
import struct
import sys
import time
import uuid
import wave

ROOT = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe").resolve()
base = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else ROOT / "build/module-qualification/loudness"
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


def write_wav(path, left, right):
    with wave.open(str(path), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(48000)
        w.writeframes(b"".join(struct.pack("<hh", int(a), int(b)) for a, b in zip(left, right)))


client = module.VibeRaClient(exe)
client.process.stdin.reconfigure(newline="\n")
passed = False
measured = {}
try:
    call = lambda args: client.call("vibera_project", args, allow_failure=True).get("structuredContent", {})
    n = 48000 * 10
    tone = [32767 * math.sin(2 * math.pi * 997 * i / 48000) for i in range(n)]
    reference = output / "reference.wav"
    write_wav(reference, tone, [0] * n)
    r = call({"action": "loudness", "path": str(reference)})
    measured["reference"] = r
    check(r.get("completionState") == "completed" and abs(r["integratedLufs"] + 3.01) < .1,
          f"997 Hz 0 dBFS one channel reads -3.01 LUFS ({r.get('integratedLufs')})")
    check(abs(r["truePeakDbtp"]) < .1, f"Its true peak is 0 dBTP ({r.get('truePeakDbtp')})")
    silent = output / "silent.wav"
    write_wav(silent, [0] * 48000, [0] * 48000)
    s = call({"action": "loudness", "path": str(silent)})
    check(s.get("completionState") == "completed" and s.get("integratedLufs") is None, "Silence reports null loudness")
    junk = output / "junk.wav"
    junk.write_bytes(b"not audio")
    bad = call({"action": "loudness", "path": str(junk)})
    check(bad.get("completionState") == "failed", "An unreadable file is refused")
    call({"action": "new", "template": "demo", "discardChanges": True})
    song = output / "demo.wav"
    job = call({"action": "export", "path": str(song), "overwrite": True})
    deadline = time.monotonic() + 120
    while job.get("completionState") == "pending" and time.monotonic() < deadline:
        time.sleep(.2)
        job = call({"action": "job", "operation": job["operation"]})
    check(job.get("completionState") == "completed", "The demo Song exports")
    d = call({"action": "loudness", "path": str(song)})
    measured["demo"] = d
    check(d.get("integratedLufs") is not None and -40 < d["integratedLufs"] < 0, f"The demo reads a finite loudness ({d.get('integratedLufs')})")
    check(d["truePeakDbtp"] >= d["samplePeakDbfs"] - 1e-6, "True peak is never below the sample peak")
    passed = True
finally:
    client.close()
    (output / "verification.json").write_text(json.dumps(dict(passed=passed, checks=checks, measured=measured), indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks), demo=measured.get("demo", {}).get("integratedLufs"))))
sys.exit(0 if passed else 1)
