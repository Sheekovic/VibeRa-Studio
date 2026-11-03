"""Original strict stage-difference source tests, no compiler/provider/audio."""
import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest

import test_dev_local_source_guard as fixtures
from test_dev_summing_guard import load_verifier

ROOT = Path(__file__).resolve().parents[1]
FAMILY = "patches/tracktion-stage-difference/manifest.json"
TARGET = "modules/tracktion_graph/tracktion_graph/nodes/tracktion_LatencyNode.h"


class StageDifferenceGuardTests(unittest.TestCase):
    def setUp(self):
        (ROOT / "build").mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix="vibera-stage-difference-")
        self.root = Path(self.temporary.name)
        authority = fixtures.load_guard(ROOT)
        self.guard = fixtures.copy_fixture(ROOT, self.root, authority.APPROVED, profile="all-final")
        # Select by exact family/path, never by the final family ordinal.
        self.family = next(family for family in self.guard.APPROVED["families"] if family["path"] == FAMILY)
        self.entry = next(entry for entry in self.family["files"] if entry["path"] == TARGET)
        self.relative = "vendor/source/" + self.guard.APPROVED["sourceDirectory"] + "/" + TARGET
        self.target = self.root / self.relative
        self.aggregate_final = self.target.read_bytes()
        self.verifier = load_verifier()
        self.verifier.prepare_source_stage(self.root, self.guard, self.entry, self.relative, postimage=True)
        self.final = self.target.read_bytes()  # Exact diagnostic stage, not a later successor.
        payload = (self.root / self.entry["patch"]).read_bytes()
        self.hunks = self.guard.parse(payload, TARGET, self.entry["addedLines"], self.entry["deletedLines"])
        # The preceding full transaction already validated every current input
        # and reconstructed this exact historical postimage. Derive only its
        # predecessor bytes here; do not repeat that whole transaction per case.
        self.original = self.guard.apply(self.hunks, self.final, reverse=True)
        self.assertEqual(self.guard.digest(self.final), self.entry["patchedSha256"])
        self.assertEqual(self.guard.digest(self.original), self.entry["originalSha256"])
        self.target.write_bytes(self.original)

    def tearDown(self):
        self.temporary.cleanup()

    def snapshot(self, known_paths=None):
        # main() just performed the actual complete validation. Retain fresh
        # bytes/mtime observations of every baseline target without repeating it.
        paths = [item["rel"] for item in self.guard.preflight()] if known_paths is None else known_paths
        return {name: ((self.root / name).read_bytes(), (self.root / name).stat().st_mtime_ns)
                for name in paths}

    def test_exact_previous_family_identity_and_derived_closure(self):
        predecessor = next(family for family in self.guard.APPROVED["families"]
                           if family["path"] == "patches/tracktion-stable-bus-stage/manifest.json")
        before = next(entry for entry in predecessor["files"] if entry["path"] == TARGET)
        self.assertEqual(before["patchedSha256"], self.entry["originalSha256"])
        self.assertEqual(self.guard.digest(self.original), self.entry["originalSha256"])
        self.assertEqual(self.guard.digest(self.final), self.entry["patchedSha256"])
        stages = list(self.guard.APPROVED["sourceBurstFence"]["files"])
        stages += [entry for family in self.guard.APPROVED["families"] for entry in family["files"]]
        changes = self.guard.preflight()
        self.assertEqual(len(changes), len({entry["path"] for entry in stages}))
        self.assertEqual(sum(len(item["chain"]) for item in changes), len(stages))
        self.assertEqual([item["rel"] for item in changes if item["pending"]], [self.relative])
        self.assertEqual((self.entry["addedLines"], self.entry["deletedLines"]), (96, 6))

    def test_read_only_verify_and_all_three_predecessor_successors(self):
        before = self.snapshot()
        argv = sys.argv
        try:
            sys.argv = ["guard-structural-graph.py", "--verify-only"]
            with contextlib.redirect_stdout(io.StringIO()):
                self.guard.main()
            self.assertEqual(self.snapshot(before), before)
            self.target.write_bytes(self.aggregate_final)
            final = self.snapshot()
            for predecessor in ("tracktion-retained-history", "tracktion-retained-midi", "tracktion-consumed-phase"):
                sys.argv = ["guard-structural-graph.py", "--known-successor",
                            "patches/" + predecessor + "/manifest.json", TARGET, self.guard.digest(self.aggregate_final)]
                with contextlib.redirect_stdout(io.StringIO()):
                    self.guard.main()
                self.assertEqual(self.snapshot(final), final)
        finally:
            sys.argv = argv

    def test_unknown_and_raw_eol_preserve_all_inputs(self):
        before = self.snapshot()
        for raw in (self.original + b"// unknown\n", self.original.replace(b"\n", b"\r\n")):
            self.target.write_bytes(raw)
            with self.assertRaisesRegex(ValueError, "unknown/ambiguous vendor"):
                self.guard.preflight()
            self.assertEqual(self.target.read_bytes(), raw)
            for name, (data, mtime) in before.items():
                if name != self.relative:
                    self.assertEqual(((self.root / name).read_bytes(), (self.root / name).stat().st_mtime_ns),
                                     (data, mtime))
        self.target.write_bytes(self.original)

    def test_manifest_and_payload_exact_pins(self):
        before = self.snapshot()
        manifest = self.root / FAMILY
        original = manifest.read_bytes()
        data = json.loads(original)
        data["files"][0]["patchedSha256"] = "0" * 64
        manifest.write_text(json.dumps(data))
        with self.assertRaisesRegex(ValueError, "contract raw identity"):
            self.guard.preflight()
        manifest.write_bytes(original)
        payload = self.root / self.entry["patch"]
        payload.write_bytes(payload.read_bytes() + b"\n")
        with self.assertRaisesRegex(ValueError, "payload exact identity"):
            self.guard.preflight()
        for name, (raw, mtime) in before.items():
            self.assertEqual(((self.root / name).read_bytes(), (self.root / name).stat().st_mtime_ns), (raw, mtime))

    def test_parser_path_directive_and_numstat_are_exact(self):
        raw = (self.root / self.entry["patch"]).read_bytes()
        for invalid in (raw.replace(b"+++ b/", b"+++ b/../", 1), raw + b"\0",
                        raw + b"diff --git forbidden forbidden\n"):
            with self.assertRaises(ValueError):
                self.guard.parse(invalid, TARGET, 96, 6)
        with self.assertRaisesRegex(ValueError, "numstat"):
            self.guard.parse(raw, TARGET, 97, 6)
        self.assertEqual(self.guard.apply(self.hunks, self.original), self.final)

    def test_repository_helpers_and_distribution_reference_current_family(self):
        for helper, predecessor in (("apply-retained-history-patch.ps1", "tracktion-retained-history"),
                                    ("apply-retained-midi-patch.ps1", "tracktion-retained-midi"),
                                    ("apply-consumed-phase-patch.ps1", "tracktion-consumed-phase")):
            body = (ROOT / "scripts" / helper).read_text()
            start = body.index("        # Only this exact reviewed path delegates future successor admission")
            end = body.index("        # Narrow reviewed stable-stage final only;", start)
            admission = body[start:end]
            value = "$hash" if helper == "apply-retained-history-patch.ps1" else "$actual"
            self.assertIn("$entry.path -ceq '" + TARGET + "'", admission)
            self.assertIn(value + " -cne $entry.originalSha256 -and " + value + " -cne $entry.patchedSha256", admission)
            self.assertEqual(admission.count("--known-successor"), 1)
            self.assertIn("--known-successor 'patches/" + predecessor + "/manifest.json' $entry.path " + value, admission)
            failure = "if ($LASTEXITCODE -ne 0) {throw 'LatencyNode authoritative successor verification failed; no source changed'}"
            self.assertIn(failure, admission)
            self.assertLess(admission.index("--known-successor"), admission.index(failure))
            self.assertLess(admission.index(failure), admission.index("; continue"))
            self.assertNotIn("Test-Path", admission)
        self.assertIn("'scripts/verify-stage-difference-guard.py'", (ROOT / "scripts/distribution.py").read_text())
        self.assertIn("patches/tracktion-stage-difference/*.patch -text -whitespace",
                      (ROOT / ".gitattributes").read_text())


if __name__ == "__main__":
    unittest.main()
