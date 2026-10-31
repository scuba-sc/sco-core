import contextlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

import issue302_valid_parity as parity


class ParityGateTest(unittest.TestCase):
    def test_cmake_checkouts_materialize_baseline_from_shallow_source(self):
        # The integration workflow starts from checkout's depth-one default. The
        # gate archives a pinned commit, so its jobs must materialize history.
        workflow = (parity.ROOT / '.github/workflows/cmake.yml').read_text()
        for job in ('linux', 'windows'):
            section = workflow.split(f'  {job}:', 1)[1].split('\n  #', 1)[0]
            checkout = section.split('      - name: ', 1)[0]
            self.assertIn('fetch-depth: 0', checkout)

        def git(*args, cwd):
            return subprocess.run(['git', *map(str, args)], cwd=cwd, check=True,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE)

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / 'source'
            source.mkdir()
            git('init', cwd=source)
            (source / 'fixture').write_text('baseline\n')
            git('add', 'fixture', cwd=source)
            commit = ['-c', 'user.name=CI Fixture', '-c', 'user.email=fixture@example.invalid',
                      'commit', '-m']
            git(*commit, 'baseline', cwd=source)
            baseline = git('rev-parse', 'HEAD', cwd=source).stdout.decode().strip()
            (source / 'fixture').write_text('current\n')
            git('add', 'fixture', cwd=source)
            git(*commit, 'current', cwd=source)
            clone = root / 'shallow'
            git('clone', '--depth=1', source.as_uri(), clone, cwd=root)
            missing = subprocess.run(['git', 'cat-file', '-e', baseline], cwd=clone,
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            self.assertNotEqual(missing.returncode, 0)
            git('fetch', '--unshallow', cwd=clone)
            archive = git('archive', baseline, cwd=clone).stdout
            self.assertTrue(archive)

    def run_gate(self, baseline, current):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / 'out'
            with mock.patch.object(sys, 'argv', ['issue302_valid_parity.py', '--out', str(out)]), \
                 mock.patch.object(parity, 'generate', side_effect=[(baseline, {}), (current, {})]), \
                 contextlib.redirect_stdout(io.StringIO()):
                status = parity.main()
            return status, json.loads((out / 'scope-census.json').read_text())

    def test_missing_positive_parity_fails_even_when_observations_match(self):
        row = {'file': 'example_sigs.cpp', 'id': 'example', 'fixture': 'gap:no-valid-fixture',
               'state': 'FAIL', 'rva': None, 'matches': 0, 'reason': 'no valid fixture', 'fixture_hash': 'abc'}
        status, report = self.run_gate([row], [row.copy()])
        self.assertEqual(report['coverage_gap_ids'], ['example'])
        self.assertEqual(status, 1)

    def test_removing_only_positive_registry_case_breaks_gate(self):
        row = {'file': 'example_sigs.cpp', 'id': 'example', 'fixture': 'pattern',
               'state': 'FAIL', 'rva': None, 'matches': 0, 'reason': 'miss', 'fixture_hash': 'abc'}
        positive = dict(row, file='registry', fixture='existing:QuitImage', state='OK', rva=1, matches=1, reason=None)
        status, _ = self.run_gate([row, positive], [row.copy(), positive.copy()])
        self.assertEqual(status, 0)
        status, report = self.run_gate([row], [row.copy()])
        self.assertEqual(report['coverage_gap_ids'], ['example'])
        self.assertEqual(status, 1)

    def test_success_claim_is_scoped_to_synthetic_cases(self):
        row = {'file': 'example_sigs.cpp', 'id': 'example', 'fixture': 'pattern',
               'state': 'OK', 'rva': 1, 'matches': 1, 'reason': None, 'fixture_hash': 'abc'}
        status, report = self.run_gate([row], [row.copy()])
        self.assertEqual(status, 0)
        self.assertTrue(report['synthetic_case_parity_proven'])
        self.assertNotIn('full_valid_input_parity_proven', report)


if __name__ == '__main__':
    unittest.main()
