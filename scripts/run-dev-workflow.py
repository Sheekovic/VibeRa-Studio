"""Run the complete development unittest inventory in five isolated processes.

There are no retries, internal timeouts, in-process parallel tests or application
launches. CTest applies a 60-second outer deadline (dev tooling only). Each worker does
normal unittest discovery, and the parent checks full, disjoint execution.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import unittest
import uuid

# Fixed bound: five light unittest workers, never application/audio/build jobs.
GROUPS = ("source-guards", "workflow", "native-contracts", "stage-contracts", "support-contracts")
MAX_CONCURRENT_WORKERS = 5
# Current167-case four-worker run timed out with completed peers12.43/14.26/14.83s.
# Preserve the existing independent final-output case split and move whole
# support modules into a fifth group. Actual five-worker cost is unqualified;
# all discovery, outcome, fresh guard observations and the15s deadline remain.
GUARD_MODULES = frozenset({"test_dev_forward_guard", "test_dev_fixture_seed", "test_dev_workflow"})
CONTRACT_MODULES = frozenset({"test_dev_plugin_final_output_guard", "test_dev_local_source_guard"})
STAGE_MODULES = frozenset({"test_dev_guard_transform_cache", "test_dev_source_stability", "test_dev_parallel_runner"})
SUPPORT_MODULES = frozenset({"test_dev_summing_guard", "test_dev_stage_difference_guard", "test_dev_send_block_guard"})
CASE_GROUPS = {
    "test_dev_plugin_final_output_guard.PluginFinalOutputGuardTests."
    "test_exact_six_successors_history_and_known_successors": "native-contracts",
    "test_dev_plugin_final_output_guard.PluginFinalOutputGuardTests."
    "test_vendor_authority_mutations_and_ancestor_successors": "stage-contracts",
}
PATTERN = "test_dev_*.py"


def test_cases(suite):
    for item in suite:
        if isinstance(item, unittest.TestSuite):
            yield from test_cases(item)
        else:
            yield item


def group_for(test_id):
    if test_id in CASE_GROUPS:
        return CASE_GROUPS[test_id]
    module = test_id.split(".", 1)[0]
    if module in GUARD_MODULES:
        return "source-guards"
    if module in STAGE_MODULES:
        return "stage-contracts"
    if module in SUPPORT_MODULES:
        return "support-contracts"
    return "native-contracts" if module in CONTRACT_MODULES else "workflow"


def write_receipt(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


class RecordedResult(unittest.TextTestResult):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.executed = []
        self.caseSeconds = []

    def startTest(self, test):
        self.executed.append(test.id())
        super().startTest(test)
        self.startedAt = time.perf_counter()

    def stopTest(self, test):
        self.caseSeconds.append({"id": test.id(), "seconds": time.perf_counter() - self.startedAt})
        super().stopTest(test)


def run_group(root, group, receipt_path, loader=None):
    """One process, standard discovery/order/fixtures, real unittest result."""
    receipt = {"group": group, "status": "failed", "pid": os.getpid()}
    started = time.perf_counter()
    try:
        loader = loader or unittest.TestLoader()
        cases = list(test_cases(loader.discover(str(root / "tests"), pattern=PATTERN)))
        inventory = [test.id() for test in cases]
        if len(inventory) != len(set(inventory)) or not inventory:
            raise ValueError("Empty or duplicate development unittest inventory")
        selected = [test for test in cases if group_for(test.id()) == group]
        receipt.update(discovered=inventory, selected=[test.id() for test in selected])
        result = unittest.TextTestRunner(verbosity=2, resultclass=RecordedResult).run(unittest.TestSuite(selected))
        # A skipped case is not execution of the required development scenario.
        code = 0 if result.wasSuccessful() and not result.skipped and not result.expectedFailures else 1
        moduleSeconds = {}
        for case in result.caseSeconds:
            module = case["id"].split(".", 1)[0]
            moduleSeconds[module] = moduleSeconds.get(module, 0.0) + case["seconds"]
        receipt.update(status="completed", executed=result.executed, testsRun=result.testsRun,
                       caseSeconds=result.caseSeconds, moduleSeconds=moduleSeconds,
                       failures=[test.id() for test, _ in result.failures],
                       errors=[test.id() for test, _ in result.errors],
                       skipped=[test.id() for test, _ in result.skipped],
                       expectedFailures=[test.id() for test, _ in result.expectedFailures],
                       unexpectedSuccesses=[test.id() for test in result.unexpectedSuccesses],
                       unittestSuccessful=result.wasSuccessful(), exitCode=code)
    except Exception as error:
        code = 2
        receipt.update(error=f"{type(error).__name__}: {error}", exitCode=code)
        print(receipt["error"], file=sys.stderr)
    receipt["seconds"] = time.perf_counter() - started
    write_receipt(receipt_path, receipt)
    return code


def validate_receipts(rows):
    """Do not infer coverage or success from a child printing 'OK'."""
    discovered = None
    executed = []
    for group in GROUPS:
        row = rows[group]
        if row.get("group") != group or row.get("status") != "completed":
            raise ValueError(f"{group}: missing terminal unittest receipt")
        ids = row["discovered"]
        if not ids or len(ids) != len(set(ids)) or not all(isinstance(item, str) for item in ids):
            raise ValueError(f"{group}: invalid discovery inventory")
        if discovered is None:
            discovered = ids
        elif discovered != ids:
            raise ValueError("Child discovery inventories disagree; source/import drift")
        expected = [item for item in ids if group_for(item) == group]
        if row["selected"] != expected:
            raise ValueError(f"{group}: incomplete or misassigned selection")
        if row["executed"] != expected or row["testsRun"] != len(expected):
            raise ValueError(f"{group}: not every selected scenario executed exactly once")
        if row["exitCode"] == 0 and (not row["unittestSuccessful"] or row["skipped"] or
                                     row["failures"] or row["errors"] or row["expectedFailures"] or row["unexpectedSuccesses"]):
            raise ValueError(f"{group}: success conflicts with unittest outcomes")
        executed += row["executed"]
    if len(executed) != len(discovered) or set(executed) != set(discovered):
        raise ValueError("Child execution is not the exact full development inventory")
    return discovered


def orchestrate(root, output, *, popen=None, script=None):
    """Launch at most five children, retain all outcomes, print in fixed order."""
    popen = popen or subprocess.Popen
    script = script or Path(__file__).resolve()
    started = time.perf_counter()
    processes, rows, problems = {}, {}, []
    receipt = {"passed": False, "releaseQualification": False, "groups": {},
               "pattern": PATTERN, "maxConcurrentWorkers": MAX_CONCURRENT_WORKERS}
    for group in GROUPS:
        log_path, child_receipt = output / f"{group}.log", output / f"{group}.json"
        try:
            with log_path.open("wb") as log:
                process = popen([sys.executable, "-u", str(script), "--worker", group,
                                 "--receipt", str(child_receipt)], cwd=root,
                                stdout=log, stderr=subprocess.STDOUT)
            processes[group] = process
            receipt["groups"][group] = {"pid": process.pid, "log": str(log_path),
                                        "receipt": str(child_receipt)}
        except Exception as error:
            problems.append(f"{group} launch: {type(error).__name__}: {error}")
    # Always wait for every launched peer: no failure cancellation, retry or truncation.
    for group, process in processes.items():
        code = process.wait()
        receipt["groups"][group]["exitCode"] = code
        try:
            rows[group] = json.loads((output / f"{group}.json").read_text(encoding="utf-8"))
            for metric in ("seconds", "caseSeconds", "moduleSeconds"):
                receipt["groups"][group][metric] = rows[group].get(metric)
            if rows[group].get("exitCode") != code:
                raise ValueError("actual process exit and receipt disagree")
        except (OSError, ValueError, TypeError) as error:
            problems.append(f"{group} receipt: {type(error).__name__}: {error}")
    try:
        receipt["discovered"] = validate_receipts(rows)
    except (KeyError, ValueError, TypeError) as error:
        problems.append(f"coverage: {type(error).__name__}: {error}")
    # Preserve the real first nonzero child exit independently of receipt issues.
    child_codes = [receipt["groups"][g].get("exitCode", 0) for g in GROUPS if g in receipt["groups"]]
    code = next((value for value in child_codes if value != 0), 2 if problems else 0)
    receipt.update(passed=code == 0, exitCode=code, problems=problems,
                   seconds=time.perf_counter() - started)
    write_receipt(output / "verification.json", receipt)
    for group in GROUPS:
        log = output / f"{group}.log"
        print(f"\n--- {group} ---", flush=True)
        if log.exists():
            print(log.read_bytes().decode("utf-8", errors="replace"), end="", flush=True)
    for problem in problems:
        print(problem, file=sys.stderr)
    print(f"Development workflows: {len(receipt.get('discovered', []))} discovered; "
          f"exit {code}; {receipt['seconds']:.3f}s; evidence {output}", flush=True)
    return code


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--worker", choices=GROUPS, help=argparse.SUPPRESS)
    parser.add_argument("--receipt", type=Path, help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    root = Path(__file__).resolve().parents[1]
    if args.worker:
        if args.receipt is None:
            parser.error("Worker requires an owned receipt path")
        return run_group(root, args.worker, args.receipt)
    if args.receipt is not None:
        parser.error("Receipt path is an internal worker argument")
    output = root / "build/development-workflow" / ("attempt-" + uuid.uuid4().hex)
    output.mkdir(parents=True, exist_ok=False)
    print(f"Development workflow evidence: {output}", flush=True)
    return orchestrate(root, output)


if __name__ == "__main__":
    sys.exit(main())
