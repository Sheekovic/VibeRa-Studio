"""Single-folder distribution identity and staging. No persistent source ZIPs."""
import hashlib
import importlib.util
import io
import json
import re
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

from dev_support import TEXT, content_identity

ROOT = Path(__file__).resolve().parent.parent
GIT = ['git', '-c', 'safe.directory=' + ROOT.as_posix(), '-C', ROOT.as_posix()]
RUNTIME = ['model-worker.cjs', 'magenta-runtime.cjs', 'package.json', 'package-lock.json',
           'fetch-magenta.mjs', 'anticipation-worker.py', 'anticipation_context.py']


def digest(data):
    return hashlib.sha256(data).hexdigest()


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def variants(data):
    lf = data.replace(b'\r\n', b'\n')
    return {digest(data), digest(lf), digest(lf.replace(b'\n', b'\r\n'))}


def source_identity_matches(item, data):
    """Compare a captured source identity without discarding its raw SHA.

    New provenance has explicit LF content identity. Legacy text provenance
    retains its uniform-EOL compatibility; binary/NUL payloads remain exact.
    A present malformed content hash never falls back to a legacy/raw match.
    """
    if not isinstance(item, dict) or not isinstance(item.get('path'), str):
        return False
    valid = lambda value: isinstance(value, str) and re.fullmatch(r'[0-9a-fA-F]{64}', value)
    if not valid(item.get('sha256')):
        return False
    suffix = Path(item['path']).suffix.lower()
    supported = suffix in TEXT
    # Existing .inc snapshots store contentSha256 as raw bytes. Preserve their
    # source-text EOL compatibility without changing captured/compiled identities.
    legacy_include = suffix == '.inc'
    text = (supported or legacy_include) and b'\0' not in data
    raw = item['sha256'].lower()
    if 'contentSha256' not in item:
        return raw in variants(data) if text else raw == digest(data)
    if not valid(item['contentSha256']):
        return False
    if legacy_include:
        return item['contentSha256'].lower() == raw and (
            raw in variants(data) if text else raw == digest(data))
    # Same explicit suffix-based LF identity as dev_support.content_identity.
    # For NUL payloads it is recorded metadata only, never byte-equivalence.
    content = digest(data.replace(b'\r\n', b'\n') if supported else data)
    return item['contentSha256'].lower() == content and (text or raw == digest(data))


