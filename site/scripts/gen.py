#!/usr/bin/env python3
"""Render SDK contract catalogue schemas 1-2 and selected public engineering docs.

No source files, examples, tests or unlisted docs are copied into the site.
Source evidence is linked at the catalogue's immutable commit.
"""
import argparse
from collections import Counter
import hashlib
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
SOURCE_ROOT = 'https://github.com/scuba-sc/sco-core/blob/'
RELEASES = 'https://github.com/scuba-sc/sco-core/releases/tag/'
# Offline fixture for local/PR previews. Deployments supply freshly verified release
# metadata; neither value is a live-release selector.
PUBLISHED_RELEASE = 'sdk-v2.4.0'
PUBLISHED_DOWNLOAD_TAG = 'sdk-v2.4.0'
PUBLISHED_METADATA = None
DOWNLOADS = 'https://github.com/scuba-sc/sco-core/releases/download/'
COMMITS = 'https://github.com/scuba-sc/sco-core/commit/'
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
# When to take each archive; the profile purposes in sdk/components.json describe contents.
PROFILE_USE = {
    'core': 'You write a plugin against the game-neutral SDK: the C, C++, C# and Lua layers, host services, examples and tools.',
    'game-sc': 'Your plugin calls Star Citizen `game.*` services. Unpack it over the core archive of the version it names.',
    'legacy': 'You already use the single combined zip. It is not game-neutral and stays until consumers move to core and game-sc.',
    'core-source': 'You build the kernel host and loaders from source, without any game code.',
    'host-source-sc': 'You build the Star Citizen host from source. It includes the core-source kit.',
}
# Plain-language tier names for the native index page and hub card.
NATIVE_TIER_TEXT = {
    '1': ('exposed', 'A game capability uses this row.'),
    '2': ('resolved, not exposed', 'Registered, but no game capability uses it yet.'),
    '3': ('held', 'Backs a capability that is off by default until it has been run in game.'),
    '4': ('blocked', 'Backs a capability that has no trustworthy locator.'),
}
VERSIONS_ROUTE = 'versions'
VERSIONS_TITLE = 'Versions and evidence'
STATUSES = {'direct', 'typed-wrapper', 'restricted', 'unsupported'}
LANGUAGES = ('c', 'cpp', 'csharp', 'lua')
# Editorial task phrases, not API aliases or claims of additional entrypoints.
TASK_TERMS = {
    'sco-api-run-on-game-thread': 'schedule game thread task',
    'sco-storage-v1-query': 'open SQL cursor',
    'sco-api-query-service': 'find service table',
    'sc-actors-v1-spawn-npc': 'spawn NPC',
    'sc-spawn-service-v1-spawn-near-player': 'spawn a ship; spawn a vehicle; spawn an item near the player',
    'sc-inventory-v1-snapshot': 'read inventory; read player inventory; inspect attached item ports',
    'sc-container-contents-v1-snapshot': 'read container contents; list stored items in a container',
}
PAGE_TYPES = {'APIs', 'Services', 'Languages', 'Guides', 'Overview'}
# Four library hubs: sidebar groups, landing pages at hubs/<key>/ and the home cards.
# Every allowlisted doc sits in exactly one hub, and generation refuses a doc that
# has none, so a new public input cannot drop out of navigation. Grouping only
# changes navigation; every page keeps its route.
HUBS = (
    ('start', 'Get started',
     'Pick an SDK archive, build and check a first plugin, and learn how plugins are found and loaded.'),
    ('sdks', 'Language SDKs',
     'The C++ and C# SDK layers, plus Lua, data packs, examples and the per-language status of every contract.'),
    ('reference', 'API reference',
     'Service tables, callable contracts, game services and the engine-native index.'),
    ('integration', 'Hosts, launchers and agents',
     'Launcher and host contracts, the host-source kit, MCP discovery for agents and IDEs, and release notes.'),
)
# Editorial one-line summaries for hub cards; navigation text, not contract claims.
HUB_DOCS = {
    'start': (
        ('docs/roadmap.md', 'Where the SDK stands, what it delivers today, what comes next and how to contribute.'),
        ('docs/getting-started.md', 'One path from a published SDK archive to a checked C plugin deployed on Windows.'),
        ('docs/README.md', 'Which guide to read first: plugin author, host integrator, game-pack contributor, agent user or maintainer.'),
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
        ('docs/game/relocation-position.md', 'Position-only relocation of the local player: the `request_position` method added in `game.relocation` 1.1.'),
        ('docs/game/status.md', 'Which `game.*` tables have a contract, a native implementation, signature checks or in-game verification.'),
        ('docs/game/builds.md', 'Every Star Citizen patch the game pack was checked against, and which rows each one changed.'),
        ('docs/storage.md', 'Per-plugin key-value and SQL storage over SQLite.'),
        ('docs/ui.md', 'Renderer-neutral `sco.widgets` and the legacy `sco.ui` service.'),
        ('docs/ipc.md', 'Local IPC through one named shared-memory mapping.'),
        ('docs/net.md', 'Multiplayer messages over `sco.net`.'),
        ('docs/datacore.md', 'DataCore files and packs with `sco-dcb`.'),
        ('docs/api.md', 'Internal C++ namespace `sco`; version 0, not the plugin ABI.'),
    ),
    'integration': (
        ('docs/agent-workflows.md', 'Skills that help agents build, check, package and maintain plugins against the public SDK.'),
        ('docs/defaults-readiness.md', 'Which game capabilities are on by default, and what each backend needs before it is available.'),
        ('docs/agent-integration.md', 'Read-only MCP discovery tools for agents and IDEs.'),
        ('docs/sdk-mcp-tutorial.md', 'Discover a contract, request a scaffold, validate manifest/widgets and run a native checker.'),
        ('docs/host-source-sc.md', 'Build the Star Citizen host from the generated host-source kit.'),
        ('docs/launcher-contract.md', 'What a launcher and a host agree on: environment variables, folder layout, the disabled marker, settings and events.'),
        ('docs/building-a-launcher.md', 'Build a launcher on the SDK with the reference launcher in `sdk/examples/launcher`: install a plugin from a zip, and see what a product must supply itself. Source-level and synthetic tests only; not run in game.'),
        ('docs/widgets-view.md', 'The host-side panel reader and click queue for a host menu thread (`sco.widgets.view`).'),
        ('docs/releases/sdk-v2.4.1.md', 'SDK 2.4.1 release notes: what is new, compatibility and evidence.'),
        ('docs/releases/sdk-v2.4.0.md', 'SDK 2.4.0 release notes and archive identities.'),
        ('docs/reference-snapshots.md', 'For maintainers: freeze a copy of this reference against specific SDK archives and a source commit.'),
    ),
}
LANGUAGE_LABELS = {'c': 'C', 'cpp': 'C++', 'csharp': 'C#', 'lua': 'Lua'}
# Navigation labels where a doc's own title would read as a duplicate of the home page.
NAV_LABELS = {'docs/README.md': 'Guides by audience'}
# Repository guides and examples outside docs/: pinned source links, never imported.
SDK_SOURCES = (
    ('sdk/docs/lua.md', 'Lua plugins', 'Write a plugin in sandboxed Lua.'),
    ('sdk/docs/data-packs.md', 'Data packs', 'Content-only packs without native code.'),
    ('sdk/docs/widgets.md', 'Widgets', 'Building native UI with `sco.widgets`.'),
    ('sdk/examples', 'Examples', 'Buildable examples in C, C++, C#, Lua and Rust, plus the reference launcher.'),
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


def published_stage():
    """(status label, note) of PUBLISHED_DOWNLOAD_TAG: the final release or one of its candidates."""
    match = re.fullmatch(re.escape(PUBLISHED_RELEASE) + r'(?:-rc\.([1-9][0-9]*))?', PUBLISHED_DOWNLOAD_TAG)
    if not match:
        raise ValueError('PUBLISHED_DOWNLOAD_TAG must be PUBLISHED_RELEASE or one of its -rc.N tags')
    if match[1]:
        return 'Published release candidate', 'Release candidate ' + match[1] + '; the final release follows.'
    return 'Published release', ''


def documents_published_release(catalogue):
    """True when the catalogue's SDK and game-pack versions are the published final release's."""
    return bool(PUBLISHED_METADATA
                and PUBLISHED_METADATA['tag'] == 'sdk-v' + catalogue['sdk_version']
                and PUBLISHED_METADATA['sdk_version'] == catalogue['sdk_version']
                and PUBLISHED_METADATA['game_pack_version'] == catalogue['game_pack_version'])


def banner(catalogue, release_tag, category):
    """One line on every page: what the page documents. The detail lives on the home page and the versions page."""
    sdk, pack = catalogue['sdk_version'], catalogue['game_pack_version']
    source = '`' + catalogue['source_sha'][:12] + '`'
    if not release_tag and documents_published_release(catalogue):
        # The deployed source documents the published release; later main commits may only fix docs.
        tag = PUBLISHED_METADATA['tag']
        line = ('Documents SDK ' + sdk + ', game pack ' + pack + ', published as [' + tag + '](' + RELEASES
                + quote(tag, safe='') + '). Source ' + source
                + ('' if catalogue['source_sha'] == PUBLISHED_METADATA['source_sha']
                   else ', which may include changes made after the release') + '.')
    elif release_tag:
        line = ('Release [' + release_tag + '](' + RELEASES + quote(release_tag, safe='') + '): SDK ' + sdk
                + ', game pack ' + pack + ', source ' + source + '.')
    else:
        line = ('Development preview of source ' + source + ': SDK ' + sdk + ', game pack ' + pack
                + '. ' + ('Latest release: ' if PUBLISHED_METADATA else 'Release in the offline fixture: ')
                + '[' + PUBLISHED_DOWNLOAD_TAG + '](' + RELEASES + quote(PUBLISHED_DOWNLOAD_TAG, safe='') + ').')
    return ('<div class="sco-banner" data-pagefind-ignore="index">\n\n' + line + ' [' + VERSIONS_TITLE + '](' + route(VERSIONS_ROUTE)
            + ')<span data-pagefind-filter="Type:' + esc(category) + '"></span>\n\n</div>\n\n')


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
        if PUBLISHED_METADATA is not None:
            archive = PUBLISHED_METADATA['archives'][name]
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


def profile_choice(cards, sha, doc_routes, pinned_release=False):
    """Which archive to take: one row per published profile, with a guide link."""
    out = ['| Profile | Take this when |', '| --- | --- |']
    for card in cards:
        guide, guide_label = PROFILE_GUIDES[card['profile']]
        path, _, fragment = guide.partition('#')
        guide_url = (source_link(path, sha) + ('#' + fragment if fragment else '')
                     if pinned_release else repo_link(guide, sha, doc_routes))
        out.append('| `' + card['profile'] + '` | ' + PROFILE_USE[card['profile']] + ' [' + guide_label + '](' + guide_url + ') |')
    return out


def archive_table(cards):
    """The archive file name of each profile; each row carries its profile, name and version for tools."""
    out = ['<table>', '<thead><tr><th scope="col">Profile</th><th scope="col">Archive</th></tr></thead>', '<tbody>']
    for card in cards:
        out.append('<tr class="sco-profile" data-profile="' + esc(card['profile']) + '" data-archive="'
                   + esc(card['archive']) + '" data-version="' + esc(card['version']) + '"><td><code>'
                   + esc(card['profile']) + '</code></td><td><code>' + esc(card['archive']) + '</code></td></tr>')
    out += ['</tbody>', '</table>']
    return out


def download_section(cards, sha, doc_routes, tag):
    """Which archive to take and one block that verifies any of them."""
    pinned = PUBLISHED_METADATA is not None
    out = ['\n## Download',
           ('Archives in ' if pinned else 'Offline archive fixture for ') + PUBLISHED_DOWNLOAD_TAG
           + '. Most plugin authors need only `core`; add `game-sc` for Star Citizen services.\n',
           *profile_choice(cards, sha, doc_routes, pinned_release=pinned)]
    out += ['\n## Verify a download',
            'Put `SHA256SUMS` and the archive from the [release page](' + RELEASES + tag + ') in one folder. '
            'Set `ARCHIVE` to one of these names:\n',
            *archive_table(cards), '',
            '```sh wrap',
            'TAG=' + PUBLISHED_DOWNLOAD_TAG,
            'ARCHIVE=' + cards[0]['archive'],
            'curl -fLO ' + DOWNLOADS + '$TAG/SHA256SUMS',
            'curl -fLO ' + DOWNLOADS + '$TAG/$ARCHIVE',
            "awk -v f=\"$ARCHIVE\" '$2 == f' SHA256SUMS | shasum -a 256 -c -",
            '```',
            '`OK` means the file matches the release list. This is an integrity check, not a signature check. '
            'On Linux use `sha256sum -c -` in place of `shasum -a 256 -c -`. On Windows use the Python version below.',
            '\n### Verify with Python',
            'Works on Windows too (fetch the files with `curl.exe`). Set `archive` to your file.',
            '```python wrap', 'import hashlib', 'from pathlib import Path', '',
            'archive = "' + cards[0]['archive'] + '"',
            'sums = dict(line.split()[::-1] for line in Path("SHA256SUMS").read_text().splitlines() if line.strip())',
            'actual = hashlib.sha256(Path(archive).read_bytes()).hexdigest()',
            'print(archive + (": OK" if actual == sums[archive] else ": FAILED"))',
            'raise SystemExit(actual != sums[archive])', '```']
    return out


def release_portal(root, catalogue, release_tag, doc_routes):
    """Home-page release boxes, archive choice and checksum commands, all from repository facts."""
    notes = 'docs/releases/' + PUBLISHED_RELEASE + '.md'
    if notes not in doc_routes:
        raise ValueError('Published release notes must be an allowlisted input: ' + notes)
    scope = release_scope((root / notes).read_text(encoding='utf-8'), PUBLISHED_RELEASE)
    if PUBLISHED_METADATA is not None:
        scope = {key: PUBLISHED_METADATA[key] for key in ('sdk_version', 'game_pack_version', 'plugin_abi', 'profiles')} | {'tag': PUBLISHED_RELEASE}
    cards = published_profiles(root, scope)
    sha = catalogue['source_sha']
    status, stage = published_stage()
    if PUBLISHED_METADATA is None:
        status, stage = 'Offline release fixture', 'Offline fixture; not current publication data.'
    tag = quote(PUBLISHED_DOWNLOAD_TAG, safe='')

    def facts(pairs):
        return ('<dl class="sco-facts">' + ''.join('<dt>' + esc(k) + '</dt><dd>' + v + '</dd>' for k, v in pairs)
                + '</dl>')

    published_source = ([('Source', '<a href="' + esc(COMMITS + PUBLISHED_METADATA['source_sha']) + '">'
                          + esc(PUBLISHED_METADATA['source_sha'][:12]) + '</a>')] if PUBLISHED_METADATA else [])
    published = [
        '<section class="sco-track" data-od-id="release-published" data-track="published" data-tag="' + esc(scope['tag'])
        + '" data-download-tag="' + esc(PUBLISHED_DOWNLOAD_TAG) + '" aria-labelledby="track-published">',
        '<span class="sco-status sco-status--published">' + esc(status) + '</span>',
        '<h3 id="track-published"><a href="' + esc(RELEASES + tag) + '">' + esc(PUBLISHED_DOWNLOAD_TAG)
        + '</a></h3>',
        facts(published_source + [('SDK', esc(scope['sdk_version'])), ('Game pack', esc(scope['game_pack_version'])),
                                  ('Plugin ABI', esc(scope['plugin_abi'])), ('Archives', str(len(cards)) + ' profiles')]),
        '<p class="sco-track__note">' + (esc(stage) + ' ' if stage else '') + '<a href="'
        + esc(route(doc_routes[notes])) + '">Release notes</a></p>',
        '</section>']
    if release_tag:
        label, title = 'Release build', release_tag
        note = 'This site build was generated from the commit its release tag identifies.'
    else:
        label, title = 'Development preview', 'This site\'s source'
        note = ('Not a release. These pages describe this source commit. An archive built from it may carry '
                'another version: read COMPATIBILITY.json inside the archive.')
    preview = [
        '<section class="sco-track" data-od-id="release-build" data-track="' + ('release' if release_tag else 'preview')
        + '" data-source-sha="' + esc(sha) + '" aria-labelledby="track-build">',
        '<span class="sco-status sco-status--' + ('published' if release_tag else 'preview') + '">' + esc(label) + '</span>',
        '<h3 id="track-build">' + esc(title) + '</h3>',
        facts([('Source', '<a href="' + esc(COMMITS + sha) + '">' + esc(sha[:12]) + '</a>'),
               ('SDK', esc(catalogue['sdk_version'])), ('Game pack', esc(catalogue['game_pack_version']))]),
        '<p class="sco-track__note">' + esc(note) + '</p>',
        '</section>']
    out = ['\n## Releases', '<div class="sco-tracks not-content">', *published, *preview, '</div>',
           *download_section(cards, PUBLISHED_METADATA['source_sha'] if PUBLISHED_METADATA else sha, doc_routes, tag)]
    return '\n'.join(out)


def first_sentence(text, limit=220, plain=True):
    """First sentence of Markdown-ish text, cut at a word boundary past limit; plain drops the markup."""
    if plain:
        text = re.sub(r'\[([^\]]*)\]\([^)]*\)', r'\1', text)
        text = re.sub(r'<[^>]+>', '', text)
        text = re.sub(r'[`*]', '', text)
    text = ' '.join(text.split())
    text = re.split(r'(?<=[.!?])\s+(?=[A-Z(])', text, maxsplit=1)[0]
    if len(text) > limit:
        text = text[:limit].rsplit(' ', 1)[0].rstrip(',;:') + ' ...'
    return text


def page(title, body, catalogue, release_tag, category, toc=True, hero=None, description=None):
    if category not in PAGE_TYPES:
        raise ValueError('Unknown search page type: ' + category)
    title = plain_title(title)
    description = description or title + ' | sco-core SDK for Star Citizen offline plugins'
    return ('---\ntitle: ' + json.dumps(title, ensure_ascii=False)
            + '\ndescription: ' + json.dumps(description, ensure_ascii=False)
            + ('' if toc else '\ntableOfContents: false')
            + ('\ntemplate: splash\nhero: ' + json.dumps(hero, ensure_ascii=False) if hero else '')
            + '\n---\n\n'
            + banner(catalogue, release_tag, category) + body.rstrip() + '\n')


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
    groups: list[dict[str, object]] = [{'label': 'Home', 'slug': 'index'},
                                       {'label': VERSIONS_TITLE, 'slug': VERSIONS_ROUTE}]
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
        groups.append({'label': label, 'collapsed': True, 'items': items})
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
    for number, ((key, label, blurb), group) in enumerate(zip(HUBS, groups[2:]), 1):
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
    raw = (root / NATIVE_INDEX).read_bytes()
    data = json.loads(raw.decode('utf-8'))
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
    return data, raw


def tier_name(tiers, tier):
    return NATIVE_TIER_TEXT[str(tier)][0]


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
           'tier. Filter by subsystem and tier, or open `' + BASE + '/natives/?' + example
           + '` to jump to one row. [Download native-index.json](' + BASE + '/' + NATIVE_DOWNLOAD + ')\n',
           ':::note[What an entry is]',
           'Entries list row names and readiness only: no byte patterns, masks, offsets or addresses. '
           'A hash is derived from the row id; it is a name, not an address. '
           'Not run in game. [What that means](' + route(VERSIONS_ROUTE) + ')',
           ':::\n',
           '## Tier legend\n',
           '<dl class="sco-tiers not-content">']
    for tier in sorted(NATIVE_TIERS):
        name, about = NATIVE_TIER_TEXT[tier]
        out += ['<div class="sco-tiers__item">',
                '<dt><span class="sco-tier sco-tier--' + tier + '">' + tier + ' · ' + esc(name) + '</span></dt>',
                '<dd>' + esc(about) + ' <span class="sco-tiers__count">'
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


def hub_body(key, blurb, catalogue, doc_routes, titles, services, natives):
    """A hub landing page: the hub's pages as cards, plus its generated indexes."""
    sha = catalogue['source_sha']
    guides = cards([(route(doc_routes[name]), NAV_LABELS.get(name, titles[name]), summary, None)
                    for name, summary in HUB_DOCS[key]])
    out = [blurb + '\n']
    if key == 'start':
        out += ['## Guides', guides, '\n## Which archive to take',
                'Archive choice and checksum steps are on the home page: [Download](' + route('index') + '#download) and '
                '[Verify a download](' + route('index') + '#verify-a-download).']
    elif key == 'sdks':
        languages = cards([(route('languages/' + language), LANGUAGE_LABELS[language] + ' language mappings',
                            'How each contract is exposed to ' + LANGUAGE_LABELS[language] + ' plugins.', None)
                           for language in LANGUAGES])
        sources = cards([(source_link(path, sha), label, summary, 'Source, pinned at <code>' + esc(sha[:12]) + '</code>')
                         for path, label, summary in SDK_SOURCES])
        out += ['## SDK guides', guides, '\n## Language mappings',
                'Each page lists the status of every contract for one language: direct, typed-wrapper, restricted or unsupported.\n',
                languages,
                '\n## Lua, data packs and examples',
                'These live with the SDK archive, not on this site. The links open the source at this site\'s source commit.\n',
                sources]
    elif key == 'reference':
        tiers = Counter(entry['tier'] for entry in natives['entries'])
        native = cards([(route('natives'), NATIVE_TITLE,
                         'Every registered signature row by symbolic hash and tier, with search, '
                         'filters and deep links. Names and readiness only; no patterns or addresses.',
                         esc(str(len(natives['entries'])) + ' rows · ' + str(tiers[1]) + ' exposed · '
                             + str(tiers[3]) + ' held'))], 'sco-cards sco-cards--single')
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
    rows = ['| Language | Status | Evidence |',
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
        rows.append('| ' + LANGUAGE_LABELS[language] + ' | **' + mapping['status'] + '** | ' + '<br>'.join(evidence) + ' |')
    return '\n'.join(rows) + '\n'


def table_version(packed):
    """Packed table version 0x00010001 as '1.1 (`0x00010001`)'."""
    return str(packed >> 16) + '.' + str(packed & 0xFFFF) + ' (`0x%08x`)' % packed


def declaration(record):
    """(C declaration with parameter names, remaining text) of a record's buffer contract."""
    head, _, rest = record['buffer_contract'].partition('<br>')
    if head.startswith('`') and head.endswith('`') and head.count('`') == 2:
        return head[1:-1], rest
    return None, record['buffer_contract']


SHARED_LIMITS_TITLE = 'Limits that apply to every method'
SHARED_LIMITS_ANCHOR = 'limits-that-apply-to-every-method'
UNSPECIFIED_ASPECTS = (('callback_context', 'callbacks'), ('reentrancy', 'reentrancy'),
                       ('absent_capability', 'behaviour when the table is absent'))
SHARED_LIMIT_TEXT = {
    'callback_context': 'Callbacks are not specified unless the method page lists a Callbacks entry.',
    'reentrancy': 'Reentrancy is not specified unless the method page lists a Reentrancy entry.',
}


def shared_limits(catalogue):
    """Limits repeated on more than a third of all records. Keys: ('text', string) or ('aspect', field)."""
    records = catalogue['records']
    counts = Counter()
    for record in records:
        counts.update(('text', item) for item in set(record['limitations']))
        counts.update(('aspect', key) for key, _ in UNSPECIFIED_ASPECTS if record[key] == 'unspecified')
    return {key: count for key, count in counts.items()
            if count * 3 > len(records) and (key[0] == 'text' or key[1] in SHARED_LIMIT_TEXT)}


def shared_limits_section(catalogue, doc_routes):
    shared = shared_limits(catalogue)
    if not shared:
        return []
    total = len(catalogue['records'])
    lines = ['\n## ' + SHARED_LIMITS_TITLE,
             'These limits are repeated on more than a third of the ' + str(total)
             + ' method pages. Each method page links here and lists only the limits that are specific to it.']
    for key, count in sorted(shared.items(), key=lambda item: (-item[1], item[0])):
        text = key[1] if key[0] == 'text' else SHARED_LIMIT_TEXT[key[1]]
        text = rewrite_links(text, catalogue['contract_context']['source'], catalogue['source_sha'], doc_routes)
        lines.append('- ' + text + ' (' + str(count) + ' of ' + str(total) + ' methods)')
    lines.append('How to read the rows: [Public service contract catalogue](' + route(doc_routes['docs/contracts.md']) + ').')
    return lines


def render_record(record, catalogue, doc_routes):
    sha = catalogue['source_sha']
    prose = lambda text: rewrite_links(text, catalogue['contract_context']['source'], sha, doc_routes)
    table = catalogue['tables'].get(record['table'], {})
    code, rest = declaration(record)
    out = [prose(record['purpose'])]
    if code is not None:
        out += ['\n```c', code, '```']
    if rest and rest != record['purpose']:
        out += ['\n' + prose(rest)]
    since = record['since'].replace('|', '&#124;').replace('\n', '<br>')
    rows = [('Symbol', '<code>' + cell(record['symbol']) + '</code>'),
            ('Service', '[' + cell(record['service']) + '](' + route('services/' + service_id(record['service'])) + ')'),
            ('Table', cell(record['table'] or 'Native entrypoint (no table)')),
            ('Version macro', cell(record['version_macro']))]
    if 'version' in table:
        rows.append(('Table version', table_version(table['version'])))
    rows.append(('Since', since))
    if record['member_extent'] is not None:
        rows.append(('Member extent (x64 bytes)', cell(record['member_extent'])))
    if record['table_size'] is not None:
        rows.append(('Table size (x64 bytes)', cell(record['table_size'])))
    rows.append(('Header', '[' + cell(record['source_header']) + '](' + source_link(record['source_header'], sha) + ')'))
    out += ['\n## Availability', '| Property | Value |', '| --- | --- |',
            *['| ' + name + ' | ' + value + ' |' for name, value in rows],
            '\n## Results and failures', prose(record['results'])]
    if record['absent_capability'] != 'unspecified':
        out += ['\n## Absent capability', prose(record['absent_capability'])]
    recorded = [('Thread', record['thread_affinity']), ('Callbacks', record['callback_context']),
                ('Reentrancy', record['reentrancy'])]
    threading = [(name, value) for name, value in recorded if value != 'unspecified']
    if threading:
        out += ['\n## Threading and callbacks', *['- **' + name + ':** ' + prose(value) for name, value in threading]]
    ownership = [('Ownership', record['ownership']), ('Cleanup and cancellation', record['cleanup'])]
    ownership = [(name, value) for name, value in ownership if value != 'unspecified']
    if ownership:
        out += ['\n## Ownership and cleanup', *['- **' + name + ':** ' + prose(value) for name, value in ownership]]
    shared = shared_limits(catalogue)
    specific = [prose(item) for item in record['limitations'] if ('text', item) not in shared]
    unspecified = [name for key, name in UNSPECIFIED_ASPECTS
                   if record[key] == 'unspecified' and ('aspect', key) not in shared]
    if unspecified:
        specific.append('Not specified: ' + ', '.join(unspecified) + '. See [how to read the rows]('
                        + route(doc_routes['docs/contracts.md']) + ').')
    if shared or specific:
        out += ['\n## Limitations']
    if shared:
        out += ['[' + SHARED_LIMITS_TITLE + '](' + route(VERSIONS_ROUTE) + '#' + SHARED_LIMITS_ANCHOR + ').']
    out += ['- ' + item for item in specific]
    out += ['\n## Language mappings',
            'Status per language ([what the labels mean](' + route('languages/c') + ')).\n',
            mapping_rows(record, sha)]
    for key in ('examples', 'tests'):
        if record[key]:
            out.append('\n## ' + key.title())
            out.extend('- [' + name + '](' + source_link(name, sha) + ')' for name in record[key])
    if record['id'] in TASK_TERMS:
        out += ['\n## Find by task', '<p data-pagefind-weight="10">' + esc(TASK_TERMS[record['id']]) + '</p>']
    # Evidence tokens include wrapper names and Lua adapters, making those terms
    # discoverable by the built-in Pagefind body index without invented aliases.
    return '\n'.join(out)


HERO_TAGLINE = 'A versioned plugin interface for Star Citizen offline mods'


def registry_by_service(root):
    """Service entries of sdk/status_registry.json by service name, for default capability state."""
    path = root / 'sdk/status_registry.json'
    if not no_symlink_component(root, 'sdk/status_registry.json') or not path.is_file():
        return {}
    data = json.loads(path.read_text(encoding='utf-8'))
    return {entry['service']: entry for entry in data.get('services', []) if 'service' in entry}


def method_list(records, prose):
    """Bullet list: contract link, then the first sentence of its purpose."""
    return ['- [' + r['symbol'] + '](' + route('reference/' + r['id']) + '): ' + prose(first_sentence(r['purpose'], plain=False))
            for r in records]


def service_body(service, records, catalogue, entry, prose, doc_routes):
    mine = [r for r in records if r['service'] == service]
    tables = sorted((name, table) for name, table in catalogue['tables'].items() if table['service'] == service)
    methods = [r for r in mine if r['table']]
    entrypoints = [r for r in mine if not r['table']]
    bits = []
    if methods:
        bits.append(str(len(methods)) + (' method' if len(methods) == 1 else ' methods') + ' in '
                    + str(len(tables)) + (' table' if len(tables) == 1 else ' tables'))
    if entrypoints:
        bits.append(str(len(entrypoints)) + (' entrypoint' if len(entrypoints) == 1 else ' entrypoints'))
    parts = ['`' + service + '` has ' + ' and '.join(bits) + '.']
    if entry:
        parts[0] += ' Stability: ' + entry['stability'] + '.' if 'stability' in entry else ''
        capabilities = [cap for cap in entry['capabilities'] if cap.get('default') in ('ON', 'OFF')]
        if capabilities:
            parts += ['\n## Capabilities',
                      *['- `' + cap['name'] + '`: ' + cap['default'].lower() + ' by default' for cap in capabilities]]
    for name, table in tables:
        parts += ['\n## ' + name, 'Version ' + table_version(table['version']) + '.\n', prose(table['context']), '',
                  *method_list([r for r in mine if r['table'] == name], prose)]
    if entrypoints:
        parts += ['\n## Entrypoints']
        if service == 'native-plugin':
            parts.append(prose(catalogue['contract_context']['entrypoint_context']) + '\n')
        parts += method_list(entrypoints, prose)
    return '\n'.join(parts)


def language_body(language, records):
    """Status of every contract for one language, grouped by service and status."""
    label = LANGUAGE_LABELS[language]
    counts = Counter(r['language_mappings'][language]['status'] for r in records)
    order = [status for status in ('direct', 'typed-wrapper', 'restricted', 'unsupported') if counts[status]]
    summary = ('All ' + str(len(records)) + ' contracts are ' + order[0] + '.' if len(order) == 1
               else ', '.join(str(counts[status]) + ' ' + status for status in order) + '.')
    parts = ['How each contract is exposed to ' + label + ' plugins. ' + summary + '\n',
             '- **direct**: native access to the C table.',
             '- **typed-wrapper**: backed by a wrapper in the language SDK.',
             '- **restricted**: a subset or an adaptation.',
             '- **unsupported**: no supported route is established.\n',
             'The status describes the catalogue entry; it does not test the host at run time.\n',
             '| Service | Status | Contracts |', '| --- | --- | --- |']
    for service in sorted({r['service'] for r in records}):
        for status in order:
            group = [r for r in records if r['service'] == service and r['language_mappings'][language]['status'] == status]
            if group:
                parts.append('| ' + cell(service) + ' | **' + status + '** | '
                             + '<br>'.join('[' + r['symbol'] + '](' + route('reference/' + r['id']) + ')' for r in group) + ' |')
    return '\n'.join(parts)


def versions_body(doc_routes, catalogue):
    """The one explanation of version numbers, development previews and evidence wording."""
    status = route(doc_routes['docs/game/status.md'])
    builds = route(doc_routes['docs/game/builds.md'])
    contracts = route(doc_routes['docs/contracts.md'])
    return '\n'.join([
        'Every page of this site names the source commit it was built from, the SDK version and the game pack version.',
        '\n## Version numbers',
        '| Number | What it is |', '| --- | --- |',
        '| SDK version | The sco-core release, for example `2.4.1`. Release tags are `sdk-v<version>`. |',
        '| Game pack version | The Star Citizen extension, shipped as the `game-sc` archive. It has its own number. |',
        '| Plugin ABI | The version of `sco_api.h`. Version 1 only grows, so a plugin built against 1.x keeps loading in later 1.x hosts. |',
        '| Game build | The Star Citizen build the game pack was checked against. See [game builds](' + builds + '). |',
        '\n## Development preview and releases',
        'A page marked **Development preview** describes a source commit that is not a release. '
        'An archive built from that commit can carry another version. Read `COMPATIBILITY.json` inside the archive '
        '(`COMPATIBILITY.json.game-sc` for the standalone game overlay), and `VERSION` or `sdk/VERSION` where present.',
        'When the source documents the latest release, the line at the top of the page says so and links the release. '
        'Releases and their checksums are on the [home page](' + route('index') + '#download).',
        '\n## Evidence',
        'These pages list what the headers declare. A listed method is not a promise that it works in the game.',
        'A feature marked **Not run in game.** has only synthetic tests or static analysis behind it. '
        'Synthetic tests run against fake game objects. Static analysis reads the game binary without running it. '
        'A passing ABI test shows the table layout, not the behavior.',
        'The status of every `game.*` table, with its evidence level, is on the [game capability status page](' + status
        + '). How to read a method page: [Public service contract catalogue](' + contracts + ').',
        *shared_limits_section(catalogue, doc_routes),
    ])


def generate(root, catalogue, output, release_tag=None):
    root, output = Path(root).resolve(), Path(output).resolve()
    schema = catalogue.get('schema_version')
    expected = ({'schema_version', 'sdk_version', 'game_pack_version', 'source_sha',
                 'contract_context', 'tables', 'records'} if schema == 1 else
                {'schema_version', 'sdk_version', 'game_pack_version', 'source_sha',
                 'status_registry_sha256', 'contract_context', 'tables', 'records'} if schema == 2 else None)
    if (expected is None or set(catalogue) != expected
            or not re.fullmatch(r'[0-9a-f]{40}', catalogue.get('source_sha', ''))
            or (schema == 2 and not re.fullmatch(r'[0-9a-f]{64}', catalogue.get('status_registry_sha256', '')))):
        raise ValueError('Expected catalogue schema 1 or 2 with valid provenance')
    if schema == 2:
        registry_path = root / 'sdk/status_registry.json'
        if not no_symlink_component(root, 'sdk/status_registry.json') or not registry_path.is_file():
            raise ValueError('Catalogue status-registry source is unavailable or unsafe')
        try:
            with registry_path.open('rb') as source:
                registry = source.read((1 << 20) + 1)
        except OSError as error:
            raise ValueError('Catalogue status-registry source is unavailable or unsafe') from error
        if len(registry) > (1 << 20):
            raise ValueError('Catalogue status-registry source exceeds 1 MiB')
        if hashlib.sha256(registry).hexdigest() != catalogue['status_registry_sha256']:
            raise ValueError('Catalogue status-registry digest does not match source')
    if release_tag:
        if PUBLISHED_METADATA is None or PUBLISHED_METADATA['tag'] != release_tag:
            raise ValueError('Release build requires matching supplied published metadata')
        if not re.fullmatch(re.escape('sdk-v' + catalogue['sdk_version']) + r'(?:-rc\.[1-9][0-9]*)?', release_tag):
            raise ValueError('Release tag does not match SDK version')
        if PUBLISHED_METADATA['source_sha'] != catalogue['source_sha']:
            raise ValueError('Published release source does not match the catalogue')
        tag_sha = subprocess.check_output(['git', 'rev-parse', 'refs/tags/' + release_tag + '^{}'], cwd=root, text=True).strip()
        if tag_sha != catalogue['source_sha']:
            raise ValueError('Release tag does not identify the catalogue source')
    docs = inputs(root)
    hub_docs(docs)
    doc_routes = {name: guide_id(name) for name in docs}
    natives, natives_bytes = native_index(root)
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
    prose = lambda text: rewrite_links(text, catalogue['contract_context']['source'], catalogue['source_sha'], doc_routes)
    registry_services = registry_by_service(root)
    for record in records:
        pages['reference/' + record['id']] = page(record['symbol'], render_record(record, catalogue, doc_routes),
                                                  catalogue, release_tag, 'APIs',
                                                  description=first_sentence(record['purpose']))
    for service in services:
        pages['services/' + service_id(service)] = page(
            service, service_body(service, records, catalogue, registry_services.get(service), prose, doc_routes),
            catalogue, release_tag, 'Services',
            description='Methods of the ' + service + ' service and the table versions that carry them.')
    for language in LANGUAGES:
        pages['languages/' + language] = page(
            LANGUAGE_LABELS[language] + ' language mappings', language_body(language, records),
            catalogue, release_tag, 'Languages',
            description='How each contract is exposed to ' + LANGUAGE_LABELS[language]
            + ' plugins: direct, typed-wrapper, restricted or unsupported.')
    summaries = {name: summary for entries in HUB_DOCS.values() for name, summary in entries}
    titles = {}
    for name in docs:
        text = (root / name).read_text(encoding='utf-8')
        title = re.search(r'^#\s+(.+)$', text, re.M)
        titles[name] = plain_title(title[1] if title else PurePosixPath(name).stem)
        text = re.sub(r'^#\s+[^\n]+\n', '', text, count=1, flags=re.M)
        text = rewrite_links(text, name, catalogue['source_sha'], doc_routes)
        if PUBLISHED_METADATA and name in {
                'docs/releases/' + PUBLISHED_RELEASE + '.md', 'docs/roadmap.md',
                'docs/game/status.md',
                'docs/agent-integration.md'}:
            text = ('> **Latest release:** [' + PUBLISHED_METADATA['tag'] + ']('
                    + RELEASES + PUBLISHED_METADATA['tag'] + '), source `'
                    + PUBLISHED_METADATA['source_sha'][:12] + '`. Dated status and candidate '
                    'records below describe the state when written; see the release for current assets.\n\n'
                    + text)
        if doc_routes[name] in pages:
            raise ValueError('Generated route collision: ' + name)
        pages[doc_routes[name]] = page(titles[name], text, catalogue, release_tag, 'Guides',
                                       description=first_sentence(summaries[name], limit=300))
    groups = sidebar(doc_routes, titles, services, natives)
    for key, label, blurb in HUBS:
        pages['hubs/' + key] = page(label, hub_body(key, blurb, catalogue, doc_routes, titles, services, natives),
                                    catalogue, release_tag, 'Overview', description=blurb)
    # No table of contents: the row table takes the wider content column (portal.css).
    pages['natives'] = page(NATIVE_TITLE, natives_body(natives, catalogue['source_sha']), catalogue, release_tag, 'APIs',
                            toc=False, description='Every registered signature row by symbolic hash and tier. '
                            'Names and readiness only.')
    pages[VERSIONS_ROUTE] = page(VERSIONS_TITLE, versions_body(doc_routes, catalogue), catalogue, release_tag, 'Overview',
                                 description='What the SDK, game pack and plugin ABI versions on these pages mean, '
                                 'and what "Not run in game." means.')
    libraries = '\n## Libraries\n\n' + hub_cards(groups)
    home = ['**New here:** [Start your first plugin](' + route(doc_routes['docs/getting-started.md']) + '). [Roadmap]('
            + route(doc_routes['docs/roadmap.md']) + ') · [Download sdk-catalogue.json](' + BASE + '/sdk-catalogue.json)',
            libraries,
            release_portal(root, catalogue, release_tag, doc_routes),
            '\n## Reading the contract pages',
            'Each method page follows the same headings. [How to read the rows, and what is still unspecified]('
            + route(doc_routes['docs/contracts.md']) + '). [' + VERSIONS_TITLE + '](' + route(VERSIONS_ROUTE)
            + ') explains the version numbers and what "Not run in game." means.']
    hero = {'title': 'sco-core SDK', 'tagline': HERO_TAGLINE,
            'actions': [
                {'text': 'Start a plugin', 'link': route(doc_routes['docs/getting-started.md']),
                 'icon': 'right-arrow'},
                {'text': 'Browse API', 'link': route('hubs/reference'), 'variant': 'secondary'}]}
    pages['index'] = page('sco-core SDK', '\n'.join(home), catalogue, release_tag, 'Overview', hero=hero,
                          description=HERO_TAGLINE + '.')
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
    release_metadata_path = public / 'published-release.json'
    if PUBLISHED_METADATA is not None:
        release_metadata_path.write_text(json.dumps(PUBLISHED_METADATA, sort_keys=True, indent=2) + '\n', encoding='utf-8')
    else:
        release_metadata_path.unlink(missing_ok=True)
    (public / 'sdk-catalogue.json').write_text(json.dumps(catalogue, sort_keys=True, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    # The validated index, byte for byte: the page and the download state the same rows.
    (public / NATIVE_DOWNLOAD).write_bytes(natives_bytes)
    return len(records), len(services), len(pages)


def use_published_metadata(data):
    """Use a separately resolved release for the published track; preview stays catalogue-bound."""
    global PUBLISHED_RELEASE, PUBLISHED_DOWNLOAD_TAG, PUBLISHED_METADATA
    expected = {'schema_version', 'tag', 'source_sha', 'release_id', 'sdk_version', 'game_pack_version', 'plugin_abi', 'profiles', 'archives', 'asset_digests'}
    if not isinstance(data, dict) or set(data) != expected or data['schema_version'] != 1:
        raise ValueError('Invalid published release metadata')
    sdk, game, abi, tag = (data[k] for k in ('sdk_version', 'game_pack_version', 'plugin_abi', 'tag'))
    if (not all(isinstance(v, str) for v in (sdk, game, abi, tag, data['source_sha']))
            or not re.fullmatch(r'\d+\.\d+\.\d+', sdk) or not re.fullmatch(r'\d+\.\d+\.\d+', game)
            or not re.fullmatch(r'\d+\.\d+', abi) or not re.fullmatch(r'[0-9a-f]{40}', data['source_sha'])
            or not re.fullmatch(re.escape('sdk-v' + sdk) + r'(?:-rc\.[1-9][0-9]*)?', tag)):
        raise ValueError('Invalid published release coordinates')
    profiles = ['core', 'game-sc', 'legacy', 'core-source', 'host-source-sc']
    archives = {'core': f'sco-sdk-core-{sdk}.zip', 'game-sc': f'sco-sdk-game-sc-{game}.zip',
                'legacy': f'sco-sdk-{sdk}.zip', 'core-source': f'sco-sdk-core-source-{sdk}.zip',
                'host-source-sc': f'sco-sdk-host-source-sc-{sdk}.zip'}
    required = set(archives.values()) | {n[:-4] + '.spdx.json' for n in archives.values()} | {'SHA256SUMS'}
    digests = data['asset_digests']
    if (data['profiles'] != profiles or data['archives'] != archives or type(data['release_id']) is not int or data['release_id'] <= 0
            or not isinstance(digests, dict) or set(digests) not in (required, required | {'release.json'})
            or any(not isinstance(v, str) or not re.fullmatch(r'sha256:[0-9a-f]{64}', v) for v in digests.values())):
        raise ValueError('Invalid published release profiles')
    PUBLISHED_RELEASE, PUBLISHED_DOWNLOAD_TAG, PUBLISHED_METADATA = 'sdk-v' + sdk, tag, data


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT)
    parser.add_argument('--catalogue', type=Path)
    parser.add_argument('--out', type=Path)
    parser.add_argument('--release-tag', help='Existing sdk-v tag matching the source SHA; otherwise development preview')
    parser.add_argument('--published-metadata', type=Path, help='Verified published release metadata')
    args = parser.parse_args()
    if args.published_metadata:
        use_published_metadata(json.loads(args.published_metadata.read_text(encoding='utf-8')))
    catalogue_path = args.catalogue or args.root / 'dist/sdk-catalogue.json'
    result = generate(args.root, json.loads(catalogue_path.read_text(encoding='utf-8')), args.out or args.root / 'site', args.release_tag)
    print('Generated %d record pages, %d service indexes, %d total pages' % result)
