#!/usr/bin/env python3
"""Compiler-backed public SDK catalogue; Python standard library plus Clang/C11.

Records live in sdk/contracts. check also pins their rendered row cells to
 docs/contracts.md, so changing either editable representation alone fails.
extract validates and emits the current header inventory; build emits schema 1.
No timestamps, absolute paths, compiler IDs or implicit Git state enter output.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

import abi_snapshot

ROOT = Path(__file__).resolve().parents[1]
SCHEMA_VERSION = 1
RECORD_KEYS = {
    'id', 'kind', 'symbol', 'table', 'service', 'version_macro', 'source_header',
    'signature', 'member_extent', 'table_size', 'since', 'purpose',
    'buffer_contract', 'ownership', 'thread_affinity', 'callback_context',
    'reentrancy', 'results', 'absent_capability', 'cleanup', 'language_mappings',
    'examples', 'tests', 'limitations',
}
STATUSES = {'direct', 'typed-wrapper', 'restricted', 'unsupported'}
ENTRYPOINTS = {'sco_plugin_query', 'sco_plugin_load', 'sco_plugin_unload'}


class CatalogueError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise CatalogueError(message)


def encoded(value):
    return json.dumps(value, indent=2, sort_keys=True, ensure_ascii=False) + '\n'


def run(command):
    proc = subprocess.run(command, capture_output=True, text=True)
    require(proc.returncode == 0, 'compiler/probe failed: ' + proc.stderr.strip())
    return proc.stdout


def local_path(root, name):
    require(isinstance(name, str) and name and '\\' not in name,
            'invalid evidence path')
    path = Path(name)
    require(not path.is_absolute() and '..' not in path.parts,
            'evidence path must be repository-relative: ' + name)
    result = (root / path).resolve()
    require(result.is_relative_to(root.resolve()) and result.is_file(),
            'referenced path missing or outside repository: ' + name)
    return result


def walk(node):
    yield node
    for child in node.get('inner', []):
        yield from walk(child)


def find_clang():
    """CLANG, then PATH, then (Windows only) the standard LLVM and WinGet LLVM-MinGW installs.

    Non-Windows hosts behave exactly as before: CLANG, then `clang` on PATH, else None.
    """
    explicit = os.environ.get('CLANG')
    if explicit:
        return explicit
    found = shutil.which('clang')
    if found or sys.platform != 'win32':
        return found
    candidates = []
    for var in ('ProgramFiles', 'ProgramFiles(x86)'):
        base = os.environ.get(var)
        if base:
            candidates.append(Path(base) / 'LLVM/bin/clang.exe')
    local_app = os.environ.get('LOCALAPPDATA')
    if local_app:
        packages = Path(local_app) / 'Microsoft/WinGet/Packages'
        if packages.is_dir():
            candidates.extend(sorted(packages.glob('*LLVM*/*/bin/clang.exe'), reverse=True))
    for candidate in candidates:
        if candidate.is_file():
            return str(candidate)
    return None


def header_inventory(root, clang=None):
    """AST membership/types, compiled extents/versions, and reused root ABI probe.

    Regex only finds primary-file declarations/macros, never method membership,
    signature or layout. Callback-bearing command descriptors are not tables.
    Top-level C headers are the documented public boundary, not include/sco/.
    """
    clang = clang or find_clang()
    require(clang is not None, 'Clang is required for compiler-backed extraction')
    include = root / 'include'
    methods = {}
    tables = {}
    nested_prefixes = {}
    headers = sorted(include.glob('*.h'))
    require(headers, 'no public headers found')
    with tempfile.TemporaryDirectory(prefix='sdk_catalogue_') as work:
        work = Path(work)
        frozen, error = abi_snapshot.surface(cc=clang, include=str(include), work=str(work))
        require(error is None, error or 'root ABI probe failed')
        require(frozen is not None, 'root ABI probe returned no surface')
        frozen = frozen or []
        require('sizeof sco_api 128' in frozen and 'sizeof void* 8' in frozen,
                'catalogue requires frozen 128-byte sco_api and x64 layout')
        for header in headers:
            source = header.read_text(encoding='utf-8')
            ast = json.loads(run([clang, '-std=c11', '-I', str(include), '-Xclang',
                                 '-ast-dump=json', '-fsyntax-only', '-x', 'c', str(header)]))
            # Primary-file candidates only; included headers are inspected themselves.
            primary = set(re.findall(r'\bstruct\s+(\w+)\s*\{',
                                     re.sub(r'/\*.*?\*/|//[^\n]*', '', source, flags=re.S)))
            nodes = {node.get('name'): node for node in walk(ast)
                     if node.get('kind') == 'RecordDecl' and node.get('name') in primary
                     and any(f.get('kind') == 'FieldDecl' for f in node.get('inner', []))}
            fields_by_name = {name: [f for f in node.get('inner', []) if f.get('kind') == 'FieldDecl']
                              for name, node in nodes.items()}

            def callbacks(fields):
                result = []
                for field in fields:
                    typ = field.get('type', {})
                    sig = typ.get('desugaredQualType', typ.get('qualType', ''))
                    if re.search(r'\(\s*\*[^)]*\)\s*\(', sig):
                        result.append((field['name'], sig))
                return result

            for name, fields in fields_by_name.items():
                if not callbacks(fields) or fields[0]['name'] == 'size' or name == 'sco_command':
                    continue
                kind = fields[0].get('type', {}).get('desugaredQualType', '')
                match = re.fullmatch(r'struct (\w+)', kind)
                base = match.group(1) if match else None
                base_fields = fields_by_name.get(base, [])
                require(base_fields and base_fields[0]['name'] == 'size' and callbacks(base_fields),
                        'public method table has no leading size: ' + name)
                nested_prefixes[name] = (fields[0]['name'], base)

            for node in nodes.values():
                name = node.get('name')
                if node.get('kind') != 'RecordDecl' or name not in primary or name == 'sco_command':
                    continue
                fields = fields_by_name[name]
                functions = callbacks(fields)
                if not functions:
                    continue
                require(fields and (fields[0]['name'] == 'size' or name in nested_prefixes),
                        'public method table has no leading size: ' + name)
                if name in nested_prefixes:
                    field, base = nested_prefixes[name]
                    functions = [(field + '.' + member, sig) for member, sig in callbacks(fields_by_name[base])] + functions
                require(name not in tables, 'duplicate header table: ' + name)
                own_macros = re.findall(r'^\s*#\s*define\s+(\w+)', source, re.M)
                if name == 'sco_api':
                    version_macro = 'SCO_API_MAJOR/SCO_API_MINOR'
                    name_macro = None
                else:
                    versions = [m for m in own_macros if re.search(r'_VERSION(?:_\d+_\d+)?$', m)]
                    require(versions, 'no version macro for ' + name)
                    def version_key(macro):
                        return tuple(int(x) for x in re.findall(r'\d+', macro)) or (0,)
                    child_versions = [tuple(map(int, match.groups())) for child, (_, base) in nested_prefixes.items()
                                      if base == name and (match := re.search(r'_v(\d+)_(\d+)$', child))]
                    if child_versions:
                        versions = [m for m in versions if version_key(m) < min(child_versions)]
                        require(versions, 'no base version macro for ' + name)
                    version_macro = sorted(versions, key=version_key)[-1]
                    prefix = re.sub(r'_v\d+(?:_\d+)?$', '', name).upper()
                    names = [m for m in own_macros if m in {prefix + '_NAME', prefix + '_SERVICE_NAME'}]
                    require(len(names) == 1, 'ambiguous service name macro for ' + name)
                    name_macro = names[0]
                tables[name] = {
                    'source_header': header.relative_to(root).as_posix(),
                    'version_macro': version_macro, 'name_macro': name_macro,
                    'members': [f for f, _ in functions],
                }
                for field, signature in functions:
                    symbol = name + '.' + field
                    methods[symbol] = {'kind': 'table-method', 'symbol': symbol, 'table': name,
                                       'source_header': header.relative_to(root).as_posix(),
                                       'signature': signature, 'version_macro': version_macro}
            if header.name == 'sco_api.h':
                for node in walk(ast):
                    if node.get('kind') == 'FunctionDecl' and node.get('name') in ENTRYPOINTS:
                        symbol = node['name']
                        methods[symbol] = {'kind': 'entrypoint', 'symbol': symbol, 'table': None,
                                           'source_header': 'include/sco_api.h',
                                           'signature': node['type']['qualType'],
                                           'version_macro': 'SCO_API_MAJOR/SCO_API_MINOR',
                                           'member_extent': None, 'table_size': None,
                                           'service': 'native-plugin'}
        require(ENTRYPOINTS <= methods.keys(), 'plugin entrypoints missing from headers')
        lines = ['#include <stddef.h>', '#include <stdio.h>']
        lines += ['#include "' + h.name + '"' for h in headers]
        lines += ['int main(void) {',
                  '_Static_assert(sizeof(void*) == 8, "x64 required");',
                  'printf("game-pack %s\\n", SCO_GAME_PACK_VERSION);']
        for name, (field, base) in sorted(nested_prefixes.items()):
            lines.append(f'_Static_assert(offsetof({name}, {field}) == 0, "nested table must start at zero");')
            lines.append(f'_Static_assert(offsetof({base}, size) == 0 && sizeof((({base}*)0)->size) == 4, "nested table needs a 32-bit size prefix");')
        for name, table in sorted(tables.items()):
            macro = table['version_macro']
            version = '((SCO_API_MAJOR << 16) | SCO_API_MINOR)' if name == 'sco_api' else macro
            service = '"sco_api"' if name == 'sco_api' else table['name_macro']
            lines.append(f'printf("table {name} %zu %u %s\\n", sizeof({name}), (unsigned)({version}), {service});')
            for member in table['members']:
                lines.append(f'printf("member {name}.{member} %zu\\n", offsetof({name}, {member}) + sizeof((({name}*)0)->{member}));')
        lines += ['return 0;', '}']
        probe = work / 'catalogue.c'
        binary = work / 'catalogue'
        probe.write_text('\n'.join(lines), encoding='utf-8')
        run([clang, '-std=c11', '-I', str(include), str(probe), '-o', str(binary)])
        game_version = None
        for line in run([str(binary)]).splitlines():
            parts = line.split()
            if parts[0] == 'game-pack':
                game_version = parts[1]
            elif parts[0] == 'table':
                _, name, size, version, service = parts
                tables[name].update(size=int(size), version=int(version), service=service)
            elif parts[0] == 'member':
                methods[parts[1]]['member_extent'] = int(parts[2])
        for method in methods.values():
            if method['table']:
                table = tables[method['table']]
                method.update(table_size=table['size'], service=table['service'])
        # Verify root offsets independently against the existing compiled ABI authority.
        frozen_offsets = {line.split()[1]: int(line.split()[2]) for line in frozen if line.startswith('offset sco_api.')}
        for symbol, method in methods.items():
            if method['table'] == 'sco_api':
                require(method['member_extent'] == frozen_offsets[symbol] + 8,
                        'ABI probe extent mismatch: ' + symbol)
        for table in tables.values():
            table.pop('name_macro')
        return {'methods': sorted(methods.values(), key=lambda m: m['symbol']),
                'tables': dict(sorted(tables.items())), 'game_pack_version': game_version}


def load_records(root):
    records = []
    for path in sorted((root / 'sdk/contracts').glob('*.json')):
        document = json.loads(path.read_text(encoding='utf-8'))
        require(set(document) == {'schema_version', 'records'} and document['schema_version'] == SCHEMA_VERSION,
                'invalid record document: ' + path.name)
        require(isinstance(document['records'], list), 'records must be an array')
        records.extend(document['records'])
    require(records, 'no contract records')
    return records


def language_check(root, record, language, mapping):
    symbol = record['symbol']
    require(isinstance(mapping, dict) and set(mapping) == {'status', 'evidence', 'tests'},
            'invalid language mapping: ' + symbol)
    status = mapping['status']
    require(status in STATUSES and isinstance(mapping['evidence'], list) and mapping['evidence'],
            'invalid language status/evidence: ' + symbol)
    positive = status != 'unsupported'
    member = symbol.split('.')[-1]
    supported_evidence = False
    for evidence in mapping['evidence']:
        require(isinstance(evidence, dict) and set(evidence) == {'path', 'token'},
                'invalid language evidence: ' + symbol)
        token = evidence['token']
        path = evidence['path']
        text = local_path(root, path).read_text(encoding='utf-8')
        require(isinstance(token, str) and token and token in text,
                'language evidence token missing: ' + symbol + ' / ' + language)
        call = re.search(r'->\s*' + re.escape(member) + r'\s*\(', token) is not None
        export = record['kind'] == 'entrypoint' and member in token
        if language == 'c':
            supported_evidence |= status == 'direct' and path == record['source_header'] and member in token
        elif language == 'cpp':
            typed = status in {'typed-wrapper', 'restricted'} and path.startswith('include/scosdk/')
            table_present = record['table'] is None or bool(re.search(r'\b' + re.escape(record['table']) + r'\b', text))
            supported_evidence |= ((status == 'direct' and path == record['source_header'] and member in token)
                                   or (typed and table_present and (call or export)))
        elif language == 'csharp':
            wrapper = path.startswith('sdk/csharp/Sco.Sdk/') and path.endswith('.cs')
            interop = Path(path).name in {'Interop.cs', 'DataCoreInterop.cs'}
            managed_table = ''.join(part[:1].upper() + part[1:] for part in (record['table'] or '').split('_'))
            table_present = record['table'] is None or bool(re.search(r'\b' + re.escape(managed_table) + r'\b', text))
            supported_evidence |= wrapper and table_present and ((status == 'direct' and interop and member in token)
                                               or (status in {'typed-wrapper', 'restricted'} and not interop and (call or export)))
            supported_evidence |= (export and path == 'sdk/examples/cs_hello/CsHello.cs'
                                   and f'EntryPoint = "{member}"' in token)
        elif language == 'lua':
            transaction_adapter = (record['table'] == 'sco_storage_v1'
                                   and member in {'begin', 'commit', 'rollback'}
                                   and 'StoreTx(L, st ? st->' + member + ' : NULL' in token)
            supported_evidence |= (path in {'plugins/lua/sco_lua.c', 'plugins/lua/sco_lua_game.c'}
                                   and (call or transaction_adapter))
    if language == 'lua' and positive:
        require(status in {'typed-wrapper', 'restricted'},
                'unsupported language claimed as supported: ' + symbol + ' / lua direct FFI')
        # The game adapter is read-only. self-taking last_error/state are queries,
        # not exceptions authorizing mutation; native table discovery is not Lua FFI.
        readonly_self = {('sc_gamerules_v1', 'read'), ('sc_gamerules_v1', 'list_params'),
                         ('sc_inventory_v1', 'snapshot'), ('sc_inventory_v1_2', 'snapshot_layout')}
        mutation = (record['source_header'].startswith('include/sc_') and 'sco_plugin *' in record['signature']
                    and member not in {'last_error', 'state'} and (record['table'], member) not in readonly_self)
        require(not mutation and record['kind'] != 'entrypoint',
                'unsupported language claimed as supported: ' + symbol + ' / lua')
    require(not positive or supported_evidence,
            'unsupported language claimed as supported: ' + symbol + ' / ' + language)
    tests = mapping['tests']
    require(isinstance(tests, list) and all(isinstance(p, str) and p for p in tests),
            'invalid language tests: ' + symbol + ' / ' + language)
    require(not positive or tests, 'supported language has no test: ' + symbol + ' / ' + language)
    for path in tests:
        require(path in record['tests'], 'test not in record evidence: ' + symbol + ' / ' + path)
        local_path(root, path)
        # C ABI pins compile as both C11 and C++20; C# and Lua must name their
        # own suites, not borrow C layout evidence for an untested adapter.
        if language == 'c':
            valid = path.startswith('tests/') and path.endswith('.c')
        elif language == 'cpp':
            valid = ((path.startswith('tests/') and path.endswith(('.c', '.cpp', '.inc')))
                     or path == 'tools/test.sh') # independently compiles every scosdk header
        elif language == 'csharp':
            valid = path.startswith('sdk/csharp/Sco.Sdk.Tests/') and path.endswith('.cs')
        else:
            valid = path == 'tests/test_lua.cpp' or path.startswith('tests/game_contracts/lua_')
        require(valid, 'wrong language test: ' + symbol + ' / ' + language + ' / ' + path)


def documentation_check(root, records, inventory):
    text = (root / 'docs/contracts.md').read_text(encoding='utf-8')
    rows = {}
    for line in text.splitlines():
        match = re.match(r'^\| `((?:\w+(?:\.\w+)+)|sco_plugin_\w+)` \|', line)
        if not match:
            continue
        cells = [cell.strip() for cell in line.strip().strip('|').split('|')]
        require(len(cells) == 7, 'invalid documentation row: ' + match[1])
        require(match[1] not in rows, 'duplicate documentation row: ' + match[1])
        rows[match[1]] = cells[1:]
    require(set(rows) == {r['symbol'] for r in records}, 'documentation inventory disagrees with records')
    columns = ('since', 'buffer_contract', 'ownership', 'thread_affinity', 'results', 'cleanup')
    for record in records:
        require(rows[record['symbol']] == [record[key].strip() for key in columns],
                'documentation/record contract mismatch: ' + record['symbol'])
        require(record['purpose'] == record['buffer_contract'].split('<br>', 1)[-1],
                'purpose/buffer contract mismatch: ' + record['symbol'])
    for name, table in inventory['tables'].items():
        version = str(table['version'] >> 16) + '.' + str(table['version'] & 0xffff)
        row = f"| `{name}` | {len(table['members'])} | {table['size']} | {version} |"
        require(row in text, 'documentation table inventory mismatch: ' + name)


def documentation_signatures(root, records, clang=None):
    """Compile the C declarations printed in row cells, not merely their text.
    This catches a header/record edit which leaves the human declaration stale.
    """
    clang = clang or find_clang()
    lines = ['#include "' + p.name + '"' for p in sorted((root / 'include').glob('*.h'))]
    for index, record in enumerate(records):
        declaration = re.match(r'^`([^`]+)`', record['buffer_contract'])
        require(declaration is not None, 'missing documented C declaration: ' + record['symbol'])
        declaration = declaration[1].rstrip(';') + ';'
        member = record['symbol'].split('.')[-1]
        field_path = record['symbol'].split('.', 1)[1] if record['table'] else member
        actual = f"(({record['table']}*)0)->{field_path}" if record['table'] else member
        lines += [f'void docs_signature_{index}(void) {{', declaration,
                  f'_Static_assert(__builtin_types_compatible_p(__typeof__({actual}), __typeof__({member})), "documented signature mismatch: {record["symbol"]}");', '}']
    with tempfile.TemporaryDirectory(prefix='sdk_documented_signature_') as work:
        source = Path(work) / 'signatures.c'
        source.write_text('\n'.join(lines), encoding='utf-8')
        run([clang, '-std=c11', '-I', str(root / 'include'), '-fsyntax-only', str(source)])


def validate(root, clang=None):
    inventory = header_inventory(root, clang)
    records = load_records(root)
    ids = set()
    documented = {}
    headers = {method['symbol']: method for method in inventory['methods']}
    symbols = {r['symbol'] for r in records}
    require(not (symbols - headers.keys()), 'removed symbol still documented: ' + ', '.join(sorted(symbols - headers.keys())))
    require(not (headers.keys() - symbols), 'new undocumented method: ' + ', '.join(sorted(headers.keys() - symbols)))
    for record in records:
        require(isinstance(record, dict) and set(record) == RECORD_KEYS, 'invalid record keys')
        symbol = record['symbol']
        require(isinstance(record['id'], str) and re.fullmatch(r'[a-z][a-z0-9-]*', record['id']), 'invalid stable id: ' + symbol)
        require(record['id'] not in ids, 'duplicate id: ' + record['id'])
        require(symbol not in documented, 'duplicate symbol: ' + symbol)
        ids.add(record['id'])
        documented[symbol] = record
        require(symbol in headers, 'removed symbol still documented: ' + symbol)
        for key, value in headers[symbol].items():
            require(record[key] == value, key + ' mismatch: ' + symbol)
        for key in ('since', 'purpose', 'buffer_contract', 'ownership', 'thread_affinity',
                    'callback_context', 'reentrancy', 'results', 'absent_capability', 'cleanup'):
            require(isinstance(record[key], str) and record[key].strip(), 'empty semantic field: ' + symbol + ' / ' + key)
        for key in ('examples', 'tests', 'limitations'):
            require(isinstance(record[key], list) and all(isinstance(v, str) and v for v in record[key]),
                    'invalid record array: ' + symbol + ' / ' + key)
        for path in record['examples'] + record['tests']:
            local_path(root, path)
        require(set(record['language_mappings']) == {'c', 'cpp', 'csharp', 'lua'}, 'invalid language set: ' + symbol)
        for language, mapping in record['language_mappings'].items():
            language_check(root, record, language, mapping)
    missing = headers.keys() - documented.keys()
    require(not missing, 'new undocumented method: ' + ', '.join(sorted(missing)))
    documentation_check(root, records, inventory)
    documentation_signatures(root, records, clang)
    return sorted(records, key=lambda r: r['id']), inventory


def contract_context(root, inventory):
    """Carry common and per-service prose into the machine catalogue, without a
    second editable copy of those cross-method guarantees and limitations.
    """
    text = (root / 'docs/contracts.md').read_text(encoding='utf-8')
    sections = {}
    for match in re.finditer(r'^## ([^\n]+)\n(.*?)(?=^## |\Z)', text, re.M | re.S):
        sections[match[1]] = match[2].strip()
    for table in inventory['tables']:
        matches = [body for title, body in sections.items() if f'`{table}`' in title]
        require(len(matches) == 1, 'service context missing: ' + table)
        inventory['tables'][table]['context'] = matches[0].split('\n| Method |', 1)[0].strip()
    return {'source': 'docs/contracts.md',
            'reading_rules': sections['Reading the rows'],
            'entrypoint_context': sections['Native plugin entrypoints'].split('\n| Method |', 1)[0].strip(),
            'remaining_gaps': sections['Explicit remaining gaps']}


def build(root, source_sha, clang=None):
    require(re.fullmatch(r'[0-9a-f]{40}', source_sha) is not None,
            '--source-sha must be an explicit full lowercase 40-hex commit')
    records, inventory = validate(root, clang)
    context = contract_context(root, inventory)
    return {'schema_version': SCHEMA_VERSION, 'sdk_version': (root / 'sdk/VERSION').read_text().strip(),
            'game_pack_version': inventory['game_pack_version'], 'source_sha': source_sha,
            'contract_context': context, 'tables': inventory['tables'], 'records': records}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    subs = parser.add_subparsers(dest='command', required=True)
    for name in ('extract', 'check', 'build'):
        sub = subs.add_parser(name)
        sub.add_argument('--root', type=Path, default=ROOT)
        sub.add_argument('--clang', help='Clang executable (default CLANG or PATH clang)')
        if name == 'build':
            sub.add_argument('--out', type=Path, required=True)
            sub.add_argument('--source-sha', required=True)
    args = parser.parse_args(argv)
    try:
        if args.command == 'build':
            result = build(args.root, args.source_sha, args.clang)
            args.out.parent.mkdir(parents=True, exist_ok=True)
            args.out.write_text(encoded(result), encoding='utf-8')
            print('sdk_catalogue: built ' + str(args.out))
        else:
            records, inventory = validate(args.root, args.clang)
            if args.command == 'extract':
                print(encoded(inventory), end='')
            else:
                print(f"sdk_catalogue: {len(records)} records = {len(inventory['methods'])} header methods; {len(inventory['tables'])} tables")
        return 0
    except (CatalogueError, OSError, ValueError, KeyError, TypeError) as error:
        print('sdk_catalogue: FAIL ' + str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
