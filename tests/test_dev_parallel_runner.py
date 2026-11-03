"""Original runner checks: mocked orchestration and tiny in-process unit suites only."""
from contextlib import redirect_stderr, redirect_stdout
import copy
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import tempfile
import types
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("development_parallel_runner", ROOT / "scripts/run-dev-workflow.py")
runner = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(runner)

INVENTORY = ["test_dev_forward_guard.Original.test_guard", "test_dev_workflow.Original.test_workflow",
             "test_dev_plugin_final_output_guard.Original.test_contract",
             "test_dev_summing_guard.Original.test_reconstruction",
             "test_dev_send_block_guard.Original.test_send",
             "test_dev_plugin_final_output_guard.PluginFinalOutputGuardTests."
             "test_exact_six_successors_history_and_known_successors",
             "test_dev_plugin_final_output_guard.PluginFinalOutputGuardTests."
             "test_vendor_authority_mutations_and_ancestor_successors",
             "test_dev_brand_new_module.Original.test_future"]


def good_receipts():
    rows = {}
    for group in runner.GROUPS:
        selected = [item for item in INVENTORY if runner.group_for(item) == group]
        rows[group] = dict(group=group, status="completed", discovered=INVENTORY.copy(),
                           selected=selected, executed=selected.copy(), testsRun=len(selected),
                           failures=[], errors=[], skipped=[], expectedFailures=[],
                           unexpectedSuccesses=[], unittestSuccessful=True, exitCode=0)
    return rows


