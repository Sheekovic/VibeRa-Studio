"""Independent historical/successor and mutation source mirrors; all original oracles."""
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
FAMILY='patches/tracktion-plugin-final-output/manifest.json'
BRIDGE='patches/tracktion-app-source-port-bridge/manifest.json'
AUDIO_ZERO='patches/tracktion-prepared-audio-zero/manifest.json'
MEMORY='patches/tracktion-context-memory-policy/manifest.json'
ARRANGER='patches/tracktion-empty-arranger-proof/manifest.json'
EXCHANGE='patches/tracktion-whole-source-exchange/manifest.json'
ZERO_CHANNEL='patches/tracktion-zero-channel-delay/manifest.json'
STABLE_MIDI='patches/tracktion-stable-track-midi/manifest.json'
SOURCE_TIME='patches/tracktion-source-render-time/manifest.json'
SOURCE_TIME_ORIGINALS={'modules/tracktion_engine/plugins/tracktion_Plugin.h': 'f3f8b11ca783f77b4233c1430590d54deabcf6aad0a47a17c9eb8a842d3ba90c', 'modules/tracktion_engine/playback/graph/tracktion_PluginNode.cpp': '9eeb53e6cbac68973cfeed4eef811666b205e6dbd1d8b7a9d80b6c53224aed79'}
SOURCE_TIME_FINALS={'modules/tracktion_engine/plugins/tracktion_Plugin.h': '057cca2e7b865042238ec8aa6cb3fcb4b6a8977aec8bd50ae9ae12c121648486', 'modules/tracktion_engine/playback/graph/tracktion_PluginNode.cpp': '772b62f41f96eb447556d20e0595bac445acfe0092a99f51440980df53851299'}
STABLE_MIDI_ORIGINALS={'modules/tracktion_engine/playback/graph/tracktion_EditNodeBuilder.h': '5321e4db3cac516a574bafe18dc89efaa3c93b196ac8a854feb022079c556d7d', 'modules/tracktion_engine/playback/graph/tracktion_EditNodeBuilder.cpp': 'a4123302c6f0e7b8a5626627ccf1c7708cce59bccb13ed52e0446742dc4e1b95', 'modules/tracktion_graph/tracktion_graph/nodes/tracktion_SummingNode.h': '4ae7b9a7d780b593bca696bb426b00fc4219bc557ce2d2f1336e0f6497caa198', 'modules/tracktion_engine/playback/graph/tracktion_LiveMidiInjectingNode.h': '8fba987efa6dc1853fdc0d5736f766bc214ba9675dbb829f8a24a7840fdd07c2', 'modules/tracktion_engine/playback/graph/tracktion_LiveMidiInjectingNode.cpp': '658ba61e35f6abdeda04319150e95c8749ffbc5f82ebfc59a10960ccb0922014'}
STABLE_MIDI_FINALS={'modules/tracktion_engine/playback/graph/tracktion_EditNodeBuilder.h': '8ecc637b0cfc4d2c40094ff16854d8e5a50e3e6838768d3821596c2436aaf37e', 'modules/tracktion_engine/playback/graph/tracktion_EditNodeBuilder.cpp': 'fcddd584a6ccbd438fbdb7b9e16cdef3ef4ee55e72ffc860256d6053862b1470', 'modules/tracktion_graph/tracktion_graph/nodes/tracktion_SummingNode.h': 'de3924286c3308692a7c7a1e30c991fff70388b762c08f465bf0ca29a8246bad', 'modules/tracktion_engine/playback/graph/tracktion_LiveMidiInjectingNode.h': '7afc0e140b138b502a54192adafc22e78b46b93231c68315f1115a00b0218d98', 'modules/tracktion_engine/playback/graph/tracktion_LiveMidiInjectingNode.cpp': 'b9a735f459b4ad9e1fd16cae7362d35652720db8ebfb8dffabd982a58504fa49'}
ZERO_CHANNEL_ORIGINALS={'modules/tracktion_graph/tracktion_graph/nodes/tracktion_LatencyNode.h': 'cdf6e031594b50bc73bcfb7808ce70090a0c00ad50887b84094b222a5f1502a8', 'modules/tracktion_engine/playback/graph/tracktion_EditNodeBuilder.cpp': '8d484f24de269db24a84c66f1849f31094c90c86bfb287aac0b6811c6061c513'}
ZERO_CHANNEL_FINALS={'modules/tracktion_graph/tracktion_graph/nodes/tracktion_LatencyNode.h': '08377d8b098ccd1cc92024fa08b673bde57c99466f2c8037058606d398b7d849', 'modules/tracktion_engine/playback/graph/tracktion_EditNodeBuilder.cpp': 'a4123302c6f0e7b8a5626627ccf1c7708cce59bccb13ed52e0446742dc4e1b95'}
EXCHANGE_ORIGINALS={'modules/tracktion_engine/playback/graph/tracktion_EditNodeBuilder.h': '87eb609979435cad79a9d332fa090abad826b0db6d4ca5a820517f364cb9f0e5', 'modules/tracktion_engine/playback/graph/tracktion_EditNodeBuilder.cpp': 'a11e6a85c98aceea8f4ae548ae5574c569d1d3579c9943f9d6c2c5a0c8089ca0', 'modules/tracktion_graph/tracktion_graph/nodes/tracktion_LatencyNode.h': 'f392c3ebc572497223d97c94888a5df36613e6f0bd9a857a771b98aa98f61179'}
EXCHANGE_FINALS={'modules/tracktion_engine/playback/graph/tracktion_EditNodeBuilder.h': '5321e4db3cac516a574bafe18dc89efaa3c93b196ac8a854feb022079c556d7d', 'modules/tracktion_engine/playback/graph/tracktion_EditNodeBuilder.cpp': '8d484f24de269db24a84c66f1849f31094c90c86bfb287aac0b6811c6061c513', 'modules/tracktion_graph/tracktion_graph/nodes/tracktion_LatencyNode.h': 'cdf6e031594b50bc73bcfb7808ce70090a0c00ad50887b84094b222a5f1502a8'}
ARRANGER_ORIGINALS={'modules/tracktion_engine/playback/graph/tracktion_EditNodeBuilder.cpp': 'a6c5ad7a65f46ad8053ab9e226b5fda6b104f11d229a1c346b24fd67e7b8ccf5'}
ARRANGER_FINALS={'modules/tracktion_engine/playback/graph/tracktion_EditNodeBuilder.cpp': 'a11e6a85c98aceea8f4ae548ae5574c569d1d3579c9943f9d6c2c5a0c8089ca0'}
MEMORY_ORIGINALS={'modules/tracktion_engine/model/edit/tracktion_Edit.h': 'd44588b95a209cb4ecdebed15efb5f540d73aeddb24ce4272e40a6fa1726dcca', 'modules/tracktion_engine/model/edit/tracktion_Edit.cpp': 'e3ce65a316fc93c77a26603e12bb65c00a845e08f52012699537f5c68fc853f6', 'modules/tracktion_engine/playback/tracktion_EditPlaybackContext.cpp': 'de67ad8fcc5b10f23e90674558ffaa6e3f1e6ce37c58b18b6ade91ab59c6df4b', 'modules/tracktion_engine/playback/graph/tracktion_EditNodeBuilder.h': '6cbb9ba4c32735379ab4ff703dc312632a1907fd0b0cf317ee91f42d200de34b', 'modules/tracktion_engine/playback/graph/tracktion_EditNodeBuilder.cpp': 'b39f92bce779101bba324c64f712df72938e3924a908866a04fc42f9137c36ed'}
MEMORY_FINALS={'modules/tracktion_engine/model/edit/tracktion_Edit.h': 'b65b2fda6c42114271fc987a6f324f297c0917f7dfa1f7cdcb5aec1abf135d20', 'modules/tracktion_engine/model/edit/tracktion_Edit.cpp': '0010031f639b9b930e6c7e550f0d7c661deeb0ce7da2394cbbd9a1516a6c3f15', 'modules/tracktion_engine/playback/tracktion_EditPlaybackContext.cpp': '4bee9ee83df5d12739fcad416040aa1858c25d482e93285a2fb7aed707009976', 'modules/tracktion_engine/playback/graph/tracktion_EditNodeBuilder.h': '87eb609979435cad79a9d332fa090abad826b0db6d4ca5a820517f364cb9f0e5', 'modules/tracktion_engine/playback/graph/tracktion_EditNodeBuilder.cpp': 'a6c5ad7a65f46ad8053ab9e226b5fda6b104f11d229a1c346b24fd67e7b8ccf5'}
ZERO_ORIGINALS={'modules/tracktion_graph/tracktion_graph/nodes/tracktion_LatencyNode.h': '4e23469446500c47bfe2c229eccaaeb9e808249084e25c4766ddef38b24365b9', 'modules/tracktion_engine/playback/graph/tracktion_EditNodeBuilder.h': '25c50861d17496d2a536b6d0040f684c4265c2e288b96da55f90fc1a5c03adaa', 'modules/tracktion_engine/playback/graph/tracktion_EditNodeBuilder.cpp': 'c2cf53738165ce6fdb5741417c5a6165cae972e78e6672a564f57f5888ae23a5'}
ZERO_FINALS={'modules/tracktion_graph/tracktion_graph/nodes/tracktion_LatencyNode.h': 'f392c3ebc572497223d97c94888a5df36613e6f0bd9a857a771b98aa98f61179', 'modules/tracktion_engine/playback/graph/tracktion_EditNodeBuilder.h': '6cbb9ba4c32735379ab4ff703dc312632a1907fd0b0cf317ee91f42d200de34b', 'modules/tracktion_engine/playback/graph/tracktion_EditNodeBuilder.cpp': 'b39f92bce779101bba324c64f712df72938e3924a908866a04fc42f9137c36ed'}
ORIGINALS={'modules/tracktion_engine/model/edit/tracktion_Edit.h': 'c99ac27761c8f2960e7db38718f2880d06cd562dc2bd5fa375fa743a26cd594b', 'modules/tracktion_engine/model/edit/tracktion_Edit.cpp': 'b0c59048400cc8055ccf38ec8e1e62a02a9d5fa7c16188ed78b5561eb8fd5462', 'modules/tracktion_engine/playback/tracktion_EditPlaybackContext.cpp': '65bbd756aefb1e733a02f034f054d2aa903a34ea80e66853dc9e569b874f4974', 'modules/tracktion_engine/playback/graph/tracktion_EditNodeBuilder.h': 'aba1dc22c508ae2b8a5a66cf924b70ab5b29b0578c1986f8d1270fd704172a8d', 'modules/tracktion_engine/playback/graph/tracktion_EditNodeBuilder.cpp': '13c25a853ebdb3e073994a91c0e855c08641bbb70036b899bbb39d2788c3bdd1'}
FINALS={'modules/tracktion_engine/model/edit/tracktion_Edit.h': 'd44588b95a209cb4ecdebed15efb5f540d73aeddb24ce4272e40a6fa1726dcca', 'modules/tracktion_engine/model/edit/tracktion_Edit.cpp': 'e3ce65a316fc93c77a26603e12bb65c00a845e08f52012699537f5c68fc853f6', 'modules/tracktion_engine/playback/tracktion_EditPlaybackContext.cpp': 'de67ad8fcc5b10f23e90674558ffaa6e3f1e6ce37c58b18b6ade91ab59c6df4b', 'modules/tracktion_engine/playback/graph/tracktion_EditNodeBuilder.h': '25c50861d17496d2a536b6d0040f684c4265c2e288b96da55f90fc1a5c03adaa', 'modules/tracktion_engine/playback/graph/tracktion_EditNodeBuilder.cpp': 'c2cf53738165ce6fdb5741417c5a6165cae972e78e6672a564f57f5888ae23a5'}
RENDER = 'patches/tracktion-render-worker-policy/manifest.json'
CONTINUATION = 'patches/tracktion-finished-forward-continuation/manifest.json'
LATE_PINS = [('modules/tracktion_engine/playback/graph/tracktion_NodeRenderContext.cpp', 'b3e480e824dd91350bb86574f07c17ee5ee5618e5d38d3dba209a2adca2cb343', '570f9487a286d0dc43ca6617167b454044fe62e34904bd557af71f2300d47c35'), ('modules/tracktion_graph/tracktion_graph/nodes/tracktion_LatencyNode.h', '08377d8b098ccd1cc92024fa08b673bde57c99466f2c8037058606d398b7d849', '6928c85405ae2a9962da3ed45700ecb382e35103af40f99024cabfc733093054')]

