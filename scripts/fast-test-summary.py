"""Check build/windows/fast-test/summary.json for the release transaction.

--require-current exits 0 only for a complete unfiltered run where every
registered test passed (in parallel or rerun alone) and every executed binary
is byte-identical now. --count prints the passed test count.
"""
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
SUMMARY = ROOT / "build/windows/fast-test/summary.json"


RECEIPT = ROOT / "build/module-qualification/rps-current/verification.json"


def complete_regression():
    """A passed dev.ps1 -Full regression (record-rps-evidence.py receipt) is the same complete
    suite: reuse it when it ran every registered test and every candidate binary is unchanged."""
    if not RECEIPT.exists():
        return None, "no complete regression receipt"
    receipt = json.loads(RECEIPT.read_text(encoding="utf-8"))
    import re
    registered = re.findall(r"^\s*add_test\(NAME (\S+)", (ROOT / "CMakeLists.txt").read_text(encoding="utf-8"), re.M)
    log = ROOT / receipt.get("sourceLog", {}).get("path", "")
    text = log.read_text(encoding="utf-8-sig", errors="replace") if log.is_file() else ""
    passed = set(re.findall(r"Test +#\d+: (\S+) \.+ +Passed", text))
    if not receipt.get("passed") or set(registered) != passed or len(registered) < 200:
        return None, "the complete regression does not cover the current test inventory"
    if hashlib.sha256(log.read_bytes()).hexdigest() != receipt["sourceLog"]["sha256"]:
        return None, "the regression log changed after it was recorded"
    for item in receipt.get("candidateHashes", []):
        file = ROOT / item["path"]
        if not file.is_file() or hashlib.sha256(file.read_bytes()).hexdigest() != item["sha256"]:
            return None, "binary changed since the regression: " + item["path"]
    return {"results": {name: {"state": "pass"} for name in registered}}, ""


def current():
    if not SUMMARY.exists():
        return complete_regression()
    summary = json.loads(SUMMARY.read_text(encoding="utf-8"))
    results = summary.get("results", {})
    if summary.get("regex"):
        return None, "summary is from a filtered run"
    if len(results) != summary.get("inventory") or summary.get("inventory", 0) < 200:
        return None, "summary does not cover the whole inventory"
    failed = [n for n, r in results.items() if r["state"] not in ("pass", "parallel-sensitive")]
    if failed:
        return None, "failed: " + ", ".join(sorted(failed)[:10])
    for path, digest in summary.get("binaries", {}).items():
        file = ROOT / path
        if not file.is_file() or hashlib.sha256(file.read_bytes()).hexdigest() != digest:
            return None, "binary changed since the run: " + path
    return summary, ""


if __name__ == "__main__":
    summary, reason = current()
    if not summary:
        summary, fallback = complete_regression()
        reason = reason if not summary else ""
        if summary and "--count" not in sys.argv:
            print(f"Reusing complete dev.ps1 -Full regression of {len(summary['results'])} tests on unchanged binaries")
            sys.exit(0)
    if "--count" in sys.argv:
        print(len(summary["results"]) if summary else 0)
        sys.exit(0 if summary else 1)
    if not summary:
        print("fast-test summary not reusable: " + reason)
        sys.exit(1)
    print(f"Reusing complete fast-test pass of {len(summary['results'])} tests on unchanged binaries")
