"""Make unique as sample through MCP (isolated --mcp-test instance).

clip.uniqueSample gives an Audio Clip its own Channel pointing at a fresh copy of its audio file
("<name> unique.wav" beside the original). The original file and any other clip keep the old
Channel; Undo restores the clip's Channel (the copied file stays, like FL). A target that is not
an Audio Clip is refused and leaves no copy behind.

usage: python scripts/verify-unique-sample-mcp.py [executable] [output folder]
"""
import array
import importlib.util
import json
import math
from pathlib import Path
import sys
import uuid
import wave

ROOT = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe").resolve()
base = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else ROOT / "build/module-qualification/unique-sample"
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


source = output / "loop.wav"
with wave.open(str(source), "wb") as w:
    w.setparams((1, 2, 48000, 0, "NONE", "not compressed"))
    w.writeframes(array.array("h", [int(6000 * math.sin(2 * math.pi * 330 * i / 48000)) for i in range(48000)]).tobytes())
original_bytes = source.read_bytes()

client = module.VibeRaClient(exe)
client.process.stdin.reconfigure(newline="\n")
passed = False
try:
    call = lambda tool, args=None: client.call(tool, args or {}, allow_failure=True).get("structuredContent", {})
    command = lambda name, **args: call("vibera_command", {"command": name, "args": args})
    call("vibera_project", {"action": "new", "template": "empty", "discardChanges": True})
    command("audio.add", channel=0, text=str(source), length=2, lane=0)
    command("audio.add", channel=0, text=str(source), length=2, lane=1)
    audio = lambda: sorted((c for c in call("vibera_inspect", {"entity": "clips"})["items"] if c["kind"] == "audio"),
                           key=lambda c: c["lane"])
    channel_source = lambda cid: next(c.get("source") for c in call("vibera_inspect", {"entity": "channels"})["items"]
                                      if c["id"] == cid)
    first, second = audio()
    shared, other = first["source"], second["source"]
    check(Path(channel_source(shared)) == source and Path(channel_source(other)) == source,
          "Both clips start on the original file")

    result = command("clip.uniqueSample", target=first["id"])
    check(result.get("completionState") == "completed", "clip.uniqueSample completes: " + str(result.get("error"))[:160])
    first, second = audio()
    copied = Path(channel_source(first["source"]))
    check(first["source"] != shared and copied != source and copied.parent == source.parent and "unique" in copied.name,
          f"The clip uses a new Channel on a copied file ({copied.name})")
    check(copied.is_file() and copied.read_bytes() == original_bytes, "The copy has the original audio")
    check(second["source"] == other and Path(channel_source(other)) == source and source.read_bytes() == original_bytes,
          "The other clip and the original file are untouched")

    call("vibera_command", {"command": "edit.undo"})
    check(audio()[0]["source"] == shared, "Undo returns the clip to the shared Channel")
    check(copied.is_file(), "The copied file stays on disk after Undo (as in FL)")

    files_before = sorted(p.name for p in source.parent.iterdir())
    refused = command("clip.uniqueSample", target=999999)
    check(refused.get("completionState") != "completed", "A target that is not an Audio Clip is refused")
    check(sorted(p.name for p in source.parent.iterdir()) == files_before, "A refusal leaves no copy behind")
    passed = True
finally:
    client.close()
    (output / "verification.json").write_text(json.dumps(dict(passed=passed, checks=checks), indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks), last=checks[-1] if checks else None)))
sys.exit(0 if passed else 1)
