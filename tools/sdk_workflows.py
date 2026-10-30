#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build a bounded, embedded SDK workflow index from an explicit public manifest."""
import argparse
import hashlib
import types
import json
from pathlib import Path, PurePosixPath
import re
import subprocess

MANIFEST = 'sdk/skills/manifest.json'
MAX_FILE = 64 * 1024
MAX_INDEX = 2 * 1024 * 1024


def encoded(value):
    return (json.dumps(value, sort_keys=True, indent=2, ensure_ascii=True) + '\n').encode('ascii')


def digest(data):
    return hashlib.sha256(data).hexdigest()


def safe_path(value):
    if (not isinstance(value, str) or not value or len(value) > 256 or
            not re.fullmatch(r'[a-zA-Z0-9_./-]+', value) or
            any(p in ('', '.', '..') for p in value.split('/')) or
            PurePosixPath(value).is_absolute()):
        raise ValueError('Invalid workflow path')
    return value


def read_public(root, name):
    safe_path(name)
    target = root
    for part in name.split('/'):
        target = target / part
        if part not in {p.name for p in target.parent.iterdir()}:
            raise ValueError('Missing or case-mismatched workflow input: ' + name)
        if target.is_symlink():
            raise ValueError('Symlink in workflow input: ' + name)
    if not target.is_file() or target.stat().st_size > MAX_FILE:
        raise ValueError('Missing, nonregular or oversized workflow input: ' + name)
    return target.read_bytes()


def manifest_files(raw):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError('Duplicate workflow manifest key')
            result[key] = value
        return result
    data = json.loads(raw, object_pairs_hook=unique)
    if (not isinstance(data, dict) or set(data) != {'schema_version', 'bundle_version', 'baseline_sdk', 'skills', 'mcp'} or
            type(data['schema_version']) is not int or data['schema_version'] != 1 or
            not all(isinstance(data[k], str) and re.fullmatch(r'\d+\.\d+\.\d+', data[k])
                    for k in ('bundle_version', 'baseline_sdk')) or
            not isinstance(data['skills'], list) or not 1 <= len(data['skills']) <= 32):
        raise ValueError('Invalid workflow manifest')
    mcp = data['mcp']
    if (not isinstance(mcp, dict) or set(mcp) != {'protocol', 'server_version', 'tools'} or
            not isinstance(mcp['protocol'], str) or not re.fullmatch(r'\d{4}-\d{2}-\d{2}', mcp['protocol']) or
            not isinstance(mcp['server_version'], str) or not re.fullmatch(r'\d+\.\d+\.\d+', mcp['server_version']) or
            not isinstance(mcp['tools'], dict) or not mcp['tools'] or
            any(not re.fullmatch(r'[a-z][a-z_]+', k) or not isinstance(v, str) or
                not re.fullmatch(r'[0-9a-f]{64}', v) for k, v in mcp['tools'].items())):
        raise ValueError('Invalid MCP workflow contract')
    result = []
    seen = set()
    for item in data['skills']:
        if not isinstance(item, dict) or set(item) != {'id', 'resources'}:
            raise ValueError('Invalid workflow declaration')
        key = item['id']
        if not isinstance(key, str) or not re.fullmatch(r'sco-[a-z0-9-]{1,60}', key) or key in seen:
            raise ValueError('Invalid or duplicate workflow ID')
        seen.add(key)
        if not isinstance(item['resources'], list) or len(item['resources']) > 15:
            raise ValueError('Invalid workflow resources')
        names = ['SKILL.md'] + item['resources']
        if len(set(names)) != len(names):
            raise ValueError('Duplicate workflow resource')
        for name in names:
            safe_path(name)
            if name != 'SKILL.md' and (not name.startswith('references/') or not name.endswith('.md')):
                raise ValueError('Only declared Markdown references may be indexed')
        result.append((key, ['sdk/skills/' + key + '/' + name for name in names]))
    return data, result


BLOCK_PATH = 'sdk/skills/sco-build-check-package/references/inputs.md'
BLOCK_START = '<!-- sdk-workflows:mcp:start -->'
BLOCK_END = '<!-- sdk-workflows:mcp:end -->'


