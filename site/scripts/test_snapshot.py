#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Targeted release-input snapshot tests; no release, signing or publication."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'site/scripts/snapshot.py'


class SnapshotTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='reference-test-', dir=os.environ.get('TMPDIR'))
        cls.work = Path(cls.temp.name)
        cls.sha = subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip()
        cls.catalogue = ROOT / 'dist/sdk-catalogue.json'
        spec = importlib.util.spec_from_file_location('snapshot_under_test', SCRIPT)
        assert spec is not None and spec.loader is not None
        cls.tool = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.tool)
        cls.archives = {}
        # Fixture packages are built from the same immutable Git tree as the snapshot,
        # not from a possibly dirty worktree. The existing package tool is exercised.
        with cls.tool.source_tree(ROOT, cls.sha) as source:
            package = cls.tool.package_module(source, cls.sha)
            manifest = package.load_manifest(str(source / 'sdk/components.json'), str(source))
            for profile in ('core', 'game-sc'):
                top, entries = package.archive_entries(manifest, profile)
                archive = cls.work / (top + '.zip')
                archive.write_bytes(package.archive_bytes(top, entries))
                cls.archives[profile] = archive

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_bound_deterministic_snapshot_and_verifier(self):
        first = self.tool.create(ROOT, self.sha, self.catalogue, self.archives, self.work / 'first', 'candidate')
        second = self.tool.create(ROOT, self.sha, self.catalogue, dict(reversed(list(self.archives.items()))),
                                  self.work / 'second', 'candidate')
        self.assertEqual(first.name, self.sha)
        self.assertEqual((first / 'inventory.json').read_bytes(), (second / 'inventory.json').read_bytes())
        inventory = json.loads((first / 'inventory.json').read_bytes())
        catalogue = json.loads(self.catalogue.read_bytes())
        self.assertEqual(inventory['coordinates']['sdk_version'], catalogue['sdk_version'])
        self.assertEqual(inventory['publication_status'], 'not-approved')
        expected_examples = {p for record in catalogue['records'] for p in record['examples']}
        self.assertEqual({e['source'] for e in inventory['examples']}, expected_examples)
        self.assertTrue(all(e['shipped_in'] for e in inventory['examples']))
        digest = hashlib.sha256((first / 'inventory.json').read_bytes()).hexdigest()
        self.tool.verify(ROOT, first, self.archives, digest)

    def test_cli_available(self):
        result = subprocess.run(['python3', str(SCRIPT), '--help'], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('verify', result.stdout)

    def catalogue_variant(self, label, change):
        value = json.loads(self.catalogue.read_bytes())
        change(value)
        path = self.work / (label + '.json')
        path.write_text(json.dumps(value))
        return path

    def test_git_export_cannot_substitute_different_blob_bytes(self):
        import io
        import tarfile
        from unittest import mock
        original = subprocess.check_output
        exported = original(['git', '-C', str(ROOT), 'archive', '--format=tar', self.sha])
        changed = io.BytesIO()
        with tarfile.open(fileobj=io.BytesIO(exported)) as old, tarfile.open(fileobj=changed, mode='w') as new:
            for member in old:
                if member.isfile():
                    stream = old.extractfile(member)
                    assert stream is not None
                    data = stream.read()
                    if member.name == 'sdk/examples/hello/hello.c':
                        data += b'\n/* substituted export */\n'
                        member.size = len(data)
                    new.addfile(member, io.BytesIO(data))
                else:
                    new.addfile(member)
        def output(command, **kwargs):
            return changed.getvalue() if 'archive' in command else original(command, **kwargs)
        with mock.patch.object(self.tool.subprocess, 'check_output', side_effect=output):
            with self.assertRaisesRegex(ValueError, 'Git blob mismatch'):
                with self.tool.source_tree(ROOT, self.sha):
                    pass

    def test_reject_floating_source(self):
        with self.assertRaisesRegex(ValueError, 'full lowercase'):
            self.tool.create(ROOT, 'main', self.catalogue, self.archives, self.work / 'floating')

    def test_reject_catalogue_source_version_signature_and_missing_example(self):
        changes = {
            'source': lambda c: c.update(source_sha='0' * 40),
            'version': lambda c: c.update(sdk_version='99.0.0'),
            'signature': lambda c: c['records'][0].update(signature='void (*)(void)'),
            'example': lambda c: c['records'][0].update(examples=['sdk/examples/absent/no.c']),
            'link': lambda c: c.update(note='https://github.com/scubamount/sco-core/blob/main/include/sco_api.h'),
            'member': lambda c: c['tables']['sco_api']['members'].append('nonexistent'),
            'table-version': lambda c: c['tables']['sco_api'].update(version=1),
        }
        for label, change in changes.items():
            with self.subTest(label=label), self.assertRaises(ValueError):
                self.tool.create(ROOT, self.sha, self.catalogue_variant(label, change),
                                 self.archives, self.work / ('bad-' + label))

    def test_reject_archive_example_metadata_and_unlisted_member(self):
        import zipfile
        for label, change in (
            ('example', lambda n, d: d + b'\n/* different input */\n' if n.endswith('/examples/hello/hello.c') else d),
            ('metadata', lambda n, d: d.replace(self.sha.encode(), b'0' * 40) if n.endswith('/COMPATIBILITY.json') else d),
        ):
            path = self.work / ('mutated-' + label + '.zip')
            with zipfile.ZipFile(self.archives['core']) as old, zipfile.ZipFile(path, 'w') as new:
                for info in old.infolist():
                    new.writestr(info, change(info.filename, old.read(info)))
            with self.subTest(label=label), self.assertRaisesRegex(ValueError, 'archive inputs/metadata'):
                self.tool.create(ROOT, self.sha, self.catalogue, {**self.archives, 'core': path}, self.work / label)
        path = self.work / 'extra.zip'
        shutil.copyfile(self.archives['core'], path)
        with zipfile.ZipFile(path, 'a') as archive:
            top = archive.namelist()[0].split('/')[0]
            archive.writestr(top + '/unlisted.txt', b'unlisted')
        with self.assertRaisesRegex(ValueError, 'archive inputs/metadata'):
            self.tool.create(ROOT, self.sha, self.catalogue, {'core': path}, self.work / 'extra')

    def test_reject_fresh_package_from_dirty_inputs_despite_matching_source_label(self):
        with self.tool.source_tree(ROOT, self.sha) as source:
            package = self.tool.package_module(source, self.sha)
            manifest = package.load_manifest(str(source / 'sdk/components.json'), str(source))
            hello = source / 'sdk/examples/hello/hello.c'
            hello.write_bytes(hello.read_bytes() + b'\n/* uncommitted package input */\n')
            top, entries = package.archive_entries(manifest, 'core')
            forged = self.work / 'fresh-dirty.zip'
            forged.write_bytes(package.archive_bytes(top, entries))
        # All sums/SBOM/compatibility were freshly produced by package.py. Its
        # commit label alone cannot establish that it packaged committed bytes.
        with self.assertRaisesRegex(ValueError, 'archive inputs/metadata'):
            self.tool.create(ROOT, self.sha, self.catalogue, {'core': forged}, self.work / 'fresh-dirty')

    def test_reject_snapshot_symlink(self):
        target = self.tool.create(ROOT, self.sha, self.catalogue, self.archives, self.work / 'symlink')
        pin = hashlib.sha256((target / 'inventory.json').read_bytes()).hexdigest()
        example = target / 'examples/sdk/examples/hello/hello.c'
        outside = self.work / 'linked-example.c'
        outside.write_bytes(example.read_bytes())
        example.unlink()
        example.symlink_to(outside)
        with self.assertRaisesRegex(ValueError, 'symlink'):
            self.tool.verify(ROOT, target, self.archives, pin)

    def test_verifier_rejects_mutation_extra_file_and_rehashed_inventory(self):
        target = self.tool.create(ROOT, self.sha, self.catalogue, self.archives, self.work / 'mutations')
        inventory_path = target / 'inventory.json'
        pinned = hashlib.sha256(inventory_path.read_bytes()).hexdigest()
        example = target / 'examples/sdk/examples/hello/hello.c'
        original = example.read_bytes()
        example.write_bytes(original + b'\n/* changed */\n')
        with self.assertRaisesRegex(ValueError, 'file set/bytes'):
            self.tool.verify(ROOT, target, self.archives, pinned)
        example.write_bytes(original)
        extra = target / 'extra.txt'
        extra.write_text('not inventoried')
        with self.assertRaisesRegex(ValueError, 'file set/bytes'):
            self.tool.verify(ROOT, target, self.archives, pinned)
        extra.unlink()
        inventory = json.loads(inventory_path.read_bytes())
        inventory['publication_status'] = 'approved'
        inventory_path.write_bytes(self.tool.encoded(inventory))
        with self.assertRaisesRegex(ValueError, 'pinned inventory hash'):
            self.tool.verify(ROOT, target, self.archives, pinned)
        attacker_hash = hashlib.sha256(inventory_path.read_bytes()).hexdigest()
        with self.assertRaisesRegex(ValueError, 'inventory mismatch'):
            self.tool.verify(ROOT, target, self.archives, attacker_hash)

    def test_no_regeneration_or_preview_candidate_collision(self):
        out = self.work / 'channels'
        preview = self.tool.create(ROOT, self.sha, self.catalogue, self.archives, out)
        candidate = self.tool.create(ROOT, self.sha, self.catalogue, self.archives, out, 'candidate')
        self.assertIn('previews', preview.parts)
        self.assertIn('candidates', candidate.parts)
        before = (candidate / 'inventory.json').read_bytes()
        with self.assertRaisesRegex(ValueError, 'refusing regeneration'):
            self.tool.create(ROOT, self.sha, self.catalogue, self.archives, out, 'candidate')
        self.assertEqual(before, (candidate / 'inventory.json').read_bytes())

    def test_reject_wrong_selected_archive_hash(self):
        import zipfile
        # A byte-equivalent recompressed core is still not the overlay's exact base.
        path = self.work / 'recompressed.zip'
        with zipfile.ZipFile(self.archives['core']) as old, zipfile.ZipFile(path, 'w') as new:
            for info in old.infolist():
                data = old.read(info)
                info.compress_type = zipfile.ZIP_STORED
                new.writestr(info, data)
        with self.assertRaisesRegex(ValueError, 'overlay does not bind'):
            self.tool.create(ROOT, self.sha, self.catalogue, {**self.archives, 'core': path}, self.work / 'wrong-base')

    def test_cli_create_verify_and_fail_closed(self):
        import sys
        archive_args = [arg for role, path in self.archives.items() for arg in ('--archive', role + '=' + str(path))]
        command = [sys.executable, str(SCRIPT), 'create', '--root', str(ROOT), '--source-sha', self.sha,
                   '--catalogue', str(self.catalogue), '--out', str(self.work / 'cli'), *archive_args]
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        target = lines[0].removeprefix('reference snapshot: created ')
        pin = lines[1].removeprefix('inventory sha256: ')
        print(lines[1])
        command = [sys.executable, str(SCRIPT), 'verify', '--root', str(ROOT), '--snapshot', target,
                   '--inventory-sha256', pin, *archive_args]
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        print(result.stdout.strip())
        command[command.index(pin)] = '0' * 64
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(result.returncode, 1)
        self.assertIn('pinned inventory hash mismatch', result.stderr)

    def test_archived_hello_compiles_with_existing_sdk_cmake(self):
        import zipfile
        if not shutil.which('cmake'):
            self.skipTest('cmake unavailable; archived example compilation remains owed')
        unpacked = self.work / 'unpacked'
        with zipfile.ZipFile(self.archives['core']) as archive:
            archive.extractall(unpacked)
        sdk = next(unpacked.iterdir())
        build = self.work / 'hello-build'
        for command in (
            ['cmake', '-S', str(sdk), '-B', str(build), '-DCMAKE_BUILD_TYPE=Debug'],
            ['cmake', '--build', str(build), '--target', 'hello'],
        ):
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue(any(build.rglob('*hello*.dylib')) or any(build.rglob('*hello*.so'))
                        or any(build.rglob('*hello*.dll')), 'hello binary missing')
        print('archived example: core sha256 ' + hashlib.sha256(self.archives['core'].read_bytes()).hexdigest())
        print(result.stdout.strip())


if __name__ == '__main__':
    unittest.main(verbosity=2)
