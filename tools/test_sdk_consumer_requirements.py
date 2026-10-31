#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
TOOL = ROOT / 'tools/sdk_consumer_requirements.py'
FIXTURE = ROOT / 'tools/sco-core-mcp/fixtures/catalogue.json'


def raw(value):
    return json.dumps(value, sort_keys=True).encode()


def sha(value):
    return hashlib.sha256(value).hexdigest()


class ReportTest(unittest.TestCase):
    def setUp(self):
        self.cat = json.loads(FIXTURE.read_bytes())
        self.cat['schema_version'] = 2
        self.reg = {'schema_version': 2, 'about': 'fixture', 'services': []}
        self.items = [{'id': 'api-query-service', 'language': 'c'}]

    def run_report(self, *, req_edit=None, cat_raw=None, reg_raw=None):
        reg = raw(self.reg) if reg_raw is None else reg_raw
        self.cat['status_registry_sha256'] = sha(reg)
        cat = raw(self.cat) if cat_raw is None else cat_raw
        req = {'schema_version': 1, 'source_sha': self.cat['source_sha'], 'catalogue_sha256': sha(cat), 'items': self.items}
        if req_edit:
            req_edit(req)
        with tempfile.TemporaryDirectory() as d:
            paths = [Path(d) / n for n in ('req', 'cat', 'reg')]
            for p, b in zip(paths, (raw(req), cat, reg)):
                p.write_bytes(b)
            proc = subprocess.run([sys.executable, str(TOOL), '--requirements', str(paths[0]), '--catalogue', str(paths[1]), '--registry', str(paths[2])], capture_output=True, text=True)
        return proc

    def test_success_deterministic_and_neutral(self):
        a, b = self.run_report(), self.run_report()
        self.assertEqual(a.returncode, 0, a.stderr)
        self.assertEqual(a.stdout, b.stdout)
        row = json.loads(a.stdout)['items'][0]
        self.assertEqual(row['static_status']['scope'], 'not_applicable')
        self.assertEqual(row['member_extent'], 24)

    def test_unknown_unsupported_and_restricted(self):
        self.items = [{'id': 'unknown', 'language': 'c'}, {'id': 'sample-write', 'language': 'lua'}]
        p = self.run_report()
        self.assertEqual(p.returncode, 1, p.stderr)
        self.assertEqual({r['result'] for r in json.loads(p.stdout)['items']}, {'unknown_id', 'unsupported'})
        rec = next(r for r in self.cat['records'] if r['id'] == 'sample-read')
        rec['language_mappings']['c']['status'] = 'restricted'
        self.items = [{'id': 'sample-read', 'language': 'c'}]
        self.assertEqual(json.loads(self.run_report().stdout)['items'][0]['result'], 'review_required')
        self.items[0]['accept_restricted'] = True
        self.assertEqual(self.run_report().returncode, 0)

    def test_irrelevant_restricted_acknowledgement_is_ignored(self):
        self.items = [
            {'id': 'api-query-service', 'language': 'c', 'accept_restricted': True},
            {'id': 'sample-write', 'language': 'lua', 'accept_restricted': True},
        ]
        p = self.run_report()
        self.assertEqual(p.returncode, 1, p.stderr)
        rows = {row['id']: row for row in json.loads(p.stdout)['items']}
        self.assertEqual(rows['api-query-service']['result'], 'supported')
        self.assertEqual(rows['sample-write']['result'], 'unsupported')
        self.assertFalse(rows['api-query-service']['restricted_acknowledged'])
        self.assertFalse(rows['sample-write']['restricted_acknowledged'])

    def test_invalid_and_mismatched_inputs_refused(self):
        changes = [lambda r: r.update(source_sha='0'*40), lambda r: r.update(catalogue_sha256='0'*64), lambda r: r.update(schema_version=True), lambda r: r.update(items=[]), lambda r: r['items'].append(copy.deepcopy(r['items'][0])), lambda r: r['items'][0].update(language='rust'), lambda r: r['items'][0].update(accept_restricted=1), lambda r: r.update(extra=1)]
        for change in changes:
            with self.subTest(change=change):
                p = self.run_report(req_edit=change)
                self.assertEqual(p.returncode, 2)
                self.assertEqual(p.stdout, '')
        self.cat['schema_version'] = 1
        self.cat.pop('status_registry_sha256', None)
        # Run with explicitly schema-1 catalogue bytes.
        p = self.run_report(cat_raw=raw(self.cat))
        self.assertEqual(p.returncode, 2)
        p = self.run_report(reg_raw=b'{"schema_version":2,"schema_version":2}')
        self.assertEqual(p.returncode, 2)
        self.assertEqual(p.stdout, '')

    def test_entrypoint_and_duplicate_json_keys(self):
        self.items = [{'id': 'plugin-load', 'language': 'c'}]
        p = self.run_report()
        self.assertEqual(p.returncode, 0, p.stderr)
        row = json.loads(p.stdout)['items'][0]
        self.assertIsNone(row['table_version'])
        self.assertIsNone(row['member_extent'])
        self.assertEqual(row['static_status']['scope'], 'not_applicable')
        p = self.run_report(cat_raw=b'{"schema_version":2,"schema_version":2}')
        self.assertEqual(p.returncode, 2)
        self.assertEqual(p.stdout, '')

    def test_requirements_file_refusals(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cat = raw(self.cat)
            reg = raw(self.reg)
            (root / 'cat').write_bytes(cat)
            (root / 'reg').write_bytes(reg)
            req = root / 'req'
            command = [sys.executable, str(TOOL), '--requirements', str(req),
                       '--catalogue', str(root / 'cat'), '--registry', str(root / 'reg')]
            bad = [b'{', b'{"schema_version":1,"schema_version":1}', b' ' * (256 * 1024 + 1)]
            for contents in bad:
                with self.subTest(size=len(contents)):
                    req.write_bytes(contents)
                    p = subprocess.run(command, capture_output=True, text=True)
                    self.assertEqual(p.returncode, 2)
                    self.assertEqual(p.stdout, '')
                    self.assertLess(len(p.stderr), 1024)
            req.unlink()
            req.mkdir()
            p = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(p.returncode, 2)
            self.assertEqual(p.stdout, '')
            self.assertLess(len(p.stderr), 1024)

    def test_evidence_paths_are_sanitized(self):
        rec = next(r for r in self.cat['records'] if r['id'] == 'api-query-service')
        paths = ['C:/secret', '//host/share', 'good//bad', 'good/../bad',
                 'good/\\bad', 'good/line\nbreak', 'include/good.h']
        rec['language_mappings']['c']['evidence'] = [
            {'path': path, 'token': 'token'} for path in paths
        ]
        p = self.run_report()
        self.assertEqual(p.returncode, 0, p.stderr)
        evidence = json.loads(p.stdout)['items'][0]['source_evidence']
        self.assertEqual([item['path'] for item in evidence], ['include/good.h'])

    def test_nested_member_does_not_match_unqualified_method(self):
        self.items = [{'id': 'sample-read', 'language': 'c'}]
        table = self.cat['tables']['sco_sample_v1']
        table['service'] = 'game.sample'
        table['members'][0] = 'base.snapshot'
        for record in self.cat['records']:
            if record['table'] == 'sco_sample_v1':
                record['service'] = 'game.sample'
            if record['id'] == 'sample-read':
                record['symbol'] = 'sco_sample_v1.base.snapshot'
        self.reg['services'] = [{
            'id': 'game.sample', 'kind': 'service', 'service': 'game.sample',
            'header': 'include/sco_sample.h', 'version': '1.0', 'stability': 'experimental',
            'capabilities': [{'name': 'game.sample.read', 'methods': ['snapshot'],
                              'default': 'ON', 'evidence_level': 'synthetic_only',
                              'evidence': 'fixture'}],
        }]
        # This is a neutral-header fixture; change both header coordinates to game scope.
        table['source_header'] = 'include/sc_sample.h'
        self.reg['services'][0]['header'] = 'include/sc_sample.h'
        for record in self.cat['records']:
            if record['table'] == 'sco_sample_v1':
                record['source_header'] = 'include/sc_sample.h'
        p = self.run_report()
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertEqual(json.loads(p.stdout)['items'][0]['static_status']['scope'], 'unestablished')

    def test_registry_hash_and_explicit_method_version_scope(self):
        self.items = [{'id': 'sample-read', 'language': 'c'}]
        table = self.cat['tables']['sco_sample_v1']
        table['service'] = 'game.sample'
        table['source_header'] = 'include/sc_sample.h'
        for r in self.cat['records']:
            if r['table'] == 'sco_sample_v1':
                r['service'] = 'game.sample'
                r['source_header'] = 'include/sc_sample.h'
        newer = copy.deepcopy(table)
        newer['version'] = 65537
        self.cat['tables']['sco_sample_v1_1'] = newer
        self.reg['services'] = [{'id': 'game.sample', 'kind': 'service', 'service': 'game.sample', 'header': 'include/sc_sample.h', 'version': '1.1', 'stability': 'experimental', 'capabilities': [{'name': 'game.sample.read', 'methods': ['read'], 'default': 'ON', 'evidence_level': 'synthetic_only', 'evidence': 'fixture'}]}]
        p = self.run_report()
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertEqual(json.loads(p.stdout)['items'][0]['static_status']['scope'], 'unestablished')
        self.reg['services'][0]['version'] = '1.0'
        row = json.loads(self.run_report().stdout)['items'][0]
        self.assertEqual(row['static_status']['scope'], 'explicit_method')
        self.reg['services'][0]['capabilities'][0]['methods'] = []
        self.assertEqual(json.loads(self.run_report().stdout)['items'][0]['static_status']['scope'], 'unestablished')
        # Preserve the catalogue binding while changing the supplied registry bytes.
        reg = raw(self.reg)
        self.cat['status_registry_sha256'] = sha(reg)
        cat = raw(self.cat)
        p = self.run_report(cat_raw=cat, reg_raw=reg + b' ')
        self.assertEqual(p.returncode, 2)
        self.assertEqual(p.stdout, '')


if __name__ == '__main__':
    unittest.main()
