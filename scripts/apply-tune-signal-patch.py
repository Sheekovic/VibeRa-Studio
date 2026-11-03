# SPDX-License-Identifier: AGPL-3.0-or-later
"""Apply the explicit MIT research-only phase-frequency experiment."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parent.parent
candidate = Path(sys.argv[1]).resolve()
if candidate != (root / 'build/tune-upstream-a670068').resolve() or not candidate.is_relative_to(root):
    raise RuntimeError('Only the project-owned pinned Signalsmith research copy is authorized')
spec = json.loads((root / 'experiments/tune/signalsmith-phase-frequency.json').read_text())
git = ['git', '-c', 'safe.directory=' + candidate.as_posix(), '-C', str(candidate)]
def inspect(arguments):
    result = subprocess.run(git + arguments, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stderr)
    if result.stderr:
        print(result.stderr.strip())
    return result.stdout.strip()

if inspect(['rev-parse', 'HEAD']) != spec['revision']:
    raise RuntimeError('Research revision changed; preserved')
if spec['path'] != 'signalsmith-stretch.h' or any(p != spec['path'] for p in inspect(['diff', '--name-only', 'HEAD']).splitlines()):
    raise RuntimeError('Other tracked research work exists; preserved')
target = candidate / spec['path']
if not target.resolve().is_relative_to(candidate):
    raise RuntimeError('Linked target escapes the candidate')
before = target.read_bytes()
fingerprint = hashlib.sha256(before).hexdigest()
if fingerprint == spec['preparedSha256']:
    print('Verified existing MIT phase-frequency experiment')
elif fingerprint == spec['originalSha256'] and before.count(spec['old'].encode()) == 1:
    after = before.replace(spec['old'].encode(), spec['new'].encode())
    if hashlib.sha256(after).hexdigest() != spec['preparedSha256']:
        raise RuntimeError('Research patch hash mismatch')
    target.write_bytes(after)
    if hashlib.sha256(target.read_bytes()).hexdigest() != spec['preparedSha256']:
        raise RuntimeError('Written research patch hash mismatch')
    print('Applied research-only MIT phase-frequency patch')
else:
    raise RuntimeError('Unexpected research source preserved without overwrite')
