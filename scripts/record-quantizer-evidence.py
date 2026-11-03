"""Bind only the Quantizer increment to exact tested inputs.

This qualifies only the new increment; inherited custom handlers and gaps remain.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--source-log", required=True)
args = parser.parse_args()
read = lambda name: json.loads((root / name).read_text(encoding="utf-8-sig"))
write = lambda name, data: (root / name).write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
sha = lambda name: hashlib.sha256((root / name).read_bytes()).hexdigest()
app = "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe"
mcp = read("build/windows/quantizer_mcp_acceptance/verification.json")
assert mcp["passed"] and mcp["checks"] >= 39 and mcp["exitCode"] == 0 and mcp["applicationSha256"] == sha(app)
for name in ["gui-smoke.txt", "quantizer-gui.txt"]:
    assert (root / "build/windows/gui_acceptance" / name).read_text(encoding="utf-8").startswith("PASS")
log = root / args.source_log
data = log.read_bytes()
summary = re.search(r"100% tests passed, 0 tests failed out of (\d+)", data.decode("utf-16" if data.startswith(b"\xff\xfe") else "utf-8-sig"))
assert summary and int(summary[1]) == len(re.findall(r"^add_test\(", (root / "CMakeLists.txt").read_text(encoding="utf-8"), re.M))
assert log.stat().st_mtime >= (root / app).stat().st_mtime
inputs = {x["path"]: x["sha256"] for x in read("build/build-inputs.json")}
for name in ["scripts/verify-quantizer-mcp.py", "scripts/record-quantizer-evidence.py", "skills/vibera-studio/SKILL.md",
             "skills/vibera-studio/references/quantizer.md", "skills/vibera-studio/scripts/stdio_client.py"]:
    inputs[name] = sha(name)
assert all(sha(name) == digest for name, digest in inputs.items()), "Acceptance inputs changed"
cases = ["three canonical grid timing commands with strict ranges/modes/targets and exact history",
         "baseline preview slider strengths, sensitivity and four duration/end modes; Reset/Accept/Cancel",
         "active-Channel empty selection, explicit ghosts, stable IDs/pitch/velocity and linked source Patterns",
         "real component Slider/ComboBox/button callbacks, disabled duration in leave modes and actual dialog render",
         "Pattern/Song40 edits/history417 virtual callbacks, held same-pitch MIDI and retained graph/device/sample lifetime",
         "real stdio39 workflow checks with invalid atomic errors, one Undo/Redo, cancellation and actual lease expiry",
         "actual48k pulse onsets1921/15121 to1/18001 and byte-identical untouched Channel PCM",
         "actual project save/reopen/WAV and terminal failed export; no listening/hardware inference"]

receipt_path = "docs/mcp/QUANTIZER_EVIDENCE.json"
write(receipt_path, {"passed": True, "applicationSha256": sha(app), "sourceInputs": inputs, "checks": cases,
                     "sourceSuites": f"{summary[1]}/{summary[1]}", "mcpChecks": mcp["checks"],
                     "nativeInput": False, "physicalMidi": False, "listening": False,
                     "scope": "Grid Quantizer only; groove/property templates and exact installed numerical/default parity remain open; native OS input separate"})
verification = {"status": "passed", "receipt": receipt_path, "inputs": inputs, "cases": cases}
inventory = read("docs/mcp/SURFACE_INVENTORY.json")
inventory["operations"] = [x for x in inventory["operations"] if x["id"] != "piano.quantizer"]
commands = ["notes.quantize", "notes.quickQuantize", "notes.quantizeStarts"]
inventory["operations"].append({"id": "piano.quantizer", "name": "Grid Quantizer preview, four modes, strengths and distinct quick actions",
    "source": "src/StudioQuantizer.cpp", "canonical": "Controller note transform and shared owned/leased baseline gestures",
    "mcp": "vibera_command", "additionalInterfaces": ["vibera_gesture"], "commands": commands,
    "readback": ["vibera_state:quantizerPreview", "vibera_inspect:notes", "vibera_gesture:state", "vibera_state:audio"],
    "skill": "skills/vibera-studio/references/quantizer.md", "verification": verification})
write("docs/mcp/SURFACE_INVENTORY.json", inventory)
# Review only dedicated Quantizer control/service callbacks. These bindings combine
# actual Slider/menu cases and canonical typed workflows; native modal submission
# is explicitly not claimed. Shared legacy handlers remain unreviewed.
import importlib.util
spec = importlib.util.spec_from_file_location("coverage_gate", root / "scripts/mcp-coverage.py")
gate = importlib.util.module_from_spec(spec); spec.loader.exec_module(gate)
reviewed = read("docs/mcp/REVIEWED_SURFACES.json")
for entry in gate.inventory()["customSurfaceCandidates"]:
    source, line, _ = entry["surface"].split(":")
    text = (root/source).read_text(encoding="utf-8-sig").splitlines()[int(line)-1]
    if source in {"src/StudioQuantizer.cpp"}:
        reviewed[entry["surface"]] = {"sourceSha256": entry["sourceSha256"], "operations": commands,
            "mcp": "vibera_command", "additionalInterfaces": ["vibera_gesture"],
            "readback": ["vibera_state:quantizerPreview", "vibera_inspect:notes", "vibera_gesture:state"],
            "skill": "skills/vibera-studio/references/quantizer.md", "verification": verification}

write("docs/mcp/REVIEWED_SURFACES.json", reviewed)
print("Recorded dedicated Quantizer evidence; inherited full MCP coverage remains incomplete")
