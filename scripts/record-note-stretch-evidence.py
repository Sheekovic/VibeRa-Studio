"""Bind the selection stretch increment to exact passing inputs; legacy gaps stay deferred."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--source-log", required=True)
parser.add_argument("--native-output", required=True)
args = parser.parse_args()
read = lambda p: json.loads((root/p).read_text(encoding="utf-8-sig"))
sha = lambda p: hashlib.sha256((root/p).read_bytes()).hexdigest()


def write(path, value):
    (root/path).write_text(json.dumps(value, indent=2)+"\n", encoding="utf-8")


app = "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe"
mcp = read("build/windows/note_stretch_mcp/verification.json")
subprocess.run([sys.executable,str(root/"scripts/verify-note-stretch-batch.py"),str(root/app),str(root/"build/note-stretch-batch")],check=True)
batch=read("build/note-stretch-batch/verification.json")
assert batch["passed"] and batch["checks"]>=5 and batch["exitCode"]==0 and batch["applicationSha256"]==sha(app)

assert mcp["passed"] and mcp["checks"] >= 38 and mcp["exitCode"] == 0 and mcp["applicationSha256"] == sha(app)
assert (root/"build/windows/note_stretch_gui/note-stretch-gui.txt").read_text().startswith("PASS")
assert (root/"build/windows/gui_acceptance/note-stretch-held-take.txt").read_text().startswith("PASS")
assert (root/"build/windows/acceptance/note-stretch-core.txt").read_text().startswith("PASS")
live=(root/"build/windows/acceptance/note-stretch-live.txt").read_text()
assert len(live.splitlines())==2
for row in live.splitlines():
    assert all(token in row for token in ["overruns=0","silentHeld=0","allocations=0","destructions=0","edit/context/device replacements=0"])
    assert int(re.search(r"loops=(\d+)",row)[1])>=2
native=read(str(Path(args.native_output)/"process.json"))
assert native["passed"] and native["exitCode"]==0 and not native["forcedClose"] and native["applicationSha256"]==sha(app)
assert "PASS owned native selected handle" in (root/args.native_output/"verification.txt").read_text()
log = root/args.source_log
raw = log.read_bytes()
summary = re.search(r"100% tests passed, 0 tests failed out of (\d+)", raw.decode("utf-16" if raw.startswith(b"\xff\xfe") else "utf-8-sig"))
assert summary and int(summary[1]) == len(re.findall(r"^add_test\(", (root/"CMakeLists.txt").read_text(), re.M))
assert log.stat().st_mtime >= max((root/app).stat().st_mtime, (root/"tests/NoteStretchLiveAcceptance.h").stat().st_mtime)
inputs = {i["path"]: i["sha256"] for i in read("build/build-inputs.json")}
for path in ["scripts/verify-note-stretch-mcp.py","scripts/record-note-stretch-evidence.py","scripts/verify-note-stretch-desktop.py","scripts/verify-note-stretch-batch.py",
             "skills/vibera-studio/SKILL.md","skills/vibera-studio/references/note-stretch.md","skills/vibera-studio/scripts/stdio_client.py"]:
    inputs[path]=sha(path)
assert all(sha(path) == digest for path, digest in inputs.items()), "Acceptance inputs changed"
commands=["notes.stretch"]
cases=["Actual JUCE selected stretch handle/drag/float Alt coordinates and visible nondestructive proposed score",
       "Owned native handle/release, RightShift+Alt edge25% ratios, held-stretch Escape, Ctrl+Z/Y/S and actual saved source; app-only captures",
       "Canonical anchor-relative onsets/durations, stable IDs/order/pitch/velocity/mute/linked source and exact one Undo/Redo",
       "Ratio1 preserves step flags/manual short loops without history; source-dependent minimum/maximum, ghosts, Pattern growth and unchanged placement extents",
       "Strict numeric macro builder and ratio dialog, current-context earliest anchor, shared macro order/history and real project/WAV/SMF source persistence",
       "Shared local/remote baseline-relative preview/lease/pagination, no source/history/audio publication before commit, invalid-update retention, ownership/context/tool/hide/Escape/expiry cancellation",
       "Pattern/Song sustained/rapid/history/loops retain prepared samples/Edit/context/device/held input and release edited/deleted score without stuck notes",
       "Actual Studio local and semantic stretch preview/commit preserve held MIDI take and recipient; Stop has separate exact Undo/Redo",
       "Actual semantic discovery/targets/readback/structured errors and real save/reopen, changed rendered music/unaffected right PCM, async MIDI completion/failure"]
receipt = "docs/mcp/NOTE_STRETCH_EVIDENCE.json"
write(receipt, dict(passed=True, applicationSha256=sha(app), sourceInputs=inputs, checks=cases,
                    sourceSuites=f"{summary[1]}/{summary[1]}", mcpChecks=mcp["checks"], batchChecks=batch["checks"], nativeInput=args.native_output,
                    listening=False, physicalMidi=False,
                    scope="Selection note-time stretching; original anchor/minimum/default-snap policies, exact installed FL parity and full PR-03 remain unqualified"))
verification = dict(status="passed", receipt=receipt, inputs=inputs, cases=cases)
inventory = read("docs/mcp/SURFACE_INVENTORY.json")
inventory["operations"]=[o for o in inventory["operations"] if o["id"]!="piano.noteStretch"]
inventory["operations"].append(dict(id="piano.noteStretch",name="Piano Roll selection time stretching",
    source="src/Editors.h",canonical="Controller notes.stretch and shared Studio baseline-relative stretch preview/commit/cancel service",
    mcp="vibera_command",additionalInterfaces=["vibera_stretch","vibera_batch","vibera_inspect","vibera_project"],commands=commands,
    readback=["vibera_inspect:notes","vibera_state:piano.stretchPreview/stretchHandleBounds/pendingAudioUpdate","vibera_stretch:state","vibera_project:job"],
    skill="skills/vibera-studio/references/note-stretch.md",verification=verification))
write("docs/mcp/SURFACE_INVENTORY.json", inventory)
print("Recorded scoped selection stretching evidence; full inherited MCP gate remains incomplete")
