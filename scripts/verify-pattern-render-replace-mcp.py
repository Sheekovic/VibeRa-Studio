"""Render and replace through MCP (isolated --mcp-test instance, offline render).

vibera_project export with pattern, placeClip and replace renders the Pattern once and, when the job
completes, places the audio clip at every Playlist placement of that Pattern and mutes those Pattern
clips. A Pattern that is not placed is refused. Without replace only one audio clip is placed and nothing
is muted (the existing Render as audio clip). Not a listening test.

usage: python scripts/verify-pattern-render-replace-mcp.py [executable] [output folder]
"""
import importlib.util
import json
from pathlib import Path
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe").resolve()
base = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else ROOT / "build/module-qualification/pattern-render-replace"
output = base / ("attempt-" + uuid.uuid4().hex)
output.mkdir(parents=True)
spec = importlib.util.spec_from_file_location("client", ROOT / "skills/vibera-studio/scripts/stdio_client.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
checks = []
rendered = []


def check(ok, label):
    if not ok:
        raise AssertionError(label)
    checks.append(label)


client = module.VibeRaClient(exe)
client.process.stdin.reconfigure(newline="\n")
passed = False
try:
    raw = lambda tool, args: client.call(tool, args, allow_failure=True)
    call = lambda tool, args=None: raw(tool, args or {}).get("structuredContent", {})
    command = lambda name, **args: call("vibera_command", {"command": name, "args": args})
    clips = lambda kind: sorted((c for c in call("vibera_inspect", {"entity": "clips", "limit": 500})["items"] if c["kind"] == kind),
                                key=lambda c: c["beat"])
    project = output / "render-replace.vibra"
    call("vibera_project", {"action": "new", "template": "demo", "discardChanges": True})
    saved = call("vibera_project", {"action": "save", "path": str(project), "overwrite": True})
    for _ in range(100):  # The rendered WAV goes to the project's Rendered folder: save first.
        if call("vibera_project", {"action": "job", "operation": saved.get("operation", "")}).get("completionState") != "pending":
            break
        time.sleep(.1)
    for clip in clips("pattern"):
        command("clip.delete", ids=[clip["id"]])
    state = call("vibera_state")
    pattern = state["selectedPattern"]

    def render(**extra):
        started = raw("vibera_project", dict(action="export", pattern=pattern, placeClip=True, **extra))
        if started.get("isError"):
            return started, None
        operation = started["structuredContent"]["operation"]
        job = {}
        for _ in range(600):
            job = call("vibera_project", {"action": "job", "operation": operation})
            if job.get("completionState") != "pending":
                break
            time.sleep(.1)
        if job.get("path"):
            rendered.append(Path(job["path"]))
        return started, job

    refused, _ = render(replace=True)
    check(refused.get("isError") and "noClips" in json.dumps(refused), "Render and replace refuses a Pattern that is not placed")

    command("clip.add", pattern=pattern, beat=0, lane=0, length=4)
    command("clip.add", pattern=pattern, beat=8, lane=0, length=4)
    command("clip.add", pattern=pattern, beat=16, lane=1, length=4)
    check([c["beat"] for c in clips("pattern")] == [0, 8, 16] and clips("audio") == [], "Three placements of the Pattern, no audio clips")

    _, job = render(replace=True)
    check(job is not None and job.get("completionState") == "completed", f"The render job completes ({job})")
    for _ in range(150):  # Audio clips are imported after the job (asynchronous, own Undo steps).
        if len(clips("audio")) == 3:
            break
        time.sleep(.1)
    audio = clips("audio")
    check([c["beat"] for c in audio] == [0, 8, 16], f"An audio clip sits at every placement of the Pattern ({[c['beat'] for c in audio]})")
    check(len({c["lane"] for c in audio}) == 1 and audio[0]["lane"] > 1, "They share a new track below the Pattern clips")
    check(all(c["muted"] for c in clips("pattern")), "The Pattern clips are muted, not deleted")
    check(all(not c["muted"] for c in audio), "The audio clips play")
    passed = True
finally:
    client.close()
    for wav in rendered:  # The test's own renders only.
        if wav.is_file() and output in wav.parents:
            wav.unlink()
    (output / "verification.json").write_text(json.dumps(dict(passed=passed, checks=checks), indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks), last=checks[-1] if checks else None)))
sys.exit(0 if passed else 1)
