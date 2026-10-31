#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Workflow allowlist, exact archive bytes and real stdio discovery; stdlib only."""
import copy
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
        shutil.copytree(ROOT / 'tools/sco-core-mcp', self.root / 'tools/sco-core-mcp')

    def test_mcp_tool_argument_schema_and_version_drift(self):
        server = self.root / 'tools/sco-core-mcp/server.py'
        original = server.read_text()
        for before, after in (
            ('tool("get_symbol",', 'tool("get_symbol_renamed",'),
            ('DEV_TIMEOUT = 600', 'del TOOL_INDEX["get_symbol"]\nDEV_TIMEOUT = 600'),
            ('DEV_TIMEOUT = 600\n', 'del TOOL_INDEX["generate_plugin_scaffold"]["inputSchema"]["properties"]["with_menu"]\nDEV_TIMEOUT = 600\n'),
            ('SYMBOL = {"symbol_or_id":', 'SYMBOL = {"renamed_symbol":'),
            ('"with_menu": {"type": "boolean"', '"with_menu": {"type": "string"'),
            ('PROTOCOL_VERSION = "2025-11-25"', 'PROTOCOL_VERSION = "2099-01-01"'),
            ('SERVER_VERSION = "1.0.0"', 'SERVER_VERSION = "9.0.0"'),
        ):
            with self.subTest(before=before):
                self.assertIn(before, original)
                server.write_text(original.replace(before, after))
                with self.assertRaisesRegex(ValueError, 'MCP'):
                    workflows.build(self.root, SHA)
        server.write_text(original)

    def test_generated_contract_check_and_repair_preserve_guidance(self):
        path = self.root / 'sdk/skills/sco-build-check-package/references/inputs.md'
        original = path.read_text()
        self.assertIn('<!-- sdk-workflows:mcp:start -->', original)
        path.write_text(original.replace('| `get_symbol` |', '| `stale_symbol` |'))
        with self.assertRaisesRegex(ValueError, 'generated MCP'):
            workflows.build(self.root, SHA)
        workflows.generate(self.root)
        self.assertEqual(path.read_text(), original)
        workflows.build(self.root, SHA)

    def test_handwritten_argument_reference_drift(self):
        path = self.root / 'sdk/skills/sco-build-check-package/references/build.md'
        original = path.read_text()
        self.assertIn('generate_plugin_scaffold(id, lang, with_menu, services)', original)
        path.write_text(original.replace('with_menu, services)', 'renamed_menu, services)'))
        with self.assertRaisesRegex(ValueError, 'Unknown MCP argument'):
            workflows.build(self.root, SHA)

    def test_generated_markers_must_be_unique_and_ordered(self):
        path = self.root / workflows.BLOCK_PATH
        original = path.read_text()
        for text in (original + '\n' + workflows.BLOCK_END,
                     workflows.BLOCK_END + '\n' + original):
            path.write_text(text)
            with self.assertRaisesRegex(ValueError, 'MCP markers'):
                workflows.build(self.root, SHA)
            with self.assertRaisesRegex(ValueError, 'MCP markers'):
                workflows.generate(self.root)
            self.assertEqual(path.read_text(), text)

    def test_generate_refuses_missing_or_ambiguous_insertion_point(self):
        path = self.root / workflows.BLOCK_PATH
        text = path.read_text()
        start, end = workflows.block_extent(text)
        text = text[:start] + text[end:]
        manifest_path = self.root / workflows.MANIFEST
        manifest = manifest_path.read_bytes()
        for candidate in (text.replace('## Receipt template', '## Other'),
                          text + '\n## Receipt template\n'):
            path.write_text(candidate)
            with self.assertRaisesRegex(ValueError, 'insertion point'):
                workflows.generate(self.root)
            self.assertEqual(path.read_text(), candidate)
            self.assertEqual(manifest_path.read_bytes(), manifest)

    def test_non_string_resource_is_rejected(self):
        data = json.loads((self.root / workflows.MANIFEST).read_bytes())
        data['skills'][0]['resources'] = [{}]
        with self.assertRaisesRegex(ValueError, 'workflow path'):
            workflows.manifest_files(workflows.encoded(data))

    def test_generate_preserves_crlf_outside_contract(self):
        path = self.root / workflows.BLOCK_PATH
        lf = path.read_bytes().replace(b'\r\n', b'\n')
        # Exercise both LF and CRLF checkout inputs without turning CRLF into
        # CRCRLF on Windows. The generated block and surrounding text must match.
        for checkout in (lf, lf.replace(b'\n', b'\r\n')):
            with self.subTest(line_ending='CRLF' if b'\r\n' in checkout else 'LF'):
                path.write_bytes(checkout)
                workflows.generate(self.root)
                self.assertEqual(path.read_bytes(), checkout)
                workflows.build(self.root, SHA)

    def test_generate_refuses_symlink_without_writing_target(self):
        path = self.root / workflows.BLOCK_PATH
        target = self.root / 'outside.md'
        original = path.read_bytes().replace(b'| `get_symbol` |', b'| `stale_symbol` |')
        target.write_bytes(original)
        path.unlink()
        path.symlink_to(target)
        manifest = (self.root / workflows.MANIFEST).read_bytes()
        with self.assertRaisesRegex(ValueError, 'Symlink'):
            workflows.generate(self.root)
        self.assertEqual(target.read_bytes(), original)
        self.assertEqual((self.root / workflows.MANIFEST).read_bytes(), manifest)

    def test_check_rejects_output_without_writing(self):
        output = self.root / 'untouched'
        output.write_bytes(b'original')
        result = subprocess.run([sys.executable, str(ROOT / 'tools/sdk_workflows.py'),
                                 '--check', '--out', str(output)], capture_output=True, timeout=10)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(output.read_bytes(), b'original')

    def test_unbound_handwritten_tool_reference_fails(self):
        path = self.root / 'sdk/skills/sco-debug-plugin/SKILL.md'
        path.write_text(path.read_text() + '\nUse `get_removed_tool`.\n')
        with self.assertRaisesRegex(ValueError, 'Unbound MCP'):
            workflows.build(self.root, SHA)

    def test_determinism_and_progressive_files(self):
        a = workflows.build(self.root, SHA)
        self.assertEqual(workflows.encoded(a), workflows.encoded(workflows.build(self.root, SHA)))
        self.assertEqual(len(a['workflows']), 6)
        self.assertEqual(sum(len(i['files']) for i in a['workflows']), 11)
        self.assertEqual(a['content_sha256'], workflows.digest(workflows.encoded(a['workflows'])))

    def test_gap_report_index_tracks_selected_sdk_not_bundle_baseline(self):
        # Old, current, and unknown future SDK versions retain the same report schema;
        # the consumer supplies actual identities or marks them unknown in the draft.
        for version in ('2.3.0', '2.4.0', '9.7.1'):
            with self.subTest(version=version):
                (self.root / 'sdk/VERSION').write_text(version + '\n')
                index = workflows.build(self.root, SHA)
                self.assertEqual(index['sdk_version'], version)
                for workflow in index['workflows']:
                    entry = next(f for f in workflow['files'] if f['path'] == workflow['entry'])
                    self.assertIn('references/inputs.md', entry['text'])
                gap = next(w for w in index['workflows'] if w['id'] == 'sco-report-sdk-gap')
                template = next(f for f in gap['files'] if f['path'].endswith('/references/report-template.md'))
                self.assertIn('sco-sdk-gap-report/v1', template['text'])
                self.assertIn('unknown', template['text'])
                self.assertIn('negotiated tool/schema', template['text'])

    def test_crlf_checkout_preserves_source_and_packaged_bytes(self):
        originals = {}
        for path in (self.root / 'sdk/skills').rglob('*'):
            if path.is_file():
                data = path.read_bytes().replace(b'\r\n', b'\n').replace(b'\n', b'\r\n')
                path.write_bytes(data)
                originals[path.relative_to(self.root).as_posix()] = data
        source = workflows.build(self.root, SHA)
        paths = {name: name.removeprefix('sdk/') for name in originals}
        dirs = {'sdk/skills': 'skills'}
        packaged = {paths[name]: (pkg.fix_links(name, paths[name], data, paths, dirs), False)
                    for name, data in originals.items()}
        archive = workflows.build(self.root, SHA, paths, packaged)
        for mode, index in (('source', source), ('archive', archive)):
            with self.subTest(mode=mode):
                self.assertEqual(len(index['workflows']), 6)
                self.assertEqual(sum(len(item['files']) for item in index['workflows']), 11)
                for item in index['workflows']:
                    for record in item['files']:
                        raw = originals[record['path']] if mode == 'source' else packaged[record['path']][0]
                        self.assertIn(b'\r\n', raw)
                        self.assertEqual(record['text'].encode('utf-8'), raw)
                        self.assertEqual(record['sha256'], workflows.digest(raw))
                        self.assertNotEqual(record['sha256'], workflows.digest(raw.replace(b'\r\n', b'\n')))
                self.assertEqual(index['content_sha256'], workflows.digest(workflows.encoded(index['workflows'])))
        catalogue = json.loads((ROOT / 'tools/sco-core-mcp/fixtures/catalogue.json').read_bytes())
        catalogue.update(sdk_version=source['sdk_version'], source_sha=SHA)
        catalogue_path = self.root / 'catalogue.json'
        catalogue_path.write_text(json.dumps(catalogue))
        index_path = self.root / 'sdk-workflows.json'
        index_path.write_bytes(workflows.encoded(source))
        client = client_module.Client(catalogue_path, extended=True, extra=('--workflows', index_path))
        self.addCleanup(client.close)
        self.assertIn('result', client.initialize())
        for item in source['workflows']:
            for record in item['files']:
                response = client.call('get_workflow', {'id': item['id'], 'path': record['path']})
                received = response['result']['structuredContent']['file']
                self.assertEqual(received, record)
                self.assertEqual(received['text'].encode('utf-8'), originals[record['path']])
        for name, data in originals.items():
            self.assertEqual((self.root / name).read_bytes(), data)

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
                    if profile == 'legacy':
                        # The kit snapshot must describe RC2 without presenting RC1 as its current release.
                        readme = archive.read(top + '/README.md').decode()
                        lua = archive.read(top + '/docs/lua.md').decode()
                        mcp = archive.read(top + '/tools/sco-core-mcp/README.md').decode()
                        guide_index = json.loads(archive.read(top + '/sdk-guides.json'))
                        self.assertIn('This guide covers RC2 workflows', readme)
                        self.assertIn('sdk-v2.4.0-rc.2', readme)
                        self.assertIn('This guide covers RC2 workflows', lua)
                        self.assertIn('sdk-v2.4.0-rc.2', mcp)
                        for text in (readme, lua, mcp):
                            self.assertNotIn('latest release', ' '.join(text.lower().split()))
                        sections = [s for s in guide_index['sections']
                                    if s['doc'] == 'docs/getting-started.md']
                        self.assertTrue(any('RC2 release page' in s['text'] for s in sections))
                        self.assertFalse(any('latest release' in ' '.join(s['text'].lower().split())
                                             for s in sections))
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

    def test_packaged_server_contract_and_workflows(self):
        top, entries = pkg.archive_entries(self.manifest, 'legacy')
        archive = self.root / 'sdk.zip'
        archive.write_bytes(pkg.archive_bytes(top, entries))
        extracted = self.root / 'extracted'
        with zipfile.ZipFile(archive) as zipped:
            zipped.extractall(extracted)
        kit = extracted / top
        old = client_module.SERVER
        client_module.SERVER = kit / 'tools/sco-core-mcp/server.py'
        try:
            c = client_module.Client(kit / 'sdk-catalogue.json', repo_root=kit, extended=True,
                                     extra=('--project-root', str(self.root)))
            self.addCleanup(c.close)
            init = c.initialize()['result']
            manifest = json.loads((kit / 'skills/manifest.json').read_bytes())
            self.assertEqual(init['protocolVersion'], manifest['mcp']['protocol'])
            self.assertEqual(init['serverInfo']['version'], manifest['mcp']['server_version'])
            tools = {t['name']: t['inputSchema'] for t in c.request('tools/list')['result']['tools']}
            self.assertLessEqual(set(manifest['mcp']['tools']), set(tools))
            for name, expected_hash in manifest['mcp']['tools'].items():
                self.assertEqual(workflows.digest(workflows.encoded(tools[name])), expected_hash)
            listed = c.call('list_workflows')['result']['structuredContent']['workflows']
            self.assertEqual(len(listed), 6)
            for item in listed:
                response = c.call('get_workflow', {'id': item['id']})['result']['structuredContent']
                for record in [response['file']] + [
                    c.call('get_workflow', {'id': item['id'], 'path': r['path']})['result']['structuredContent']['file']
                    for r in response['resources']]:
                    self.assertEqual(record['text'].encode(), (kit / record['path']).read_bytes())
            self.assertNotIn('error', c.call('list_services'))
        finally:
            client_module.SERVER = old

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
