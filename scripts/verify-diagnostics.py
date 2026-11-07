"""Diagnostics export acceptance (RQ-G), on an isolated --mcp-test instance.

Creates a Synth Channel (one plugin worker), provokes a typed command failure, exports the
report through vibera_project diagnostics and checks its content: version, OS, audio device,
the worker, the recent error, and no home folder or account name. Overwrite and extension
refusals are checked too.

usage: python scripts/verify-diagnostics.py [executable]
"""
import getpass
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe").resolve()
output = ROOT / "build/module-qualification/release-quality/diagnostics" / ("attempt-" + uuid.uuid4().hex)
output.mkdir(parents=True)
spec = importlib.util.spec_from_file_location("client", ROOT / "skills/vibera-studio/scripts/stdio_client.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
checks = []


def check(ok, label):
    if not ok:
        raise AssertionError(label)
    checks.append(label)


passed = False
client = module.VibeRaClient(exe)
client.process.stdin.reconfigure(newline="\n")
try:
    call = lambda tool, args: client.call(tool, args, allow_failure=True).get("structuredContent", {})
    call("vibera_project", {"action": "new", "template": "demo", "discardChanges": True})
    call("vibera_synth", {"action": "create"})
    time.sleep(4)
    kick = call("vibera_state", {})["rack"]["rows"][0]["id"]
    refused = call("vibera_command", {"command": "steps.rotate", "args": {"ids": [kick], "value": 0}})
    check(refused.get("completionState") == "failed", "A typed command failure is provoked")
    time.sleep(.5)  # Let the 20 Hz timer record the failure status.
    report_path = output / "diagnostics.txt"
    result = call("vibera_project", {"action": "diagnostics", "path": str(report_path)})
    check(result.get("completionState") == "completed" and report_path.is_file(), "The report is written: " + str(result)[:200])
    report = report_path.read_text(encoding="utf-8")
    check(result.get("report") == report, "The MCP result returns the same report")
    version = (ROOT / "build/windows/generated/VibeRaVersion.h").read_text(encoding="utf-8").split('"')[1]
    check("Version: " + version in report, "The report names the version " + version)
    check("OS: " in report and "CPU: " in report and "Memory: " in report, "The report describes the system")
    check("Audio device: " in report, "The report describes the audio device")
    check("Plugin workers: 1" in report and "VibeRa Synthesizer" in report, "The report lists the Synth plugin worker")
    check("steps.rotate.invalidValue" in report, "The report lists the recent typed error")
    home = str(Path.home())
    check(home.lower() not in report.lower() and home.replace("\\", "/").lower() not in report.lower(),
          "The report contains no home folder path")
    user = getpass.getuser()
    check(len(user) < 2 or user.lower() not in report.lower(), "The report contains no account name")
    again = call("vibera_project", {"action": "diagnostics", "path": str(report_path)})
    check(again.get("completionState") == "failed" and report_path.read_text(encoding="utf-8") == report,
          "An existing report is not replaced without overwrite")
    replaced = call("vibera_project", {"action": "diagnostics", "path": str(report_path), "overwrite": True})
    check(replaced.get("completionState") == "completed", "overwrite replaces the report")
    wrong = call("vibera_project", {"action": "diagnostics", "path": str(output / "report.json")})
    check(wrong.get("completionState") == "failed" and not (output / "report.json").exists(), "Only .txt reports are written")
    passed = True
finally:
    client.close()
    sys.path.insert(0, str(ROOT / "scripts"))
    from dev_support import content_identity
    deps = ["scripts/verify-diagnostics.py", "src/StudioDiagnostics.cpp", "src/StudioMcp.cpp"]
    receipt = dict(passed=passed, checks=checks,
                   dependencyHashes=[dict(path=d, mode="content", sha256=content_identity(ROOT / d)["contentSha256"]) for d in deps],
                   candidateHashes=[dict(path=p.relative_to(ROOT).as_posix(), sha256=hashlib.sha256(p.read_bytes()).hexdigest())
                                    for p in (exe, exe.parent / "VibeRa Plugin Worker.exe") if p.is_file() and ROOT in p.parents],
                   scope="Isolated --mcp-test instance; the Tools menu path shares writeDiagnostics")
    (output / "verification.json").write_text(json.dumps(receipt, indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks), receipt=str(output / "verification.json"))))
sys.exit(0 if passed else 1)
