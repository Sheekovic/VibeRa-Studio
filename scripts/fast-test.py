"""Parallel CTest runner for development.

Runs registered tests from build/windows concurrently, longest first, while
tests listed in tests/exclusive-tests.txt run afterwards in a light pool of
--exclusive-jobs (default 2; 1 = strictly alone) because they measure real
deadlines or interfere with heavy parallel peers. Every
test keeps its own CTest TIMEOUT. A failure is rerun once alone; one that
then passes is reported as PARALLEL-SENSITIVE (add it to the exclusive list),
never silently counted as a pass.

    python scripts/fast-test.py                 # all tests
    python scripts/fast-test.py -R "sampler_"   # regex subset
    python scripts/fast-test.py --jobs 8

Development evidence only; release qualification keeps its own gates.
"""
from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import threading
import time

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build/windows"
EXCLUSIVE = ROOT / "tests/exclusive-tests.txt"
DURATIONS = BUILD / "fast-test-durations.json"
LOGS = BUILD / "fast-test"


def ctest_path():
    vswhere = Path("C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe")
    found = subprocess.check_output([str(vswhere), "-products", "*", "-property", "installationPath"],
                                    text=True).splitlines()
    for vs in found:
        ctest = Path(vs.strip()) / "Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/ctest.exe"
        if ctest.is_file():
            return ctest
    raise RuntimeError("Visual Studio CMake/CTest not found")


def inventory():
    raw = subprocess.check_output([str(ctest_path()), "-C", "Release", "--show-only=json-v1"], cwd=BUILD,
                                  text=True, encoding="utf-8")
    tests = []
    for t in json.loads(raw)["tests"]:
        props = {p["name"]: p["value"] for p in t.get("properties", [])}
        tests.append(dict(name=t["name"], command=t["command"], timeout=float(props.get("TIMEOUT", 1500)),
                          cwd=props.get("WORKING_DIRECTORY", str(BUILD)), depends=props.get("DEPENDS", [])))
    return tests


def exclusive_names():
    """Names to run after the pool; a leading '!' means strictly alone."""
    if not EXCLUSIVE.exists():
        return set(), set()
    names = [line.split("#", 1)[0].strip() for line in EXCLUSIVE.read_text(encoding="utf-8").splitlines()]
    names = [n for n in names if n]
    return {n.lstrip("!") for n in names}, {n[1:] for n in names if n.startswith("!")}


def kill_tree(process):
    # Owned child handle only: terminate exactly the process tree this runner started.
    subprocess.run(["taskkill", "/T", "/F", "/PID", str(process.pid)], capture_output=True)


