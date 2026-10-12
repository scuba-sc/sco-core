#!/usr/bin/env python3
"""Render schema-1 SDK contracts and explicitly selected public engineering docs.

No source files, examples, tests or unlisted docs are copied into the site.
Source evidence is linked at the catalogue's immutable commit.
"""
import argparse
import html
import json
from pathlib import Path, PurePosixPath
import posixpath
import re
import shutil
import subprocess
from urllib.parse import quote, urlsplit

ROOT = Path(__file__).resolve().parents[2]
BASE = '/sco-core'
SOURCE_ROOT = 'https://github.com/scubamount/sco-core/blob/'
STATUSES = {'direct', 'typed-wrapper', 'restricted', 'unsupported'}
LANGUAGES = ('c', 'cpp', 'csharp', 'lua')
# Editorial task phrases, not API aliases or claims of additional entrypoints.
TASK_TERMS = {
    'sco-api-run-on-game-thread': 'schedule game thread task',
    'sco-storage-v1-query': 'open SQL cursor',
    'sco-api-query-service': 'find service table',
    'sc-actors-v1-spawn-npc': 'spawn NPC',
}
LINK = re.compile(r'(?P<image>!?)\[(?P<label>[^\]\n]*)\]\((?P<target>[^\s)]+)\)')


def route(identifier):
    return BASE + '/' + (identifier.rstrip('/') + '/' if identifier != 'index' else '')


def service_id(service):
    if not re.fullmatch(r'[a-z0-9_.-]+', service):
        raise ValueError('Invalid service identifier')
    return service.replace('.', '-')


def source_link(path, sha):
    path = PurePosixPath(path)
    if path.is_absolute() or '..' in path.parts or not path.parts or '\\' in str(path):
        raise ValueError('Source paths must be repository-relative')
    return SOURCE_ROOT + sha + '/' + quote(str(path), safe='/')


def inputs(root):
    data = json.loads((root / 'site/public-inputs.json').read_text(encoding='utf-8'))
    if data.get('schema_version') != 1 or set(data) != {'schema_version', 'docs'}:
        raise ValueError('Invalid public-input allowlist')
    docs = data['docs']
    if not isinstance(docs, list) or not docs or len(docs) != len(set(docs)):
        raise ValueError('Allowlist must contain unique explicit paths')
    for name in docs:
        path = PurePosixPath(name)
        if (not isinstance(name, str) or not name.startswith('docs/') or path.suffix != '.md'
                or '..' in path.parts or '\\' in name or '*' in name
                or path.parts[1] in {'legal', 'design', 'releases'}):
            raise ValueError('Only explicit public engineering docs may be rendered')
        real = (root / name).resolve()
        if not real.is_relative_to((root / 'docs').resolve()) or not real.is_file():
            raise ValueError('Missing input or path outside docs')
    return sorted(docs)


def guide_id(path):
    return 'guides/' + str(PurePosixPath(path).relative_to('docs').with_suffix('')).lower()


def rewrite_links(text, source, sha, doc_routes):
    """Resolve selected docs locally; other evidence becomes a pinned source link."""
    def replace(match):
        target = match['target']
        url = urlsplit(target)
        if target.startswith('#') or url.scheme or url.netloc:
            if target.startswith(SOURCE_ROOT):
                suffix = target[len(SOURCE_ROOT):].split('/', 1)
                if len(suffix) == 2:
                    target = source_link(suffix[1].split('#', 1)[0], sha) + ('#' + url.fragment if url.fragment else '')
            return match['image'] + '[' + match['label'] + '](' + target + ')'
        path = posixpath.normpath(posixpath.join(posixpath.dirname(source), url.path))
        if path.startswith('../') or path.startswith('/'):
            raise ValueError('Link escapes repository: ' + target)
        if path in doc_routes and not match['image']:
            # Original document heading anchors are not stable after Starlight
            # formatting; link to the generated page, which has its own TOC.
            target = route(doc_routes[path])
        else:
            target = source_link(path, sha) + ('#' + url.fragment if url.fragment else '')
        # Do not fetch or embed images from a non-allowlisted repository path.
        return '[' + match['label'] + '](' + target + ')'
    return LINK.sub(replace, text)


def banner(catalogue, release_tag):
    label = 'release ' + release_tag if release_tag else 'development preview'
    return (':::caution[Catalogue snapshot — ' + label + ']\n'
            + 'SDK **' + catalogue['sdk_version'] + '** · game pack **' + catalogue['game_pack_version']
            + '** · source `' + catalogue['source_sha'] + '`\n'
            + 'Source-backed inventory is not a runtime or in-game verification claim.\n:::\n\n')


