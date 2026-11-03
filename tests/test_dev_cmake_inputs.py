"""Original CMake input identity test; no compiler/configure/provider work."""
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import distribution
from dev_support import compare_compiled_inputs


class CMakeInputIdentityTests(unittest.TestCase):
    def test_actual_cmake_helper_is_included_normalized_and_material_changes_invalidate(self):
        (ROOT / 'build').mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='cmake-input-', dir=ROOT / 'build') as temporary:
            owned = Path(temporary)
            for name in ('CMakeLists.txt', 'CMakePresets.json', 'dependencies.lock.json', *distribution.BUILD_GUARD_INPUTS):
                path = owned / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b'// Original disposable input fixture\n')
            helper = owned / 'cmake/TracktionImplementation.cmake'
            helper.parent.mkdir()
            actual = (ROOT / 'cmake/TracktionImplementation.cmake').read_bytes().replace(b'\r\n', b'\n')
            helper.write_bytes(actual)
            with patch.object(distribution, 'ROOT', owned):
                before = distribution.source_state()
                self.assertTrue({'scripts/apply-consumed-phase-patch.ps1',
                                 'scripts/apply-retained-history-patch.ps1',
                                 'scripts/apply-retained-midi-patch.ps1'} <=
                                {row['path'] for row in before})
                rows = [r for r in before if r['path'] == 'cmake/TracktionImplementation.cmake']
                self.assertEqual(len(rows), 1)
                helper.write_bytes(actual.replace(b'\n', b'\r\n'))
                eol = distribution.source_state()
                self.assertNotEqual(rows[0]['sha256'], next(r for r in eol if r['path'] == rows[0]['path'])['sha256'])
                self.assertTrue(compare_compiled_inputs(before, eol)['stable'])
                helper.write_bytes(actual + b'\nmessage(FATAL_ERROR "material fixture change")\n')
                self.assertFalse(compare_compiled_inputs(before, distribution.source_state())['stable'])
                helper.unlink()
                self.assertFalse(compare_compiled_inputs(before, distribution.source_state())['stable'])


if __name__ == '__main__':
    unittest.main()
