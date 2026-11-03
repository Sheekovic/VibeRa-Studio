"""Task-owned bounded explicit-provider experiment, never a release gate."""
from pathlib import Path
import argparse
import ctypes
from ctypes import wintypes
import datetime
import os
import stat
import hashlib
import json
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]

def observe_environment():
    """Read-only names/availability; no foreground, input or desktop switching."""
    result = dict(windows=os.name == "nt", threadIsDefaultDesktop=False,
                  threadDesktopObservable=False)
    if os.name != "nt":
        return result
    user = ctypes.WinDLL("user32", use_last_error=True)
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.GetCurrentThreadId.restype = wintypes.DWORD
    user.GetThreadDesktop.argtypes = [wintypes.DWORD]
    user.GetThreadDesktop.restype = wintypes.HANDLE
    user.GetUserObjectInformationW.argtypes = [wintypes.HANDLE, ctypes.c_int,
        ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD)]
    user.GetUserObjectInformationW.restype = wintypes.BOOL
    handle = user.GetThreadDesktop(kernel.GetCurrentThreadId())
    value = ctypes.create_unicode_buffer(256)
    needed = wintypes.DWORD()
    known = bool(handle and user.GetUserObjectInformationW(
        handle, 2, value, ctypes.sizeof(value), ctypes.byref(needed)))
    result.update(threadDesktopObservable=known,
                  threadIsDefaultDesktop=known and value.value == "Default")
    return result

def environment_ready(observation):
    # This is the known suitable installed-provider test environment. A refusal
    # is NOT evidence that the plugin or headless/native SDK hosting is broken.
    return all(observation.get(key) is True for key in
               ("windows", "threadDesktopObservable", "threadIsDefaultDesktop"))

def validate_output(root, evidence):
    if (not evidence.is_absolute() or ".." in evidence.parts or
            not evidence.is_relative_to(root / "build/windows") or evidence == root / "build/windows"):
        raise ValueError("Fresh owned build/windows child output required")
    for path in (evidence, *evidence.parents):
        try:
            info = path.lstat()
        except FileNotFoundError:
            continue
        if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & 0x400:
            raise ValueError("Output ancestry crosses reparse point; preserve it")
    if evidence.exists():
        raise ValueError("Existing output preserved; use a fresh owned folder")

def run_provider(root, binary, metadata, evidence, environment):
    from dev_support import process_identity
    if not all(p.is_absolute() for p in (binary, metadata, evidence)):
        raise SystemExit("Absolute explicit inputs only")
    validate_output(root, evidence)
    runner = root / "build/windows/VibeRaTests_artefacts/Release/VibeRaTests.exe"
    worker = runner.with_name("VibeRa Plugin Worker.exe")
    for path in (runner, worker, metadata):
        if not path.is_file():
            raise SystemExit("Required explicit runtime/metadata missing: " + str(path))
    if not binary.exists():
        raise SystemExit("Explicit provider missing; no fallback scan")
    evidence.mkdir(parents=True)
    command = [str(runner), "--installed-effect-bus-check", str(evidence / "cases"),
               str(binary), str(metadata)]
    hashes = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in (runner, worker, metadata)}
    receipt = dict(passed=False, scope="Explicit installed-effect API/bus readiness only",
                   releaseQualification=False, command=command, budgetSeconds=480,
                   listening=False, hardware=False, nativeInput=False,
                   startedUtc=datetime.datetime.now(datetime.timezone.utc).isoformat(), inputs=hashes,
                   environment=environment)
    started = time.perf_counter()
    status = 1
    with (evidence / "process.log").open("w", encoding="utf-8") as log:
        process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT,
                                   creationflags=subprocess.CREATE_NO_WINDOW)
        # Popen retains the exact Windows process HANDLE; no executable-name or
        # PID-only termination. EngineeringOwner tracks the process tree outside.
        receipt["pid"] = process.pid
        receipt["creationIdentity"] = process_identity(process.pid)
        try:
            status = process.wait(timeout=480)
            receipt["exitCode"] = status
            receipt["passed"] = status == 0
        except subprocess.TimeoutExpired:
            receipt["timedOut"] = True
            receipt["gracefulCancellation"] = "Test CLI has no graceful cancellation interface"
            receipt["termination"] = "Exact task-created retained Windows process handle after budget expiry"
            process.terminate()
            process.wait(timeout=10)
            receipt["exitCode"] = process.returncode
        finally:
            receipt["elapsedSeconds"] = time.perf_counter() - started
            receipt["inputsStable"] = all(p.is_file() and hashlib.sha256(p.read_bytes()).hexdigest() == value
                                         for name, value in hashes.items() for p in (Path(name),))
            receipt["passed"] &= receipt["inputsStable"]
            (evidence / "process.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
            print(json.dumps(dict(receipt=str(evidence / "process.json"),
                                  processPassed=receipt["passed"], seconds=receipt["elapsedSeconds"])))
    return 0 if receipt["passed"] else 1

def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("metadata", type=Path)
    parser.add_argument("evidence", type=Path)
    parser.add_argument("--preflight-only", action="store_true")
    args = parser.parse_args(argv)
    for path in (args.binary, args.metadata, args.evidence):
        if not path.is_absolute():
            parser.error("Explicit absolute inputs/output required")
    observation = observe_environment()
    preflight = dict(environment=observation, suitable=environment_ready(observation),
                     kind="installed-provider environment only", providerLaunched=False,
                     listening=False, hardware=False, nativeInput=False)
    if args.preflight_only or not preflight["suitable"]:
        print(json.dumps(preflight))
        if not preflight["suitable"]:
            print("environment_unavailable: use the authorized Default-desktop host execution path; "
                  "do not repeat installed-provider timeouts in the sandbox", file=sys.stderr)
        return 0 if preflight["suitable"] else 2
    from dev_support import EngineeringOwner
    with EngineeringOwner(ROOT, "explicit installed Mixer effect bus/API readiness"):
        return run_provider(ROOT, args.binary, args.metadata, args.evidence, observation)

if __name__ == "__main__":
    raise SystemExit(main())
