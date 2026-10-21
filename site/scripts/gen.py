#!/usr/bin/env python3
"""Render schema-1 SDK contracts and explicitly selected public engineering docs.

No source files, examples, tests or unlisted docs are copied into the site.
Source evidence is linked at the catalogue's immutable commit.
"""
import argparse
from collections import Counter
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
PUBLISHED_RELEASE = 'sdk-v2.3.0'
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
# Four library hubs: sidebar groups, landing pages at hubs/<key>/ and the home cards.
# Every allowlisted doc sits in exactly one hub, and generation refuses a doc that
# has none, so a new public input cannot drop out of navigation. Grouping only
# changes navigation; every page keeps its route.
HUBS = (
    ('start', 'Start & SDK profiles',
     'Choose an SDK archive, build and check a first plugin, and learn how plugins are found and loaded.'),
    ('sdks', 'Language SDKs & cookbooks',
     'The C++ and C# SDK layers, worked examples and the per-language status of every contract.'),
    ('reference', 'Native & service reference',
     'Service tables, callable contracts, game services and the engine-native index.'),
    ('integration', 'Host integration & AI agents',
     'MCP discovery for agents and IDEs, the host-source kit, release notes and provenance.'),
)
# Editorial one-line summaries for hub cards; navigation text, not contract claims.
HUB_DOCS = {
    'start': (
        ('docs/roadmap.md', 'Where the SDK stands, what 2.4.0 delivers, what comes after, and how to contribute.'),
        ('docs/getting-started.md', 'One path from a published SDK archive to a checked C plugin deployed on Windows.'),
        ('docs/README.md', 'Curated guides grouped by audience.'),
        ('docs/plugins.md', 'Plugin folders, `plugin.ini`, discovery, loading and data packs.'),
        ('docs/plugin-design.md', 'Design principles for modular plugins, with a provider and consumer example pair.'),
        ('docs/plugin-directory.md', 'Community plugins in their own repositories, with reported SDK compatibility.'),
        ('docs/framework.md', 'How the runtime, plugin system, services and SDK sit under each product.'),
    ),
    'sdks': (
        ('docs/sdk-cpp.md', 'Header-only C++20 layer over `sco_api.h`.'),
        ('docs/sdk-csharp.md', '.NET 8 layer over `sco_api.h`, published with NativeAOT as one native DLL.'),
    ),
    'reference': (
        ('docs/api-v1.md', 'The plain-C root plugin ABI: exports, root table, capabilities, commands, events and threads.'),
        ('docs/contracts.md', 'How to read the per-method service contract catalogue.'),
        ('docs/stability.md', 'What stays compatible across SDK 2.x: Stable and Experimental service tables.'),
        ('docs/game-services.md', 'Services the Star Citizen game pack publishes under the reserved owner `game`.'),
        ('docs/game/status.md', 'Status of every `game.*` table.'),
        ('docs/storage.md', 'Per-plugin key-value and SQL storage over SQLite.'),
        ('docs/ui.md', 'Renderer-neutral `sco.widgets` and the legacy `sco.ui` service.'),
        ('docs/ipc.md', 'Local IPC through one named shared-memory mapping.'),
        ('docs/net.md', 'Multiplayer messages over `sco.net`.'),
        ('docs/datacore.md', 'DataCore files and packs with `sco-dcb`.'),
        ('docs/api.md', 'Internal C++ namespace `sco`; version 0, not the plugin ABI.'),
    ),
    'integration': (
        ('docs/agent-integration.md', 'Read-only MCP discovery tools for agents and IDEs.'),
        ('docs/sdk-mcp-tutorial.md', 'Discover a contract, request a scaffold, validate manifest/widgets and run a native checker.'),
        ('docs/host-source-sc.md', 'Build the Star Citizen host from the generated host-source kit.'),
        ('docs/releases/sdk-v2.3.0.md', 'Release notes, scope and archives of SDK 2.3.0.'),
        ('docs/releases/sdk-v2.2.0.md', 'Release notes, scope and archives of SDK 2.2.0.'),
        ('docs/releases/sdk-v2.1.0.md', 'Release notes, scope and archives of SDK 2.1.0.'),
        ('docs/reference-snapshots.md', 'Snapshots that bind reference content to SDK inputs.'),
    ),
}
LANGUAGE_LABELS = {'c': 'C', 'cpp': 'C++', 'csharp': 'C#', 'lua': 'Lua'}
# Navigation labels where a doc's own title would read as a duplicate of the home page.
NAV_LABELS = {'docs/README.md': 'Guides by audience'}
# Repository guides and examples outside docs/: pinned source links, never imported.
SDK_SOURCES = (
    ('sdk/docs/lua.md', 'Lua plugins', 'Lua plugin guide.'),
    ('sdk/docs/data-packs.md', 'Data packs', 'Content-only packs without native code.'),
    ('sdk/docs/widgets.md', 'Widgets', 'Building native UI with `sco.widgets`.'),
    ('sdk/examples', 'Example plugins', 'Buildable examples in C, C++, C# and Lua.'),
)
NATIVE_INDEX = 'sdk/native_index.json'
NATIVE_DOWNLOAD = 'native-index.json'
NATIVE_TITLE = 'Engine-native index'
NATIVE_KEYS = {'id', 'subsystem', 'hash', 'tier', 'capability', 'source'}
NATIVE_TIERS = {'1', '2', '3', '4'}
SIDEBAR = 'src/generated-sidebar.json'
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


