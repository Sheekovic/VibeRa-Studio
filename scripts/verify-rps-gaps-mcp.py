"""Actual production-stdio checks for the 2026-10-25 RPS gap closures.

Channel preset save/load jobs (sampler and native instrument), previewNote,
embedded file-loop application, HQ interpolation and Keep pitch readback.
Original fixtures; no listening, native OS input or hardware claim.
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

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("rps_gaps_client", ROOT / "skills/vibera-studio/scripts/stdio_client.py")
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


def tone(path, seconds=1.0):
    with wave.open(str(path), "wb") as w:
        w.setparams((1, 2, 48000, 0, "NONE", "not compressed"))
        w.writeframes(b"".join(struct.pack("<h", round(6000 * math.sin(2 * math.pi * 330 * i / 48000)))
                               for i in range(int(48000 * seconds))))


client = None
passed = False
try:
    client = client_module.VibeRaClient(app)
    client.process.stdin.reconfigure(newline="\n")
    data = client.data
    command = lambda name, **args: data("vibera_command", dict(command=name, args=args))
    project = lambda **args: data("vibera_project", args)
    channel = lambda ident: next(c for c in data("vibera_state")["channels"] if c["id"] == ident)

    def settle_job(job, limit=30):
        deadline = time.monotonic() + limit
        while job["completionState"] == "pending" and time.monotonic() < deadline:
            time.sleep(.1)
            job = project(action="job", operation=job["operation"])
        return job

    client.wait_job(project(action="new", discardChanges=True))
    sample = output / "tone.wav"
    tone(sample)
    command("channel.source", channel=1, text=str(sample))
    command("channel.sampler.root", channel=1, value=48)
    command("channel.sampler.hqInterpolation", channel=1, enabled=True)
    check(channel(1)["sampler"]["hqInterpolation"] is True, "HQ interpolation command and readback")

    preset = output / "tone.vibch"
    job = settle_job(project(action="savePreset", channel=1, path=str(preset)))
    check(job["completionState"] == "completed" and preset.is_file(), "savePreset job writes a sampler preset")
    refused = None
    try:
        project(action="savePreset", channel=1, path=str(preset))
    except RuntimeError as error:
        refused = str(error)
    check(refused and "output_exists" in refused, "savePreset refuses to overwrite without overwrite=true")
    revision = data("vibera_state")["revision"]
    job = project(action="loadPreset", channel=2, path=str(preset))
    check(job["completionState"] == "completed" and data("vibera_state")["revision"] == revision + 1 and
          channel(2)["sampler"]["root"] == 48 and channel(2)["sampler"]["hqInterpolation"] is True,
          "loadPreset applies the preset to another Channel in one revision")
    command("edit.undo")
    check(channel(2)["sampler"]["root"] != 48, "loadPreset is one Undo step")
    # Moved preset folder: the sample travels beside the preset file.
    moved = output / "moved"
    moved.mkdir()
    (moved / "tone.vibch").write_text(preset.read_text(encoding="utf-8").replace(str(sample), "C:/missing/tone.wav"),
                                      encoding="utf-8")
    tone(moved / "tone.wav")
    job = project(action="loadPreset", channel=2, path=str(moved / "tone.vibch"))
    # The original path is gone, so completion (not channelPreset.missingSample) proves sibling recovery.
    check(job["completionState"] == "completed" and channel(2)["sampler"]["root"] == 48,
          "Missing preset sample is recovered from beside the preset")
    command("edit.undo")

    descriptor = data("vibera_synth", dict(action="descriptor"))["descriptor"]
    command("plugin.instrument.replace", channel=3, text=descriptor)
    synth_preset = output / "synth.vibch"
    job = settle_job(project(action="savePreset", channel=3, path=str(synth_preset)), 60)
    check(job["completionState"] == "completed" and "PLUGIN" in synth_preset.read_text(encoding="utf-8"),
          "savePreset captures a native instrument Channel preset")

    check(data("vibera_transport", dict(action="previewNote", channel=1, seconds=.2)) is not None,
          "previewNote accepted on a stopped sampler Channel")
    bad = None
    try:
        data("vibera_transport", dict(action="previewNote", channel=999999))
    except RuntimeError as error:
        bad = str(error)
    check(bad and "invalid_target" in bad, "previewNote refuses an unknown Channel")

    command("channel.sampler.loopPoints", channel=1, beat=.25, length=.75, enabled=True)
    sampler = channel(1)["sampler"]
    check(sampler["loop"] is True and sampler["loopStart"] == .25 and sampler["loopEnd"] == .75,
          "loopPoints sets range and Use loop points atomically")
    check(sampler.get("fileLoopStart") is None, "A WAV without smpl loops reports no file loop")

    command("channel.sampler.sourceTempo", channel=1, value=60)
    command("channel.sampler.fitTempo", channel=1, enabled=True)
    started = time.monotonic()
    command("channel.sampler.stretch", channel=1, enabled=True)
    check(time.monotonic() - started < 2 and channel(1)["sampler"]["stretch"] is True,
          "Keep pitch returns promptly (stretch prepares in the background)")
    passed = True
finally:
    receipt = dict(passed=passed, checks=checks, output=str(output), nativeInput=False, listening=False)
    (output / "verification.json").write_text(json.dumps(receipt, indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks))))
    if client:
        client.process.terminate()
sys.exit(0 if passed else 1)