def run(test):
    LOGS.mkdir(parents=True, exist_ok=True)
    log = LOGS / (test["name"] + ".log")
    started = time.monotonic()
    with open(log, "w", encoding="utf-8", errors="replace") as out:
        process = subprocess.Popen(test["command"], cwd=test["cwd"], stdout=out, stderr=subprocess.STDOUT,
                                   creationflags=subprocess.CREATE_NEW_PROCESS_GROUP)
        try:
            code = process.wait(timeout=test["timeout"])
            state = "pass" if code == 0 else "fail"
        except subprocess.TimeoutExpired:
            kill_tree(process)
            process.wait()
            state = "timeout"
    return state, time.monotonic() - started


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("-R", "--regex", default="")
    parser.add_argument("--jobs", type=int, default=max(2, min(8, (os.cpu_count() or 4) // 4)))
    parser.add_argument("--exclusive-jobs", type=int, default=2,
                        help="light concurrency for load-sensitive tests; failures still rerun alone")
    parser.add_argument("--no-rerun", action="store_true", help="report parallel failures without a solo rerun")
    args = parser.parse_args()
    tests = [t for t in inventory() if re.search(args.regex, t["name"])]
    if not tests:
        print("No tests matched")
        return 1
    exclusive, strict = exclusive_names()
    durations = json.loads(DURATIONS.read_text(encoding="utf-8")) if DURATIONS.exists() else {}
    names = {t["name"] for t in tests}
    # A test with a dependency runs alone after its dependency, to keep ordering simple.
    solo = [t for t in tests if t["name"] in exclusive or t["depends"]]
    pool = [t for t in tests if t not in solo]
    pool.sort(key=lambda t: -durations.get(t["name"], 30))
    results, lock = {}, threading.Lock()
    began = time.monotonic()

    def record(test, state, seconds, phase):
        with lock:
            results[test["name"]] = (state, seconds, phase)
            if state == "pass":
                durations[test["name"]] = round(seconds, 2)
            mark = "ok " if state == "pass" else state.upper()
            print(f"[{len(results):3}/{len(tests)}] {mark:7} {seconds:7.1f}s  {test['name']}  ({phase})",
                  flush=True)

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as executor:
        futures = {executor.submit(run, t): t for t in pool}
        for future in concurrent.futures.as_completed(futures):
            state, seconds = future.result()
            record(futures[future], state, seconds, f"parallel x{args.jobs}")
    # Load-sensitive tests share a light pool; dependent tests run strictly alone last.
    light = sorted((t for t in solo if not t["depends"] and t["name"] not in strict),
                   key=lambda t: -durations.get(t["name"], 30))
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.exclusive_jobs)) as executor:
        futures = {executor.submit(run, t): t for t in light}
        for future in concurrent.futures.as_completed(futures):
            state, seconds = future.result()
            phase = "alone" if args.exclusive_jobs <= 1 else f"sensitive x{args.exclusive_jobs}"
            record(futures[future], state, seconds, phase)
    for test in (t for t in solo if t["depends"] or t["name"] in strict):
        state, seconds = run(test)
        record(test, state, seconds, "alone")
    sensitive = []
    if not args.no_rerun:
        for name, (state, _, phase) in list(results.items()):
            if state != "pass" and phase != "alone":
                test = next(t for t in tests if t["name"] == name)
                again, seconds = run(test)
                if again == "pass":
                    sensitive.append(name)
                    durations[name] = round(seconds, 2)
                results[name] = (again if again != "pass" else "parallel-sensitive", seconds, "rerun alone")
                print(f"rerun alone: {name} -> {again}", flush=True)
    DURATIONS.write_text(json.dumps(durations, indent=1, sort_keys=True), encoding="utf-8")
    failed = sorted(n for n, (s, _, _) in results.items() if s not in ("pass", "parallel-sensitive"))
    # Machine-readable outcome bound to the exact binaries the tests executed.
    binaries = sorted({part for t in tests for part in t["command"] if part.lower().endswith(".exe") and
                       Path(part).is_file() and BUILD in Path(part).resolve().parents})
    summary = dict(regex=args.regex, inventory=len(tests),
                   results={n: dict(state=s, seconds=round(sec, 2), phase=ph) for n, (s, sec, ph) in results.items()},
                   binaries={str(Path(b).resolve().relative_to(ROOT).as_posix()):
                             hashlib.sha256(Path(b).read_bytes()).hexdigest() for b in binaries})
    (LOGS / "summary.json").write_text(json.dumps(summary, indent=1, sort_keys=True), encoding="utf-8")
    wall = time.monotonic() - began
    print(f"\n{len(tests) - len(failed) - len(sensitive)} passed, {len(failed)} failed, "
          f"{len(sensitive)} parallel-sensitive; wall {wall / 60:.1f} min "
          f"(serial sum {sum(r[1] for r in results.values()) / 60:.1f} min). Logs: {LOGS}")
    for name in failed:
        print("FAILED:", name, "->", LOGS / (name + ".log"))
    for name in sensitive:
        print("PARALLEL-SENSITIVE (passed alone; add to tests/exclusive-tests.txt):", name)
    return 1 if failed or sensitive else 0


if __name__ == "__main__":
    sys.exit(main())
