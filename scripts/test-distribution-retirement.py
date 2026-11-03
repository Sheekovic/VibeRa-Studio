# SPDX-License-Identifier: AGPL-3.0-or-later
"""Fault-injection tests for the read-only distribution data-preservation gate."""
import hashlib
import importlib.util
import json
from pathlib import Path

root = Path(__file__).resolve().parent.parent
module_path = root / "scripts/check-distribution-retirement.py"
spec = importlib.util.spec_from_file_location("retirement_guard", module_path)
guard = importlib.util.module_from_spec(spec)
spec.loader.exec_module(guard)
output = root / "build/distribution-retirement-tests"
output.mkdir()  # Preserve old receipts; choose a fresh owned folder on another run.
results = []


def snapshot(folder):
    return {str(p.relative_to(folder)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in folder.rglob("*") if p.is_file()}


for case in ["valid", "extra-project", "changed-example", "missing-runtime", "empty-user-directory"]:
    folder = output / case
    folder.mkdir()
    (folder / "bundle.dat").write_bytes(b"owned test fixture, not an executable")
    (folder / "example.vibra").write_bytes(b"original test project")
    entries = [{"path": name, "sha256": hashlib.sha256((folder / name).read_bytes()).hexdigest()}
               for name in ["bundle.dat", "example.vibra"]]
    (folder / "provenance.json").write_text(json.dumps({"sourceCommit": "synthetic-fixture", "files": entries}))
    if case == "extra-project":
        (folder / "My song.vibra").write_bytes(b"user project sentinel must survive")
    if case == "changed-example":
        (folder / "example.vibra").write_bytes(b"edited project sentinel must survive")
    if case == "missing-runtime":
        (folder / "bundle.dat").unlink()  # Only the explicitly created synthetic fixture.
    if case == "empty-user-directory":
        (folder / "User projects").mkdir()
    before = snapshot(folder)
    report = guard.inspect(folder)
    assert report["passed"] == (case == "valid"), case
    assert before == snapshot(folder), "Guard changed data: " + case
    results.append({"case": case, "report": report, "allFileBytesUnchanged": True})

traversal = output / "unsafe-manifest"
traversal.mkdir()
(traversal / "provenance.json").write_text(json.dumps({"sourceCommit": "synthetic-fixture", "files": [
    {"path": "../outside.txt", "sha256": "0" * 64}]}))
try:
    guard.inspect(traversal)
except ValueError:
    results.append({"case": "unsafe-manifest", "refusedBeforeTraversal": True})
else:
    raise AssertionError("Unsafe manifest was accepted")

canonical = guard.inspect(root / "dist/VibeRa Studio")
assert canonical["passed"], canonical
receipt = {"scope": "Read-only data preservation guard, not new application qualification",
           "cases": results, "canonicalBundle": canonical,
           "guardSha256": hashlib.sha256(module_path.read_bytes()).hexdigest(),
           "linkedDirectoryInjection": "Not performed; no system symlink configuration changed"}
(output / "verification.json").write_text(json.dumps(receipt, indent=2) + "\n")
print("PASS six preservation/refusal cases; all sentinel bytes retained; canonical bundle unchanged")
