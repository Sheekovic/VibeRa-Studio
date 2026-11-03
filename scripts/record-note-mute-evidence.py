"""Bind the Note mute increment to exact passing inputs; legacy gaps stay deferred."""
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
mcp = read("build/windows/note_mute_mcp/verification.json")
assert mcp["passed"] and mcp["checks"] >= 46 and mcp["exitCode"] == 0 and mcp["applicationSha256"] == sha(app)
assert (root/"build/windows/note_mute_gui/note-mute-gui.txt").read_text().startswith("PASS")
assert (root/"build/windows/gui_acceptance/note-mute-held-take.txt").read_text().startswith("PASS")
assert (root/"build/windows/acceptance/note-mute-core.txt").read_text().startswith("PASS")
live=(root/"build/windows/acceptance/note-mute-live.txt").read_text()
assert len(live.splitlines())==2
for row in live.splitlines():
    assert all(token in row for token in ["overruns=0","silentHeld=0","allocations=0","destructions=0","edit/context/device replacements=0"])
    assert int(re.search(r"loops=(\d+)",row)[1])>=2
native=read(str(Path(args.native_output)/"process.json"))
assert native["passed"] and native["exitCode"]==0 and not native["forcedClose"] and native["applicationSha256"]==sha(app)
assert "PASS owned OS T/P" in (root/args.native_output/"verification.txt").read_text()
log = root/args.source_log
raw = log.read_bytes()
summary = re.search(r"100% tests passed, 0 tests failed out of (\d+)", raw.decode("utf-16" if raw.startswith(b"\xff\xfe") else "utf-8-sig"))
assert summary and int(summary[1]) == len(re.findall(r"^add_test\(", (root/"CMakeLists.txt").read_text(), re.M))
assert log.stat().st_mtime >= (root/app).stat().st_mtime
inputs = {i["path"]: i["sha256"] for i in read("build/build-inputs.json")}
for path in ["scripts/verify-note-mute-mcp.py","scripts/record-note-mute-evidence.py","scripts/verify-note-mute-desktop.py",
             "skills/vibera-studio/SKILL.md","skills/vibera-studio/references/note-mute.md","skills/vibera-studio/scripts/stdio_client.py"]:
    inputs[path]=sha(path)
assert all(sha(path) == digest for path, digest in inputs.items()), "Acceptance inputs changed"
commands=["notes.mute","notes.unmute","piano.toolMute","piano.exportMidi"]
cases=["Actual JUCE T/P, toolbar, swept/revisited mute/unmute, Ctrl selection and right erase",
       "Owned native OS T/P/stroke/Ctrl+Z/Y/S and actual saved source flag; app-only capture",
       "Stable IDs/musical properties/selection, linked Patterns, exact stroke Undo/Redo/cancel/expiry and ordered user macros",
       "Schema17 typed flag, genuine old-file migration, atomic malformed/missing/masquerading flag refusal",
       "Virtual Pattern/Song sustained/rapid/history/loop edits retain Edit/context/device/prepared samples/held MIDI, release muted voice without stuck ownership",
       "Actual Studio held MIDI recording take survives shared UI mute and semantic noteMute lease/unmute/commit in Pattern/Song; Stop remains a separate undoable recording edit; instrumented callback/PCM/WAV/SMF checks",
       "Canonical semantic targets/readback/typed errors, leased additive strokes, real save/reopen, async exportMidi completion/failure and overwrite admission"]
receipt = "docs/mcp/NOTE_MUTE_EVIDENCE.json"
write(receipt, dict(passed=True, applicationSha256=sha(app), sourceInputs=inputs, checks=cases,
                    sourceSuites=f"{summary[1]}/{summary[1]}", mcpChecks=mcp["checks"], nativeInput=args.native_output,
                    listening=False, physicalMidi=False,
                    scope="Source-preserving Piano Roll note mute; exact FL runtime pointer/mixed-selection policy and full PR-03 remain unqualified"))
verification = dict(status="passed", receipt=receipt, inputs=inputs, cases=cases)
inventory = read("docs/mcp/SURFACE_INVENTORY.json")
inventory["operations"]=[o for o in inventory["operations"] if o["id"]!="piano.noteMute"]
inventory["operations"].append(dict(id="piano.noteMute",name="Piano Roll source-preserving Note mute and score export",
    source="src/Editors.h",canonical="Controller notes.mute/unmute and Studio shared async score-export service",
    mcp="vibera_command",additionalInterfaces=["vibera_gesture","vibera_inspect","vibera_project"],commands=commands,
    readback=["vibera_inspect:notes.muted","vibera_state:piano.tool/pendingAudioUpdate","vibera_project:job"],
    skill="skills/vibera-studio/references/note-mute.md",verification=verification))
write("docs/mcp/SURFACE_INVENTORY.json", inventory)
spec = importlib.util.spec_from_file_location("coverage_gate", root/"scripts/mcp-coverage.py")
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)
reviewed = read("docs/mcp/REVIEWED_SURFACES.json")
for entry in gate.inventory()["customSurfaceCandidates"]:
    source, line, _ = entry["surface"].split(":")
    text = (root/source).read_text(encoding="utf-8-sig").splitlines()[int(line)-1]
    if source == "src/Editors.h" and "pianoMute.onClick" in text:
        reviewed[entry["surface"]] = dict(sourceSha256=entry["sourceSha256"], operations=commands, mcp="vibera_command",
            additionalInterfaces=["vibera_project", "vibera_gesture"], readback=["vibera_inspect:notes.muted", "vibera_state:piano.tool"],
            skill="skills/vibera-studio/references/note-mute.md", verification=verification)
write("docs/mcp/REVIEWED_SURFACES.json", reviewed)
print("Recorded scoped Piano Roll note-mute evidence; full inherited MCP gate remains incomplete")
