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
parser.add_argument("--rerun-log", action="append", default=[],
                    help="log of a failed test re-run alone (repeatable); see scripts/test_run_evidence.py")
parser.add_argument("--apply", action="store_true")
args = parser.parse_args()

sys.path.insert(0, str(ROOT / "scripts"))
from test_run_evidence import complete_passing_run  # noqa: E402
passed, rerun_note = complete_passing_run(ROOT, args.source_log, args.rerun_log)

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
                                    rerunLogs=args.rerun_log, rerunNote=rerun_note,
                                    tests=len(passed))
        receipts[receipt_name] = (path, receipt)
    if receipt_name in receipts:
        new_inputs = receipts[receipt_name][1]["sourceInputs"]
        if verification.get("inputs") != new_inputs:
            changed_ops += 1
        verification["inputs"] = new_inputs

# Reviewed callback surfaces that cite one of the rebound receipts follow it: same receipt, same
# complete run. A surface whose own file is not among the receipt's inputs stays stale.
reviewed_path = ROOT / "docs/mcp/REVIEWED_SURFACES.json"
reviewed = json.loads(reviewed_path.read_text(encoding="utf-8-sig")) if reviewed_path.is_file() else {}
changed_surfaces = 0
for key, entry in reviewed.items():
    verification = entry.get("verification") or {}
    receipt_name = verification.get("receipt")
    if verification.get("status") != "passed" or receipt_name not in receipts:
        continue
    new_inputs = receipts[receipt_name][1]["sourceInputs"]
    if key.rsplit(":", 2)[0] in new_inputs and verification.get("inputs") != new_inputs:
        verification["inputs"] = new_inputs
        verification["sourceHashMode"] = receipts[receipt_name][1].get("sourceHashMode", "raw")
        changed_surfaces += 1

print(json.dumps(dict(receipts=len(receipts), operationsRebound=changed_ops, surfacesRebound=changed_surfaces,
                      missing=sorted(missing), applied=args.apply)))
if args.apply:
    reviewed_path.write_text(json.dumps(reviewed, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    for path, receipt in receipts.values():
        path.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    inventory_path.write_text(json.dumps(inventory, indent=2) + "\n", encoding="utf-8")
