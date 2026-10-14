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
RELEASES = 'https://github.com/scubamount/sco-core/releases/tag/'
# Most recent SDK release verified as published. A preview links here instead of
# deriving a tag from its own sdk_version, which may name an unpublished version.
PUBLISHED_RELEASE = 'sdk-v2.1.0'
DOWNLOADS = 'https://github.com/scubamount/sco-core/releases/download/'
COMMITS = 'https://github.com/scubamount/sco-core/commit/'
# The same delimited block tools/doc_release_gate.py checks against packaging.
SCOPE_START = '<!-- release-scope:start -->'
SCOPE_END = '<!-- release-scope:end -->'
SCOPE_KEYS = {'tag', 'sdk_version', 'game_pack_version', 'plugin_abi', 'profiles'}
VERSION_KEYS = {'sdk/VERSION': 'sdk_version', 'include/sc_game_pack.h': 'game_pack_version'}
# Editorial guide per published profile. An allowlisted doc renders as its local
# page; any other path becomes a source link pinned at the catalogue commit.
PROFILE_GUIDES = {
    'core': ('docs/getting-started.md', 'Start a plugin'),
    'game-sc': ('sdk/README.md#zips', 'Archive composition'),
    'legacy': ('docs/getting-started.md', 'Start a plugin'),
    'core-source': ('sdk/README.md#build-the-kernel-source-kit', 'Build the kernel source kit'),
    'host-source-sc': ('docs/host-source-sc.md', 'Host-source guide'),
}
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
BLOCKED_TREES = {'legal', 'design', 'releases'}
# The only exception to BLOCKED_TREES: an explicitly allowlisted semantic-version
# release note directly under docs/releases/. legal/ and design/ never match.
RELEASE_NOTE = re.compile(r'docs/releases/sdk-v\d+\.\d+\.\d+\.md\Z')
REGULAR_FILE_MODES = {'100644', '100755'}
# Percent-encoded dot, slash or backslash in a link path could decode to traversal.
ENCODED_PATH_META = re.compile(r'%(?:2e|2f|5c)', re.I)


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


def tracked_regular_files(root):
    """Paths under docs/ that root's git index tracks as regular files (not symlinks or gitlinks)."""
    try:
        top = subprocess.run(['git', '-C', str(root), 'rev-parse', '--show-toplevel'],
                             capture_output=True, text=True, check=True).stdout.strip()
        listing = subprocess.run(['git', '-C', str(root), 'ls-files', '-s', '-z', '--', 'docs'],
                                 capture_output=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError) as error:
        raise ValueError('Public inputs must come from a git work tree') from error
    if Path(top).resolve() != Path(root).resolve():
        raise ValueError('Public-input root must be the git work-tree root')
    tracked = set()
    for entry in listing.decode('utf-8').split('\0'):
        if entry:
            meta, name = entry.split('\t', 1)
            if meta.split()[0] in REGULAR_FILE_MODES:
                tracked.add(name)
    return tracked


def no_symlink_component(root, name):
    current = root
    for part in PurePosixPath(name).parts:
        current = current / part
        if current.is_symlink():
            return False
    return True


def inputs(root):
    data = json.loads((root / 'site/public-inputs.json').read_text(encoding='utf-8'))
    if data.get('schema_version') != 1 or set(data) != {'schema_version', 'docs'}:
        raise ValueError('Invalid public-input allowlist')
    docs = data['docs']
    if (not isinstance(docs, list) or not docs or not all(isinstance(name, str) for name in docs)
            or len(docs) != len(set(docs))):
        raise ValueError('Allowlist must contain unique explicit paths')
    for name in docs:
        path = PurePosixPath(name)
        if (str(path) != name or not name.startswith('docs/') or path.suffix != '.md'
                or '..' in path.parts or '%' in name or '\\' in name or '*' in name
                or (path.parts[1] in BLOCKED_TREES and not RELEASE_NOTE.fullmatch(name))):
            raise ValueError('Only explicit public engineering docs may be rendered')
    tracked = tracked_regular_files(root)
    docs_root = (root / 'docs').resolve()
    routes = {}
    for name in docs:
        if name not in tracked:
            raise ValueError('Input is not a git-tracked regular file: ' + name)
        if not no_symlink_component(root, name):
            raise ValueError('Input path crosses a symlink: ' + name)
        real = (root / name).resolve()
        if not real.is_relative_to(docs_root) or not real.is_file():
            raise ValueError('Missing input or path outside docs')
        key = route_key(guide_id(name))
        if key in routes:
            raise ValueError('Generated route collision: ' + name + ' and ' + routes[key])
        routes[key] = name
    return sorted(docs)


def guide_id(path):
    """Content identifier in the slug form Starlight serves (it drops '.' and other punctuation per segment)."""
    parts = PurePosixPath(path).relative_to('docs').with_suffix('').parts
    slugs = [re.sub(r'[^a-z0-9_-]', '', part.lower()) for part in parts]
    if not all(slugs):
        raise ValueError('Input name has no routable slug: ' + str(path))
    return 'guides/' + '/'.join(slugs)


