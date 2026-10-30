import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

import issue302_valid_parity as parity


class ParityGateTest(unittest.TestCase):
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
