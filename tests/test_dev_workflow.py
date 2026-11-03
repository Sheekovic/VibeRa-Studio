"""Disposable development-guard tests; never builds or launches the application."""
from contextlib import contextmanager
import importlib.util
import json
import os
from pathlib import Path
import queue
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from dev_support import EngineeringOwner, configure_required, content_identity, process_identity, project_schema

SPEC = importlib.util.spec_from_file_location("dev_runner", ROOT / "scripts/dev.py")
runner = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(runner)


class VerificationNoteTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.path = self.root / "docs/VERIFICATION.md"
        self.path.parent.mkdir()

    def test_utf8_failure_text_and_missing_fixture_are_preserved(self):
        self.assertEqual(runner.verification_note_integrity(self.root), {"present": False})
        raw = "Retained failure: packet255\u219264; \u0639\u0631\u0628\u064a\n".encode("utf-8")
        self.path.write_bytes(raw)
        self.assertTrue(runner.verification_note_integrity(self.root)["utf8"])
        self.assertEqual(self.path.read_bytes(), raw)

    def test_reported_reencoding_growth_is_rejected_without_rewriting(self):
        text = "Retained failure: packet255\u219264\n"
        for _ in range(14):
            text = text.encode("utf-8").decode("cp1252")
        raw = text.encode("utf-8")
        self.path.write_bytes(raw)
        with self.assertRaisesRegex(ValueError, "8192 bytes|1 MiB"):
            runner.verification_note_integrity(self.root)
        self.assertEqual(self.path.read_bytes(), raw)

    def test_oversize_is_refused_before_loading_and_invalid_encodings_fail(self):
        with self.path.open("wb") as stream:
            stream.truncate(1024 * 1024 + 1)
        with patch.object(Path, "read_bytes", side_effect=AssertionError("Must stat before loading")):
            with self.assertRaisesRegex(ValueError, "1 MiB"):
                runner.verification_note_integrity(self.root)
        for raw in (b"invalid\xff", "UTF16 failure".encode("utf-16"), b"binary\0text"):
            self.path.write_bytes(raw)
            with self.assertRaisesRegex(ValueError, "UTF-8"):
                runner.verification_note_integrity(self.root)
            self.assertEqual(self.path.read_bytes(), raw)

    def test_preflight_rejects_growth_before_required_build_inputs(self):
        with self.path.open("wb") as stream:
            stream.truncate(1024 * 1024 + 1)
        with self.assertRaisesRegex(ValueError, "1 MiB"):
            runner.preflight(self.root, {"testNames": []})


class TestInventoryRefreshTests(unittest.TestCase):
    def test_matching_inventory_reuses_configuration(self):
        inventory = {"tests": [{"name": "original"}]}
        def unexpected():
            self.fail("Matching cached test graph must not reconfigure")
        self.assertIs(runner.synchronized_test_inventory(lambda: inventory, {"original"}, unexpected), inventory)

    def test_new_test_only_selection_refreshes_once_and_preserves_source_expectation(self):
        calls = []
        before = {"tests": [{"name": "original"}]}
        after = {"tests": [{"name": "original"}, {"name": "new_source_case"}]}
        expected = {"original", "new_source_case"}
        result = runner.synchronized_test_inventory(lambda: after if calls else before, expected,
                                                    lambda: calls.append("configure"))
        self.assertEqual(calls, ["configure"])
        self.assertIs(result, after)
        self.assertEqual(expected, {"original", "new_source_case"})

    def test_no_build_and_persistent_drift_never_report_partial_success(self):
        stale = lambda: {"tests": [{"name": "original"}]}
        with self.assertRaisesRegex(RuntimeError, "registration drift"):
            runner.synchronized_test_inventory(stale, {"original", "required"})
        calls = []
        with self.assertRaisesRegex(RuntimeError, "registration drift"):
            runner.synchronized_test_inventory(stale, {"original", "required"}, lambda: calls.append(1))
        self.assertEqual(calls, [1])


