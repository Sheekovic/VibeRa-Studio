"""Incremental native-effects development acceptance; never replaces dist."""
from pathlib import Path
import hashlib
import json
import os
import sys
import time

from dev import tools
from dev_support import EngineeringOwner

ROOT = Path(__file__).resolve().parents[1]


def main():
    attempt_id = str(time.time_ns())
    attempt = ROOT / "build/windows" / ("native-effects-dev-" + attempt_id)
    attempt.mkdir(parents=True)
    report = dict(passed=False, releaseQualification=False, commands=[])
    began = time.perf_counter()
    try:
        cmake, ctest = tools()
        os.environ["VIBERA_EFFECTS_ATTEMPT"] = attempt_id
        def inputs():
            paths = [ROOT / "CMakeLists.txt"]
            for folder in ("src", "tests", "scripts"):
                paths.extend(p for p in (ROOT / folder).rglob("*")
                             if p.is_file() and p.suffix in (".h", ".cpp", ".inc", ".py", ".ps1"))
            return {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
        with EngineeringOwner(ROOT, "native effects incremental development") as owner:
            report["inputs"] = inputs()
            commands = [
                [str(cmake), "--preset", "windows"],
                [str(cmake), "--build", "build/windows", "--config", "Release", "--target",
                 "VibeRa", "VibeRaTests", "VibeRaEffectsDspTests", "VibeRaNativeEffectsTests", "-j4"],
                [str(ctest), "--test-dir", "build/windows", "-C", "Release", "-j1",
                 "--output-on-failure", "--no-tests=error", "--output-junit", str(attempt / "ctest.xml"),
                 "-R", "^(vibera_effects_dsp|vibera_native_effects|vibera_native_effect_model|vibera_native_effects_mcp|mcp_semantics|plugin_catalogue_gui|plugin_editor_requests|plugin_editor_attachment|plugin_wrapper_gui)$"],
            ]
            for index, command in enumerate(commands):
                result = owner.run(command, cwd=ROOT, log=attempt / f"step-{index}.log")
                report["commands"].append(result)
                if result["exitCode"]:
                    return result["exitCode"]
            report["inputsStable"] = report["inputs"] == inputs()
            if not report["inputsStable"]:
                raise RuntimeError("Source changed during acceptance; rerun affected checks")
        report["passed"] = True
        return 0
    except Exception as error:
        report["error"] = str(error)
        print(str(error), flush=True)
        return 1
    finally:
        report["seconds"] = time.perf_counter() - began
        (attempt / "verification.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
        print("Native effects development receipt:", attempt, flush=True)


if __name__ == "__main__":
    raise SystemExit(main())
