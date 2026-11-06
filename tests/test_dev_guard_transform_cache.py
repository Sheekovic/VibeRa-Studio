"""Lightweight immutable-transform cache checks; no native/build/provider work."""
from pathlib import Path
import tempfile
import os
import stat
import subprocess
import types
import unittest
from unittest.mock import patch

import test_dev_local_source_guard as fixtures

ROOT=Path(__file__).resolve().parents[1]
PATH='modules/original-cache-fixture.h'
PAYLOAD=('--- a/'+PATH+'\n+++ b/'+PATH+'\n@@ -1 +1 @@\n-old\n+new\n').encode()


class GuardPureTransformCacheTests(unittest.TestCase):
    def setUp(self):
        self.guard=fixtures.load_guard(ROOT) # Module import only; no preflight/native reads.

    def test_content_keys_deep_immutable_results_and_both_directions(self):
        guard=self.guard
        hunks=guard.parse(PAYLOAD,PATH,1,1)
        again=guard.parse(bytes(bytearray(PAYLOAD)),str(PATH),1,1)
        self.assertIs(hunks,again)
        self.assertIsInstance(hunks,tuple)
        self.assertIsInstance(hunks[0],tuple)
        self.assertIsInstance(hunks[0][4],tuple)
        self.assertTrue(all(type(line) is bytes for line in hunks[0][4]))
        with self.assertRaises(TypeError):hunks[0][4][0]=b'-changed\n'
        with self.assertRaises(AttributeError):hunks.append('changed')
        source=b'old\nuntouched\n'
        final=guard.apply(hunks,source)
        self.assertEqual(final,b'new\nuntouched\n')
        self.assertIs(final,guard.apply(hunks,bytes(bytearray(source))))
        self.assertEqual(guard.apply(hunks,final,True),source)
        self.assertEqual(guard.apply(hunks,source),guard._apply_uncached(hunks,source))
        self.assertGreater(guard._parse_cached.cache_info().hits,0)
        self.assertGreater(guard._apply_cached.cache_info().hits,0)

        # The actual authoritative chain outgrew the original96/192 bounds.
        # Exercise fresh complete preflights, never cache filesystem validity.
        # A future chain exceeding bounded capacity must fail here before its
        # repeated valid parses/replays silently turn into all misses again.
        guard._parse_cached.cache_clear();guard._apply_cached.cache_clear()
        first=guard.preflight()
        stages=sum(len(row['chain']) for row in first)
        parsed=guard._parse_cached.cache_info();replayed=guard._apply_cached.cache_info()
        self.assertLessEqual(stages,guard._PARSE_CACHE_ENTRIES)
        self.assertLessEqual(stages*2,guard._REPLAY_CACHE_ENTRIES)
        self.assertEqual(first,guard.preflight())
        next_parse=guard._parse_cached.cache_info();next_replay=guard._apply_cached.cache_info()
        self.assertEqual(next_parse.misses,parsed.misses)
        self.assertEqual(next_replay.misses,replayed.misses)
        self.assertEqual(next_parse.hits-parsed.hits,stages)
        self.assertEqual(next_replay.hits-replayed.hits,stages*2)
        # A changed actual payload must still undergo validation, even after
        # its original immutable input has a successful cache entry.
        entry=first[0]['chain'][0]['entry'];payload=guard.read(entry['patch'])
        for _ in range(2):
            with self.assertRaisesRegex(ValueError,'numstat mismatch'):
                guard.parse(payload,entry['path'],entry['addedLines']+1,entry['deletedLines'])


    def test_changed_and_mutable_inputs_never_reuse_prior_success(self):
        guard=self.guard;hunks=guard.parse(PAYLOAD,PATH,1,1)
        guard.apply(hunks,b'old\n')
        for _ in range(2):
            with self.assertRaisesRegex(ValueError,'preimage mismatch'):guard.apply(hunks,b'foreign\n')
            with self.assertRaisesRegex(ValueError,'numstat mismatch'):guard.parse(PAYLOAD,PATH,2,1)
            with self.assertRaisesRegex(ValueError,'path/header mismatch'):guard.parse(PAYLOAD,PATH+'-foreign',1,1)
        mutable=bytearray(b'old\n')
        before=guard._apply_cached.cache_info()
        self.assertEqual(guard.apply(hunks,mutable),b'new\n')
        mutable[0]=ord('x')
        with self.assertRaisesRegex(ValueError,'preimage mismatch'):guard.apply(hunks,mutable)
        self.assertEqual(guard._apply_cached.cache_info(),before)
        mutable_payload=bytearray(PAYLOAD)
        parsed=guard.parse(mutable_payload,PATH,1,1)
        self.assertIs(type(parsed),tuple) # Only verified exact bytes get cache marker.
        mutable_hunks=[(a,b,c,d,list(body)) for a,b,c,d,body in hunks]
        self.assertEqual(guard.apply(mutable_hunks,b'old\n'),b'new\n')
        mutable_hunks[0][4][0]=b'-foreign\n'
        with self.assertRaisesRegex(ValueError,'preimage mismatch'):guard.apply(mutable_hunks,b'old\n')

    def test_entry_limits_and_oversize_inputs_keep_uncached_semantics(self):
        guard=self.guard
        for i in range(guard._PARSE_CACHE_ENTRIES+7):
            path=PATH+str(i);payload=PAYLOAD.replace(PATH.encode(),path.encode())
            guard.parse(payload,path,1,1)
        self.assertEqual(guard._parse_cached.cache_info().currsize,guard._PARSE_CACHE_ENTRIES)
        hunks=guard.parse(PAYLOAD,PATH,1,1)
        for i in range(guard._REPLAY_CACHE_ENTRIES+7):
            source=b'old\n'+str(i).encode()+b'\n'
            self.assertEqual(guard.apply(hunks,source),b'new\n'+str(i).encode()+b'\n')
        self.assertEqual(guard._apply_cached.cache_info().currsize,guard._REPLAY_CACHE_ENTRIES)
        long_line=b'o'*(guard._MAX_CACHED_PATCH_BYTES+1)+b'\n'
        large=('--- a/'+PATH+'\n+++ b/'+PATH+'\n@@ -1 +1 @@\n').encode()+b'-'+long_line+b'+new\n'
        before=guard._parse_cached.cache_info()
        uncached=guard.parse(large,PATH,1,1)
        self.assertIs(type(uncached),tuple)
        self.assertEqual(guard._parse_cached.cache_info(),before)
        self.assertEqual(guard.apply(uncached,long_line),b'new\n')
        source=b'old\n'+b'x'*guard._MAX_CACHED_SOURCE_BYTES
        before=guard._apply_cached.cache_info()
        self.assertEqual(guard.apply(hunks,source),b'new\n'+source[4:])
        self.assertEqual(guard._apply_cached.cache_info(),before)

    def test_filesystem_target_and_contract_hash_checks_stay_fresh(self):
        guard=self.guard;hunks=guard.parse(PAYLOAD,PATH,1,1);guard.apply(hunks,b'old\n')
        with tempfile.TemporaryDirectory(prefix='guard-cache-io-contract-') as temporary:
            root=Path(temporary);guard.ROOT=root
            contract=root/'contract.json';contract.write_bytes(b'{"allowed": true}\n')
            expected=guard.digest(contract.read_bytes())
            first=guard.load_pinned('contract.json',expected)
            self.assertEqual(first,guard.load_pinned('contract.json',expected))
            self.assertIsNot(first,guard.load_pinned('contract.json',expected))
            contract.write_bytes(b'{"allowed": false}\n')
            with self.assertRaisesRegex(ValueError,'contract (raw )?identity'):guard.load_pinned('contract.json',expected)
            native=root/'native.h';native.write_bytes(b'old\n')
            self.assertEqual(guard.apply(hunks,guard.read_target('native.h')),b'new\n')
            native.write_bytes(b'foreign\n')
            self.assertEqual(guard.read_target('native.h'),b'foreign\n')
            with self.assertRaisesRegex(ValueError,'preimage mismatch'):guard.apply(hunks,guard.read_target('native.h'))
            native.unlink()
            with self.assertRaisesRegex(ValueError,'required ancestry/input missing'):guard.read_target('native.h')
            with self.assertRaisesRegex(ValueError,'escapes project root'):guard.ordinary(root/'..'/'foreign.h')

            # Warm lexical ancestry is only immutable path spelling. Each
            # read still checks every actual ancestor and leaf exactly once.
            native.write_bytes(b'old\n')
            absolute=native.absolute()
            expected=[str(absolute),*[str(item) for item in absolute.parents]]
            ancestry=guard._lexical_ancestry(str(root),str(absolute))
            self.assertIs(type(ancestry),tuple)
            self.assertTrue(all(type(item) is str for item in ancestry))
            self.assertEqual(list(ancestry),expected)
            self.assertIs(ancestry,guard._lexical_ancestry(str(root),str(absolute)))
            original_lstat=guard.os.lstat
            observed=[]
            def fresh(path):
                observed.append(str(path))
                return original_lstat(path)
            with patch.object(guard, '_directory_attributes', None):
                with patch.object(guard.os,'lstat',side_effect=fresh):
                    self.assertEqual(guard.read_target('native.h'),b'old\n')
                    self.assertEqual(observed,expected)
                    observed.clear()
                    native.write_bytes(b'changed\n')
                    self.assertEqual(guard.read_target('native.h'),b'changed\n')
                    self.assertEqual(observed,expected)
                # A reparse change at ANY ancestor is refused despite the warm key.
                for changed in expected:
                    def reparse(path):
                        actual=original_lstat(path)
                        if str(path)==changed:
                            return types.SimpleNamespace(st_mode=actual.st_mode,st_file_attributes=0x400)
                        return actual
                    with patch.object(guard.os,'lstat',side_effect=reparse):
                        with self.assertRaisesRegex(ValueError,'crosses reparse point'):guard.read_target('native.h')
            # No missing/existing/type result survives a later operation.
            native.unlink()
            self.assertIsNone(guard.read_target('native.h',allow_absent=True))
            native.write_bytes(b'newly present\n')
            self.assertEqual(guard.read_target('native.h',allow_absent=True),b'newly present\n')
            native.unlink();native.mkdir()
            with self.assertRaisesRegex(ValueError,'target must be an ordinary file'):guard.read_target('native.h',allow_absent=True)
            with self.assertRaisesRegex(ValueError,'required ordinary file missing'):guard.read('native.h')
            native.rmdir()
            native.write_bytes(b'after directory\n')
            self.assertEqual(guard.read('native.h'),b'after directory\n')
            # ROOT is part of the lexical key, so previous lexical scope proof
            # never authorizes the same path under a different project root.
            with patch.object(guard,'ROOT',root/'different-root'):
                with self.assertRaisesRegex(ValueError,'escapes project root'):guard.ordinary(native)
            for index in range(guard._ANCESTRY_CACHE_ENTRIES+7):
                guard._lexical_ancestry(str(root),str(root/('lexical-only-'+str(index))))
            self.assertEqual(guard._lexical_ancestry.cache_info().currsize,guard._ANCESTRY_CACHE_ENTRIES)

    def test_directory_attributes_are_fresh_and_fail_closed_with_original_fallback(self):
        guard=self.guard
        with tempfile.TemporaryDirectory(prefix='guard-directory-attributes-') as temporary:
            root=Path(temporary);guard.ROOT=root
            native=root/'native.h';native.write_bytes(b'first\n')
            expected=[str(native.absolute()),*[str(p) for p in native.absolute().parents]]
            guard._lexical_ancestry(str(root),expected[0])
            original_lstat=guard.os.lstat
            observed=[]
            def leaf(path):
                observed.append(('stat',str(path)))
                return original_lstat(path)
            def attributes(path):
                observed.append(('attributes',str(path)))
                actual=original_lstat(path)
                return getattr(actual,'st_file_attributes',0) | (0x10 if stat.S_ISDIR(actual.st_mode) else 0)
            expected_calls=[('stat',expected[0]),*[('attributes',p) for p in expected[1:]]]
            with patch.object(guard,'_directory_attributes',side_effect=attributes), patch.object(guard.os,'lstat',side_effect=leaf):
                self.assertEqual(guard.read_target('native.h'),b'first\n')
                self.assertEqual(observed,expected_calls)
                native.write_bytes(b'changed\n');observed.clear()
                self.assertEqual(guard.read_target('native.h'),b'changed\n')
                self.assertEqual(observed,expected_calls)
            # Every ancestor, including those above ROOT and drive/share root,
            # is queried anew. A warmed lexical key cannot authorize a reparse.
            for changed in expected[1:]:
                def reparse(path):return 0x410 if path==changed else attributes(path)
                with patch.object(guard,'_directory_attributes',side_effect=reparse):
                    with self.assertRaisesRegex(ValueError,'crosses reparse point'):guard.read_target('native.h')
                def not_directory(path):return 0x80 if path==changed else attributes(path)
                with patch.object(guard,'_directory_attributes',side_effect=not_directory):
                    with self.assertRaisesRegex(ValueError,'ancestry is not directory'):guard.read_target('native.h')
            with patch.object(guard,'_directory_attributes',return_value=0xffffffff), patch.object(guard.os,'lstat',side_effect=leaf):
                observed.clear()
                self.assertEqual(guard.read_target('native.h'),b'changed\n')
                self.assertEqual(observed,[('stat',p) for p in expected])
                for error in (FileNotFoundError('missing'),PermissionError('denied')):
                    def failed_parent(path):
                        if str(path)==expected[1]:raise error
                        return original_lstat(path)
                    with patch.object(guard.os,'lstat',side_effect=failed_parent):
                        if isinstance(error,FileNotFoundError):
                            with self.assertRaisesRegex(ValueError,'required ancestry/input missing'):guard.read_target('native.h',allow_absent=True)
                        else:
                            with self.assertRaises(PermissionError):guard.read_target('native.h')
            # Actual native API, not a mocked detector, must reject a real
            # task-owned junction/symlink. Removing the link never walks target.
            target=root/'target';target.mkdir();(target/'data.h').write_bytes(b'owned\n')
            link=root/'link'
            if os.name=='nt':
                self.assertIsNotNone(guard._directory_attributes)
                self.assertEqual(guard._directory_attributes.restype.__name__,'c_ulong')
                env=dict(os.environ,VIBERA_TEST_JUNCTION=str(link),VIBERA_TEST_JUNCTION_TARGET=str(target))
                subprocess.run(['powershell','-NoProfile','-Command',
                    "$ErrorActionPreference='Stop'; New-Item -ItemType Junction -Path $env:VIBERA_TEST_JUNCTION -Target $env:VIBERA_TEST_JUNCTION_TARGET | Out-Null"],
                    env=env,check=True,capture_output=True,timeout=5)
            else:
                link.symlink_to(target,target_is_directory=True)
            try:
                with self.assertRaisesRegex(ValueError,'crosses reparse point'):guard.read_target('link/data.h')
                with self.assertRaisesRegex(ValueError,'crosses reparse point'):guard.ordinary(link)
                self.assertEqual((target/'data.h').read_bytes(),b'owned\n')
            finally:
                if os.name=='nt':os.rmdir(link)
                else:link.unlink()



if __name__=='__main__':unittest.main()
