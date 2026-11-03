# SPDX-License-Identifier: AGPL-3.0-or-later
"""Apply the hash-pinned, experimental GPL resampler preparation change."""
import hashlib
import argparse
import json
from pathlib import Path
import subprocess
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('candidate', type=Path)
parser.add_argument('--output-source', action='store_true')
parser.add_argument('--linked-phase', action='store_true')
parser.add_argument('--resource-audit', action='store_true')
args = parser.parse_args()
root = Path(__file__).resolve().parent.parent
candidate = args.candidate.resolve()
expected = (root / "build/tune-rubberband-1d95888").resolve()
if candidate != expected or not candidate.is_relative_to(root):
    raise RuntimeError("Only the existing project-owned pinned research candidate is authorized")
specs = [json.loads((root / "experiments/tune/rubberband-preparation.json").read_text())]
if args.output_source:
    specs.append(json.loads((root / "experiments/tune/rubberband-output-source.json").read_text()))
if args.linked_phase:
    specs.append(json.loads((root / "experiments/tune/rubberband-linked-phase.json").read_text()))
if args.resource_audit:
    specs.append(json.loads((root / "experiments/tune/rubberband-resource-observer.json").read_text()))
allowed = {'src/common/BQResampler.cpp', 'src/finer/R3LiveShifter.cpp', 'src/finer/PhaseAdvance.h', 'src/common/Allocators.h'}
if any(spec['path'] not in allowed for spec in specs):
    raise RuntimeError('Unexpected patch target')
git = ["git", "-c", "safe.directory=" + candidate.as_posix(), "-C", str(candidate)]
def read_git(arguments):
    result = subprocess.run(git + arguments, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stderr.strip() or 'Candidate Git inspection failed')
    if result.stderr:
        # Preserve successful Git notices without turning them into PowerShell
        # native error records or corrupting the parsed stdout identity.
        print(result.stderr.strip())
    return result.stdout


revision = read_git(["rev-parse", "HEAD"]).strip()
if any(revision != spec["revision"] for spec in specs):
    raise RuntimeError("Candidate revision changed; preserve it")
changed = read_git(["diff", "--name-only", "HEAD"]).splitlines()
if any(path not in {spec['path'] for spec in specs} for path in changed):
    raise RuntimeError("Other tracked candidate work exists; preserve it for review")
pending = []
for spec in specs:
    target = candidate / spec['path']
    if not target.resolve().is_relative_to(candidate):
        raise RuntimeError('Linked patch target escapes the research candidate')
    before = target.read_bytes()
    fingerprint = hashlib.sha256(before).hexdigest()
    if fingerprint == spec['preparedSha256']:
        continue
    legacy = fingerprint in spec.get('legacyPreparedSha256', [])
    if fingerprint != spec['originalSha256'] and not legacy:
        raise RuntimeError('Unexpected candidate content preserved without overwrite')
    after = before
    for edit in (spec['legacyEdits'] if legacy else spec.get('edits', [spec])):
        old, new = edit['old'].encode(), edit['new'].encode()
        if after.count(old) != edit.get('count', 1):
            raise RuntimeError('Patch anchor changed')
        after = after.replace(old, new)
    if hashlib.sha256(after).hexdigest() != spec['preparedSha256']:
        raise RuntimeError('Prepared patch hash mismatch')
    pending.append((target, after, spec['preparedSha256']))
# Validate every target before changing any candidate source.
for target, after, fingerprint in pending:
    target.write_bytes(after)
    if hashlib.sha256(target.read_bytes()).hexdigest() != fingerprint:
        raise RuntimeError('Written patch hash mismatch')
print(f'Verified {len(specs)} experimental source patches; applied {len(pending)}; GPL notices retained')