def mcp_contract(root, manifest):
    # Contributor/package-time only: import the selected trusted checkout, never
    # execute workflow text or accept a module path from an MCP caller. Compile
    # exact bytes so timestamp-based .pyc caching cannot hide schema mutations.
    path = Path(root) / 'tools/sco-core-mcp/server.py'
    server = types.ModuleType('workflow_mcp_contract')
    server.__file__ = str(path)
    exec(compile(path.read_bytes(), str(path), 'exec'), server.__dict__)
    tools = {}
    for name in manifest['mcp']['tools']:
        if name not in server.TOOL_INDEX:
            raise ValueError('Missing MCP tool: ' + name)
        tools[name] = server.TOOL_INDEX[name]['inputSchema']
    return server, tools


def contract_block(manifest, tools):
    lines = [BLOCK_START, '## Bundled MCP contract (generated)', '',
             'This bundle targets server ' + manifest['mcp']['server_version'] +
             ', protocol ' + manifest['mcp']['protocol'] + '.',
             'Inspect negotiated schemas for other kits/profiles; this is not runtime availability.', '',
             '| Tool | Arguments (* required) |', '| --- | --- |']
    for name, schema in tools.items():
        arguments = ', '.join('`' + key + '`' + ('*' if key in schema['required'] else '')
                              for key in schema['properties']) or '(none)'
        lines.append('| `' + name + '` | ' + arguments + ' |')
    return '\n'.join(lines + ['', BLOCK_END])


def block_extent(text):
    if (text.count(BLOCK_START) != 1 or text.count(BLOCK_END) != 1 or
            text.index(BLOCK_END) < text.index(BLOCK_START)):
        raise ValueError('Malformed generated MCP markers')
    return text.index(BLOCK_START), text.index(BLOCK_END) + len(BLOCK_END)


def validate_mcp(root, manifest, texts):
    server, tools = mcp_contract(root, manifest)
    expected = manifest['mcp']
    if (expected['protocol'] != server.PROTOCOL_VERSION or
            expected['server_version'] != server.SERVER_VERSION):
        raise ValueError('MCP version drift; regenerate workflow contract')
    for name, schema in tools.items():
        if expected['tools'][name] != digest(encoded(schema)):
            raise ValueError('MCP input schema drift: ' + name)
    block = contract_block(manifest, tools)
    text = texts[BLOCK_PATH].replace('\r\n', '\n')
    start, end = block_extent(text)
    if block != text[start:end]:
        raise ValueError('Stale generated MCP block; run tools/sdk_workflows.py --generate')
    for name, text in texts.items():
        for tool_name, arguments in re.findall(r'`([a-z_]+)\(([^`()]*)\)`', text):
            if tool_name not in tools:
                raise ValueError('Unbound MCP tool reference: ' + tool_name)
            for argument in arguments.split(','):
                argument = argument.strip()
                if argument and argument not in tools[tool_name]['properties']:
                    raise ValueError('Unknown MCP argument: ' + tool_name + '.' + argument)
        for reference in re.findall(r'`((?:get|list|search|check|validate|generate|run|build|test)_[a-z_]+)`', text):
            if reference not in tools:
                raise ValueError('Unbound MCP tool reference in ' + name + ': ' + reference)


def generate(root):
    root = Path(root)
    manifest, _ = manifest_files(read_public(root, MANIFEST))
    server, tools = mcp_contract(root, manifest)
    manifest['mcp'] = {'protocol': server.PROTOCOL_VERSION, 'server_version': server.SERVER_VERSION,
                       'tools': {name: digest(encoded(schema)) for name, schema in tools.items()}}
    path = root / BLOCK_PATH
    text = read_public(root, BLOCK_PATH).decode('utf-8')
    newline = '\r\n' if '\r\n' in text else '\n'
    block = contract_block(manifest, tools).replace('\n', newline)
    if BLOCK_START not in text and BLOCK_END not in text:
        text = text.replace('## Receipt template', block + newline + newline + '## Receipt template')
    else:
        start, end = block_extent(text)
        text = text[:start] + block + text[end:]
    (root / MANIFEST).write_bytes(encoded(manifest))
    path.write_bytes(text.encode('utf-8'))


