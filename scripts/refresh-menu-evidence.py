"""Refresh the menu-inventory receipts with their own small tests (never the full suite).

docs/mcp/MENU_EVIDENCE_SOURCES.json lists each menu receipt: either one verify script whose checks become
the receipt's checks ("script"), or registered tests that are run alone through CTest ("ctests"), plus the
source files the receipt binds to. This reruns exactly those tests on the built application, rewrites the
receipts with the current source hashes and application identity, and points every SURFACE_INVENTORY
entry that cites a receipt at the new inputs. It refuses when a test fails or when an entry's recorded
case is no longer among the receipt's checks; nothing is rebound without a passing run.

usage: python scripts/refresh-menu-evidence.py [receipt name substring ...]
"""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from dev_support import content_identity  # noqa: E402
from dev import tools  # noqa: E402

exe = ROOT / "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe"
application = hashlib.sha256(exe.read_bytes()).hexdigest()
manifest = json.loads((ROOT / "docs/mcp/MENU_EVIDENCE_SOURCES.json").read_text(encoding="utf-8"))
wanted = sys.argv[1:]
inventory_path = ROOT / "docs/mcp/SURFACE_INVENTORY.json"
inventory = json.loads(inventory_path.read_text(encoding="utf-8-sig"))
summary = {}
for receipt_name, source in manifest["receipts"].items():
    if wanted and not any(text in receipt_name for text in wanted):
        continue
    if "script" in source:
        ran = subprocess.run([sys.executable, source["script"]], cwd=ROOT, capture_output=True, text=True)
        result = json.loads(ran.stdout.strip().splitlines()[-1]) if ran.stdout.strip() else {"passed": False}
        if not result.get("passed"):
            sys.exit(f"Refused {receipt_name}: {source['script']} failed\n{ran.stdout[-600:]}{ran.stderr[-600:]}")
        folder = ROOT / "build/module-qualification" / source["folder"]
        latest = max(folder.glob("attempt-*"), key=lambda p: p.stat().st_mtime)
        checks = json.loads((latest / "verification.json").read_text(encoding="utf-8"))["checks"]
        if source.get("prefix"):
            checks = [source["prefix"] + c for c in checks]
        extra = {}
    else:
        log = ROOT / "build/module-qualification" / source["folder"] / "related-ctest.log"
        log.parent.mkdir(parents=True, exist_ok=True)
        with log.open("w", encoding="utf-8") as out:
            subprocess.run([str(tools()[1]), "-C", "Release", "-R", "^(" + "|".join(source["ctests"]) + ")$"],
                           cwd=ROOT / "build/windows", stdout=out, stderr=subprocess.STDOUT)
        text = log.read_text(encoding="utf-8", errors="replace")
        passed = set(re.findall(r"Test +#\d+: (\S+) \.+ +Passed", text))
        missing = sorted(set(source["ctests"]) - passed)
        if missing:
            sys.exit(f"Refused {receipt_name}: not passed alone: {missing} (see {log})")
        checks = sorted("ctest " + name + " passed (run alone)" for name in passed)
        extra = {"sourceLog": log.relative_to(ROOT).as_posix()}
    receipts = [(receipt_name, checks)]
    for name, more in source.get("merge", {}).items():  # Extra scripts whose checks join this receipt.
        ran = subprocess.run([sys.executable, more["script"]], cwd=ROOT, capture_output=True, text=True)
        if not json.loads(ran.stdout.strip().splitlines()[-1]).get("passed"):
            sys.exit(f"Refused {receipt_name}: {more['script']} failed")
        latest = max((ROOT / "build/module-qualification" / more["folder"]).glob("attempt-*"), key=lambda p: p.stat().st_mtime)
        checks += [name + ": " + c for c in json.loads((latest / "verification.json").read_text(encoding="utf-8"))["checks"]]
    inputs = {name: content_identity(ROOT / name)["contentSha256"] for name in source["sources"]}
    receipt = dict(passed=True, applicationSha256=application, sourceInputs=inputs, sourceHashMode="text-lf-v1", checks=checks,
                   scope=source["scope"], refreshedBy="scripts/refresh-menu-evidence.py", **extra)
    (ROOT / receipt_name).write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    entries = 0
    for operation in inventory["operations"]:
        verification = operation.get("verification") or {}
        if verification.get("receipt") != receipt_name:
            continue
        # Check labels may carry run-specific values (IDs, counts): compare them without those parts.
        plain = lambda label: re.sub(r"\([^)]*\)|\{[^}]*\}|\[[^\]]*\]|[\d.]+", "", label)
        current = {plain(check): check for check in checks}
        lost = [case for case in verification.get("cases", []) if plain(case) not in current]
        if lost:
            sys.exit(f"Refused {receipt_name}: {operation['id']} cites checks that no longer pass or exist: {lost[:2]}")
        verification["cases"] = [current[plain(case)] for case in verification.get("cases", [])]
        verification["inputs"] = inputs
        entries += 1
    summary[receipt_name] = dict(checks=len(checks), entries=entries)
inventory_path.write_text(json.dumps(inventory, indent=2) + "\n", encoding="utf-8")
print(json.dumps(summary, indent=1))
