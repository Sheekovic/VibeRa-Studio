"""Record only verified Normalize scope, preserving unrelated inherited MCP gaps."""
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


def write(path, data):
    (root/path).write_text(json.dumps(data, indent=2)+"\n", encoding="utf-8")


app = "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe"
mcp = read("build/windows/normalize_mcp_acceptance/verification.json")
assert mcp["passed"] and mcp["checks"] >= 38 and mcp["exitCode"] == 0 and mcp["applicationSha256"] == sha(app)
assert (root/"build/windows/acceptance/normalize-core.txt").read_text().startswith("PASS")
assert (root/"build/windows/sampler_gestures_gui/normalize-mcp-gui.txt").read_text().startswith("PASS")
live = (root/"build/windows/acceptance/normalize-live.txt").read_text()
rows = live.splitlines()
assert len(rows) == 2
for row in rows:
    assert all(token in row for token in ["overruns=0", "silentLeft=0", "silentRight=0", "allocations=0",
                                          "destructions=0", "graphReplacements=0", "deviceStarts=0", "deviceStops=0"])
    assert int(re.search(r"loops=(\d+)", row)[1]) >= 2
native_path = str(Path(args.native_output)/"native-sampler-voices-verification.txt")
native = (root/native_path).read_text(encoding="utf-8-sig")
assert sha(app).upper() in native and "PASS native Normalize checkbox" in native and "exitCode=0; forcedClose=False" in native
log = root/args.source_log
raw = log.read_bytes()
summary = re.search(r"100% tests passed, 0 tests failed out of (\d+)", raw.decode("utf-16" if raw.startswith(b"\xff\xfe") else "utf-8-sig"))
assert summary and int(summary[1]) == len(re.findall(r"^add_test\(", (root/"CMakeLists.txt").read_text(), re.M))
assert log.stat().st_mtime >= (root/app).stat().st_mtime
inputs = {i["path"]: i["sha256"] for i in read("build/build-inputs.json")}
for path in ["scripts/verify-normalize-mcp.py", "scripts/record-normalize-evidence.py", "scripts/verify-pattern-desktop.ps1",
             "skills/vibera-studio/SKILL.md", "skills/vibera-studio/references/normalize.md", "skills/vibera-studio/scripts/stdio_client.py"]:
    inputs[path] = sha(path)
assert all(sha(path) == digest for path, digest in inputs.items()), "Acceptance inputs changed"
commands = ["channel.sampler.normalize"]
cases = ["Actual Channel checkbox, fixed window target despite other MIDI selection, correct requested waveform and one exact Undo",
         "Owned native Normalize click/Save/Undo/Redo, current app identity and graceful exit; not human physical input",
         "Whole-source stereo peak/polarity/balance, unchanged PCM, trim policy, smooth held transitions and silence/extreme finite values",
         "Real multi-command user macro, canonical context/order and one exact Undo",
         "Actual Pattern/Song repeated edits/history/loops with held PCM and instrumented callback heap/deadline/lifetime checks",
         "Actual held MIDI take survives both UI and MCP setting changes without retarget/retrigger/cancel",
         "Real schema15 file migration/round trip and malformed/old-schema masquerade atomic refusal",
         "Real stdio typed discovery/invalid input/source identity, virtual controller mapping, project/exact WAV comparison and actual async file failure"]
receipt = "docs/mcp/NORMALIZE_EVIDENCE.json"
write(receipt, dict(passed=True, applicationSha256=sha(app), sourceInputs=inputs, checks=cases,
                    sourceSuites=f"{summary[1]}/{summary[1]}", mcpChecks=mcp["checks"], nativeInput=native_path,
                    listening=False, physicalMidi=False,
                    scope="Whole-source sample/original-instrument peak Normalize; Audio Clip/VST3 interiors and exact FL trim ordering unsupported"))
verification = dict(status="passed", receipt=receipt, inputs=inputs, cases=cases)
inventory = read("docs/mcp/SURFACE_INVENTORY.json")
inventory["operations"] = [o for o in inventory["operations"] if o["id"] != "sampler.normalize"]
inventory["operations"].append(dict(id="sampler.normalize", name="Non-destructive sampler Normalize",
    source="src/InstrumentWindow.h", canonical="Shared Controller command/history, prepared immutable source peak and fixed-storage voice gain",
    mcp="vibera_command", additionalInterfaces=["vibera_controller"], commands=commands,
    readback=["vibera_state:channels.sampler.normalize", "vibera_state:channels.sampler.sourcePeak/normalizationGain", "vibera_state:pendingAudioUpdate"],
    skill="skills/vibera-studio/references/normalize.md", verification=verification))
write("docs/mcp/SURFACE_INVENTORY.json", inventory)
spec = importlib.util.spec_from_file_location("coverage_gate", root/"scripts/mcp-coverage.py")
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)
reviewed = read("docs/mcp/REVIEWED_SURFACES.json")
for entry in gate.inventory()["customSurfaceCandidates"]:
    source, line, _ = entry["surface"].split(":")
    text = (root/source).read_text(encoding="utf-8-sig").splitlines()[int(line)-1]
    if source == "src/InstrumentWindow.h" and "normalize.onClick" in text:
        reviewed[entry["surface"]] = dict(sourceSha256=entry["sourceSha256"], operations=commands, mcp="vibera_command",
            additionalInterfaces=["vibera_controller"], readback=["vibera_state:channels.sampler"],
            skill="skills/vibera-studio/references/normalize.md", verification=verification)
write("docs/mcp/REVIEWED_SURFACES.json", reviewed)
print("Recorded scoped Normalize evidence; full inherited MCP gate remains incomplete")
