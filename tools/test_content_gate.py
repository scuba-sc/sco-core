#!/usr/bin/env python3
"""G14-only acceptance probe: catalogue, package membership, archive module + fixture.
GPL-3.0-only. Run after building test_content; no suites, network or game data.
"""
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]

def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

def run(command):
    print('+ ' + ' '.join(map(str, command)), flush=True)
    subprocess.run(list(map(str, command)), check=True)

def main():
    if len(sys.argv) != 2:
        raise SystemExit('usage: python3 -B tools/test_content_gate.py <scratch-build-dir>')
    build = Path(sys.argv[1]).resolve()
    if build == ROOT or ROOT in build.parents:
        raise SystemExit('use an independent scratch build path')
    work = build / 'content-gate'
    work.mkdir(exist_ok=True)
    sys.path.insert(0, str(ROOT / 'tools'))
    catalogue = load('sdk_catalogue', ROOT / 'tools/sdk_catalogue.py')
    records, inventory = catalogue.validate(ROOT)
    content = [r for r in records if r['table'] == 'sco_content_v1']
    assert {r['symbol'] for r in content} == {'sco_content_v1.item', 'sco_content_v1.read'}
    assert inventory['tables']['sco_content_v1']['size'] == 24
    print('content catalogue: 2 methods, complete extents 16/24, C/C++ routes; C#/Lua unsupported', flush=True)
    clang = shutil.which('clang')
    assert clang, 'clang required for Windows layout-only proof'
    for language, standard in [('c', 'c11'), ('c++', 'c++20')]:
        run([clang, '--target=x86_64-pc-windows-msvc', '-ffreestanding', '-std=' + standard,
             '-x', language, '-Wall', '-Wextra', '-Werror', '-fsyntax-only', '-I', ROOT / 'include', ROOT / 'tests/abi_content.c'])
    package = load('content_package', ROOT / 'sdk/package.py')
    manifest = package.load_manifest()
    expected = {'include/sco_content.h', 'include/scosdk/content.hpp'}
    assert expected <= set(package.public_headers(manifest, ['core']))
    # Generate real local archives. No release/tag/push, published-artifact claim or full suite.
    archives = {}
    sdk = None
    for profile in ['core', 'core-source']:
        path = Path(package.build(manifest, profile, str(work)))
        archives[profile] = path
        with zipfile.ZipFile(path) as archive:
            entries = {n.split('/', 1)[1]: n for n in archive.namelist() if '/' in n}
            for entry in expected:
                assert entry in entries, (profile, entry)
            if profile == 'core-source':
                for entry in ['src/content/content.cpp', 'include/sco/content.h', 'tests/test_content.cpp', 'tests/abi_content.c']:
                    assert entry in entries, (profile, entry)
            else:
                assert not any(n.startswith('include/sco/') for n in entries)
                extraction = work / 'extracted-core'
                if extraction.exists():
                    shutil.rmtree(extraction)
                archive.extractall(extraction)
                sdk = extraction / archive.namelist()[0].split('/')[0]
    print('content package membership: real core/core-source archives; no private headers in core', flush=True)
    module = work / ('content_reader.dll' if sys.platform == 'win32' else 'content_reader.so')
    assert sdk is not None
    source = sdk / 'examples/content_reader/content_reader.c'
    run([clang, '-std=c11', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC',
         '-I', sdk / 'include', source, '-o', module])
    program = build / ('test_content.exe' if sys.platform == 'win32' else 'test_content')
    run([program, build / 'tests/out', module])
    print('G14 content gate: PASS (synthetic, kernel-only; not run in game)', flush=True)

if __name__ == '__main__':
    main()
