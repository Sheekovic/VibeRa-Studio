"""Exercise the real JUCE patch helper against pristine pinned archive bytes."""
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest
import zipfile


ROOT = Path(__file__).resolve().parents[1]
SHELL = shutil.which("pwsh") or shutil.which("powershell")
ARCHIVE = ROOT / "vendor/archives/juce.zip"


@unittest.skipUnless(SHELL and ARCHIVE.is_file(), "Requires PowerShell and the pinned JUCE archive")
class DependencyPatchLineEndings(unittest.TestCase):
    def fixture(self, parent, autocrlf):
        root = Path(parent)
        scripts = root / "scripts"
        scripts.mkdir()
        helper = ROOT / "scripts/apply-dependency-patches.ps1"
        shutil.copy2(helper, scripts / helper.name)
        # Isolate this family's real implementation; subsequent families have
        # separate guards and are outside this line-ending regression.
        for name in re.findall(r"Join-Path \$PSScriptRoot '(apply-[^']+\.ps1)'",
                               helper.read_text(encoding="utf-8-sig")):
            (scripts / name).write_text("# Separate patch family\n", encoding="utf-8")
        shutil.copy2(ROOT / "dependencies.lock.json", root / "dependencies.lock.json")
        shutil.copy2(ROOT / ".gitattributes", root / ".gitattributes")
        (root / ".gitignore").write_text("/vendor/\n", encoding="utf-8")
        family = "patches/juce-vst3-parameters"
        shutil.copytree(ROOT / family, root / family)
        manifest = json.loads((root / family / "manifest.json").read_text(encoding="utf-8"))
        with zipfile.ZipFile(ARCHIVE) as archive:
            for entry in manifest["files"]:
                relative = manifest["sourceDirectory"] + "/" + entry["path"]
                target = root / "vendor/source" / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                original = archive.read(relative)
                self.assertEqual(hashlib.sha256(original).hexdigest(), entry["originalSha256"])
                target.write_bytes(original)
        git = ["git", "-c", "safe.directory=" + root.as_posix()]
        subprocess.run(git + ["init", "--quiet"], cwd=root, check=True)
        for key, value in [("core.autocrlf", autocrlf), ("core.eol", "lf")]:
            subprocess.run(git + ["config", key, value], cwd=root, check=True)
        return root, manifest

    def invoke(self, root):
        return subprocess.run(
            [SHELL, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
             str(root / "scripts/apply-dependency-patches.ps1")],
            cwd=root, capture_output=True, text=True, encoding="utf-8", errors="replace",
        )

    def test_exact_bytes_and_idempotence_across_git_line_ending_modes(self):
        for mode in ("input", "false", "true"):
            with self.subTest(autocrlf=mode), tempfile.TemporaryDirectory() as folder:
                root, manifest = self.fixture(folder, mode)
                for attempt in range(2):
                    result = self.invoke(root)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    for entry in manifest["files"]:
                        target = root / "vendor/source" / manifest["sourceDirectory"] / entry["path"]
                        self.assertEqual(hashlib.sha256(target.read_bytes()).hexdigest(),
                                         entry["patchedSha256"], (mode, attempt, entry["path"]))

    def test_unknown_vendor_bytes_are_refused_and_preserved(self):
        with tempfile.TemporaryDirectory() as folder:
            root, manifest = self.fixture(folder, "input")
            target = root / "vendor/source" / manifest["sourceDirectory"] / manifest["files"][0]["path"]
            modified = target.read_bytes() + b"\r\n// unknown local change\r\n"
            target.write_bytes(modified)
            result = self.invoke(root)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Vendor file differs", result.stdout + result.stderr)
            self.assertEqual(target.read_bytes(), modified)


if __name__ == "__main__":
    unittest.main()
