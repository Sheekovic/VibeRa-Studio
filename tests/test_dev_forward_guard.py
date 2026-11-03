"""Original source-only combined-family guards; no compiler/provider/audio."""
import contextlib
import importlib.util
import io
from pathlib import Path
import sys
import tempfile
import unittest

import test_dev_local_source_guard as fixtures
from test_dev_summing_guard import load_verifier

ROOT = Path(__file__).resolve().parents[1]
FORWARD = 'patches/tracktion-forward-stage-drain/manifest.json'
PHASE = 'patches/tracktion-native-phase-probe/manifest.json'
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
NODE = 'modules/tracktion_graph/tracktion_graph/nodes/tracktion_LatencyNode.h'
PROCESSOR = 'modules/tracktion_graph/utilities/tracktion_LatencyProcessor.h'


class ForwardGuardTests(unittest.TestCase):
    def setUp(self):
        (ROOT / "build").mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix='vibera-combined-native-guard-')
        self.root = Path(self.temporary.name)
        authority = fixtures.load_guard(ROOT)
        self.guard = fixtures.copy_fixture(ROOT, self.root, authority.APPROVED, profile="all-final")
        self.verifier = load_verifier()
        self.family = next(f for f in self.guard.APPROVED['families'] if f['path'] == FORWARD)
        self.phase = next(f for f in self.guard.APPROVED['families'] if f['path'] == PHASE)
        self.prefix = 'vendor/source/' + self.guard.APPROVED['sourceDirectory'] + '/'

    def tearDown(self):
        self.temporary.cleanup()

    def snapshot(self, known_paths=None):
        # Successful main() just validated the complete chain. Observe every
        # baseline target freshly without repeating that whole transaction.
        paths = [item['rel'] for item in self.guard.preflight()] if known_paths is None else known_paths
        return {name: ((self.root / name).read_bytes(), (self.root / name).stat().st_mtime_ns)
                for name in paths}

    def test_closed_targets_and_independently_counted_combined_closure(self):
        self.assertEqual({row['path'] for row in self.family['files']}, {NODE, PROCESSOR})
        self.assertEqual(len(self.family['files']), 2)
        self.assertEqual(len(self.phase['files']), 5)
        stages = list(self.guard.APPROVED['sourceBurstFence']['files'])
        stages += [entry for family in self.guard.APPROVED['families'] if family['path'] not in (ZERO, INVERSE, SEAL, MIDI, FINAL, BRIDGE, AUDIO_ZERO, MEMORY, ARRANGER, EXCHANGE, ZERO_CHANNEL, STABLE_MIDI, SOURCE_TIME, LEGACY_MIDI, SOURCE_GATE, RENDER, CONTINUATION, CONTEXT_WAIT) for entry in family['files']]
        self.assertEqual(len({row['path'] for row in stages}), 35)
        self.assertEqual(len(stages), 59)
        late = [f for f in self.guard.APPROVED['families'] if f['path'] in (RENDER, CONTINUATION)]
        self.assertEqual([f['path'] for f in late], [RENDER, CONTINUATION])
        late_rows = [e for f in late for e in f['files']]
        self.assertEqual([(e['path'], e['originalSha256'], e['patchedSha256']) for e in late_rows], LATE_PINS)
        current_prefix = stages + late_rows
        self.assertEqual((len({e['path'] for e in current_prefix}), len(current_prefix)), (36, 61))
        zero = next(f for f in self.guard.APPROVED['families'] if f['path'] == ZERO)
        self.assertEqual({e['path']: e['originalSha256'] for e in zero['files']}, {
            NODE: '8546eb26c6e86688c43cb267fdc27fdc487aaf4638f146d218c9c87cafa57c0f',
            PROCESSOR: 'b3c4c51a558977555807cbfc2e72d6aeaf059b29942086835a387b1d6f1a1ce2'})
        final_stages = stages + zero['files']
        self.assertEqual((len({e['path'] for e in final_stages}), len(final_stages)), (35, 61))
        inverse = next(f for f in self.guard.APPROVED['families'] if f['path'] == INVERSE)
        self.assertEqual({e['path']: e['originalSha256'] for e in inverse['files']}, {'modules/tracktion_graph/tracktion_graph/nodes/tracktion_LatencyNode.h': 'd87227a1272c9d0a1a35781bdc667accbf77c1d36977e4e8e73a1ff00b318517', 'modules/tracktion_graph/utilities/tracktion_LatencyProcessor.h': '7b53f5d0baff6ccca2ba667ddb8709c5b33784b6ca854acdc274c22592340d6d'})
        all_stages = final_stages + inverse['files']
        self.assertEqual((len({e['path'] for e in all_stages}), len(all_stages)), (35, 63))
        seal = next(f for f in self.guard.APPROVED['families'] if f['path'] == SEAL)
        self.assertEqual([(e['path'], e['originalSha256']) for e in seal['files']], [('modules/tracktion_graph/tracktion_graph/nodes/tracktion_LatencyNode.h', '9f2cc965396a6892bb8f329fbdfcad4e2048559a415e00e3d348ed0901e085ff')])
        sealed_stages = all_stages + seal['files']
        self.assertEqual((len({e['path'] for e in sealed_stages}), len(sealed_stages)), (35, 64))
        midi = next(f for f in self.guard.APPROVED['families'] if f['path'] == MIDI)
        self.assertEqual({e['path']: e['originalSha256'] for e in midi['files']}, {'modules/tracktion_engine/playback/graph/tracktion_LiveMidiInjectingNode.h': '0739e47a4e6e9e155566f7ba24ea9088f9f4a77b06aee16e92825ae35e2be5cd', 'modules/tracktion_engine/playback/graph/tracktion_LiveMidiInjectingNode.cpp': 'a7931b42a1efd8052405020b477da8057be4ac971952b51715c2d8a1209e89ff', 'modules/tracktion_engine/playback/graph/tracktion_EditNodeBuilder.h': 'bc0e47f34c3ed634b31d6e1d934e10ab9bd8f264f57dbfdfb9af0a6c1d11b704', 'modules/tracktion_engine/playback/graph/tracktion_EditNodeBuilder.cpp': '5470b9b2a04459a5fc3a657405d318b96109a596769b70c84c0e2eb032d53a85', 'modules/tracktion_graph/tracktion_graph/tracktion_Node.h': '484b39777a7ef9d48eb54a6577843941cf51046d1e010befcddec3e44c5ab1d4'})
        midi_stages = sealed_stages + midi['files']
        self.assertEqual((len({e['path'] for e in midi_stages}), len(midi_stages)), (37, 69))
        output = next(f for f in self.guard.APPROVED['families'] if f['path'] == FINAL)
        output_stages = midi_stages + output['files']
        self.assertEqual((len({e['path'] for e in output_stages}), len(output_stages)), (37, 75))
        bridge = next(f for f in self.guard.APPROVED['families'] if f['path'] == BRIDGE)
        bridge_stages = output_stages + bridge['files']
        self.assertEqual((len({e['path'] for e in bridge_stages}), len(bridge_stages)), (37, 80))
        audio_zero = next(f for f in self.guard.APPROVED['families'] if f['path'] == AUDIO_ZERO)
        audio_stages = bridge_stages + audio_zero['files']
        self.assertEqual((len({e['path'] for e in audio_stages}), len(audio_stages)), (37, 83))
        memory = next(f for f in self.guard.APPROVED['families'] if f['path'] == MEMORY)
        memory_stages = audio_stages + memory['files']
        self.assertEqual((len({e['path'] for e in memory_stages}), len(memory_stages)), (37, 88))
        arranger = next(f for f in self.guard.APPROVED['families'] if f['path'] == ARRANGER)
        arranger_stages = memory_stages + arranger['files']
        self.assertEqual((len({e['path'] for e in arranger_stages}), len(arranger_stages)), (37, 89))
        exchange = next(f for f in self.guard.APPROVED['families'] if f['path'] == EXCHANGE)
        exchange_stages = arranger_stages + exchange['files']
        self.assertEqual((len({e['path'] for e in exchange_stages}), len(exchange_stages)), (37, 92))
        zero_channel = next(f for f in self.guard.APPROVED['families'] if f['path'] == ZERO_CHANNEL)
        zero_channel_stages = exchange_stages + zero_channel['files']
        self.assertEqual((len({e['path'] for e in zero_channel_stages}), len(zero_channel_stages)), (37, 94))
        stable_midi = next(f for f in self.guard.APPROVED['families'] if f['path'] == STABLE_MIDI)
        stable_midi_stages = zero_channel_stages + stable_midi['files']
        self.assertEqual((len({e['path'] for e in stable_midi_stages}), len(stable_midi_stages)), (37, 99))
        source_time = next(f for f in self.guard.APPROVED['families'] if f['path'] == SOURCE_TIME)
        source_time_stages = stable_midi_stages + source_time['files']
        self.assertEqual((len({e['path'] for e in source_time_stages}), len(source_time_stages)), (37, 101))
        legacy_midi = next(f for f in self.guard.APPROVED['families'] if f['path'] == LEGACY_MIDI)
        legacy_midi_stages = source_time_stages + legacy_midi['files']
        self.assertEqual((len({e['path'] for e in legacy_midi_stages}), len(legacy_midi_stages)), (37, 104))
        source_gate = next(f for f in self.guard.APPROVED['families'] if f['path'] == SOURCE_GATE)
        source_gate_stages = legacy_midi_stages + source_gate['files']
        self.assertEqual((len({e['path'] for e in source_gate_stages}), len(source_gate_stages)), (37, 110))
        current_stages = source_gate_stages + late_rows
        self.assertEqual((len({e['path'] for e in current_stages}), len(current_stages)), (38, 112))
        processor = next(row for row in self.family['files'] if row['path'] == PROCESSOR)
        self.assertEqual(processor['lineage'], [])
        node = next(row for row in self.family['files'] if row['path'] == NODE)
        self.assertEqual(node['lineage'], ['patches/tracktion-' + name + '/manifest.json' for name in
            ('retained-history', 'retained-midi', 'consumed-phase', 'stable-bus-stage', 'stage-difference')])

    def test_both_pinned_originals_reconstruct_then_final_without_peer_mutation(self):
        latest = self.snapshot()
        for row in self.family['files']:
            self.verifier.prepare_source_predecessor(self.root, self.guard, row, self.prefix + row['path'])
        before = self.snapshot()
        pending = [item for item in self.guard.preflight() if item['pending']]
        self.assertEqual({item['entry']['path'] for item in pending}, {NODE, PROCESSOR})
        self.assertEqual(len(pending), 2)
        for item in pending:
            (self.root / item['rel']).write_bytes(item['final'])
        after = self.snapshot()
        for name, (raw, mtime) in latest.items():
            self.assertEqual(after[name][0], raw)
            if name not in {self.prefix + NODE, self.prefix + PROCESSOR}:
                self.assertEqual(after[name], before[name])

    def test_all_earlier_owners_recognize_only_exact_pinned_latest_endpoint_read_only(self):
        snapshot = self.snapshot()
        digest = self.guard.digest(snapshot[self.prefix + NODE][0])
        argv = sys.argv
        try:
            for name in ('retained-history', 'retained-midi', 'consumed-phase', 'stable-bus-stage', 'stage-difference', 'forward-stage-drain', 'zero-history-creation', 'inverse-stage-residual'):
                sys.argv = ['guard-structural-graph.py', '--known-successor',
                            'patches/tracktion-' + name + '/manifest.json', NODE, digest]
                with contextlib.redirect_stdout(io.StringIO()):
                    self.guard.main()
                self.assertEqual(self.snapshot(snapshot), snapshot)
            sys.argv[-1] = '0' * 64
            with self.assertRaisesRegex(ValueError, 'not exact reviewed current final'):
                self.guard.main()
        finally:
            sys.argv = argv

    def test_unknown_raw_or_eol_on_either_native_header_refuses_without_writes(self):
        before = self.snapshot()
        for target in (self.prefix + NODE, self.prefix + PROCESSOR):
            original = before[target][0]
            for raw in (original + b'// unknown original guard fixture\n', original.replace(b'\n', b'\r\n')):
                (self.root / target).write_bytes(raw)
                with self.assertRaisesRegex(ValueError, 'unknown/ambiguous vendor'):
                    self.guard.preflight()
                for name, (unchanged, mtime) in before.items():
                    self.assertEqual((self.root / name).read_bytes(), raw if name == target else unchanged)
                    if name != target:
                        self.assertEqual((self.root / name).stat().st_mtime_ns, mtime)
                (self.root / target).write_bytes(original)
                # Our explicit restoration is outside the operation under test;
                # subsequent scenarios bind their mtime oracle to that new input.
                before = self.snapshot()

    def test_tamper_each_family_manifest_or_payload_refuses_without_vendor_writes(self):
        before = self.snapshot()
        for family in (self.family, self.phase, next(f for f in self.guard.APPROVED['families'] if f['path'] == ZERO), next(f for f in self.guard.APPROVED['families'] if f['path'] == INVERSE), next(f for f in self.guard.APPROVED['families'] if f['path'] == SEAL), next(f for f in self.guard.APPROVED['families'] if f['path'] == MIDI), next(f for f in self.guard.APPROVED['families'] if f['path'] == CONTEXT_WAIT)):
            for name, message in ((family['path'], 'contract raw identity'),
                                  (family['files'][0]['patch'], 'payload exact identity')):
                path = self.root / name
                raw = path.read_bytes()
                path.write_bytes(raw + b'\n')
                with self.assertRaisesRegex(ValueError, message):
                    self.guard.preflight()
                path.write_bytes(raw)
                # The refusal above is the operation under test. Re-read every
                # pinned vendor byte/mtime without replaying a second preflight
                # merely to rediscover the unchanged target list after restoration.
                self.assertEqual(self.snapshot(before), before)

    def test_diagnostic_and_local_historical_stage_reconstruction_survive_later_families(self):
        before = self.snapshot()
        for family_name, target in (
            ('patches/tracktion-stage-difference/manifest.json', NODE),
            ('patches/tracktion-local-source-initial/manifest.json',
             'modules/tracktion_engine/playback/tracktion_EditPlaybackContext.cpp')):
            family = next(f for f in self.guard.APPROVED['families'] if f['path'] == family_name)
            row = next(row for row in family['files'] if row['path'] == target)
            receipt = self.verifier.prepare_source_predecessor(self.root, self.guard, row, self.prefix + target)
            self.assertTrue(receipt['reconstructed'])
            self.assertEqual(self.guard.digest((self.root / (self.prefix + target)).read_bytes()), row['originalSha256'])
            self.assertTrue(any(stage['reverse'] for stage in receipt['stages']))
            for name, (raw, _) in before.items():
                if name != self.prefix + target:
                    self.assertEqual((self.root / name).read_bytes(), raw)
            (self.root / (self.prefix + target)).write_bytes(before[self.prefix + target][0])

    def test_context_wait_stage_preserves_exact_predecessor_and_peer_files(self):
        family = next(f for f in self.guard.APPROVED['families'] if f['path'] == CONTEXT_WAIT)
        self.assertTrue(family['productionEnabled'])
        self.assertEqual(len(family['files']), 1)
        row = family['files'][0]
        target = 'modules/tracktion_engine/playback/tracktion_DeviceManager.cpp'
        self.assertEqual((row['path'], row['originalSha256'], row['patchedSha256']), (
            target, '66fb39c7a9e7c62375064865ddce5c0d463c820201105da157f8c97ab0bfbfb4',
            '3f78ac18c46630ed58f1351658c21285eb267519d66cfa44a47d23d103165036'))
        latest = self.snapshot()
        receipt = self.verifier.prepare_source_predecessor(self.root, self.guard, row,
                                                          self.prefix + target)
        self.assertTrue(receipt['reconstructed'])
        self.assertEqual(self.guard.digest((self.root / (self.prefix + target)).read_bytes()),
                         row['originalSha256'])
        pending = [item for item in self.guard.preflight() if item['pending']]
        self.assertEqual({item['entry']['path'] for item in pending}, {target})
        self.assertEqual(len(pending), 1)
        (self.root / pending[0]['rel']).write_bytes(pending[0]['final'])
        restored = self.snapshot()
        for name, (raw, mtime) in latest.items():
            self.assertEqual(restored[name][0], raw)
            if name != self.prefix + target:
                self.assertEqual(restored[name][1], mtime)
        argv = sys.argv
        try:
            for owner in (SOURCE_GATE, 'patches/tracktion-device-fence/manifest.json'):
                sys.argv = ['guard', '--known-successor', owner, target, row['patchedSha256']]
                with contextlib.redirect_stdout(io.StringIO()):
                    self.guard.main()
                self.assertEqual(self.snapshot(restored), restored)
            # The operation recognizes a genuine predecessor, never its own
            # family as an invented predecessor or a permissive hash bypass.
            sys.argv = ['guard', '--known-successor', CONTEXT_WAIT, target, row['patchedSha256']]
            with self.assertRaisesRegex(ValueError, 'not exact reviewed current final'):
                self.guard.main()
            self.assertEqual(self.snapshot(), restored)
        finally:
            sys.argv = argv


if __name__ == '__main__':
    unittest.main()
