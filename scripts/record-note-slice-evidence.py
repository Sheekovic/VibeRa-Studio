"""Bind the manual Slice increment to exact passing inputs; legacy gaps stay deferred."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re

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
mcp = read("build/windows/note_slice_mcp/verification.json")
assert mcp["passed"] and mcp["checks"] >= 43 and mcp["exitCode"] == 0 and mcp["applicationSha256"] == sha(app)
assert (root/"build/windows/note_slice_gui/note-slice-gui.txt").read_text().startswith("PASS")
assert (root/"build/windows/gui_acceptance/note-slice-held-take.txt").read_text().startswith("PASS")
assert (root/"build/windows/acceptance/note-slice-core.txt").read_text().startswith("PASS")
live=(root/"build/windows/acceptance/note-slice-live.txt").read_text()
assert len(live.splitlines())==2
for row in live.splitlines():
    assert all(token in row for token in ["overruns=0","silentHeld=0","allocations=0","destructions=0","edit/context/device replacements=0"])
    assert int(re.search(r"loops=(\d+)",row)[1])>=2
native=read(str(Path(args.native_output)/"process.json"))
assert native["passed"] and native["exitCode"]==0 and not native["forcedClose"] and native["applicationSha256"]==sha(app)
assert "PASS owned OS C/P" in (root/args.native_output/"verification.txt").read_text()
log = root/args.source_log
raw = log.read_bytes()
summary = re.search(r"100% tests passed, 0 tests failed out of (\d+)", raw.decode("utf-16" if raw.startswith(b"\xff\xfe") else "utf-8-sig"))
assert summary and int(summary[1]) == len(re.findall(r"^add_test\(", (root/"CMakeLists.txt").read_text(), re.M))
assert log.stat().st_mtime >= (root/app).stat().st_mtime
inputs = {i["path"]: i["sha256"] for i in read("build/build-inputs.json")}
for path in ["scripts/verify-note-slice-mcp.py","scripts/record-note-slice-evidence.py","scripts/verify-note-slice-desktop.py",
             "skills/vibera-studio/SKILL.md","skills/vibera-studio/references/note-slice.md","skills/vibera-studio/scripts/stdio_client.py"]:
    inputs[path]=sha(path)
assert all(sha(path) == digest for path, digest in inputs.items()), "Acceptance inputs changed"
commands=["notes.slice","piano.toolSlice"]
cases=["Actual JUCE C/P, toolbar, line preview/release commit, Shift+Alt point cut, Ctrl selection and independent right erase",
       "Owned native OS C/P/line-release/Ctrl+Z/Y/S, held-cut Escape and actual saved split flags; app-only captures",
       "Stable retained left IDs/selection and fresh right halves, pitch/velocity/mute/linked Pattern and exact one Undo/Redo",
       "Explicit original row-centred/corner/minimum policy, snapped/reversed/horizontal/ghost/edge/subminimum cuts and atomic note-capacity errors",
       "Profile3 exact line macro recording/builder, strict finite full numeric input, source-preserving move of fine notes and genuine older-profile migration",
       "Non-mutating shared local/remote preview/lease/readback, source anchor retained while scrolling, context/hide/tool/expiry cancellation, ownership and invalid-update preservation",
       "Virtual Pattern/Song sustained/rapid/history/loop edits retain Edit/context/device/prepared samples/held MIDI, edited/deleted halves release without stuck notes",
       "Actual Studio held MIDI recording take survives local and semantic Slice preview/commit in Pattern/Song; Stop has its own exact Undo/Redo",
       "Actual canonical semantic targets/schema/errors, one-history Slice+move batch, save/reopen and real WAV/async SMF completion/failure"]
receipt = "docs/mcp/NOTE_SLICE_EVIDENCE.json"
write(receipt, dict(passed=True, applicationSha256=sha(app), sourceInputs=inputs, checks=cases,
                    sourceSuites=f"{summary[1]}/{summary[1]}", mcpChecks=mcp["checks"], nativeInput=args.native_output,
                    listening=False, physicalMidi=False,
                    scope="Manual Piano Roll note slicing; original minimum/intersection/selection policies, exact FL runtime parity and full PR-03 remain unqualified"))
verification = dict(status="passed", receipt=receipt, inputs=inputs, cases=cases)
inventory = read("docs/mcp/SURFACE_INVENTORY.json")
inventory["operations"]=[o for o in inventory["operations"] if o["id"]!="piano.noteSlice"]
inventory["operations"].append(dict(id="piano.noteSlice",name="Piano Roll manual score slicing",
    source="src/Editors.h",canonical="Controller notes.slice and shared Studio nondestructive preview/commit/cancel service",
    mcp="vibera_command",additionalInterfaces=["vibera_slice","vibera_batch","vibera_inspect","vibera_project"],commands=commands,
    readback=["vibera_inspect:notes","vibera_state:piano.tool/slicePreview/pendingAudioUpdate","vibera_slice:state","vibera_project:job"],
    skill="skills/vibera-studio/references/note-slice.md",verification=verification))
write("docs/mcp/SURFACE_INVENTORY.json", inventory)
spec = importlib.util.spec_from_file_location("coverage_gate", root/"scripts/mcp-coverage.py")
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)
reviewed = read("docs/mcp/REVIEWED_SURFACES.json")
for entry in gate.inventory()["customSurfaceCandidates"]:
    source, line, _ = entry["surface"].split(":")
    text = (root/source).read_text(encoding="utf-8-sig").splitlines()[int(line)-1]
    if source == "src/Editors.h" and "pianoSlice.onClick" in text:
        reviewed[entry["surface"]] = dict(sourceSha256=entry["sourceSha256"], operations=commands, mcp="vibera_command",
            additionalInterfaces=["vibera_slice", "vibera_project"], readback=["vibera_inspect:notes", "vibera_slice:state", "vibera_state:piano.tool"],
            skill="skills/vibera-studio/references/note-slice.md", verification=verification)
write("docs/mcp/REVIEWED_SURFACES.json", reviewed)
print("Recorded scoped Piano Roll note-slice evidence; full inherited MCP gate remains incomplete")