def build(root, source_sha, paths=None, entries=None):
    """Use transformed archive entries when supplied, so hashes describe shipped bytes."""
    root = Path(root).resolve()
    if not isinstance(source_sha, str) or not re.fullmatch(r'[0-9a-f]{40}', source_sha):
        raise ValueError('Workflow source must be a full Git SHA')
    raw = read_public(root, MANIFEST)
    manifest, declarations = manifest_files(raw)
    names = [MANIFEST] + [name for _, files in declarations for name in files]
    if paths is None:
        paths = {name: name for name in names}
    if any(name not in paths for name in names):
        raise ValueError('Workflow profile omits declared files')
    if len({paths[name].casefold() for name in names}) != len(names):
        raise ValueError('Workflow destination collision')
    for name in names:
        safe_path(paths[name])
    def content(name):
        original = read_public(root, name)
        data = original if entries is None else entries[paths[name]][0]
        if len(data) > MAX_FILE:
            raise ValueError('Oversized workflow text')
        return data
    validate_mcp(root, manifest, {name: content(name).decode('utf-8')
                                  for _, files in declarations for name in files})
    workflows = []
    for key, files in declarations:
        text = content(files[0]).decode('utf-8')
        # Accept checkout line endings without altering the embedded text or its byte hash.
        front_text = text.replace('\r\n', '\n')
        front = re.match(r'\A---\nname: ([a-z0-9-]+)\ndescription: ("[^\n]+")\nmetadata:\n  version: "([^"\n]+)"\n---\n', front_text)
        if not front or front[1] != key or front[3] != manifest['bundle_version']:
            raise ValueError('Workflow frontmatter/version mismatch: ' + key)
        description = json.loads(front[2])
        if not isinstance(description, str) or not 1 <= len(description) <= 1024:
            raise ValueError('Invalid workflow description')
        records = []
        for name in files:
            data = content(name)
            records.append({'path': paths[name], 'sha256': digest(data), 'text': data.decode('utf-8')})
        workflows.append({'id': key, 'description': description, 'entry': paths[files[0]], 'files': records})
    # All local Markdown references must resolve within the explicit workflow bundle.
    from posixpath import normpath, dirname, join
    shipped = {paths[name] for name in names}
    for workflow in workflows:
        for record in workflow['files']:
            for target in re.findall(r'\]\(([^)\s]+)\)', record['text']):
                if ':' in target or target.startswith('#'):
                    continue
                dest = normpath(join(dirname(record['path']), target.split('#')[0]))
                if dest not in shipped:
                    raise ValueError('Undeclared workflow link: ' + target)
    result = {'schema_version': 1, 'bundle_version': manifest['bundle_version'],
              'baseline_sdk': manifest['baseline_sdk'], 'sdk_version': (root / 'sdk/VERSION').read_text().strip(),
              'source_sha': source_sha, 'manifest_path': paths[MANIFEST],
              'manifest_sha256': digest(content(MANIFEST)), 'workflows': workflows,
              'content_sha256': digest(encoded(workflows))}
    if len(encoded(result)) > MAX_INDEX:
        raise ValueError('Workflow index exceeds 2 MiB')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--out', type=Path)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--check', action='store_true', help='validate contracts and generated blocks without writing (default)')
    mode.add_argument('--generate', action='store_true', help='refresh MCP schema pins and the generated reference block')
    args = parser.parse_args()
    if args.check and args.out:
        parser.error('--check cannot be combined with --out')
    try:
        if args.generate:
            generate(args.root)
        sha = subprocess.check_output(['git', '-C', str(args.root), 'rev-parse', 'HEAD'], text=True).strip()
        data = encoded(build(args.root, sha))
        if args.out:
            args.out.write_bytes(data)
        else:
            print('SDK workflows valid; ' + str(len(data)) + ' index bytes')
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, 'sdk_workflows: ' + str(error) + '\n')


if __name__ == '__main__':
    main()
