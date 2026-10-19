#!/usr/bin/env python3
"""Synthetic refusal tests for the compiler-backed SDK catalogue gate."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

import sdk_catalogue as catalogue

ROOT = Path(__file__).resolve().parents[1]
SHA = '0123456789abcdef0123456789abcdef01234567'


class FindClangTests(unittest.TestCase):
    def test_clang_env_wins(self):
        with mock.patch.dict(os.environ, {'CLANG': 'custom-clang'}):
            self.assertEqual(catalogue.find_clang(), 'custom-clang')

    def test_path_beats_install_locations(self):
        with mock.patch.dict(os.environ, {}, clear=False), mock.patch.object(
                catalogue.shutil, 'which', return_value='/on/path/clang'):
            os.environ.pop('CLANG', None)
            self.assertEqual(catalogue.find_clang(), '/on/path/clang')

    def test_non_windows_does_not_search_install_locations(self):
        with tempfile.TemporaryDirectory() as local:
            exe = Path(local) / 'Microsoft/WinGet/Packages/X.LLVM-MinGW/p/bin/clang.exe'
            exe.parent.mkdir(parents=True)
            exe.write_text('')
            env = {'LOCALAPPDATA': local}
            with mock.patch.dict(os.environ, env), mock.patch.object(
                    catalogue.shutil, 'which', return_value=None), mock.patch.object(
                    catalogue.sys, 'platform', 'linux'):
                os.environ.pop('CLANG', None)
                self.assertIsNone(catalogue.find_clang())

    def test_windows_falls_back_to_winget_llvm(self):
        with tempfile.TemporaryDirectory() as local:
            exe = Path(local) / 'Microsoft/WinGet/Packages/X.LLVM-MinGW/p/bin/clang.exe'
            exe.parent.mkdir(parents=True)
            exe.write_text('')
            env = {'LOCALAPPDATA': local, 'ProgramFiles': str(Path(local) / 'none'),
                   'ProgramFiles(x86)': str(Path(local) / 'none')}
            with mock.patch.dict(os.environ, env), mock.patch.object(
                    catalogue.shutil, 'which', return_value=None), mock.patch.object(
                    catalogue.sys, 'platform', 'win32'):
                os.environ.pop('CLANG', None)
                self.assertEqual(Path(catalogue.find_clang()), exe)


class CatalogueTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='sdk_catalogue_test_')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        paths = {'docs/contracts.md', 'sdk/VERSION'}
        paths.update(p.relative_to(ROOT).as_posix() for p in (ROOT / 'include').glob('*.h'))
        paths.update(p.relative_to(ROOT).as_posix() for p in (ROOT / 'sdk/contracts').glob('*.json'))
        for record in catalogue.load_records(ROOT):
            paths.update(record['examples'] + record['tests'])
            for mapping in record['language_mappings'].values():
                paths.update(e['path'] for e in mapping['evidence'])
        for path in paths:
            dest = self.root / path
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / path, dest)

    def record(self, symbol, change):
        for path in (self.root / 'sdk/contracts').glob('*.json'):
            data = json.loads(path.read_text())
            for record in data['records']:
                if record['symbol'] == symbol:
                    change(record)
                    path.write_text(catalogue.encoded(data))
                    return
        self.fail('test symbol not found')

    def refuses(self, reason):
        with self.assertRaisesRegex(catalogue.CatalogueError, reason):
            catalogue.validate(self.root)

    def header_change(self, name, before, after):
        path = self.root / 'include' / name
        source = path.read_text()
        self.assertEqual(source.count(before), 1)
        path.write_text(source.replace(before, after))

    def test_baseline_matches_header_count(self):
        records, extracted = catalogue.validate(self.root)
        self.assertEqual(len(records), len(extracted['methods']))
        self.assertEqual(len(records), 198)
        self.assertEqual(len(extracted['tables']), 42)
        self.assertEqual(sum(len(t['members']) for t in extracted['tables'].values()), 195)
        wave3 = {
            'sc_mining_v1': (['request', 'state'], 'tests/abi_catalogue_mining.c', 'tests/test_mining.cpp'),
            'sc_private_session_v1': (['request_host', 'request_join', 'leave', 'get_state'],
                                      'tests/abi_private_session.c', 'tests/test_net_control.cpp'),
        }
        clang = catalogue.find_clang()
        assert clang is not None, 'Clang is required for catalogue C layout evidence'
        for table, (members, c_test, cpp_test) in wave3.items():
            self.assertEqual(extracted['tables'][table]['members'], members)
            for record in (r for r in records if r['table'] == table):
                self.assertEqual({lang: route['status'] for lang, route in record['language_mappings'].items()},
                                 {'c': 'direct', 'cpp': 'direct', 'csharp': 'unsupported', 'lua': 'unsupported'})
                self.assertIn(c_test, record['language_mappings']['c']['tests'])
                self.assertIn(cpp_test, record['language_mappings']['cpp']['tests'])
            # Compile the linked C layout pins as catalogue evidence, not runtime proof.
            result = subprocess.run([clang, '-std=c11', '-I', str(self.root / 'include'),
                                     '-fsyntax-only', str(self.root / c_test)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
        widgets = {r['symbol']: r for r in records if r['table'] == 'sco_widgets_v1'}
        wrapped = {'sco_widgets_v1.set_panel', 'sco_widgets_v1.remove_panel'}
        direct = {'sco_widgets_v1.update_nodes', 'sco_widgets_v1.remove_nodes', 'sco_widgets_v1.capabilities'}
        self.assertEqual(set(widgets), wrapped | direct)
        for symbol, record in widgets.items():
            self.assertEqual({lang: route['status'] for lang, route in record['language_mappings'].items()},
                             {'c': 'direct', 'cpp': 'typed-wrapper', 'csharp': 'typed-wrapper', 'lua': 'typed-wrapper'}
                             if symbol in wrapped else
                             {'c': 'direct', 'cpp': 'direct', 'csharp': 'unsupported', 'lua': 'unsupported'})

    def test_new_undocumented_method(self):
        self.header_change('sco_storage.h', '} sco_storage_v1;',
                           '    int (*future_method)(void);\n} sco_storage_v1;')
        self.refuses('new undocumented method: sco_storage_v1.future_method')

    def test_new_undocumented_table(self):
        path = self.root / 'include/sco_future.h'
        path.write_text('#include "sco_api.h"\n#define SCO_FUTURE_NAME "sco.future"\n'
                        '#define SCO_FUTURE_VERSION_1_0 0x00010000u\n'
                        'typedef struct sco_future_v1 { uint32_t size; uint32_t pad; '
                        'int (*ping)(void); } sco_future_v1;\n')
        self.refuses('new undocumented method: sco_future_v1.ping')

    def test_removed_symbol_still_documented(self):
        self.header_change('sco_storage.h',
                           'sco_result (*del)(sco_plugin* self, const char* key);', '')
        self.refuses('removed symbol still documented: sco_storage_v1.del')

    def test_signature_mismatch_in_record(self):
        self.record('sco_storage_v1.put', lambda r: r.update(signature='int (*)(void)'))
        self.refuses('signature mismatch: sco_storage_v1.put')

    def test_signature_mismatch_in_header(self):
        self.header_change('sco_storage.h',
                           'sco_result (*del)(sco_plugin* self, const char* key);',
                           'int (*del)(sco_plugin* self, const char* key);')
        self.refuses('signature mismatch: sco_storage_v1.del')

    def test_member_extent_mismatch(self):
        self.record('sco_storage_v1.put', lambda r: r.update(member_extent=8))
        self.refuses('member_extent mismatch: sco_storage_v1.put')

    def test_reordered_header_members(self):
        path = self.root / 'include/sco_settings.h'
        source = path.read_text()
        first = 'sco_result (*get_bool)(sco_plugin* self, const char* name, int32_t* out);'
        second = 'sco_result (*get_int)(sco_plugin* self, const char* name, int64_t* out);'
        self.assertIn(first, source)
        self.assertIn(second, source)
        path.write_text(source.replace(first, 'SWAP_MARKER').replace(second, first).replace('SWAP_MARKER', second))
        self.refuses('member_extent mismatch')

    def test_duplicate_id(self):
        records = catalogue.load_records(self.root)
        self.record(records[1]['symbol'], lambda r: r.update(id=records[0]['id']))
        self.refuses('duplicate id:')

    def test_missing_example(self):
        self.record('sco_api.has', lambda r: r['examples'].append('sdk/examples/missing.c'))
        self.refuses('referenced path missing')

    def test_missing_test(self):
        self.record('sco_api.has', lambda r: r['tests'].append('tests/missing.c'))
        self.refuses('referenced path missing')

    def test_nonexistent_csharp_typed_wrapper(self):
        self.record('sc_spawn_service_v1.spawn_as',
                    lambda r: r['language_mappings']['csharp'].update(status='typed-wrapper'))
        self.refuses('unsupported language claimed as supported: sc_spawn_service_v1.spawn_as / csharp')

    def test_csharp_raw_field_is_not_typed_wrapper(self):
        self.record('sco_api.list_commands',
                    lambda r: r['language_mappings']['csharp'].update(status='typed-wrapper'))
        self.refuses('unsupported language claimed as supported: sco_api.list_commands / csharp')

    def test_wrong_table_csharp_wrapper_evidence(self):
        evidence = {'path': 'sdk/csharp/Sco.Sdk/Game/Actors.cs',
                    'token': '_t->last_error(self, o, &n)'}
        # The token is taken from the actual adapter, but its table is not World.
        source = (self.root / evidence['path']).read_text()
        calls = [line.strip() for line in source.splitlines() if '->last_error(' in line]
        self.assertTrue(calls)
        evidence['token'] = calls[0]
        self.record('sc_world_v1.last_error', lambda r: r['language_mappings']['csharp'].update(
            status='typed-wrapper', evidence=[evidence]))
        self.refuses('unsupported language claimed as supported: sc_world_v1.last_error / csharp')

    def test_lua_mutation_claim(self):
        self.record('sc_creative_v1.set_god_mode',
                    lambda r: r['language_mappings']['lua'].update(status='typed-wrapper'))
        self.refuses('unsupported language claimed as supported: sc_creative_v1.set_god_mode / lua')

    def test_lua_direct_ffi_claim(self):
        self.record('sc_world_v1.camera',
                    lambda r: r['language_mappings']['lua'].update(status='direct'))
        self.refuses('lua direct FFI')

    def test_language_evidence_token_missing(self):
        self.record('sco_api.has', lambda r: r['language_mappings']['cpp']['evidence'][0].update(token='missing_wrapper_token'))
        self.refuses('language evidence token missing')

    def test_stale_printed_signature_even_if_docs_and_records_agree(self):
        before = 'sco_result (*del)(sco_plugin* self, const char* key);'
        after = 'int (*del)(sco_plugin* self, const char* key);'
        self.record('sco_storage_v1.del', lambda r: r.update(buffer_contract=r['buffer_contract'].replace(before, after)))
        path = self.root / 'docs/contracts.md'
        path.write_text(path.read_text().replace(before, after))
        self.refuses('documented signature mismatch: sco_storage_v1.del')

    def test_documentation_drift(self):
        path = self.root / 'docs/contracts.md'
        path.write_text(path.read_text().replace('Borrowed static host string; never free.', 'Free the host string.'))
        self.refuses('documentation/record contract mismatch: sco_api.host_version')

    def test_service_or_version_mismatch(self):
        self.record('sco_storage_v1.put', lambda r: r.update(service='sco.invented'))
        self.refuses('service mismatch: sco_storage_v1.put')

    def test_evidence_path_traversal(self):
        self.record('sco_api.has', lambda r: r['tests'].append('../outside.c'))
        self.refuses('repository-relative')

    def test_supported_language_requires_its_own_test(self):
        record = next(r for r in catalogue.load_records(ROOT) if r['symbol'] == 'sc_actors_v1.health')
        mapping = dict(record['language_mappings']['csharp'])
        mapping['tests'] = []
        with self.assertRaisesRegex(catalogue.CatalogueError, 'supported language has no test'):
            catalogue.language_check(ROOT, record, 'csharp', mapping)

    def test_csharp_cannot_use_c_layout_as_wrapper_test(self):
        record = next(r for r in catalogue.load_records(ROOT) if r['symbol'] == 'sc_actors_v1.health')
        mapping = dict(record['language_mappings']['csharp'])
        mapping['tests'] = ['tests/abi_game_actors.c']
        with self.assertRaisesRegex(catalogue.CatalogueError, 'wrong language test'):
            catalogue.language_check(ROOT, record, 'csharp', mapping)

    def test_test_path_must_be_in_record_evidence(self):
        record = next(r for r in catalogue.load_records(ROOT) if r['symbol'] == 'sc_actors_v1.health')
        mapping = dict(record['language_mappings']['cpp'])
        mapping['tests'] = ['tests/test_net.cpp']
        with self.assertRaisesRegex(catalogue.CatalogueError, 'test not in record evidence'):
            catalogue.language_check(ROOT, record, 'cpp', mapping)

    def test_invalid_source_sha(self):
        with self.assertRaisesRegex(catalogue.CatalogueError, '40-hex'):
            catalogue.build(self.root, 'main')

    def test_two_builds_byte_identical(self):
        outputs = [self.root / 'first.json', self.root / 'second.json']
        for output in outputs:
            result = subprocess.run([sys.executable, str(ROOT / 'tools/sdk_catalogue.py'),
                                     'build', '--root', str(self.root), '--out', str(output),
                                     '--source-sha', SHA], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(outputs[0].read_bytes(), outputs[1].read_bytes())
        result = json.loads(outputs[0].read_bytes())
        self.assertEqual(result['source_sha'], SHA)
        self.assertEqual(result['sdk_version'], (self.root / 'sdk/VERSION').read_text().strip())
        self.assertEqual(result['game_pack_version'], '0.8.0')
        self.assertNotIn(str(self.root), outputs[0].read_text())
        self.assertEqual(set(result), {'schema_version', 'sdk_version', 'game_pack_version',
                                       'source_sha', 'contract_context', 'tables', 'records'})
        self.assertIn('argument-definition stride', result['contract_context']['reading_rules'])
        self.assertIn('64 open cursors', result['tables']['sco_storage_v1']['context'])


if __name__ == '__main__':
    unittest.main()
