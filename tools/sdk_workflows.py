#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build a bounded, embedded SDK workflow index from an explicit public manifest."""
import argparse
import hashlib
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
    if (not isinstance(data, dict) or set(data) != {'schema_version', 'bundle_version', 'baseline_sdk', 'skills'} or
            type(data['schema_version']) is not int or data['schema_version'] != 1 or
            not all(isinstance(data[k], str) and re.fullmatch(r'\d+\.\d+\.\d+', data[k])
                    for k in ('bundle_version', 'baseline_sdk')) or
            not isinstance(data['skills'], list) or not 1 <= len(data['skills']) <= 32):
        raise ValueError('Invalid workflow manifest')
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
    workflows = []
    for key, files in declarations:
        text = content(files[0]).decode('utf-8')
        front = re.match(r'\A---\nname: ([a-z0-9-]+)\ndescription: ("[^\n]+")\nmetadata:\n  version: "([^"\n]+)"\n---\n', text)
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
    args = parser.parse_args()
    try:
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
