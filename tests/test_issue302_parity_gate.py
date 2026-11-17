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
    def test_ci_checkouts_materialize_baseline_from_shallow_source(self):
        # The integration workflow starts from checkout's depth-one default. The
        # gate archives a pinned commit, so its jobs must materialize history.
        for workflow_name, job in (('cmake.yml', 'linux'), ('cmake.yml', 'windows'),
                                   ('test.yml', 'test')):
            workflow = (parity.ROOT / '.github/workflows' / workflow_name).read_text()
            section = workflow.split(f'  {job}:', 1)[1].split('\n  #', 1)[0]
            checkout = section.split('      - name: ', 1)[0]
            self.assertIn('fetch-depth: 0', checkout, f'{workflow_name}: {job}')

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

    def run_gate(self, baseline, current, intentional=None):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / 'out'
            with mock.patch.object(sys, 'argv', ['issue302_valid_parity.py', '--out', str(out)]), \
                 mock.patch.object(parity, 'generate', side_effect=[(baseline, {}), (current, {})]), \
                 mock.patch.object(parity, 'INTENTIONAL_LAYOUT_CHANGES', intentional or {}), \
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

    def test_intentional_layout_change_is_reported_not_counted(self):
        test = 'tests/test_world_bounds_302.cpp'
        old = {'file': 'world_sigs.cpp', 'id': 'quantum.on_action', 'fixture': 'pattern',
               'state': 'OK', 'rva': 1, 'matches': 1, 'reason': None, 'fixture_hash': 'abc'}
        new = dict(old, fixture='shared-Fn:kQuantumOnAction', state='FAILED', rva=None, matches=0,
                   reason='quantum action range changed')
        other = dict(old, file='example_sigs.cpp', id='example')
        exempt = {'quantum.on_action': ('layout change', test, 'quantum.on_action')}
        status, report = self.run_gate([old, other], [new, other.copy()], exempt)
        self.assertEqual(status, 0)
        self.assertEqual(report['coverage_gap_ids'], [])
        self.assertEqual(len(report['intentional_layout_change_differences']), 2)
        self.assertEqual(report['intentional_layout_changes']['quantum.on_action']['covered_by'], test)
        # Any other difference still fails the gate.
        status, _ = self.run_gate([old, other], [new, dict(other, rva=2)], exempt)
        self.assertEqual(status, 1)

    def test_intentional_layout_change_must_name_a_known_row_and_covering_test(self):
        row = {'file': 'example_sigs.cpp', 'id': 'example', 'fixture': 'pattern',
               'state': 'OK', 'rva': 1, 'matches': 1, 'reason': None, 'fixture_hash': 'abc'}
        for exempt in ({'missing.row': ('x', 'tests/test_world_bounds_302.cpp', 'quantum')},
                       {'example': ('x', 'tests/no_such_test.cpp', 'example')},
                       {'example': ('x', 'tests/test_world_bounds_302.cpp', 'token-not-in-file')}):
            with self.assertRaises(AssertionError):
                self.run_gate([row], [row.copy()], exempt)

    def test_declared_layout_changes_have_covering_tests(self):
        parity.check_intentional_layout_changes(set(parity.INTENTIONAL_LAYOUT_CHANGES))


if __name__ == '__main__':
    unittest.main()
