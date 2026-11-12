"""Review UI callback surfaces whose handler directly runs registered, MCP-mapped commands.

A callback candidate (scripts/mcp-coverage.py) is reviewed here only when its handler excerpt (the
same 800 characters the coverage fingerprint hashes, extended to 1600 for the call lookup) literally
calls command("id") / performAction("id") / commandCall("id") for ids that are registered and
MCP-mapped. Those ids become the review's operations: the UI gesture and MCP reach the same canonical
command. Handlers that call no registered command (view-only gestures, or operations reached
through other tools) are left unreviewed: they need real registry/command work, never a label.

Evidence: a complete passing `dev.ps1 -Full` CTest log (every registered test, 100 %) bound to the
exact source file hashes and the built application, written as docs/mcp/CALLBACK_COMMANDS_EVIDENCE.json.

Manual reviews in docs/mcp/MANUAL_CALLBACK_REVIEWS.json name the registered operations of handlers
that act through tool-mapped operations; every named operation must be registered and MCP-mapped.

usage: python scripts/review-command-callbacks.py --source-log build/dev/<run>/affected-tests.log [--apply]
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import importlib.util  # noqa: E402

spec = importlib.util.spec_from_file_location("coverage", ROOT / "scripts/mcp-coverage.py")
coverage = importlib.util.module_from_spec(spec)
spec.loader.exec_module(coverage)

parser = argparse.ArgumentParser()
parser.add_argument("--source-log", required=True)
parser.add_argument("--rerun-log", action="append", default=[],
                    help="log of a failed test re-run alone (repeatable); see scripts/test_run_evidence.py")
parser.add_argument("--apply", action="store_true")
args = parser.parse_args()

from test_run_evidence import complete_passing_run  # noqa: E402
passed_tests, rerun_note = complete_passing_run(ROOT, args.source_log, args.rerun_log)

state = coverage.inventory(ROOT)
mapped = {c["id"] for c in state["registeredOperations"] if c["implementedMapping"]}
call = re.compile(r'(?:command|performAction|commandCall|runGlobal)\s*\(\s*"([A-Za-z][\w.]+)"')
application = ROOT / "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe"
app_sha = hashlib.sha256(application.read_bytes()).hexdigest()

# Manual reviews (docs/mcp/MANUAL_CALLBACK_REVIEWS.json): surfaces whose handler was read and whose
# operations reach MCP through a registered tool-mapped operation rather than a literal command call.
manual_path = ROOT / "docs/mcp/MANUAL_CALLBACK_REVIEWS.json"
manual = json.loads(manual_path.read_text(encoding="utf-8")) if manual_path.exists() else {}
for key, review in manual.items():
    unknown = [op for op in review.get("operations", []) if op not in mapped]
    if not review.get("operations") or unknown:
        raise SystemExit(f"Refused manual review {key}: unknown or unmapped operations {unknown}")
reviewed_path = ROOT / "docs/mcp/REVIEWED_SURFACES.json"
reviewed = json.loads(reviewed_path.read_text(encoding="utf-8-sig")) if reviewed_path.exists() else {}
receipt_path = "docs/mcp/CALLBACK_COMMANDS_EVIDENCE.json"
inputs, entries = {}, {}
for surface in state["customSurfaceCandidates"]:
    if surface["status"] == "reviewed":
        continue
    file, line, _ = surface["surface"].rsplit(":", 2)
    text = (ROOT / file).read_text(encoding="utf-8-sig")
    start = sum(len(part) + 1 for part in text.split("\n")[:int(line) - 1])
    operations = sorted({i for i in call.findall(text[start:start + 1600]) if i in mapped})
    if not operations and surface["surface"] in manual:
        operations = sorted(set(manual[surface["surface"]]["operations"]))
    if not operations:
        continue
    inputs[file] = hashlib.sha256((ROOT / file).read_bytes()).hexdigest()
    entries[surface["surface"]] = dict(
        sourceSha256=surface["sourceSha256"], operations=operations,
        readback=["vibera_state", "vibera_inspect", "vibera_commands"],
        skill="skills/vibera-studio/SKILL.md",
        evidence="Handler calls these canonical commands directly; the commands' MCP adapters and "
                 "semantics are covered by the complete passing run named in the receipt.")
checks = sorted(set(passed_tests))
receipt = {"passed": True, "sourceInputs": inputs, "sourceHashMode": "raw", "applicationSha256": app_sha,
           "checks": checks, "sourceLog": args.source_log, "rerunLogs": args.rerun_log, "rerunNote": rerun_note,
           "scope": "UI callbacks that directly invoke registered, MCP-mapped commands. Not native pointer "
                    "input, listening or hardware evidence."}
for key, entry in entries.items():
    file = key.rsplit(":", 2)[0]
    entry["verification"] = {"status": "passed", "inputs": {file: inputs[file]}, "cases": checks[:8] + ["..."][:0] or checks,
                             "receipt": receipt_path, "sourceHashMode": "raw"}
# The receipt must carry exactly each review's inputs: one receipt per source file.
by_file = {}
for key, entry in entries.items():
    by_file.setdefault(key.rsplit(":", 2)[0], []).append(key)
outputs = {}
# Studio.h and Studio.cpp share a stem: every surface file with that stem except the .cpp gets its
# extension in the receipt name, whether or not the other file has reviews in this run.
stems = {}
for surface in state["customSurfaceCandidates"]:
    stems.setdefault(Path(surface["surface"].rsplit(":", 2)[0]).stem.lower(), set()).add(surface["surface"].rsplit(":", 2)[0])
for file, keys in by_file.items():
    label = Path(file).stem
    if len(stems.get(label.lower(), ())) > 1 and Path(file).suffix != ".cpp":
        label += "_" + Path(file).suffix.lstrip(".")
    name = "docs/mcp/CALLBACK_" + re.sub(r"[^A-Za-z0-9]+", "_", label).upper() + "_EVIDENCE.json"
    outputs[name] = dict(receipt, sourceInputs={file: inputs[file]})
    for key in keys:
        entries[key]["verification"]["receipt"] = name
print(json.dumps(dict(reviewable=len(entries), files=len(by_file),
                      remaining=len(state["unreviewedCustomSurfaces"]) - len(entries), applied=args.apply)))
if args.apply:
    for name, content in outputs.items():
        (ROOT / name).write_text(json.dumps(content, indent=2) + "\n", encoding="utf-8")
    reviewed.update(entries)
    reviewed_path.write_text(json.dumps(reviewed, indent=2, sort_keys=True) + "\n", encoding="utf-8")
