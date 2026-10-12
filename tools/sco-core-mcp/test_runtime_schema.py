#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Step-0 design vectors only: no runtime imports, IPC or game required."""
from copy import deepcopy
import json
from pathlib import Path
import sys
import unittest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
from ip_audit import validate_schema

NAMES = (
    'runtime_get_host_info', 'runtime_list_plugins', 'runtime_tail_plugin_log',
    'runtime_list_service_status', 'runtime_get_resolution_status',
    'runtime_get_game_status',
)


def expand(schema, root):
    """Resolve this acyclic document's local refs for the existing stdlib validator."""
    if '$ref' in schema:
        assert set(schema) == {'$ref'}
        assert schema['$ref'].startswith('#/$defs/')
        return expand(root['$defs'][schema['$ref'].split('/')[-1]], root)
    # description/default are annotations, never validation constraints.
    result = {k: deepcopy(v) for k, v in schema.items()
              if k not in ('$defs', 'description', 'default')}
    for key in ('properties',):
        if key in result:
            result[key] = {k: expand(v, root) for k, v in result[key].items()}
    if 'items' in result:
        result['items'] = expand(result['items'], root)
    if 'oneOf' in result:
        result['oneOf'] = [expand(v, root) for v in result['oneOf']]
    return result


def check(vector, schema):
    errors = []
    validate_schema(vector, schema, 'vector', errors)
    # Byte ceilings are separate from JSON Schema character limits.
    for key, maximum in (('request', 4096 - 104), ('response', 65536 - 104)):
        if len(json.dumps(vector[key], ensure_ascii=False, separators=(',', ':')).encode()) > maximum:
            errors.append(key + ' exceeds authenticated record payload budget')
    if not errors:
        def check_u64(value):
            if isinstance(value, dict):
                for key, item in value.items():
                    if key in ('epoch', 'generation', 'load_generation', 'resolution_generation',
                               'sequence', 'after_sequence', 'next_sequence', 'first_available_sequence'):
                        if item is not None and int(item) > 18446744073709551615:
                            errors.append(key + ' exceeds unsigned 64-bit range')
                    check_u64(item)
            elif isinstance(value, list):
                for item in value:
                    check_u64(item)
        check_u64(vector)
        response = vector['response']
        p = response['provenance']
        if response['status'] in ('ok', 'stale', 'restart_required'):
            if any(p[k] is None for k in ('session_id', 'epoch', 'generation', 'observed_ms', 'age_ms')):
                errors.append('observations require session and freshness provenance')
        if response['status'] == 'stale' and (p['age_ms'] is None or p['age_ms'] <= 1000):
            errors.append('stale requires measured age above normal freshness window')
        data = response['data']
        if data and 'next_cursor' in data and data['has_more'] != (data['next_cursor'] is not None):
            errors.append('pagination cursor/has_more mismatch')
        if data and 'entries' in data:
            if len(json.dumps(data, ensure_ascii=False, separators=(',', ':')).encode()) > 32768:
                errors.append('log tail exceeds serialized byte budget')
            if any(len(entry['message'].encode()) > 512 for entry in data['entries']):
                errors.append('log message exceeds UTF-8 byte budget')
    if errors:
        raise ValueError('; '.join(errors))


class RuntimeSchemaTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = HERE / 'runtime_schema'
        cls.contract = json.loads((path / 'contract.schema.json').read_text())
        cls.schema = expand(cls.contract, cls.contract)
        cls.requests = json.loads((path / 'requests.json').read_text())
        cls.responses = json.loads((path / 'responses.json').read_text())

    def test_vectors_cover_exact_surface_and_states(self):
        self.assertEqual(set(self.requests), set(NAMES))
        self.assertEqual(set(self.responses), set(NAMES))
        states = set()
        for index, name in enumerate(NAMES, 256):
            self.assertTrue(self.responses[name])
            for response in self.responses[name]:
                vector = {'schema_version': 1, 'tool': name, 'opcode': index,
                          'request': self.requests[name], 'response': response}
                with self.subTest(name=name, status=response['status']):
                    check(vector, self.schema)
                states.add(response['status'])
        self.assertEqual(states, {'ok', 'unavailable', 'stale', 'unknown', 'restart_required'})

    def test_unknown_fields_opcodes_versions_and_unsafe_fields_refused(self):
        name = NAMES[0]
        base = {'schema_version': 1, 'tool': name, 'opcode': 256,
                'request': self.requests[name], 'response': self.responses[name][0]}
        mutations = [dict(base, schema_version=2), dict(base, opcode=999),
                     dict(base, tool='invoke_raw')]
        for field in ('path', 'address', 'pointer', 'command', 'filename'):
            mutant = deepcopy(base)
            mutant['request'][field] = 'synthetic_forbidden_value'
            mutations.append(mutant)
            mutant = deepcopy(base)
            mutant['response']['data'][field] = 'synthetic_forbidden_value'
            mutations.append(mutant)
        for mutant in mutations:
            with self.subTest(mutant=mutant['request']):
                with self.assertRaises(ValueError):
                    check(mutant, self.schema)

    def test_limits_and_boolean_integer_confusion_refused(self):
        name = NAMES[1]
        base = {'schema_version': 1, 'tool': name, 'opcode': 257,
                'request': self.requests[name], 'response': self.responses[name][0]}
        for limit in (0, 51, True):
            mutant = deepcopy(base)
            mutant['request']['limit'] = limit
            with self.assertRaises(ValueError):
                check(mutant, self.schema)
        mutant = deepcopy(base)
        mutant['request']['cursor'] = 'a' * 129
        with self.assertRaises(ValueError):
            check(mutant, self.schema)
        mutant = deepcopy(base)
        mutant['response']['data']['items'] *= 51
        with self.assertRaises(ValueError):
            check(mutant, self.schema)
        mutant = deepcopy(base)
        mutant['response']['provenance']['epoch'] = '18446744073709551616'
        with self.assertRaisesRegex(ValueError, '64-bit'):
            check(mutant, self.schema)
        # Valid character limits can still exceed the UTF-8 response byte limit.
        name = NAMES[2]
        mutant = {'schema_version': 1, 'tool': name, 'opcode': 258,
                  'request': self.requests[name], 'response': deepcopy(self.responses[name][0])}
        entry = deepcopy(mutant['response']['data']['entries'][0])
        entry['message'] = '\U0001f642' * 512
        mutant['response']['data']['entries'] = [deepcopy(entry) for _ in range(100)]
        with self.assertRaisesRegex(ValueError, 'budget'):
            check(mutant, self.schema)


if __name__ == '__main__':
    unittest.main()
