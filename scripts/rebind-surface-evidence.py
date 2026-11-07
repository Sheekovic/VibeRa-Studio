"""Re-bind reviewed UI-surface MCP evidence to the current inputs after a complete passing run.

Reviewed explicit UI operations (docs/mcp/SURFACE_INVENTORY.json) keep their review only while
every recorded input hash matches. Ordinary development changes shared inputs (CMakeLists.txt,
shared sources), so the reviews go stale although their tests still pass. This script refuses
unless the given `dev.ps1 -Full` CTest log passed every registered test; it then rewrites each
receipt's sourceInputs/applicationSha256 and the matching inventory inputs, and records the run
it relied on. Without --apply it only reports what would change.

usage: python scripts/rebind-surface-evidence.py --source-log build/dev/<run>/affected-tests.log [--apply]
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from dev_support import content_identity  # noqa: E402

parser = argparse.ArgumentParser()
parser.add_argument("--source-log", required=True)
parser.add_argument("--apply", action="store_true")
args = parser.parse_args()

log = (ROOT / args.source_log).read_text(encoding="utf-8-sig", errors="replace")
summary = re.search(r"100% tests passed, 0 tests failed out of (\d+)", log)
registered = re.findall(r"^\s*add_test\(NAME (\S+)", (ROOT / "CMakeLists.txt").read_text(encoding="utf-8"), re.M)
if not summary or int(summary[1]) != len(registered):
    sys.exit("Refused: the log must pass every registered test (%d)" % len(registered))
passed = set(re.findall(r"Test +#\d+: (\S+) \.+ +Passed", log))
if passed != set(registered):
    sys.exit("Refused: every registered test needs a Passed line")

exe = ROOT / "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe"
application = hashlib.sha256(exe.read_bytes()).hexdigest()


def digest(name, mode):
    path = ROOT / name
    return content_identity(path)["contentSha256"] if mode == "text-lf-v1" else hashlib.sha256(path.read_bytes()).hexdigest()


inventory_path = ROOT / "docs/mcp/SURFACE_INVENTORY.json"
inventory = json.loads(inventory_path.read_text(encoding="utf-8-sig"))
receipts, changed_ops, missing = {}, 0, set()
for op in inventory["operations"]:
    verification = op.get("verification") or {}
    if verification.get("status") != "passed":
        continue
    receipt_name = verification["receipt"]
    if receipt_name not in receipts:
        path = ROOT / receipt_name
        if not path.is_file():
            missing.add(receipt_name)
            continue
        receipt = json.loads(path.read_text(encoding="utf-8-sig"))
        mode = receipt.get("sourceHashMode", "raw")
        inputs = receipt.get("sourceInputs") or {}
        absent = [name for name in inputs if not (ROOT / name).is_file()]
        if absent:
            missing.add(receipt_name + " (inputs gone: " + ", ".join(absent[:3]) + ")")
            continue
        receipt["sourceInputs"] = {name: digest(name, mode) for name in inputs}
        receipt["applicationSha256"] = application
        receipt["reboundBy"] = dict(script="scripts/rebind-surface-evidence.py", sourceLog=args.source_log,
                                    tests=len(registered))
        receipts[receipt_name] = (path, receipt)
    if receipt_name in receipts:
        new_inputs = receipts[receipt_name][1]["sourceInputs"]
        if verification.get("inputs") != new_inputs:
            changed_ops += 1
        verification["inputs"] = new_inputs

print(json.dumps(dict(receipts=len(receipts), operationsRebound=changed_ops, missing=sorted(missing), applied=args.apply)))
if args.apply:
    for path, receipt in receipts.values():
        path.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    inventory_path.write_text(json.dumps(inventory, indent=2) + "\n", encoding="utf-8")
