"""Affected incremental build/check runner. Never qualifies or packages a release."""
from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import time
import uuid

from dev_support import (EngineeringOwner, configure_required, content_identity, project_schema,
                         verification_inputs, compare_compiled_inputs, record_compiled_input_stability)

ROOT = Path(__file__).resolve().parents[1]


def run_owned(root, purpose, command):
    """Retain direct provider/research failures and elapsed process cost too."""
    output = root / "build/dev/owned" / (time.strftime("%Y%m%d-%H%M%S") + "-" + uuid.uuid4().hex[:12])
    output.mkdir(parents=True, exist_ok=False)
    receipt = dict(passed=False, purpose=purpose, releaseQualification=False,
                   resultAuthority="Process exit only; inspect the operation's semantic acceptance receipt")
    began = time.perf_counter()
    try:
        with EngineeringOwner(root, purpose) as owner:
            result = owner.run(command, log=output / "operation.log")
        receipt["process"] = result
        receipt["passed"] = result["exitCode"] == 0
        return result["exitCode"]
    except BaseException as error:
        receipt["launcherFailure"] = type(error).__name__
        raise
    finally:
        receipt["seconds"] = time.perf_counter() - began
        (output / "verification.json").write_text(json.dumps(receipt, indent=2), encoding="utf-8")
        print("Owned operation evidence:", output, "; process exit is not semantic qualification", flush=True)