def prepare(inputs, proof_path, stage):
    proof = json.loads(proof_path.read_text(encoding='utf-8-sig'))
    commit = proof.get('sourceArchiveCommit') or proof.get('sourceCommit')
    require(commit and proof.get('sourceHashes'), 'Missing exact source identity')
    require(proof.get('sourceSuites') and proof['sourceSuites'].split('/')[0] == proof['sourceSuites'].split('/')[1], 'Source tests are not all passing')
    archive = zipfile.ZipFile(io.BytesIO(subprocess.check_output(GIT + ['archive', '--format=zip', commit])))
    for item in proof['sourceHashes']:
        require(source_identity_matches(item, archive.read(item['path'])), 'Source mismatch: ' + item['path'])
    binaries = {Path(item['path']).name: item['sha256'].lower() for item in proof['binaries']}
    required = ['VibeRa Studio.exe', 'VibeRa Plugin Worker.exe', 'VibeRaTests.exe',
                'VibeRaScannerFixture.exe', 'VibeRaParameterFixture.vst3']
    if any(item['path'] == 'tests/MixerRiskAcceptance.h' for item in proof['sourceHashes']):
        required.append('VibeRaMixerLatencyFixture.vst3')
    if any(item['path'] == 'tests/PluginBusChoiceAcceptance.h' for item in proof['sourceHashes']):
        required.append('VibeRaBusChoiceFixture.vst3')
    if any(item['path'] == 'tests/NoteReleaseWorkerAcceptance.h' for item in proof['sourceHashes']):
        required.append('VibeRaReleaseInstrumentFixture.vst3')
    for name in required:
        data = (inputs / name).read_bytes()
        require(digest(data) == binaries[name], 'Unverified binary: ' + name)
        (stage / name).write_bytes(data)
    for name in RUNTIME:
        data = archive.read('scripts/coproducer/' + name)
        require(digest((inputs / 'CoProducer' / name).read_bytes()) in variants(data), 'Runtime mismatch: ' + name)
        target = stage / 'CoProducer' / name
        target.parent.mkdir(exist_ok=True)
        target.write_bytes(data)
    for name in archive.namelist():
        if not name.endswith('/') and (name == 'LICENSE' or name.startswith(('licenses/', 'skills/vibera-studio/')) or name in ['docs/DEPENDENCIES.md', 'dependencies.lock.json']):
            target = stage / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(archive.read(name))
    # Factory library and its exact source notices are installation-relative.
    for name in archive.namelist():
        if name.startswith('assets/synthesizer/') and not name.endswith('/'):
            relative = name[len('assets/synthesizer/'):]
            if relative == 'library.json' or relative.startswith(('samples/', 'sources/')):
                target = stage / 'Content/Synthesizer' / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(archive.read(name))
    notice = 'docs/SYNTHESIZER_SOUND_SOURCES.md'
    if notice in archive.namelist():
        target = stage / 'licenses/SYNTHESIZER_SOUND_SOURCES.md'
        target.parent.mkdir(parents=True, exist_ok=True)
        text = archive.read(notice).decode('utf-8-sig')
        text = text.replace('../assets/synthesizer/', '../Content/Synthesizer/')
        target.write_text(text, encoding='utf-8')
    import xml.etree.ElementTree as ET
    example = archive.read('examples/Midnight Session.vibra')
    tree = ET.fromstring(example)
    require(all(c.get('source', '').startswith('builtin:') for c in tree.findall('.//CHANNEL')), 'Only built-in sample sources in bundled example')
    require(not tree.findall('.//PLUGIN'), 'No provider state in bundled example')
    (stage / 'Examples').mkdir()
    (stage / 'Examples/Midnight Session.vibra').write_bytes(example)
    (stage / 'SOURCE.txt').write_text(
        'VibeRa Studio - local open-source development distribution\n'
        'Exact implementation source commit: ' + commit + '\n'
        'Source repository: ' + str(ROOT) + '\n'
        'Use git show / git checkout of this commit; no duplicate source archive is retained.\n'
        'Pinned dependency sources and licenses remain in the main repository/vendor.\n'
        'External publication requires a corresponding-source delivery path.\n' +
        ('Indexed Browser search is included; advanced Browser work remains in source/roadmap.\n'
         if any(row['path'] == 'src/StudioBrowser.cpp' for row in proof['sourceHashes']) else
         'Browser work in progress is preserved in source and is NOT included in these bytes.\n') +
        'CoProducer frameworks/checkpoints are not bundled or newly qualified.\n', encoding='utf-8')
    manifest = dict(format=1, sourceCommit=commit, sourceHashes=proof['sourceHashes'],
                    sourceSuites=proof['sourceSuites'], sourceProof=str(proof_path.relative_to(ROOT)),
                    priorQualification=proof.get('scope'), files=[])
    for field in ['skillVersion', 'mcpContractVersion', 'mcpCoverageComplete', 'mcpCoverageCounts']:
        if field in proof:
            manifest[field] = proof[field]
    for path in sorted(stage.rglob('*')):
        if path.is_file():
            manifest['files'].append(dict(path=path.relative_to(stage).as_posix(), sha256=digest(path.read_bytes())))
    (stage / 'provenance.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps(dict(sourceCommit=commit, files=len(manifest['files']), application=binaries['VibeRa Studio.exe'])))


def verify(folder):
    manifest = json.loads((folder / 'provenance.json').read_text())
    for item in manifest['files']:
        path = folder / item['path']
        require(path.resolve().is_relative_to(folder.resolve()), 'Unsafe manifest path')
        require(digest(path.read_bytes()) == item['sha256'], 'Distribution mismatch: ' + item['path'])
    skill_path = folder / 'skills/vibera-studio/manifest.json'
    if skill_path.exists():
        skill = json.loads(skill_path.read_text())
        reference = json.loads((skill_path.parent / 'references/generated/provenance.json').read_text())
        contracts = json.loads((skill_path.parent / 'references/generated/commands.json').read_text())
        require(manifest.get('skillVersion') == skill['version'], 'Missing/mismatched skill provenance')
        require(manifest.get('mcpContractVersion') == reference['contractVersion'], 'Mismatched MCP contract provenance')
        require(manifest.get('mcpCoverageComplete') == skill['completeFunctionalCoverage'], 'MCP readiness provenance mismatch')
        require(manifest.get('mcpCoverageCounts', {}).get('registered') == len(contracts), 'MCP registry count mismatch')
        require(manifest['mcpCoverageCounts'].get('mapped') == sum(bool(c['supported']) for c in contracts), 'MCP mapping count mismatch')
        require(reference['applicationSha256'] == digest((folder / 'VibeRa Studio.exe').read_bytes()), 'Stale packaged skill reference')
    print('PASS distribution hashes: ' + str(len(manifest['files'])))


BUILD_GUARD_INPUTS = ('scripts/apply-dependency-patches.ps1',
                      'scripts/apply-mixer-send-patch.ps1',
                      'scripts/apply-owned-metering-patch.ps1',
                      'scripts/apply-retained-midi-patch.ps1',
                      'scripts/apply-consumed-phase-patch.ps1',
                      'scripts/apply-sidechain-bus-patch.ps1',
                      'scripts/guard-structural-graph.py',
                      'scripts/apply-structural-graph-patches.ps1',
                      'scripts/apply-device-fence-patch.ps1',
                      'scripts/apply-retained-history-patch.ps1',
                      'scripts/apply-physical-callback-patch.ps1')


def source_state():
    paths = [ROOT / 'CMakeLists.txt', ROOT / 'CMakePresets.json', ROOT / 'dependencies.lock.json']
    paths.extend(ROOT / name for name in BUILD_GUARD_INPUTS)
    for name in ['src', 'tests', 'assets', 'patches', 'scripts/coproducer', 'cmake']:
        paths.extend(p for p in (ROOT / name).rglob('*') if p.is_file() and '__pycache__' not in p.parts)
    from dev_support import content_identity
    return [dict(path=p.relative_to(ROOT).as_posix(), **content_identity(p)) for p in sorted(paths)]


def refresh_inputs():
    # Timestamp refresh records what was invalidated, not a claim that every
    # target was compiled. A partial target build must not bless unbuilt targets
    # or repeatedly touch headers whose content has already been invalidated.
    cache = ROOT / 'build/refreshed-project-inputs.json'
    if not cache.exists():
        cache = ROOT / 'build/compiled-project-inputs.json'
    previous = {row['path']: row for row in json.loads(cache.read_text())} if cache.exists() else {}
    changed = []
    current = source_state()
    for row in current:
        old = previous.get(row['path'], {})
        if old.get('sha256') != row['sha256'] and old.get('contentSha256') != row['contentSha256']:
            # Copies/checkouts may preserve an mtime older than an object file.
            # Force content-changed project inputs through MSBuild dependencies.
            (ROOT / row['path']).touch()
            changed.append(row['path'])
    (ROOT / 'build/refreshed-project-inputs.json').write_text(json.dumps(current, indent=2))
    print('Refreshed timestamps for ' + str(len(changed)) + ' changed/unrecorded project inputs')


def compiled(before_path):
    before = json.loads(before_path.read_text())
    require(before == source_state(), 'Project inputs changed while compiling')
    (ROOT / 'build/compiled-project-inputs.json').write_text(json.dumps(before, indent=2))


def record(before_path, configuration, passed, total):
    before = json.loads(before_path.read_text())
    require(before == source_state(), 'Build inputs changed during verification')
    require(int(passed) == int(total) and int(total) > 0, 'Full source suite did not pass')
    paths = [('VibeRa', 'VibeRa Studio.exe'), ('VibeRaPluginWorker', 'VibeRa Plugin Worker.exe'),
             ('VibeRaTests', 'VibeRaTests.exe'), ('VibeRaScannerFixture', 'VibeRaScannerFixture.exe')]
    binaries = [ROOT / f'build/windows/{target}_artefacts/{configuration}/{name}' for target, name in paths]
    binaries.append(ROOT / f'build/windows/{configuration}/VibeRaParameterFixture.vst3')
    binaries.append(ROOT / f'build/windows/{configuration}/VibeRaMixerLatencyFixture.vst3')
    binaries.append(ROOT / f'build/windows/{configuration}/VibeRaBusChoiceFixture.vst3')
    if any(item['path'] == 'tests/NoteReleaseWorkerAcceptance.h' for item in before):
        binaries.append(ROOT / f'build/windows/{configuration}/VibeRaReleaseInstrumentFixture.vst3')
    commit = subprocess.check_output(GIT + ['rev-parse', 'HEAD'], text=True).strip()
    # A runnable artifact must identify committed source, even when unrelated
    # concurrent drafts exist. Refuse accidental packaging of uncommitted code.
    archive = zipfile.ZipFile(io.BytesIO(subprocess.check_output(GIT + ['archive', '--format=zip', commit])))
    for item in before:
        require(source_identity_matches(item, archive.read(item['path'])), 'Checkpoint build inputs before packaging: ' + item['path'])
    proof = dict(sourceCommit=commit, sourceHashes=before, sourceSuites=f'{passed}/{total}',
                 binaries=[dict(path=p.relative_to(ROOT).as_posix(), sha256=digest(p.read_bytes())) for p in binaries])
    # Ship the exact skill contract/reference for these bytes. This is scoped
    # preview qualification; full MCP readiness may still be false and must be
    # recorded rather than inferred from green musical tests.
    skill = json.loads(archive.read('skills/vibera-studio/manifest.json'))
    reference = json.loads(archive.read('skills/vibera-studio/references/generated/provenance.json'))
    coverage = json.loads(archive.read('docs/mcp/COVERAGE.json'))
    # A stale generated inventory must not retain old reviewed counts after a
    # backend, skill or evidence input changed. Partial previews remain allowed;
    # the complete readiness flag/counts must still describe current authority.
    sys.dont_write_bytecode = True
    spec = importlib.util.spec_from_file_location('vibera_coverage_gate', ROOT / 'scripts/mcp-coverage.py')
    gate = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(gate)
    require(coverage == gate.inventory(), 'Regenerate and checkpoint current MCP coverage; stored inventory is stale')
    authorities = {'scripts/distribution.py', 'scripts/package.ps1', 'scripts/verify-distribution.ps1',
                   'scripts/verify-installed-mcp.py',
                   'scripts/verify-synthesizer-mcp.py', 'scripts/verify-synthesizer-installed.py',
                   'scripts/verify-synthesizer-desktop.py', 'assets/synthesizer/validate_library.py',
                   'docs/modules/SYNTHESIZER.json', 'docs/modules/SYNTHESIZER.md',
                   'docs/feature/VibeRaSynthesizer.md', 'docs/SYNTHESIZER_SOUND_SOURCES.md',
                   'docs/mcp/SYNTHESIZER_EVIDENCE.json', 'docs/mcp/SYNTHESIZER_SURFACES.md',
                   'scripts/check-distribution-retirement.py',
                   'scripts/verify-glue-mcp.py',
                   'scripts/verify-rack-mcp.py',
                   'scripts/verify-step-fill-mcp.py',
                   'scripts/verify-channel-solo-mcp.py',
                   'scripts/verify-channel-management-mcp.py',
                   'scripts/verify-step-velocity-mcp.py',
                   'scripts/verify-swing-mcp.py',
                   'scripts/verify-quantizer-mcp.py',
                   'scripts/verify-groove-mcp.py',
                   'scripts/verify-sampler-voices-mcp.py',
                   'scripts/verify-sampler-tempo-mcp.py',
                   'scripts/verify-channel-appearance-mcp.py',
                   'scripts/verify-channel-scoped-solo-mcp.py',
                   'scripts/verify-loop-bounds-mcp.py',
                   'scripts/verify-normalize-mcp.py',
                   'scripts/verify-sampler-filter-mcp.py',
                   'scripts/verify-sampler-modulation-mcp.py',
                   'scripts/verify-sampler-envelope-mcp.py',
                   'scripts/verify-sampler-automation-mcp.py',
                   'scripts/verify-sampler-controller-mcp.py',
                   'scripts/verify-clip-groups-mcp.py',
                   'scripts/verify-grouping-desktop.py',
                   'scripts/verify-note-mute-mcp.py', 'scripts/verify-note-mute-desktop.py',
                   'scripts/verify-note-slice-mcp.py', 'scripts/verify-note-slice-desktop.py',
                   'scripts/verify-note-stretch-mcp.py', 'scripts/verify-note-stretch-desktop.py',
                   'scripts/verify-note-stretch-batch.py',
                   'scripts/verify-mixer-routing-mcp.py', 'scripts/verify-mixer-routing-desktop.py',
                   'scripts/verify-mixer-controls-mcp.py', 'scripts/verify-mixer-controls-desktop.py',
                   'scripts/verify-plugin-latency-mcp.py',
                   'scripts/verify-plugin-io-mcp.py',
                   'scripts/module-gate.py', 'scripts/build.ps1', 'scripts/package.ps1',
                   'scripts/dev.py', 'scripts/dev.ps1', 'scripts/dev_support.py', 'scripts/dev-select.py',
                   'scripts/engineering-owner.ps1', 'scripts/test.ps1', 'scripts/audit-distribution.py',
                   'scripts/clear-build-runnables.ps1', 'docs/DEV_WORKFLOW.md', 'docs/MIXER_SIDECHAIN_DESIGN.md',
                   'docs/modules/MIXER.json', 'docs/modules/MIXER.md', 'docs/DEVELOPMENT_POLICY.md',
                   'scripts/apply-mixer-send-patch.ps1', 'scripts/apply-dependency-patches.ps1',
                   'scripts/apply-sidechain-bus-patch.ps1', 'scripts/apply-retained-history-patch.ps1',
                   'scripts/apply-retained-midi-patch.ps1', 'scripts/apply-consumed-phase-patch.ps1', 'scripts/apply-ready-graph-patch.ps1',
                   'scripts/guard-structural-graph.py', 'scripts/apply-structural-graph-patches.ps1',
                   'scripts/verify-summing-properties-guard.py',
                   'scripts/verify-stage-difference-guard.py',
                   'scripts/verify-forward-drain-guard.py',
                   'scripts/apply-owned-metering-patch.ps1', 'scripts/apply-vst3-editor-patch.ps1', 'scripts/apply-history-preview-patch.ps1', 'scripts/apply-prepared-history-patch.ps1', 'scripts/apply-number-safety-patch.ps1', 'scripts/apply-device-fence-patch.ps1',
                   'scripts/apply-physical-callback-patch.ps1', 'scripts/verify-physical-callback-guard.py',
                   'scripts/mcp-coverage.py', 'docs/mcp/SURFACE_INVENTORY.json', 'docs/mcp/REVIEWED_SURFACES.json'}
    for entry in coverage['customSurfaceCandidates'] + coverage['explicitUiOperations']:
        if entry['status'] == 'reviewed':
            authorities.add(entry['verification']['receipt'])
            authorities.update(entry['verification']['inputs'])
    for name in authorities:
        require(source_identity_matches(dict(path=name, **content_identity(ROOT / name)), archive.read(name)),
                'Checkpoint the exact coverage authority/evidence before packaging: ' + name)
    require(reference['applicationSha256'].lower() == digest(binaries[0].read_bytes()).lower(),
            'Regenerate and checkpoint the skill references for the exact compiled executable before packaging')
    proof['skillVersion'] = skill['version']
    proof['mcpContractVersion'] = reference['contractVersion']
    proof['mcpCoverageComplete'] = bool(coverage['coverageComplete'])
    proof['mcpCoverageCounts'] = coverage['counts']
    (ROOT / 'build/distribution-candidate.json').write_text(json.dumps(proof, indent=2) + '\n')
    inputs = ROOT / 'build/package-input'
    inputs.mkdir(exist_ok=False)
    for p in binaries:
        shutil.copy2(p, inputs / p.name)
    shutil.copytree(binaries[0].parent / 'CoProducer', inputs / 'CoProducer')
    print('Recorded exact source/binaries/full-suite candidate')


if __name__ == '__main__':
    if sys.argv[1] == 'prepare':
        prepare(*(Path(x).resolve() for x in sys.argv[2:5]))
    elif sys.argv[1] == 'verify':
        verify(Path(sys.argv[2]).resolve())
    elif sys.argv[1] == 'capture':
        Path(sys.argv[2]).write_text(json.dumps(source_state(), indent=2))
    elif sys.argv[1] == 'refresh-inputs':
        refresh_inputs()
    elif sys.argv[1] == 'compiled':
        compiled(Path(sys.argv[2]))
    elif sys.argv[1] == 'check-inputs':
        require(json.loads(Path(sys.argv[2]).read_text()) == source_state(), 'Project inputs changed during selected-target compilation')
    elif sys.argv[1] == 'record':
        record(Path(sys.argv[2]), *sys.argv[3:6])
    else:
        raise SystemExit('Unknown distribution operation')
