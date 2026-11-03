"""Original disposable source guard tests. No build, application or provider."""
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
_fixture_seeds = {}  # One immutable content key/image set per closed setup profile.


def load_guard(root):
    spec = importlib.util.spec_from_file_location("original_local_source_guard", root / "scripts/guard-structural-graph.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def fixture_inputs(root, approved):
    """Explicit reviewed metadata closure, never recursively harvest vendor trees."""
    files = {"dependencies.lock.json", "scripts/guard-structural-graph.py",
             "scripts/verify-summing-properties-guard.py"}
    manifest_paths = set(approved["requiredContractSha256"])
    manifest_paths.update(f["path"] for f in approved["families"])
    manifest_paths.add(approved["sourceBurstJuce"]["path"])
    for name in sorted(manifest_paths):
        files.add(name)
        data = json.loads((root / name).read_text(encoding="utf-8"))
        for item in data.get("files", []):
            files.add(item["patch"])
            vendor = "vendor/source/" + data["sourceDirectory"] + "/" + item["path"]
            if not item.get("originalAbsent", False) or (root / vendor).exists():
                files.add(vendor)
        for item in data.get("vendorReadDependencies", []):
            files.add("vendor/source/" + data["sourceDirectory"] + "/" + item["path"])
        dependencies = list(data.get("sourceDependencies", []))
        if "sourceDependency" in data:
            dependencies.append(data["sourceDependency"])
        files.update(item["path"] for item in dependencies)
    return sorted(files)


def copy_fixture(root, destination, approved, profile="local-preimage"):
    # Reuse deterministic setup images only; actual test guard calls remain fresh.
    # Closed profiles keep the cache bounded and preserve mixed-stage fixtures.
    if profile not in ("local-preimage", "all-final", "absent-only"):
        raise ValueError("Unknown disposable fixture preparation profile")
    if destination.resolve() == root.resolve() or (destination.exists() and any(destination.iterdir())):
        raise ValueError("Fixture destination must be a fresh independent tree")
    inputs = {name: (root / name).read_bytes() for name in fixture_inputs(root, approved)}
    key = (str(root.resolve()), json.dumps(approved, sort_keys=True), profile,
           tuple((name, hashlib.sha256(raw).digest()) for name, raw in inputs.items()))
    previous = _fixture_seeds.get(profile)
    cached = previous is not None and key == previous[0]
    images = previous[1] if cached else tuple(inputs.items())
    prepared_directories = set()
    for name, raw in images:
        target = destination / name
        # This fresh private copy is sole-owned during setup. Establish each
        # parent once, without caching any source/guard/filesystem validity.
        if target.parent not in prepared_directories:
            target.parent.mkdir(parents=True, exist_ok=True)
            prepared_directories.add(target.parent)
        target.write_bytes(raw)
        if target.read_bytes() != raw:
            raise OSError("Immutable fixture seed copy differs from validated bytes")
    guard = load_guard(destination)
    local = next(family for family in approved["families"]
                 if family["path"] == "patches/tracktion-local-source-initial/manifest.json")
    prefix = "vendor/source/" + approved["sourceDirectory"] + "/"
    if cached:
        if profile == "local-preimage":
            for entry in local["files"]:
                assert guard.digest((destination / (prefix + entry["path"])).read_bytes()) == entry["originalSha256"]
        return guard
    changes = guard.preflight()
    for item in changes:
        selected = item['pending'] if profile != "absent-only" else item['before'] is None
        if selected:
            guard.ordinary(destination / item['rel'], required=item['before'] is not None).write_bytes(item['final'])
    if profile == "local-preimage":
        # Retain the exact checked historical reconstruction and original oracles.
        spec = importlib.util.spec_from_file_location("original_local_stage_reconstruction",
            root / "scripts/verify-summing-properties-guard.py")
        verifier = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(verifier)
        verifier.prepare_source_stages(destination, guard,
            [(entry, prefix + entry["path"], False) for entry in local["files"]])
        for entry in local["files"]:
            assert guard.digest((destination / (prefix + entry["path"])).read_bytes()) == entry["originalSha256"]
    names = set(inputs) | {item['rel'] for item in changes}
    final_images = tuple((name, (destination / name).read_bytes())
                        for name in sorted(names) if (destination / name).is_file())
    # Failed validation/preparation never publishes a reusable seed.
    _fixture_seeds[profile] = (key, final_images)
    return guard


class LocalSourceGuardTests(unittest.TestCase):
    def setUp(self):
        (ROOT / "build").mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix="vibera-local-source-guard-")
        self.root = Path(self.temporary.name)
        authority = load_guard(ROOT)
        self.guard = copy_fixture(ROOT, self.root, authority.APPROVED)
        self.family = next(family for family in self.guard.APPROVED["families"]
                           if family["path"] == "patches/tracktion-local-source-initial/manifest.json")
        self.prefix = "vendor/source/" + self.guard.APPROVED["sourceDirectory"] + "/"

    def tearDown(self):
        self.temporary.cleanup()

    def snapshot(self):
        return {row["rel"]: (self.root / row["rel"]).read_bytes()
                for row in self.guard.preflight()}

    def test_exact_fresh_and_final_identity_mtime(self):
        changes = self.guard.preflight()
        # Derive aggregate closure from the independently enumerated pinned
        # authority. LOCAL's deliberate14/13 pending assertions stay literal.
        stages = list(self.guard.APPROVED["sourceBurstFence"]["files"])
        stages += [entry for family in self.guard.APPROVED["families"] for entry in family["files"]]
        self.assertEqual(len(changes), len({entry["path"] for entry in stages}))
        self.assertEqual(sum(len(item["chain"]) for item in changes), len(stages))
        self.assertEqual(sum(item["pending"] for item in changes), 14)
        for item in changes:
            if item["pending"]:
                (self.root / item["rel"]).write_bytes(item["final"])
        before = {item["rel"]: (self.root / item["rel"]).stat().st_mtime_ns for item in changes}
        self.assertFalse(any(item["pending"] for item in self.guard.preflight()))
        self.assertEqual(before, {name: (self.root / name).stat().st_mtime_ns for name in before})

    def test_exact_mixed_stages(self):
        changes = self.guard.preflight()
        for item in changes:
            if item["pending"] and item["entry"]["path"] == self.family["files"][0]["path"]:
                (self.root / item["rel"]).write_bytes(item["final"])
        self.assertEqual(sum(item["pending"] for item in self.guard.preflight()), 13)

    def test_unknown_late_target_refuses_without_writes(self):
        snapshot = self.snapshot()
        last = self.prefix + self.family["files"][-1]["path"]
        raw = (self.root / last).read_bytes()
        (self.root / last).write_bytes(raw + b"// original unknown test bytes\n")
        with self.assertRaisesRegex(ValueError, "unknown/ambiguous vendor"):
            self.guard.preflight()
        for name, original in snapshot.items():
            if name != last:
                self.assertEqual((self.root / name).read_bytes(), original)

    def test_manifest_and_payload_are_pinned(self):
        manifest = self.root / self.family["path"]
        original = manifest.read_bytes()
        value = json.loads(original)
        value["productionEnabled"] = True
        manifest.write_text(json.dumps(value), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "contract raw identity"):
            self.guard.preflight()
        manifest.write_bytes(original)
        payload = self.root / self.family["files"][-1]["patch"]
        payload.write_bytes(payload.read_bytes() + b"\n")
        with self.assertRaisesRegex(ValueError, "payload exact identity"):
            self.guard.preflight()

    def test_raw_vendor_line_endings_are_exact(self):
        target = self.root / (self.prefix + self.family["files"][-1]["path"])
        target.write_bytes(target.read_bytes().replace(b"\n", b"\r\n"))
        with self.assertRaisesRegex(ValueError, "unknown/ambiguous vendor"):
            self.guard.preflight()

    def test_lock_pin_and_outside_path_refuse(self):
        lock = self.root / "dependencies.lock.json"
        value = json.loads(lock.read_text(encoding="utf-8"))
        next(item for item in value["dependencies"] if item["name"] == "tracktion_engine")["revision"] = "0" * 40
        lock.write_text(json.dumps(value), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "pinned dependency"):
            self.guard.preflight()
        with self.assertRaisesRegex(ValueError, "escapes project root"):
            self.guard.ordinary(self.root.parent / "original-unrelated-file")

    def test_hunk_parser_rejects_path_binary_directive_and_span(self):
        entry = self.family["files"][-1]
        payload = (self.root / entry["patch"]).read_bytes()
        for malformed in (payload.replace(b"--- a/", b"--- a/../", 1), payload+b"\0",
                          payload+b"diff --git forbidden forbidden\n"):
            with self.assertRaises(ValueError):
                self.guard.parse(malformed, entry["path"], entry["addedLines"], entry["deletedLines"])


if __name__ == "__main__":
    unittest.main()
