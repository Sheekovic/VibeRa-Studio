"""Bind only the Swing/Mix/truncation increment to exact tested inputs.

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
mcp = read("build/windows/swing_mcp_acceptance/verification.json")
assert mcp["passed"] and mcp["checks"] >= 33 and mcp["exitCode"] == 0 and mcp["applicationSha256"] == sha(app)
for name in ["gui-smoke.txt", "swing-gui.txt", "swing-held-take.txt"]:
    assert (root / "build/windows/gui_acceptance" / name).read_text(encoding="utf-8").startswith("PASS")
log = root / args.source_log
data = log.read_bytes()
summary = re.search(r"100% tests passed, 0 tests failed out of (\d+)", data.decode("utf-16" if data.startswith(b"\xff\xfe") else "utf-8-sig"))
assert summary and int(summary[1]) == len(re.findall(r"^add_test\(", (root / "CMakeLists.txt").read_text(encoding="utf-8"), re.M))
assert log.stat().st_mtime >= (root / app).stat().st_mtime
inputs = {x["path"]: x["sha256"] for x in read("build/build-inputs.json")}
for name in ["scripts/verify-swing-mcp.py", "scripts/record-swing-evidence.py", "skills/vibera-studio/SKILL.md",
             "skills/vibera-studio/references/swing.md", "skills/vibera-studio/scripts/stdio_client.py"]:
    inputs[name] = sha(name)
assert all(sha(name) == digest for name, digest in inputs.items()), "Acceptance inputs changed"
cases = ["global/Channel/bulk Swing and truncation share typed commands, strict atomic target/range errors and exact history",
         "schema13 exact round trip and unchanged-audio legacy defaults; source notes/IDs remain unchanged",
         "steps and melody onsets, linked/cropped placements, off-grid ordering and optional original-end truncation",
         "actual Slider continuous callbacks/one Undo/Redo/Escape and real bulk Mix/truncation menu actions",
         "Pattern/Song actual UI and scoped semantic Swing leases retain held MIDI take/target/graph; Stop separate Undo",
         "canonical current-Channel Swing macro; normalized units and stable readback",
         "real stdio native-free typed errors, recording/domain ownership, exact gesture commit and real expiry",
         "actual pulse WAV onsets verify straight/full/product amounts; unrelated right PCM byte-identical",
         "actual project save/reopen/WAV and terminal async export failure; no physical input/listening inference"]

receipt_path = "docs/mcp/SWING_EVIDENCE.json"
write(receipt_path, {"passed": True, "applicationSha256": sha(app), "sourceInputs": inputs, "checks": cases,
                     "sourceSuites": f"{summary[1]}/{summary[1]}", "mcpChecks": mcp["checks"],
                     "nativeInput": False, "physicalMidi": False, "listening": False,
                     "scope": "Global Swing/Channel Mix/truncation; exact installed off-grid parity, native modal input, automation and Burn-to-score remain separate"})
verification = {"status": "passed", "receipt": receipt_path, "inputs": inputs, "cases": cases}
inventory = read("docs/mcp/SURFACE_INVENTORY.json")
inventory["operations"] = [x for x in inventory["operations"] if x["id"] != "rack.swing"]
commands = ["project.swing", "channel.swingMix", "channel.truncateSwing", "rack.swingMix", "rack.truncateSwing"]
inventory["operations"].append({"id": "rack.swing", "name": "Global Swing, Channel/bulk Mix and optional note truncation",
    "source": "src/StudioSwing.cpp", "canonical": "Controller Swing commands and shared local/leased gesture history",
    "mcp": "vibera_command", "additionalInterfaces": ["vibera_gesture"], "commands": commands,
    "readback": ["vibera_state:swing", "vibera_rack:state", "vibera_inspect:notes", "vibera_gesture:state", "vibera_state:audio"],
    "skill": "skills/vibera-studio/references/swing.md", "verification": verification})
write("docs/mcp/SURFACE_INVENTORY.json", inventory)
# Review only dedicated Swing control/service callbacks. These bindings combine
# actual Slider/menu cases and canonical typed workflows; native modal submission
# is explicitly not claimed. Shared legacy handlers remain unreviewed.
import importlib.util
spec = importlib.util.spec_from_file_location("coverage_gate", root / "scripts/mcp-coverage.py")
gate = importlib.util.module_from_spec(spec); spec.loader.exec_module(gate)
reviewed = read("docs/mcp/REVIEWED_SURFACES.json")
for entry in gate.inventory()["customSurfaceCandidates"]:
    source, line, _ = entry["surface"].split(":")
    text = (root/source).read_text(encoding="utf-8-sig").splitlines()[int(line)-1]
    if source in {"src/RackSwingControl.h", "src/StudioSwing.cpp"}:
        reviewed[entry["surface"]] = {"sourceSha256": entry["sourceSha256"], "operations": commands,
            "mcp": "vibera_command", "additionalInterfaces": ["vibera_gesture"],
            "readback": ["vibera_state:swing", "vibera_rack:state", "vibera_inspect:notes", "vibera_gesture:state"],
            "skill": "skills/vibera-studio/references/swing.md", "verification": verification}

write("docs/mcp/REVIEWED_SURFACES.json", reviewed)
print("Recorded dedicated Swing evidence; inherited full MCP coverage remains incomplete")
