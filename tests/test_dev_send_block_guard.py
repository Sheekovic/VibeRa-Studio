"""Original optional send-block guard preflight; no build/provider/DSP work."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

import test_dev_local_source_guard as fixtures
from test_dev_summing_guard import load_verifier

ROOT = Path(__file__).resolve().parents[1]
FAMILY = 'patches/tracktion-send-gain-block/manifest.json'
NEW_HEADER = 'modules/tracktion_graph/tracktion_graph/tracktion_SendGainBlock.h'
ZERO = 'patches/tracktion-zero-history-creation/manifest.json'
INVERSE = 'patches/tracktion-inverse-stage-residual/manifest.json'
SEAL = 'patches/tracktion-inverse-stage-seal/manifest.json'
MIDI = 'patches/tracktion-native-midi-ingress/manifest.json'
FINAL = 'patches/tracktion-plugin-final-output/manifest.json'
BRIDGE = 'patches/tracktion-app-source-port-bridge/manifest.json'
AUDIO_ZERO = 'patches/tracktion-prepared-audio-zero/manifest.json'
MEMORY = 'patches/tracktion-context-memory-policy/manifest.json'
ARRANGER = 'patches/tracktion-empty-arranger-proof/manifest.json'
EXCHANGE = 'patches/tracktion-whole-source-exchange/manifest.json'
ZERO_CHANNEL = 'patches/tracktion-zero-channel-delay/manifest.json'
STABLE_MIDI = 'patches/tracktion-stable-track-midi/manifest.json'
SOURCE_TIME = 'patches/tracktion-source-render-time/manifest.json'
LEGACY_MIDI = 'patches/tracktion-legacy-midi-runtime/manifest.json'
SOURCE_GATE = 'patches/tracktion-production-source-gate/manifest.json'
RENDER = 'patches/tracktion-render-worker-policy/manifest.json'
CONTINUATION = 'patches/tracktion-finished-forward-continuation/manifest.json'
CONTEXT_WAIT = 'patches/tracktion-context-wait-deadline/manifest.json'
LATE_PINS = [('modules/tracktion_engine/playback/graph/tracktion_NodeRenderContext.cpp', 'b3e480e824dd91350bb86574f07c17ee5ee5618e5d38d3dba209a2adca2cb343', '570f9487a286d0dc43ca6617167b454044fe62e34904bd557af71f2300d47c35'), ('modules/tracktion_graph/tracktion_graph/nodes/tracktion_LatencyNode.h', '08377d8b098ccd1cc92024fa08b673bde57c99466f2c8037058606d398b7d849', '6928c85405ae2a9962da3ed45700ecb382e35103af40f99024cabfc733093054')]


class SendBlockGuardTests(unittest.TestCase):
    def test_exact_absent_header_and_nonterminal_successor_contracts(self):
        (ROOT / "build").mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='vibera-send-block-guard-') as temporary:
            root = Path(temporary)
            authority = fixtures.load_guard(ROOT)
            for name in fixtures.fixture_inputs(ROOT, authority.APPROVED):
                target = root / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes((ROOT / name).read_bytes())
            guard = fixtures.load_guard(root)
            verifier = load_verifier()
            family = next(f for f in guard.APPROVED['families'] if f['path'] == FAMILY)
            self.assertEqual(len(family['files']), 6)
            prefix_family = next(f for f in guard.APPROVED['families'] if f['path'] == 'patches/tracktion-node-prefix-clear/manifest.json')
            self.assertEqual(len(prefix_family['files']), 1)
            prefix_row = prefix_family['files'][0]
            self.assertEqual(prefix_row['path'], 'modules/tracktion_graph/tracktion_graph/tracktion_Node.h')
            self.assertEqual((prefix_row['addedLines'], prefix_row['deletedLines']), (8, 1))
            self.assertEqual(prefix_row['lineage'], ['patches/tracktion-' + name + '/manifest.json' for name in ('retained-history', 'retained-midi', 'stable-bus-stage')])
            absent = [e for e in family['files'] if e.get('originalAbsent', False)]
            self.assertEqual([e['path'] for e in absent], [NEW_HEADER])
            stages = guard.APPROVED['sourceBurstFence']['files'] + [e for f in guard.APPROVED['families'] if f['path'] not in (ZERO, INVERSE, SEAL, MIDI, FINAL, BRIDGE, AUDIO_ZERO, MEMORY, ARRANGER, EXCHANGE, ZERO_CHANNEL, STABLE_MIDI, SOURCE_TIME, LEGACY_MIDI, SOURCE_GATE, RENDER, CONTINUATION, CONTEXT_WAIT) for e in f['files']]
            self.assertEqual((len({e['path'] for e in stages}), len(stages)), (35, 59))
            late = [f for f in guard.APPROVED['families'] if f['path'] in (RENDER, CONTINUATION)]
            self.assertEqual([f['path'] for f in late], [RENDER, CONTINUATION])
            late_rows = [e for f in late for e in f['files']]
            self.assertEqual([(e['path'], e['originalSha256'], e['patchedSha256']) for e in late_rows], LATE_PINS)
            current_prefix = stages + late_rows
            self.assertEqual((len({e['path'] for e in current_prefix}), len(current_prefix)), (36, 61))
            zero = next(f for f in guard.APPROVED['families'] if f['path'] == ZERO)
            self.assertEqual({e['path']: e['originalSha256'] for e in zero['files']}, {
                'modules/tracktion_graph/tracktion_graph/nodes/tracktion_LatencyNode.h': '8546eb26c6e86688c43cb267fdc27fdc487aaf4638f146d218c9c87cafa57c0f',
                'modules/tracktion_graph/utilities/tracktion_LatencyProcessor.h': 'b3c4c51a558977555807cbfc2e72d6aeaf059b29942086835a387b1d6f1a1ce2'})
            final_stages = stages + zero['files']
            self.assertEqual((len({e['path'] for e in final_stages}), len(final_stages)), (35, 61))
            inverse = next(f for f in guard.APPROVED['families'] if f['path'] == INVERSE)
            self.assertEqual({e['path']: e['originalSha256'] for e in inverse['files']}, {'modules/tracktion_graph/tracktion_graph/nodes/tracktion_LatencyNode.h': 'd87227a1272c9d0a1a35781bdc667accbf77c1d36977e4e8e73a1ff00b318517', 'modules/tracktion_graph/utilities/tracktion_LatencyProcessor.h': '7b53f5d0baff6ccca2ba667ddb8709c5b33784b6ca854acdc274c22592340d6d'})
            all_stages = final_stages + inverse['files']
            self.assertEqual((len({e['path'] for e in all_stages}), len(all_stages)), (35, 63))
            seal = next(f for f in guard.APPROVED['families'] if f['path'] == SEAL)
            self.assertEqual([(e['path'], e['originalSha256']) for e in seal['files']], [('modules/tracktion_graph/tracktion_graph/nodes/tracktion_LatencyNode.h', '9f2cc965396a6892bb8f329fbdfcad4e2048559a415e00e3d348ed0901e085ff')])
            sealed_stages = all_stages + seal['files']
            self.assertEqual((len({e['path'] for e in sealed_stages}), len(sealed_stages)), (35, 64))
            midi = next(f for f in guard.APPROVED['families'] if f['path'] == MIDI)
            self.assertEqual({e['path']: e['originalSha256'] for e in midi['files']}, {'modules/tracktion_engine/playback/graph/tracktion_LiveMidiInjectingNode.h': '0739e47a4e6e9e155566f7ba24ea9088f9f4a77b06aee16e92825ae35e2be5cd', 'modules/tracktion_engine/playback/graph/tracktion_LiveMidiInjectingNode.cpp': 'a7931b42a1efd8052405020b477da8057be4ac971952b51715c2d8a1209e89ff', 'modules/tracktion_engine/playback/graph/tracktion_EditNodeBuilder.h': 'bc0e47f34c3ed634b31d6e1d934e10ab9bd8f264f57dbfdfb9af0a6c1d11b704', 'modules/tracktion_engine/playback/graph/tracktion_EditNodeBuilder.cpp': '5470b9b2a04459a5fc3a657405d318b96109a596769b70c84c0e2eb032d53a85', 'modules/tracktion_graph/tracktion_graph/tracktion_Node.h': '484b39777a7ef9d48eb54a6577843941cf51046d1e010befcddec3e44c5ab1d4'})
            midi_stages = sealed_stages + midi['files']
            self.assertEqual((len({e['path'] for e in midi_stages}), len(midi_stages)), (37, 69))
            output = next(f for f in guard.APPROVED['families'] if f['path'] == FINAL)
            output_stages = midi_stages + output['files']
            self.assertEqual((len({e['path'] for e in output_stages}), len(output_stages)), (37, 75))
            bridge = next(f for f in guard.APPROVED['families'] if f['path'] == BRIDGE)
            bridge_stages = output_stages + bridge['files']
            self.assertEqual((len({e['path'] for e in bridge_stages}), len(bridge_stages)), (37, 80))
            audio_zero = next(f for f in guard.APPROVED['families'] if f['path'] == AUDIO_ZERO)
            audio_stages = bridge_stages + audio_zero['files']
            self.assertEqual((len({e['path'] for e in audio_stages}), len(audio_stages)), (37, 83))
            memory = next(f for f in guard.APPROVED['families'] if f['path'] == MEMORY)
            memory_stages = audio_stages + memory['files']
            self.assertEqual((len({e['path'] for e in memory_stages}), len(memory_stages)), (37, 88))
            arranger = next(f for f in guard.APPROVED['families'] if f['path'] == ARRANGER)
            arranger_stages = memory_stages + arranger['files']
            self.assertEqual((len({e['path'] for e in arranger_stages}), len(arranger_stages)), (37, 89))
            exchange = next(f for f in guard.APPROVED['families'] if f['path'] == EXCHANGE)
            exchange_stages = arranger_stages + exchange['files']
            self.assertEqual((len({e['path'] for e in exchange_stages}), len(exchange_stages)), (37, 92))
            zero_channel = next(f for f in guard.APPROVED['families'] if f['path'] == ZERO_CHANNEL)
            zero_channel_stages = exchange_stages + zero_channel['files']
            self.assertEqual((len({e['path'] for e in zero_channel_stages}), len(zero_channel_stages)), (37, 94))
            stable_midi = next(f for f in guard.APPROVED['families'] if f['path'] == STABLE_MIDI)
            stable_midi_stages = zero_channel_stages + stable_midi['files']
            self.assertEqual((len({e['path'] for e in stable_midi_stages}), len(stable_midi_stages)), (37, 99))
            source_time = next(f for f in guard.APPROVED['families'] if f['path'] == SOURCE_TIME)
            source_time_stages = stable_midi_stages + source_time['files']
            self.assertEqual((len({e['path'] for e in source_time_stages}), len(source_time_stages)), (37, 101))
            legacy_midi = next(f for f in guard.APPROVED['families'] if f['path'] == LEGACY_MIDI)
            legacy_midi_stages = source_time_stages + legacy_midi['files']
            self.assertEqual((len({e['path'] for e in legacy_midi_stages}), len(legacy_midi_stages)), (37, 104))
            source_gate = next(f for f in guard.APPROVED['families'] if f['path'] == SOURCE_GATE)
            source_gate_stages = legacy_midi_stages + source_gate['files']
            self.assertEqual((len({e['path'] for e in source_gate_stages}), len(source_gate_stages)), (37, 110))
            current_stages = source_gate_stages + late_rows
            self.assertEqual((len({e['path'] for e in current_stages}), len(current_stages)), (38, 112))
            prefix = 'vendor/source/' + guard.APPROVED['sourceDirectory'] + '/'
            # All normalization is disposable and follows checked postimages.
            for item in guard.preflight():
                target = guard.ordinary(root / item['rel'], required=item['before'] is not None)
                if item['pending']:
                    target.write_bytes(item['final'])
            final = {i['rel']: (root / i['rel']).read_bytes() for i in guard.preflight()}
            stamp = {name: (root / name).stat().st_mtime_ns for name in final}
            self.assertFalse(any(i['pending'] for i in guard.preflight()))
            self.assertEqual(stamp, {name: (root / name).stat().st_mtime_ns for name in final})
            # Reconstruct only the five existing send predecessors. The newly
            # created header has explicit absence, not a fictional empty file.
            existing = [e for e in family['files'] if not e.get('originalAbsent', False)]
            self.assertEqual(len(existing), 5)
            verifier.prepare_source_stages(root, guard, [(e, prefix + e['path'], False) for e in existing])
            newpath = root / (prefix + NEW_HEADER)
            newpath.unlink()
            changes = guard.preflight()
            self.assertEqual(sum(i['pending'] for i in changes), 6)
            row = next(i for i in changes if i['entry']['path'] == NEW_HEADER)
            self.assertIsNone(row['before'])
            self.assertEqual(guard.digest(row['final']), absent[0]['patchedSha256'])
            # Verify-only preflight plans creation without writing any file.
            self.assertFalse(newpath.exists())
            for raw, message in ((b'', 'absent, not an empty impostor'), (b'// unknown\n', 'unknown/ambiguous vendor')):
                newpath.write_bytes(raw)
                before = {i['rel']: guard.read_target(i['rel'], i['chain'][0]['entry'].get('originalAbsent', False)) for i in changes}
                with self.assertRaisesRegex(ValueError, message):
                    guard.preflight()
                self.assertEqual(before, {name: guard.read_target(name, name == prefix + NEW_HEADER) for name in before})
                newpath.unlink()
            # The new absence permission never admits missing existing sources.
            victim = root / (prefix + existing[0]['path'])
            original = victim.read_bytes()
            victim.unlink()
            with self.assertRaisesRegex(ValueError, 'required ancestry/input missing'):
                guard.preflight()
            victim.write_bytes(original)
            payload = root / absent[0]['patch']
            original_payload = payload.read_bytes()
            payload.write_bytes(original_payload + b'\n')
            with self.assertRaisesRegex(ValueError, 'payload exact identity'):
                guard.preflight()
            payload.write_bytes(original_payload)
            self.assertEqual(sum(i['pending'] for i in guard.preflight()), 6)
            # Restore only exact reviewed images; no application/vendor paths
            # outside this TemporaryDirectory have been changed by this test.
            for item in guard.preflight():
                if item['pending']:
                    (root / item['rel']).write_bytes(item['final'])
            self.assertFalse(any(i['pending'] for i in guard.preflight()))
            self.assertEqual(final, {i['rel']: (root / i['rel']).read_bytes() for i in guard.preflight()})


if __name__ == '__main__':
    unittest.main()
