#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: 2026 the sco-core contributors
"""Build altered ELF artifacts and require the binary audit to reject them."""
import argparse
import pathlib
import subprocess
import sys
import tempfile


def run(command):
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, errors='replace')
    if result.returncode:
        raise RuntimeError('command failed:\n' + result.stdout)


def main():
    parser = argparse.ArgumentParser()
    for name in ('compiler', 'source', 'python', 'nm', 'readelf', 'host', 'module'):
        parser.add_argument('--' + name, required=True)
    parser.add_argument('--allowed-runpath', default='')
    args = parser.parse_args()
    source = pathlib.Path(args.source)
    fixture = source / 'tests/curated_reserved'
    audit = [args.python, str(fixture / 'audit_elf.py'), '--nm', args.nm, '--readelf', args.readelf,
             '--allowed-runpath', args.allowed_runpath]
    with tempfile.TemporaryDirectory(prefix='sco-curated-elf-negative-') as temp:
        root = pathlib.Path(temp)
        module_source = root / 'module.cpp'
        module_source.write_text((fixture / 'curated_module.cpp').read_text(encoding='utf-8') +
                                 '\nextern "C" void sco_runtime_provide_service();\n'
                                 '__attribute__((used)) static void injected_import() {'
                                 ' sco_runtime_provide_service(); }\n', encoding='utf-8')
        module = root / 'bad_module.so'
        run([args.compiler, '-std=c++20', '-O0', '-fPIC', '-shared', '-fvisibility=hidden',
             '-pthread', '-I' + str(source / 'include'), str(module_source),
             '-Wl,--version-script=' + str(fixture / 'curated_module.exports'), '-o', str(module)])
        result = subprocess.run(audit + ['--host', args.host, '--module', str(module)],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        if (result.returncode == 0 or 'unexpected module import:' not in result.stdout or
                'sco_runtime_provide_service' not in result.stdout):
            raise RuntimeError('extra import was not rejected for the expected reason:\n' + result.stdout)
        module_source.write_text((fixture / 'curated_module.cpp').read_text(encoding='utf-8') +
                                 '\nnamespace sco { __attribute__((used)) void HiddenLegacyProvider() {} }\n',
                                 encoding='utf-8')
        run([args.compiler, '-std=c++20', '-O0', '-fPIC', '-shared', '-fvisibility=hidden',
             '-pthread', '-I' + str(source / 'include'), str(module_source),
             '-Wl,--version-script=' + str(fixture / 'curated_module.exports'), '-o', str(module)])
        result = subprocess.run(audit + ['--host', args.host, '--module', str(module)],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        if result.returncode == 0 or 'unexpected module project symbol: sco::HiddenLegacyProvider()' not in result.stdout:
            raise RuntimeError('hidden module implementation was not rejected:\n' + result.stdout)
        module_source.write_text((fixture / 'curated_module.cpp').read_text(encoding='utf-8'), encoding='utf-8')
        run([args.compiler, '-std=c++20', '-O0', '-fPIC', '-shared', '-fvisibility=hidden',
             '-pthread', '-I' + str(source / 'include'), str(module_source),
             '-Wl,--version-script=' + str(fixture / 'curated_module.exports'),
             '-Wl,-rpath,/unreviewed-test-path', '-o', str(module)])
        result = subprocess.run(audit + ['--host', args.host, '--module', str(module)],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        if result.returncode == 0 or 'unexpected runtime search path' not in result.stdout:
            raise RuntimeError('unexpected module search path was not rejected:\n' + result.stdout)
        run([args.compiler, '-std=c++20', '-O0', '-fPIC', '-shared', '-fvisibility=hidden',
             '-pthread', '-I' + str(source / 'include'), str(module_source),
             '-Wl,--version-script=' + str(fixture / 'curated_module.exports'),
             '-Wl,--audit=/unreviewed-test-audit.so', '-o', str(module)])
        result = subprocess.run(audit + ['--host', args.host, '--module', str(module)],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        if result.returncode == 0 or 'unexpected dynamic tags' not in result.stdout:
            raise RuntimeError('unexpected module audit tag was not rejected:\n' + result.stdout)
        host_source = root / 'host.cpp'
        host_source.write_text((fixture / 'reserved_host.cpp').read_text(encoding='utf-8') +
                               '\nnamespace sco { __attribute__((used)) void InjectedLegacyRuntime() {} }\n',
                               encoding='utf-8')
        host = root / 'bad_host'
        run([args.compiler, '-std=c++20', '-O0', '-pthread', '-fvisibility=hidden',
             '-fvisibility-inlines-hidden', '-DSCO_RESERVED_DRAIN_TESTING=1',
             '-I' + str(source / 'include'), '-I' + str(source / 'src'), str(host_source),
             str(source / 'src/reserved_session.cpp'), '-ldl',
             '-Wl,--export-dynamic-symbol=reserved_host_mock_table',
             '-Wl,--version-script=' + str(fixture / 'reserved_host.exports'), '-o', str(host)])
        result = subprocess.run(audit + ['--host', str(host), '--module', args.module],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        if result.returncode == 0 or 'unexpected host project symbol: sco::InjectedLegacyRuntime()' not in result.stdout:
            raise RuntimeError('hidden runtime symbol was not rejected for the expected reason:\n' + result.stdout)
        print('PASS: altered imports, hidden implementations, search path and audit tag were rejected')


if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError) as error:
        print('FAIL:', error, file=sys.stderr)
        sys.exit(1)
