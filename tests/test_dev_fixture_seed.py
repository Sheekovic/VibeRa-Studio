"""Fixture seed reuse preserves independent trees and real content invalidation."""
import os
from pathlib import Path
import tempfile
import unittest

import test_dev_local_source_guard as fixtures

ROOT = Path(__file__).resolve().parents[1]


class FixtureSeedTests(unittest.TestCase):
    def test_independent_copies_and_same_stat_content_change(self):
        with tempfile.TemporaryDirectory(prefix="fixture-seed-") as directory:
            base = Path(directory)
            source, first, second, refused = (base / name for name in ("source", "first", "second", "refused"))
            approved = fixtures.load_guard(ROOT).APPROVED
            fixtures.copy_fixture(ROOT, source, approved)
            fixtures.copy_fixture(source, first, approved)
            second_guard = fixtures.copy_fixture(source, second, approved)
            lock = "dependencies.lock.json"
            original_lock = (source / lock).read_bytes()
            (first / lock).write_bytes(b"deliberately invalid independent fixture")
            self.assertEqual((source / lock).read_bytes(), original_lock)
            self.assertEqual((second / lock).read_bytes(), original_lock)
            self.assertTrue(second_guard.preflight())

            manifest = approved["families"][0]["path"]
            path = source / manifest
            before = path.stat()
            raw = path.read_bytes()
            changed = raw.replace(b"tracktion_engine", b"tracktion_enginf", 1)
            self.assertNotEqual(changed, raw)
            self.assertEqual(len(changed), len(raw))
            path.write_bytes(changed)
            os.utime(path, ns=(before.st_atime_ns, before.st_mtime_ns))
            self.assertEqual(path.stat().st_mtime_ns, before.st_mtime_ns)
            with self.assertRaises(ValueError):
                fixtures.copy_fixture(source, refused, approved)
            self.assertEqual(path.read_bytes(), changed)
            self.assertEqual((second / manifest).read_bytes(), raw)