def page(title, body, catalogue, release_tag):
    return ('---\ntitle: ' + json.dumps(title, ensure_ascii=False)
            + '\ndescription: ' + json.dumps('sco-core public engineering documentation: ' + title)
            + '\n---\n\n' + banner(catalogue, release_tag) + body.rstrip() + '\n')


def cell(value):
    return html.escape(str(value), quote=False).replace('|', '&#124;').replace('\n', '<br>')


def mapping_rows(record, sha):
    rows = ['| Language | Catalogue status | Source evidence / wrapper tokens |',
            '| --- | --- | --- |']
    if set(record['language_mappings']) != set(LANGUAGES):
        raise ValueError('Missing or unknown language mapping')
    for language in LANGUAGES:
        mapping = record['language_mappings'][language]
        if mapping['status'] not in STATUSES:
            raise ValueError('Unknown language status')
        evidence = []
        for item in mapping['evidence']:
            evidence.append('[' + cell(item['path']) + '](' + source_link(item['path'], sha)
                            + ')<br><code>' + cell(item['token']) + '</code>')
        rows.append('| ' + language + ' | **' + mapping['status'] + '** | ' + '<br>'.join(evidence) + ' |')
    return '\n'.join(rows) + '\n'


def render_record(record, catalogue, doc_routes):
    sha = catalogue['source_sha']
    prose = lambda text: rewrite_links(text, catalogue['contract_context']['source'], sha, doc_routes)
    table = catalogue['tables'].get(record['table'], {})
    out = [prose(record['purpose']), '\n:::danger[Limitations / unestablished guarantees]',
           *['- ' + prose(item) for item in record['limitations']], ':::\n',
           '## Signature', '```c', record['signature'], '```',
           '## Service and availability',
           '[Service index](' + route('services/' + service_id(record['service'])) + ')',
           '| Property | Value |', '| --- | --- |',
           '| Symbol | <code>' + cell(record['symbol']) + '</code> |',
           '| Record ID | <code>' + cell(record['id']) + '</code> |',
           '| Service | ' + cell(record['service']) + ' |',
           '| Table | ' + cell(record['table'] or 'Native entrypoint (no table)') + ' |',
           '| Version macro | ' + cell(record['version_macro']) + ' |',
           '| Packed table version | ' + cell(table.get('version', 'Not applicable')) + ' |',
           '| Since / availability | ' + cell(record['since']) + ' |',
           '| Complete member extent (x64 bytes) | ' + cell(record['member_extent']) + ' |',
           '| Table size (x64 bytes) | ' + cell(record['table_size']) + ' |',
           '\n[Header source](' + source_link(record['source_header'], sha) + ')',
           '\n## Parameters and buffers', prose(record['buffer_contract']),
           '\n## Results and failures', prose(record['results']),
           '\n## Absent capability', prose(record['absent_capability']),
           '\n## Ownership', prose(record['ownership']),
           '\n## Thread affinity', prose(record['thread_affinity']),
           '\n## Callback context', prose(record['callback_context']),
           '\n## Reentrancy', prose(record['reentrancy']),
           '\n## Cleanup and cancellation', prose(record['cleanup']),
           '\n## Language mappings',
           'Status labels are copied verbatim from the catalogue: **direct** is native access; '
           '**typed-wrapper** is wrapper-backed; **restricted** is a subset/adaptation; '
           '**unsupported** means no supported route is established. No status implies universal safety.',
           mapping_rows(record, sha)]
    for key in ('examples', 'tests'):
        out.append('\n## ' + key.title())
        out.extend('- [' + name + '](' + source_link(name, sha) + ')' for name in record[key])
        if not record[key]:
            out.append('None identified in the catalogue; this is not proof none can exist.')
    if record['id'] in TASK_TERMS:
        out += ['\n## Find by task', TASK_TERMS[record['id']],
                'This is a search phrase for the existing contract, not an additional API alias.']
    out.append('\nABI pins prove layout, not semantic behavior or a live game session.')
    # Evidence tokens include wrapper names and Lua adapters, making those terms
    # discoverable by the built-in Pagefind body index without invented aliases.
    return '\n'.join(out)


