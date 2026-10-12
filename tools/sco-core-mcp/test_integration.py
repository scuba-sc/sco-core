#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the literal documented launcher; no client settings are changed."""
import json
from pathlib import Path
import re
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
TOOLS = {'search_api', 'get_symbol', 'list_services', 'get_example',
         'get_capability_status', 'check_compatibility'}


def configuration(text):
    blocks = [json.loads(b) for b in re.findall(r'```json\n(.*?)\n```', text, re.S)]
    return next(b['mcpServers']['sco-core-sdk'] for b in blocks if 'mcpServers' in b)


def validate(config):
    if (set(config) != {'type', 'command', 'args', 'tools'}
            or config['type'] != 'stdio' or config['command'] != 'python3'
            or len(config['tools']) != len(TOOLS) or set(config['tools']) != TOOLS):
        raise ValueError('Expected explicit read-only launcher with six-tool allowlist and no secrets')
    expected = ['/absolute/path/to/sco-core/tools/sco-core-mcp/server.py', '--catalogue',
                '/absolute/path/to/sco-core/dist/sdk-catalogue.json', '--repo-root',
                '/absolute/path/to/sco-core']
    if config['args'] != expected:
        raise ValueError('Unexpected launcher arguments')


class IntegrationTests(unittest.TestCase):
    def test_documented_launcher_and_read_only_discovery(self):
        config = configuration((ROOT / 'docs/agent-integration.md').read_text())
        validate(config)
        command = [config['command']] + [arg.replace('/absolute/path/to/sco-core', str(ROOT))
                                         for arg in config['args']]
        messages = [
            {'jsonrpc': '2.0', 'id': 1, 'method': 'initialize', 'params': {
                'protocolVersion': '2025-11-25', 'capabilities': {},
                'clientInfo': {'name': 'documented-launcher-test', 'version': '1'}}},
            {'jsonrpc': '2.0', 'method': 'notifications/initialized'},
            {'jsonrpc': '2.0', 'id': 2, 'method': 'tools/list'},
            {'jsonrpc': '2.0', 'id': 3, 'method': 'tools/call',
             'params': {'name': 'list_services', 'arguments': {}}},
        ]
        result = subprocess.run(command, input=''.join(json.dumps(m) + '\n' for m in messages),
                                capture_output=True, text=True, timeout=10, cwd=ROOT.parent)
        self.assertEqual(result.returncode, 0, result.stderr)
        responses = {m['id']: m for m in map(json.loads, result.stdout.splitlines())}
        self.assertEqual(responses[1]['result']['protocolVersion'], '2025-11-25')
        tools = responses[2]['result']['tools']
        self.assertEqual({t['name'] for t in tools}, TOOLS)
        self.assertEqual(len(tools), len(TOOLS))
        for tool in tools:
            self.assertIs(tool['annotations']['readOnlyHint'], True)
        self.assertNotIn('error', responses[3])
        self.assertFalse(responses[3]['result'].get('isError', False))
        catalogue = json.loads((ROOT / 'dist/sdk-catalogue.json').read_text())
        self.assertIn(catalogue['source_sha'], json.dumps(responses[3]))
        print('Documented launcher: six annotated tools; actual list_services; ' + catalogue['source_sha'])

    def test_wildcards_unknown_tools_and_secrets_refused(self):
        from copy import deepcopy
        config = configuration((ROOT / 'docs/agent-integration.md').read_text())
        for field, value in [('tools', ['*']), ('tools', sorted(TOOLS) + ['write_file']),
                             ('env', {'API_KEY': 'forbidden-config-value'})]:
            bad = deepcopy(config)
            bad[field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                validate(bad)


if __name__ == '__main__':
    unittest.main(verbosity=2)
