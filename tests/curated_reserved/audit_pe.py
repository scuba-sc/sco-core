#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: 2026 the sco-core contributors
"""Prototype PE export/import audit; validate output format against actual dumpbin first."""
import argparse
import pathlib
import re
import subprocess
import sys

HOST_EXPORTS = {'reserved_host_mock_table'}
MODULE_EXPORTS = {'curated_module_identity', 'curated_module_host_accessor_found', 'curated_module_table',
                  'curated_module_read', 'curated_module_move', 'curated_module_enqueue_set',
                  'curated_module_enqueue_add', 'curated_module_arm_move_gate',
                  'curated_module_wait_move_gate', 'curated_module_release_move_gate'}
ROW = re.compile(r'^\s*\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(\S+)'
                 r'(?:\s+=\s+@ILT\+[0-9]+\((\S+)\))?\s*$')
HEADER = re.compile(r'^\s*([\w.\-]+\.(?:dll|exe))\s*$', re.I)
IMPORT = re.compile(r'^\s*[0-9A-Fa-f]+\s+(\S+)\s*$')

def exports(s):
    result = []
    table = False
    seen = False
    for line in s.splitlines():
        if re.match(r'^\s*ordinal\s+hint\s+RVA\s+name\s*$', line, re.I):
            if seen:
                raise ValueError('duplicate export table')
            table = seen = True
            continue
        if not table or not line.strip():
            continue
        if line.strip() == 'Summary':
            table = False
            continue
        m = ROW.match(line)
        if not m:
            raise ValueError('unrecognized export row: ' + line.strip())
        if m.group(2) and m.group(2) != m.group(1):
            raise ValueError('export thunk does not name its export: ' + line.strip())
        result.append(m.group(1))
    if not seen or not result:
        raise ValueError('missing or unrecognized export table')
    return result

def imports(s):
    result, current = {}, None
    for line in s.splitlines():
        if m := HEADER.match(line):
            current = m.group(1).lower()
            if current in result: raise ValueError('duplicate import descriptor: ' + current)
            result[current] = []
        elif current is not None and (m := IMPORT.match(line)):
            result[current].append(m.group(1))
    if not result: raise ValueError('no import descriptors parsed; dumpbin format unknown')
    return result

def check(host_exports, module_exports, module_imports, lib_symbols, exe_name):
    if set(host_exports) != HOST_EXPORTS or len(host_exports) != len(HOST_EXPORTS): raise ValueError('unexpected EXE exports: ' + repr(host_exports))
    if set(module_exports) != MODULE_EXPORTS or len(module_exports) != len(MODULE_EXPORTS): raise ValueError('unexpected module exports: ' + repr(module_exports))
    host_imports = module_imports.get(exe_name.lower())
    if host_imports != ['reserved_host_mock_table']: raise ValueError('wrong host import descriptor: ' + repr(host_imports))
    for dll, names in module_imports.items():
        if dll != exe_name.lower() and any('reserved_host_mock_table' in x for x in names): raise ValueError('accessor from unexpected library: ' + dll)
    if not re.search(r'^\s*[0-9A-Fa-f]+\s+__imp_reserved_host_mock_table\s*$', lib_symbols, re.MULTILINE):
        raise ValueError('missing import library accessor symbol')

def selftest():
    header = ' ordinal hint RVA name\n'
    ex = header + '  1    0 00001000 reserved_host_mock_table\n'
    im = ' reserved_host.exe\n  0000000000001000 Import Address Table\n          0 reserved_host_mock_table\n KERNEL32.dll\n          0 GetLastError\n'
    me = header + '\n'.join(f'{i} {i:x} 00001000 {s}' for i, s in enumerate(sorted(MODULE_EXPORTS), 1))
    assert exports(ex) == list(HOST_EXPORTS)
    assert exports(header + ' 1 0 0000281A reserved_host_mock_table = @ILT+6165(reserved_host_mock_table)\n') == list(HOST_EXPORTS)
    for bad_export in (' 1 0 0000281A reserved_host_mock_table = @ILT+1(other)\n',
                       ' 1 0 0000281A reserved_host_mock_table = other.dll.target\n',
                       ' 1 0000281A [NONAME]\n'):
        try: exports(header + bad_export)
        except ValueError: continue
        raise AssertionError('unexpected export alias accepted')
    check(exports(ex), exports(me), imports(im), ' 123 __imp_reserved_host_mock_table\n', 'reserved_host.exe')
    try: check(exports(ex), exports(me), imports(im), 'text __imp_reserved_host_mock_table\n', 'reserved_host.exe')
    except ValueError: pass
    else: raise AssertionError('unrecognized import-library row accepted')
    for bad in [im.replace('reserved_host.exe', 'other.exe'), im.replace('reserved_host_mock_table', 'other'), im.replace('reserved_host_mock_table', 'reserved_host_mock_table\n          1 hidden_symbol')]:
        try: check(exports(ex), exports(me), imports(bad), ' 123 __imp_reserved_host_mock_table\n', 'reserved_host.exe')
        except ValueError: continue
        raise AssertionError('negative control passed')
    print('synthetic parser self-test passed; no PE or Windows execution was tested')

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--selftest', action='store_true')
    p.add_argument('--dumpbin', default='dumpbin.exe')
    p.add_argument('--exe', type=pathlib.Path)
    p.add_argument('--module', type=pathlib.Path)
    p.add_argument('--import-lib', type=pathlib.Path)
    p.add_argument('--evidence-dir', type=pathlib.Path)
    a = p.parse_args()
    if a.selftest: return selftest()
    if not all((a.exe, a.module, a.import_lib, a.evidence_dir)): p.error('all artifact and evidence paths are required')
    a.evidence_dir.mkdir(parents=True, exist_ok=True)
    def dump(name, flag, path):
        proc = subprocess.run([a.dumpbin, '/nologo', flag, str(path)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        out = proc.stdout.decode('utf-8', errors='replace')
        (a.evidence_dir / (name + '.txt')).write_text(out, encoding='utf-8')
        if proc.returncode: raise ValueError('dumpbin failed: ' + name + '\n' + out)
        return out
    he = dump('host-exports', '/exports', a.exe)
    me = dump('module-exports', '/exports', a.module)
    mi = dump('module-imports', '/imports', a.module)
    # Short COFF import members are listed by the archive linker index; they
    # are not necessarily expanded by dumpbin's ordinary /symbols view.
    il = dump('host-import-lib-symbols', '/linkermember:1', a.import_lib)
    try:
        check(exports(he), exports(me), imports(mi), il, a.exe.name)
    except ValueError:
        # Preserve the actual tool format in CI when a parser or inventory fails.
        for label, output in (('host exports', he), ('module exports', me), ('module imports', mi),
                              ('host import library symbols', il)):
            print('--- ' + label + ' ---\n' + output, file=sys.stderr)
        raise
    print('PASS: expected host/module exports, named EXE import and import-lib symbol; dependency/source closure NOT audited')

if __name__ == '__main__':
    try: main()
    except (ValueError, OSError) as e:
        print('FAIL:', e, file=sys.stderr); sys.exit(1)