def generate(root, catalogue, output, release_tag=None):
    root, output = Path(root).resolve(), Path(output).resolve()
    if catalogue.get('schema_version') != 1 or not re.fullmatch(r'[0-9a-f]{40}', catalogue.get('source_sha', '')):
        raise ValueError('Expected schema 1 and an immutable full source SHA')
    if release_tag:
        if release_tag != 'sdk-v' + catalogue['sdk_version']:
            raise ValueError('Release tag does not match SDK version')
        tag_sha = subprocess.check_output(['git', 'rev-parse', 'refs/tags/' + release_tag + '^{}'], cwd=root, text=True).strip()
        if tag_sha != catalogue['source_sha']:
            raise ValueError('Release tag does not identify the catalogue source')
    docs = inputs(root)
    doc_routes = {name: guide_id(name) for name in docs}
    records = sorted(catalogue['records'], key=lambda item: item['id'])
    ids = [r['id'] for r in records]
    if not records or len(ids) != len(set(ids)) or any(not re.fullmatch(r'[a-z0-9-]+', id_) for id_ in ids):
        raise ValueError('Empty, duplicate or unsafe record identifiers')
    if not set(TASK_TERMS) <= set(ids):
        raise ValueError('Task search term references a retired or missing record')
    services = sorted({r['service'] for r in records})
    if len(services) != len({service_id(s) for s in services}):
        raise ValueError('Service route collision')
    pages = {}
    for record in records:
        pages['reference/' + record['id']] = page(record['symbol'], render_record(record, catalogue, doc_routes), catalogue, release_tag)
    for service in services:
        parts = ['Methods and entrypoints for **' + service + '**.\n']
        for name, table in sorted(catalogue['tables'].items()):
            if table['service'] == service:
                parts += ['## ' + name, rewrite_links(table['context'], catalogue['contract_context']['source'], catalogue['source_sha'], doc_routes)]
        if service == 'native-plugin':
            parts.append(rewrite_links(catalogue['contract_context']['entrypoint_context'], catalogue['contract_context']['source'], catalogue['source_sha'], doc_routes))
        parts += ['\n## Contract records']
        parts += ['- [' + r['symbol'] + '](' + route('reference/' + r['id']) + ')' for r in records if r['service'] == service]
        pages['services/' + service_id(service)] = page(service, '\n'.join(parts), catalogue, release_tag)
    for language in LANGUAGES:
        parts = ['Static language view. Status is authoritative catalogue metadata, '
                 'not a runtime support filter. Unsupported entries remain explicit.\n',
                 '| Contract | Service | Status |', '| --- | --- | --- |']
        parts += ['| [' + r['symbol'] + '](' + route('reference/' + r['id']) + ') | '
                  + cell(r['service']) + ' | **' + r['language_mappings'][language]['status'] + '** |'
                  for r in records]
        pages['languages/' + language] = page(language + ' language mappings', '\n'.join(parts), catalogue, release_tag)
    for name in docs:
        text = (root / name).read_text(encoding='utf-8')
        title = re.search(r'^#\s+(.+)$', text, re.M)
        title = title[1] if title else PurePosixPath(name).stem
        text = re.sub(r'^#\s+[^\n]+\n', '', text, count=1, flags=re.M)
        text = rewrite_links(text, name, catalogue['source_sha'], doc_routes)
        pages[doc_routes[name]] = page(title, text, catalogue, release_tag)
    home = ['Public SDK contracts generated from a compiler-validated catalogue. '
            'Engineering guides are selected by an explicit public-input allowlist.\n',
            '[Download sdk-catalogue.json](' + BASE + '/sdk-catalogue.json)',
            '\n## Services']
    home += ['- [' + name + '](' + route('services/' + service_id(name)) + ')' for name in services]
    home += ['\n## Language mappings']
    home += ['- [' + language + '](' + route('languages/' + language) + ')' for language in LANGUAGES]
    home += ['\n## Engineering guides']
    home += ['- [' + name + '](' + route(doc_routes[name]) + ')' for name in docs]
    home += ['\n## Reading rules', catalogue['contract_context']['reading_rules'],
             '\n## Remaining gaps', rewrite_links(catalogue['contract_context']['remaining_gaps'], catalogue['contract_context']['source'], catalogue['source_sha'], doc_routes)]
    pages['index'] = page('sco-core SDK documentation', '\n'.join(home), catalogue, release_tag)
    # Only the generated content directory is replaced. No recursive repository
    # publication, and stale pages cannot survive a removed record or input.
    content = output / 'src/content/docs'
    if content.exists():
        shutil.rmtree(content)
    for identifier, text in sorted(pages.items()):
        dest = content / (identifier + '.md')
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(text, encoding='utf-8')
    public = output / 'public'
    public.mkdir(parents=True, exist_ok=True)
    (public / 'sdk-catalogue.json').write_text(json.dumps(catalogue, sort_keys=True, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    return len(records), len(services), len(pages)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT)
    parser.add_argument('--catalogue', type=Path)
    parser.add_argument('--out', type=Path)
    parser.add_argument('--release-tag', help='Existing sdk-v tag matching the source SHA; otherwise development preview')
    args = parser.parse_args()
    catalogue_path = args.catalogue or args.root / 'dist/sdk-catalogue.json'
    result = generate(args.root, json.loads(catalogue_path.read_text(encoding='utf-8')), args.out or args.root / 'site', args.release_tag)
    print('Generated %d record pages, %d service indexes, %d total pages' % result)