def load_selector():
    spec = importlib.util.spec_from_file_location("dev_select", ROOT / "scripts/dev-select.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def tools():
    vswhere = Path("C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe")
    vs = subprocess.check_output([str(vswhere), "-latest", "-products", "*", "-requires",
                                  "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                                  "-property", "installationPath"], text=True).strip()
    directory = Path(vs) / "Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin"
    if not all((directory / name).is_file() for name in ("cmake.exe", "ctest.exe")):
        raise RuntimeError("Installed VS C++ CMake/CTest required; no dependency fetch attempted")
    return directory / "cmake.exe", directory / "ctest.exe"


def synchronized_test_inventory(read, expected, configure=None):
    """Refresh derived CTest registration only on observed authoritative drift.

    A test-only selection can have no MSBuild target to trigger CMake generation.
    Never accept a partial inventory or rewrite the source expectations.
    """
    inventory = read()
    if {test["name"] for test in inventory["tests"]} != set(expected) and configure is not None:
        configure()
        inventory = read()
    if {test["name"] for test in inventory["tests"]} != set(expected):
        raise RuntimeError("Configured CTest/source registration drift after allowed refresh")
    return inventory


def verification_note_integrity(root):
    """Bound the human summary before reading it; raw evidence belongs in receipts."""
    path = root / "docs/VERIFICATION.md"
    if not path.exists():
        return dict(present=False)
    size = path.stat().st_size
    if size > 1024 * 1024:
        raise ValueError("docs/VERIFICATION.md exceeds 1 MiB: repair encoding growth or move raw evidence "
                         "to receipts; never truncate verification history automatically")
    raw = path.read_bytes()
    try:
        raw.decode("utf-8-sig")
    except UnicodeDecodeError as error:
        raise ValueError("docs/VERIFICATION.md must be UTF-8; use explicit encoding for reads and writes") from error
    if b"\0" in raw:
        raise ValueError("docs/VERIFICATION.md must be UTF-8 text, not UTF-16/binary")
    longest = max(map(len, raw.splitlines()), default=0)
    if longest > 8192:
        raise ValueError("docs/VERIFICATION.md contains a line over 8192 bytes: inspect encoding "
                         "corruption/raw output and retain a concise evidence link")
    return dict(present=True, bytes=len(raw), utf8=True, longestLineBytes=longest)


def preflight(root, plan, test_specs=None):
    """Cheap source contracts; independent legacy migration/oracles stay intact."""
    note = verification_note_integrity(root)
    for path in ("CMakeLists.txt", "CMakePresets.json", "dependencies.lock.json", "src/PluginProtocol.h",
                 "src/Project.h", "docs/modules/RACK-PATTERN-SAMPLER.json", "scripts/distribution.py"):
        if not (root / path).is_file():
            raise ValueError(f"Required input missing: {path}")
    ledger = json.loads((root / "docs/modules/RACK-PATTERN-SAMPLER.json").read_text())
    if ledger.get("id") != "rack-pattern-sampler" or not ledger.get("requirements"):
        raise ValueError("Active Rack/Pattern/sampler module identity/requirements inconsistent")
    prerequisites = []
    if test_specs is not None:
        for name in plan["testNames"]:
            for script in test_specs[name]["scripts"]:
                path = root / script
                if not path.is_file():
                    raise ValueError(f"Required selected main verifier missing: {script} ({name})")
                prerequisites.append(dict(test=name, path=script,
                                          **content_identity(path, normalize_text=True)))
    schema = project_schema(root)
    # Only assertions about *new/current* projects use this helper. Genuine
    # old-schema migration fixtures retain their deliberately fixed versions.
    return dict(projectSchema=schema, workerProtocol=content_identity(root / "src/PluginProtocol.h"),
                module="rack-pattern-sampler", selectedTests=len(plan["testNames"]),
                outputPolicy="Task-owned timestamped logs in build/dev; no project overwrite; no distribution writes",
                candidateQualification=False, selectedMainPrerequisites=prerequisites,
                verificationNote=note)


def main():
    if sys.argv[1:] == ["document-check"]:
        print(json.dumps(verification_note_integrity(ROOT)))
        return 0
    if sys.argv[1:] == ["cache-state"]:
        print(json.dumps(dict(needsConfigure=configure_required(ROOT))))
        return 0
    if len(sys.argv) > 1 and sys.argv[1] == "owned":
        purpose = sys.argv[2]
        command = sys.argv[3:]
        if command and command[0] == "--":
            command = command[1:]
        if not command:
            raise ValueError("Owned command missing")
        return run_owned(ROOT, purpose, command)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--full", action="store_true")
    parser.add_argument("--additional-test", action="append", default=[])
    parser.add_argument("--files", nargs="*")
    parser.add_argument("--plan", action="store_true")
    parser.add_argument("--no-build", action="store_true", help="Requires exact compiled input snapshot")
    parser.add_argument("--configuration", choices=["Release", "Debug"], default="Release")
    args = parser.parse_args()
    selector = load_selector()
    changes = selector.git_changes(ROOT) if args.files is None else [dict(path=x, status="explicit") for x in args.files]
    plan = selector.select(ROOT, changes, args.full, args.additional_test)
    for reason in plan["reasons"]:
        print(reason["path"] + ": " + reason["reason"], flush=True)
    print("Build targets:", ", ".join(plan["buildTargets"]) or "none", flush=True)
    print("Tests:", ", ".join(plan["testNames"]) or "none", flush=True)
    receipt = dict(passed=False, releaseQualification=False, plan=plan, steps=[], preflight=preflight(ROOT, plan, selector.cmake_tests(ROOT)))
    if args.plan:
        print(json.dumps(receipt["preflight"], indent=2))
        return 0
    with EngineeringOwner(ROOT, "affected development") as owner:
        output = ROOT / "build/dev" / time.strftime("%Y%m%d-%H%M%S")
        output.mkdir(parents=True, exist_ok=False)
        started = time.perf_counter()
        verification_commands = None
        compiled_inputs = None
        verification_tools = ()
        def run(name, command):
            result = owner.run([str(x) for x in command], log=output / (name + ".log"))
            receipt["steps"].append(result)
            print(f"{name}: exit {result['exitCode']}, {result['seconds']:.3f}s, {result['log']}", flush=True)
            if result["exitCode"]:
                raise RuntimeError(f"{name} failed; retained evidence at {output}")
        try:
            # Runner/helper implementations were loaded before a potentially
            # long compilation; retain startup source independently as well.
            receipt["verificationInputsAtStart"] = verification_inputs(ROOT, [], (sys.executable,))
            if plan["testNames"]:
                cmake, ctest = tools()
                if not args.no_build:
                    run("patch-contract", ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", ROOT / "scripts/apply-dependency-patches.ps1"])
                    run("refresh-inputs", [sys.executable, ROOT / "scripts/distribution.py", "refresh-inputs"])
                    inputs = output / "inputs.json"
                    run("capture-inputs", [sys.executable, ROOT / "scripts/distribution.py", "capture", inputs])
                    if configure_required(ROOT, plan["buildTargets"]):
                        missing = [target for target in plan["buildTargets"]
                                   if not (ROOT / "build/windows" / (target + ".vcxproj")).is_file()]
                        print("Configure required:", "new targets " + ", ".join(missing)
                              if missing else "missing cache or changed preset values", flush=True)
                        run("configure", [cmake, "--preset", "windows"])
                    if plan["buildTargets"]:
                        run("compile", [cmake, "--build", "--preset", args.configuration.lower(), "--target", *plan["buildTargets"]])
                    run("compiled-inputs", [sys.executable, ROOT / "scripts/distribution.py", "check-inputs", inputs])
                    compiled_inputs = json.loads(inputs.read_text())
                else:
                    import distribution
                    proof_path = ROOT / "build/dev-compiled.json"
                    if not proof_path.is_file():
                        raise RuntimeError("--no-build requires a successful affected-runner compilation receipt")
                    proof = json.loads(proof_path.read_text())
                    if proof.get("configuration") != args.configuration or not compare_compiled_inputs(proof.get("inputs"), distribution.source_state())["stable"] or not set(plan["buildTargets"]).issubset(proof.get("targets", [])):
                        raise RuntimeError("--no-build compiled inputs/targets do not match; compile incrementally")
                    for path, identity in proof["binaries"].items():
                        if content_identity(path)["sha256"] != identity["sha256"]:
                            raise RuntimeError("--no-build binary changed: " + path)
                    compiled_inputs = proof["inputs"]
                    (output / "inputs.json").write_text(json.dumps(compiled_inputs, indent=2))
                inventory = synchronized_test_inventory(
                    lambda: json.loads(subprocess.check_output([str(ctest), "--preset", args.configuration.lower(), "--show-only=json-v1"], cwd=ROOT, text=True)),
                    selector.cmake_tests(ROOT),
                    None if args.no_build else lambda: run("refresh-test-registration", [cmake, "--preset", "windows"]))
                commands = [t for t in inventory["tests"] if t["name"] in plan["testNames"]]
                for test in commands:
                    command = test.get("command", [])
                    if not command or not Path(command[0]).is_file():
                        raise RuntimeError("Missing test executable: " + test["name"])
                    for argument in command:
                        if argument.lower().endswith(".vst3") and not Path(argument).is_file():
                            raise RuntimeError("Required original plugin fixture missing: " + argument)
                binaries = {str(Path(t["command"][0])): content_identity(t["command"][0]) for t in commands}
                for test in commands:
                    working = Path(next((p["value"] for p in test.get("properties", [])
                                         if p.get("name") == "WORKING_DIRECTORY"), str(ROOT / "build/windows")))
                    if not working.is_absolute():
                        working = ROOT / working
                    for argument in test["command"]:
                        if argument.lower().endswith(".exe"):
                            candidate = Path(argument)
                            if not candidate.is_absolute():
                                candidate = working / candidate
                            # Python/PowerShell verifiers execute a candidate
                            # passed as an argument: the interpreter alone does
                            # not identify the actual tested application bytes.
                            binaries[str(candidate)] = content_identity(candidate)
                        if argument.lower().endswith(".vst3"):
                            if Path(argument).read_bytes()[:2] != b"MZ":
                                raise RuntimeError("Windows fixture is not a native binary: " + argument)
                            binaries[argument] = content_identity(argument)
                worker = ROOT / f"build/windows/VibeRaPluginWorker_artefacts/{args.configuration}/VibeRa Plugin Worker.exe"
                if any(x in plan["buildTargets"] for x in ("VibeRa", "VibeRaTests")):
                    binaries[str(worker)] = content_identity(worker)
                    for host in ("VibeRa", "VibeRaTests"):
                        if host in plan["buildTargets"]:
                            adjacent = ROOT / f"build/windows/{host}_artefacts/{args.configuration}/VibeRa Plugin Worker.exe"
                            adjacent_identity = content_identity(adjacent)
                            if adjacent_identity["sha256"] != binaries[str(worker)]["sha256"]:
                                raise RuntimeError("Host/worker copied binary mismatch: " + str(adjacent))
                            binaries[str(adjacent)] = adjacent_identity
                if not args.no_build:
                    import distribution
                    (ROOT / "build/dev-compiled.json").write_text(json.dumps(dict(configuration=args.configuration, inputs=compiled_inputs, targets=plan["buildTargets"], binaries=binaries), indent=2))
                elif any(proof["binaries"].get(path, {}).get("sha256") != identity["sha256"] for path, identity in binaries.items()):
                    raise RuntimeError("--no-build selected executable/worker/fixture was not proven by this compilation")
                # Preserve exact candidate identity even when a test fails.
                # A later build may replace these files; failure receipts must
                # not depend on the mutable last-compilation proof.
                receipt["binaries"] = binaries
                verification_commands = commands
                verification_tools = (sys.executable, cmake, ctest)
                receipt["verificationInputs"] = verification_inputs(ROOT, commands, verification_tools)
                # Serial within the same lease: callback timing, providers and
                # GUI component tests never compete. Native input remains opt-in.
                junit = output / "ctest.xml"
                run("affected-tests", [ctest, "--preset", args.configuration.lower(), "-j", "1", "-R", plan["ctestRegex"], "--no-tests=error", "--output-junit", junit, "--output-on-failure"])
                import xml.etree.ElementTree as ET
                cases = ET.parse(junit).getroot().findall(".//testcase")
                if {x.get("name") for x in cases} != set(plan["testNames"]) or any(x.find("skipped") is not None or x.find("failure") is not None for x in cases):
                    raise RuntimeError("Actual completed CTest cases do not match selection")
            receipt["passed"] = True
            return 0
        except Exception as error:
            receipt["failure"] = str(error)
            raise
        finally:
            # A pending return0 must not escape with stale tested inputs.
            # Preserve an in-flight test/interruption exception as primary.
            primary_in_flight = sys.exc_info()[0] is not None
            stability_failure = None
            if compiled_inputs is not None:
                import distribution
                receipt["compiledInputsBaseline"] = str(output / "inputs.json")
                if not record_compiled_input_stability(receipt, compiled_inputs, distribution.source_state,
                                                        receipt.get("binaries", {})) and not primary_in_flight:
                    stability_failure = RuntimeError(receipt["failure"])
            if verification_commands is not None:
                try:
                    after = verification_inputs(ROOT, verification_commands, verification_tools)
                    receipt["verificationInputsAfter"] = after
                    before = receipt.get("verificationInputs", {})
                    identity = lambda snapshot: [(row["path"], row.get("contentSha256"), row["missing"])
                                                 for row in snapshot.get("files", [])]
                    receipt["verificationInputsStable"] = (before.get("complete", False) and after["complete"] and
                                                           identity(before) == identity(after))
                except Exception as error:
                    # Preserve the actual test failure/result; provenance capture
                    # must not replace it with a secondary snapshot exception.
                    receipt["verificationInputsAfter"] = dict(error=type(error).__name__)
                    receipt["verificationInputsStable"] = False
            if verification_commands is not None and not receipt.get("verificationInputsStable", False):
                receipt["passed"] = False
                receipt.setdefault("failure", "Verification script/tool inputs changed or could not be verified after tests")
                if not primary_in_flight and stability_failure is None:
                    stability_failure = RuntimeError(receipt["failure"])
            receipt["seconds"] = time.perf_counter() - started
            (output / "verification.json").write_text(json.dumps(receipt, indent=2), encoding="utf-8")
            print("Development evidence:", output, "; never release qualification", flush=True)
            if stability_failure is not None:
                raise stability_failure


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(json.dumps(dict(error="development_failed", message=str(error))), file=sys.stderr)
        sys.exit(1)
