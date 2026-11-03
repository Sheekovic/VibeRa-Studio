"""Record only passed step-fill acceptance against the current native inputs."""
import argparse
import hashlib
import json
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--source-log", default="build/fill-source-tests.log")
args = parser.parse_args()
read = lambda name: json.loads((root / name).read_text(encoding="utf-8-sig"))
write = lambda name, data: (root / name).write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
sha = lambda name: hashlib.sha256((root / name).read_bytes()).hexdigest()
app = "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe"
mcp = read("build/windows/step_fill_mcp_acceptance/verification.json")
assert mcp["passed"] and mcp["checks"] >= 25 and mcp["exitCode"] == 0 and mcp["applicationSha256"] == sha(app)
for name in ["gui-smoke.txt", "step-fill-gui.txt", "step-fill-held-take.txt"]:
    assert (root / "build/windows/gui_acceptance" / name).read_text(encoding="utf-8").startswith("PASS")
log_path = root / args.source_log
data = log_path.read_bytes()
summary = re.search(r"100% tests passed, 0 tests failed out of (\d+)", data.decode("utf-16" if data.startswith(b"\xff\xfe") else "utf-8-sig"))
assert summary and int(summary[1]) == len(re.findall(r"^add_test\(", (root / "CMakeLists.txt").read_text(encoding="utf-8"), re.M))
assert log_path.stat().st_mtime >= (root / app).stat().st_mtime
inputs = {x["path"]: x["sha256"] for x in read("build/build-inputs.json")}
for name in ["scripts/verify-step-fill-mcp.py", "scripts/record-step-fill-evidence.py", "skills/vibera-studio/SKILL.md",
             "skills/vibera-studio/references/steps.md", "skills/vibera-studio/scripts/stdio_client.py"]:
    inputs[name] = sha(name)
assert all(sha(name) == digest for name, digest in inputs.items())
cases = ["exact2/4/8 mask and idempotent surviving IDs", "melodic conversion/chord attributes/outside-window isolation",
         "atomic bulk/stale/Audio Clip/capacity validation", "typed default interval/macro profile migration",
         "shared selection/one-step Undo/Redo", "actual Channel/bulk menu callbacks/custom shortcut/stale-menu guard",
         "full96-cell viewport and filtered scrolled step editing despite dormant melody", "Pattern/Song held recording and fill macro",
         "virtual loop-boundary fill/Undo/Redo lifecycle checks", "real stdio typed errors/save/reopen/linked WAV"]
receipt_path = "docs/mcp/STEP_FILL_EVIDENCE.json"
write(receipt_path, {"passed": True, "applicationSha256": sha(app), "sourceInputs": inputs, "checks": cases,
                     "sourceSuites": f"{summary[1]}/{summary[1]}", "mcpChecks": mcp["checks"],
                     "nativeInput": False, "physicalMidi": False, "listening": False,
                     "scope": "Basic step fills only; inherited MCP/FL scope remains open"})
inventory = read("docs/mcp/SURFACE_INVENTORY.json")
inventory["operations"] = [x for x in inventory["operations"] if x["id"] != "rack.stepFill"]
inventory["operations"].append({"id": "rack.stepFill", "name": "Basic interval fill, bulk conversion and full-Pattern step editing",
    "source": "src/StudioSteps.cpp", "canonical": "Controller steps.fill* and shared Rack viewport/steps.set",
    "mcp": "vibera_command", "commands": ["steps.fill", "steps.fill2", "steps.fill4", "steps.fill8", "steps.set", "pattern.length"],
    "readback": ["vibera_inspect:notes", "vibera_rack:state", "vibera_state"], "skill": "skills/vibera-studio/references/steps.md",
    "verification": {"status": "passed", "receipt": receipt_path, "inputs": inputs, "cases": cases}})
write("docs/mcp/SURFACE_INVENTORY.json", inventory)
print("Recorded current step-fill feature evidence; inherited full-coverage gate remains incomplete")
