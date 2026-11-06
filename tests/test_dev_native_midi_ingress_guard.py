"""Original disposable native MIDI source guards; no build/provider/activation."""
import ast
import contextlib
import io
from pathlib import Path
import sys
import tempfile
import unittest

import test_dev_local_source_guard as fixtures
from test_dev_summing_guard import load_verifier

ROOT=Path(__file__).resolve().parents[1]
FAMILY='patches/tracktion-native-midi-ingress/manifest.json'
NODE='modules/tracktion_graph/tracktion_graph/tracktion_Node.h'
BUILDER='modules/tracktion_engine/playback/graph/tracktion_EditNodeBuilder.cpp'
HEADER='modules/tracktion_engine/playback/graph/tracktion_EditNodeBuilder.h'
LIVE='modules/tracktion_engine/playback/graph/tracktion_LiveMidiInjectingNode'
RENDER='patches/tracktion-render-worker-policy/manifest.json'
CONTINUATION='patches/tracktion-finished-forward-continuation/manifest.json'
LATE_PINS=[('modules/tracktion_engine/playback/graph/tracktion_NodeRenderContext.cpp', 'b3e480e824dd91350bb86574f07c17ee5ee5618e5d38d3dba209a2adca2cb343', '570f9487a286d0dc43ca6617167b454044fe62e34904bd557af71f2300d47c35'), ('modules/tracktion_graph/tracktion_graph/nodes/tracktion_LatencyNode.h', '08377d8b098ccd1cc92024fa08b673bde57c99466f2c8037058606d398b7d849', '6928c85405ae2a9962da3ed45700ecb382e35103af40f99024cabfc733093054')]


