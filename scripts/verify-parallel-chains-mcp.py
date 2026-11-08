"""Parallel chains (FX container) acceptance through MCP (isolated --mcp-test instance).

mixer.insert.create with target=<insert> adds parallel branches: each named '<source> Parallel N',
fed by a unity send from the source and routed to the source's destinations at the same levels.
Audio: a tone on Insert 1 exported before and after adding one empty branch doubles in level (the
dry path plus an identical parallel path), and muting the branch restores the original level.
Master and missing sources are refused; one Undo removes every branch.

usage: python scripts/verify-parallel-chains-mcp.py [executable] [output folder]
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
base = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else ROOT / "build/module-qualification/parallel-chains"
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


tone = output / "original-parallel-tone.wav"
with wave.open(str(tone), "wb") as w:
    w.setparams((1, 2, 48000, 0, "NONE", "not compressed"))
    w.writeframes(array.array("h", [int(6000 * math.sin(2 * math.pi * 220 * i / 48000)) for i in range(96000)]).tobytes())

client = module.VibeRaClient(exe)
client.process.stdin.reconfigure(newline="\n")
passed = False
try:
    def call(tool, args=None):
        return client.call(tool, args or {}, allow_failure=True).get("structuredContent", {})

    def command(name, **args):
        result = call("vibera_command", {"command": name, "args": args})
        deadline = time.monotonic() + 60
        while result.get("completionState") == "pending" and result.get("operation") and time.monotonic() < deadline:
            time.sleep(.1)
            result = call("vibera_mixerchain", {"action": "job", "operation": result["operation"]})
        return result

    def mixer():
        return {m["index"]: m for m in call("vibera_inspect", {"entity": "mixer"})["items"]}

    def routes(row):
        return sorted((r["target"], round(r["gain"], 4)) for r in row["routes"])

    def export(name):
        path = output / name
        job = call("vibera_project", {"action": "export", "path": str(path), "overwrite": True})
        deadline = time.monotonic() + 120
        while job.get("completionState") == "pending" and time.monotonic() < deadline:
            time.sleep(.1)
            job = call("vibera_project", {"action": "job", "operation": job["operation"]})
        check(job.get("completionState") == "completed", "Export " + name + ": " + str(job.get("error"))[:120])
        with wave.open(str(path)) as w:
            data, channels = w.readframes(w.getnframes()), w.getnchannels()
        values = [int.from_bytes(data[i:i + 3], "little", signed=True) for i in range(0, len(data), 3 * channels)]
        middle = values[len(values) // 8: len(values) // 2]
        return math.sqrt(sum(v * v for v in middle) / len(middle))

    call("vibera_project", {"action": "new", "template": "empty", "discardChanges": True})
    call("vibera_command", {"command": "audio.add", "args": {"channel": 0, "text": str(tone), "length": 2, "lane": 0, "mixer": 1}})
    before_rms = export("before.wav")
    source_routes = routes(mixer()[1])
    count = len(mixer())
    result = command("mixer.insert.create", target=1, value=2)
    check(result.get("completionState") == "completed", "Two parallel chains are added: " + str(result)[:200])
    rows = mixer()
    branches = [m for m in rows.values() if m["name"].startswith(rows[1]["name"] + " Parallel")]
    check(len(rows) == count + 2 and len(branches) == 2, "Two branches named '<source> Parallel N'")
    check(all(routes(b) == source_routes for b in branches), "Each branch outputs where the source does")
    check(sorted(routes(rows[1])) == sorted(source_routes + [(b["index"], 1.0) for b in branches]),
          "The source sends to each branch at unity and keeps its dry routes")
    plain = command("mixer.insert.create", target=0)
    newest = max(mixer().values(), key=lambda m: m["index"])
    check(plain.get("completionState") == "completed" and routes(newest) == [(0, 1.0)] and
          not newest["name"].endswith("Parallel 1"), "target 0 adds an ordinary insert routed to Master")
    command("edit.undo")
    check(command("mixer.insert.create", target=999).get("completionState") == "failed", "A missing source is refused")
    command("edit.undo")
    check(len(mixer()) == count and routes(mixer()[1]) == source_routes, "One Undo removes both branches and their sends")

    command("mixer.insert.create", target=1, value=1)
    branch = next(m for m in mixer().values() if m["name"].endswith("Parallel 1"))
    after_rms = export("parallel.wav")
    ratio = after_rms / before_rms
    check(1.9 < ratio < 2.1, f"An empty parallel branch doubles the level (ratio {ratio:.3f})")
    command("mixer.mute", mixer=branch["index"], enabled=True)
    muted_rms = export("muted.wav")
    check(abs(muted_rms / before_rms - 1) < .05, f"Muting the branch restores the dry level (ratio {muted_rms / before_rms:.3f})")
    (output / "audio.json").write_text(json.dumps(dict(before=before_rms, parallel=after_rms, muted=muted_rms)), encoding="utf-8")
    passed = True
finally:
    client.close()
    (output / "verification.json").write_text(json.dumps(dict(passed=passed, checks=checks), indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks), last=checks[-1] if checks else None)))
sys.exit(0 if passed else 1)
