"""Vocal pitch editor acceptance through MCP (isolated --mcp-test instance).

A generated monophonic "voice" (harmonic tone with vibrato) sings four notes that are off pitch by
known amounts. vocalPitch must find four notes with those pitches (within 8 cents); vocalPitchApply
with snap=1 renders a new source of the same length in which every note is within 8 cents of a
semitone, and an explicit shift moves one note by exactly +2 semitones. One Undo restores the
original source; invalid shifts are refused.

usage: python scripts/verify-vocal-pitch-mcp.py [executable] [output folder]
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
base = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else ROOT / "build/module-qualification/vocal-pitch"
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


# Four sung notes (MIDI pitch with deliberate cents error), 0.5 s each, 0.1 s breaths between.
RATE = 48000
sung = [57.30, 60 - .20, 64.0, 55.45]  # A4 +30, C5 -20, E5, G4 +45 cents (C5 = 60)
source = output / "original-vocal-fixture.wav"
samples = []
phase = 0.0
for k, midi in enumerate(sung):
    hz = 440 * 2 ** ((midi - 69) / 12)
    n = int(.5 * RATE)
    for i in range(n):
        t = i / RATE
        vibrato = 1 + .002 * math.sin(2 * math.pi * 5.5 * t)  # about +/-3.5 cents
        phase += 2 * math.pi * hz * vibrato / RATE
        envelope = min(1, i / 960, (n - i) / 960)
        value = sum(math.sin(h * phase) / h for h in range(1, 7)) * .25 * envelope
        samples.append(value)
    samples.extend([0.0] * int(.1 * RATE))
with wave.open(str(source), "wb") as w:
    w.setparams((1, 2, RATE, 0, "NONE", "not compressed"))
    w.writeframes(b"".join(struct.pack("<h", int(max(-1, min(1, s)) * 32000)) for s in samples))
duration = len(samples) / RATE

client = module.VibeRaClient(exe)
client.process.stdin.reconfigure(newline="\n")
passed = False
try:
    def call(tool, args=None):
        return client.call(tool, args or {}, allow_failure=True).get("structuredContent", {})

    def wait(job, seconds=120):
        deadline = time.monotonic() + seconds
        while job.get("completionState") == "pending" and time.monotonic() < deadline:
            time.sleep(.1)
            job = call("vibera_project", {"action": "job", "operation": job["operation"]})
        return job

    def clip_source(clip_id):
        clip = next(c for c in call("vibera_inspect", {"entity": "clips"})["items"] if c["id"] == clip_id)
        channel = next(c for c in call("vibera_inspect", {"entity": "channels"})["items"] if c["id"] == clip["source"])
        return channel["source"]

    call("vibera_project", {"action": "new", "template": "empty", "discardChanges": True})
    call("vibera_command", {"command": "audio.add", "args": {"channel": 0, "text": str(source), "length": duration, "lane": 0}})
    clip = next(c for c in call("vibera_inspect", {"entity": "clips"})["items"] if c["kind"] == "audio")["id"]
    analysis = call("vibera_project", {"action": "vocalPitch", "clip": clip})
    notes = analysis.get("notes", [])
    found = [round(n["pitch"], 2) for n in notes]
    check(len(notes) == 4, f"Four sung notes are found ({found})")
    check(all(abs(n["pitch"] - want) < .08 for n, want in zip(notes, sung)), f"Note pitches within 8 cents ({found})")
    check(all(abs(n["start"] - k * .6) < .06 and abs(n["end"] - (k * .6 + .5)) < .06 for k, n in enumerate(notes)),
          "Note boundaries match the sung notes")
    check([n["name"] for n in notes] == ["A4", "C5", "E5", "G4"], "Note names")
    check(call("vibera_project", {"action": "vocalPitchApply", "clip": clip, "shifts": [{"note": 9, "semitones": 1}]})
          .get("completionState") != "pending", "An unknown note index is refused")

    original = clip_source(clip)
    job = wait(call("vibera_project", {"action": "vocalPitchApply", "clip": clip, "snap": 1,
                                        "shifts": [{"note": 2, "semitones": 2}]}))
    check(job.get("completionState") == "completed", "vocalPitchApply renders: " + str(job)[:200])
    edited = clip_source(clip)
    check(edited != original and Path(edited).exists(), "The clip now plays a new source file")
    with wave.open(edited) as w:
        check(abs(w.getnframes() / w.getframerate() - duration) < .01, "The edited source keeps the length")
    after = call("vibera_project", {"action": "vocalPitch", "clip": clip}).get("notes", [])
    pitches = [round(n["pitch"], 2) for n in after]
    check(len(after) == 4, f"Four notes after editing ({pitches})")
    targets = [57, 60, 66, 55]  # Snapped, with E4 moved up two semitones to F#4.
    check(all(abs(n["pitch"] - want) < .08 for n, want in zip(after, targets)),
          f"Snapped notes sit within 8 cents of a semitone and the moved note reads F#5 ({pitches})")
    call("vibera_command", {"command": "edit.undo", "args": {}})
    check(clip_source(clip) == original, "One Undo restores the original source")

    # Formants: shift note 2 by +6 formant semitones only. Its pitch stays, its spectral centroid rises.
    import numpy as np

    def centroid(path, start, end):
        with wave.open(path) as w:
            raw = np.frombuffer(w.readframes(w.getnframes()), dtype=np.uint8)
            width, rate = w.getsampwidth(), w.getframerate()
        if width == 3:
            raw = raw.reshape(-1, 3)
            values = (raw[:, 0].astype(np.int32) | (raw[:, 1].astype(np.int32) << 8) | (raw[:, 2].astype(np.int32) << 16))
            values = np.where(values >= 1 << 23, values - (1 << 24), values).astype(float)
        else:
            values = raw.view(np.int16).astype(float)
        part = values[int(start * rate):int(end * rate)] * np.hanning(int(end * rate) - int(start * rate))
        spectrum = np.abs(np.fft.rfft(part))
        freqs = np.fft.rfftfreq(len(part), 1 / rate)
        return float((spectrum * freqs).sum() / spectrum.sum())

    before = centroid(original, 1.25, 1.65)
    job = wait(call("vibera_project", {"action": "vocalPitchApply", "clip": clip, "shifts": [{"note": 2, "formant": 6}]}))
    check(job.get("completionState") == "completed", "A formant-only shift renders: " + str(job)[:160])
    formanted = clip_source(clip)
    after = centroid(formanted, 1.25, 1.65)
    pitch_after = call("vibera_project", {"action": "vocalPitch", "clip": clip}).get("notes", [])[2]["pitch"]
    check(abs(pitch_after - sung[2]) < .1, f"The formant shift keeps the pitch ({pitch_after:.2f})")
    check(after > before * 1.05, f"Formant +6 brightens the voice (centroid {before:.0f} -> {after:.0f} Hz)")
    call("vibera_command", {"command": "edit.undo", "args": {}})
    job = wait(call("vibera_project", {"action": "vocalPitchApply", "clip": clip, "shifts": [{"note": 2, "formant": -6}]}))
    darker = centroid(clip_source(clip), 1.25, 1.65)
    check(job.get("completionState") == "completed" and darker < before * .95,
          f"Formant -6 darkens the voice (centroid {before:.0f} -> {darker:.0f} Hz)")
    # Timing: move note 1 later by 50 ms; its neighbours stay, the clip length stays.
    call("vibera_command", {"command": "edit.undo", "args": {}})
    job = wait(call("vibera_project", {"action": "vocalPitchApply", "clip": clip, "shifts": [{"note": 1, "time": .05}]}))
    check(job.get("completionState") == "completed", "A timing move renders: " + str(job)[:160])
    moved = call("vibera_project", {"action": "vocalPitch", "clip": clip}).get("notes", [])
    starts = [round(n["start"], 3) for n in moved]
    check(len(moved) == 4 and abs(moved[1]["start"] - (.6 + .05)) < .03 and abs(moved[2]["start"] - 1.2) < .03 and
          abs(moved[0]["start"]) < .03, f"Note 1 starts 50 ms later, its neighbours stay ({starts})")
    check(abs(moved[1]["pitch"] - sung[1]) < .1, "The moved note keeps its pitch")
    with wave.open(clip_source(clip)) as w:
        check(abs(w.getnframes() / w.getframerate() - duration) < .01, "The retimed clip keeps its length")
    (output / "formant.json").write_text(json.dumps(dict(before=before, up=after, down=darker, pitch=pitch_after)),
                                         encoding="utf-8")
    (output / "pitches.json").write_text(json.dumps(dict(sung=sung, found=found, edited=pitches), indent=2), encoding="utf-8")
    passed = True
finally:
    client.close()
    (output / "verification.json").write_text(json.dumps(dict(passed=passed, checks=checks), indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks), last=checks[-1] if checks else None)))
sys.exit(0 if passed else 1)
