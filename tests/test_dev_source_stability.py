"""Disposable source/receipt runner tests; no real build, provider or test process."""
from pathlib import Path
import copy
import io
from contextlib import redirect_stdout
import json
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import dev as runner
import distribution
from dev_support import compare_compiled_inputs, content_identity, record_compiled_input_stability


class FinalCompiledInputsTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="vibera-owned-source-inputs-")
        self.root = Path(self.temporary.name)
        for name in ("CMakeLists.txt", "CMakePresets.json", "dependencies.lock.json",
                     "src/PluginProtocol.h", "scripts/distribution.py", *distribution.BUILD_GUARD_INPUTS):
            file = self.root / name
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_text("original disposable prerequisite\n")
        (self.root / "src/Project.h").write_text("struct Project { static constexpr int currentVersion = 173; };\n")
        self.verifier = self.root / "scripts/owned_verifier.py"
        self.verifier.write_bytes(b"original = 42\r\n")
        self.source = self.root / "src/source with spaces.cpp"
        self.source.write_bytes(b"int value = 42;\r\n")
        self.asset = self.root / "assets/original.bin"
        self.asset.parent.mkdir()
        self.asset.write_bytes(b"ORIGINAL\r\n")
        ledger = self.root / "docs/modules/RACK-PATTERN-SAMPLER.json"
        ledger.parent.mkdir(parents=True)
        ledger.write_text(json.dumps({"id": "rack-pattern-sampler", "requirements": [{"id": "original"}]}))
        self.binary = self.root / "build/windows/original owned test.exe"
        self.binary.parent.mkdir(parents=True)
        self.binary.write_bytes(b"MZ-original-disposable-identity\r\n")
        # This fixture never launches a process; bind an owned stand-in, not the real Python installation.
        self.interpreter = self.binary.with_name("original owned interpreter.exe")
        self.interpreter.write_bytes(b"MZ-original-disposable-interpreter\r\n")

    def tearDown(self):
        self.temporary.cleanup()

    def snapshot(self):
        with patch.object(distribution, "ROOT", self.root):
            return distribution.source_state()

    def test_authoritative_snapshot_uses_lf_text_and_exact_asset_bytes(self):
        before = self.snapshot()
        self.assertTrue(compare_compiled_inputs(before, self.snapshot())["stable"])
        self.source.write_bytes(self.source.read_bytes().replace(b"\r\n", b"\n"))
        self.assertTrue(compare_compiled_inputs(before, self.snapshot())["stable"])
        self.asset.write_bytes(self.asset.read_bytes().replace(b"\r\n", b"\n"))
        difference = compare_compiled_inputs(before, self.snapshot())
        self.assertFalse(difference["stable"])
        self.assertEqual([row["path"] for row in difference["changed"]], ["assets/original.bin"])

    def test_added_removed_and_substantive_source_are_visible(self):
        before = self.snapshot()
        self.source.write_bytes(b"int value = 43;\n")
        self.asset.unlink()
        (self.root / "src/new.cpp").write_text("int newSource;\n")
        difference = compare_compiled_inputs(before, self.snapshot())
        self.assertEqual(difference["added"], ["src/new.cpp"])
        self.assertEqual(difference["removed"], ["assets/original.bin"])
        self.assertEqual([x["path"] for x in difference["changed"]], ["src/source with spaces.cpp"])

    def test_unknown_snapshot_does_not_overwrite_primary_failure(self):
        receipt = dict(passed=False, failure="Original CTest failed; retained original failure evidence")
        baseline = self.snapshot()
        def unavailable():
            raise PermissionError("original disposable read uncertainty")
        self.assertFalse(record_compiled_input_stability(receipt, baseline, unavailable, {}))
        self.assertEqual(receipt["failure"], "Original CTest failed; retained original failure evidence")
        self.assertEqual(receipt["compiledInputsCheck"]["source"]["unavailable"], "PermissionError")
        bad = copy.deepcopy(baseline)
        bad.append(copy.deepcopy(bad[0]))
        with self.assertRaisesRegex(ValueError, "duplicate"):
            compare_compiled_inputs(bad, baseline)

    def simulate_no_build(self, mutate=lambda: None, failed=False, initial_eol_change=False,
                          no_build=True, change_after_compile=False, include_host_worker=False,
                          wrapped_candidate=False, missing_candidate_proof=False):
        """Run real dev.main control/finally logic, replacing only OS/build boundaries."""
        plan = dict(reasons=[], buildTargets=["VibeRaCompensationTests"], testNames=["owned_unit"],
                    ctestRegex="^owned_unit$")
        baseline = self.snapshot()
        binaries = {str(self.binary): content_identity(self.binary)}
        if wrapped_candidate:
            binaries[str(self.interpreter)] = content_identity(self.interpreter)
            if missing_candidate_proof:
                del binaries[str(self.binary)]
        if include_host_worker:
            plan["buildTargets"] = ["VibeRaTests"]
            central = self.root / "build/windows/VibeRaPluginWorker_artefacts/Release/VibeRa Plugin Worker.exe"
            self.adjacent_worker = self.root / "build/windows/VibeRaTests_artefacts/Release/VibeRa Plugin Worker.exe"
            for worker in (central, self.adjacent_worker):
                worker.parent.mkdir(parents=True, exist_ok=True)
                worker.write_bytes(b"MZ-identical-owned-worker\n")
                binaries[str(worker)] = content_identity(worker)
        proof = dict(configuration="Release", inputs=baseline, targets=plan["buildTargets"], binaries=binaries)
        proof_path = self.root / "build/dev-compiled.json"
        proof_path.write_text(json.dumps(proof))
        proof_bytes = proof_path.read_bytes()
        if initial_eol_change:
            self.source.write_bytes(self.source.read_bytes().replace(b"\r\n", b"\n"))
        selector = types.SimpleNamespace(select=lambda *args: plan,
                                         cmake_tests=lambda *args: {"owned_unit": dict(scripts=[])})
        inventory = dict(tests=[dict(name="owned_unit", command=[str(self.binary)])])
        if wrapped_candidate:
            inventory["tests"][0]["command"] = [str(self.interpreter), str(self.verifier), str(self.binary)]
        calls = []
        class NoProcessOwner:
            def __enter__(owner):
                return owner
            def __exit__(owner, *args):
                return False
            def run(owner, command, *, log):
                calls.append(command)
                # Build/control boundaries are simulated, never subprocesses.
                if "--output-junit" in command:
                    mutate()
                    junit = Path(command[command.index("--output-junit") + 1])
                    junit.write_text('<testsuite><testcase name="owned_unit" /></testsuite>')
                    code = 7 if failed else 0
                else:
                    if len(command) > 3 and command[2] == "capture":
                        Path(command[3]).write_text(json.dumps(baseline))
                    code = 0
                return dict(exitCode=code, seconds=0, log=str(log))
        owner = NoProcessOwner()
        error = None
        result = None
        def read_inventory(*args, **kwargs):
            if change_after_compile:
                self.source.write_text("int changedAfterCompile = 1;\n")
            return json.dumps(inventory)
        def verifier_snapshot(*args, **kwargs):
            return dict(complete=True, files=[dict(path=str(self.verifier), missing=False,
                                                   **content_identity(self.verifier, normalize_text=True))])
        arguments = ["dev.py"] + (["--no-build"] if no_build else []) + ["--files", "src/source with spaces.cpp"]
        with redirect_stdout(io.StringIO()), \
             patch.object(runner, "ROOT", self.root), patch.object(distribution, "ROOT", self.root), \
             patch.object(runner, "load_selector", return_value=selector), \
             patch.object(runner, "EngineeringOwner", return_value=owner), \
             patch.object(runner, "tools", return_value=(self.interpreter, self.interpreter)), \
             patch.object(sys, "executable", str(self.interpreter)), \
             patch.object(runner, "verification_inputs", side_effect=verifier_snapshot), \
             patch.object(runner.subprocess, "check_output", side_effect=read_inventory), \
             patch.object(runner, "configure_required", return_value=False), \
             patch.object(sys, "argv", arguments):
            try:
                result = runner.main()
            except Exception as caught:
                error = caught
        expected_calls = (0 if missing_candidate_proof else 1) if no_build else 6
        self.assertEqual(len(calls), expected_calls, "Only mocked process boundaries are allowed in this fixture")
        evidence, = (self.root / "build/dev").glob("*/verification.json")
        receipt = json.loads(evidence.read_text())
        self.assertEqual(json.loads((evidence.parent / "inputs.json").read_text()), baseline)
        if no_build:
            self.assertEqual(proof_path.read_bytes(), proof_bytes, "No-build must preserve original compiled proof")
        else:
            self.assertEqual(json.loads(proof_path.read_text())["inputs"], baseline,
                             "Compilation proof must retain actual compile baseline, not bless a later edit")
        return result, error, receipt

    def test_runner_unchanged_success_has_final_source_and_binary_proof(self):
        result, error, receipt = self.simulate_no_build()
        self.assertEqual(result, 0)
        self.assertIsNone(error)
        self.assertTrue(receipt["passed"] and receipt["compiledInputsStable"])
        self.assertTrue(receipt["compiledInputsCheck"]["binaries"][0]["stable"])

    def test_interpreter_wrapped_candidate_is_bound_to_exact_compilation_proof(self):
        result, error, receipt = self.simulate_no_build(wrapped_candidate=True)
        self.assertEqual(result, 0)
        self.assertIsNone(error)
        self.assertIn(str(self.binary), receipt["binaries"])
        self.assertEqual(receipt["binaries"][str(self.interpreter)]["sha256"],
                         content_identity(self.interpreter)["sha256"])
        self.assertEqual(receipt["binaries"][str(self.binary)]["sha256"], content_identity(self.binary)["sha256"])

    def test_wrapped_candidate_replacement_invalidates_a_passing_run(self):
        _, error, receipt = self.simulate_no_build(
            lambda: self.binary.write_bytes(b"MZ-replaced-wrapped-candidate"), wrapped_candidate=True)
        self.assertIsInstance(error, RuntimeError)
        self.assertFalse(receipt["passed"] or receipt["compiledInputsStable"])
        changed = [row["path"] for row in receipt["compiledInputsCheck"]["binaries"] if not row["stable"]]
        self.assertEqual(changed, [str(self.binary)])

    def test_wrapped_candidate_missing_from_compilation_proof_fails_before_ctest(self):
        _, error, receipt = self.simulate_no_build(wrapped_candidate=True, missing_candidate_proof=True)
        self.assertIn("was not proven by this compilation", str(error))
        self.assertFalse(receipt["passed"])

    def test_failed_wrapped_workflow_stays_primary_when_candidate_also_changes(self):
        _, error, receipt = self.simulate_no_build(
            lambda: self.binary.write_bytes(b"MZ-replaced-wrapped-candidate"), wrapped_candidate=True, failed=True)
        self.assertTrue(str(error).startswith("affected-tests failed;"))
        self.assertEqual(receipt["failure"], str(error))
        self.assertFalse(receipt["compiledInputsStable"])

    def test_no_build_accepts_only_eol_change_before_and_during_checks(self):
        result, error, receipt = self.simulate_no_build(
            lambda: self.source.write_bytes(b"int value = 42;\r\n"), initial_eol_change=True)
        self.assertEqual(result, 0)
        self.assertIsNone(error)
        self.assertTrue(receipt["compiledInputsStable"])

    def test_successful_tests_with_later_source_edit_cannot_return_zero(self):
        result, error, receipt = self.simulate_no_build(lambda: self.source.write_text("int value = 43;\n"))
        self.assertIsNone(result)
        self.assertIsInstance(error, RuntimeError)
        self.assertFalse(receipt["passed"] or receipt["compiledInputsStable"])
        self.assertEqual(receipt["compiledInputsCheck"]["source"]["changed"][0]["path"], "src/source with spaces.cpp")

    def test_selected_binary_byte_change_is_independently_fatal(self):
        result, error, receipt = self.simulate_no_build(lambda: self.binary.write_bytes(b"MZ-different\n"))
        self.assertIsNone(result)
        self.assertIsInstance(error, RuntimeError)
        self.assertTrue(receipt["compiledInputsCheck"]["source"]["stable"])
        self.assertFalse(receipt["passed"] or receipt["compiledInputsCheck"]["binaries"][0]["stable"])

    def test_actual_copied_worker_identity_is_retained_and_rechecked(self):
        _, error, receipt = self.simulate_no_build(
            lambda: self.adjacent_worker.write_bytes(b"MZ-changed-copied-worker\n"), include_host_worker=True)
        self.assertIsInstance(error, RuntimeError)
        self.assertFalse(receipt["passed"] or receipt["compiledInputsStable"])
        changed = [row for row in receipt["compiledInputsCheck"]["binaries"] if not row["stable"]]
        self.assertEqual([row["path"] for row in changed], [str(self.adjacent_worker)])
        self.assertIn(str(self.adjacent_worker), receipt["binaries"])

    def test_test_failure_remains_primary_when_source_also_drifts(self):
        _, error, receipt = self.simulate_no_build(lambda: self.source.write_text("int value = 44;\n"), failed=True)
        self.assertIsInstance(error, RuntimeError)
        self.assertTrue(str(error).startswith("affected-tests failed;"))
        self.assertEqual(receipt["failure"], str(error))
        self.assertFalse(receipt["compiledInputsStable"])
        self.assertEqual(len(receipt["compiledInputsCheck"]["source"]["changed"]), 1)

    def test_successful_tests_with_script_drift_cannot_return_zero(self):
        _, error, receipt = self.simulate_no_build(lambda: self.verifier.write_text("changed = 43;\n"))
        self.assertIsInstance(error, RuntimeError)
        self.assertFalse(receipt["passed"] or receipt["verificationInputsStable"])
        self.assertTrue(receipt["compiledInputsStable"], "Script-only changes do not invalidate compiled objects")

    def test_failed_tests_and_script_drift_retain_primary_test_error(self):
        _, error, receipt = self.simulate_no_build(lambda: self.verifier.write_text("changed = 44;\n"), failed=True)
        self.assertTrue(str(error).startswith("affected-tests failed;"))
        self.assertEqual(receipt["failure"], str(error))
        self.assertFalse(receipt["verificationInputsStable"])
        self.assertTrue(receipt["compiledInputsStable"])

    def test_verifier_eol_only_change_remains_content_stable(self):
        result, error, receipt = self.simulate_no_build(lambda: self.verifier.write_bytes(b"original = 42\n"))
        self.assertEqual(result, 0)
        self.assertIsNone(error)
        self.assertTrue(receipt["verificationInputsStable"] and receipt["compiledInputsStable"])

    def test_post_test_verifier_unavailable_is_not_pass(self):
        _, error, receipt = self.simulate_no_build(lambda: self.verifier.unlink())
        self.assertIsInstance(error, RuntimeError)
        self.assertFalse(receipt["passed"] or receipt["verificationInputsStable"])
        self.assertEqual(receipt["verificationInputsAfter"]["error"], "FileNotFoundError")
        self.assertTrue(receipt["compiledInputsStable"])

    def test_compile_path_cannot_bless_source_edit_between_compile_check_and_proof(self):
        _, error, receipt = self.simulate_no_build(no_build=False, change_after_compile=True)
        self.assertIsInstance(error, RuntimeError)
        self.assertFalse(receipt["passed"] or receipt["compiledInputsStable"])
        self.assertEqual(receipt["compiledInputsCheck"]["source"]["changed"][0]["path"], "src/source with spaces.cpp")

    def test_failed_tests_plus_unavailable_source_keep_original_test_error(self):
        _, error, receipt = self.simulate_no_build(lambda: (self.root / "CMakeLists.txt").unlink(), failed=True)
        self.assertTrue(str(error).startswith("affected-tests failed;"))
        self.assertEqual(receipt["failure"], str(error))
        self.assertFalse(receipt["compiledInputsStable"])
        self.assertEqual(receipt["compiledInputsCheck"]["source"]["unavailable"], "FileNotFoundError")

    def test_success_with_post_test_source_read_uncertainty_is_not_pass(self):
        _, error, receipt = self.simulate_no_build(lambda: (self.root / "CMakeLists.txt").unlink())
        self.assertIsInstance(error, RuntimeError)
        self.assertFalse(receipt["passed"] or receipt["compiledInputsStable"])
        self.assertEqual(receipt["compiledInputsCheck"]["source"]["unavailable"], "FileNotFoundError")


if __name__ == "__main__":
    unittest.main()