def esc(value):
    return html.escape(str(value), quote=True)


def plain_title(title):
    """Page titles and sidebar labels are plain text: drop Markdown code-span backticks."""
    return re.sub(r'`+', '', title).strip()


def inline_code(text):
    """HTML-escape text, rendering `code` spans as <code>."""
    parts = re.split(r'`([^`]+)`', text)
    return ''.join('<code>' + esc(part) + '</code>' if index % 2 else esc(part)
                   for index, part in enumerate(parts))


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


def profile_cards(cards, sha, doc_routes):
    """Archive cards for each published profile: name, archive, version, purpose, guide and checksum link."""
    out = ['<ul class="sco-profiles not-content">']
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
    out.append('</ul>')
    return out


def release_portal(root, catalogue, release_tag, doc_routes, libraries=''):
    """Home-page release tracks, profile cards and checksum commands, all from repository facts.

    libraries is Markdown placed between the release tracks and the archive cards."""
    notes = 'docs/releases/' + PUBLISHED_RELEASE + '.md'
    if notes not in doc_routes:
        raise ValueError('Published release notes must be an allowlisted input: ' + notes)
    scope = release_scope((root / notes).read_text(encoding='utf-8'), PUBLISHED_RELEASE)
    cards = published_profiles(root, scope)
    sha = catalogue['source_sha']
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
           libraries,
           '\n## Choose an archive',
           'Archives published in ' + scope['tag'] + '. Profiles and versions come from its release-scope declaration.\n',
           *profile_cards(cards, sha, doc_routes)]
    out += ['\n## Verify a download',
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


def page(title, body, catalogue, release_tag, toc=True):
    title = plain_title(title)
    return ('---\ntitle: ' + json.dumps(title, ensure_ascii=False)
            + '\ndescription: ' + json.dumps('sco-core public engineering documentation: ' + title)
            + ('' if toc else '\ntableOfContents: false')
            + '\n---\n\n' + banner(catalogue, release_tag) + body.rstrip() + '\n')


def hub_docs(docs):
    """Check that every allowlisted doc belongs to exactly one hub."""
    if [key for key, _, _ in HUBS] != list(HUB_DOCS):
        raise ValueError('Hub definitions and hub documents disagree')
    assigned = [name for entries in HUB_DOCS.values() for name, _ in entries]
    if len(assigned) != len(set(assigned)) or set(assigned) != set(docs):
        raise ValueError('Every public input needs exactly one library hub: '
                         + ', '.join(sorted(set(assigned) ^ set(docs)) or ['duplicate assignment']))


def sidebar(doc_routes, titles, services, natives):
    """Starlight sidebar: home, then the four library hubs in order."""
    groups: list[dict[str, object]] = [{'label': 'Home', 'slug': 'index'}]
    for key, label, _ in HUBS:
        items: list[dict[str, object]] = [{'label': 'Overview', 'slug': 'hubs/' + key}]
        if key == 'reference':
            items.append({'label': NATIVE_TITLE, 'slug': 'natives',
                          'badge': {'text': str(len(natives['entries'])), 'variant': 'note'}})
        for name, _ in HUB_DOCS[key]:
            items.append({'label': NAV_LABELS.get(name, titles[name]), 'slug': route_key(doc_routes[name])})
        if key == 'sdks':
            items.append({'label': 'Language mappings', 'items': [
                {'label': LANGUAGE_LABELS[language], 'slug': 'languages/' + language} for language in LANGUAGES]})
        if key == 'reference':
            items.append({'label': 'Service tables', 'collapsed': True, 'items': [
                {'label': service, 'slug': 'services/' + service_id(service)} for service in services]})
        groups.append({'label': label, 'items': items})
    return groups


def count_pages(items):
    return sum(count_pages(item['items']) if 'items' in item else 1 for item in items)


def cards(entries, cls='sco-cards'):
    """Link cards: (href, title, summary Markdown-ish text with `code`, optional meta HTML)."""
    out = ['<ul class="' + cls + ' not-content">']
    for href, title, summary, meta in entries:
        out += ['<li class="sco-card">', '<h3><a href="' + esc(href) + '">' + inline_code(title) + '</a></h3>',
                '<p>' + inline_code(summary) + '</p>']
        if meta:
            out.append('<p class="sco-card__meta">' + meta + '</p>')
        out.append('</li>')
    out.append('</ul>')
    return '\n'.join(out)


def hub_cards(groups):
    """Home-page library cards, numbered in sidebar order."""
    out = ['<ol class="sco-hubs not-content">']
    for number, ((key, label, blurb), group) in enumerate(zip(HUBS, groups[1:]), 1):
        out += ['<li class="sco-card sco-card--hub" data-hub="' + esc(key) + '">',
                '<span class="sco-card__index" aria-hidden="true">' + '%02d' % number + '</span>',
                '<h3><a href="' + esc(route('hubs/' + key)) + '">' + esc(label) + '</a></h3>',
                '<p>' + esc(blurb) + '</p>',
                '<p class="sco-card__meta">' + str(count_pages(group['items'])) + ' pages</p>',
                '</li>']
    out.append('</ol>')
    return '\n'.join(out)


def native_index(root):
    """sdk/native_index.json, validated: symbolic names and readiness only, never patterns or addresses."""
    text = (root / NATIVE_INDEX).read_text(encoding='utf-8')
    data = json.loads(text)
    entries = data.get('entries') if isinstance(data, dict) else None
    tiers = data.get('tiers') if isinstance(data, dict) else None
    if (data.get('schema_version') != 1 or not isinstance(entries, list) or not entries
            or data.get('row_count') != len(entries) or not isinstance(tiers, dict) or set(tiers) != NATIVE_TIERS
            or not all(isinstance(value, str) and ':' in value for value in tiers.values())):
        raise ValueError('Invalid native index header')
    hashes, ids = set(), set()
    for entry in entries:
        if not isinstance(entry, dict) or not NATIVE_KEYS - {'capability'} <= set(entry) <= NATIVE_KEYS:
            raise ValueError('Native index entry has missing or unknown keys')
        capability = entry.get('capability', [])
        if (not isinstance(entry['hash'], str) or not re.fullmatch(r'_0x[0-9A-F]{16}', entry['hash'])
                or not isinstance(entry['id'], str) or not re.fullmatch(r'[a-z0-9_.-]+', entry['id'])
                or not isinstance(entry['subsystem'], str) or not re.fullmatch(r'[a-z0-9_-]+', entry['subsystem'])
                or type(entry['tier']) is not int or str(entry['tier']) not in NATIVE_TIERS
                or not isinstance(entry['source'], str) or not re.fullmatch(r'src/game/[a-z0-9_]+_sigs\.cpp', entry['source'])
                or not isinstance(capability, list)
                or not all(isinstance(c, str) and re.fullmatch(r'[a-z0-9_.-]+', c) for c in capability)):
            raise ValueError('Malformed native index entry')
        if entry['hash'] in hashes or entry['id'] in ids:
            raise ValueError('Duplicate native index hash or id: ' + entry['id'])
        hashes.add(entry['hash'])
        ids.add(entry['id'])
    return data, text


def tier_name(tiers, tier):
    return tiers[str(tier)].split(':', 1)[0].strip()


# The only client script on the site: filters the server-rendered rows and resolves
# /natives/?_0x<HASH> (or #_0x<HASH>) to one row. Inline, no network access.
NATIVES_SCRIPT = r'''<script>
(() => {
  const root = document.querySelector('[data-sco-natives]');
  if (!root) return;
  const rows = Array.from(root.querySelectorAll('tbody tr'));
  const form = root.querySelector('form');
  const query = root.querySelector('#sco-natives-q');
  const subsystem = root.querySelector('#sco-natives-subsystem');
  const tier = root.querySelector('#sco-natives-tier');
  const count = root.querySelector('#sco-natives-count');
  const empty = root.querySelector('#sco-natives-empty');
  const notice = root.querySelector('#sco-natives-notice');
  const apply = () => {
    const text = query.value.trim().toLowerCase();
    let shown = 0;
    for (const row of rows) {
      const match = (!text || row.dataset.search.includes(text))
        && (!subsystem.value || row.dataset.subsystem === subsystem.value)
        && (!tier.value || row.dataset.tier === tier.value);
      row.hidden = !match;
      if (match) shown += 1;
    }
    count.textContent = 'Showing ' + shown + ' of ' + rows.length + ' rows';
    empty.hidden = shown !== 0;
  };
  const requested = () => {
    for (const part of (location.search.slice(1) + '&' + location.hash.slice(1)).split('&')) {
      let value = '';
      try { value = decodeURIComponent(part.split('=').pop()).trim(); } catch { continue; }
      if (/^_0x[0-9a-f]{16}$/i.test(value)) return '_0x' + value.slice(3).toUpperCase();
    }
    return null;
  };
  const reveal = () => {
    for (const row of root.querySelectorAll('tr.is-target')) {
      row.classList.remove('is-target');
      row.removeAttribute('aria-current');
    }
    notice.hidden = true;
    const hash = requested();
    if (!hash) return;
    const row = document.getElementById(hash);
    if (!row || !root.contains(row)) {
      notice.textContent = 'No row has the hash ' + hash + '.';
      notice.hidden = false;
      return;
    }
    if (row.hidden) {
      query.value = '';
      subsystem.value = '';
      tier.value = '';
      apply();
    }
    row.classList.add('is-target');
    row.setAttribute('aria-current', 'location');
    row.scrollIntoView({ block: 'center' });
    row.focus({ preventScroll: true });
  };
  form.hidden = false;
  form.addEventListener('submit', event => event.preventDefault());
  for (const control of [query, subsystem, tier]) {
    control.addEventListener('input', apply);
    control.addEventListener('change', apply);
  }
  root.addEventListener('click', event => {
    const link = event.target.closest('a.sco-hash');
    if (!link || event.button !== 0 || event.metaKey || event.ctrlKey || event.shiftKey || event.altKey) return;
    event.preventDefault();
    history.pushState(null, '', link.href);
    reveal();
  });
  addEventListener('popstate', reveal);
  addEventListener('hashchange', reveal);
  apply();
  reveal();
  if (document.readyState !== 'complete') addEventListener('load', reveal, { once: true });
})();
</script>'''


def natives_body(index, sha):
    """Server-rendered rows plus filter controls; the table works and is searchable without the script."""
    entries, tiers = index['entries'], index['tiers']
    by_tier = Counter(str(entry['tier']) for entry in entries)
    by_subsystem = Counter(entry['subsystem'] for entry in entries)
    example = entries[0]['hash']
    out = ['Every signature row the game pack registers, listed under a stable symbolic hash with its '
           'promotion tier. Filter by subsystem and tier, or open `' + BASE + '/natives/?' + example
           + '` to jump to one row. [Download native-index.json](' + BASE + '/' + NATIVE_DOWNLOAD + ')\n',
           ':::note[What an entry is]',
           'Entries list row names and readiness only: no byte patterns, masks, offsets or addresses. '
           'A hash is derived from the row id; it is a name, not an address. '
           'Nothing here is verified in game unless an entry is marked so, and no entry in this index carries that mark.',
           ':::\n',
           '## Tier legend\n',
           '<dl class="sco-tiers not-content">']
    for tier in sorted(NATIVE_TIERS):
        name, _, about = tiers[tier].partition(':')
        out += ['<div class="sco-tiers__item">',
                '<dt><span class="sco-tier sco-tier--' + tier + '">' + tier + ' · ' + esc(name.strip()) + '</span></dt>',
                '<dd>' + esc(about.strip()[:1].upper() + about.strip()[1:]) + '. <span class="sco-tiers__count">'
                + str(by_tier[tier]) + (' row' if by_tier[tier] == 1 else ' rows') + '</span></dd>',
                '</div>']
    out += ['</dl>\n', '## Rows\n',
            '<div class="sco-natives not-content" data-sco-natives>',
            '<form class="sco-natives__filters" role="search" aria-label="Filter native rows" hidden>',
            '<label class="sco-field sco-field--grow"><span>Search</span><input type="search" id="sco-natives-q" '
            'placeholder="Row id, hash or capability" autocomplete="off" spellcheck="false"></label>',
            '<label class="sco-field"><span>Subsystem</span><select id="sco-natives-subsystem">'
            '<option value="">All subsystems</option>'
            + ''.join('<option value="' + esc(name) + '">' + esc(name) + ' (' + str(by_subsystem[name]) + ')</option>'
                      for name in sorted(by_subsystem)) + '</select></label>',
            '<label class="sco-field"><span>Tier</span><select id="sco-natives-tier"><option value="">All tiers</option>'
            + ''.join('<option value="' + tier + '">' + tier + ' · ' + esc(tier_name(tiers, tier)) + ' ('
                      + str(by_tier[tier]) + ')</option>' for tier in sorted(NATIVE_TIERS)) + '</select></label>',
            '</form>',
            '<p class="sco-natives__count" id="sco-natives-count" role="status" aria-live="polite">'
            + str(len(entries)) + ' rows</p>',
            '<p class="sco-natives__notice" id="sco-natives-notice" role="alert" hidden></p>',
            '<table class="sco-natives__table">',
            '<thead><tr><th scope="col">Row</th><th scope="col">Hash</th>'
            '<th scope="col">Tier</th><th scope="col">Capability</th></tr></thead>',
            '<tbody>']
    for entry in entries:
        capability = entry.get('capability', [])
        tier = str(entry['tier'])
        haystack = ' '.join([entry['id'], entry['hash'], entry['subsystem'], entry['source'], *capability]).lower()
        caps = ('<span class="sco-natives__caps">' + ''.join('<code>' + esc(c) + '</code>' for c in capability) + '</span>'
                if capability else '<span class="sco-natives__none">none</span>')
        out.append('<tr id="' + esc(entry['hash']) + '" tabindex="-1" data-subsystem="' + esc(entry['subsystem'])
                   + '" data-tier="' + tier + '" data-search="' + esc(haystack) + '">'
                   + '<td data-label="Row"><div class="sco-natives__row"><code class="sco-natives__id">'
                   + re.sub(r'([._])', r'\1<wbr>', esc(entry['id'])) + '</code>'
                   + '<span class="sco-natives__meta">' + esc(entry['subsystem']) + ' · <a href="'
                   + esc(source_link(entry['source'], sha)) + '">' + esc(PurePosixPath(entry['source']).name)
                   + '</a></span></div></td>'
                   + '<td data-label="Hash"><a class="sco-hash" href="' + esc(route('natives') + '?' + entry['hash'])
                   + '"><code>' + esc(entry['hash']) + '</code></a></td>'
                   + '<td data-label="Tier"><span class="sco-tier sco-tier--' + tier + '">' + tier + ' · '
                   + esc(tier_name(tiers, tier)) + '</span></td>'
                   + '<td data-label="Capability">' + caps + '</td></tr>')
    out += ['</tbody>', '</table>',
            '<p class="sco-natives__empty" id="sco-natives-empty" hidden>No row matches these filters.</p>',
            '</div>\n', NATIVES_SCRIPT]
    return '\n'.join(out)


def hub_body(key, blurb, catalogue, doc_routes, titles, services, natives, profiles):
    """A hub landing page: the hub's pages as cards, plus its generated indexes."""
    sha = catalogue['source_sha']
    guides = cards([(route(doc_routes[name]), NAV_LABELS.get(name, titles[name]), summary, None)
                    for name, summary in HUB_DOCS[key]])
    out = [blurb + '\n']
    if key == 'start':
        out += ['## Guides', guides, '\n## SDK profiles',
                'Archives published in ' + PUBLISHED_RELEASE + '. Each card links to its guide and to the checksum '
                'steps on the [home page](' + route('index') + '#verify-a-download).\n', *profiles]
    elif key == 'sdks':
        languages = cards([(route('languages/' + language), LANGUAGE_LABELS[language] + ' language mappings',
                            'Catalogue status of every contract for ' + LANGUAGE_LABELS[language] + '.', None)
                           for language in LANGUAGES])
        sources = cards([(source_link(path, sha), label, summary, 'Source, pinned at <code>' + esc(sha[:12]) + '</code>')
                         for path, label, summary in SDK_SOURCES])
        out += ['## SDK guides', guides, '\n## Language mappings',
                'Static views of the catalogue. A status is metadata, not a runtime support filter.\n', languages,
                '\n## Lua, data packs and examples',
                'These live with the SDK archive rather than in this site; links open the source at the catalogue commit.\n',
                sources]
    elif key == 'reference':
        tiers = Counter(entry['tier'] for entry in natives['entries'])
        native = cards([(route('natives'), NATIVE_TITLE,
                         'Every registered signature row by symbolic hash and promotion tier, with search, '
                         'filters and deep links. Names and readiness only; no patterns or addresses.',
                         esc(str(len(natives['entries'])) + ' rows · ' + str(tiers[1]) + ' promoted · '
                             + str(tiers[3]) + ' spike hold'))], 'sco-cards sco-cards--single')
        out += ['## Engine-native index', native, '\n## Guides', guides, '\n## Service tables',
                'One index per service, linking each callable contract record.\n',
                '<ul class="sco-links not-content">',
                *['<li><a href="' + esc(route('services/' + service_id(service))) + '"><code>' + esc(service)
                  + '</code></a></li>' for service in services],
                '</ul>']
    else:
        out += ['## Guides', guides, '\n## Machine-readable downloads',
                '- [sdk-catalogue.json](' + BASE + '/sdk-catalogue.json): the contract catalogue these pages render.',
                '- [' + NATIVE_DOWNLOAD + '](' + BASE + '/' + NATIVE_DOWNLOAD + '): the engine-native index, '
                'names and readiness only.']
    return '\n'.join(out)


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
    hub_docs(docs)
    doc_routes = {name: guide_id(name) for name in docs}
    natives, natives_text = native_index(root)
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
    titles = {}
    for name in docs:
        text = (root / name).read_text(encoding='utf-8')
        title = re.search(r'^#\s+(.+)$', text, re.M)
        titles[name] = plain_title(title[1] if title else PurePosixPath(name).stem)
        text = re.sub(r'^#\s+[^\n]+\n', '', text, count=1, flags=re.M)
        text = rewrite_links(text, name, catalogue['source_sha'], doc_routes)
        if doc_routes[name] in pages:
            raise ValueError('Generated route collision: ' + name)
        pages[doc_routes[name]] = page(titles[name], text, catalogue, release_tag)
    groups = sidebar(doc_routes, titles, services, natives)
    notes = 'docs/releases/' + PUBLISHED_RELEASE + '.md'
    profiles = profile_cards(published_profiles(root, release_scope((root / notes).read_text(encoding='utf-8'),
                                                                    PUBLISHED_RELEASE)),
                             catalogue['source_sha'], doc_routes)
    for key, label, blurb in HUBS:
        pages['hubs/' + key] = page(label, hub_body(key, blurb, catalogue, doc_routes, titles, services,
                                                     natives, profiles), catalogue, release_tag)
    # No table of contents: the row table takes the wider content column (portal.css).
    pages['natives'] = page(NATIVE_TITLE, natives_body(natives, catalogue['source_sha']), catalogue, release_tag,
                            toc=False)
    libraries = '\n## Libraries\n\n' + hub_cards(groups)
    home = ['Public SDK contracts generated from a compiler-validated catalogue. '
            'Engineering guides are selected by an explicit public-input allowlist.\n',
            '[Download sdk-catalogue.json](' + BASE + '/sdk-catalogue.json) · [Roadmap and direction]('
            + route(doc_routes['docs/roadmap.md']) + ')',
            release_portal(root, catalogue, release_tag, doc_routes, libraries)]
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
    (output / SIDEBAR).write_text(json.dumps(groups, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    public = output / 'public'
    public.mkdir(parents=True, exist_ok=True)
    (public / 'sdk-catalogue.json').write_text(json.dumps(catalogue, sort_keys=True, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    # The validated index, byte for byte: the page and the download state the same rows.
    (public / NATIVE_DOWNLOAD).write_text(natives_text, encoding='utf-8')
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
