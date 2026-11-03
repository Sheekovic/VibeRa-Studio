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
parser.add_argument("--native-output", default="build/groove-native-third")
args = parser.parse_args()
read = lambda name: json.loads((root / name).read_text(encoding="utf-8-sig"))
write = lambda name, data: (root / name).write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
sha = lambda name: hashlib.sha256((root / name).read_bytes()).hexdigest()
app = "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe"
mcp = read("build/windows/groove_mcp_acceptance/verification.json")
assert mcp["passed"] and mcp["checks"] >= 38 and mcp["exitCode"] == 0 and mcp["applicationSha256"] == sha(app)
for name in ["gui-smoke.txt", "quantizer-gui.txt", "groove-gui.txt"]:
    assert (root / "build/windows/gui_acceptance" / name).read_text(encoding="utf-8").startswith("PASS")
log = root / args.source_log
data = log.read_bytes()
summary = re.search(r"100% tests passed, 0 tests failed out of (\d+)", data.decode("utf-16" if data.startswith(b"\xff\xfe") else "utf-8-sig"))
assert summary and int(summary[1]) == len(re.findall(r"^add_test\(", (root / "CMakeLists.txt").read_text(encoding="utf-8"), re.M))
assert log.stat().st_mtime >= (root / app).stat().st_mtime
inputs = {x["path"]: x["sha256"] for x in read("build/build-inputs.json")}
for name in ["scripts/verify-groove-mcp.py", "scripts/verify-quantizer-mcp.py", "scripts/record-groove-evidence.py", "scripts/verify-pattern-desktop.ps1", "skills/vibera-studio/SKILL.md",
             "skills/vibera-studio/references/grooves.md", "skills/vibera-studio/references/quantizer.md", "skills/vibera-studio/scripts/stdio_client.py"]:
    inputs[name] = sha(name)
assert all(sha(name) == digest for name, digest in inputs.items()), "Acceptance inputs changed"
native=(root/args.native_output/"native-quantizer-verification.txt").read_text(encoding="utf-8-sig")
assert sha(app).upper() in native and "PASS native Capture" in native and "exitCode=0; forcedClose=False" in native
cases = ["strict original versioned template capture/presets/validate/save/load/preview and authoritative typed schema",
         "selected/current part capture, stable chord tie policy and explicit loop/phase constraints",
         "real component capture/preset/velocity/rest/reset/cancel and UI file adapters, restored source selection",
         "actual owned native mouse/keyboard controls and Windows Save/Load dialogs; filename WM_SETTEXT, clipboard untouched",
         "real repeated WAV onsets1/7501/12001/19501/24001/31501 versus straight1/6001/12001/18001/24001/30001",
         "zero rest removal verified in actual PCM; unaffected right Channel byte-identical",
         "baseline notes deleted by rests restore exact IDs/order, one Undo/Redo, commit/cancel and real lease expiry",
         "shared recording/project-I/O admission and version2 groove-macro persistence with composite survivor selection",
         "bounded UTF8-BOM file decode, malformed/oversized files and explicit overwrite refusal preserve data",
         "source score save/reopen/export remains independent of moved preset files; terminal job verification",
         "Pattern/Song held same-pitch MIDI, four modes/levels/rests, retained graph/device/sample identities and callback timing"]

receipt_path = "docs/mcp/GROOVE_EVIDENCE.json"
write(receipt_path, {"passed": True, "applicationSha256": sha(app), "sourceInputs": inputs, "checks": cases,
                     "sourceSuites": f"{summary[1]}/{summary[1]}", "mcpChecks": mcp["checks"],
                     "nativeInput": "Owned OS mouse/keyboard plus targeted filename WM_SETTEXT; not physical human input", "physicalMidi": False, "listening": False,
                     "scope": "Original reusable groove timing/velocity/rest workflow; other note properties/FSC/exact installed numerics and listening/hardware remain open"})
verification = {"status": "passed", "receipt": receipt_path, "inputs": inputs, "cases": cases}
inventory = read("docs/mcp/SURFACE_INVENTORY.json")
inventory["operations"] = [x for x in inventory["operations"] if x["id"] != "piano.grooves"]
commands = ["notes.quantize", "notes.quickQuantize", "notes.quantizeStarts"] + ["quantizer.groove."+action for action in ["capture","presets","validate","load","save","preview"]]
inventory["operations"].append({"id": "piano.grooves", "name": "Reusable Quantizer templates, capture/file I/O, level/rest preview and guides",
    "source": "src/StudioQuantizer.cpp", "canonical": "Controller note transform and shared owned/leased baseline gestures",
    "mcp": "vibera_command", "additionalInterfaces": ["vibera_gesture", "vibera_groove"], "commands": commands,
    "readback": ["vibera_groove:preview", "vibera_state:quantizerGuide", "vibera_state:quantizerPreview", "vibera_inspect:notes", "vibera_gesture:state", "vibera_state:audio"],
    "skill": "skills/vibera-studio/references/grooves.md", "verification": verification})
write("docs/mcp/SURFACE_INVENTORY.json", inventory)
# Review dedicated Quantizer/groove callbacks with actual component, semantic and
# owned native file-picker evidence. This is not human physical input/listening.
# Shared legacy handlers remain unreviewed.
import importlib.util
spec = importlib.util.spec_from_file_location("coverage_gate", root / "scripts/mcp-coverage.py")
gate = importlib.util.module_from_spec(spec); spec.loader.exec_module(gate)
reviewed = read("docs/mcp/REVIEWED_SURFACES.json")
for entry in gate.inventory()["customSurfaceCandidates"]:
    source, line, _ = entry["surface"].split(":")
    text = (root/source).read_text(encoding="utf-8-sig").splitlines()[int(line)-1]
    if source in {"src/StudioQuantizer.cpp"}:
        reviewed[entry["surface"]] = {"sourceSha256": entry["sourceSha256"], "operations": commands,
            "mcp": "vibera_command", "additionalInterfaces": ["vibera_gesture", "vibera_groove"],
            "readback": ["vibera_groove:preview", "vibera_state:quantizerGuide", "vibera_state:quantizerPreview", "vibera_inspect:notes", "vibera_gesture:state"],
            "skill": "skills/vibera-studio/references/grooves.md", "verification": verification}

write("docs/mcp/REVIEWED_SURFACES.json", reviewed)
print("Recorded dedicated reusable-groove evidence; inherited full MCP coverage remains incomplete")
