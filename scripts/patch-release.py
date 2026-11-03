"""Patch release gate (0.0.x -> 0.0.x+1): no complete suite.

check: the CMake version must keep the qualified distribution's major.minor and raise its
patch; a minor/major release still needs the complete dev.ps1 -Full regression.
tests: runs the tests dev-select.py maps to every change since the distribution's source
commit, plus a fixed core smoke set, through fast-test.py, and verifies every one passed on
the current binaries. Prints the passed count for distribution.py record.
"""
import hashlib
import importlib.util
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DIST = ROOT / "dist/VibeRa Studio/provenance.json"
SUMMARY = ROOT / "build/windows/fast-test/summary.json"
# Startup/workspace, save/open/history, musical playback/export and the MCP contract.
SMOKE = ["gui_workflow", "musical_acceptance", "history_ui_recovery", "number_serialization_safety",
         "mcp_semantics", "mcp_coverage_detector", "module_release_policy"]
RELEASE_TOOLING = {"scripts/build.ps1", "scripts/package.ps1", "scripts/module-gate.py", "scripts/patch-release.py",
                   "scripts/fast-test-summary.py", "scripts/record-rps-evidence.py"}
TOOLING_TESTS = ["module_release_policy", "development_workflow", "development_fixture_profiles",
                 "installer_package"]


def git(*args):
    return subprocess.run(["git", "-c", f"safe.directory={ROOT.as_posix()}", *args], cwd=ROOT, check=True,
                          capture_output=True, text=True, encoding="utf-8").stdout


def version(text):
    found = re.search(r"project\(VibeRa VERSION (\d+)\.(\d+)\.(\d+)", text)
    if not found:
        raise SystemExit("No project version in CMakeLists.txt")
    return tuple(int(x) for x in found.groups())


def released():
    commit = json.loads(DIST.read_text(encoding="utf-8-sig"))["sourceCommit"]
    return commit, version(git("show", f"{commit}:CMakeLists.txt"))


def check():
    commit, before = released()
    now = version((ROOT / "CMakeLists.txt").read_text(encoding="utf-8"))
    shown = lambda v: ".".join(map(str, v))
    if now[:2] != before[:2]:
        raise SystemExit(f"{shown(now)} changes major/minor over {shown(before)}: run the complete regression")
    if now[2] <= before[2]:
        raise SystemExit(f"Raise the patch version above the released {shown(before)} first")
    print(json.dumps(dict(released=".".join(map(str, before)), candidate=".".join(map(str, now)), since=commit)))


def selection():
    commit, _ = released()
    spec = importlib.util.spec_from_file_location("dev_select", ROOT / "scripts/dev-select.py")
    selector = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(selector)
    changed = [line for line in git("diff", "--name-only", commit, "HEAD").splitlines() if line]
    # Every release bumps the version line in CMakeLists.txt; that alone selects no tests.
    # Release tooling is exercised by its own policy/workflow tests, not the product suite.
    considered = []
    for path in changed:
        if path == "CMakeLists.txt":
            lines = [line for line in git("diff", "-U0", commit, "HEAD", "--", path).splitlines()
                     if line[:1] in "+-" and not line.startswith(("+++", "---"))]
            if all("project(VibeRa VERSION" in line for line in lines):
                continue
        if path in RELEASE_TOOLING:
            continue
        considered.append(path)
    plan = selector.select(ROOT, [dict(path=path, status="M") for path in considered], False, [])
    tooling = TOOLING_TESTS if any(path in RELEASE_TOOLING for path in changed) else []
    return sorted(set(plan["testNames"]) | set(SMOKE) | set(tooling)), changed


def tests():
    names, changed = selection()
    regex = "^(" + "|".join(re.escape(n) for n in names) + ")$"
    print(f"Patch tests for {len(changed)} changed files: {len(names)} tests", flush=True)
    subprocess.run([sys.executable, "-I", str(ROOT / "scripts/fast-test.py"), "-R", regex], cwd=ROOT)
    summary = json.loads(SUMMARY.read_text(encoding="utf-8"))
    results = summary.get("results", {})
    failed = [n for n in names if results.get(n, {}).get("state") not in ("pass", "parallel-sensitive")]
    if summary.get("regex") != regex or failed:
        raise SystemExit("Patch tests failed or did not run: " + ", ".join(failed[:20]))
    for path, digest in summary.get("binaries", {}).items():
        if hashlib.sha256((ROOT / path).read_bytes()).hexdigest() != digest:
            raise SystemExit("Binary changed during the patch tests: " + path)
    print(len(names))


if __name__ == "__main__":
    command = sys.argv[1] if len(sys.argv) > 1 else ""
    if command == "check":
        check()
    elif command == "tests":
        tests()
    elif command == "plan":
        names, changed = selection()
        print(json.dumps(dict(changed=len(changed), tests=names), indent=1))
    else:
        raise SystemExit("usage: patch-release.py check|tests|plan")
