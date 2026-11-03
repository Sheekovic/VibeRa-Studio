"""Bind only the new Rack feature's passed acceptance to current source inputs.

Leaves inherited reviews, gaps and the incomplete full-product inventory intact.
Run after the exact source suite passes; generated tool schemas remain authoritative.
"""
import hashlib
import importlib.util
import json
import argparse
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source-log',default='build/rack-source-tests.log')
args=parser.parse_args()
read = lambda p: json.loads((root / p).read_text(encoding="utf-8-sig"))
write = lambda p, value: (root / p).write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
sha = lambda p: hashlib.sha256((root / p).read_bytes()).hexdigest()
app = "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe"
mcp = read("build/windows/rack_mcp_acceptance/verification.json")
assert mcp["passed"] and mcp["exitCode"] == 0 and mcp["checks"] >= 30 and mcp["applicationSha256"] == sha(app)
for name in ["gui-smoke.txt", "rack-groups-gui.txt", "rack-groups-held-take.txt"]:
    assert (root / "build/windows/gui_acceptance" / name).read_text().startswith("PASS")
log_path=root/args.source_log
log = log_path.read_bytes()
summary=re.search(r'100% tests passed, 0 tests failed out of (\d+)',log.decode("utf-16" if log.startswith(b"\xff\xfe") else "utf-8-sig"))
assert summary and int(summary[1])==len(re.findall(r'^add_test\(', (root/'CMakeLists.txt').read_text(encoding='utf-8'), re.M))
assert log_path.stat().st_mtime >= (root/app).stat().st_mtime, 'Source suite log predates the compiled application'
suites=f'{summary[1]}/{summary[1]}'
inputs = {item["path"]: item["sha256"] for item in read("build/build-inputs.json")}
for path in ["scripts/verify-rack-mcp.py", "scripts/record-rack-evidence.py", "skills/vibera-studio/SKILL.md",
             "skills/vibera-studio/references/rack.md", "skills/vibera-studio/scripts/stdio_client.py"]:
    inputs[path] = sha(path)
assert all(sha(path) == digest for path, digest in inputs.items()), "Acceptance inputs changed"
cases = ["persisted empty/named groups and schema10 migration", "atomic group validation/create/reuse/rename/delete",
         "exact Undo/Redo and current-selection macro", "visible-row selector single/right drag/skipped rows/double all",
         "filtered step/mute/instrument/PR/sample-drop/MIDI-drop identity", "group dialog validation/cancel/confirm",
         "filter cycle and shared clamped scrolling", "Pattern/Song grouping while held MIDI recording",
         "actual stdio state/selection/history/invalid IDs/save/reopen/WAV/terminal failed export"]
receipt = {"passed": True, "applicationSha256": sha(app), "sourceInputs": inputs, "checks": cases,
           "mcpChecks": mcp["checks"], "sourceSuites": suites, "nativeInput": False, "listening": False,
           "hardware": False, "scope": "CR-01 grouping increment only; inherited full coverage remains incomplete"}
path = "docs/mcp/RACK_GROUPS_EVIDENCE.json"
write(path, receipt)
verification = {"status": "passed", "receipt": path, "inputs": inputs, "cases": cases}
commands = ["rack.group.create", "rack.group.rename", "rack.group.delete", "rack.group.assign",
            "rack.group.dialog", "rack.group.previous", "rack.group.next"]
surfaces = read("docs/mcp/SURFACE_INVENTORY.json")
surfaces["operations"] = [x for x in surfaces["operations"] if x["id"] != "rack.selectionGroupsAndFilter"]
surfaces["operations"].append({"id": "rack.selectionGroupsAndFilter", "name": "Rack selection, groups, display filter and scrolling",
    "source": "src/StudioRack.cpp", "canonical": "Studio Rack services and Controller rack.group commands",
    "mcp": "vibera_rack", "commands": commands, "readback": ["vibera_rack:state", "vibera_state:rack"],
    "skill": "skills/vibera-studio/references/rack.md", "verification": verification})
write("docs/mcp/SURFACE_INVENTORY.json", surfaces)
spec = importlib.util.spec_from_file_location("coverage_gate", root / "scripts/mcp-coverage.py")
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)
reviewed = read("docs/mcp/REVIEWED_SURFACES.json")
# Dedicated new controls only. Shared legacy Editor mouse handlers remain unreviewed
# because this increment does not establish all their other feature branches.
for entry in gate.inventory()["customSurfaceCandidates"]:
    source, line, _ = entry["surface"].split(":")
    text = (root / source).read_text(encoding="utf-8-sig").splitlines()[int(line)-1]
    if ((source == "src/Editors.h" and ("rackFilter.onChange" in text or "rackMenu.onClick" in text)) or
            (source == "src/StudioRack.cpp" and "onTextChange" in text)):
        reviewed[entry["surface"]] = {"sourceSha256": entry["sourceSha256"], "operations": commands,
            "readback": ["vibera_rack:state"], "skill": "skills/vibera-studio/references/rack.md", "verification": verification}
write("docs/mcp/REVIEWED_SURFACES.json", reviewed)
print("Recorded current Rack-only evidence; inherited coverage gate is not waived")
