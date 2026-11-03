"""Additional preparation-profile oracles; original development inventory stays intact."""
import os
import json
from pathlib import Path
import tempfile
import unittest
import test_dev_local_source_guard as fixtures

ROOT=Path(__file__).resolve().parents[1]

class PreparedFixtureProfileTests(unittest.TestCase):
    def test_closed_profiles_preserve_mixed_stages_and_optional_header(self):
        with tempfile.TemporaryDirectory(prefix="fixture-profiles-") as directory:
            base=Path(directory);source=base/'source';approved=fixtures.load_guard(ROOT).APPROVED
            fixtures.copy_fixture(ROOT,source,approved)
            prefix='vendor/source/'+approved['sourceDirectory']+'/'
            local=next(f for f in approved['families'] if f['path']=='patches/tracktion-local-source-initial/manifest.json')
            for profile in ('local-preimage','all-final','absent-only'):
                first,second=(base/(profile+suffix) for suffix in ('-first','-second'))
                a=fixtures.copy_fixture(source,first,approved,profile)
                b=fixtures.copy_fixture(source,second,approved,profile)
                self.assertIsNot(a,b)
                rows=b.preflight()
                self.assertEqual(any(row['pending'] for row in rows),profile!='all-final')
                for entry in local['files']:
                    rel=prefix+entry['path']
                    if profile!='all-final':self.assertEqual((first/rel).read_bytes(),(source/rel).read_bytes())
                    raw=(second/rel).read_bytes();(first/rel).write_bytes(b'independent mutation')
                    self.assertEqual((second/rel).read_bytes(),raw)
            self.assertLessEqual(len(fixtures._fixture_seeds),3)
            with self.assertRaisesRegex(ValueError,'profile'):
                fixtures.copy_fixture(source,base/'bad-profile',approved,'unknown')
            with self.assertRaisesRegex(ValueError,'fresh independent'):
                fixtures.copy_fixture(source,source,approved)
            optional=next(entry for f in approved['families'] for entry in f['files'] if entry.get('originalAbsent'))
            path=source/(prefix+optional['path']);path.unlink()
            fixtures.copy_fixture(source,base/'absent-header',approved,'all-final')
            previous=fixtures._fixture_seeds['all-final']
            path.write_bytes(b'unregistered optional-header contents')
            with self.assertRaises(ValueError):fixtures.copy_fixture(source,base/'foreign-header',approved,'all-final')
            self.assertIs(fixtures._fixture_seeds['all-final'],previous)

    def test_all_profiles_invalidate_actual_changed_content_without_publishing_failure(self):
        with tempfile.TemporaryDirectory(prefix="fixture-profile-invalidation-") as directory:
            base=Path(directory);source=base/'source';approved=fixtures.load_guard(ROOT).APPROVED
            fixtures.copy_fixture(ROOT,source,approved)
            family=approved['families'][0];entry=family['files'][0]
            dependency=None
            metadata=set(approved['requiredContractSha256']) | {f['path'] for f in approved['families']} | {approved['sourceBurstJuce']['path']}
            for name in sorted(metadata):
                data=json.loads((source/name).read_text(encoding='utf-8'))
                if data.get('vendorReadDependencies'):
                    dependency='vendor/source/'+data['sourceDirectory']+'/'+data['vendorReadDependencies'][0]['path'];break
            self.assertIsNotNone(dependency)
            prefix='vendor/source/'+approved['sourceDirectory']+'/'
            targets=[('local-preimage',family['path']),('absent-only',family['path']),
                     ('all-final',family['path']),('all-final',entry['patch']),
                     ('all-final',prefix+entry['path']),('all-final',dependency)]
            for index,(profile,name) in enumerate(targets):
                with self.subTest(profile=profile,path=name):
                    fixtures.copy_fixture(source,base/('valid-'+str(index)),approved,profile)
                    previous=fixtures._fixture_seeds[profile]
                    path=source/name;raw=path.read_bytes();before=path.stat()
                    changed=bytes([raw[0]^1])+raw[1:]
                    path.write_bytes(changed);os.utime(path,ns=(before.st_atime_ns,before.st_mtime_ns))
                    self.assertEqual((path.stat().st_size,path.stat().st_mtime_ns),(len(raw),before.st_mtime_ns))
                    try:
                        if name==dependency:
                            # Read-only closure dependencies are not patch targets:
                            # the seed must copy new bytes, never bless an old key.
                            dest=base/('changed-read-dependency-'+str(index))
                            fixtures.copy_fixture(source,dest,approved,profile)
                            self.assertEqual((dest/name).read_bytes(),changed)
                            self.assertNotEqual(fixtures._fixture_seeds[profile][0],previous[0])
                        else:
                            with self.assertRaises(ValueError):fixtures.copy_fixture(source,base/('refused-'+str(index)),approved,profile)
                            self.assertIs(fixtures._fixture_seeds[profile],previous)
                    finally:
                        path.write_bytes(raw);os.utime(path,ns=(before.st_atime_ns,before.st_mtime_ns))
