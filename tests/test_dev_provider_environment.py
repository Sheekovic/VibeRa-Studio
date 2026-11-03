"""Cheap provider admission tests; no worker, input, audio or compatibility claim."""
import contextlib
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("installed_effect_probe", ROOT / "scripts/verify-installed-effect-bus.py")
probe = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(probe)


class ProviderEnvironmentTests(unittest.TestCase):
    def test_default_desktop_requires_all_observations(self):
        ready = dict(windows=True, threadDesktopObservable=True, threadIsDefaultDesktop=True)
        self.assertTrue(probe.environment_ready(ready))
        for key in ready:
            for refused in (False, None, "true", 1):
                self.assertFalse(probe.environment_ready(dict(ready, **{key: refused})))
        self.assertFalse(probe.environment_ready({}))

    def test_unavailable_environment_never_launches_or_creates_output(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "build") as name:
            folder = Path(name)
            output = folder / "never-created"
            with patch.object(probe, "observe_environment", return_value={}), \
                    patch.object(probe, "run_provider") as launch, \
                    contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(probe.main([str(folder / "effect.vst3"), str(folder / "metadata.xml"), str(output)]), 2)
            launch.assert_not_called()
            self.assertFalse(output.exists())

    def test_environment_only_success_never_launches(self):
        ready = dict(windows=True, threadDesktopObservable=True, threadIsDefaultDesktop=True)
        with patch.object(probe, "observe_environment", return_value=ready), \
                patch.object(probe, "run_provider") as launch, contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(probe.main([str(ROOT / "fixture.vst3"), str(ROOT / "metadata.xml"),
                                         str(ROOT / "build/windows/not-created"), "--preflight-only"]), 0)
        launch.assert_not_called()

    def test_fresh_output_and_existing_or_outside_preservation(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "build") as name:
            root = Path(name)
            windows = root / "build/windows"
            windows.mkdir(parents=True)
            probe.validate_output(root, windows / "new")
            sentinel = windows / "existing"
            sentinel.mkdir()
            file = sentinel / "keep.txt"
            file.write_text("user work")
            for refused in (windows, sentinel, root / "outside", windows / "a/../new"):
                with self.assertRaises(ValueError):
                    probe.validate_output(root, refused)
            self.assertEqual(file.read_text(), "user work")
            self.assertFalse((windows / "new").exists())

    def test_reparse_ancestor_refused_before_output_mutation(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "build") as name:
            root = Path(name)
            target = root / "build/windows/new"
            class Reparse:
                st_mode = 0
                st_file_attributes = 0x400
            with patch.object(Path, "lstat", return_value=Reparse()):
                with self.assertRaisesRegex(ValueError, "reparse"):
                    probe.validate_output(root, target)
            self.assertFalse(target.exists())
