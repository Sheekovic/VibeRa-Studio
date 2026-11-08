"""Modulator acceptance through MCP (isolated --mcp-test instance).

automation.modulator adds a tempo-synced LFO to a Mixer control; vibera_inspect modulators reads
back the compiled program's quarter-cycle values (sine around the manual value, depth as a fraction
of the range); set changes shape/rate/depth, enable turns it off, a duplicate or an automated target
is refused, Undo restores, it survives save/reopen (schema 57), and deleting the insert removes it.
Audio: a full-depth square on Master volume with a 4-beat period silences the second half of every
bar in an exported WAV while the first half plays.

usage: python scripts/verify-modulators-mcp.py [executable] [output folder]
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
base = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else ROOT / "build/module-qualification/modulators"
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


def close(a, b, tolerance=1e-3):
    return abs(a - b) <= tolerance


client = module.VibeRaClient(exe)
client.process.stdin.reconfigure(newline="\n")
passed = False
try:
    def call(tool, args=None):
        return client.call(tool, args or {}, allow_failure=True).get("structuredContent", {})

    def command(name, **args):
        return call("vibera_command", {"command": name, "args": args})

    def modulators():
        return call("vibera_inspect", {"entity": "modulators"}).get("items", [])

    def wait(job, seconds=120):
        deadline = time.monotonic() + seconds
        while job.get("completionState") == "pending" and time.monotonic() < deadline:
            time.sleep(.1)
            job = call("vibera_project", {"action": "job", "operation": job["operation"]})
        return job

    call("vibera_project", {"action": "new", "template": "demo", "discardChanges": True})
    mixer = call("vibera_inspect", {"entity": "mixer"})["items"]
    insert = next(m for m in mixer if m["index"] != 0)["index"]
    result = command("automation.modulator", mixer=insert, automationControl="pan", text="sine", value=4,
                     strength=.5, offset=0)
    check(result.get("completionState") == "completed", "automation.modulator adds a pan LFO: " + str(result.get("error"))[:160])
    rows = modulators()
    check(len(rows) == 1 and rows[0]["shape"] == "sine" and rows[0]["period"] == 4 and rows[0]["active"],
          "The modulator reads back active with its shape and period")
    values = rows[0]["quarterCycleValues"]
    # Pan range -1..1, manual 0, depth 0.5 -> swing +/-0.5: sine at 0, 1/4, 1/2, 3/4 cycle.
    check(close(values[0], 0) and close(values[1], .5) and close(values[2], 0) and close(values[3], -.5),
          f"Compiled sine swings +/-0.5 around centre pan ({values})")
    mod = rows[0]["id"]
    check(command("automation.modulator", mixer=insert, automationControl="pan").get("completionState") == "failed",
          "A second modulator on the same control is refused")
    check(command("automation.modulator.set", target=mod, text="square", value=2, strength=1, offset=0)
          .get("completionState") == "completed", "automation.modulator.set changes it")
    values = modulators()[0]["quarterCycleValues"]
    check(values == [1, 1, -1, -1] and modulators()[0]["period"] == 2,
          f"Full-depth square clamps to the pan range ({values})")
    check(command("automation.modulator.set", target=mod, text="wobble", value=2, strength=1)
          .get("completionState") == "failed", "An unknown shape is refused")
    check(command("automation.modulator.set", target=mod, text="sine", value=5000, strength=1)
          .get("completionState") == "failed", "A period outside 1/64..1024 beats is refused")
    check(command("automation.modulator.enable", target=mod, enabled=False).get("completionState") == "completed"
          and not modulators()[0]["active"], "Disabling keeps it but makes it inactive")
    command("automation.modulator.enable", target=mod, enabled=True)
    clip = command("automation.create", mixer=insert, automationControl="pan", beat=0, length=4)
    check(clip.get("completionState") == "failed", "An Automation Clip cannot target a modulated control")
    command("edit.undo")
    check(not modulators()[0]["active"], "Undo restores the disabled state")
    command("edit.redo")

    project = output / "modulators.vibra"
    job = wait(call("vibera_project", {"action": "save", "path": str(project), "overwrite": True}))
    check(job.get("completionState") == "completed" and b'VIBERA version="57"' in project.read_bytes()
          and b"MODULATOR" in project.read_bytes(), "The project saves modulators at schema 57")
    call("vibera_project", {"action": "new", "template": "empty", "discardChanges": True})
    check(modulators() == [], "A new project has no modulators")
    wait(call("vibera_project", {"action": "open", "path": str(project), "discardChanges": True}))
    rows = modulators()
    check(len(rows) == 1 and rows[0]["shape"] == "square" and rows[0]["period"] == 2 and rows[0]["active"],
          "Modulators survive save and reopen")
    deleted = command("mixer.insert.delete", mixer=insert,
                      mixerDelete={"replacement": 0, "removeAutomation": True, "removeEffects": True, "removeGroupMembership": True})
    deadline = time.monotonic() + 60
    while deleted.get("completionState") == "pending" and time.monotonic() < deadline:
        time.sleep(.1)
        deleted = call("vibera_mixerchain", {"action": "job", "operation": deleted["operation"]})
    check(deleted.get("completionState") == "completed" and modulators() == [],
          "Deleting the insert removes its modulator: " + str(deleted.get("message") or deleted.get("error"))[:160])

    # Audio: square on Master volume, period one bar (4 beats), full depth.
    call("vibera_project", {"action": "new", "template": "demo", "discardChanges": True})
    check(command("automation.modulator", mixer=0, automationControl="gain", text="square", value=4, strength=1)
          .get("completionState") == "completed", "A Master volume modulator is added")
    state = call("vibera_state")
    tempo = state.get("tempo") or call("vibera_inspect", {"entity": "tempo"})["items"][0]["bpm"]
    wav = output / "square.wav"
    job = wait(call("vibera_project", {"action": "export", "path": str(wav), "overwrite": True}), 300)
    check(job.get("completionState") == "completed", "The song exports: " + str(job.get("error"))[:160])
    with wave.open(str(wav)) as w:
        rate, channels, width, frames = w.getframerate(), w.getnchannels(), w.getsampwidth(), w.getnframes()
        data = w.readframes(frames)
    check(width == 3, "24-bit export")
    samples = [int.from_bytes(data[i:i + 3], "little", signed=True) for i in range(0, len(data), 3 * channels)]
    beat = int(rate * 60 / float(tempo))
    loud, quiet = [], []
    for bar in range(min(4, len(samples) // (4 * beat))):
        start = bar * 4 * beat
        first = samples[start + beat // 8: start + 2 * beat - beat // 8]
        second = samples[start + 2 * beat + beat // 8: start + 4 * beat - beat // 8]
        rms = lambda part: math.sqrt(sum(s * s for s in part) / max(1, len(part)))
        loud.append(rms(first))
        quiet.append(rms(second))
    report = dict(tempo=tempo, rate=rate, loud=loud, quiet=quiet)
    (output / "audio.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    check(len(loud) >= 2 and all(l > 1000 for l in loud) and all(q < 1e-3 * 8388608 for q in quiet),
          f"The first half of each bar plays and the second half is silent ({report})")
    passed = True
finally:
    client.close()
    (output / "verification.json").write_text(json.dumps(dict(passed=passed, checks=checks), indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks), last=checks[-1] if checks else None)))
sys.exit(0 if passed else 1)
