#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Focused G6 catalogue/package/composition proof, no aggregate suites."""
import json
from pathlib import Path
import unittest
ROOT = Path(__file__).resolve().parents[3]
class LoadoutMetadata(unittest.TestCase):
    def test_game_package_membership(self):
        manifest = json.loads((ROOT / 'sdk/components.json').read_text())
        components = {c['source']: c for c in manifest['components']}
        for path in ('include/sc_loadout.h', 'include/scosdk/game/loadout.hpp', 'docs/game/loadout.md', 'tests/abi_game_loadout.c'):
            self.assertIn(path, components)
            self.assertEqual(components[path]['profiles'], ['game-sc'])
        self.assertTrue(components['include/sc_loadout.h']['public_header'])
        self.assertTrue(components['include/scosdk/game/loadout.hpp']['public_header'])
    def test_production_composition_fail_closed(self):
        source = (ROOT / 'src/game/services/spatial.cpp').read_text()
        self.assertIn('r = StartLoadout();', source)
        self.assertNotIn('StartLoadout(&', source)
        self.assertGreaterEqual(source.count('StopLoadout();'), 2)
    def test_catalogue_language_routes(self):
        records = json.loads((ROOT / 'sdk/contracts/sc_loadout_v1.json').read_text())['records']
        self.assertEqual(len(records), 1)
        record = records[0]
        self.assertEqual(record['symbol'], 'sc_loadout_v1.apply')
        self.assertEqual(record['member_extent'], 16)
        self.assertEqual(record['language_mappings']['c']['status'], 'direct')
        self.assertEqual(record['language_mappings']['cpp']['status'], 'typed-wrapper')
        for language in ('csharp', 'lua'):
            self.assertEqual(record['language_mappings'][language]['status'], 'unsupported')
if __name__ == '__main__':
    unittest.main()
