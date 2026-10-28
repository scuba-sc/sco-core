#!/usr/bin/env python3
"""Offline verification of the unsigned candidate inventory."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
import release_verify as verify


class ReleaseVerifyTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.payload = self.root / "payload"
        self.payload.mkdir()
        (self.root / "sdk").mkdir()
        (self.root / "sdk/VERSION").write_text("9.9.9\n")
        (self.root / "sdk/components.json").write_text('{}\n')
        (self.root / "include").mkdir()
        (self.root / "include/sc_game_pack.h").write_text('#define SCO_GAME_PACK_VERSION "1.2.3"\n')
        (self.root / ".github/workflows").mkdir(parents=True)
        (self.root / verify.WORKFLOW).write_text('name: candidate\n')
        self.git('init', '-q')
        self.git('add', 'sdk', 'include', '.github')
        self.git('-c', 'user.name=test', '-c', 'user.email=test@example.invalid', 'commit', '-qm', 'fixture')
        self.sha = self.git('rev-parse', 'HEAD').decode().strip()
        self.tag = 'sdk-v9.9.9'
        self.expected = verify.expected_payload(self.root, self.sha, self.tag)
        for name in self.expected:
            if name != 'SHA256SUMS':
                (self.payload / name).write_bytes(name.encode())
        sums = ''.join(f'{hashlib.sha256((self.payload / name).read_bytes()).hexdigest()}  {name}\n'
                       for name in sorted(self.expected) if name != 'SHA256SUMS')
        (self.payload / 'SHA256SUMS').write_text(sums)
        self.record = {'schema_version': 3, 'tag': self.tag,
                       'source': {'repo': 'example/sdk', 'sha': self.sha, 'dependencies': []},
                       'components_manifest_sha256': hashlib.sha256((self.root / 'sdk/components.json').read_bytes()).hexdigest(),
                       'builder': {'workflow_path': verify.WORKFLOW, 'workflow_sha': self.sha, 'run_id': 123, 'run_attempt': 1},
                       'profiles': verify.PROFILES.copy(),
                       'artifacts': [{'name': n, 'role': role, 'size': (self.payload / n).stat().st_size,
                                      'sha256': hashlib.sha256((self.payload / n).read_bytes()).hexdigest(), 'artifact_id': 456}
                                     for n, role in sorted(self.expected.items())]}
        self.manifest = self.root / 'release.json'

    def git(self, *args):
        return subprocess.check_output(['git', '-C', str(self.root), *args], stderr=subprocess.PIPE)

    def run_verify(self, **changes):
        self.manifest.write_text(json.dumps(self.record))
        args = {'manifest': self.manifest, 'payload-dir': self.payload, 'tag': self.tag,
                'source-sha': self.sha, 'repo': 'example/sdk', 'run-id': 123, 'artifact-id': 456}
        args.update(changes)
        command = [sys.executable, str(Path(verify.__file__)), 'verify']
        for name, value in args.items():
            command.extend(['--' + name, str(value)])
        return subprocess.run(command, cwd=self.root, capture_output=True, text=True)

    def assert_refused(self, result, message):
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn(message, result.stderr)

    def test_valid_unsigned_inventory(self):
        result = self.run_verify()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse((self.root / 'release.json.sig').exists())

    def test_identity_refusals(self):
        for key, value, message in [('tag', 'sdk-v9.9.9-rc.1', 'tag mismatch'),
                                    ('run-id', 999, 'run id mismatch'), ('artifact-id', 999, 'artifact id mismatch'),
                                    ('repo', 'other/repo', 'repository mismatch')]:
            with self.subTest(key=key): self.assert_refused(self.run_verify(**{key: value}), message)
        self.git('-c', 'user.name=test', '-c', 'user.email=test@example.invalid', 'commit', '--allow-empty', '-qm', 'other')
        other = self.git('rev-parse', 'HEAD').decode().strip()
        self.assert_refused(self.run_verify(**{'source-sha': other}), 'tag source SHA differs')

    def test_payload_tampering_and_members(self):
        name = next(n for n in self.expected if n.endswith('.zip'))
        original = (self.payload / name).read_bytes()
        (self.payload / name).write_bytes(b'x' * len(original))
        self.assert_refused(self.run_verify(), 'artifact SHA256 mismatch')
        (self.payload / name).unlink()
        self.assert_refused(self.run_verify(), 'missing payload file')
        (self.payload / name).write_bytes(original)
        (self.payload / 'extra').write_text('x')
        self.assert_refused(self.run_verify(), 'extra payload file')
        (self.payload / 'extra').unlink()
        (self.payload / name).unlink()
        (self.payload / name).symlink_to(self.root / 'sdk/VERSION')
        self.assert_refused(self.run_verify(), 'not a regular file')

    def test_checksums_are_independently_checked(self):
        p = self.payload / 'SHA256SUMS'
        p.write_text('wrong\n')
        item = next(a for a in self.record['artifacts'] if a['name'] == p.name)
        item.update(size=p.stat().st_size, sha256=hashlib.sha256(p.read_bytes()).hexdigest())
        self.assert_refused(self.run_verify(), 'SHA256SUMS mismatch')

    def test_profile_and_artifact_inventory_refused(self):
        self.record['profiles'] = ['core']
        self.assert_refused(self.run_verify(), 'invalid inventory fields')
        self.record['profiles'] = verify.PROFILES.copy()
        self.record['artifacts'].pop()
        self.assert_refused(self.run_verify(), 'expected SDK profiles')
        self.record['artifacts'].append(dict(self.record['artifacts'][0]))
        self.assert_refused(self.run_verify(), 'duplicate artifact filename')

    def test_invalid_builder_or_source(self):
        self.record['builder']['workflow_path'] = '.github/workflows/other.yml'
        self.assert_refused(self.run_verify(), 'invalid builder fields')
        self.record['builder']['workflow_path'] = verify.WORKFLOW
        self.record['components_manifest_sha256'] = 'a' * 64
        self.assert_refused(self.run_verify(), 'components manifest SHA mismatch')
        self.record['components_manifest_sha256'] = hashlib.sha256((self.root / 'sdk/components.json').read_bytes()).hexdigest()
        self.record['source']['dependencies'] = [{'path': '../outside', 'sha': 'a' * 40}]
        self.assert_refused(self.run_verify(), 'unsafe or duplicate dependency')

    def test_malformed_records_and_duplicate_json_keys_refused(self):
        for change in ('extra', 'boolean', 'attempt', 'role'):
            with self.subTest(change=change):
                record = json.loads(json.dumps(self.record))
                if change == 'extra': record['approved_by'] = 'someone'
                if change == 'boolean': record['builder']['run_id'] = True
                if change == 'attempt': record['builder']['run_attempt'] = 2
                if change == 'role': record['artifacts'][0]['role'] = 'other'
                old, self.record = self.record, record
                self.assertEqual(self.run_verify().returncode, 1)
                self.record = old
        self.manifest.write_text('{"schema_version":3,"schema_version":3}')
        result = subprocess.run([sys.executable, str(Path(verify.__file__)), 'verify', '--manifest', str(self.manifest),
                                 '--payload-dir', str(self.payload), '--tag', self.tag, '--source-sha', self.sha,
                                 '--repo', 'example/sdk', '--run-id', '123', '--artifact-id', '456'],
                                cwd=self.root, capture_output=True, text=True)
        self.assert_refused(result, 'duplicate JSON key')

    def test_recursive_dependency_mismatch_refused(self):
        child = self.root / 'external/library'
        child.mkdir(parents=True)
        subprocess.run(['git', 'init', '-q', str(child)], check=True)
        subprocess.run(['git', '-C', str(child), '-c', 'user.name=test', '-c', 'user.email=test@example.invalid',
                        'commit', '--allow-empty', '-qm', 'dependency'], check=True)
        child_sha = subprocess.check_output(['git', '-C', str(child), 'rev-parse', 'HEAD']).decode().strip()
        self.git('update-index', '--add', '--cacheinfo', '160000,' + child_sha + ',external/library')
        self.git('-c', 'user.name=test', '-c', 'user.email=test@example.invalid', 'commit', '-qm', 'pin')
        self.sha = self.git('rev-parse', 'HEAD').decode().strip()
        self.record['source']['sha'] = self.sha
        self.record['builder']['workflow_sha'] = self.sha
        self.assert_refused(self.run_verify(), 'recursive source dependencies mismatch')
        self.record['source']['dependencies'] = [{'path': 'external/library', 'sha': child_sha}]
        result = self.run_verify()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.record['source']['dependencies'][0]['sha'] = 'a' * 40
        self.assert_refused(self.run_verify(), 'recursive source dependencies mismatch')

    def test_inventory_command_is_sorted_and_excludes_itself(self):
        p = self.payload / 'SHA256SUMS'
        previous = p.read_bytes()
        result = subprocess.run([sys.executable, str(Path(verify.__file__)), 'inventory', '--payload-dir', str(self.payload), '--out', str(p)], capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(p.read_bytes(), previous)

if __name__ == '__main__':
    unittest.main()
