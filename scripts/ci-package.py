# SPDX-License-Identifier: AGPL-3.0-or-later
"""Archive native GitHub build candidates without touching the qualified dist.

This is CI packaging only, not the local release acceptance/promotion transaction.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import zipfile


ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "out"


def run(*args):
    return subprocess.check_output(args, cwd=ROOT, text=True).strip()


def checksum(path):
    with path.open("rb") as source:
        digest = hashlib.file_digest(source, "sha256").hexdigest()
    path.with_name(path.name + ".sha256").write_text(
        f"{digest}  {path.name}\n", encoding="utf-8"
    )
    return digest


def require(path):
    if not path.is_file() or path.stat().st_size == 0:
        raise RuntimeError(f"Missing build output: {path.relative_to(ROOT)}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platform", choices=("windows-x64", "linux-x64", "macos-universal"))
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--tag", default="")
    parser.add_argument("--record-installer", type=Path)
    args = parser.parse_args()
    if os.environ.get("GITHUB_ACTIONS") != "true":
        parser.error("This command is for GitHub's isolated runners; use package.ps1 for local releases")
    if args.record_installer:
        installer = args.record_installer.resolve()
        if installer.parent != OUT or installer.suffix.lower() != ".exe":
            parser.error("Installer must be an executable directly under out/")
        require(installer)
        print(checksum(installer), installer.name)
        return
    if not args.platform or not args.build_dir:
        parser.error("--platform and --build-dir are required")
    build = args.build_dir.resolve()
    if not build.is_relative_to(ROOT / "build"):
        parser.error("Build directory must be inside the repository build directory")
    version_match = re.search(
        r"project\(VibeRa\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)",
        (ROOT / "CMakeLists.txt").read_text(encoding="utf-8"),
    )
    if not version_match:
        raise RuntimeError("Cannot determine the source version")
    version = version_match[1]
    if args.tag and args.tag != "v" + version:
        raise RuntimeError(f"Release tag {args.tag!r} does not match source version v{version}")
    artifact = build / "VibeRa_artefacts" / "Release"
    stage = OUT / "stage"
    stage.mkdir(parents=True, exist_ok=False)
    mac = args.platform == "macos-universal"
    if mac:
        source = artifact / "VibeRa Studio.app"
        if not source.is_dir():
            raise RuntimeError("The build produced no VibeRa Studio.app")
        app = stage / source.name
        shutil.copytree(source, app, symlinks=True)
        runtime = app / "Contents" / "MacOS"
        executable = runtime / "VibeRa Studio"
        worker = runtime / "VibeRa Plugin Worker"
    else:
        app = stage / "VibeRa Studio"
        app.mkdir()
        for item in artifact.iterdir():
            if item.suffix.lower() in (".pdb", ".ilk"):
                continue
            destination = app / item.name
            if item.is_dir():
                shutil.copytree(item, destination, symlinks=True)
            else:
                shutil.copy2(item, destination)
        runtime = app
        extension = ".exe" if args.platform == "windows-x64" else ""
        executable = runtime / ("VibeRa Studio" + extension)
        worker = runtime / ("VibeRa Plugin Worker" + extension)
    for path in (executable, worker, runtime / "Content/Synthesizer/library.json",
                 runtime / "CoProducer/model-worker.cjs", runtime / "CoProducer/anticipation-worker.py"):
        require(path)
    shutil.copy2(ROOT / "LICENSE", runtime / "LICENSE")
    shutil.copytree(ROOT / "licenses", runtime / "licenses")
    shutil.copytree(ROOT / "examples", runtime / "Examples")
    (runtime / "BUILD-CANDIDATE.txt").write_text(
        "Native build candidate. Audio, recording, plugins and native UI acceptance remain unverified.\n"
        + ("Ad-hoc signed; not Developer ID signed or notarized.\n" if mac else "")
        + ("Requires Ubuntu 24.04-compatible system audio/GUI libraries.\n" if args.platform == "linux-x64" else ""),
        encoding="utf-8",
    )
    name = f"VibeRa-Studio-{version}-{args.platform}"
    if mac:
        for binary in (executable, worker):
            run("lipo", str(binary), "-verify_arch", "arm64", "x86_64")  # lipo wants the file first.
        # All bundle resources are finalized before signing. No paid identity or
        # notarization credentials are assumed by this open-source workflow.
        run("codesign", "--force", "--deep", "--sign", "-", str(app))
        run("codesign", "--verify", "--deep", "--strict", str(app))
        archive = OUT / (name + ".zip")
        run("ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", str(app), str(archive))
    elif args.platform == "linux-x64":
        for binary in (executable, worker):
            data = binary.read_bytes()[:20]
            if data[:4] != b"\x7fELF" or data[4:6] != b"\x02\x01" or data[18:20] != b"\x3e\x00":
                raise RuntimeError(f"Expected a Linux x86-64 ELF executable: {binary.name}")
            libraries = run("ldd", str(binary))
            if "not found" in libraries:
                raise RuntimeError(f"Unresolved libraries for {binary.name}:\n{libraries}")
        archive = OUT / (name + ".tar.gz")
        with tarfile.open(archive, "w:gz") as target:
            target.add(app, arcname=app.name)
    else:
        archive = OUT / (name + ".zip")
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as target:
            for path in sorted(app.rglob("*")):
                if path.is_file():
                    target.write(path, path.relative_to(stage))
    manifest = {
        "schemaVersion": 1,
        "version": version,
        "platform": args.platform,
        "sourceCommit": run("git", "-c", f"safe.directory={ROOT.as_posix()}", "rev-parse", "HEAD"),
        "archiveBase": name,
        "archive": archive.name,
        "archiveSha256": checksum(archive),
        "executableSha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
        "workerSha256": hashlib.sha256(worker.read_bytes()).hexdigest(),
        "runtimeAcceptance": "unverified",
        "signing": "ad-hoc, not notarized" if mac else "unsigned",
    }
    (OUT / f"candidate-{args.platform}.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )
    print(archive.relative_to(ROOT))


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))