def route_key(identifier):
    """Served route of a content identifier: Starlight serves x/index.md at x/."""
    return identifier[:-len('/index')] if identifier.endswith('/index') else identifier


def rewrite_links(text, source, sha, doc_routes):
    """Resolve selected docs locally; other evidence becomes a pinned source link."""
    def replace(match):
        target = match['target']
        url = urlsplit(target)
        if (not (url.scheme or url.netloc) or target.startswith(SOURCE_ROOT)) and ENCODED_PATH_META.search(url.path):
            raise ValueError('Encoded dot or separator in link: ' + target)
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
    published = release_tag or PUBLISHED_RELEASE
    return (':::caution[Catalogue snapshot — ' + label + ']\n'
            + 'SDK **' + catalogue['sdk_version'] + '** · game pack **' + catalogue['game_pack_version']
            + '** · source `' + catalogue['source_sha'] + '`\n'
            + 'Published release: [' + published + '](' + RELEASES + quote(published, safe='') + ')'
            + (' (immutable approved assets).\n' if release_tag
               else ' (immutable approved assets; this development preview is not an approved release).\n')
            + 'Source-backed inventory is not a runtime or in-game verification claim.\n:::\n\n')


def release_scope(text, tag):
    """The release note's delimited JSON scope declaration for tag."""
    if text.count(SCOPE_START) != 1 or text.count(SCOPE_END) != 1:
        raise ValueError('Release note needs exactly one release-scope block')
    body = text[text.index(SCOPE_START) + len(SCOPE_START):text.index(SCOPE_END)]
    fence = re.fullmatch(r'\s*```json\n(.+?)\n```\s*', body, re.S)
    if not fence:
        raise ValueError('Release-scope block must hold one json fence')
    scope = json.loads(fence[1])
    if not isinstance(scope, dict) or set(scope) != SCOPE_KEYS or scope['tag'] != tag:
        raise ValueError('Release scope keys or tag do not match ' + tag)
    profiles = scope['profiles']
    if (not isinstance(profiles, list) or not profiles or len(set(profiles)) != len(profiles)
            or not all(isinstance(p, str) and re.fullmatch(r'[a-z0-9-]+', p) for p in profiles)):
        raise ValueError('Release scope profiles must be unique profile names')
    for key, pattern in (('sdk_version', r'\d+\.\d+\.\d+'), ('game_pack_version', r'\d+\.\d+\.\d+'),
                         ('plugin_abi', r'\d+\.\d+')):
        if not isinstance(scope[key], str) or not re.fullmatch(pattern, scope[key]):
            raise ValueError('Malformed release scope ' + key)
    return scope


def published_profiles(root, scope):
    """Archive name, version and purpose of each declared profile, from sdk/components.json."""
    data = json.loads((root / 'sdk/components.json').read_text(encoding='utf-8'))
    defined = data.get('profiles') if isinstance(data, dict) else None
    cards = []
    for name in scope['profiles']:
        profile = defined.get(name) if isinstance(defined, dict) else None
        if (not isinstance(profile, dict) or profile.get('version_from') not in VERSION_KEYS
                or not isinstance(profile.get('zip'), str) or not isinstance(profile.get('about'), str)
                or name not in PROFILE_GUIDES):
            raise ValueError('Published profile lacks a packaging definition or guide: ' + name)
        version = scope[VERSION_KEYS[profile['version_from']]]
        archive = profile['zip'].format(version=version, game_pack_version=version) + '.zip'
        if not re.fullmatch(r'[a-z0-9][a-z0-9.-]*\.zip', archive):
            raise ValueError('Unsafe archive name for profile ' + name)
        cards.append({'profile': name, 'archive': archive, 'version': version,
                      'about': re.split(r'(?<=\.)\s', profile['about'].strip(), maxsplit=1)[0],
                      'includes': list(profile.get('includes', [])), 'requires': profile.get('requires')})
    return cards


def repo_link(target, sha, doc_routes):
    path, _, fragment = target.partition('#')
    if path in doc_routes:
        return route(doc_routes[path])
    return source_link(path, sha) + ('#' + fragment if fragment else '')


