import copy
import hashlib
import importlib.util
import io
import json
import tempfile
import subprocess
import sys
import unittest
import unittest.mock
import zipfile
from pathlib import Path
import synthetic_native_evidence as n
HERE=Path(__file__).resolve().parent.parent / "tests/fixtures/synthetic_native_evidence"

class TestNativeEvidence(unittest.TestCase):
    def setUp(self): self.d=n.load(HERE/'fixture.json'); self.ids=n.load(HERE/'index.json')
    def bad(self, mutate, match):
        d=copy.deepcopy(self.d); mutate(d)
        with self.assertRaisesRegex(n.Invalid,match): n.catalogue(d,self.ids)
    def test_cross_profile_synthetic_fixture(self):
        raw=(HERE/'profile-fixture.canonical.json').read_bytes(); p=json.loads(raw)
        self.assertEqual(raw, (HERE.parent/'exact_build_profile/fixture.synthetic.canonical.json').read_bytes())
        self.assertEqual(hashlib.sha256(raw).hexdigest(),self.d['profile']['definition_sha256'])
        self.assertEqual(p['profile_id'],self.d['profile']['profile_id'])
        self.assertEqual(p['revision'],self.d['profile']['revision'])
        self.assertEqual(p['executable'],self.d['build']['executable'])
        self.assertEqual(p['manifest'],self.d['build']['manifest'])
        self.assertEqual(p['pack_source_sha'],self.d['source']['game_pack_source_sha'])
        self.assertEqual(len(n.catalogue(self.d,self.ids)['entries']),2)
        wrong=copy.deepcopy(self.d); wrong['records'][0]['profile']['definition_sha256']='f'*64
        with self.assertRaisesRegex(n.Invalid,'mismatch'): n.catalogue(wrong,self.ids)
    def test_real_index_namespace_is_rejected(self):
        # A label alone must not let this prototype ingest the published index.
        with self.assertRaisesRegex(n.Invalid, 'unique valid ids'):
            n.catalogue(self.d, ['game.real_native'])
        d = copy.deepcopy(self.d)
        d['records'][0]['native_id'] = 'game.real_native'
        d['evidence'][0]['native_id'] = 'game.real_native'
        with self.assertRaisesRegex(n.Invalid, 'invalid native id'):
            n.catalogue(d, self.ids)

    def test_cli_projection_matches_fixture(self):
        result = subprocess.run([sys.executable, str(Path(n.__file__)),
                                 str(HERE / 'fixture.json'), str(HERE / 'index.json')],
                                capture_output=True, check=True)
        self.assertEqual(result.stdout, (HERE / 'generated-fixture.json').read_bytes())
        self.assertEqual(result.stderr, b'')

    def test_cli_file_projection_matches_stdout_and_fixture(self):
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / 'projection.json'
            result = subprocess.run([sys.executable, str(Path(n.__file__)),
                                     str(HERE / 'fixture.json'), str(HERE / 'index.json'),
                                     '--output', str(output)], capture_output=True, check=True)
            stdout = subprocess.run([sys.executable, str(Path(n.__file__)),
                                     str(HERE / 'fixture.json'), str(HERE / 'index.json')],
                                    capture_output=True, check=True)
            self.assertEqual(result.stdout, b'')
            self.assertEqual(result.stderr, b'')
            self.assertEqual(stdout.stderr, b'')
            self.assertEqual(output.read_bytes(), stdout.stdout)
            self.assertEqual(output.read_bytes(), (HERE / 'generated-fixture.json').read_bytes())

    def test_all_sdk_archives_exclude_model_and_fixtures(self):
        root = HERE.parents[2]
        spec = importlib.util.spec_from_file_location('sco_sdk_package', root / 'sdk/package.py')
        package = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(package)
        manifest = package.load_manifest()
        # Substitute compiler-backed generated documentation only. This checks
        # archive membership, not production catalogue generation or release.
        generated = lambda profile: (b'{}\n' if profile in
                                     ('game-sc', 'legacy', 'host-source-sc') else None)
        with unittest.mock.patch.object(package, 'developer_catalogue', side_effect=generated), \
             unittest.mock.patch.object(package, 'developer_guides', side_effect=generated):
            for profile in manifest['profiles']:
                with self.subTest(profile=profile):
                    self.assertFalse(any('synthetic_native_evidence' in c['source']
                                         for c in package.select(manifest, profile)))
                    top, entries = package.archive_entries(manifest, profile)
                    archive = package.archive_bytes(top, entries)
                    with zipfile.ZipFile(io.BytesIO(archive)) as zipped:
                        self.assertFalse(any('synthetic_native_evidence' in name
                                             for name in zipped.namelist()))

    def test_deterministic_and_missing(self):
        result=n.catalogue(self.d,self.ids)
        self.assertEqual(result,n.catalogue(self.d,list(reversed(self.ids))))
        self.assertEqual(result['entries'],[{'native_id':'fixture.native_a','evidence_status':'unverified'},{'native_id':'fixture.native_b','evidence_status':'not_recorded'}])
    def test_wrong_build(self): self.bad(lambda d:d['records'][0]['build']['executable'].update(sha256='f'*64),'mismatch')
    def test_zero_profile_revision(self): self.bad(lambda d:d['profile'].update(revision=0),'invalid unsigned')
    def test_wrong_profile(self): self.bad(lambda d:d['evidence'][0]['profile'].update(definition_sha256='f'*64),'mismatch')
    def test_profile_alias_does_not_prove_equality(self): self.bad(lambda d:d['records'][0]['profile'].update(profile_id='another-profile'),'mismatch')
    def test_missing_provenance(self): self.bad(lambda d:d['evidence'][0].pop('producer'),'expected exactly')
    def test_bad_digest(self): self.bad(lambda d:d['build']['executable'].update(sha256='oops'),'SHA-256')
    def test_mismatched_evidence_native(self): self.bad(lambda d:d['evidence'][0].update(native_id='fixture.native_b'),'evidence native mismatch')
    def test_wrong_source(self): self.bad(lambda d:d['evidence'][0]['source'].update(core_source_sha='f'*40),'source mismatch')
    def test_wrong_manifest(self): self.bad(lambda d:d['records'][0]['build']['manifest'].update(build=[1,0,0,0]),'mismatch')
    def test_wrong_image_size(self): self.bad(lambda d:d['records'][0]['build']['executable'].update(size_of_image=2),'mismatch')
    def test_unknown_evidence(self): self.bad(lambda d:d['records'][0].update(evidence_ids=['missing']),'unknown evidence')
    def test_orphan_known_native_rejected(self):
        def add(d):
            e=copy.deepcopy(d['evidence'][0]); e.update(id='orphan-known', native_id='fixture.native_b'); d['evidence'].append(e)
        self.bad(add,'unreferenced evidence ids: orphan-known')
    def test_orphan_unknown_native_rejected(self):
        def add(d):
            e=copy.deepcopy(d['evidence'][0]); e.update(id='orphan-unknown', native_id='fixture.unknown'); d['evidence'].append(e)
        self.bad(add,'unreferenced evidence ids: orphan-unknown')
    def test_unknown_native_even_when_joined_rejected(self):
        def add(d):
            e=copy.deepcopy(d['evidence'][0]); e.update(id='unknown-joined',native_id='fixture.unknown'); d['evidence'].append(e)
            r=copy.deepcopy(d['records'][0]); r.update(native_id='fixture.unknown',evidence_ids=['unknown-joined']); d['records'].append(r)
        self.bad(add,'unknown native ids: fixture.unknown')
    def test_duplicate_record(self): self.bad(lambda d:d['records'].append(copy.deepcopy(d['records'][0])),'duplicate native')
    def test_duplicate_evidence(self): self.bad(lambda d:d['evidence'].append(copy.deepcopy(d['evidence'][0])),'duplicate evidence')
    def test_no_real_claim_from_synthetic(self): self.bad(lambda d:d['records'][0].update(status='in_game_verified'),'cannot claim')
    def test_no_static_claim_from_synthetic(self): self.bad(lambda d:d['records'][0].update(status='static_resolved'),'cannot claim')
    def test_no_non_synthetic_dataset(self): self.bad(lambda d:d.update(dataset_kind='real'),'synthetic dataset only')
    def test_no_non_synthetic_evidence(self): self.bad(lambda d:d['evidence'][0].update(kind='static_report'),'synthetic evidence only')
    def test_no_implicit_extra_field(self): self.bad(lambda d:d['records'][0].update(address='forbidden'),'expected exactly')
    def test_unknown_native(self): self.bad(lambda d:(d['records'][0].update(native_id='fixture.not_in_index'), d['evidence'][0].update(native_id='fixture.not_in_index')),'unknown native')
    def test_invalid_calendar(self): self.bad(lambda d:d['evidence'][0].update(created_utc='2026-99-99T99:99:99Z'),'invalid UTC')
    def test_unhashable_status(self): self.bad(lambda d:d['records'][0].update(status=[]),'unknown status')
    def test_unhashable_kind(self): self.bad(lambda d:d['evidence'][0].update(kind={}),'invalid evidence kind')
    def test_nonfinite_json(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'nonfinite.json'
            path.write_text('{"x":NaN}', encoding='utf8')
            with self.assertRaisesRegex(n.Invalid,'non-finite'): n.load(path)
    def test_duplicate_json_keys(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'duplicate.json'
            path.write_text('{"x":1,"x":2}', encoding='utf8')
            with self.assertRaisesRegex(n.Invalid,'duplicate JSON key'): n.load(path)
    def test_duplicate_native_index(self):
        with self.assertRaisesRegex(n.Invalid,'unique'): n.catalogue(self.d,self.ids+self.ids)

if __name__ == '__main__': unittest.main()
