"""Bind the stereo routing increment to actual tests; inherited gaps stay deferred."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source-log', required=True)
parser.add_argument('--native-output', required=True)
args = parser.parse_args()
read = lambda p: json.loads((root / p).read_text(encoding='utf-8-sig'))
sha = lambda p: hashlib.sha256((root / p).read_bytes()).hexdigest()
write = lambda p, v: (root / p).write_text(json.dumps(v, indent=2) + '\n', encoding='utf-8')
app = 'build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe'
mcp = read('build/windows/mixer_routing_mcp/verification.json')
assert mcp['passed'] and mcp['checks'] >= 48 and mcp['exitCode'] == 0 and mcp['applicationSha256'] == sha(app)
for path in ['build/windows/mixer_routing_gui/mixer-routing-gui.txt',
             'build/windows/gui_acceptance/mixer-routing-held-take.txt',
             'build/windows/acceptance/mixer-routing-core.txt',
             'build/windows/acceptance/mixer-routing-effect-latency.txt']:
    assert (root / path).read_text().startswith('PASS'), path
live = (root / 'build/windows/acceptance/mixer-routing-live.txt').read_text()
assert len(live.splitlines()) == 2
for row in live.splitlines():
    assert all(t in row for t in ['overruns=0', 'silentHeld=0', 'allocations=0', 'destructions=0',
                                 'edit/context/device replacements=0', 'level graph initialisations=0'])
    assert int(re.search(r'loops=(\d+)', row)[1]) >= 2
native = read(str(Path(args.native_output) / 'process.json'))
assert native['passed'] and native['exitCode'] == 0 and not native['forcedClose'] and native['applicationSha256'] == sha(app)
log = root / args.source_log
raw = log.read_bytes()
summary = re.search(r'100% tests passed, 0 tests failed out of (\d+)', raw.decode('utf-16' if raw.startswith(b'\xff\xfe') else 'utf-8-sig'))
assert summary and int(summary[1]) == len(re.findall(r'^add_test\(', (root / 'CMakeLists.txt').read_text(), re.M))
assert log.stat().st_mtime >= (root / app).stat().st_mtime
inputs = {i['path']: i['sha256'] for i in read('build/build-inputs.json')}
for path in ['scripts/verify-mixer-routing-mcp.py', 'scripts/verify-mixer-routing-desktop.py',
             'scripts/record-mixer-routing-evidence.py', 'scripts/apply-mixer-send-patch.ps1',
             'skills/vibera-studio/SKILL.md', 'skills/vibera-studio/references/mixer-routing.md',
             'skills/vibera-studio/references/workflows.md',
             'skills/vibera-studio/scripts/stdio_client.py']:
    inputs[path] = sha(path)
assert all(sha(p) == digest for p, digest in inputs.items()), 'Acceptance inputs changed'
cases = ['Shared source selection, destination toggle, all-other-route removal, strict finite levels and cycle/missing-target errors',
         'Actual JUCE narrow FX-pane/clipped-send input isolation, drag/fine/wheel/unity/percent dialog/cancel/stale-source/macro builder, exact one Undo/Redo and Escape selection preservation',
         'Owned native destination/source/menu/drag/RightShift/Escape/CtrlZ/Y/S, saved XML and owned-window screenshots',
         'Schema18 genuine legacy defaults, stable sparse targets, source round trip, atomic failed batch and exact Undo/Redo',
         'Semantic typed discovery/readback, context selection, lease ownership/expiry/cancel, actual save/reopen/WAV and real async failure',
         'Actual direct/bus/parallel PCM sums and original SDK VST3 effect/bypass alignment at fixed one-block host latency',
         'Pattern/Song repeated levels/topology/history/loops retain Edit/context/device/cache and held voices; graph lifecycle counts explicit',
         'Local and semantic send edits preserve held recording take; Stop remains a separate history entry']
receipt = 'docs/mcp/MIXER_ROUTING_EVIDENCE.json'
write(receipt, dict(passed=True, applicationSha256=sha(app), sourceInputs=inputs, checks=cases,
                   sourceSuites=f'{summary[1]}/{summary[1]}', mcpChecks=mcp['checks'], nativeInput=args.native_output,
                   listening=False, physicalMidi=False,
                   scope='Stereo post-fader buses/sends; full MX-02, sidechains, arbitrary channel layouts and nonzero/dynamic plugin-latency PDC remain unqualified'))
inventory = read('docs/mcp/SURFACE_INVENTORY.json')
inventory['operations'] = [o for o in inventory['operations'] if o['id'] != 'mixer.routing']
inventory['operations'].append(dict(id='mixer.routing', name='Stereo Mixer buses and post-fader sends',
    source='src/StudioMixer.cpp', canonical='Controller mixer.route/routeOnly/send, shared source selection and leased mixerSend gestures',
    mcp='vibera_command', additionalInterfaces=['vibera_mixer', 'vibera_gesture', 'vibera_batch', 'vibera_inspect', 'vibera_project'],
    commands=['mixer.route', 'mixer.routeOnly', 'mixer.send'],
    readback=['vibera_mixer:state', 'vibera_inspect:mixer', 'vibera_gesture:state', 'vibera_state:mixerRouting', 'vibera_project:job'],
    skill='skills/vibera-studio/references/mixer-routing.md',
    verification=dict(status='passed', receipt=receipt, inputs=inputs, cases=cases)))
write('docs/mcp/SURFACE_INVENTORY.json', inventory)
# Only dedicated new routing adapters are reviewed here. Shared inherited editor
# handlers are not qualified wholesale by the scoped routing cases.
spec = importlib.util.spec_from_file_location('coverage_gate', root / 'scripts/mcp-coverage.py')
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)
reviewed = read('docs/mcp/REVIEWED_SURFACES.json')
for entry in gate.inventory()['customSurfaceCandidates']:
    if entry['surface'].split(':')[0] == 'src/StudioMixer.cpp':
        reviewed[entry['surface']] = dict(sourceSha256=entry['sourceSha256'],
            operations=['mixer.route', 'mixer.routeOnly', 'mixer.send'], mcp='vibera_command',
            additionalInterfaces=['vibera_mixer', 'vibera_gesture'],
            readback=['vibera_mixer:state', 'vibera_inspect:mixer', 'vibera_gesture:state'],
            skill='skills/vibera-studio/references/mixer-routing.md',
            verification=dict(status='passed', receipt=receipt, inputs=inputs, cases=cases))
write('docs/mcp/REVIEWED_SURFACES.json', reviewed)
print('Recorded scoped stereo routing evidence; inherited full MCP gate remains incomplete')