def release_portal(root, catalogue, release_tag, doc_routes):
    """Home-page release tracks, profile cards and checksum commands, all from repository facts."""
    notes = 'docs/releases/' + PUBLISHED_RELEASE + '.md'
    if notes not in doc_routes:
        raise ValueError('Published release notes must be an allowlisted input: ' + notes)
    scope = release_scope((root / notes).read_text(encoding='utf-8'), PUBLISHED_RELEASE)
    cards = published_profiles(root, scope)
    sha = catalogue['source_sha']
    esc = lambda value: html.escape(str(value), quote=True)
    tag = quote(scope['tag'], safe='')

    def facts(pairs):
        return ('<dl class="sco-facts">' + ''.join('<dt>' + esc(k) + '</dt><dd>' + v + '</dd>' for k, v in pairs)
                + '</dl>')

    published = [
        '<section class="sco-track" data-track="published" data-tag="' + esc(scope['tag'])
        + '" aria-labelledby="track-published">',
        '<span class="sco-status sco-status--published">Published release</span>',
        '<h3 id="track-published"><a href="' + esc(RELEASES + tag) + '">' + esc(scope['tag']) + '</a></h3>',
        facts([('SDK', esc(scope['sdk_version'])), ('Game pack', esc(scope['game_pack_version'])),
               ('Plugin ABI', esc(scope['plugin_abi'])), ('Archives', str(len(cards)) + ' profiles')]),
        '<p class="sco-track__note">Immutable approved assets. <a href="' + esc(route(doc_routes[notes]))
        + '">Release notes</a></p>',
        '</section>']
    if release_tag:
        label, title = 'Release build', release_tag
        note = 'This site build was generated from the commit its release tag identifies.'
    else:
        label, title = 'Mainline development preview', 'This documentation build'
        note = 'Not an approved release. Pages describe this source commit.'
    preview = [
        '<section class="sco-track" data-track="' + ('release' if release_tag else 'preview')
        + '" data-source-sha="' + esc(sha) + '" aria-labelledby="track-build">',
        '<span class="sco-status sco-status--' + ('published' if release_tag else 'preview') + '">' + esc(label) + '</span>',
        '<h3 id="track-build">' + esc(title) + '</h3>',
        facts([('Source', '<a href="' + esc(COMMITS + sha) + '">' + esc(sha) + '</a>'),
               ('SDK', esc(catalogue['sdk_version'])), ('Game pack', esc(catalogue['game_pack_version']))]),
        '<p class="sco-track__note">' + esc(note) + '</p>',
        '</section>']
    out = ['\n## Releases', '<div class="sco-tracks not-content">', *published, *preview, '</div>',
           '\n## Choose an archive',
           'Archives published in ' + scope['tag'] + '. Profiles and versions come from its release-scope declaration.\n',
           '<ul class="sco-profiles not-content">']
    for card in cards:
        relation = ('Includes ' + ', '.join(card['includes']) if card['includes']
                    else 'Requires ' + card['requires'] if card['requires'] else 'Standalone')
        guide, guide_label = PROFILE_GUIDES[card['profile']]
        out += ['<li class="sco-profile" data-profile="' + esc(card['profile']) + '" data-archive="'
                + esc(card['archive']) + '" data-version="' + esc(card['version']) + '">',
                '<h3>' + esc(card['profile']) + '</h3>',
                '<p class="sco-profile__archive"><code>' + esc(card['archive']) + '</code></p>',
                '<p class="sco-profile__meta"><span class="sco-pill">' + esc(card['version']) + '</span> '
                + esc(relation) + '</p>',
                '<p>' + esc(card['about']) + '</p>',
                '<p class="sco-profile__links"><a href="' + esc(repo_link(guide, sha, doc_routes)) + '">'
                + esc(guide_label) + '</a><a href="' + esc(route('index') + '#verify-' + card['profile'])
                + '">Verify checksum</a></p>',
                '</li>']
    out += ['</ul>', '\n## Verify a download',
            'Each archive\'s SHA-256 digest is listed in the release\'s [SHA256SUMS](' + DOWNLOADS + tag
            + '/SHA256SUMS). Download it and the archive into one folder, then check that archive\'s line: '
            '`OK` means the bytes match the published list. This checks integrity against the '
            'release\'s own list; it is not a signature check. On Linux, use `sha256sum` '
            'in place of `shasum -a 256`.']
    for card in cards:
        out += ['\n### Verify ' + card['profile'], '```sh wrap',
                'curl -fLO ' + DOWNLOADS + tag + '/SHA256SUMS',
                'curl -fLO ' + DOWNLOADS + tag + '/' + card['archive'],
                "awk '$2 == \"" + card['archive'] + "\"' SHA256SUMS | shasum -a 256 -c -", '```']
    out += ['\n### Verify with Python',
            'For any platform, including Windows (download with `curl.exe`). Set `archive` to the file you downloaded.',
            '```python wrap', 'import hashlib', 'from pathlib import Path', '',
            'archive = "' + cards[0]['archive'] + '"',
            'sums = dict(line.split()[::-1] for line in Path("SHA256SUMS").read_text().splitlines() if line.strip())',
            'actual = hashlib.sha256(Path(archive).read_bytes()).hexdigest()',
            'print(archive + (": OK" if actual == sums[archive] else ": FAILED"))',
            'raise SystemExit(actual != sums[archive])', '```']
    return '\n'.join(out)


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
        if doc_routes[name] in pages:
            raise ValueError('Generated route collision: ' + name)
        pages[doc_routes[name]] = page(title, text, catalogue, release_tag)
    home = ['Public SDK contracts generated from a compiler-validated catalogue. '
            'Engineering guides are selected by an explicit public-input allowlist.\n',
            '[Download sdk-catalogue.json](' + BASE + '/sdk-catalogue.json)',
            release_portal(root, catalogue, release_tag, doc_routes),
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