class ParallelRunnerTests(unittest.TestCase):
    def setUp(self):
        parent = ROOT / "build/dev-parallel-unit"
        parent.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix="owned mock ", dir=parent)
        self.output = Path(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()

    def simulate(self, rows=None, codes=None, launch_fail=None, missing=None):
        rows = copy.deepcopy(rows if rows is not None else good_receipts())
        codes = codes or {}
        events = []
        class OwnedFakeProcess:
            def __init__(process, group):
                process.pid = 700 + len(events)
                process.group = group
            def wait(process):
                events.append(("wait", process.group))
                return codes.get(process.group, rows[process.group]["exitCode"])
        def launch(command, *, cwd, stdout, stderr):
            self.assertEqual(cwd, ROOT)
            self.assertEqual(stderr, subprocess.STDOUT)
            group = command[command.index("--worker") + 1]
            events.append(("launch", group))
            if launch_fail == group:
                raise OSError("original owned launch failure")
            stdout.write(("original log " + group + "\n").encode())
            if group != missing:
                runner.write_receipt(Path(command[command.index("--receipt") + 1]), rows[group])
            return OwnedFakeProcess(group)
        with redirect_stdout(io.StringIO()) as text, redirect_stderr(io.StringIO()) as errors:
            code = runner.orchestrate(ROOT, self.output, popen=launch)
        receipt = json.loads((self.output / "verification.json").read_text(encoding="utf-8"))
        return code, receipt, events, text.getvalue(), errors.getvalue()

    def test_two_children_start_before_wait_complete_exact_inventory_and_ordered_logs(self):
        code, receipt, events, text, _ = self.simulate()
        # Historical case ID retained; now asserts the exact bounded5 contract.
        self.assertEqual(events, [("launch", "source-guards"), ("launch", "workflow"),
                                  ("launch", "native-contracts"), ("launch", "stage-contracts"),
                                  ("launch", "support-contracts"),
                                  ("wait", "source-guards"), ("wait", "workflow"),
                                  ("wait", "native-contracts"), ("wait", "stage-contracts"),
                                  ("wait", "support-contracts")])
        self.assertEqual(code, 0)
        self.assertTrue(receipt["passed"])
        self.assertEqual(receipt["maxConcurrentWorkers"], 5)
        self.assertEqual(runner.group_for("test_dev_summing_guard.Original.test_guard"), "support-contracts")
        self.assertEqual(runner.group_for("test_dev_plugin_final_output_guard.Original.test_guard"), "native-contracts")
        self.assertEqual(runner.group_for("test_dev_native_midi_ingress_guard.Original.test_guard"), "workflow")
        self.assertEqual(runner.group_for("test_dev_stage_difference_guard.Original.test_guard"), "support-contracts")
        self.assertEqual(runner.group_for("test_dev_send_block_guard.Original.test_guard"), "support-contracts")
        self.assertEqual(runner.group_for("test_dev_local_source_guard.Original.test_guard"), "native-contracts")
        self.assertEqual(runner.group_for("test_dev_parallel_runner.Original.test_guard"), "stage-contracts")
        self.assertEqual(runner.group_for("test_dev_source_stability.Original.test_guard"), "stage-contracts")
        self.assertEqual(runner.group_for("test_dev_fixture_seed.Original.test_guard"), "source-guards")
        self.assertEqual(runner.group_for("test_dev_guard_transform_cache.Original.test_guard"), "stage-contracts")
        self.assertEqual(runner.group_for("test_dev_workflow.Original.test_workflow"), "source-guards")
        self.assertEqual(runner.group_for("test_dev_selection.Original.test_selection"), "workflow")
        self.assertEqual(len(runner.GROUPS), runner.MAX_CONCURRENT_WORKERS)
        self.assertEqual(receipt["discovered"], INVENTORY)
        self.assertLess(text.index("original log source-guards"), text.index("original log workflow"))
        self.assertLess(text.index("original log workflow"), text.index("original log native-contracts"))
        self.assertLess(text.index("original log native-contracts"), text.index("original log stage-contracts"))
        self.assertLess(text.index("original log stage-contracts"), text.index("original log support-contracts"))
        self.assertEqual((self.output / "support-contracts.log").read_text(), "original log support-contracts\n")
        self.assertEqual((self.output / "stage-contracts.log").read_text(), "original log stage-contracts\n")
        self.assertEqual((self.output / "native-contracts.log").read_text(), "original log native-contracts\n")
        self.assertEqual((self.output / "workflow.log").read_text(), "original log workflow\n")
        self.assertFalse(receipt["releaseQualification"])

    def test_nonzero_actual_exit_preserved_without_abandoning_successful_peer(self):
        rows = good_receipts()
        rows["source-guards"].update(exitCode=7, unittestSuccessful=False, failures=[INVENTORY[0]])
        code, receipt, events, text, _ = self.simulate(rows)
        self.assertEqual(code, 7)
        self.assertFalse(receipt["passed"])
        self.assertEqual(events[-1], ("wait", "support-contracts"))
        self.assertIn(("wait", "stage-contracts"), events)
        self.assertIn("original log source-guards", text)
        self.assertIn("original log workflow", text)
        self.output = self.output / "third-failure"
        self.output.mkdir()
        rows = good_receipts()
        rows["native-contracts"].update(exitCode=8, unittestSuccessful=False, failures=[INVENTORY[2]])
        code, receipt, events, text, _ = self.simulate(rows)
        self.assertEqual(code, 8)
        self.assertFalse(receipt["passed"])
        self.assertEqual([event for event in events if event[0] == "wait"],
                         [("wait", group) for group in runner.GROUPS])
        self.assertIn("original log native-contracts", text)
        self.output = self.output / "fourth-failure"
        self.output.mkdir()
        rows = good_receipts()
        rows["stage-contracts"].update(exitCode=12, unittestSuccessful=False, failures=[INVENTORY[-2]])
        code, receipt, events, text, _ = self.simulate(rows)
        self.assertEqual(code, 12)
        self.assertFalse(receipt["passed"])
        self.assertEqual([event for event in events if event[0] == "wait"],
                         [("wait", group) for group in runner.GROUPS])
        self.assertIn("original log stage-contracts", text)

        self.output = self.output / "fifth-failure"
        self.output.mkdir()
        rows = good_receipts()
        rows["support-contracts"].update(exitCode=14, unittestSuccessful=False, failures=[INVENTORY[3]])
        code, receipt, events, text, _ = self.simulate(rows)
        self.assertEqual(code, 14)
        self.assertFalse(receipt["passed"])
        self.assertEqual([event for event in events if event[0] == "wait"],
                         [("wait", group) for group in runner.GROUPS])
        self.assertIn("original log support-contracts", text)

    def test_process_exit_cannot_be_overridden_by_success_receipt(self):
        code, receipt, _, _, errors = self.simulate(codes={"workflow": 9})
        self.assertEqual(code, 9)
        self.assertFalse(receipt["passed"])
        self.assertIn("actual process exit and receipt disagree", errors)
        self.output = self.output / "third-exit-mismatch"
        self.output.mkdir()
        code, receipt, events, _, errors = self.simulate(codes={"native-contracts": 11})
        self.assertEqual(code, 11)
        self.assertFalse(receipt["passed"])
        self.assertIn("actual process exit and receipt disagree", errors)
        self.assertEqual([event for event in events if event[0] == "wait"],
                         [("wait", group) for group in runner.GROUPS])

        self.output = self.output / "fourth-exit-mismatch"
        self.output.mkdir()
        code, receipt, events, _, errors = self.simulate(codes={"stage-contracts": 13})
        self.assertEqual(code, 13)
        self.assertFalse(receipt["passed"])
        self.assertIn("actual process exit and receipt disagree", errors)
        self.assertEqual([event for event in events if event[0] == "wait"],
                         [("wait", group) for group in runner.GROUPS])

        self.output = self.output / "fifth-exit-mismatch"
        self.output.mkdir()
        code, receipt, events, _, errors = self.simulate(codes={"support-contracts": 15})
        self.assertEqual(code, 15)
        self.assertFalse(receipt["passed"])
        self.assertIn("actual process exit and receipt disagree", errors)
        self.assertEqual([event for event in events if event[0] == "wait"],
                         [("wait", group) for group in runner.GROUPS])

    def test_launcher_failure_still_waits_for_already_running_peer(self):
        code, receipt, events, _, errors = self.simulate(launch_fail="workflow")
        self.assertEqual(code, 2)
        self.assertFalse(receipt["passed"])
        self.assertEqual([event for event in events if event[0] == "wait"],
                         [("wait", "source-guards"), ("wait", "native-contracts"), ("wait", "stage-contracts"), ("wait", "support-contracts")])
        self.assertIn("original owned launch failure", errors)
        self.output = self.output / "third-launch-failure"
        self.output.mkdir()
        code, receipt, events, _, errors = self.simulate(launch_fail="native-contracts")
        self.assertEqual(code, 2)
        self.assertFalse(receipt["passed"])
        self.assertEqual([event for event in events if event[0] == "wait"],
                         [("wait", "source-guards"), ("wait", "workflow"), ("wait", "stage-contracts"), ("wait", "support-contracts")])
        self.assertEqual([event for event in events if event[0] == "launch"],
                         [("launch", group) for group in runner.GROUPS])
        self.assertIn("original owned launch failure", errors)

        self.output = self.output / "fourth-launch-failure"
        self.output.mkdir()
        code, receipt, events, _, errors = self.simulate(launch_fail="stage-contracts")
        self.assertEqual(code, 2)
        self.assertFalse(receipt["passed"])
        self.assertEqual([event for event in events if event[0] == "wait"],
                         [("wait", "source-guards"), ("wait", "workflow"), ("wait", "native-contracts"), ("wait", "support-contracts")])
        self.assertEqual([event for event in events if event[0] == "launch"],
                         [("launch", group) for group in runner.GROUPS])
        self.assertIn("original owned launch failure", errors)

        self.output = self.output / "fifth-launch-failure"
        self.output.mkdir()
        code, receipt, events, _, errors = self.simulate(launch_fail="support-contracts")
        self.assertEqual(code, 2)
        self.assertFalse(receipt["passed"])
        self.assertEqual([event for event in events if event[0] == "wait"],
                         [("wait", group) for group in runner.GROUPS if group != "support-contracts"])
        self.assertEqual([event for event in events if event[0] == "launch"],
                         [("launch", group) for group in runner.GROUPS])
        self.assertIn("original owned launch failure", errors)

    def test_missing_receipt_or_different_discovery_never_qualifies(self):
        code, receipt, _, _, _ = self.simulate(missing="workflow")
        self.assertEqual(code, 2)
        self.assertFalse(receipt["passed"])
        rows = good_receipts()
        rows["workflow"]["discovered"] = rows["workflow"]["discovered"][:-1]
        # Second simulation needs its own exclusive mocked output, retaining the first.
        self.output = self.output / "second"
        self.output.mkdir()
        code, receipt, _, _, errors = self.simulate(rows)
        self.assertEqual(code, 2)
        self.assertFalse(receipt["passed"])
        self.assertIn("discovery inventories disagree", errors)
        self.output = self.output / "third-missing"
        self.output.mkdir()
        code, receipt, events, _, errors = self.simulate(missing="native-contracts")
        self.assertEqual(code, 2)
        self.assertFalse(receipt["passed"])
        self.assertEqual([event for event in events if event[0] == "wait"],
                         [("wait", group) for group in runner.GROUPS])
        self.assertIn("native-contracts receipt", errors)
        self.output = self.output / "fourth-missing"
        self.output.mkdir()
        code, receipt, events, _, errors = self.simulate(missing="stage-contracts")
        self.assertEqual(code, 2)
        self.assertFalse(receipt["passed"])
        self.assertEqual([event for event in events if event[0] == "wait"],
                         [("wait", group) for group in runner.GROUPS])
        self.assertIn("stage-contracts receipt", errors)

        self.output = self.output / "fifth-missing"
        self.output.mkdir()
        code, receipt, events, _, errors = self.simulate(missing="support-contracts")
        self.assertEqual(code, 2)
        self.assertFalse(receipt["passed"])
        self.assertEqual([event for event in events if event[0] == "wait"],
                         [("wait", group) for group in runner.GROUPS])
        self.assertIn("support-contracts receipt", errors)

    def test_unexecuted_duplicate_and_cross_group_cases_are_rejected(self):
        for change in ("missing execution", "duplicate discovery", "wrong group", "expected failure", "false zero", "third missing execution", "third wrong group",
                       "fourth missing execution", "fourth wrong group", "fourth discovery", "fourth skip",
                       "fifth missing execution", "fifth wrong group", "fifth discovery", "fifth skip"):
            with self.subTest(change=change):
                rows = good_receipts()
                if change == "missing execution":
                    rows["workflow"]["executed"] = rows["workflow"]["executed"][:-1]
                elif change == "duplicate discovery":
                    rows["source-guards"]["discovered"].append(INVENTORY[0])
                elif change == "wrong group":
                    rows["workflow"]["selected"].append(INVENTORY[0])
                elif change == "expected failure":
                    rows["workflow"]["expectedFailures"] = [INVENTORY[1]]
                elif change == "third missing execution":
                    rows["native-contracts"]["executed"] = []
                elif change == "third wrong group":
                    rows["native-contracts"]["selected"].append(INVENTORY[0])
                elif change == "fourth missing execution":
                    rows["stage-contracts"]["executed"] = []
                elif change == "fourth wrong group":
                    rows["stage-contracts"]["selected"].append(INVENTORY[0])
                elif change == "fourth discovery":
                    rows["stage-contracts"]["discovered"] = INVENTORY[:-1]
                elif change == "fourth skip":
                    rows["stage-contracts"]["skipped"] = [INVENTORY[-2]]
                elif change == "fifth missing execution":
                    rows["support-contracts"]["executed"] = []
                elif change == "fifth wrong group":
                    rows["support-contracts"]["selected"].append(INVENTORY[0])
                elif change == "fifth discovery":
                    rows["support-contracts"]["discovered"] = INVENTORY[:-1]
                elif change == "fifth skip":
                    rows["support-contracts"]["skipped"] = [INVENTORY[3]]
                else:
                    rows["workflow"]["failures"] = [INVENTORY[1]]
                with self.assertRaises(ValueError):
                    runner.validate_receipts(rows)

    def test_future_module_is_included_and_skip_cannot_be_success(self):
        # Real split-case IDs are disjoint and participate in full discovery;
        # a new unrelated module still cannot disappear from default workflow.
        history,mutations=INVENTORY[-3:-1]
        self.assertEqual(runner.CASE_GROUPS, {history: "native-contracts", mutations: "stage-contracts"})
        self.assertEqual(runner.group_for(history), "native-contracts")
        self.assertEqual(runner.group_for(mutations), "stage-contracts")
        self.assertEqual(runner.MAX_CONCURRENT_WORKERS, 5)
        rows = good_receipts()
        self.assertEqual(runner.validate_receipts(rows), INVENTORY)
        for identity,group in ((history,"native-contracts"),(mutations,"stage-contracts")):
            self.assertIn(identity,rows[group]["executed"])
            self.assertEqual(sum(identity in row["executed"] for row in rows.values()),1)
            missing=copy.deepcopy(rows)
            missing[group]["executed"].remove(identity)
            with self.assertRaisesRegex(ValueError, "not every selected scenario"):
                runner.validate_receipts(missing)
        self.assertIn(INVENTORY[-1], rows["workflow"]["selected"])
        rows["workflow"]["skipped"] = [INVENTORY[-1]]
        with self.assertRaisesRegex(ValueError, "success conflicts"):
            runner.validate_receipts(rows)

    def run_tiny_worker(self, failing=False, skipped=False, duplicates=False):
        class OriginalCase(unittest.TestCase):
            def id(case):
                return "test_dev_workflow.Tiny.test_original"
            def runTest(case):
                if failing:
                    case.fail("original unit failure must remain a failure")
                if skipped:
                    case.skipTest("original tiny skipped scenario")
        class GuardCase(unittest.TestCase):
            def id(case):
                return "test_dev_plugin_final_output_guard.Tiny.test_original"
            def runTest(case):
                pass
        cases = [GuardCase(), OriginalCase()]
        if duplicates:
            cases.append(OriginalCase())
        seen = []
        def discover(start_dir, *, pattern):
            seen.append((start_dir, pattern))
            return unittest.TestSuite([unittest.TestSuite(cases)])
        receipt_path = self.output / "tiny.json"
        with redirect_stderr(io.StringIO()) as log:
            code = runner.run_group(ROOT, "source-guards", receipt_path,
                                    loader=types.SimpleNamespace(discover=discover))
        return code, json.loads(receipt_path.read_text()), log.getvalue(), seen

    def test_worker_uses_real_unittest_result_and_preserves_original_failure(self):
        code, row, log, seen = self.run_tiny_worker(failing=True)
        self.assertEqual(code, 1)
        self.assertFalse(row["unittestSuccessful"])
        self.assertEqual(row["testsRun"], 1)
        self.assertEqual(row["executed"], row["selected"])
        self.assertEqual(row["failures"], ["test_dev_workflow.Tiny.test_original"])
        self.assertIn("original unit failure must remain a failure", log)
        self.assertEqual([case["id"] for case in row["caseSeconds"]], row["executed"])
        self.assertGreaterEqual(row["caseSeconds"][0]["seconds"], 0)
        self.assertEqual(row["moduleSeconds"]["test_dev_workflow"], row["caseSeconds"][0]["seconds"])
        self.assertEqual(seen, [(str(ROOT / "tests"), "test_dev_*.py")])
        self.assertEqual(len(row["discovered"]), 2)

    def test_worker_success_skip_and_duplicate_inventory_are_explicit(self):
        # Independent runner-local clock; never patch unittest's/global time.
        ticks = iter((10.0, 11.0, 12.25, 14.0))
        with patch.object(runner, "time", types.SimpleNamespace(perf_counter=lambda: next(ticks))):
            code, row, _, _ = self.run_tiny_worker()
        self.assertEqual(row["seconds"], 4.0)
        self.assertEqual(row["caseSeconds"], [{"id": "test_dev_workflow.Tiny.test_original", "seconds": 1.25}])
        self.assertEqual(row["moduleSeconds"], {"test_dev_workflow": 1.25})
        self.assertEqual(code, 0)
        self.assertTrue(row["unittestSuccessful"])
        code, row, _, _ = self.run_tiny_worker(skipped=True)
        self.assertEqual(code, 1)
        self.assertEqual(row["skipped"], ["test_dev_workflow.Tiny.test_original"])
        code, row, _, _ = self.run_tiny_worker(duplicates=True)
        self.assertEqual(code, 2)
        self.assertIn("duplicate", row["error"])


if __name__ == "__main__":
    unittest.main()
