"""Bind the new Channel clone/delete/reorder evidence to exact inputs.

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
mcp = read("build/windows/channel_management_mcp_acceptance/verification.json")
assert mcp["passed"] and mcp["checks"] >= 32 and mcp["exitCode"] == 0 and mcp["applicationSha256"] == sha(app)
for name in ["gui-smoke.txt", "channel-management-gui.txt", "channel-management-held-take.txt"]:
    assert (root / "build/windows/gui_acceptance" / name).read_text(encoding="utf-8").startswith("PASS")
log = root / args.source_log
data = log.read_bytes()
summary = re.search(r"100% tests passed, 0 tests failed out of (\d+)", data.decode("utf-16" if data.startswith(b"\xff\xfe") else "utf-8-sig"))
assert summary and int(summary[1]) == len(re.findall(r"^add_test\(", (root / "CMakeLists.txt").read_text(encoding="utf-8"), re.M))
assert log.stat().st_mtime >= (root / app).stat().st_mtime
inputs = {x["path"]: x["sha256"] for x in read("build/build-inputs.json")}
for name in ["scripts/verify-channel-management-mcp.py", "scripts/record-channel-management-evidence.py", "skills/vibera-studio/SKILL.md",
             "skills/vibera-studio/references/channels.md", "skills/vibera-studio/scripts/stdio_client.py"]:
    inputs[name] = sha(name)
assert all(sha(name) == digest for name, digest in inputs.items()), "Acceptance inputs changed"
cases = ["adjacent settings-only bulk clones and multi-Pattern score isolation",
         "fresh captured instrument identity/state and exact clone/delete Undo baselines",
         "block order/edge no-op/stale/duplicate/capacity and solo reconciliation",
         "owned Audio Clip/native automation cleanup retains media/Mixer links/curves",
         "actual Clone menu and name Shift-wheel graph/target/history plus grid scrolling",
         "first score Paste into an empty Channel reveals pitches without changing Rack focus or horizontal zoom",
         "held Pattern/Song recording and contextual reorder macros; async macro refusal",
         "real stdio Copy/Clone/Paste/history/state discovery/cancel/stale failure",
         "real project round trip and192000-frame48kHz24-bit non-silent stereo WAV"]
receipt_path = "docs/mcp/CHANNEL_MANAGEMENT_EVIDENCE.json"
write(receipt_path, {"passed": True, "applicationSha256": sha(app), "sourceInputs": inputs, "checks": cases,
                     "sourceSuites": f"{summary[1]}/{summary[1]}", "mcpChecks": mcp["checks"],
                     "nativeInput": False, "physicalMidi": False, "listening": False,
                     "scope": "Basic Channel settings clone/delete/reorder only; native/provider receipts and full-family limits separate"})
verification = {"status": "passed", "receipt": receipt_path, "inputs": inputs, "cases": cases}
inventory = read("docs/mcp/SURFACE_INVENTORY.json")
inventory["operations"] = [x for x in inventory["operations"] if x["id"] != "rack.channelManagement"]
inventory["operations"].append({"id": "rack.channelManagement", "name": "Channel settings clone, reorder and delete",
    "source": "src/StudioChannels.cpp", "canonical": "Studio Channel state-capture service and Controller channel.clone/delete/moveUp/moveDown",
    "mcp": "vibera_rack", "additionalInterfaces": ["vibera_command"],
    "commands": ["channel.clone", "channel.delete", "channel.moveUp", "channel.moveDown", "clipboard.copy", "clipboard.paste", "channel.paste"],
    "readback": ["vibera_rack:state", "vibera_rack:job", "vibera_inspect:channels", "vibera_inspect:notes", "vibera_state:audio"],
    "skill": "skills/vibera-studio/references/channels.md", "verification": verification})
write("docs/mcp/SURFACE_INVENTORY.json", inventory)
print("Recorded scoped Channel management evidence; shared legacy mouse handlers remain unreviewed")