GRAPH='modules/tracktion_engine/playback/graph/'
EXPECTED={GRAPH+n for n in ('tracktion_EditNodeBuilder.h','tracktion_EditNodeBuilder.cpp',
    'tracktion_PluginNode.h','tracktion_PluginNode.cpp','tracktion_LiveMidiInjectingNode.h','tracktion_LiveMidiInjectingNode.cpp')}


class PluginFinalOutputGuardTests(unittest.TestCase):
    @contextlib.contextmanager
    def _disposable_fixture(self):
        # Each case owns its independent complete source mirror and guard module.
        # No cached validity, cross-process fixture, worker or application launch.
        authority=fixtures.load_guard(ROOT)
        (ROOT / "build").mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='plugin-final-output-source-') as temporary:
            root=Path(temporary)
            prepared_directories=set()
            for name in fixtures.fixture_inputs(ROOT,authority.APPROVED):
                path=root/name
                if path.parent not in prepared_directories:
                    path.parent.mkdir(parents=True,exist_ok=True)
                    prepared_directories.add(path.parent)
                path.write_bytes((ROOT/name).read_bytes())
            guard=fixtures.load_guard(root);verifier=load_verifier()
            planned=guard.preflight()
            for e in planned:
                if e['pending']:(root/e['rel']).write_bytes(e['final'])
            def snapshot():
                return {e['rel']:((root/e['rel']).read_bytes(),(root/e['rel']).stat().st_mtime_ns) for e in planned}
            def assert_unchanged(before,victim=None,raw=None):
                for name,(expected,mtime) in before.items():
                    self.assertEqual((root/name).read_bytes(),raw if name==victim else expected)
                    if name!=victim:self.assertEqual((root/name).stat().st_mtime_ns,mtime)
            yield root,guard,verifier,planned,snapshot,assert_unchanged

    def test_exact_six_successors_history_and_known_successors(self):
        with self._disposable_fixture() as (root,guard,verifier,planned,snapshot,assert_unchanged):
            family=next(f for f in guard.APPROVED['families'] if f['path']==FAMILY)
            rows=family['files'];prefix='vendor/source/'+guard.APPROVED['sourceDirectory']+'/'
            self.assertEqual(len(rows),6);self.assertEqual({e['path'] for e in rows},EXPECTED)
            expected_lineages={
                'tracktion_EditNodeBuilder.h':('detached-graph','native-midi-ingress'),
                'tracktion_EditNodeBuilder.cpp':('sidechain-bus','owned-metering','detached-graph','send-gain-block','native-midi-ingress'),
                'tracktion_PluginNode.h':('detached-graph',),
                'tracktion_PluginNode.cpp':('detached-graph','stable-bus-stage'),
                'tracktion_LiveMidiInjectingNode.h':('native-midi-ingress',),
                'tracktion_LiveMidiInjectingNode.cpp':('native-midi-ingress',)}
            for e in rows:
                self.assertEqual(e['lineage'],['patches/tracktion-'+s+'/manifest.json' for s in expected_lineages[Path(e['path']).name]])
            historical=list(guard.APPROVED['sourceBurstFence']['files'])
            for f in guard.APPROVED['families']:
                if f['path']==FAMILY:break
                historical.extend(f['files'])
            self.assertEqual((len({e['path'] for e in historical}),len(historical)),(37,69))
            output_history=historical+rows
            self.assertEqual((len({e['path'] for e in output_history}),len(output_history)),(37,75))
            bridge=next(f for f in guard.APPROVED['families'] if f['path']==BRIDGE)
            bridge_rows=bridge['files']
            self.assertEqual(len(bridge_rows),5)
            self.assertEqual({e['path']:e['originalSha256'] for e in bridge_rows}, ORIGINALS)
            self.assertEqual({e['path']:e['patchedSha256'] for e in bridge_rows}, FINALS)
            self.assertEqual((len({e['path'] for e in output_history+bridge_rows}),len(output_history+bridge_rows)),(37,80))
            zero=next(f for f in guard.APPROVED['families'] if f['path']==AUDIO_ZERO)
            zero_rows=zero['files']
            self.assertEqual(len(zero_rows),3)
            self.assertEqual({e['path']:e['originalSha256'] for e in zero_rows},ZERO_ORIGINALS)
            self.assertEqual({e['path']:e['patchedSha256'] for e in zero_rows},ZERO_FINALS)
            self.assertEqual((len({e['path'] for e in output_history+bridge_rows+zero_rows}),len(output_history+bridge_rows+zero_rows)),(37,83))
            memory=next(f for f in guard.APPROVED['families'] if f['path']==MEMORY)
            memory_rows=memory['files']
            self.assertEqual(len(memory_rows),5)
            self.assertEqual({e['path']:e['originalSha256'] for e in memory_rows},MEMORY_ORIGINALS)
            self.assertEqual({e['path']:e['patchedSha256'] for e in memory_rows},MEMORY_FINALS)
            self.assertEqual((len({e['path'] for e in output_history+bridge_rows+zero_rows+memory_rows}),len(output_history+bridge_rows+zero_rows+memory_rows)),(37,88))
            arranger=next(f for f in guard.APPROVED['families'] if f['path']==ARRANGER)
            arranger_rows=arranger['files']
            self.assertEqual(len(arranger_rows),1)
            self.assertEqual({e['path']:e['originalSha256'] for e in arranger_rows},ARRANGER_ORIGINALS)
            self.assertEqual({e['path']:e['patchedSha256'] for e in arranger_rows},ARRANGER_FINALS)
            self.assertEqual((len({e['path'] for e in output_history+bridge_rows+zero_rows+memory_rows+arranger_rows}),len(output_history+bridge_rows+zero_rows+memory_rows+arranger_rows)),(37,89))
            exchange=next(f for f in guard.APPROVED['families'] if f['path']==EXCHANGE)
            exchange_rows=exchange['files']
            self.assertEqual(len(exchange_rows),3)
            self.assertEqual({e['path']:e['originalSha256'] for e in exchange_rows},EXCHANGE_ORIGINALS)
            self.assertEqual({e['path']:e['patchedSha256'] for e in exchange_rows},EXCHANGE_FINALS)
            self.assertEqual((len({e['path'] for e in output_history+bridge_rows+zero_rows+memory_rows+arranger_rows+exchange_rows}),len(output_history+bridge_rows+zero_rows+memory_rows+arranger_rows+exchange_rows)),(37,92))
            zero_channel=next(f for f in guard.APPROVED['families'] if f['path']==ZERO_CHANNEL)
            zero_channel_rows=zero_channel['files']
            self.assertEqual(len(zero_channel_rows),2)
            self.assertEqual({e['path']:e['originalSha256'] for e in zero_channel_rows},ZERO_CHANNEL_ORIGINALS)
            self.assertEqual({e['path']:e['patchedSha256'] for e in zero_channel_rows},ZERO_CHANNEL_FINALS)
            self.assertEqual((len({e['path'] for e in output_history+bridge_rows+zero_rows+memory_rows+arranger_rows+exchange_rows+zero_channel_rows}),len(output_history+bridge_rows+zero_rows+memory_rows+arranger_rows+exchange_rows+zero_channel_rows)),(37,94))
            stable_midi=next(f for f in guard.APPROVED['families'] if f['path']==STABLE_MIDI)
            stable_midi_rows=stable_midi['files']
            self.assertEqual(len(stable_midi_rows),5)
            self.assertEqual({e['path']:e['originalSha256'] for e in stable_midi_rows},STABLE_MIDI_ORIGINALS)
            self.assertEqual({e['path']:e['patchedSha256'] for e in stable_midi_rows},STABLE_MIDI_FINALS)
            self.assertEqual((len({e['path'] for e in output_history+bridge_rows+zero_rows+memory_rows+arranger_rows+exchange_rows+zero_channel_rows+stable_midi_rows}),len(output_history+bridge_rows+zero_rows+memory_rows+arranger_rows+exchange_rows+zero_channel_rows+stable_midi_rows)),(37,99))
            source_time=next(f for f in guard.APPROVED['families'] if f['path']==SOURCE_TIME)
            source_time_rows=source_time['files']
            self.assertEqual(len(source_time_rows),2)
            self.assertEqual({e['path']:e['originalSha256'] for e in source_time_rows},SOURCE_TIME_ORIGINALS)
            self.assertEqual({e['path']:e['patchedSha256'] for e in source_time_rows},SOURCE_TIME_FINALS)
            source_time_history=output_history+bridge_rows+zero_rows+memory_rows+arranger_rows+exchange_rows+zero_channel_rows+stable_midi_rows+source_time_rows
            self.assertEqual((len({e['path'] for e in source_time_history}),len(source_time_history)),(37,101))
            legacy=next(f for f in guard.APPROVED['families'] if f['path']=='patches/tracktion-legacy-midi-runtime/manifest.json')
            self.assertEqual(len(legacy['files']),3)
            legacy_history=source_time_history+legacy['files']
            self.assertEqual((len({e['path'] for e in legacy_history}),len(legacy_history)),(37,104))
            gate=next(f for f in guard.APPROVED['families'] if f['path']=='patches/tracktion-production-source-gate/manifest.json')
            self.assertEqual(len(gate['files']),6)
            gate_history=legacy_history+gate['files']
            self.assertEqual((len({e['path'] for e in gate_history}),len(gate_history)),(37,110))
            late=[f for f in guard.APPROVED['families'] if f['path'] in (RENDER,CONTINUATION)]
            self.assertEqual([f['path'] for f in late],[RENDER,CONTINUATION])
            late_rows=[e for f in late for e in f['files']]
            self.assertEqual([(e['path'],e['originalSha256'],e['patchedSha256']) for e in late_rows],LATE_PINS)
            self.assertEqual({e['path'] for e in late_rows}-{e['path'] for e in gate_history},{LATE_PINS[0][0]})
            history = gate_history + late_rows
            self.assertEqual((len({e['path'] for e in history}), len(history)), (38,112))
            wait = next(f for f in guard.APPROVED['families'] if f['path'] ==
                        'patches/tracktion-context-wait-deadline/manifest.json')
            self.assertEqual(len(wait['files']), 1)
            current = history + wait['files']
            self.assertEqual((len({e['path'] for e in current}), len(current)), (38,113))
            # The reviewed CONTROL wait adds one stage; historical112 remains above.
            self.assertEqual((len(planned),sum(len(e['chain']) for e in planned)),(38,113))
            # Preserve raw replay of the exact HISTORICAL six postimages even
            # when two of those targets now have an explicit bridge successor.
            verifier.prepare_source_stages(root,guard,[(e,prefix+e['path'],True) for e in rows],protected_root=ROOT)
            output_images={e['path']:(root/(prefix+e['path'])).read_bytes() for e in rows}
            for e in rows:self.assertEqual(guard.digest(output_images[e['path']]),e['patchedSha256'])
            for item in guard.preflight():
                if item['pending']:(root/item['rel']).write_bytes(item['final'])
            verifier.prepare_source_stages(root,guard,[(e,prefix+e['path'],True) for e in bridge_rows],protected_root=ROOT)
            bridge_images={e['path']:(root/(prefix+e['path'])).read_bytes() for e in bridge_rows}
            for e in bridge_rows:self.assertEqual(guard.digest(bridge_images[e['path']]),e['patchedSha256'])
            for item in guard.preflight():
                if item['pending']:(root/item['rel']).write_bytes(item['final'])
            verifier.prepare_source_stages(root,guard,[(e,prefix+e['path'],True) for e in zero_rows],protected_root=ROOT)
            zero_images={e['path']:(root/(prefix+e['path'])).read_bytes() for e in zero_rows}
            for e in zero_rows:self.assertEqual(guard.digest(zero_images[e['path']]),e['patchedSha256'])
            for item in guard.preflight():
                if item['pending']:(root/item['rel']).write_bytes(item['final'])
            verifier.prepare_source_stages(root,guard,[(e,prefix+e['path'],True) for e in memory_rows],protected_root=ROOT)
            memory_images={e['path']:(root/(prefix+e['path'])).read_bytes() for e in memory_rows}
            for e in memory_rows:self.assertEqual(guard.digest(memory_images[e['path']]),e['patchedSha256'])
            for item in guard.preflight():
                if item['pending']:(root/item['rel']).write_bytes(item['final'])
            verifier.prepare_source_stages(root,guard,[(e,prefix+e['path'],True) for e in arranger_rows],protected_root=ROOT)
            arranger_images={e['path']:(root/(prefix+e['path'])).read_bytes() for e in arranger_rows}
            for e in arranger_rows:self.assertEqual(guard.digest(arranger_images[e['path']]),e['patchedSha256'])
            for item in guard.preflight():
                if item['pending']:(root/item['rel']).write_bytes(item['final'])
            verifier.prepare_source_stages(root,guard,[(e,prefix+e['path'],True) for e in exchange_rows],protected_root=ROOT)
            exchange_images={e['path']:(root/(prefix+e['path'])).read_bytes() for e in exchange_rows}
            for e in exchange_rows:self.assertEqual(guard.digest(exchange_images[e['path']]),e['patchedSha256'])
            for item in guard.preflight():
                if item['pending']:(root/item['rel']).write_bytes(item['final'])
            verifier.prepare_source_stages(root,guard,[(e,prefix+e['path'],True) for e in zero_channel_rows],protected_root=ROOT)
            zero_channel_images={e['path']:(root/(prefix+e['path'])).read_bytes() for e in zero_channel_rows}
            for e in zero_channel_rows:self.assertEqual(guard.digest(zero_channel_images[e['path']]),e['patchedSha256'])
            for item in guard.preflight():
                if item['pending']:(root/item['rel']).write_bytes(item['final'])
            # The stable-MIDI historical postimages now precede the direct
            # legacy Runtime repair; never reverse an old patch from its successor.
            verifier.prepare_source_stages(root,guard,[(e,prefix+e['path'],True) for e in stable_midi_rows],protected_root=ROOT)
            stable_midi_images={e['path']:(root/(prefix+e['path'])).read_bytes() for e in stable_midi_rows}
            for e in stable_midi_rows:self.assertEqual(guard.digest(stable_midi_images[e['path']]),STABLE_MIDI_FINALS[e['path']])
            for item in guard.preflight():
                if item['pending']:(root/item['rel']).write_bytes(item['final'])
            latest=snapshot()
            for e in rows:
                payload=(root/e['patch']).read_bytes();hunks=guard.parse(payload,e['path'],e['addedLines'],e['deletedLines'])
                final=output_images[e['path']];original=guard.apply(hunks,final,True)
                self.assertEqual(guard.digest(original),e['originalSha256'])
                self.assertEqual(guard.digest(guard.apply(hunks,original)),e['patchedSha256'])
                with self.assertRaisesRegex(ValueError,'path/header mismatch'):
                    guard.parse(payload.replace(('+++ b/'+e['path']).encode(),b'+++ b/modules/unapproved.h',1),e['path'],e['addedLines'],e['deletedLines'])
            for e in bridge_rows:
                payload=(root/e['patch']).read_bytes()
                hunks=guard.parse(payload,e['path'],e['addedLines'],e['deletedLines'])
                final=bridge_images[e['path']];original=guard.apply(hunks,final,True)
                self.assertEqual(guard.digest(original),ORIGINALS[e['path']])
                self.assertEqual(guard.digest(guard.apply(hunks,original)),FINALS[e['path']])
                with self.assertRaisesRegex(ValueError,'path/header mismatch'):
                    guard.parse(payload.replace(('+++ b/'+e['path']).encode(),b'+++ b/modules/unapproved.h',1),e['path'],e['addedLines'],e['deletedLines'])
            for e in zero_rows:
                payload=(root/e['patch']).read_bytes()
                hunks=guard.parse(payload,e['path'],e['addedLines'],e['deletedLines'])
                final=zero_images[e['path']];original=guard.apply(hunks,final,True)
                self.assertEqual(guard.digest(original),ZERO_ORIGINALS[e['path']])
                self.assertEqual(guard.digest(guard.apply(hunks,original)),ZERO_FINALS[e['path']])
                with self.assertRaisesRegex(ValueError,'path/header mismatch'):
                    guard.parse(payload.replace(('+++ b/'+e['path']).encode(),b'+++ b/modules/unapproved.h',1),e['path'],e['addedLines'],e['deletedLines'])
            for e in memory_rows:
                payload=(root/e['patch']).read_bytes()
                hunks=guard.parse(payload,e['path'],e['addedLines'],e['deletedLines'])
                final=memory_images[e['path']];original=guard.apply(hunks,final,True)
                self.assertEqual(guard.digest(original),MEMORY_ORIGINALS[e['path']])
                self.assertEqual(guard.digest(guard.apply(hunks,original)),MEMORY_FINALS[e['path']])
                with self.assertRaisesRegex(ValueError,'path/header mismatch'):
                    guard.parse(payload.replace(('+++ b/'+e['path']).encode(),b'+++ b/modules/unapproved.h',1),e['path'],e['addedLines'],e['deletedLines'])
            for e in arranger_rows:
                payload=(root/e['patch']).read_bytes()
                hunks=guard.parse(payload,e['path'],e['addedLines'],e['deletedLines'])
                final=arranger_images[e['path']];original=guard.apply(hunks,final,True)
                self.assertEqual(guard.digest(original),ARRANGER_ORIGINALS[e['path']])
                self.assertEqual(guard.digest(guard.apply(hunks,original)),ARRANGER_FINALS[e['path']])
                with self.assertRaisesRegex(ValueError,'path/header mismatch'):
                    guard.parse(payload.replace(('+++ b/'+e['path']).encode(),b'+++ b/modules/unapproved.h',1),e['path'],e['addedLines'],e['deletedLines'])
            for e in exchange_rows:
                payload=(root/e['patch']).read_bytes()
                hunks=guard.parse(payload,e['path'],e['addedLines'],e['deletedLines'])
                final=exchange_images[e['path']];original=guard.apply(hunks,final,True)
                self.assertEqual(guard.digest(original),EXCHANGE_ORIGINALS[e['path']])
                self.assertEqual(guard.digest(guard.apply(hunks,original)),EXCHANGE_FINALS[e['path']])
                with self.assertRaisesRegex(ValueError,'path/header mismatch'):
                    guard.parse(payload.replace(('+++ b/'+e['path']).encode(),b'+++ b/modules/unapproved.h',1),e['path'],e['addedLines'],e['deletedLines'])
            for e in zero_channel_rows:
                payload=(root/e['patch']).read_bytes()
                hunks=guard.parse(payload,e['path'],e['addedLines'],e['deletedLines'])
                final=zero_channel_images[e['path']];original=guard.apply(hunks,final,True)
                self.assertEqual(guard.digest(original),ZERO_CHANNEL_ORIGINALS[e['path']])
                self.assertEqual(guard.digest(guard.apply(hunks,original)),ZERO_CHANNEL_FINALS[e['path']])
                with self.assertRaisesRegex(ValueError,'path/header mismatch'):
                    guard.parse(payload.replace(('+++ b/'+e['path']).encode(),b'+++ b/modules/unapproved.h',1),e['path'],e['addedLines'],e['deletedLines'])
            for e in stable_midi_rows:
                payload=(root/e['patch']).read_bytes()
                hunks=guard.parse(payload,e['path'],e['addedLines'],e['deletedLines'])
                final=stable_midi_images[e['path']];original=guard.apply(hunks,final,True)
                self.assertEqual(guard.digest(original),STABLE_MIDI_ORIGINALS[e['path']])
                self.assertEqual(guard.digest(guard.apply(hunks,original)),STABLE_MIDI_FINALS[e['path']])
                with self.assertRaisesRegex(ValueError,'path/header mismatch'):
                    guard.parse(payload.replace(('+++ b/'+e['path']).encode(),b'+++ b/modules/unapproved.h',1),e['path'],e['addedLines'],e['deletedLines'])
            for e in source_time_rows:
                payload=(root/e['patch']).read_bytes()
                hunks=guard.parse(payload,e['path'],e['addedLines'],e['deletedLines'])
                final=latest[prefix+e['path']][0];original=guard.apply(hunks,final,True)
                self.assertEqual(guard.digest(original),SOURCE_TIME_ORIGINALS[e['path']])
                self.assertEqual(guard.digest(guard.apply(hunks,original)),SOURCE_TIME_FINALS[e['path']])
                with self.assertRaisesRegex(ValueError,'path/header mismatch'):
                    guard.parse(payload.replace(('+++ b/'+e['path']).encode(),b'+++ b/modules/unapproved.h',1),e['path'],e['addedLines'],e['deletedLines'])
            for e in legacy['files']:
                payload=(root/e['patch']).read_bytes()
                hunks=guard.parse(payload,e['path'],e['addedLines'],e['deletedLines'])
                final=latest[prefix+e['path']][0];original=guard.apply(hunks,final,True)
                self.assertEqual(guard.digest(original),e['originalSha256'])
                self.assertEqual(guard.digest(guard.apply(hunks,original)),e['patchedSha256'])
                with self.assertRaisesRegex(ValueError,'path/header mismatch'):
                    guard.parse(payload.replace(('+++ b/'+e['path']).encode(),b'+++ b/modules/unapproved.h',1),e['path'],e['addedLines'],e['deletedLines'])
            verifier.prepare_source_stages(root,guard,[(e,prefix+e['path'],False) for e in rows],protected_root=ROOT)
            pending=guard.preflight();self.assertEqual({e['entry']['path'] for e in pending if e['pending']},EXPECTED)
            for e in pending:
                if e['pending']:(root/e['rel']).write_bytes(e['final'])
            self.assertFalse(any(e['pending'] for e in guard.preflight()))
            stable=snapshot();self.assertEqual({p:v[0] for p,v in stable.items()},{p:v[0] for p,v in latest.items()})
            guard.preflight();assert_unchanged(stable) # Idempotent verification never touches mtimes.
            verifier.prepare_source_stages(root,guard,[(rows[0],prefix+rows[0]['path'],False)],protected_root=ROOT)
            mixed=guard.preflight();self.assertEqual([e['entry']['path'] for e in mixed if e['pending']],[rows[0]['path']])
            for e in mixed:
                if e['pending']:(root/e['rel']).write_bytes(e['final'])
            all_rows=rows+bridge_rows+zero_rows+memory_rows+arranger_rows+exchange_rows+zero_channel_rows+stable_midi_rows+source_time_rows+legacy['files']
            unique_rows={e['path']:e for e in all_rows}
            before=snapshot();argv=sys.argv
            try:
                # New exact latest endpoints must also recognize their
                # immediate reviewed ancestor, including the historical port family.
                for e in bridge_rows+zero_rows+memory_rows+arranger_rows+exchange_rows+zero_channel_rows+stable_midi_rows+source_time_rows:
                    sys.argv=['guard','--known-successor',e['lineage'][-1],e['path'],guard.digest(before[prefix+e['path']][0])]
                    with contextlib.redirect_stdout(io.StringIO()):guard.main()
                    assert_unchanged(before)
                sys.argv[-1]='0'*64
                with self.assertRaisesRegex(ValueError,'not exact reviewed current final'):guard.main()
            finally:sys.argv=argv
            # Existing narrow earlier appliers already delegate Builder unknown
            # bytes to this complete strict proof; do not create extra appliers.
            inputs=ast.literal_eval(next(n.value for n in ast.parse((ROOT/'scripts/distribution.py').read_bytes()).body
                if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='BUILD_GUARD_INPUTS' for t in n.targets)))
            self.assertTrue({'scripts/guard-structural-graph.py','scripts/apply-sidechain-bus-patch.ps1','scripts/apply-owned-metering-patch.ps1'}<=set(inputs))
            self.assertIn('scripts/apply-retained-history-patch.ps1',inputs)
            body=(ROOT/'scripts/apply-retained-history-patch.ps1').read_text(encoding='utf-8')
            start=body.index('# Exact app-bridge EPC successor:')
            end=body.index('# Exact default-null native phase diagnostic successor only.',start)
            block=body[start:end]
            self.assertIn("$entry.path -cin @('modules/tracktion_engine/playback/tracktion_EditPlaybackContext.h',",block)
            self.assertIn("'modules/tracktion_engine/playback/tracktion_EditPlaybackContext.cpp') -and",block)
            self.assertIn('$hash -cne $entry.originalSha256 -and $hash -cne $entry.patchedSha256',block)
            self.assertIn("--known-successor 'patches/tracktion-retained-history/manifest.json' $entry.path $hash",block)
            self.assertNotIn('Test-Path',block)
            refusal="if ($LASTEXITCODE -ne 0) {throw 'App bridge EPC successor verification failed; no source changed'}"
            self.assertIn(refusal,block)
            self.assertLess(block.index('--known-successor'),block.index(refusal))
            self.assertLess(block.index(refusal),block.index('$acceptedContextSuccessors[$entry.path]=$hash; continue'))

            for filename,variable,ancestor in [('apply-sidechain-bus-patch.ps1','$hash','sidechain-bus'),('apply-owned-metering-patch.ps1','$actual','owned-metering')]:
                body=(ROOT/'scripts'/filename).read_text(encoding='utf-8')
                start=body.index('# Exact MIDI-affected native target:')
                snippet=body[start:body.index("throw 'MIDI-affected native successor verification failed; no source changed'",start)]
                self.assertIn("$entry.path -ceq '"+GRAPH+"tracktion_EditNodeBuilder.cpp'",snippet)
                self.assertIn("--known-successor 'patches/tracktion-"+ancestor+"/manifest.json' $entry.path "+variable,snippet)


            # Exact affected Summing path only, after original/patched checks;
            # the shared whole-chain guard must succeed before accepting bytes.
            summing='modules/tracktion_graph/tracktion_graph/nodes/tracktion_SummingNode.h'
            for filename,ancestor in [('apply-sidechain-bus-patch.ps1','sidechain-bus'),('apply-retained-history-patch.ps1','retained-history')]:
                body=(ROOT/'scripts'/filename).read_text(encoding='utf-8')
                start=body.index('# Exact stable-MIDI Summing successor:')
                end=body.index('# Exact Summing property-reuse successor only.',start)
                block=body[start:end]
                self.assertIn("$entry.path -ceq '"+summing+"'",block)
                self.assertIn('$hash -cne $entry.originalSha256 -and $hash -cne $entry.patchedSha256',block)
                self.assertIn("--known-successor 'patches/tracktion-"+ancestor+"/manifest.json' $entry.path $hash",block)
                refusal="if ($LASTEXITCODE -ne 0) {throw 'Stable MIDI Summing successor verification failed; no source changed'}"
                self.assertIn(refusal,block)
                self.assertNotIn('Test-Path',block)
                self.assertLess(block.index('--known-successor'),block.index(refusal))
                self.assertLess(block.index(refusal),block.index('continue'))
                self.assertIn('scripts/'+filename,inputs)


            body=(ROOT/'scripts/apply-sidechain-bus-patch.ps1').read_text(encoding='utf-8')
            start=body.index('# Exact source-time Plugin successor:')
            block=body[start:body.index('# Exact optional send block successor only;',start)]
            self.assertIn("$entry.path -ceq 'modules/tracktion_engine/plugins/tracktion_Plugin.h'",block)
            self.assertIn('$hash -cne $entry.originalSha256 -and $hash -cne $entry.patchedSha256',block)
            self.assertIn("--known-successor 'patches/tracktion-sidechain-bus/manifest.json' $entry.path $hash",block)
            refusal="if ($LASTEXITCODE -ne 0) {throw 'Source-time Plugin successor verification failed; no source changed'}"
            self.assertIn(refusal,block)
            self.assertNotIn('Test-Path',block)
            self.assertLess(block.index('--known-successor'),block.index(refusal))
            self.assertLess(block.index(refusal),block.index('continue'))
            self.assertIn('scripts/apply-sidechain-bus-patch.ps1',inputs)

    def test_vendor_authority_mutations_and_ancestor_successors(self):
        with self._disposable_fixture() as (root,guard,verifier,planned,snapshot,assert_unchanged):
            families={family['path']:family for family in guard.APPROVED['families']}
            rows=families[FAMILY]['files']
            bridge_rows=families[BRIDGE]['files'];zero_rows=families[AUDIO_ZERO]['files']
            memory_rows=families[MEMORY]['files'];arranger_rows=families[ARRANGER]['files']
            exchange_rows=families[EXCHANGE]['files'];zero_channel_rows=families[ZERO_CHANNEL]['files']
            stable_midi_rows=families[STABLE_MIDI]['files'];source_time_rows=families[SOURCE_TIME]['files']
            legacy=families['patches/tracktion-legacy-midi-runtime/manifest.json']
            prefix='vendor/source/'+guard.APPROVED['sourceDirectory']+'/'
            self.assertEqual((len(planned),sum(len(e['chain']) for e in planned)),(38,113))
            all_rows=rows+bridge_rows+zero_rows+memory_rows+arranger_rows+exchange_rows+zero_channel_rows+stable_midi_rows+source_time_rows+legacy['files']
            unique_rows={e['path']:e for e in all_rows}
            for e in unique_rows.values():
                target=prefix+e['path'];original=(root/target).read_bytes()
                for altered in (original+b'// unknown source\n',original.replace(b'\n',b'\r\n')):
                    before=snapshot();(root/target).write_bytes(altered)
                    with self.assertRaisesRegex(ValueError,'unknown/ambiguous vendor'):guard.preflight()
                    assert_unchanged(before,target,altered);(root/target).write_bytes(original)
            for name,message in [(FAMILY,'contract (raw )?identity'),(BRIDGE,'contract (raw )?identity'),(AUDIO_ZERO,'contract (raw )?identity'),(MEMORY,'contract (raw )?identity'),(ARRANGER,'contract (raw )?identity'),(EXCHANGE,'contract (raw )?identity'),(ZERO_CHANNEL,'contract (raw )?identity'),(STABLE_MIDI,'contract (raw )?identity'),(SOURCE_TIME,'contract (raw )?identity'),(legacy['path'],'contract (raw )?identity')]+[(e['patch'],'payload exact identity') for e in all_rows]:
                path=root/name;original=path.read_bytes();before=snapshot();path.write_bytes(original+b'\n')
                with self.assertRaisesRegex(ValueError,message):guard.preflight()
                assert_unchanged(before);path.write_bytes(original)
            with self.assertRaisesRegex(ValueError,'escapes project root'):guard.ordinary(root/'..'/'unapproved-native-target')
            # Prove valid historical ancestor admission after all refused corruptions.
            before=snapshot();argv=sys.argv
            try:
                for e in rows:
                    for ancestor in e['lineage']:
                        sys.argv=['guard','--known-successor',ancestor,e['path'],guard.digest(before[prefix+e['path']][0])]
                        with contextlib.redirect_stdout(io.StringIO()):guard.main()
                        assert_unchanged(before)
            finally:sys.argv=argv


if __name__=='__main__':unittest.main()
