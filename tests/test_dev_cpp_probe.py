"""Small mocked tests only: never refresh real inputs or invoke MSBuild."""
import importlib.util
from pathlib import Path
import sys
import unittest
from unittest.mock import MagicMock, patch

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
spec = importlib.util.spec_from_file_location("probe_cpp", ROOT / "scripts/probe-cpp.py")
probe_cpp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe_cpp)


class ProbeTests(unittest.TestCase):
    def test_refresh_precedes_compile_under_verified_owner(self):
        order = []
        owner = MagicMock(nested=True)
        owner.run.side_effect = lambda command, **kw: (order.append(("compile", command)) or
                                                     dict(exitCode=7, seconds=.25))
        context = MagicMock()
        context.__enter__.return_value = owner
        with patch.dict(probe_cpp.os.environ, {"VIBERA_DEV_OWNER_TOKEN": "owned"}), \
             patch.object(probe_cpp, "EngineeringOwner", return_value=context), \
             patch.object(probe_cpp, "validate", return_value=(Path("target.vcxproj"), Path("src/Example.cpp"))), \
             patch.object(probe_cpp, "installed_msbuild", return_value=Path("MSBuild.exe")), \
             patch.object(probe_cpp.distribution, "refresh_inputs", side_effect=lambda: order.append(("refresh", None))):
            self.assertEqual(probe_cpp.probe(ROOT, "VibeRaCore", "src/Example.cpp"), 7)
        self.assertEqual([event[0] for event in order], ["refresh", "compile"])
        command = order[1][1]
        for item in ("/t:ClCompile", "/m:1", "/nr:false", "/p:Configuration=Release", "/p:Platform=x64"):
            self.assertIn(item, command)
        self.assertEqual(sum(x.startswith("/p:SelectedFiles=") for x in command), 1)

    def test_no_owner_refuses_before_validation_or_refresh(self):
        with patch.dict(probe_cpp.os.environ, {}, clear=True), \
             patch.object(probe_cpp, "validate") as validate, \
             patch.object(probe_cpp.distribution, "refresh_inputs") as refresh:
            with self.assertRaisesRegex(RuntimeError, "dev.py owned"):
                probe_cpp.probe(ROOT, "VibeRaCore", "src/Commands.cpp")
            validate.assert_not_called()
            refresh.assert_not_called()

    def test_invalid_source_and_target_never_reach_cache_or_compiler(self):
        for target, source in (("VibeRaCore;Build", "src/Commands.cpp"),
                               ("VibeRaCore", "vendor/Original.cpp"),
                               ("VibeRaCore", "src/../src/Commands.cpp"),
                               ("VibeRaCore", "src/Commands.h")):
            with self.subTest(source=source), patch.object(probe_cpp, "configure_required") as configure:
                with self.assertRaises(ValueError):
                    probe_cpp.validate(ROOT, target, source)
                configure.assert_not_called()

    def test_stale_owner_refuses_before_refresh(self):
        with patch.dict(probe_cpp.os.environ, {"VIBERA_DEV_OWNER_TOKEN": "stale"}), \
             patch.object(probe_cpp, "EngineeringOwner", side_effect=RuntimeError("stale owner")), \
             patch.object(probe_cpp.distribution, "refresh_inputs") as refresh:
            with self.assertRaisesRegex(RuntimeError, "stale owner"):
                probe_cpp.probe(ROOT, "VibeRaCore", "src/Commands.cpp")
            refresh.assert_not_called()


if __name__ == "__main__":
    unittest.main()
