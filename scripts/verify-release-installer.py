"""Qualify the real per-user installer built from the qualified distribution (RQ-A).

Builds nothing: takes the setup made by scripts/make-installer.ps1 from dist/VibeRa Studio,
extracts it, installs into a temporary folder (no shortcuts or registry), checks every
installed file against the distribution's provenance hashes, launches the installed app
(isolated --mcp-test mode) and reads its version, reinstalls over it (upgrade path),
uninstalls, and checks the user's settings folder was not touched. A pass keeps only the
receipt so the single-distribution audit stays clean; a failure keeps everything.

usage: python scripts/verify-release-installer.py [setup.exe] [output folder]
"""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import uuid

ROOT = Path(__file__).resolve().parents[1]
DIST = ROOT / "dist/VibeRa Studio"
setup = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/windows/installer/VibeRa-Studio-Setup.exe").resolve()
base = Path(sys.argv[2] if len(sys.argv) > 2 else ROOT / "build/module-qualification/release-quality/installer").resolve()
output = base / ("attempt-" + uuid.uuid4().hex)
output.mkdir(parents=True, exist_ok=False)
checks = []


def check(ok, label):
    if not ok:
        raise AssertionError(label)
    checks.append(label)


def digest(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def powershell(*args, env=None):
    return subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", *args],
                          capture_output=True, text=True, timeout=900, env=env)


def settings_snapshot():
    folder = Path(os.environ["APPDATA"]) / "VibeRa Studio"
    if not folder.is_dir():
        return {}
    return {str(p.relative_to(folder)): (p.stat().st_size, p.stat().st_mtime_ns)
            for p in folder.rglob("*") if p.is_file()}


passed = False
receipt = {}
try:
    provenance = json.loads((DIST / "provenance.json").read_text(encoding="utf-8"))
    expected = {row["path"]: row["sha256"].lower() for row in provenance["files"]}
    check(setup.is_file(), "The setup exists: " + str(setup))
    receipt.update(setup=str(setup), setupSha256=digest(setup), sourceCommit=provenance["sourceCommit"],
                   distributionFiles=len(expected))
    settings_before = settings_snapshot()

    extracted = output / "extracted"
    subprocess.run([str(setup), "/C", f"/T:{extracted}", "/Q"], timeout=600)
    check(all((extracted / n).exists() for n in ("install.ps1", "uninstall.ps1", "payload.zip")),
          "The setup carries the installer scripts and the payload")

    target = output / "Programs" / "VibeRa Studio"
    install = powershell("-File", str(extracted / "install.ps1"), "-Target", str(target), "-NoShortcuts", "-NoRegistry")
    check(install.returncode == 0 and (target / "VibeRa Studio.exe").is_file() and (target / "uninstall.ps1").is_file(),
          "Install unpacks the app and its uninstaller: " + install.stderr[-300:])
    installed = {p.relative_to(target).as_posix(): p for p in target.rglob("*") if p.is_file()}
    missing = sorted(set(expected) - set(installed))
    wrong = sorted(path for path, sha in expected.items() if path in installed and digest(installed[path]) != sha)
    extra = sorted(set(installed) - set(expected) - {"uninstall.ps1", "provenance.json"})
    check(not missing, "Every distribution file is installed; missing: " + ", ".join(missing[:5]))
    check(not wrong, "Every installed file matches its provenance hash; different: " + ", ".join(wrong[:5]))
    check(not extra, "The install adds only its uninstaller; extra: " + ", ".join(extra[:5]))

    spec = importlib.util.spec_from_file_location("client", ROOT / "skills/vibera-studio/scripts/stdio_client.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    client = module.VibeRaClient(target / "VibeRa Studio.exe")
    try:
        version = client.server.get("version")
        state = client.call("vibera_state", {}, allow_failure=True).get("structuredContent", {})
    finally:
        client.close()
    import re
    expected_version = re.search(r"project\(VibeRa VERSION ([0-9.]+)", (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")).group(1)
    check(version == expected_version, f"The installed app runs and reports version {expected_version}: {version}")
    check(isinstance(state, dict) and "rack" in state, "The installed app serves its project state")
    receipt["installedVersion"] = version

    (target / "stale.txt").write_text("old", encoding="utf-8")
    again = powershell("-File", str(extracted / "install.ps1"), "-Target", str(target), "-NoShortcuts", "-NoRegistry")
    check(again.returncode == 0 and not (target / "stale.txt").exists() and
          digest(target / "VibeRa Studio.exe") == expected["VibeRa Studio.exe"],
          "Installing over an existing install replaces it cleanly (upgrade path)")

    env = dict(os.environ, VIBERA_UNINSTALL_WAIT="1")
    remove = powershell("-File", str(target / "uninstall.ps1"), "-Target", str(target), "-NoShortcuts", "-NoRegistry", env=env)
    check(remove.returncode == 0 and not target.exists(), "Uninstall removes the program folder: " + remove.stderr[-300:])
    check(settings_snapshot() == settings_before, "The user's settings folder is unchanged by install, run and uninstall")
    passed = True
finally:
    if passed:
        shutil.rmtree(output / "extracted", ignore_errors=True)
        shutil.rmtree(output / "Programs", ignore_errors=True)
    sys.path.insert(0, str(ROOT / "scripts"))
    from dev_support import content_identity
    sources = ["scripts/verify-release-installer.py", "scripts/make-installer.ps1", "packaging/install.ps1",
               "packaging/uninstall.ps1", "skills/vibera-studio/scripts/stdio_client.py"]
    receipt["dependencyHashes"] = [dict(path=s, mode="content", sha256=content_identity(ROOT / s)["contentSha256"])
                                   for s in sources]
    receipt["candidateHashes"] = [dict(path=p.relative_to(ROOT).as_posix(), sha256=digest(p)) for p in
                                  (DIST / "VibeRa Studio.exe", DIST / "VibeRa Plugin Worker.exe", setup) if p.is_file()]
    receipt.update(passed=passed, checks=checks,
                   scope="Temporary-folder install without shortcuts/registry; not a clean-machine or setup-UI test")
    (output / "verification.json").write_text(json.dumps(receipt, indent=2), encoding="utf-8")
    print(json.dumps(dict(passed=passed, checks=len(checks), receipt=str(output / "verification.json"))))
sys.exit(0 if passed else 1)
