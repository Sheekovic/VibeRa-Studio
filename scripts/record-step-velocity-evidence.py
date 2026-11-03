"""Bind only the velocity Graph Editor increment to exact tested inputs.

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
mcp = read("build/windows/step_velocity_mcp_acceptance/verification.json")
assert mcp["passed"] and mcp["checks"] >= 42 and mcp["exitCode"] == 0 and mcp["applicationSha256"] == sha(app)
for name in ["gui-smoke.txt", "step-velocity-gui.txt", "step-velocity-held-take.txt"]:
    assert (root / "build/windows/gui_acceptance" / name).read_text(encoding="utf-8").startswith("PASS")
log = root / args.source_log
data = log.read_bytes()
summary = re.search(r"100% tests passed, 0 tests failed out of (\d+)", data.decode("utf-16" if data.startswith(b"\xff\xfe") else "utf-8-sig"))
assert summary and int(summary[1]) == len(re.findall(r"^add_test\(", (root / "CMakeLists.txt").read_text(encoding="utf-8"), re.M))
assert log.stat().st_mtime >= (root / app).stat().st_mtime
inputs = {x["path"]: x["sha256"] for x in read("build/build-inputs.json")}
for name in ["scripts/verify-step-velocity-mcp.py", "scripts/record-step-velocity-evidence.py", "skills/vibera-studio/SKILL.md",
             "skills/vibera-studio/references/velocity-graph.md", "skills/vibera-studio/scripts/stdio_client.py"]:
    inputs[name] = sha(name)
assert all(sha(name) == digest for name, digest in inputs.items()), "Acceptance inputs changed"
cases = ["canonical step-only set/reset/scale/ramp and atomic validation preserve other notes/Patterns",
         "gesture-start proportional/zero-anchor scale and shrinking/reverse ramps; exact Undo/Redo/cancel",
         "actual menu/button/shortcut and graph paint/scale/ramp/reset/Escape/scroll/hide/compact-layout handlers",
         "captured step drag and file drop over graph cannot edit an obscured Channel",
         "Pattern/Song UI and narrow semantic leases retain held MIDI take/target/graph; separate take Undo",
         "current-Channel parameterized macro and schema12 round trip",
         "real stdio typed target/domain errors, recording guard, cancellation and timer expiry",
         "actual linked Song save/reopen/24bit WAV; MIDI25/51 PCM ratio, exact unaffected right and zero silence",
         "actual asynchronous export failure and ownership readback; no mouse simulation for semantic coverage"]
receipt_path = "docs/mcp/STEP_VELOCITY_EVIDENCE.json"
write(receipt_path, {"passed": True, "applicationSha256": sha(app), "sourceInputs": inputs, "checks": cases,
                     "sourceSuites": f"{summary[1]}/{summary[1]}", "mcpChecks": mcp["checks"],
                     "nativeInput": False, "physicalMidi": False, "listening": False,
                     "scope": "Velocity-only Graph Editor; native/provider receipts, other properties and full-family limits separate"})
verification = {"status": "passed", "receipt": receipt_path, "inputs": inputs, "cases": cases}
inventory = read("docs/mcp/SURFACE_INVENTORY.json")
inventory["operations"] = [x for x in inventory["operations"] if x["id"] != "rack.velocityGraph"]
inventory["operations"].append({"id": "rack.velocityGraph", "name": "Velocity Graph Editor navigation, gestures and musical dynamics",
    "source": "src/StudioRackVelocity.cpp", "canonical": "Shared Rack view service, Controller steps.velocity and local/leased gesture history",
    "mcp": "vibera_rack", "additionalInterfaces": ["vibera_command", "vibera_gesture"],
    "commands": ["rack.graph", "steps.velocity"],
    "readback": ["vibera_rack:state.graph", "vibera_inspect:notes", "vibera_gesture:state", "vibera_state:audio"],
    "skill": "skills/vibera-studio/references/velocity-graph.md", "verification": verification})
write("docs/mcp/SURFACE_INVENTORY.json", inventory)
# Review only the new dedicated graph callbacks. Do not waive shared legacy
# Editor gestures or unrelated Settings/Macro controls.
import importlib.util
spec = importlib.util.spec_from_file_location("coverage_gate", root / "scripts/mcp-coverage.py")
gate = importlib.util.module_from_spec(spec); spec.loader.exec_module(gate)
reviewed = read("docs/mcp/REVIEWED_SURFACES.json")
for entry in gate.inventory()["customSurfaceCandidates"]:
    source, line, _ = entry["surface"].split(":")
    text = (root/source).read_text(encoding="utf-8-sig").splitlines()[int(line)-1]
    if (source == "src/RackVelocityGraph.h" or
        (source == "src/Editors.h" and "rackGraphButton.onClick" in text) or
        (source == "src/StudioRackVelocity.cpp" and "item.action" in text)):
        reviewed[entry["surface"]] = {"sourceSha256": entry["sourceSha256"], "operations": ["rack.graph", "steps.velocity"],
            "mcp": "vibera_rack", "additionalInterfaces": ["vibera_command", "vibera_gesture"], "readback": ["vibera_rack:state.graph", "vibera_inspect:notes", "vibera_gesture:state"],
            "skill": "skills/vibera-studio/references/velocity-graph.md", "verification": verification}
write("docs/mcp/REVIEWED_SURFACES.json", reviewed)
print("Recorded dedicated velocity graph evidence; inherited full MCP coverage remains incomplete")
