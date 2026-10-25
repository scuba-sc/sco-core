#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Workflow allowlist, exact archive bytes and real stdio discovery; stdlib only."""
import copy
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import sdk_workflows as workflows


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


pkg = load('workflow_package', ROOT / 'sdk/package.py')
client_module = load('workflow_client', ROOT / 'tools/sco-core-mcp/test_server.py')
SHA = 'a' * 40


class Inputs(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        shutil.copytree(ROOT / 'sdk/skills', self.root / 'sdk/skills')
        (self.root / 'sdk/VERSION').write_text('2.4.0\n')

    def test_determinism_and_progressive_files(self):
        a = workflows.build(self.root, SHA)
        self.assertEqual(workflows.encoded(a), workflows.encoded(workflows.build(self.root, SHA)))
        self.assertEqual(len(a['workflows']), 6)
        self.assertEqual(sum(len(i['files']) for i in a['workflows']), 10)
        self.assertEqual(a['content_sha256'], workflows.digest(workflows.encoded(a['workflows'])))

    def test_manifest_refusals(self):
        path = self.root / workflows.MANIFEST
        original = json.loads(path.read_bytes())
        for mutate in (
            lambda d: d['skills'].append(d['skills'][0]),
            lambda d: d['skills'][0]['resources'].append('../../private.md'),
            lambda d: d['skills'][0]['resources'].append('/private.md'),
            lambda d: d['skills'][0]['resources'].append('references/hidden.py'),
            lambda d: d.update(bundle_version='99.0.0'),
            lambda d: d.update(extra='unexpected'),
        ):
            d = copy.deepcopy(original)
            mutate(d)
            path.write_text(json.dumps(d))
            with self.subTest(d=d), self.assertRaises(ValueError):
                workflows.build(self.root, SHA)

    def test_missing_oversized_symlink_and_link(self):
        path = self.root / 'sdk/skills/sco-debug-plugin/SKILL.md'
        original = path.read_bytes()
        for data in (b'x' * (workflows.MAX_FILE + 1), original + b'\n[private](../../private.md)\n'):
            path.write_bytes(data)
            with self.assertRaises(ValueError):
                workflows.build(self.root, SHA)
        path.unlink()
        with self.assertRaises(ValueError):
            workflows.build(self.root, SHA)
        target = self.root / 'outside.md'
        target.write_bytes(original)
        try:
            path.symlink_to(target)
        except OSError:
            self.skipTest('symlink creation unavailable')
        with self.assertRaisesRegex(ValueError, 'Symlink'):
            workflows.build(self.root, SHA)

    def test_unshipped_profile_and_destination_collision(self):
        _, declarations = workflows.manifest_files((self.root / workflows.MANIFEST).read_bytes())
        names = [workflows.MANIFEST] + [n for _, files in declarations for n in files]
        paths = {n: n for n in names}
        del paths[names[-1]]
        with self.assertRaises(ValueError):
            workflows.build(self.root, SHA, paths, {})
        paths[names[-1]] = paths[names[0]]
        with self.assertRaises(ValueError):
            workflows.build(self.root, SHA, paths, {})


class ArchiveAndMCP(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.manifest = pkg.load_manifest()
        cls.temp = tempfile.TemporaryDirectory()
        cls.root = Path(cls.temp.name)
        cls.index = workflows.build(ROOT, SHA)
        cls.catalogue = json.loads((ROOT / 'tools/sco-core-mcp/fixtures/catalogue.json').read_bytes())
        cls.catalogue.update(sdk_version=cls.index['sdk_version'], source_sha=SHA)
        cls.cat = cls.root / 'catalogue.json'
        cls.cat.write_text(json.dumps(cls.catalogue))
        cls.index_path = cls.root / 'sdk-workflows.json'
        cls.index_path.write_bytes(workflows.encoded(cls.index))

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_packaged_hashes_composition_and_round_trip(self):
        # Exercise both archive layouts; legacy adds the real catalogue and guides.
        for profile in ('core', 'core-source', 'legacy'):
            with self.subTest(profile=profile):
                top, entries = pkg.archive_entries(self.manifest, profile)
                index = json.loads(entries['sdk-workflows.json'][0])
                self.assertEqual(index['source_sha'], pkg.source_commit())
                self.assertEqual(len(index['workflows']), 6)
                inventory = json.loads(entries['ATTRIBUTIONS.json'][0])['files']
                by_name = {r['path']: r for r in inventory}
                self.assertEqual(by_name['sdk-workflows.json']['status'], 'needs-review')
                for item in index['workflows']:
                    for record in item['files']:
                        self.assertEqual(record['text'].encode(), entries[record['path']][0])
                        self.assertEqual(record['sha256'], workflows.digest(entries[record['path']][0]))
                        self.assertEqual(by_name[record['path']]['status'], 'needs-review')
                raw = pkg.archive_bytes(top, entries)
                with zipfile.ZipFile(io.BytesIO(raw)) as archive:
                    for name, (data, _) in entries.items():
                        self.assertEqual(archive.read(top + '/' + name), data)
                sums = entries['SHA256SUMS'][0].decode()
                self.assertIn(workflows.digest(entries['sdk-workflows.json'][0]) + '  sdk-workflows.json\n', sums)
        overlay = pkg.select(self.manifest, 'game-sc')
        self.assertFalse(any(c['source'].startswith('sdk/skills/') for c in overlay))
        self.assertNotIn('sdk-workflows.json', pkg.archive_entries(self.manifest, 'game-sc')[1])

    def client(self, **kw):
        c = client_module.Client(self.cat, **kw)
        self.addCleanup(c.close)
        self.assertIn('result', c.initialize())
        return c

    def test_minimal_and_default_discovery_stdio(self):
        for extended in (False, True):
            c = self.client(extended=extended)
            names = [t['name'] for t in c.request('tools/list')['result']['tools']]
            expected = client_module.EXTENDED_ORDER if extended else client_module.ORIGINAL_ORDER
            self.assertEqual(names, expected)
            if not extended:
                self.assertIn('error', c.call('list_workflows'))
        c = self.client(extended=True, extra=('--workflow-tools',))
        tools = c.request('tools/list')['result']['tools']
        self.assertEqual([t['name'] for t in tools], client_module.EXTENDED_ORDER)
        self.assertTrue(all(t['annotations']['readOnlyHint'] for t in tools))
        listed = c.call('list_workflows')['result']['structuredContent']
        self.assertEqual(len(listed['workflows']), 6)
        for item in listed['workflows']:
            response = c.call('get_workflow', {'id': item['id']})['result']['structuredContent']
            self.assertEqual(response['file']['path'], item['entry'])
            self.assertEqual(response['workflow_provenance']['source_sha'], SHA)
            for resource in response['resources']:
                file = c.call('get_workflow', {'id': item['id'], 'path': resource['path']})['result']['structuredContent']['file']
                self.assertEqual(workflows.digest(file['text'].encode()), resource['sha256'])
        for args in ({'id': 'missing'}, {'id': listed['workflows'][0]['id'], 'path': '../../secret'},
                     {'id': listed['workflows'][0]['id'], 'path': listed['workflows'][1]['entry']}):
            self.assertTrue(c.call('get_workflow', args)['result']['isError'])

    def test_startup_refuses_invalid_index(self):
        def corrupt(d):
            d['workflows'][0]['files'][0]['text'] += 'tampered'
        def duplicate(d):
            d['workflows'][0]['files'].append(d['workflows'][0]['files'][0])
            d['content_sha256'] = workflows.digest(workflows.encoded(d['workflows']))
        for mutation in (lambda d: d.update(source_sha='b' * 40), lambda d: d.update(sdk_version='2.3.0'),
                         corrupt, duplicate, lambda d: d.update(content_sha256='0' * 64)):
            d = copy.deepcopy(self.index)
            mutation(d)
            path = self.root / 'invalid.json'
            path.write_bytes(workflows.encoded(d))
            r = subprocess.run([sys.executable, str(client_module.SERVER), '--catalogue', str(self.cat),
                                '--workflow-tools', '--workflows', str(path)], input=b'', capture_output=True, timeout=10)
            self.assertNotEqual(r.returncode, 0)
            self.assertEqual(r.stdout, b'')
            self.assertIn(b'workflows refused', r.stderr)
        # The explicitly selected index also works without the old opt-in flag.
        c = self.client(extended=True, extra=('--workflows', str(self.index_path)))
        self.assertEqual(len(c.call('list_workflows')['result']['structuredContent']['workflows']), 6)



if __name__ == '__main__':
    unittest.main(verbosity=2)
