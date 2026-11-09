"""Shared rule for which CTest logs count as one complete passing run.

A complete `dev.ps1 -Full` log either passes every registered test, or its failed tests were each
re-run alone afterwards (`ctest -R ^name$ --output-on-failure > log`) and passed, with the same
built application: the executable must not be newer than the full run's log start. A failure
without a passing re-run, a test missing from both, or a rebuilt binary is refused. Failed full-run
lines stay recorded in the receipt description; nothing is relabelled as passed.
"""
from pathlib import Path
import re
import sys

PASS = re.compile(r"Test +#\d+: (\S+) \.+ +Passed")
FAIL = re.compile(r"Test +#\d+: (\S+) \.+\**(?:Failed|Timeout|Not Run|Exception)")


def complete_passing_run(root: Path, source_log: str, rerun_logs=()):
    """Returns (passed test names, note). Exits with a refusal when the evidence is incomplete."""
    registered = set(re.findall(r"^\s*add_test\(NAME (\S+)", (root / "CMakeLists.txt").read_text(encoding="utf-8"), re.M))
    source = root / source_log
    text = source.read_text(encoding="utf-8-sig", errors="replace")
    passed, failed = set(PASS.findall(text)), set(FAIL.findall(text))
    if not passed:
        sys.exit("Refused: the source log has no passing tests")
    exe = root / "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe"
    # The run builds first; the binary it tested is the one present when its compile step finished.
    compiled = source.parent / "compiled-inputs.log"
    start = compiled.stat().st_mtime if compiled.is_file() else min(
        p.stat().st_mtime for p in source.parent.iterdir() if p.is_file())
    note = ""
    if rerun_logs:
        if exe.stat().st_mtime > start:
            sys.exit("Refused: the application was rebuilt after the full run; re-run the suite")
        rerun_passed, rerun_failed = set(), set()
        for name in rerun_logs:
            rerun = root / name
            if rerun.stat().st_mtime < source.stat().st_mtime:
                sys.exit(f"Refused: re-run log {name} is older than the full run")
            body = rerun.read_text(encoding="utf-8-sig", errors="replace")
            rerun_passed |= set(PASS.findall(body))
            rerun_failed |= set(FAIL.findall(body))
        unresolved = (failed - rerun_passed) | rerun_failed
        if unresolved:
            sys.exit("Refused: failed tests without a passing re-run: " + ", ".join(sorted(unresolved)))
        unreached = registered - passed - failed
        passed |= rerun_passed
        note = (f"full run {source_log}: failed {sorted(failed)}, not reached {sorted(unreached & rerun_passed)}; "
                f"each passed when run alone ({', '.join(rerun_logs)}) with the same application")
    elif failed:
        sys.exit("Refused: failed tests " + ", ".join(sorted(failed)) + "; re-run them alone and pass --rerun-log")
    missing = registered - passed
    if missing:
        sys.exit("Refused: registered tests without a Passed line: " + ", ".join(sorted(missing)[:20]))
    return passed & registered, note
