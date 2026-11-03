# SPDX-License-Identifier: AGPL-3.0-or-later
"""Read-only refusal gate before removing/replacing a known application bundle."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re


def inspect(folder):
    if folder.is_symlink() or folder.is_junction():
        raise ValueError("Refuse a linked distribution root")
    manifest = json.loads((folder / "provenance.json").read_text(encoding="utf-8-sig"))
    expected = {}
    for row in manifest["files"]:
        name, digest = row["path"], row["sha256"]
        relative = PurePosixPath(name)
        if (relative.is_absolute() or ".." in relative.parts or str(relative) != name
                or ":" in name or "\\" in name or name in expected
                or not re.fullmatch(r"[0-9a-fA-F]{64}", digest)):
            raise ValueError("Unsafe or duplicate distribution manifest entry")
        expected[name] = digest.lower()
    if not expected:
        raise ValueError("Empty manifest cannot authorize distribution removal")
    expected_directories = {str(parent) for name in expected for parent in PurePosixPath(name).parents
                            if str(parent) != "."}
    actual, directories, pending = {}, set(), [folder]
    while pending:
        directory = pending.pop()
        for child in directory.iterdir():
            if child.is_symlink() or child.is_junction():
                raise ValueError("Linked distribution content preserved: " + str(child))
            if child.is_dir():
                directories.add(child.relative_to(folder).as_posix())
                pending.append(child)
            elif child.is_file():
                actual[child.relative_to(folder).as_posix()] = child
            else:
                raise ValueError("Unknown distribution entry preserved: " + str(child))
    unknown = sorted(set(actual) - set(expected) - {"provenance.json"})
    missing = sorted(set(expected) - set(actual))
    unknown_directories = sorted(directories - expected_directories)
    changed = [name for name, digest in expected.items() if name in actual
               and hashlib.sha256(actual[name].read_bytes()).hexdigest() != digest]
    return {"directory": str(folder), "passed": not (unknown or missing or changed or unknown_directories),
            "sourceCommit": manifest["sourceCommit"], "unmanifestedFiles": unknown,
            "unmanifestedDirectories": unknown_directories,
            "modifiedFiles": changed, "missingFiles": missing,
            "scope": "Read-only data preservation gate; not a release or malicious-manifest authenticity check"}


def authorize(folder, root, staging=False):
    if staging:
        if folder != root / "build/package-stage" or folder.parent.is_symlink() or folder.parent.is_junction():
            raise ValueError("Staging inspection requires the exact unlinked project build/package-stage")
        proof = json.loads((root / "build/distribution-candidate.json").read_text(encoding="utf-8-sig"))
        manifest = json.loads((folder / "provenance.json").read_text(encoding="utf-8-sig"))
        commit = proof.get("sourceCommit", "")
        if not re.fullmatch(r"[0-9a-f]{40}", commit) or manifest.get("sourceCommit") != commit:
            raise ValueError("Staging source does not match the recorded task-owned candidate")
    elif folder not in [root / "dist/VibeRa Studio", root / "dist/.previous"]:
        raise ValueError("Use only the project's canonical or temporary previous distribution")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("folder", type=Path)
    parser.add_argument("--staging", action="store_true", help="Inspect only build/package-stage matching the recorded candidate; retain all content protections")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    folder = args.folder.absolute()
    authorize(folder, root, args.staging)
    result = inspect(folder)
    print(json.dumps(result, indent=2))
    return int(not result["passed"])


if __name__ == "__main__":
    raise SystemExit(main())
