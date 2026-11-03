"""Read-only canonical package/source identity and obsolete-output inventory."""
import io
import json
import os
from pathlib import Path
import subprocess
import zipfile

import distribution as d


def audit():
    root = d.ROOT
    folder = root / 'dist/VibeRa Studio'
    d.verify(folder)
    manifest = json.loads((folder / 'provenance.json').read_text())
    archive = zipfile.ZipFile(io.BytesIO(subprocess.check_output(
        d.GIT + ['archive', '--format=zip', manifest['sourceCommit']])))
    differences = []
    recorded = {row['path'] for row in manifest['sourceHashes']}
    for row in manifest['sourceHashes']:
        d.require(d.source_identity_matches(row, archive.read(row['path'])),
                  'Committed source mismatch: ' + row['path'])
        path = root / row['path']
        if not path.is_file() or not d.source_identity_matches(row, path.read_bytes()):
            differences.append(row['path'])
    additions = sorted(row['path'] for row in d.source_state() if row['path'] not in recorded)
    names = {'vibera studio.exe', 'vibera.exe', 'vibera plugin worker.exe',
             'viberatests.exe', 'viberascannerfixture.exe', 'coproducertests.exe',
             'automationtests.exe', 'viberaparameterfixture.vst3', 'viberamixerlatencyfixture.vst3',
             'viberaclockwaveformfixture.vst3', 'viberainstrumentfixture.vst3',
             'viberadynamicinstrumentfixture.vst3', 'viberabuschoicefixture.vst3',
             'viberahistorytests.exe', 'viberacompensationtests.exe', 'viberaphysicalbursttests.exe'}
    names.update({'viberastretchexperiment.exe', 'viberatuneexperiment.exe',
                  'viberatunecorrectionexperiment.exe', 'viberacallbackprobe.exe'})
    names.add('viberareleaseinstrumentfixture.vst3')
    dependencies = {'fl-reference-analysis', 'coproducer-research', 'mcp-client', 'pdf-reader'}
    copies, obsolete, errors, preserved_sources, compilation_locations = [], [], [], [], []
    for base in ['dist', 'build', 'artifacts']:
        tree = root / base
        if not tree.exists():
            continue
        for directory, dirs, files in os.walk(tree, followlinks=False,
                                              onerror=lambda e: errors.append(str(e))):
            current = Path(directory)
            rel = current.relative_to(root)
            dirs[:] = [name for name in dirs if name not in {'.git', 'node_modules', '__pycache__'}
                       and not Path(current, name).is_junction()
                       and not Path(current, name).is_symlink()
                       and not (len(rel.parts) == 1 and name in dependencies)]
            for name in files:
                path = current / name
                relative = path.relative_to(root).as_posix()
                canonical = path.is_relative_to(folder)
                if name == 'CMakeCache.txt' and not path.is_relative_to(root / 'build/windows'):
                    compilation_locations.append(relative)
                if name.lower() in names:
                    copies.append(relative)
                    if not canonical:
                        obsolete.append(relative)
                elif name.lower().endswith('.zip'):
                    obsolete.append(relative)
                elif name == 'model-worker.cjs' and not canonical:
                    # A preserved source worktree is not a runtime bundle.
                    if path.parent.name == 'coproducer' and path.parent.parent.name == 'scripts':
                        preserved_sources.append(relative)
                    else:
                        obsolete.append(relative)
    temporary = [name for name in ['build/package-stage', 'build/package-input', 'dist/.previous']
                 if (root / name).exists()]
    audio = 'src/AudioEngine.cpp'
    audio_identity = next((row for row in manifest['sourceHashes'] if row['path'] == audio), None)
    report = dict(canonicalExecutable='dist/VibeRa Studio/VibeRa Studio.exe',
                  sourceCommit=manifest['sourceCommit'], verifiedSourceInputs=len(recorded),
                  verifiedPackageFiles=len(manifest['files']),
                  audioEngineSha256=d.digest(archive.read(audio)),
                  audioEngineMatchesWorkingSource=bool(audio_identity and d.source_identity_matches(audio_identity, (root / audio).read_bytes())),
                  ongoingSourceDifferences=differences, ongoingSourceAdditions=additions,
                  applicationCopies=[p for p in copies if Path(p).name.lower() in {'vibera studio.exe', 'vibera.exe'}],
                  workerCopies=[p for p in copies if Path(p).name.lower() == 'vibera plugin worker.exe'],
                  candidateObsoleteOutputs=obsolete, temporaryFolders=temporary,
                  obsoleteCompilationLocations=compilation_locations,
                  preservedSourceWorkers=preserved_sources,
                  excludedEngineeringDependencies=sorted(dependencies), errors=errors,
                  passed=not (obsolete or temporary or errors or compilation_locations))
    output = root / 'build/single-folder-current-inventory.json'
    output.parent.mkdir(exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(audit())
