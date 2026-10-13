#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Explicit local development package; never builds/downloads/releases a helper."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--adapter', required=True, type=Path)
    p.add_argument('--adapter-sha256', required=True)
    p.add_argument('--output', required=True, type=Path)
    a = p.parse_args()
    if a.adapter.is_symlink() or not a.adapter.is_file() or a.adapter.stat().st_size > 64 * 1024 * 1024:
        p.error('Invalid compiled adapter')
    digest = hashlib.sha256(a.adapter.read_bytes()).hexdigest()
    if digest != a.adapter_sha256:
        p.error('Adapter hash mismatch')
    if a.adapter.suffix not in ('.dll', '.so', '.dylib'):
        p.error('Unsupported adapter suffix')
    a.output.mkdir(parents=True, exist_ok=False)
    root = Path(__file__).resolve().parent
    for name in ('server.py', 'peer.py', 'contract.schema.json', 'README.md'):
        shutil.copyfile(root / name, a.output / name)
    name = 'sco_core_mcp_runtime_peer' + a.adapter.suffix
    shutil.copyfile(a.adapter, a.output / name)
    (a.output / 'adapter-manifest.json').write_text(json.dumps({
        'schema_version': 1, 'adapter_abi': 1, 'adapter': name, 'sha256': digest,
        'license': 'GPL-3.0-only', 'source': 'project-authored', 'default_enabled': False,
    }, indent=2) + '\n')
    print('Local runtime package created; Windows security/game acceptance NOT VERIFIED')

if __name__ == '__main__':
    main()
