"""Per-user installer: build with IExpress from a small fixture, extract, install,
upgrade and uninstall into a temporary folder (no shortcuts or registry entries are
written by this test). Does not run the real setup UI or touch the user's install.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import uuid

ROOT = Path(__file__).resolve().parents[1]
output = Path(sys.argv[1]).resolve() / ("attempt-" + uuid.uuid4().hex)
output.mkdir(parents=True, exist_ok=False)
checks = []


def check(ok, label):
    if not ok:
        raise AssertionError(label)
    checks.append(label)


def powershell(*args, env=None):
    return subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", *args],
                          capture_output=True, text=True, timeout=600, env=env)


passed = False
try:
    source = output / "source"
    (source / "Content" / "Samples").mkdir(parents=True)
    (source / "VibeRa Studio.exe").write_bytes(b"MZ fixture")
    (source / "Content" / "Samples" / "kick.wav").write_bytes(b"RIFF fixture")
    (source / "provenance.json").write_text(json.dumps({"version": "9.9.9"}), encoding="utf-8")
    build = powershell("-File", str(ROOT / "scripts/make-installer.ps1"), "-Source", str(source),
                       "-Output", str(output / "installer"))
    setup = output / "installer" / "VibeRa-Studio-Setup.exe"
    check(build.returncode == 0 and setup.exists(), "IExpress builds the setup: " + build.stderr[-300:])
    extracted = output / "extracted"
    subprocess.run([str(setup), "/C", f"/T:{extracted}", "/Q"], timeout=300)
    check(all((extracted / name).exists() for name in ("install.ps1", "uninstall.ps1", "payload.zip")),
          "The setup carries the installer scripts and the payload")
    target = output / "Programs" / "VibeRa Studio"
    install = powershell("-File", str(extracted / "install.ps1"), "-Target", str(target), "-NoShortcuts", "-NoRegistry")
    check(install.returncode == 0 and (target / "VibeRa Studio.exe").exists() and
          (target / "Content" / "Samples" / "kick.wav").exists() and (target / "uninstall.ps1").exists(),
          "Install unpacks the app and its uninstaller: " + install.stderr[-300:])
    check("9.9.9" in install.stdout, "Install reports the payload version")
    (target / "stale.txt").write_text("old", encoding="utf-8")
    again = powershell("-File", str(extracted / "install.ps1"), "-Target", str(target), "-NoShortcuts", "-NoRegistry")
    check(again.returncode == 0 and not (target / "stale.txt").exists() and (target / "VibeRa Studio.exe").exists(),
          "Reinstalling replaces the previous folder cleanly")
    env = dict(os.environ, VIBERA_UNINSTALL_WAIT="1")
    remove = powershell("-File", str(target / "uninstall.ps1"), "-Target", str(target), "-NoShortcuts", "-NoRegistry",
                        env=env)
    check(remove.returncode == 0 and not target.exists(), "Uninstall removes the program folder: " + remove.stderr[-300:])
    passed = True
finally:
    if passed:
        # The setup and extracted payload are runnable copies; a pass keeps only the receipt
        # so the single-distribution audit stays clean (a failure keeps everything).
        for name in ("installer", "extracted", "source"):
            shutil.rmtree(output / name, ignore_errors=True)
    (output / "verification.json").write_text(json.dumps(dict(passed=passed, checks=checks), indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks))))
sys.exit(0 if passed else 1)
