#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Freeze catalogue/examples against exact SDK archives; never approve or publish.

Git and the source revision's stdlib sdk/package.py are used read-only. No signed
artifact is regenerated on disk. Verification requires an independently pinned
inventory hash, not a checksum supplied only by the snapshot being checked.
"""
import argparse
from contextlib import contextmanager
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE_URL = 'https://github.com/scuba-sc/sco-core/blob/'


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def encoded(value):
    return (json.dumps(value, sort_keys=True, indent=2, ensure_ascii=True) + '\n').encode('ascii')


def safe_path(name):
    require(isinstance(name, str) and bool(name) and not name.startswith('/')
            and not any(p in ('', '.', '..') for p in name.split('/'))
            and not any(c in name for c in ('\\', ':', '\n', '\r', '\x00')),
            'unsafe relative path')
    return name


@contextmanager
def source_tree(root, sha):
    require(isinstance(sha, str) and re.fullmatch('[0-9a-f]{40}', sha),
            'source must be a full lowercase 40-hex commit, not a floating ref')
    resolved = subprocess.check_output(['git', '-C', str(root), 'rev-parse', sha + '^{commit}'],
                                       stderr=subprocess.PIPE, text=True).strip()
    require(resolved == sha, 'source must identify the exact commit object')
    archive = subprocess.check_output(['git', '-c', 'core.autocrlf=false', '-c', 'core.eol=lf',
                                       '-C', str(root), 'archive', '--format=tar', sha])
    tree = subprocess.check_output(['git', '-C', str(root), 'ls-tree', '-rz', '--full-tree', sha])
    objects = {}
    for entry in tree.split(b'\0'):
        if not entry:
            continue
        metadata, name = entry.split(b'\t', 1)
        mode, kind, object_sha = metadata.decode('ascii').split()
        require(kind == 'blob' and mode in ('100644', '100755'), 'source contains non-regular Git entry')
        objects[safe_path(name.decode('utf-8'))] = (object_sha, mode == '100755')
    seen = set()
    with tempfile.TemporaryDirectory(prefix='reference-source-', dir=os.environ.get('TMPDIR')) as temp:
        target = Path(temp)
        # Only repository files, never symlinks, hardlinks, devices or extraction paths.
        with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
            for member in tar:
                name = safe_path(member.name.rstrip('/'))
                path = target / name
                if member.isdir():
                    path.mkdir(parents=True, exist_ok=True)
                else:
                    require(member.isfile(), 'source contains non-regular input: ' + name)
                    path.parent.mkdir(parents=True, exist_ok=True)
                    stream = tar.extractfile(member)
                    if stream is None:
                        raise ValueError('unreadable source member')
                    with path.open('wb') as output:
                        shutil.copyfileobj(stream, output)
                    path.chmod(member.mode & 0o777)
                    data = path.read_bytes()
                    blob_sha = hashlib.sha1(b'blob ' + str(len(data)).encode('ascii') + b'\0' + data).hexdigest()
                    require(name not in seen and objects.get(name) == (blob_sha, bool(member.mode & 0o111)),
                            'Git blob mismatch: ' + name)
                    seen.add(name)
        require(seen == set(objects), 'Git export omits committed source files')
        yield target


def module_at(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise ValueError('source tool unavailable')
    module = importlib.util.module_from_spec(spec)
    previous = sys.dont_write_bytecode
    sys.dont_write_bytecode = True
    try:
        spec.loader.exec_module(module)
    finally:
        sys.dont_write_bytecode = previous
    return module


def package_module(source, sha):
    package = module_at(source / 'sdk/package.py', 'snapshot_package')
    # The extracted tree deliberately has no .git. Every metadata/link call must
    # use the already resolved commit, never the current checkout's HEAD.
    setattr(package, 'source_commit', lambda: sha)
    return package


def archive_contents(data, top, expected):
    content = {}
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        for info in archive.infolist():
            name = safe_path(info.filename)
            require(not info.is_dir() and name.startswith(top + '/'), 'unexpected archive root/member')
            relative = name[len(top) + 1:]
            # Reject unlisted/oversized members before decompression, using the
            # exact committed package inventory rather than attacker ZIP limits.
            require(relative in expected and info.file_size == len(expected[relative][0]),
                    'archive inputs/metadata mismatch: undeclared member or size')
            require(relative not in content, 'duplicate archive member: ' + relative)
            mode = info.external_attr >> 16
            require((mode & 0o170000) in (0, 0o100000), 'non-regular archive member')
            content[relative] = (archive.read(info), bool(mode & 0o111))
    return content


def validate_catalogue(source, sha, catalogue, package):
    require(set(catalogue) == {'schema_version', 'source_sha', 'sdk_version', 'game_pack_version',
                               'records', 'tables', 'contract_context'}, 'catalogue fields mismatch')
    require(catalogue.get('schema_version') == 1 and catalogue.get('source_sha') == sha,
            'catalogue source/schema mismatch')
    require(catalogue.get('sdk_version') == package.sdk_version()
            and catalogue.get('game_pack_version') == package.game_pack()[0], 'catalogue version mismatch')
    records = []
    for path in sorted((source / 'sdk/contracts').glob('*.json')):
        document = json.loads(path.read_bytes())
        require(document.get('schema_version') == 1, 'contract schema mismatch')
        records.extend(document['records'])
    require(catalogue.get('records') == sorted(records, key=lambda r: r['id']),
            'catalogue records/signature differ from exact source contracts')
    # Reproduce the generator's context selection without compiler/tool imports.
    text = (source / 'docs/contracts.md').read_text(encoding='utf-8')
    sections = {m[1]: m[2].strip() for m in re.finditer(
        r'^## ([^\n]+)\n(.*?)(?=^## |\Z)', text, re.M | re.S)}
    context = {'source': 'docs/contracts.md', 'reading_rules': sections['Reading the rows'],
               'entrypoint_context': sections['Native plugin entrypoints'].split('\n| Method |', 1)[0].strip(),
               'remaining_gaps': sections['Explicit remaining gaps']}
    require(catalogue.get('contract_context') == context, 'catalogue context differs from source')
    methods = [r for r in records if r['kind'] == 'table-method']
    require(set(catalogue['tables']) == {r['table'] for r in methods}, 'catalogue table inventory mismatch')
    for name, table in catalogue['tables'].items():
        require(set(table) == {'context', 'members', 'service', 'size', 'source_header', 'version', 'version_macro'},
                'catalogue table fields mismatch')
        members = [r['symbol'].split('.')[-1] for r in sorted(
            (r for r in methods if r['table'] == name), key=lambda r: r['member_extent'])]
        require(table['members'] == members, 'catalogue table member inventory mismatch')
        header = (source / safe_path(table['source_header'])).read_text(encoding='utf-8')
        def literal(macro):
            values = re.findall(r'^#define ' + re.escape(macro) + r' (0x[0-9a-fA-F]+|[0-9]+)[uU]?(?=\s|$)',
                                header, re.M)
            require(len(values) == 1, 'version must be a unique source integer literal: ' + macro)
            return int(values[0], 16 if values[0].startswith('0x') else 10)
        version = ((literal('SCO_API_MAJOR') << 16) | literal('SCO_API_MINOR')
                   if table['version_macro'] == 'SCO_API_MAJOR/SCO_API_MINOR'
                   else literal(table['version_macro']))
        require(type(table['version']) is int and table['version'] == version, 'catalogue table version mismatch')
        bodies = [body for title, body in sections.items() if f'`{name}`' in title]
        require(len(bodies) == 1 and table.get('context') == bodies[0].split('\n| Method |', 1)[0].strip(),
                'catalogue table context differs from source')
    for record in records:
        if record['kind'] == 'table-method':
            table = catalogue['tables'][record['table']]
            require(all(table[key] == record[field] for key, field in (
                ('size', 'table_size'), ('source_header', 'source_header'), ('service', 'service'),
                ('version_macro', 'version_macro'))), 'catalogue table metadata differs from contracts')
            require(record['symbol'].split('.')[-1] in table['members'], 'catalogue table member missing')
    for ref in re.findall(r'https://github\.com/scuba-sc/sco-core/(?:blob|tree)/([^/\s)]+)',
                          json.dumps(catalogue)):
        require(ref == sha, 'floating or unrelated source link')
    return records


def plan(root, sha, catalogue_path, archives, channel):
    require(channel in ('preview', 'candidate'), 'unknown snapshot channel')
    roles = set(archives)
    require(bool(roles) and roles <= {'core', 'game-sc', 'core-source', 'legacy'}, 'invalid archive selection')
    require(roles in ({'core'}, {'core', 'game-sc'}, {'core-source'}, {'legacy'}),
            'select one base package, with game-sc only over core')
    catalogue_bytes = Path(catalogue_path).read_bytes()
    catalogue = json.loads(catalogue_bytes)
    files = {'catalogue.json': catalogue_bytes}
    archive_records, examples = [], []
    with source_tree(root, sha) as source:
        package = package_module(source, sha)
        manifest = package.load_manifest(str(source / 'sdk/components.json'), str(source))
        records = validate_catalogue(source, sha, catalogue, package)
        package_inputs = {'sdk/package.py', 'sdk/components.json', 'sdk/VERSION', 'include/sco_api.h',
                          'include/sc_game_pack.h', 'docs/legal/provenance.json'}
        inputs = package_inputs | {'tools/sdk_catalogue.py', 'tools/abi_snapshot.py', 'docs/contracts.md'}
        inputs |= {p.relative_to(source).as_posix() for p in (source / 'sdk/contracts').glob('*.json')}
        for record in records:
            inputs.add(record['source_header'])
            inputs.update(record['tests'])
            for mapping in record['language_mappings'].values():
                inputs.update(evidence['path'] for evidence in mapping['evidence'])
        shipped = {}
        selected_hashes = {}
        for profile, path in sorted(archives.items()):
            # Compare every member, including compatibility, attribution, SBOM,
            # versions and SHA256SUMS, to package.py's exact source-derived bytes.
            # ZIP compression may vary; the supplied archive's own hash is pinned.
            top, expected = package.archive_entries(manifest, profile)
            archive_data = Path(path).read_bytes()
            actual = archive_contents(archive_data, top, expected)
            require(actual == expected, 'archive inputs/metadata mismatch: ' + profile)
            sha256 = digest(archive_data)
            selected_hashes[profile] = sha256
            metadata = package.metadata_names(manifest, profile)
            descriptor = json.loads(actual[metadata[2]][0])
            archive_records.append({'profile': profile, 'filename': top + '.zip', 'sha256': sha256,
                                    'compatibility': descriptor,
                                    'metadata_sha256': {n: digest(actual[n][0]) for n in metadata}})
            for component in package.select(manifest, profile):
                name = component['source']
                inputs.add(name)
                shipped.setdefault(name, []).append({'profile': profile,
                    'path': package.destination(manifest, profile, component),
                    'sha256': digest(actual[package.destination(manifest, profile, component)][0])})
        if 'game-sc' in roles:
            overlay = next(a for a in archive_records if a['profile'] == 'game-sc')
            require(overlay['compatibility']['base_core_sha256'] == selected_hashes['core'],
                    'overlay does not bind the supplied core archive')
        for name in sorted({name for record in records for name in record['examples']}):
            safe_path(name)
            require(name.startswith('sdk/examples/'), 'example outside permitted source directory')
            path = source / name
            require(path.is_file() and not path.is_symlink(), 'example missing from exact source: ' + name)
            data = path.read_bytes()
            locations = shipped.get(name, [])
            require(all(location['sha256'] == digest(data) for location in locations),
                    'archive-shipped example differs from source: ' + name)
            files['examples/' + name] = data
            inputs.add(name)
            examples.append({'source': name, 'source_url': SOURCE_URL + sha + '/' + name,
                             'sha256': digest(data), 'shipped_in': locations,
                             'compile_status': 'not-checked'})
        input_records = [{'path': safe_path(name), 'sha256': digest((source / name).read_bytes())}
                         for name in sorted(inputs)]
        files['inputs.json'] = encoded({'schema_version': 1, 'source_sha': sha, 'files': input_records})
        coordinates = {'sdk_version': package.sdk_version(), 'game_pack_version': package.game_pack()[0],
                       'source_sha': sha}
    inventory = {'schema_version': 1, 'coordinates': coordinates, 'channel': channel,
                 'publication_status': 'not-approved', 'archives': archive_records, 'examples': examples,
                 'files': [{'path': name, 'sha256': digest(data), 'size': len(data)}
                           for name, data in sorted(files.items())]}
    files['inventory.json'] = encoded(inventory)
    return inventory, files


def create(root, sha, catalogue, archives, out, channel='preview'):
    inventory, files = plan(root, sha, catalogue, archives, channel)
    coords = inventory['coordinates']
    target = Path(out) / ('previews' if channel == 'preview' else 'candidates') / (
        'sdk-' + coords['sdk_version']) / ('game-' + coords['game_pack_version']) / sha
    require(not target.exists() and not target.is_symlink(), 'snapshot already exists; refusing regeneration')
    target.mkdir(parents=True, exist_ok=False)
    try:
        for name, data in files.items():
            path = target / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
    except Exception:
        shutil.rmtree(target)
        raise
    return target


def verify(root, target, archives, inventory_sha256):
    require(isinstance(inventory_sha256, str) and re.fullmatch('[0-9a-f]{64}', inventory_sha256),
            'an independently pinned inventory SHA-256 is required')
    target = Path(target)
    require(not target.is_symlink(), 'snapshot root must not be a symlink')
    raw = (target / 'inventory.json').read_bytes()
    require(digest(raw) == inventory_sha256, 'pinned inventory hash mismatch')
    inventory = json.loads(raw)
    expected_inventory, expected_files = plan(root, inventory['coordinates']['source_sha'],
        target / 'catalogue.json', archives, inventory['channel'])
    require(inventory == expected_inventory and raw == encoded(expected_inventory), 'snapshot inventory mismatch')
    actual_files = {}
    for path in target.rglob('*'):
        require(not path.is_symlink(), 'snapshot contains a symlink')
        if path.is_dir():
            continue
        require(path.is_file(), 'snapshot contains non-regular file')
        actual_files[path.relative_to(target).as_posix()] = path.read_bytes()
    require(actual_files == expected_files, 'snapshot file set/bytes mismatch')
    return inventory


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    for command in ('create', 'verify'):
        item = sub.add_parser(command)
        item.add_argument('--root', type=Path, default=ROOT)
        item.add_argument('--archive', action='append', required=True, metavar='PROFILE=ZIP')
        if command == 'create':
            item.add_argument('--source-sha', required=True)
            item.add_argument('--catalogue', type=Path, required=True)
            item.add_argument('--out', type=Path, required=True)
            item.add_argument('--channel', choices=('preview', 'candidate'), default='preview')
        else:
            item.add_argument('--snapshot', type=Path, required=True)
            item.add_argument('--inventory-sha256', required=True)
    args = parser.parse_args(argv)
    try:
        archives = {}
        for argument in args.archive:
            role, separator, path = argument.partition('=')
            require(separator and path and role not in archives, 'expected unique PROFILE=ZIP arguments')
            archives[role] = Path(path)
        if args.command == 'create':
            target = create(args.root, args.source_sha, args.catalogue, archives, args.out, args.channel)
            print('reference snapshot: created ' + str(target))
            print('inventory sha256: ' + digest((target / 'inventory.json').read_bytes()))
            print('publication: not approved; example compilation not attested')
        else:
            inventory = verify(args.root, args.snapshot, archives, args.inventory_sha256)
            print('reference snapshot: verified ' + inventory['coordinates']['source_sha'])
            print('publication: not approved; example compilation not attested')
        return 0
    except (OSError, ValueError, KeyError, TypeError, AttributeError, subprocess.CalledProcessError,
            zipfile.BadZipFile, tarfile.TarError) as error:
        print('reference snapshot: FAIL ' + str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
