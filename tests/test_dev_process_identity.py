"""OS process birth identity for the shared engineering owner, without launching builds."""
import ctypes
import errno
import os
from pathlib import Path
import sys
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import dev_support as support


class ProcessIdentityTests(unittest.TestCase):
    def query(self, *, error=0, size=None, pid=42, seconds=1700000000, micros=123456):
        def invoke(requested, flavor, argument, buffer, length):
            self.assertEqual((requested, flavor, argument), (42, 3, 0))
            self.assertEqual(length, 136)
            info = ctypes.cast(buffer, ctypes.POINTER(support._DarwinBSDInfo)).contents
            info.pid, info.start_sec, info.start_usec = pid, seconds, micros
            ctypes.set_errno(error)
            return length if size is None else size
        library = Mock()
        library.proc_pidinfo.side_effect = invoke
        return library

    def test_darwin_birth_identity_retains_microseconds(self):
        for micros in (123456, 123457):
            with patch.object(support.ctypes, "CDLL", return_value=self.query(micros=micros)):
                self.assertEqual(support._darwin_process_identity(42), f"1700000000:{micros}")

    def test_darwin_missing_process_is_not_live(self):
        with patch.object(support.ctypes, "CDLL", return_value=self.query(error=errno.ESRCH, size=0)):
            self.assertIsNone(support._darwin_process_identity(42))

    def test_darwin_uncertain_or_mismatched_process_is_refused(self):
        for result in (dict(error=errno.EPERM, size=0), dict(size=12), dict(pid=43), dict(seconds=0)):
            with self.subTest(result=result), patch.object(support.ctypes, "CDLL", return_value=self.query(**result)):
                with self.assertRaisesRegex(RuntimeError, "preserve uncertain process"):
                    support._darwin_process_identity(42)

    def test_invalid_pids_are_not_queried(self):
        for pid in (None, 0, -1, 2**31, "42"):
            self.assertIsNone(support.process_identity(pid))

    def test_current_native_process_identity_is_stable(self):
        identity = support.process_identity(os.getpid())
        self.assertIsNotNone(identity)
        self.assertEqual(identity, support.process_identity(os.getpid()))


if __name__ == "__main__":
    unittest.main()