class NativeMidiIngressGuardTests(unittest.TestCase):
    def setUp(self):
        (ROOT/'build').mkdir(exist_ok=True)
        self.temporary=tempfile.TemporaryDirectory(prefix='native-midi-guard-')
        self.root=Path(self.temporary.name)
        authority=fixtures.load_guard(ROOT)
        self.guard = fixtures.copy_fixture(ROOT, self.root, authority.APPROVED, profile="all-final")
        self.family=next(f for f in self.guard.APPROVED['families'] if f['path']==FAMILY)
        self.prefix='vendor/source/'+self.guard.APPROVED['sourceDirectory']+'/'
        self.verifier=load_verifier()

    def tearDown(self):
        self.temporary.cleanup()

    def snapshot(self, known_paths=None):
        # After guard.main already revalidates the whole chain, observe every
        # baseline target freshly without performing that transaction twice.
        paths = [c['rel'] for c in self.guard.preflight()] if known_paths is None else known_paths
        return {name:((self.root/name).read_bytes(),(self.root/name).stat().st_mtime_ns)
                for name in paths}

    def test_historical_37_69_chain_and_closed_37_75_all_five_predecessors(self):
        rows=self.family['files']
        self.assertEqual(len(rows),5)
        self.assertEqual({e['path'] for e in rows},{LIVE+'.h',LIVE+'.cpp',NODE,BUILDER,HEADER})
        self.assertEqual({e['path']:e['originalSha256'] for e in rows if not e['lineage']},{
            LIVE+'.h':'0739e47a4e6e9e155566f7ba24ea9088f9f4a77b06aee16e92825ae35e2be5cd',
            LIVE+'.cpp':'a7931b42a1efd8052405020b477da8057be4ac971952b51715c2d8a1209e89ff'})
        self.assertEqual(next(e for e in rows if e['path']==NODE)['lineage'],['patches/tracktion-'+s+'/manifest.json' for s in ('retained-history','retained-midi','stable-bus-stage','node-prefix-clear')])
        self.assertEqual(next(e for e in rows if e['path']==BUILDER)['lineage'],['patches/tracktion-'+s+'/manifest.json' for s in ('sidechain-bus','owned-metering','detached-graph','send-gain-block')])
        self.assertEqual(next(e for e in rows if e['path']==HEADER)['lineage'],['patches/tracktion-detached-graph/manifest.json'])
        current=self.snapshot()
        self.verifier.prepare_source_stages(self.root,self.guard,[(e,self.prefix+e['path'],False) for e in rows])
        planned=self.guard.preflight()
        historical=list(self.guard.APPROVED['sourceBurstFence']['files'])
        for family in self.guard.APPROVED['families']:
            if family['path']=='patches/tracktion-plugin-final-output/manifest.json':break
            historical.extend(family['files'])
        self.assertEqual((len({e['path'] for e in historical}),len(historical)),(37,69))
        output = next(f for f in self.guard.APPROVED['families'] if f['path']=='patches/tracktion-plugin-final-output/manifest.json')
        output_stages=historical+output['files']
        self.assertEqual((len({e['path'] for e in output_stages}),len(output_stages)),(37,75))
        bridge = next(f for f in self.guard.APPROVED['families'] if f['path']=='patches/tracktion-app-source-port-bridge/manifest.json')
        bridge_stages=output_stages+bridge['files']
        self.assertEqual((len({e['path'] for e in bridge_stages}),len(bridge_stages)),(37,80))
        audio_zero = next(f for f in self.guard.APPROVED['families'] if f['path']=='patches/tracktion-prepared-audio-zero/manifest.json')
        audio_stages=bridge_stages+audio_zero['files']
        self.assertEqual((len({e['path'] for e in audio_stages}),len(audio_stages)),(37,83))
        memory = next(f for f in self.guard.APPROVED['families'] if f['path']=='patches/tracktion-context-memory-policy/manifest.json')
        memory_stages=audio_stages+memory['files']
        self.assertEqual((len({e['path'] for e in memory_stages}),len(memory_stages)),(37,88))
        arranger = next(f for f in self.guard.APPROVED['families'] if f['path']=='patches/tracktion-empty-arranger-proof/manifest.json')
        arranger_stages=memory_stages+arranger['files']
        self.assertEqual((len({e['path'] for e in arranger_stages}),len(arranger_stages)),(37,89))
        exchange = next(f for f in self.guard.APPROVED['families'] if f['path']=='patches/tracktion-whole-source-exchange/manifest.json')
        exchange_stages=arranger_stages+exchange['files']
        self.assertEqual((len({e['path'] for e in exchange_stages}),len(exchange_stages)),(37,92))
        zero_channel = next(f for f in self.guard.APPROVED['families'] if f['path']=='patches/tracktion-zero-channel-delay/manifest.json')
        zero_channel_stages=exchange_stages+zero_channel['files']
        self.assertEqual((len({e['path'] for e in zero_channel_stages}),len(zero_channel_stages)),(37,94))
        stable_midi = next(f for f in self.guard.APPROVED['families'] if f['path']=='patches/tracktion-stable-track-midi/manifest.json')
        stable_midi_stages=zero_channel_stages+stable_midi['files']
        self.assertEqual((len({e['path'] for e in stable_midi_stages}),len(stable_midi_stages)),(37,99))
        source_time = next(f for f in self.guard.APPROVED['families'] if f['path']=='patches/tracktion-source-render-time/manifest.json')
        source_time_stages=stable_midi_stages+source_time['files']
        self.assertEqual((len({e['path'] for e in source_time_stages}),len(source_time_stages)),(37,101))
        legacy = next(f for f in self.guard.APPROVED['families'] if f['path']=='patches/tracktion-legacy-midi-runtime/manifest.json')
        self.assertEqual({e['path']:e['originalSha256'] for e in legacy['files']},{
            LIVE+'.h':'7afc0e140b138b502a54192adafc22e78b46b93231c68315f1115a00b0218d98',
            LIVE+'.cpp':'b9a735f459b4ad9e1fd16cae7362d35652720db8ebfb8dffabd982a58504fa49',
            BUILDER:'fcddd584a6ccbd438fbdb7b9e16cdef3ef4ee55e72ffc860256d6053862b1470'})
        legacy_stages=source_time_stages+legacy['files']
        self.assertEqual((len({e['path'] for e in legacy_stages}),len(legacy_stages)),(37,104))
        gate = next(f for f in self.guard.APPROVED['families'] if f['path']=='patches/tracktion-production-source-gate/manifest.json')
        self.assertEqual(len(gate['files']),6)
        gate_stages=legacy_stages+gate['files']
        self.assertEqual((len({e['path'] for e in gate_stages}),len(gate_stages)),(37,110))
        late=[f for f in self.guard.APPROVED['families'] if f['path'] in (RENDER,CONTINUATION)]
        self.assertEqual([f['path'] for f in late],[RENDER,CONTINUATION])
        late_rows=[e for f in late for e in f['files']]
        self.assertEqual([(e['path'],e['originalSha256'],e['patchedSha256']) for e in late_rows],LATE_PINS)
        current_stages=gate_stages+late_rows
        self.assertEqual((len({e['path'] for e in current_stages}),len(current_stages)),(38,112))
        self.assertEqual({e['path'] for e in current_stages}-{e['path'] for e in gate_stages},{LATE_PINS[0][0]})
        wait = next(f for f in self.guard.APPROVED['families'] if f['path'] ==
                    'patches/tracktion-context-wait-deadline/manifest.json')
        self.assertEqual(len(wait['files']), 1)
        current_stages += wait['files']
        self.assertEqual((len({e['path'] for e in current_stages}), len(current_stages)), (38,113))
        self.assertEqual((len(planned),sum(len(c['chain']) for c in planned)),(38,113))
        self.assertEqual({c['entry']['path'] for c in planned if c['pending']},{e['path'] for e in rows})
        for c in planned:
            if c['pending']:(self.root/c['rel']).write_bytes(c['final'])
        self.assertFalse(any(c['pending'] for c in self.guard.preflight()))
        for path,(raw,stamp) in current.items():
            self.assertEqual((self.root/path).read_bytes(),raw)
            if path not in {self.prefix+e['path'] for e in rows}:self.assertEqual((self.root/path).stat().st_mtime_ns,stamp)
        stable=self.snapshot();argv=sys.argv
        try:
            for target,prior in ((NODE,'retained-history'),(NODE,'retained-midi'),(NODE,'stable-bus-stage'),(NODE,'node-prefix-clear'),
                                 (BUILDER,'sidechain-bus'),(BUILDER,'owned-metering'),(BUILDER,'detached-graph'),(BUILDER,'send-gain-block'),(HEADER,'detached-graph'),
                                 (LIVE+'.h','native-midi-ingress'),(LIVE+'.cpp','native-midi-ingress'),
                                 (LIVE+'.h','stable-track-midi'),(LIVE+'.cpp','stable-track-midi'),
                                 ('modules/tracktion_engine/playback/tracktion_DeviceManager.h','device-fence'),
                                 ('modules/tracktion_engine/playback/tracktion_DeviceManager.cpp','device-fence')):
                digest=self.guard.digest(stable[self.prefix+target][0])
                sys.argv=['guard','--known-successor','patches/tracktion-'+prior+'/manifest.json',target,digest]
                with contextlib.redirect_stdout(io.StringIO()):self.guard.main()
                self.assertEqual(self.snapshot(stable),stable)
            sys.argv[-1]='0'*64
            with self.assertRaisesRegex(ValueError,'not exact reviewed current final'):self.guard.main()
        finally:sys.argv=argv

    def test_new_targets_unknown_eol_tamper_and_appliers_fail_closed(self):
        before=self.snapshot()
        for target in (self.prefix+LIVE+'.h',self.prefix+LIVE+'.cpp'):
            original=before[target][0]
            for raw in (original+b'// unreviewed source\n',original.replace(b'\n',b'\r\n')):
                (self.root/target).write_bytes(raw)
                with self.assertRaisesRegex(ValueError,'unknown/ambiguous vendor'):self.guard.preflight()
                for name,(expected,stamp) in before.items():
                    self.assertEqual((self.root/name).read_bytes(),raw if name==target else expected)
                    if name!=target:self.assertEqual((self.root/name).stat().st_mtime_ns,stamp)
                (self.root/target).write_bytes(original);before=self.snapshot()
        legacy = next(f for f in self.guard.APPROVED['families'] if f['path']=='patches/tracktion-legacy-midi-runtime/manifest.json')
        gate = next(f for f in self.guard.APPROVED['families'] if f['path']=='patches/tracktion-production-source-gate/manifest.json')
        mutations=[(FAMILY,'contract (raw )?identity'),(legacy['path'],'contract (raw )?identity'),(gate['path'],'contract (raw )?identity')]
        mutations += [(e['patch'],'payload exact identity') for family in (self.family,legacy,gate) for e in family['files']]
        for path,message in mutations:
            target=self.root/path;raw=target.read_bytes();target.write_bytes(raw+b'\n')
            with self.assertRaisesRegex(ValueError,message):self.guard.preflight()
            target.write_bytes(raw);self.assertEqual(self.snapshot(),before)
        specs=(('apply-retained-history-patch.ps1',NODE,'$hash','retained-history'),('apply-retained-midi-patch.ps1',NODE,'$actual','retained-midi'),
               ('apply-sidechain-bus-patch.ps1',BUILDER,'$hash','sidechain-bus'),('apply-owned-metering-patch.ps1',BUILDER,'$actual','owned-metering'))
        for filename,target,value,prior in specs:
            body=(ROOT/'scripts'/filename).read_text();start=body.index('# Exact MIDI-affected native target:');end=body.index('\n'+('    ' if prior=='owned-metering' else '        ')+'}',start)
            admission=body[start:end]
            self.assertIn("$entry.path -ceq '"+target+"'",admission)
            self.assertIn(value+' -cne $entry.originalSha256 -and '+value+' -cne $entry.patchedSha256',admission)
            self.assertIn("--known-successor 'patches/tracktion-"+prior+"/manifest.json' $entry.path "+value,admission)
            failure="if ($LASTEXITCODE -ne 0) {throw 'MIDI-affected native successor verification failed; no source changed'}"
            self.assertIn(failure,admission)
            self.assertLess(admission.index('--known-successor'),admission.index(failure))
            self.assertLess(admission.index(failure),admission.index('return' if prior=='owned-metering' else 'continue'))
            self.assertNotIn('Test-Path',admission)
        inputs=ast.literal_eval(next(n.value for n in ast.parse((ROOT/'scripts/distribution.py').read_bytes()).body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='BUILD_GUARD_INPUTS' for t in n.targets)))
        self.assertTrue({'scripts/'+row[0] for row in specs}<=set(inputs))
        fence=(ROOT/'scripts/apply-device-fence-patch.ps1').read_text()
        start=fence.index('# Exact DeviceManager successors require the complete canonical chain,')
        end=fence.index('        $successor=$null',start)
        admission=fence[start:end]
        self.assertIn("$entry.path -cin @('modules/tracktion_engine/playback/tracktion_DeviceManager.h',",admission)
        self.assertIn("'modules/tracktion_engine/playback/tracktion_DeviceManager.cpp') -and",admission)
        self.assertIn('$actual -cne $entry.originalSha256 -and $actual -cne $entry.patchedSha256',admission)
        self.assertIn('--known-successor $manifestPath $entry.path $actual',admission)
        refusal="if ($LASTEXITCODE -ne 0) {throw 'Device fence canonical successor verification failed; no source changed'}"
        self.assertIn(refusal,admission)
        self.assertLess(admission.index('--known-successor'),admission.index(refusal))
        self.assertLess(admission.index(refusal),admission.index('continue'))
        self.assertNotIn('Test-Path',admission)
        self.assertIn('scripts/apply-device-fence-patch.ps1',inputs)
        batch_start=fence.index('    if ($changes.Count -gt 0) {')
        batch_end=fence.index('    foreach ($change in $changes) {',batch_start)
        batch=fence[batch_start:batch_end]
        self.assertIn('        $patches=@($changes | ForEach-Object {$_.patch})',batch)
        self.assertIn('        & git @arguments',batch)
        self.assertIn('Device fence isolated combined preflight failed; no dependency source changed',batch)
        self.assertNotIn('--allow-empty',fence)




if __name__=='__main__':unittest.main()
