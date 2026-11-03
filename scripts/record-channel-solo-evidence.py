"""Bind the new solo/bulk mute and Workflow preference evidence to exact inputs.

This qualifies only the new increment; inherited custom handlers and gaps remain.
"""
import argparse
import hashlib
import importlib.util
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
mcp = read("build/windows/channel_solo_mcp_acceptance/verification.json")
assert mcp["passed"] and mcp["checks"] >= 31 and mcp["exitCode"] == 0 and mcp["applicationSha256"] == sha(app)
for name in ["gui-smoke.txt", "channel-solo-gui.txt", "channel-solo-held-take.txt"]:
    assert (root / "build/windows/gui_acceptance" / name).read_text(encoding="utf-8").startswith("PASS")
log = root / args.source_log
data = log.read_bytes()
summary = re.search(r"100% tests passed, 0 tests failed out of (\d+)", data.decode("utf-16" if data.startswith(b"\xff\xfe") else "utf-8-sig"))
assert summary and int(summary[1]) == len(re.findall(r"^add_test\(", (root / "CMakeLists.txt").read_text(encoding="utf-8"), re.M))
assert log.stat().st_mtime >= (root / app).stat().st_mtime
inputs = {x["path"]: x["sha256"] for x in read("build/build-inputs.json")}
for name in ["scripts/verify-channel-solo-mcp.py", "scripts/record-channel-solo-evidence.py", "skills/vibera-studio/SKILL.md",
             "skills/vibera-studio/references/solo.md", "skills/vibera-studio/scripts/stdio_client.py"]:
    inputs[name] = sha(name)
assert all(sha(name) == digest for name, digest in inputs.items()), "Acceptance inputs changed"
cases = ["Ctrl-click and real mute-menu callback preserve active MIDI target",
         "baseline retention across solo switches/idempotence and exact one-step Undo/Redo",
         "bulk selection plus atomic stale/duplicate/non-owner errors",
         "schema12 saved active solo/reopen/restore and corrupt-input refusal",
         "real Workflow checkbox, registered shortcut and typed MCP configure share persistent preference",
         "readonly profile failure preserves current and saved preference",
         "Pattern/Song held recording, loops, live same-key owner merge and fixed-storage allocation probe",
         "real stdio save/reopen/linked arrangement/offline PCM and terminal failed export"]
receipt_path = "docs/mcp/CHANNEL_SOLO_EVIDENCE.json"
write(receipt_path, {"passed": True, "applicationSha256": sha(app), "sourceInputs": inputs, "checks": cases,
                     "sourceSuites": f"{summary[1]}/{summary[1]}", "mcpChecks": mcp["checks"],
                     "nativeInput": False, "physicalMidi": False, "listening": False,
                     "scope": "Channel solo/bulk score mute only; group/locked solo and inherited coverage remain open"})
verification = {"status": "passed", "receipt": receipt_path, "inputs": inputs, "cases": cases}
inventory = read("docs/mcp/SURFACE_INVENTORY.json")
new_ids = {"rack.soloAndScoreMute", "settings.workflow.soloRestore"}
inventory["operations"] = [x for x in inventory["operations"] if x["id"] not in new_ids]
inventory["operations"].extend([
    {"id": "rack.soloAndScoreMute", "name": "Channel solo/restore and bulk score mute",
     "source": "src/StudioSolo.cpp", "canonical": "Controller channel.solo/unsolo/mute and rack mute commands",
     "mcp": "vibera_command", "commands": ["channel.solo", "channel.unsolo", "channel.mute", "rack.muteSelected", "rack.unmuteSelected"],
     "readback": ["vibera_rack:state", "vibera_state:channels", "vibera_inspect:notes"],
     "skill": "skills/vibera-studio/references/solo.md", "verification": verification},
    {"id": "settings.workflow.soloRestore", "name": "Restore previous mute state after Channel solo",
     "source": "src/RackPage.h", "canonical": "Studio::setSoloRestorePreference persists before updating shared state",
     "mcp": "vibera_rack", "commands": ["rack.soloRestore"],
     "readback": ["vibera_rack:state"], "skill": "skills/vibera-studio/references/solo.md", "verification": verification}])
write("docs/mcp/SURFACE_INVENTORY.json", inventory)
spec = importlib.util.spec_from_file_location("coverage_gate", root / "scripts/mcp-coverage.py")
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)
reviews = read("docs/mcp/REVIEWED_SURFACES.json")
for entry in gate.inventory()["customSurfaceCandidates"]:
    source, line, _ = entry["surface"].split(":")
    if source == "src/RackPage.h" and "restore.onClick" in (root / source).read_text(encoding="utf-8").splitlines()[int(line)-1]:
        reviews[entry["surface"]] = {"sourceSha256": entry["sourceSha256"], "operations": ["rack.soloRestore"],
            "readback": ["vibera_rack:state"], "skill": "skills/vibera-studio/references/solo.md", "verification": verification}
write("docs/mcp/REVIEWED_SURFACES.json", reviews)
print("Recorded scoped solo/preference evidence; no inherited coverage waived")
