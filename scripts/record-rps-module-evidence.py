"""Write the Rack/Pattern/sampler module evidence receipt and mark reviewed rows.

Reads build/windows/fast-test/summary.json from a COMPLETE unfiltered run and
refuses unless every registered test passed (alone or in parallel) on the
current candidate binaries. The receipt binds content identities of all build
inputs plus the exact app/worker/test binaries, as scripts/module-gate.py
requires. Rows listed in ROWS become `verified` with this receipt; rows with an
unclosed review gap must be left out (see docs/modules/RPS-REVIEW.md).

Software evidence only: listening, hardware, native OS input and human FL
familiarity stay separate and unclaimed.
"""
import hashlib
import json
from pathlib import Path
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from dev_support import TEXT, content_identity  # noqa: E402

LEDGER = ROOT / "docs/modules/RACK-PATTERN-SAMPLER.json"
SUMMARY = ROOT / "build/windows/fast-test/summary.json"
RECEIPT = ROOT / "build/module-qualification/rack-pattern-sampler/verification.json"
ROWS = ["RPS-A", "RPS-B", "RPS-C", "RPS-D", "RPS-E", "RPS-F", "RPS-G", "RPS-H", "RPS-I", "RPS-J", "RPS-K",
        "RPS-L", "RPS-M"]
CANDIDATES = ["build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe",
              "build/windows/VibeRa_artefacts/Release/VibeRa Plugin Worker.exe"]


def fail(message):
    raise SystemExit("record-rps-module-evidence: " + message)


def main():
    summary = json.loads(SUMMARY.read_text(encoding="utf-8"))
    if summary.get("regex"):
        fail("summary comes from a filtered run; run scripts/fast-test.py without -R")
    results = summary["results"]
    bad = sorted(name for name, row in results.items() if row["state"] not in ("pass", "parallel-sensitive"))
    if bad or len(results) != summary["inventory"] or summary["inventory"] < 200:
        fail("full suite did not pass: " + ", ".join(bad[:10]))
    for path, digest in summary["binaries"].items():
        if hashlib.sha256((ROOT / path).read_bytes()).hexdigest() != digest:
            fail("binary changed since the test run: " + path)
    candidates = []
    for name in CANDIDATES:
        path = ROOT / name
        if not path.is_file():
            fail("missing candidate " + name)
        candidates.append(dict(path=name, sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
    for path, digest in summary["binaries"].items():
        if path not in CANDIDATES:
            candidates.append(dict(path=path, sha256=digest))
    tested = {c["path"] for c in candidates}
    if not set(summary["binaries"]) <= tested:
        fail("tested binaries are not all bound")
    dependencies = []
    roots = ["src", "tests", "assets", "patches", "cmake", "scripts", "skills/vibera-studio"]
    files = [ROOT / "CMakeLists.txt", ROOT / "CMakePresets.json", ROOT / "dependencies.lock.json"]
    for name in roots:
        files.extend(p for p in (ROOT / name).rglob("*") if p.is_file() and "__pycache__" not in p.parts)
    for path in sorted(set(files)):
        identity = content_identity(path)
        text = path.suffix.lower() in TEXT
        dependencies.append(dict(path=path.relative_to(ROOT).as_posix(), mode="content" if text else "exact",
                                 sha256=identity["contentSha256" if text else "sha256"]))
    receipt = dict(passed=True, module="rack-pattern-sampler",
                   scope="Complete registered source suite on exact candidate binaries plus the RPS row review "
                         "(docs/modules/RPS-REVIEW.md). No listening, hardware, native OS input or FL "
                         "familiarity claim.",
                   suite=dict(inventory=summary["inventory"],
                              passedAlone=sorted(n for n, r in results.items() if r["state"] == "parallel-sensitive")),
                   rows=ROWS, dependencyHashes=dependencies, candidateHashes=candidates)
    RECEIPT.parent.mkdir(parents=True, exist_ok=True)
    RECEIPT.write_text(json.dumps(receipt, indent=1) + "\n", encoding="utf-8")
    digest = hashlib.sha256(RECEIPT.read_bytes()).hexdigest()
    ledger = json.loads(LEDGER.read_text(encoding="utf-8"))
    for item in ledger["requirements"]:
        if item["id"] in ROWS:
            item["status"] = "verified"
            item["evidence"] = [dict(path=RECEIPT.relative_to(ROOT).as_posix(), sha256=digest,
                                     scope="Full suite on exact candidate binaries + RPS row review; software only")]
    LEDGER.write_text(json.dumps(ledger, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"Recorded {len(dependencies)} dependencies, {len(candidates)} binaries; rows verified: {', '.join(ROWS)}")


if __name__ == "__main__":
    main()
