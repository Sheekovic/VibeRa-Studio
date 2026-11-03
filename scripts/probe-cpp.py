"""One owned Release/x64 ClCompile probe; not a full build or qualification.

Run through: python scripts/dev.py owned "C++ probe" -- python -B
scripts/probe-cpp.py --target VibeRaCore src/Commands.cpp
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import stat
import subprocess
import xml.etree.ElementTree as ET

from dev_support import EngineeringOwner, configure_required
import distribution

ROOT = Path(__file__).resolve().parents[1]


def ordinary_path(root, path):
    """Reject aliases/reparse points instead of probing outside this checkout."""
    relative = path.relative_to(root)
    current = root
    for part in (None, *relative.parts):
        if part is not None:
            current /= part
        info = current.lstat()
        if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT:
            raise ValueError("Reparse/symlink probe paths are unsupported")
    return path


def validate(root, target, source):
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", target):
        raise ValueError("One explicit existing CMake target is required")
    candidate = Path(source)
    if not candidate.is_absolute():
        candidate = root / candidate
    if ".." in candidate.parts or any(c in str(candidate) for c in ";%$\r\n"):
        raise ValueError("Unsupported source path/MSBuild property characters")
    relative = candidate.relative_to(root)
    if not relative.parts or relative.parts[0] not in ("src", "tests") or candidate.suffix != ".cpp":
        raise ValueError("Probe exactly one original src/tests .cpp")
    ordinary_path(root, candidate)
    if not candidate.is_file():
        raise ValueError("Source file does not exist")
    project = ordinary_path(root, root / "build/windows" / (target + ".vcxproj"))
    if configure_required(root, [target]):
        raise RuntimeError("Existing canonical cache/target needs configuration; probe refuses to configure")
    tree = ET.parse(project)
    ns = {"m": "http://schemas.microsoft.com/developer/msbuild/2003"}
    if not any(e.get("Include") == "Release|x64" for e in tree.findall(".//m:ProjectConfiguration", ns)):
        raise ValueError("Existing target does not support Release|x64")
    sources = []
    for entry in tree.findall(".//m:ClCompile", ns):
        name = entry.get("Include")
        if name:
            path = Path(name)
            sources.append(path if path.is_absolute() else project.parent / path)
    if candidate.resolve() not in (p.resolve() for p in sources):
        raise ValueError("Source is not a ClCompile member of the selected target")
    return project, candidate


def installed_msbuild():
    # Same installed VS/VC-tools discovery contract as dev.py/build.ps1.
    vswhere = Path("C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe")
    installation = subprocess.check_output(
        [str(vswhere), "-latest", "-products", "*", "-requires",
         "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"],
        text=True).strip()
    if not installation:
        raise RuntimeError("Installed Visual Studio C++ tools required")
    executable = Path(installation) / "MSBuild/Current/Bin/MSBuild.exe"
    if not executable.is_file():
        raise RuntimeError("Installed MSBuild missing; no installation/fetch attempted")
    return executable


def probe(root, target, source):
    if not os.environ.get("VIBERA_DEV_OWNER_TOKEN"):
        raise RuntimeError("Use scripts/dev.py owned; an inherited engineering owner is required")
    # EngineeringOwner verifies the token, live PID and creation identity.
    with EngineeringOwner(root, "nested C++ compile probe") as owner:
        if not owner.nested:
            raise RuntimeError("Probe requires an inherited owner")
        project, candidate = validate(root, target, source)
        executable = installed_msbuild()
        # Do this BEFORE the probe, so later dev.py refresh does not invalidate
        # this newly compiled TU a second time solely for the same input change.
        distribution.refresh_inputs()
        command = [str(executable), str(project), "/t:ClCompile", "/m:1", "/nr:false",
                   "/p:Configuration=Release", "/p:Platform=x64",
                   "/p:SelectedFiles=" + str(candidate), "/verbosity:minimal"]
        result = owner.run(command, cwd=root)
        print(f"C++ probe: exit {result['exitCode']}, {result['seconds']:.3f}s; "
              "no link/full-build/release qualification", flush=True)
        return result["exitCode"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", required=True)
    parser.add_argument("source")
    args = parser.parse_args()
    return probe(ROOT, args.target, args.source)


if __name__ == "__main__":
    raise SystemExit(main())