class WorkflowTests(unittest.TestCase):
    def test_direct_owned_process_preserves_nonzero_result_log_and_timing(self):
        code = "import sys;print('original disposable provider failure');sys.exit(7)"
        self.assertEqual(runner.run_owned(self.root, "failure receipt unit", [sys.executable, "-c", code]), 7)
        receipts = list((self.root / "build/dev/owned").glob("*/verification.json"))
        self.assertEqual(len(receipts), 1)
        receipt = json.loads(receipts[0].read_text())
        self.assertFalse(receipt["passed"])
        self.assertFalse(receipt["releaseQualification"])
        self.assertEqual(receipt["process"]["exitCode"], 7)
        self.assertGreater(receipt["process"]["seconds"], 0)
        self.assertIn("original disposable provider failure", Path(receipt["process"]["log"]).read_text())
        self.assertIn("Process exit only", receipt["resultAuthority"])

    def test_direct_owned_launcher_failure_still_has_terminal_receipt(self):
        with patch.object(runner, "EngineeringOwner", side_effect=RuntimeError("original owner refusal")):
            with self.assertRaisesRegex(RuntimeError, "original owner refusal"):
                runner.run_owned(self.root, "owner failure unit", ["never launched"])
        receipt_path, = (self.root / "build/dev/owned").glob("*/verification.json")
        receipt = json.loads(receipt_path.read_text())
        self.assertFalse(receipt["passed"])
        self.assertEqual(receipt["launcherFailure"], "RuntimeError")
        self.assertNotIn("process", receipt)

    def setUp(self):
        owned_parent = ROOT / "build/dev-workflow-unit"
        owned_parent.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix="owned test ", dir=owned_parent)
        self.root = Path(self.temporary.name)
        self.environment = patch.dict(os.environ)
        self.environment.start()
        os.environ.pop("VIBERA_DEV_OWNER_TOKEN", None)

    def tearDown(self):
        self.environment.stop()
        self.temporary.cleanup()

    @contextmanager
    def child(self, lease=False):
        if lease:
            code = ("import sys;from pathlib import Path;"
                    "sys.path.insert(0,sys.argv[1]);from dev_support import EngineeringOwner;"
                    "owner=EngineeringOwner(Path(sys.argv[2]),'disposable unit child');"
                    "owner.__enter__();print('READY',flush=True);sys.stdin.readline();"
                    "owner.__exit__(None,None,None)")
        else:
            code = "import sys;print('READY',flush=True);sys.stdin.readline()"
        env = dict(os.environ)
        env.pop("VIBERA_DEV_OWNER_TOKEN", None)
        process = subprocess.Popen([sys.executable, "-c", code, str(ROOT / "scripts"), str(self.root)],
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   text=True, env=env,
                                   creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        identity = process_identity(process.pid)
        lines = queue.Queue()
        threading.Thread(target=lambda: lines.put(process.stdout.readline()), daemon=True).start()
        try:
            line = lines.get(timeout=10)
            self.assertEqual(line.strip(), "READY", "Owned disposable helper failed before readiness")
            self.assertIsNotNone(identity)
            yield process, identity
        finally:
            if process.poll() is None:
                try:
                    process.stdin.write("close\n")
                    process.stdin.flush()
                except (BrokenPipeError, OSError):
                    pass
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    # Retained Popen OS handle belongs to this disposable helper.
                    # Never terminate by executable name or an unrelated PID.
                    if process_identity(process.pid) == identity:
                        process.terminate()
                        process.wait(timeout=5)
                    else:
                        self.fail("Owned helper identity changed; refused termination")
            for stream in (process.stdin, process.stdout, process.stderr):
                stream.close()

    def test_kernel_lease_collision_with_owned_child(self):
        with self.child(lease=True):
            with self.assertRaisesRegex(RuntimeError, "busy"):
                with EngineeringOwner(self.root, "competing unit check"):
                    self.fail("Concurrent lease admitted")
        with EngineeringOwner(self.root, "after graceful helper exit"):
            pass

    def test_valid_nested_token_reuses_live_owner_without_overwrite(self):
        with EngineeringOwner(self.root, "parent unit check") as owner:
            before = owner.path.read_bytes()
            os.environ["VIBERA_DEV_OWNER_TOKEN"] = owner.token
            with EngineeringOwner(self.root, "nested unit check") as nested:
                self.assertTrue(nested.nested)
                self.assertIsNone(nested.file)
            self.assertEqual(owner.path.read_bytes(), before)

    def test_invalid_and_stale_token_rejected(self):
        os.environ["VIBERA_DEV_OWNER_TOKEN"] = "invalid-token"
        with self.assertRaisesRegex(RuntimeError, "Invalid/stale"):
            with EngineeringOwner(self.root, "invalid unit check"):
                pass
        os.environ.pop("VIBERA_DEV_OWNER_TOKEN")
        with EngineeringOwner(self.root, "prior unit check") as owner:
            token = owner.token
        os.environ["VIBERA_DEV_OWNER_TOKEN"] = token
        with self.assertRaisesRegex(RuntimeError, "Invalid/stale"):
            with EngineeringOwner(self.root, "finished owner must not be inherited"):
                pass

    def test_verified_live_interrupted_child_blocks_takeover(self):
        owner = EngineeringOwner(self.root, "interrupted fixture")
        owner.__enter__()
        with self.child() as (child, identity):
            owner.record["children"] = [{"pid": child.pid, "creation": identity,
                                           "command": ["owned disposable stdin helper"]}]
            owner.save()
            owner.__exit__(None, None, None)
            with self.assertRaisesRegex(RuntimeError, "Interrupted owned child still active"):
                with EngineeringOwner(self.root, "must reconcile surviving helper"):
                    pass
        with EngineeringOwner(self.root, "helper has exited"):
            pass

    def test_stale_child_creation_identity_does_not_match_reused_pid(self):
        with EngineeringOwner(self.root, "stale metadata fixture") as owner:
            owner.record["children"] = [{"pid": os.getpid(), "creation": "not-current-creation"}]
            owner.save()
        with EngineeringOwner(self.root, "safe stale metadata takeover"):
            pass

    def test_nested_run_registers_and_retires_specific_child(self):
        ready, stop = self.root / "child-ready.json", self.root / "child-stop"
        code = ("import json,os,sys,time;from pathlib import Path;"
                "Path(sys.argv[1]).write_text(json.dumps({'pid':os.getpid()}));"
                "deadline=time.monotonic()+4;"
                "\nwhile not Path(sys.argv[2]).exists() and time.monotonic()<deadline: time.sleep(.01)")
        with EngineeringOwner(self.root, "nested registration parent") as owner:
            os.environ["VIBERA_DEV_OWNER_TOKEN"] = owner.token
            with EngineeringOwner(self.root, "nested registration runner") as nested:
                results, errors = [], []
                def launch():
                    try:
                        results.append(nested.run([sys.executable, "-c", code, str(ready), str(stop)],
                                                  log=self.root / "owned-child.log"))
                    except Exception as error:
                        errors.append(error)
                thread = threading.Thread(target=launch)
                thread.start()
                try:
                    deadline = time.monotonic() + 3
                    children = []
                    while time.monotonic() < deadline:
                        with owner.metadata() as record:
                            children = list(record["children"])
                        if ready.exists() and children:
                            break
                        time.sleep(.01)
                    self.assertTrue(ready.exists(), "Disposable helper readiness missing")
                    pid = json.loads(ready.read_text())["pid"]
                    self.assertEqual([x["pid"] for x in children], [pid])
                    self.assertEqual(children[0]["creation"], process_identity(pid))
                finally:
                    stop.write_text("graceful stop\n")
                    thread.join(timeout=6)
                self.assertFalse(thread.is_alive())
                self.assertEqual(errors, [])
                self.assertEqual(results[0]["exitCode"], 0)
                with owner.metadata() as record:
                    self.assertEqual(record["children"], [])

    def test_source_eol_normalizes_but_binary_bytes_remain_exact(self):
        source = self.root / "source with spaces.cpp"
        source.write_bytes(b"int answer = 42;\r\n")
        crlf = content_identity(source)
        source.write_bytes(b"int answer = 42;\n")
        lf = content_identity(source)
        self.assertNotEqual(crlf["sha256"], lf["sha256"])
        self.assertEqual(crlf["contentSha256"], lf["contentSha256"])
        source.write_bytes(b"int answer = 41;\n")
        self.assertNotEqual(lf["contentSha256"], content_identity(source)["contentSha256"])
        binary = self.root / "owned-fixture.exe"
        binary.write_bytes(b"MZ\r\n")
        old = content_identity(binary)
        binary.write_bytes(b"MZ\n")
        new = content_identity(binary)
        self.assertEqual(old["sha256"], old["contentSha256"])
        self.assertNotEqual(old["contentSha256"], new["contentSha256"])

    def valid_inputs(self):
        import distribution
        for name in ("CMakeLists.txt", "CMakePresets.json", "dependencies.lock.json",
                     "src/PluginProtocol.h", "scripts/distribution.py", *distribution.BUILD_GUARD_INPUTS):
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("owned test fixture\n", encoding="utf-8")
        project = self.root / "src/Project.h"
        project.write_text("struct Project { static constexpr int currentVersion = 173; };\n", encoding="utf-8")
        ledger = self.root / "docs/modules/RACK-PATTERN-SAMPLER.json"
        ledger.parent.mkdir(parents=True, exist_ok=True)
        ledger.write_text(json.dumps({"id": "rack-pattern-sampler", "requirements": [{"id": "owned-fixture"}]}), encoding="utf-8")

    def test_schema_extracted_from_authority_and_preflight_inputs(self):
        self.valid_inputs()
        self.assertEqual(project_schema(self.root), 173)
        receipt = runner.preflight(self.root, {"testNames": ["owned-test"]})
        self.assertEqual(receipt["projectSchema"], 173)
        self.assertEqual(receipt["selectedTests"], 1)
        self.assertFalse(receipt["candidateQualification"])

    def test_preflight_missing_and_wrong_module_fail_before_expensive_work(self):
        with self.assertRaisesRegex(ValueError, "Required input missing"):
            runner.preflight(self.root, {"testNames": []})
        self.valid_inputs()
        (self.root / "src/PluginProtocol.h").unlink()
        with self.assertRaisesRegex(ValueError, "PluginProtocol.h"):
            runner.preflight(self.root, {"testNames": []})
        self.valid_inputs()
        (self.root / "docs/modules/RACK-PATTERN-SAMPLER.json").write_text('{"id":"wrong","requirements":[1]}')
        with self.assertRaisesRegex(ValueError, "identity/requirements inconsistent"):
            runner.preflight(self.root, {"testNames": []})

    def test_missing_schema_fails_closed(self):
        (self.root / "src").mkdir()
        (self.root / "src/Project.h").write_text("struct Project {};\n", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "schema missing"):
            project_schema(self.root)

    def test_cache_ownership_and_preset_variables(self):
        preset = dict(name="windows", generator="Visual Studio 17 2022", architecture="x64",
                      binaryDir="${sourceDir}/build/windows", cacheVariables={"CMAKE_CONFIGURATION_TYPES": "Debug;Release"})
        (self.root / "CMakePresets.json").write_text(json.dumps(dict(configurePresets=[preset])))
        self.assertTrue(configure_required(self.root))
        cache = self.root / "build/windows/CMakeCache.txt"
        cache.parent.mkdir(parents=True)
        contents = ("# Owned cache fixture\n\nCMAKE_CONFIGURATION_TYPES:UNINITIALIZED=Debug;Release\n\n"
                    "CMAKE_GENERATOR:INTERNAL=Visual Studio 17 2022\n"
                    "CMAKE_GENERATOR_PLATFORM:INTERNAL=x64\n"
                    f"CMAKE_HOME_DIRECTORY:INTERNAL={self.root.as_posix()}\n")
        cache.write_bytes(contents.replace("\n", "\r\n").encode())
        self.assertFalse(configure_required(self.root))
        self.assertTrue(configure_required(self.root, ["NewHistoryTarget"]))
        (cache.parent / "NewHistoryTarget.vcxproj").write_text("owned generated target fixture")
        self.assertFalse(configure_required(self.root, ["NewHistoryTarget"]))
        self.assertTrue(configure_required(self.root, ["NewHistoryTarget", "AnotherNewTarget"]))
        with self.assertRaisesRegex(ValueError, "Invalid required CMake target"):
            configure_required(self.root, ["../unrelated"])
        cache.write_text(contents.replace("Debug;Release", "Debug"))
        self.assertTrue(configure_required(self.root))
        cache.write_text(contents.replace("INTERNAL=x64", "INTERNAL=Win32"))
        with self.assertRaisesRegex(ValueError, "generator/architecture"):
            configure_required(self.root)
        cache.write_text(contents.replace(self.root.as_posix(), "E:/unrelated source"))
        with self.assertRaisesRegex(ValueError, "another source"):
            configure_required(self.root)

    def test_refresh_invalidates_once_without_claiming_all_targets_compiled(self):
        import distribution
        self.valid_inputs()
        (self.root / "build").mkdir(exist_ok=True)
        source = self.root / "src/Project.h"
        with patch.object(distribution, "ROOT", self.root):
            previous = distribution.source_state()
            compiled = self.root / "build/compiled-project-inputs.json"
            compiled.write_text(json.dumps(previous))
            baseline = compiled.read_bytes()
            source.write_bytes(b"struct Project { static constexpr int currentVersion = 174; };\r\n")
            os.utime(source, (1000, 1000))
            distribution.refresh_inputs()
            refreshed_time = source.stat().st_mtime_ns
            self.assertGreater(refreshed_time, 1000 * 10**9)
            self.assertEqual(compiled.read_bytes(), baseline)
            distribution.refresh_inputs()
            self.assertEqual(source.stat().st_mtime_ns, refreshed_time)
            source.write_bytes(source.read_bytes().replace(b"\r\n", b"\n"))
            os.utime(source, (1000, 1000))
            distribution.refresh_inputs()
            self.assertEqual(source.stat().st_mtime_ns, 1000 * 10**9)
            self.assertEqual(compiled.read_bytes(), baseline)


if __name__ == "__main__":
    unittest.main()
