"""Receipt-only provenance fixtures; no builds, providers or application launch."""
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from dev_support import content_identity, verification_inputs


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


runner = load("verification_runner_fixture", ROOT / "scripts/dev.py")
selector = load("verification_selector_fixture", ROOT / "scripts/dev-select.py")


class VerificationInputTests(unittest.TestCase):
    def setUp(self):
        parent = ROOT / "build/verification-inputs-unit"
        parent.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix="owned verifier ", dir=parent)
        self.root = Path(self.temporary.name)
        for name in ("scripts/dev.py", "scripts/dev-select.py", "scripts/dev_support.py", "scripts/distribution.py"):
            self.write(name, b"# owned implementation fixture\n")

    def tearDown(self):
        self.temporary.cleanup()

    def write(self, name, data):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return path

    def rows(self, tests=(), tools=()):
        return {row["path"]: row for row in verification_inputs(self.root, tests, tools)["files"]}

    def test_python_powershell_and_node_text_eol_and_substantive_change(self):
        for suffix in ("py", "ps1", "psm1", "js", "mjs", "cjs"):
            with self.subTest(suffix=suffix):
                name = "scripts/owned." + suffix
                self.write(name, b"value=1\r\n")
                before = self.rows()[name]
                self.write(name, b"value=1\n")
                lf = self.rows()[name]
                self.assertNotEqual(before["sha256"], lf["sha256"])
                self.assertEqual(before["contentSha256"], lf["contentSha256"])
                self.write(name, b"value=2\n")
                self.assertNotEqual(lf["contentSha256"], self.rows()[name]["contentSha256"])

    def test_binary_tool_hash_is_exact_and_default_compilation_policy_unchanged(self):
        tool = self.write("tools/owned tool.exe", b"MZ\r\n")
        before = self.rows(tools=[tool])["tools/owned tool.exe"]
        tool.write_bytes(b"MZ\n")
        after = self.rows(tools=[tool])["tools/owned tool.exe"]
        self.assertNotEqual(before["sha256"], after["sha256"])
        self.assertEqual(before["sha256"], before["contentSha256"])
        # Receipt normalization opt-in does not alter existing C++ source-state
        # consumers/default identity for a newly recognized verifier extension.
        node = self.write("scripts/owned.mjs", b"export const value=1;\r\n")
        identity = content_identity(node)
        self.assertEqual(identity["sha256"], identity["contentSha256"])
        self.assertNotEqual(identity["contentSha256"], self.rows()["scripts/owned.mjs"]["contentSha256"])

    def test_selected_relative_script_and_sibling_helpers_with_spaces(self):
        main = self.write("custom verifier/tool folder/main script.mjs", b"import './helper.cjs';\n")
        self.write("custom verifier/tool folder/helper.cjs", b"exports.answer=41;\n")
        self.write("custom verifier/tool folder/host.ps1", b"# local helper\n")
        tests = [{"name": "owned-node", "command": ["node.exe", "tool folder/main script.mjs"],
                  "properties": [{"name": "WORKING_DIRECTORY", "value": str(main.parent.parent)}]}]
        rows = self.rows(tests)
        self.assertIn("custom verifier/tool folder/main script.mjs", rows)
        self.assertIn("custom verifier/tool folder/helper.cjs", rows)
        self.assertIn("custom verifier/tool folder/host.ps1", rows)
        self.assertIn("selected CTest script: owned-node", rows["custom verifier/tool folder/main script.mjs"]["reasons"])
        old = rows["custom verifier/tool folder/helper.cjs"]["contentSha256"]
        self.write("custom verifier/tool folder/helper.cjs", b"exports.answer=42;\n")
        self.assertNotEqual(old, self.rows(tests)["custom verifier/tool folder/helper.cjs"]["contentSha256"])

    def test_copied_candidate_skill_client_and_source_client_both_identified(self):
        app = self.write("build/windows/app candidate/owned.exe", b"MZowned")
        original = "skills/vibera-studio/scripts/stdio_client.py"
        candidate = "build/windows/app candidate/skills/vibera-studio/scripts/stdio_client.py"
        self.write(original, b"# source client\n")
        self.write(candidate, b"# copied candidate client\n")
        rows = self.rows([{"name": "mcp", "command": [sys.executable, "scripts/unused.py", str(app)]}])
        self.assertIn(original, rows)
        self.assertIn(candidate, rows)
        self.assertNotEqual(rows[original]["contentSha256"], rows[candidate]["contentSha256"])
        self.assertEqual(rows["build/windows/app candidate/owned.exe"]["sha256"], content_identity(app)["sha256"])
        before = rows["build/windows/app candidate/owned.exe"]["sha256"]
        app.write_bytes(b"MZreplacement\r\n")
        changed = self.rows([{"name": "mcp", "command": [sys.executable, "scripts/unused.py", str(app)]}])
        self.assertNotEqual(before, changed["build/windows/app candidate/owned.exe"]["sha256"])

    def test_missing_selected_input_is_explicit_not_fabricated_identity(self):
        test = {"name": "missing", "command": [sys.executable, str(self.root / "scripts/missing.py")]}
        result = verification_inputs(self.root, [test])
        row = next(row for row in result["files"] if row["path"] == "scripts/missing.py")
        self.assertTrue(row["missing"])
        self.assertNotIn("sha256", row)
        self.assertFalse(result["complete"])

    def test_unittest_discovery_pattern_is_not_a_missing_script(self):
        self.write("tests/test_owned.py", b"# actual discovered test source\n")
        for arguments in (("-p", "test_dev_*.py"), ("--pattern", "test_owned.py"),
                          ("--pattern=test_*.py",), ("-ptest_*.py",)):
            with self.subTest(arguments=arguments):
                test = {"name": "development_workflow", "command": [sys.executable, "-m", "unittest",
                        "discover", "-s", str(self.root / "tests"), *arguments]}
                result = verification_inputs(self.root, [test])
                self.assertTrue(result["complete"])
                paths = {row["path"] for row in result["files"]}
                self.assertIn("tests/test_owned.py", paths)
                self.assertFalse(any("*" in path or "=" in path for path in paths))
                self.assertEqual(result["selectedTests"][0]["command"], test["command"])

    def test_conservative_helpers_and_no_secret_environment_payload(self):
        self.write("tests/imported_helper.py", b"# indirect test helper\n")
        self.write("scripts/nested/optional_helper.js", b"// dynamic helper\n")
        self.write("scripts/__pycache__/ignored.py", b"# generated cache\n")
        with patch.dict(os.environ, {"VIBERA_UNIT_ONLY_SECRET": "must-never-enter-receipt"}):
            result = verification_inputs(self.root, [])
        paths = {row["path"] for row in result["files"]}
        self.assertIn("tests/imported_helper.py", paths)
        self.assertIn("scripts/nested/optional_helper.js", paths)
        self.assertNotIn("scripts/__pycache__/ignored.py", paths)
        self.assertNotIn("must-never-enter-receipt", json.dumps(result))

    def valid_preflight(self):
        for name in ("CMakePresets.json", "dependencies.lock.json", "src/PluginProtocol.h"):
            self.write(name, b"owned fixture\n")
        self.write("src/Project.h", b"struct Project { static constexpr int currentVersion=19; };\n")
        self.write("docs/modules/RACK-PATTERN-SAMPLER.json", b'{"id":"rack-pattern-sampler","requirements":[{"id":"owned"}]}')
        self.write("CMakeLists.txt", b'add_test(NAME owned COMMAND node "${CMAKE_CURRENT_SOURCE_DIR}/scripts/main.mjs")\n')
        return {"reasons": [], "buildTargets": ["OwnedTarget"], "testNames": ["owned"]}

    def test_preflight_uses_authoritative_main_script_specs_and_records_identity(self):
        plan = self.valid_preflight()
        self.write("scripts/main.mjs", b"export const pass=true;\r\n")
        specs = selector.cmake_tests(self.root)
        self.assertEqual(specs["owned"]["scripts"], ["scripts/main.mjs"])
        receipt = runner.preflight(self.root, plan, specs)
        self.assertEqual(receipt["selectedMainPrerequisites"][0]["path"], "scripts/main.mjs")
        self.assertEqual(receipt["selectedMainPrerequisites"][0]["test"], "owned")
        self.assertIn("contentSha256", receipt["selectedMainPrerequisites"][0])
        (self.root / "scripts/main.mjs").unlink()
        with self.assertRaisesRegex(ValueError, "Required selected main verifier missing: scripts/main.mjs"):
            runner.preflight(self.root, plan, specs)

    def test_missing_main_stops_actual_runner_before_owner_or_tools_build(self):
        plan = self.valid_preflight()
        fake = types.SimpleNamespace(select=lambda *a, **k: plan, cmake_tests=selector.cmake_tests)
        with patch.object(runner, "ROOT", self.root), patch.object(runner, "load_selector", return_value=fake), \
             patch.object(runner, "EngineeringOwner") as owner, patch.object(runner, "tools") as tools, \
             patch.object(sys, "argv", ["dev.py", "--files"]), contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaisesRegex(ValueError, "Required selected main verifier missing"):
                runner.main()
        owner.assert_not_called()
        tools.assert_not_called()

    def test_parser_keeps_quoted_script_filename_spaces(self):
        self.write("CMakeLists.txt", b'add_test(NAME spaced COMMAND node "${CMAKE_CURRENT_SOURCE_DIR}/scripts/main verifier.js")\n')
        self.assertEqual(selector.cmake_tests(self.root)["spaced"]["scripts"], ["scripts/main verifier.js"])

    def test_parser_recognizes_selected_node_and_powershell_main_formats(self):
        formats = ("js", "mjs", "cjs", "ps1", "pyw")
        text = "\n".join(f'add_test(NAME owned_{suffix} COMMAND tool "${{CMAKE_CURRENT_SOURCE_DIR}}/scripts/main.{suffix}")'
                         for suffix in formats)
        self.write("CMakeLists.txt", text.encode())
        specs = selector.cmake_tests(self.root)
        for suffix in formats:
            self.assertEqual(specs["owned_" + suffix]["scripts"], ["scripts/main." + suffix])


if __name__ == "__main__":
    unittest.main()
