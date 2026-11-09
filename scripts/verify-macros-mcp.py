"""Macro knobs through MCP (isolated --mcp-test instance).

automation.macro.add creates a knob; automation.macro.link links Mixer controls with full or custom
(inverted) ranges; automation.macro.set moves every target together (vibera_inspect macros reads
the compiled values); a modulated target is refused; Undo and save/reopen (schema 57) keep it.
Audio: a tone through Insert 1 with the macro on Master volume (range 0..1) is silent at 0, about
half level at 0.5 and full at 1 in exported WAVs.

usage: python scripts/verify-macros-mcp.py [executable] [output folder]
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

ROOT = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe").resolve()
base = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else ROOT / "build/module-qualification/macros"
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


tone = output / "original-macro-tone.wav"
with wave.open(str(tone), "wb") as w:
    w.setparams((1, 2, 48000, 0, "NONE", "not compressed"))
    w.writeframes(array.array("h", [int(6000 * math.sin(2 * math.pi * 220 * i / 48000)) for i in range(96000)]).tobytes())

client = module.VibeRaClient(exe)
client.process.stdin.reconfigure(newline="\n")
passed = False
try:
    call = lambda tool, args=None: client.call(tool, args or {}, allow_failure=True).get("structuredContent", {})
    command = lambda name, **args: call("vibera_command", {"command": name, "args": args})
    macros = lambda: call("vibera_inspect", {"entity": "macros"}).get("items", [])

    def wait(job, seconds=120):
        deadline = time.monotonic() + seconds
        while job.get("completionState") == "pending" and time.monotonic() < deadline:
            time.sleep(.1)
            job = call("vibera_project", {"action": "job", "operation": job["operation"]})
        return job

    def export_rms(name):
        path = output / name
        job = wait(call("vibera_project", {"action": "export", "path": str(path), "overwrite": True}))
        check(job.get("completionState") == "completed", "Export " + name)
        with wave.open(str(path)) as w:
            data, channels = w.readframes(w.getnframes()), w.getnchannels()
        values = [int.from_bytes(data[i:i + 3], "little", signed=True) for i in range(0, len(data), 3 * channels)]
        middle = values[len(values) // 8: len(values) // 2]
        return math.sqrt(sum(v * v for v in middle) / len(middle))

    call("vibera_project", {"action": "new", "template": "empty", "discardChanges": True})
    call("vibera_command", {"command": "audio.add", "args": {"channel": 0, "text": str(tone), "length": 2, "lane": 0, "mixer": 1}})
    check(command("automation.macro.add", text="Build").get("completionState") == "completed" and len(macros()) == 1,
          "A macro knob is added")
    macro = macros()[0]["id"]
    check(command("automation.macro.link", ids=[macro], mixer=0, automationControl="gain", text="range", beat=0, length=1)
          .get("completionState") == "completed", "Master volume linked with range 0..1")
    check(command("automation.macro.link", ids=[macro], mixer=2, automationControl="pan", text="range", beat=1, length=-1)
          .get("completionState") == "completed", "Insert 2 pan linked with an inverted range 1..-1")
    command("automation.macro.set", target=macro, value=.25)
    values = [t["currentValue"] for t in macros()[0]["targets"]]
    check(abs(values[0] - .25) < 1e-6 and abs(values[1] - .5) < 1e-6, f"Both targets follow the knob ({values})")
    check(command("automation.macro.set", target=macro, value=2).get("completionState") == "failed", "A value above 1 is refused")
    command("automation.modulator", mixer=3, automationControl="pan", text="sine", value=4, strength=.5)
    check(command("automation.macro.link", ids=[macro], mixer=3, automationControl="pan").get("completionState") == "failed",
          "A modulated target cannot also be a macro target")
    command("edit.undo")
    check(len(macros()[0]["targets"]) == 2, "Undo keeps the macro and its two targets")

    levels = {}
    for value in (0, .5, 1):
        command("automation.macro.set", target=macro, value=value)
        levels[value] = export_rms(f"macro-{value}.wav")
    (output / "audio.json").write_text(json.dumps(levels), encoding="utf-8")
    check(levels[0] < 1, f"Macro 0: Master volume 0 is silent ({levels})")
    check(levels[1] > 1000 and abs(levels[.5] / levels[1] - .5) < .05, f"Macro 0.5 is half the level of macro 1 ({levels})")

    project = output / "macros.vibra"
    check(wait(call("vibera_project", {"action": "save", "path": str(project), "overwrite": True})).get("completionState")
          == "completed" and b"MACRO" in project.read_bytes(), "Saves the macro")
    call("vibera_project", {"action": "new", "template": "empty", "discardChanges": True})
    wait(call("vibera_project", {"action": "open", "path": str(project), "discardChanges": True}))
    rows = macros()
    check(len(rows) == 1 and rows[0]["value"] == 1 and len(rows[0]["targets"]) == 2, "The macro survives save and reopen")
    command("automation.macro.unlink", target=rows[0]["id"], value=1)
    check(len(macros()[0]["targets"]) == 1, "Unlink removes one target")
    passed = True
finally:
    client.close()
    (output / "verification.json").write_text(json.dumps(dict(passed=passed, checks=checks), indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks), last=checks[-1] if checks else None)))
sys.exit(0 if passed else 1)
