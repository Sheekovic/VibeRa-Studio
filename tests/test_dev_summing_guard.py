"""Focused permanent source reconstruction tests; no full16-case matrix duplication."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

import test_dev_local_source_guard as fixtures

ROOT = Path(__file__).resolve().parents[1]


def load_verifier():
    spec = importlib.util.spec_from_file_location("original_summing_source_matrix", ROOT / "scripts/verify-summing-properties-guard.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class SummingSourcePreparationTests(unittest.TestCase):
    def setUp(self):
        (ROOT / "build").mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix="vibera-summing-source-guard-")
        self.root = Path(self.temporary.name)
        authority = fixtures.load_guard(ROOT)
        self.guard = fixtures.copy_fixture(ROOT, self.root, authority.APPROVED, profile="absent-only")
        self.verifier = load_verifier()
        family = next(f for f in self.guard.APPROVED["families"]
                      if f["path"] == "patches/tracktion-summing-properties-reuse/manifest.json")
        self.entry = family["files"][0]
        self.relative = "vendor/source/" + self.guard.APPROVED["sourceDirectory"] + "/" + self.entry["path"]
        self.target = self.root / self.relative

    def tearDown(self):
        self.temporary.cleanup()

    def prepare(self):
        return self.verifier.prepare_source_predecessor(self.root, self.guard, self.entry, self.relative)

    def snapshot(self):
        return {row["rel"]: (self.root / row["rel"]).read_bytes() for row in self.guard.preflight()}

    def test_integrated_final_reconstructs_exact_disposable_predecessor(self):
        # Prepare real reviewed final even if invoked from a fresh predecessor checkout.
        item = next(row for row in self.guard.preflight() if row["rel"] == self.relative)
        self.target.write_bytes(item["final"])
        before = self.snapshot()
        result = self.prepare()
        self.assertEqual(result["inputSha256"], item["entry"]["patchedSha256"])
        self.assertTrue(result["reconstructed"])
        self.assertEqual(self.guard.digest(self.target.read_bytes()), self.entry["originalSha256"])
        for name, data in before.items():
            if name != self.relative:
                self.assertEqual((self.root / name).read_bytes(), data)

    def test_exact_predecessor_is_idempotent_without_touching_mtime(self):
        self.prepare()
        before = self.target.read_bytes(), self.target.stat().st_mtime_ns
        result = self.prepare()
        self.assertFalse(result["reconstructed"])
        self.assertEqual((self.target.read_bytes(), self.target.stat().st_mtime_ns), before)

    def test_unknown_source_is_refused_without_any_writes(self):
        before = self.snapshot()
        unknown = self.target.read_bytes() + b"// Original unknown-byte refusal fixture\n"
        self.target.write_bytes(unknown)
        with self.assertRaisesRegex(ValueError, "unknown/ambiguous vendor"):
            self.prepare()
        for name, data in before.items():
            self.assertEqual((self.root / name).read_bytes(), unknown if name == self.relative else data)

    def test_changed_payload_is_refused_without_any_writes(self):
        before = self.snapshot()
        payload = self.root / self.entry["patch"]
        payload.write_bytes(payload.read_bytes() + b"\n")
        with self.assertRaisesRegex(ValueError, "payload exact identity"):
            self.prepare()
        for name, data in before.items():
            self.assertEqual((self.root / name).read_bytes(), data)

    def test_disposable_seed_finalizes_unrelated_reviewed_successor_only(self):
        # Isolate the one deliberately pending peer; other newer reviewed
        # families may initially be pending in the checkout used by this test.
        for row in self.guard.preflight():
            (self.root / row['rel']).write_bytes(row['final'])
        changes = self.guard.preflight()
        peer = next(row for row in changes if row['entry']['path'] ==
                    'modules/tracktion_graph/tracktion_graph/nodes/tracktion_LatencyNode.h')
        reviewed = next(row for row in peer['chain'] if row['entry']['patch'] ==
                        'patches/tracktion-stage-difference/tracktion_LatencyNode.h.patch')
        peer_path = self.root / peer['rel']
        self.verifier.prepare_source_predecessor(self.root, self.guard, reviewed['entry'], peer['rel'])
        before = self.snapshot()
        result = self.verifier.prepare_source_seed(self.root, self.guard, self.entry, self.relative)
        self.assertEqual(result['otherReviewedTargetsFinalized'], [peer['rel']])
        self.assertEqual(peer_path.read_bytes(), peer['final'])
        self.assertEqual(self.guard.digest(self.target.read_bytes()), self.entry['originalSha256'])
        for name, data in before.items():
            if name not in (self.relative, peer['rel']):
                self.assertEqual((self.root / name).read_bytes(), data)

    def test_nonterminal_native_stage_reconstructs_both_images_through_exact_chain(self):
        peer = next(row for row in self.guard.preflight() if row['entry']['path'] ==
                    'modules/tracktion_graph/tracktion_graph/nodes/tracktion_LatencyNode.h')
        # This actual stable-stage entry already has a later reviewed stage;
        # the test remains valid when more exact successors are appended.
        stage = next(row['entry'] for row in peer['chain'] if row['entry']['patch'] ==
                     'patches/tracktion-stable-bus-stage/tracktion_LatencyNode.h.patch')
        path = self.root / peer['rel']
        path.write_bytes(peer['final'])
        before = self.snapshot()
        final_receipt = self.verifier.prepare_source_stage(self.root, self.guard, stage, peer['rel'], postimage=True)
        self.assertEqual(self.guard.digest(path.read_bytes()), stage['patchedSha256'])
        self.assertTrue(any(row['reverse'] for row in final_receipt['stages']))
        original_receipt = self.verifier.prepare_source_predecessor(self.root, self.guard, stage, peer['rel'])
        self.assertEqual(self.guard.digest(path.read_bytes()), stage['originalSha256'])
        self.assertEqual(original_receipt['aggregateFinalSha256'], peer['entry']['patchedSha256'])
        for name, data in before.items():
            if name != peer['rel']:
                self.assertEqual((self.root / name).read_bytes(), data)
        stable = path.read_bytes(), path.stat().st_mtime_ns
        self.verifier.prepare_source_predecessor(self.root, self.guard, stage, peer['rel'])
        self.assertEqual((path.read_bytes(), path.stat().st_mtime_ns), stable)

    def test_reconstruction_refuses_actual_guard_root_and_unregistered_stage(self):
        with self.assertRaisesRegex(ValueError, 'disposable guard mirror'):
            self.verifier.prepare_source_stage(self.root, self.guard, self.entry, self.relative,
                                              protected_root=self.root)
        before = self.snapshot()
        unregistered = dict(self.entry, patchedSha256='0' * 64)
        with self.assertRaisesRegex(ValueError, 'stage authority'):
            self.verifier.prepare_source_stage(self.root, self.guard, unregistered, self.relative)
        self.assertEqual(self.snapshot(), before)

    def test_cli_bundle_is_optional_repository_default(self):
        arguments = self.verifier.parse_args(["--output-root", str(self.root / "build/evidence")])
        self.assertIsNone(arguments.bundle)
        self.assertEqual(arguments.output_root, self.root / "build/evidence")


if __name__ == "__main__":
    unittest.main()
