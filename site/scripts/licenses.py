#!/usr/bin/env python3
"""Reproduce the declared-license inventory from the committed npm lockfile."""
import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def inventory(root):
    lock = json.loads((root / 'package-lock.json').read_text())
    packages = []
    for path, package in sorted(lock['packages'].items()):
        if not path:
            continue
        if not package.get('license') or not package.get('integrity'):
            raise ValueError('Locked dependency lacks license or integrity: ' + path)
        packages.append({'name': path.rsplit('node_modules/', 1)[1],
                         'version': package['version'], 'license': package['license'],
                         'integrity': package['integrity'], 'optional': package.get('optional', False)})
    return {'schema_version': 1, 'source': 'package-lock.json', 'packages': packages}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    text = json.dumps(inventory(ROOT), sort_keys=True, indent=2) + '\n'
    dest = ROOT / 'dependency-licenses.json'
    if args.check:
        if dest.read_text() != text:
            raise SystemExit('Dependency-license inventory stale; run python3 site/scripts/licenses.py')
        print('Dependency-license inventory matches every locked package')
    else:
        dest.write_text(text)
        print('Recorded declared licenses for every locked package')
