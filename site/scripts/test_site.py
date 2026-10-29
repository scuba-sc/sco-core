#!/usr/bin/env python3
"""Standard-library generation, publication-boundary and built-output checks."""
import argparse
from copy import deepcopy
import gzip
import html
from html.parser import HTMLParser
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import tempfile
import unittest
from urllib.parse import unquote, urlsplit
import zlib

import gen

# CI tests the same verified release metadata used by the generator.
if os.environ.get('SCO_PUBLISHED_METADATA'):
    gen.use_published_metadata(json.loads(Path(os.environ['SCO_PUBLISHED_METADATA']).read_text()))

ROOT = Path(__file__).resolve().parents[2]
BUILT = None
# Tokens assembled from fragments so this test file never matches its own denylist.
FORBIDDEN = ('/' + 'Users' + '/', 'task' + '-register', 'sco' + '-private',
             'sco-core-' + 'ecosystem-analysis', 'PRIVATE ENGINEERING ' + 'DOCUMENTATION',
             'version_' + 'registry.mjs', 'ghidra_' + 'checkpoint.mjs', 'sc-' + 'reconstructed')
SENTINELS = ('PRIVATE_SENTINEL_73c61ae9', 'PRIVATE_FILENAME_93b4167a')
DOWNLOADS = {gen.BASE + '/sdk-catalogue.json', gen.BASE + '/' + gen.NATIVE_DOWNLOAD}
# Routes external checkers depend on; the release notifier polls the release-notes page.
PINNED_ROUTES = ('guides/releases/sdk-v240', 'guides/getting-started', 'guides/api-v1', 'guides/readme')
# Short URLs served by a static page in site/public/ that links to the guide it summarizes.
SHORT_ROUTES = {'roadmap': 'guides/roadmap'}
GZIP_FILE_LIMIT = 16 << 20
GZIP_TOTAL_LIMIT = 256 << 20


def gunzip_bounded(data, limit):
    """Decompress every gzip member in data, refusing output beyond limit bytes."""
    out = bytearray()
    try:
        while data:
            stream = zlib.decompressobj(16 + zlib.MAX_WBITS)
            out += stream.decompress(data, limit + 1 - len(out))
            if len(out) > limit:
                raise AssertionError('Gzip output exceeds %d bytes' % limit)
            if not stream.eof:
                raise AssertionError('Truncated gzip stream')
            data = stream.unused_data
    except zlib.error as error:
        raise AssertionError('Invalid gzip stream') from error
    return bytes(out)


def snapshot(root):
    return {p.relative_to(root).as_posix(): p.read_bytes()
            for p in sorted(root.rglob('*')) if p.is_file()}


def check_leaks(root, forbidden=FORBIDDEN, file_limit=GZIP_FILE_LIMIT, total_limit=GZIP_TOTAL_LIMIT):
    files = snapshot(root)
    if not files:
        raise AssertionError('Output tree is empty')
    expanded = 0
    for name, data in files.items():
        payloads = [data]
        # Pagefind fragments/indexes are gzip-compressed even without a .gz suffix.
        if data.startswith(b'\x1f\x8b'):
            plain = gunzip_bounded(data, min(file_limit, total_limit - expanded))
            expanded += len(plain)
            payloads.append(plain)
        for token in forbidden:
            if token in name or any(token.encode() in payload for payload in payloads):
                raise AssertionError('Forbidden publication token in ' + name)
    return len(files)


def check_markdown_links(content, public):
    files = snapshot(content)
    routes = {gen.route(p[:-3]) for p in files if p.endswith('.md')}
    for name, data in files.items():
        if not name.endswith('.md'):
            continue
        text = data.decode()
        targets = re.findall(r'(?<!!)\[[^\]\n]*\]\(([^\s)]+)\)', text)
        targets += [html.unescape(href) for href in re.findall(r'<a href="([^"]+)"', text)]
        for target in targets:
            url = urlsplit(target)
            if target.startswith('#'):
                continue
            if url.scheme:
                if target.startswith(gen.SOURCE_ROOT):
                    if not re.match(re.escape(gen.SOURCE_ROOT) + r'[0-9a-f]{40}/', target):
                        raise AssertionError('Unpinned source link: ' + target)
                continue
            if url.path not in routes and url.path not in DOWNLOADS:
                raise AssertionError('Unresolved generated link in ' + name + ': ' + target)
    for download in ('sdk-catalogue.json', gen.NATIVE_DOWNLOAD):
        if not (public / download).is_file():
            raise AssertionError('Missing download: ' + download)


class Links(HTMLParser):
    def __init__(self):
        super().__init__()
        self.urls = []
        self.anchor_hrefs = set()
        self.ids = set()

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if tag == 'a' and attrs.get('href'):
            self.anchor_hrefs.add(attrs['href'])
        if 'id' in attrs:
            self.ids.add(attrs['id'])
        for attr in ('href', 'src'):
            if attr in attrs:
                self.urls.append(attrs[attr])


def check_starlight_notice(root):
    """The site artifact must retain the full attributed upstream notice."""
    notice = root / 'starlight-LICENSE.txt'
    expected = ROOT / 'site/public/starlight-LICENSE.txt'
    if not notice.is_file() or notice.read_bytes() != expected.read_bytes():
        raise AssertionError('Missing or changed Starlight MIT notice in built site')


def check_built(root):
    check_leaks(root)
    check_starlight_notice(root)
    pages = list(root.rglob('*.html'))
    if not pages or not list((root / 'pagefind').rglob('*.pf_index')):
        raise AssertionError('Missing HTML or Pagefind search index')
    parsed = {}
    for path in pages:
        parser = Links()
        parser.feed(path.read_text())
        parsed[path] = parser
    required_notice = gen.BASE + '/starlight-LICENSE.txt'
    if not any(required_notice in parser.anchor_hrefs for parser in parsed.values()):
        raise AssertionError('Missing built Starlight notice link')
    checked = 0
    for path, parser in parsed.items():
        for target in parser.urls:
            url = urlsplit(target)
            if url.scheme or url.netloc or not url.path.startswith('/'):
                continue
            if not url.path.startswith(gen.BASE + '/'):
                raise AssertionError('Link lost site base: ' + target)
            relative = unquote(url.path[len(gen.BASE) + 1:])
            if '..' in PurePosixPath(relative).parts or '\\' in relative:
                raise AssertionError('Traversal in built link: ' + target)
            dest = root / relative
            if dest.is_dir():
                dest /= 'index.html'
            if not dest.is_file():
                raise AssertionError('Missing built target: ' + target)
            if url.fragment and dest in parsed and unquote(url.fragment) not in parsed[dest].ids:
                raise AssertionError('Missing built anchor: ' + target)
            checked += 1
    for download in ('sdk-catalogue.json', gen.NATIVE_DOWNLOAD):
        if not (root / download).is_file():
            raise AssertionError('Missing built download: ' + download)
    return len(pages), checked


def git_track(root):
    subprocess.run(['git', 'init', '-q', str(root)], check=True)
    subprocess.run(['git', '-C', str(root), 'add', '-A'], check=True)


class SiteTests(unittest.TestCase):
    def test_no_metadata_build_removes_stale_public_release_file(self):
        saved = gen.PUBLISHED_METADATA
        try:
            gen.PUBLISHED_METADATA = None
            with tempfile.TemporaryDirectory(prefix='site-stale-release-') as work:
                output = Path(work)
                public = output / 'public'
                public.mkdir()
                (public / 'published-release.json').write_text('{"stale": true}')
                gen.generate(ROOT, self.catalogue, output)
                self.assertFalse((public / 'published-release.json').exists())
                guide = output / 'src/content/docs' / (gen.guide_id('docs/releases/sdk-v2.4.0.md') + '.md')
                self.assertNotIn('Current published SDK:', guide.read_text())
        finally:
            gen.PUBLISHED_METADATA = saved

    def test_tag_alone_does_not_claim_a_published_release(self):
        saved = gen.PUBLISHED_METADATA
        try:
            gen.PUBLISHED_METADATA = None
            with tempfile.TemporaryDirectory(prefix='site-tag-only-') as work:
                with self.assertRaisesRegex(ValueError, 'requires matching supplied published metadata'):
                    gen.generate(ROOT, self.catalogue, Path(work), 'sdk-v' + self.catalogue['sdk_version'])
        finally:
            gen.PUBLISHED_METADATA = saved

    def test_published_profile_guides_use_release_source(self):
        import test_published_release as fixture
        rel, data = fixture.build()
        meta = fixture.r.resolve(fixture.REPO, fixture.client_for(rel, data), fixture.fake_git)
        saved = gen.PUBLISHED_RELEASE, gen.PUBLISHED_DOWNLOAD_TAG, gen.PUBLISHED_METADATA
        try:
            gen.use_published_metadata(meta)
            catalogue = deepcopy(self.catalogue)
            catalogue['source_sha'] = 'd' * 40
            with tempfile.TemporaryDirectory(prefix='site-published-source-') as work:
                output = Path(work)
                gen.generate(ROOT, catalogue, output)
                for name in ('index.md', 'hubs/start.md'):
                    text = (output / 'src/content/docs' / name).read_text()
                    for guide, _ in gen.PROFILE_GUIDES.values():
                        path, _, fragment = guide.partition('#')
                        url = gen.source_link(path, meta['source_sha']) + ('#' + fragment if fragment else '')
                        self.assertIn(url, text)
                guide_names = {'docs/releases/sdk-v2.4.0.md', 'docs/roadmap.md',
                               'docs/game/status.md', 'docs/agent-integration.md'}
                self.assertTrue(guide_names <= set(gen.inputs(ROOT)))
                for name in guide_names:
                    guide = output / 'src/content/docs' / (gen.guide_id(name) + '.md')
                    text = guide.read_text()
                    self.assertIn('Current published SDK:', text)
                    self.assertIn(meta['tag'], text)
                    self.assertIn(meta['source_sha'], text)
                    self.assertIn('Dated status and candidate records below', text)
                final_release, final_data = fixture.build(tag='sdk-v2.4.0')
                final_meta = fixture.r.resolve(fixture.REPO, fixture.client_for(final_release, final_data), fixture.fake_git)
                gen.use_published_metadata(final_meta)
                gen.generate(ROOT, catalogue, output)
                guide = output / 'src/content/docs' / (gen.guide_id('docs/releases/sdk-v2.4.0.md') + '.md')
                self.assertIn('[sdk-v2.4.0](' + gen.RELEASES + 'sdk-v2.4.0)', guide.read_text())
                gen.use_published_metadata(meta)
                with self.assertRaisesRegex(ValueError, 'Published release source does not match'):
                    gen.generate(ROOT, catalogue, output, meta['tag'])
        finally:
            gen.PUBLISHED_RELEASE, gen.PUBLISHED_DOWNLOAD_TAG, gen.PUBLISHED_METADATA = saved

    @classmethod
    def setUpClass(cls):
        cls.catalogue = json.loads((ROOT / 'dist/sdk-catalogue.json').read_text())

    def generate(self, root, output):
        gen.generate(root, self.catalogue, output)

    def test_deterministic_and_complete(self):
        with tempfile.TemporaryDirectory(prefix='site-test-') as work:
            output = Path(work)
            self.generate(ROOT, output)
            first = snapshot(output)
            self.generate(ROOT, output)
            self.assertEqual(first, snapshot(output))
            docs = output / 'src/content/docs'
            records = list((docs / 'reference').glob('*.md'))
            self.assertEqual({p.stem for p in records}, {r['id'] for r in self.catalogue['records']})
            self.assertEqual(len(records), len(self.catalogue['records']))
            services = list((docs / 'services').glob('*.md'))
            self.assertEqual({p.stem for p in services},
                             {gen.service_id(r['service']) for r in self.catalogue['records']})
            check_markdown_links(docs, output / 'public')
            check_leaks(output)
            for page in docs.rglob('*.md'):
                text = page.read_text()
                self.assertIn('development preview', text)
                self.assertIn(gen.RELEASES + gen.PUBLISHED_DOWNLOAD_TAG + ')', text)
                for key in ('sdk_version', 'game_pack_version', 'source_sha'):
                    self.assertIn(self.catalogue[key], text)
            self.assertEqual(json.loads((output / 'public/sdk-catalogue.json').read_text()), self.catalogue)
            print('Verified records:', len(records), 'services:', len(services))

    def test_task_search_phrases_are_visible_and_weighted(self):
        expected = {
            'sco-api-run-on-game-thread': 'schedule game thread task',
            'sco-storage-v1-query': 'open SQL cursor',
            'sco-api-query-service': 'find service table',
            'sc-actors-v1-spawn-npc': 'spawn NPC',
            'sc-spawn-service-v1-spawn-near-player': 'spawn a ship',
            'sc-inventory-v1-snapshot': 'read inventory',
            'sc-container-contents-v1-snapshot': 'read container contents',
        }
        self.assertEqual(set(gen.TASK_TERMS), set(expected))
        routes = {name: gen.guide_id(name) for name in gen.inputs(ROOT)}
        records = {record['id']: record for record in self.catalogue['records']}
        for identifier, phrase in expected.items():
            with self.subTest(identifier=identifier):
                text = gen.render_record(records[identifier], self.catalogue, routes)
                weighted = re.findall(r'<p data-pagefind-weight="([0-9]+)">([^<]+)</p>', text)
                self.assertEqual(len(weighted), 1)
                self.assertGreater(int(weighted[0][0]), 1)
                self.assertIn(phrase, html.unescape(weighted[0][1]))
                self.assertNotIn('teleport', weighted[0][1].lower())
                self.assertIn('not an additional API alias', text)
                self.assertIn('ABI pins prove layout, not semantic behavior or a live game session.', text)
                for limitation in records[identifier]['limitations']:
                    self.assertIn(gen.rewrite_links(limitation, self.catalogue['contract_context']['source'],
                                                   self.catalogue['source_sha'], routes), text)

    def test_search_categories_cover_every_generated_page(self):
        categories = {'reference': 'APIs', 'services': 'Services', 'languages': 'Languages',
                      'guides': 'Guides', 'hubs': 'Overview', 'natives.md': 'APIs', 'index.md': 'Overview'}
        seen = set()
        with tempfile.TemporaryDirectory(prefix='site-search-types-') as work:
            output = Path(work)
            self.generate(ROOT, output)
            docs = output / 'src/content/docs'
            for path in docs.rglob('*.md'):
                relative = path.relative_to(docs)
                with self.subTest(page=relative.as_posix()):
                    expected = categories[relative.parts[0]]
                    text = path.read_text()
                    self.assertEqual(re.findall(r'data-pagefind-filter="([^"]+)"', text), ['Type:' + expected])
                    self.assertIn('<span data-pagefind-filter="Type:' + expected + '">Source-backed inventory '
                                  'is not a runtime or in-game verification claim.</span>', text)
                    seen.add(expected)
        self.assertEqual(seen, {'APIs', 'Services', 'Languages', 'Guides', 'Overview'})
        with self.assertRaisesRegex(ValueError, 'Unknown search page type'):
            gen.page('Title', 'Body', self.catalogue, None, 'Unsupported')

    def test_snapshot_notice_is_visible_but_not_searchable(self):
        notice = gen.banner(self.catalogue, None, 'APIs')
        self.assertTrue(notice.startswith('<div data-pagefind-ignore="index">\n\n:::caution['))
        self.assertTrue(notice.endswith('\n:::\n\n</div>\n\n'))
        self.assertIn('data-pagefind-filter="Type:APIs"', notice)
        self.assertIn('development preview', notice)
        self.assertIn(self.catalogue['source_sha'], notice)
        self.assertIn('not a runtime or in-game verification claim', notice)
        self.assertNotRegex(notice, r'\bhidden\b|aria-hidden|display\s*:\s*none')
        page = gen.page('Title', 'Indexed contract purpose', self.catalogue, None, 'APIs')
        self.assertIn(notice + 'Indexed contract purpose', page)
        self.assertEqual(page.count('data-pagefind-ignore'), 1)

    def test_mapping_status_is_authoritative(self):
        record = deepcopy(self.catalogue['records'][0])
        for language in record['language_mappings']:
            record['language_mappings'][language]['status'] = 'unsupported'
            rendered = gen.mapping_rows(record, self.catalogue['source_sha'])
            row = next(line for line in rendered.splitlines() if line.startswith('| ' + language + ' |'))
            self.assertIn('| **unsupported** |', row)
            self.assertNotRegex(row, r'\| \*\*supported\*\* \|')
        record['language_mappings']['lua']['status'] = 'supported'
        with self.assertRaises(ValueError):
            gen.mapping_rows(record, self.catalogue['source_sha'])

    def test_invalid_inputs_fail_closed(self):
        for key, value in [('source_sha', 'main'), ('schema_version', 3)]:
            bad = deepcopy(self.catalogue)
            bad[key] = value
            with tempfile.TemporaryDirectory(prefix='site-test-') as work:
                with self.assertRaises(ValueError):
                    gen.generate(ROOT, bad, Path(work))
        bad = deepcopy(self.catalogue)
        bad['status_registry_sha256'] = 'invalid'
        with tempfile.TemporaryDirectory(prefix='site-test-') as work:
            with self.assertRaises(ValueError):
                gen.generate(ROOT, bad, Path(work))
        with tempfile.TemporaryDirectory(prefix='site-test-') as work:
            root = Path(work)
            (root / 'site').mkdir()
            (root / 'sdk').mkdir()
            shutil.copyfile(ROOT / 'sdk/status_registry.json', root / 'sdk/status_registry.json')
            (root / 'site/public-inputs.json').write_text(json.dumps({'schema_version': 1, 'docs': ['docs/design/hidden.md']}))
            with self.assertRaises(ValueError):
                gen.generate(root, self.catalogue, root / 'output')
        with tempfile.TemporaryDirectory(prefix='site-test-registry-') as work:
            root = Path(work)
            (root / 'sdk').mkdir()
            (root / 'sdk/status_registry.json').write_bytes(b'{"schema_version":1}')
            with self.assertRaisesRegex(ValueError, 'digest does not match'):
                gen.generate(root, self.catalogue, root / 'output')

    def test_leak_detector_negative_arms(self):
        with tempfile.TemporaryDirectory(prefix='site-test-') as work:
            output = Path(work)
            for data in (SENTINELS[0].encode(), gzip.compress(SENTINELS[0].encode())):
                (output / 'fragment.pf_fragment').write_bytes(data)
                with self.assertRaises(AssertionError):
                    check_leaks(output, SENTINELS)
            (output / 'fragment.pf_fragment').unlink()
            (output / (SENTINELS[1] + '.txt')).write_text('ordinary content')
            with self.assertRaises(AssertionError):
                check_leaks(output, SENTINELS)

    def test_private_sentinels_never_publish(self):
        with tempfile.TemporaryDirectory(prefix='site-sentinel-') as work:
            root = Path(work)
            # The test copies docs to reproduce future unlisted additions; production
            # gen reads only the explicit input list and never copies a repository tree.
            shutil.copytree(ROOT / 'docs', root / 'docs')
            (root / 'docs/design').mkdir(exist_ok=True)
            (root / ('docs/design/' + SENTINELS[1] + '.md')).write_text(SENTINELS[0])
            with (root / 'docs/building.md').open('a') as doc:
                doc.write('\n' + SENTINELS[0] + '\n')
            (root / 'sdk').mkdir()
            shutil.copyfile(ROOT / 'sdk/components.json', root / 'sdk/components.json')
            shutil.copyfile(ROOT / 'sdk/status_registry.json', root / 'sdk/status_registry.json')
            shutil.copyfile(ROOT / gen.NATIVE_INDEX, root / gen.NATIVE_INDEX)
            output = root / 'site'
            output.mkdir()
            for name in ('public-inputs.json', 'package.json', 'package-lock.json', 'astro.config.mjs'):
                shutil.copyfile(ROOT / 'site' / name, output / name)
            git_track(root)
            (output / 'src').mkdir()
            (output / 'scripts').mkdir()
            shutil.copyfile(ROOT / 'site/scripts/sanitize_search.py', output / 'scripts/sanitize_search.py')
            shutil.copyfile(ROOT / 'site/src/content.config.ts', output / 'src/content.config.ts')
            for name in ('styles', 'components', 'search', 'landing'):
                shutil.copytree(ROOT / 'site/src' / name, output / 'src' / name)
            self.generate(root, output)
            shutil.copyfile(ROOT / 'site/public/favicon.svg', output / 'public/favicon.svg')
            shutil.copyfile(ROOT / 'site/public/starlight-LICENSE.txt', output / 'public/starlight-LICENSE.txt')
            check_leaks(output / 'src/content/docs', FORBIDDEN + SENTINELS)
            check_leaks(output / 'public', FORBIDDEN + SENTINELS)
            if shutil.which('node') and (ROOT / 'site/node_modules/.bin/astro').is_file():
                # A real installation keeps Astro virtual-module identities local
                # to this fixture. Symlinked node_modules mixes cache roots on Linux.
                npm_cmd = shutil.which('npm') or 'npm'
                installed = subprocess.run([npm_cmd, 'ci', '--offline'], cwd=output, capture_output=True, text=True)
                self.assertEqual(installed.returncode, 0, installed.stdout + installed.stderr)
                result = subprocess.run([npm_cmd, 'run', 'build'], cwd=output, capture_output=True, text=True,
                                        env={**os.environ, 'ASTRO_TELEMETRY_DISABLED': '1'})
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                for token in SENTINELS:
                    self.assertNotIn(token, result.stdout + result.stderr, 'Private sentinel in build logs')
                pages, links = check_built(output / 'dist')
                check_leaks(output / 'dist', FORBIDDEN + SENTINELS)
                print('Sentinel build: clean HTML, static JSON and compressed Pagefind;', pages, 'pages;', links, 'links')
            elif shutil.which('node'):
                self.fail('Node available but dependencies missing; run npm ci in site first')
            else:
                print('Sentinel generated content clean; built sentinel check skipped: Node unavailable')

    def test_home_task_links_and_hierarchy(self):
        with tempfile.TemporaryDirectory(prefix='site-home-') as work:
            output = Path(work)
            self.generate(ROOT, output)
            home = (output / 'src/content/docs/index.md').read_text()
            self.assertIn('title: "sco-core SDK"', home)
            self.assertIn('\ntemplate: splash\n', home)
            hero = json.loads(re.search(r'^hero: (.+)$', home, re.M)[1])
            self.assertEqual(hero['title'], 'sco-core SDK')
            self.assertEqual(hero['tagline'], 'Public contracts for Star Citizen offline plugins')
            self.assertEqual([(action['link'], action['text']) for action in hero['actions']], [
                (gen.route('guides/getting-started'), 'Start a plugin'),
                (gen.route('hubs/reference'), 'Browse API')])
            self.assertNotIn('sco-intro', home)
            self.assertIsNone(re.search(r'^# ', home, re.M))
            snapshot_start = home.index(':::caution[Catalogue snapshot')
            self.assertLess(snapshot_start, home.index('## Libraries'))
            self.assertLess(home.index('data-hub="integration"'), home.index('## Releases'))
            self.assertLess(home.index('## Verify a download'), home.index('## Reading rules'))
            self.assertLess(home.index('## Reading rules'), home.index('## Remaining gaps'))
            for rule in self.catalogue['contract_context']['reading_rules'].splitlines():
                self.assertIn(rule, home)
            routes = {name: gen.guide_id(name) for name in gen.inputs(ROOT)}
            self.assertIn(gen.rewrite_links(self.catalogue['contract_context']['remaining_gaps'],
                                           self.catalogue['contract_context']['source'],
                                           self.catalogue['source_sha'], routes), home)
            self.assertNotIn('template: splash', (output / 'src/content/docs/guides/getting-started.md').read_text())
            check_markdown_links(output / 'src/content/docs', output / 'public')

    def test_home_portal_matches_release_scope(self):
        notes = (ROOT / 'docs/releases' / (gen.PUBLISHED_RELEASE + '.md')).read_text()
        block = re.search(r'<!-- release-scope:start -->\s*```json\n(.+?)\n```\s*<!-- release-scope:end -->', notes, re.S)
        if block is None:
            self.fail('Published release notes have no release-scope block')
        scope = json.loads(block[1])
        defined = json.loads((ROOT / 'sdk/components.json').read_text())['profiles']
        expected = []
        for name in scope['profiles']:
            version = scope['game_pack_version' if defined[name]['version_from'] == 'include/sc_game_pack.h' else 'sdk_version']
            expected.append({'data-profile': name, 'data-version': version,
                             'data-archive': defined[name]['zip'].format(version=version, game_pack_version=version) + '.zip'})

        class Portal(HTMLParser):
            def __init__(self):
                super().__init__()
                self.cards, self.tracks, self.track_links, self.current = [], {}, {}, None

            def handle_starttag(self, tag, attrs):
                attrs = dict(attrs)
                if tag == 'li' and 'data-profile' in attrs:
                    self.cards.append({k: attrs[k] for k in ('data-profile', 'data-archive', 'data-version')})
                if tag == 'section' and 'data-track' in attrs:
                    self.current = attrs['data-track']
                    self.tracks[self.current] = attrs
                    self.track_links[self.current] = []
                if tag == 'a' and self.current:
                    self.track_links[self.current].append(attrs['href'])

            def handle_endtag(self, tag):
                if tag == 'section':
                    self.current = None

        with tempfile.TemporaryDirectory(prefix='site-portal-') as work:
            output = Path(work)
            self.generate(ROOT, output)
            index = (output / 'src/content/docs/index.md').read_text()
            portal = Portal()
            portal.feed(index)
            self.assertEqual(portal.cards, expected)
            self.assertEqual(set(portal.tracks), {'published', 'preview'})
            self.assertEqual(portal.tracks['published']['data-tag'], scope['tag'])
            self.assertEqual(portal.tracks['published']['data-download-tag'], gen.PUBLISHED_DOWNLOAD_TAG)
            self.assertIn(gen.RELEASES + gen.PUBLISHED_DOWNLOAD_TAG, portal.track_links['published'])
            self.assertEqual(portal.tracks['preview']['data-source-sha'], self.catalogue['source_sha'])
            self.assertIn(gen.COMMITS + self.catalogue['source_sha'], portal.track_links['preview'])
            downloads = gen.DOWNLOADS + gen.PUBLISHED_DOWNLOAD_TAG + '/'
            for card in expected:
                section = index.split('### Verify ' + card['data-profile'] + '\n', 1)[1].split('\n```\n', 1)[0]
                self.assertIn(downloads + 'SHA256SUMS', section)
                self.assertIn(downloads + card['data-archive'], section)
                self.assertIn('$2 == "' + card['data-archive'] + '"', section)
                self.assertIn('href="' + gen.BASE + '/#verify-' + card['data-profile'] + '"', index)
            check_markdown_links(output / 'src/content/docs', output / 'public')
            check_leaks(output)
            release = gen.release_portal(ROOT, self.catalogue, 'sdk-v9.9.9', {n: gen.guide_id(n) for n in gen.inputs(ROOT)})
            self.assertIn('data-track="release"', release)
            self.assertNotIn('data-track="preview"', release)

    def test_published_download_tag(self):
        self.assertTrue(gen.PUBLISHED_DOWNLOAD_TAG.startswith(gen.PUBLISHED_RELEASE))
        saved = gen.PUBLISHED_RELEASE, gen.PUBLISHED_DOWNLOAD_TAG
        try:
            for download, status in (('sdk-v2.4.0', 'Published release'), ('sdk-v2.4.0-rc.1', 'Published release candidate'),
                                     ('sdk-v2.4.0-rc.12', 'Published release candidate')):
                with self.subTest(download=download):
                    gen.PUBLISHED_RELEASE, gen.PUBLISHED_DOWNLOAD_TAG = 'sdk-v2.4.0', download
                    self.assertEqual(gen.published_stage()[0], status)
            for download in ('sdk-v2.3.0', 'sdk-v2.4.0-rc.0', 'sdk-v2.4.0-rc1', 'sdk-v2.4.0-beta.1', 'sdk-v2.4.0-rc.1x',
                             'sdk-v2.4.01'):
                with self.subTest(download=download), self.assertRaises(ValueError):
                    gen.PUBLISHED_RELEASE, gen.PUBLISHED_DOWNLOAD_TAG = 'sdk-v2.4.0', download
                    gen.published_stage()
        finally:
            gen.PUBLISHED_RELEASE, gen.PUBLISHED_DOWNLOAD_TAG = saved

    def test_release_portal_fails_closed(self):
        good = ('<!-- release-scope:start -->\n```json\n' + json.dumps(
            {'tag': 'sdk-v2.1.0', 'sdk_version': '2.1.0', 'game_pack_version': '0.4.0', 'plugin_abi': '1.1',
             'profiles': ['core']}) + '\n```\n<!-- release-scope:end -->\n')
        self.assertEqual(gen.release_scope(good, 'sdk-v2.1.0')['profiles'], ['core'])
        for label, text, tag in [('missing', '# notes\n', 'sdk-v2.1.0'), ('twice', good + good, 'sdk-v2.1.0'),
                                 ('wrong tag', good, 'sdk-v2.2.0'),
                                 ('extra key', good.replace('"profiles"', '"extra": 1, "profiles"'), 'sdk-v2.1.0'),
                                 ('duplicate profile', good.replace('["core"]', '["core", "core"]'), 'sdk-v2.1.0'),
                                 ('bad version', good.replace('"2.1.0"', '"2.1"'), 'sdk-v2.1.0')]:
            with self.subTest(case=label), self.assertRaises(ValueError):
                gen.release_scope(text, tag)
        unknown = {'tag': 'sdk-v2.1.0', 'sdk_version': '2.1.0', 'game_pack_version': '0.4.0',
                   'plugin_abi': '1.1', 'profiles': ['core', 'undeclared-profile']}
        with self.assertRaises(ValueError):
            gen.published_profiles(ROOT, unknown)

    def test_dependency_license_inventory(self):
        from licenses import inventory
        self.assertEqual(json.loads((ROOT / 'site/dependency-licenses.json').read_text()), inventory(ROOT / 'site'))

    def test_search_wasm_sanitizer(self):
        from sanitize_search import sanitize, NEW, OLD
        with tempfile.TemporaryDirectory(prefix='site-wasm-') as work:
            output = Path(work)
            (output / 'pagefind').mkdir()
            path = output / 'pagefind/wasm.test.pagefind'
            # Valid WASM with one passive data segment; length/offset unchanged.
            data = b'\x00asm\x01\x00\x00\x00\x0b\x0a\x01\x01\x07' + OLD
            path.write_bytes(gzip.compress(data, mtime=0))
            self.assertEqual(sanitize(output), 1)
            self.assertEqual(gzip.decompress(path.read_bytes()), data.replace(OLD, NEW))
            self.assertEqual(sanitize(output), 0)
            path.write_bytes(b'not WebAssembly')
            with self.assertRaises(ValueError):
                sanitize(output)
            # A custom section is not a data string and must not be rewritten.
            path.write_bytes(b'\x00asm\x01\x00\x00\x00\x00\x08\x00' + OLD)
            with self.assertRaises(ValueError):
                sanitize(output)
        if shutil.which('node') and BUILT:
            result = subprocess.run(['node', '--input-type=module', '-e',
                "import fs from 'node:fs'; import zlib from 'node:zlib'; "
                "for (const p of fs.readdirSync(process.argv[1]).filter(x=>/^wasm.*\\.pagefind$/.test(x))) {"
                "let b=fs.readFileSync(process.argv[1]+'/'+p); if(b[0]===31&&b[1]===139)b=zlib.gunzipSync(b);"
                "const i=b.indexOf(Buffer.from([0,97,115,109,1,0,0,0])); if(i<0)throw Error('No WASM');"
                "await WebAssembly.compile(b.subarray(i)); console.log('Valid built WASM: '+p);}",
                str(BUILT / 'pagefind')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            print(result.stdout.strip())

    def test_language_views_and_stable_routes(self):
        with tempfile.TemporaryDirectory(prefix='site-language-') as work:
            output = Path(work)
            self.generate(ROOT, output)
            docs = output / 'src/content/docs'
            for language in gen.LANGUAGES:
                text = (docs / ('languages/' + language + '.md')).read_text()
                for record in self.catalogue['records']:
                    self.assertIn(gen.route('reference/' + record['id']), text)
                    self.assertIn('**' + record['language_mappings'][language]['status'] + '**', text)
                self.assertIn('Unsupported entries remain explicit', text)
            npc = next(r for r in self.catalogue['records'] if r['id'] == 'sc-actors-v1-spawn-npc')
            self.assertEqual(npc['language_mappings']['lua']['status'], 'unsupported')
            lua = (docs / 'languages/lua.md').read_text()
            self.assertIn('| [' + npc['symbol'] + '](' + gen.route('reference/' + npc['id'])
                          + ') | ' + npc['service'] + ' | **unsupported** |', lua)
            routes = {p.relative_to(docs).as_posix() for p in docs.rglob('*.md')}
            reordered = deepcopy(self.catalogue)
            reordered['records'].reverse()
            gen.generate(ROOT, reordered, output)
            self.assertEqual(routes, {p.relative_to(docs).as_posix() for p in docs.rglob('*.md')})

    def test_retired_search_target_and_duplicate_record_refused(self):
        for identifier in gen.TASK_TERMS:
            bad = deepcopy(self.catalogue)
            bad['records'] = [r for r in bad['records'] if r['id'] != identifier]
            with tempfile.TemporaryDirectory(prefix='site-invalid-') as work:
                with self.subTest(retired=identifier), self.assertRaisesRegex(ValueError, 'retired or missing record'):
                    gen.generate(ROOT, bad, Path(work))
        bad = deepcopy(self.catalogue)
        bad['records'].append(deepcopy(bad['records'][0]))
        with tempfile.TemporaryDirectory(prefix='site-invalid-') as work:
            with self.assertRaisesRegex(ValueError, 'duplicate or unsafe record'):
                gen.generate(ROOT, bad, Path(work))

    def test_particle_field_model(self):
        self.assertIsNotNone(shutil.which('node'), 'Node required for particle model verification')
        result = subprocess.run(['node', str(ROOT / 'site/scripts/test_field_model.mjs')],
                                capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        print(result.stdout.strip())

    def test_actual_search_and_keyboard_navigation(self):
        if BUILT is None:
            self.skipTest('Use --built site/dist for browser navigation verification')
        self.assertIsNotNone(shutil.which('node'), 'Node required for browser navigation')
        result = subprocess.run(['node', str(ROOT / 'site/scripts/test_navigation.mjs'), str(BUILT),
                                 '--defer-search-idle'],
                                capture_output=True, text=True, timeout=180)
        if result.returncode == 77:
            self.skipTest(result.stderr.strip())
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        print(result.stdout.strip())

    def allowlist_fixture(self, files, docs, init_git=True):
        work = tempfile.TemporaryDirectory(prefix='site-allow-')
        self.addCleanup(work.cleanup)
        root = Path(work.name).resolve()
        for name, text in files.items():
            (root / name).parent.mkdir(parents=True, exist_ok=True)
            (root / name).write_text(text)
        (root / 'site').mkdir(exist_ok=True)
        (root / 'site/public-inputs.json').write_text(json.dumps({'schema_version': 1, 'docs': docs}))
        if init_git:
            git_track(root)
        return root

    def test_allowlist_clean_pass(self):
        docs = ['docs/a.md', 'docs/sub/b.md', 'docs/releases/sdk-v9.9.9.md']
        root = self.allowlist_fixture({name: '# ok\n' for name in docs}, docs)
        self.assertEqual(gen.inputs(root), sorted(docs))
        # Only the current release's notes are published; older notes stay in the repository as history.
        self.assertEqual([name for name in gen.inputs(ROOT) if name.startswith('docs/releases/')],
                         ['docs/releases/' + gen.PUBLISHED_RELEASE + '.md'])
        self.assertIn('docs/host-source-sc.md', gen.inputs(ROOT))

    def test_allowlist_refuses_unsafe_inputs(self):
        refused_names = ['docs/legal/x.md', 'docs/design/x.md', 'docs/releases/notes.md',
                         'docs/releases/sdk-v2.1.0-rc1.md', 'docs/releases/old/sdk-v1.0.0.md',
                         'docs/releases/sdk-v2.1.md', 'docs/a.txt']
        for name in refused_names:
            with self.subTest(name=name):
                root = self.allowlist_fixture({name: '# tracked\n'}, [name])
                with self.assertRaises(ValueError):
                    gen.inputs(root)
        for name in ('docs/%2e%2e/x.md', 'docs/./a.md', 'docs//a.md', 'docs/../site/a.md', '../docs/a.md'):
            with self.subTest(name=name):
                root = self.allowlist_fixture({'docs/a.md': '# a\n'}, [name])
                with self.assertRaises(ValueError):
                    gen.inputs(root)

    def test_allowlist_requires_tracked_regular_files(self):
        def untracked(root):
            (root / 'docs/late.md').write_text('# never added\n')

        def tracked_symlink(root):
            os.symlink('a.md', root / 'docs/link.md')
            git_track(root)

        def symlinked_directory(root):
            (root / 'outside').mkdir()
            (root / 'docs/sub').rename(root / 'outside/sub')
            os.symlink('../outside/sub', root / 'docs/sub')

        cases = [('untracked', 'docs/late.md', untracked),
                 ('tracked symlink', 'docs/link.md', tracked_symlink),
                 ('symlinked directory', 'docs/sub/b.md', symlinked_directory)]
        for label, name, mutate in cases:
            with self.subTest(case=label):
                root = self.allowlist_fixture({'docs/a.md': '# a\n', 'docs/sub/b.md': '# b\n'}, [name])
                mutate(root)
                with self.assertRaises(ValueError):
                    gen.inputs(root)
        root = self.allowlist_fixture({'docs/a.md': '# a\n'}, ['docs/a.md'], init_git=False)
        with self.assertRaises(ValueError):
            gen.inputs(root)
        outer = self.allowlist_fixture({'inner/docs/a.md': '# a\n'}, ['docs/a.md'])
        (outer / 'inner/site').mkdir()
        (outer / 'inner/site/public-inputs.json').write_text(json.dumps({'schema_version': 1, 'docs': ['docs/a.md']}))
        with self.assertRaises(ValueError):
            gen.inputs(outer / 'inner')

    def test_allowlist_refuses_route_collision(self):
        self.assertEqual(gen.guide_id('docs/releases/sdk-v2.1.0.md'), 'guides/releases/sdk-v210')
        for docs in (['docs/game.md', 'docs/game/index.md'], ['docs/a.b.md', 'docs/ab.md']):
            with self.subTest(docs=docs):
                root = self.allowlist_fixture({name: '# x\n' for name in docs}, docs)
                with self.assertRaises(ValueError):
                    gen.inputs(root)

    def test_encoded_link_traversal_refused(self):
        sha = self.catalogue['source_sha']
        for target in ('%2e%2e/%2e%2e/secret.md', '..%2F..%2Fsecret.md', '%2E%2E/x.md', '%5c..%5csecret.md',
                       '../../../outside.md', gen.SOURCE_ROOT + 'main/%2e%2e/x.md'):
            with self.subTest(target=target), self.assertRaises(ValueError):
                gen.rewrite_links('[x](' + target + ')', 'docs/a.md', sha, {})
        self.assertEqual(gen.rewrite_links('[x](b.md)', 'docs/a.md', sha, {'docs/b.md': 'guides/b'}),
                         '[x](' + gen.route('guides/b') + ')')
        self.assertEqual(gen.rewrite_links('[x](https://example.com/a%2Fb)', 'docs/a.md', sha, {}),
                         '[x](https://example.com/a%2Fb)')
        with tempfile.TemporaryDirectory(prefix='site-built-') as work:
            built = Path(work)
            (built / 'pagefind').mkdir()
            (built / 'pagefind/a.pf_index').write_bytes(b'index')
            (built / 'sdk-catalogue.json').write_text('{}')
            (built / gen.NATIVE_DOWNLOAD).write_text('{}')
            shutil.copyfile(ROOT / 'site/public/starlight-LICENSE.txt', built / 'starlight-LICENSE.txt')
            notice_link = '<a href="' + gen.BASE + '/starlight-LICENSE.txt">license</a>'
            (built / 'index.html').write_text('<a href="' + gen.BASE + '/">home</a>')
            with self.assertRaisesRegex(AssertionError, 'Missing built Starlight notice link'):
                check_built(built)
            (built / 'index.html').write_text('<img src="' + gen.BASE + '/starlight-LICENSE.txt">')
            with self.assertRaisesRegex(AssertionError, 'Missing built Starlight notice link'):
                check_built(built)
            (built / 'index.html').write_text('<a href="' + gen.BASE + '/">home</a>' + notice_link)
            self.assertEqual(check_built(built), (1, 2))
            (built / 'index.html').write_text(notice_link + '<a href="' + gen.BASE + '/%2e%2e/%2e%2e/secret">x</a>')
            with self.assertRaisesRegex(AssertionError, 'Traversal'):
                check_built(built)

    def test_private_markers_refused_plain_and_gzip(self):
        with tempfile.TemporaryDirectory(prefix='site-markers-') as work:
            output = Path(work)
            (output / 'page.html').write_text('public engineering documentation')
            self.assertEqual(check_leaks(output), 1)
            for token in FORBIDDEN:
                for data in (token.encode(), gzip.compress(token.encode(), mtime=0)):
                    with self.subTest(token=token, compressed=data != token.encode()):
                        (output / 'fragment.pf_fragment').write_bytes(data)
                        with self.assertRaises(AssertionError):
                            check_leaks(output)

    def test_gzip_decompression_bounds(self):
        at_limit = gzip.compress(b'\0' * GZIP_FILE_LIMIT, mtime=0)
        self.assertEqual(len(gunzip_bounded(at_limit, GZIP_FILE_LIMIT)), GZIP_FILE_LIMIT)
        members = gzip.compress(b'first ', mtime=0) + gzip.compress(SENTINELS[0].encode(), mtime=0)
        self.assertEqual(gunzip_bounded(members, 1024), b'first ' + SENTINELS[0].encode())
        for bad in (gzip.compress(b'x' * 64, mtime=0)[:-12], gzip.compress(b'x', mtime=0) + b'junk'):
            with self.assertRaises(AssertionError):
                gunzip_bounded(bad, 1024)
        with tempfile.TemporaryDirectory(prefix='site-gzip-') as work:
            output = Path(work)
            (output / 'members.pf_fragment').write_bytes(members)
            with self.assertRaises(AssertionError):
                check_leaks(output, SENTINELS)
            (output / 'members.pf_fragment').unlink()
            (output / 'bomb.pf_fragment').write_bytes(gzip.compress(b'\0' * (GZIP_FILE_LIMIT + 1), mtime=0))
            with self.assertRaises(AssertionError):
                check_leaks(output, SENTINELS)
            (output / 'bomb.pf_fragment').unlink()
            for index in range(3):
                (output / ('part-%d.pf_fragment' % index)).write_bytes(gzip.compress(b'\0' * 1000, mtime=0))
            self.assertEqual(check_leaks(output, SENTINELS, file_limit=1024, total_limit=3000), 3)
            with self.assertRaises(AssertionError):
                check_leaks(output, SENTINELS, file_limit=1024, total_limit=2999)

    def test_starlight_notice_is_required_and_exact(self):
        with tempfile.TemporaryDirectory(prefix='site-notice-') as work:
            root = Path(work)
            with self.assertRaises(AssertionError):
                check_starlight_notice(root)
            notice = root / 'starlight-LICENSE.txt'
            notice.write_bytes((ROOT / 'site/public/starlight-LICENSE.txt').read_bytes())
            check_starlight_notice(root)
            notice.write_bytes(b'changed notice')
            with self.assertRaises(AssertionError):
                check_starlight_notice(root)

    def test_supplied_build(self):
        if BUILT is None:
            self.skipTest('Use --built site/dist for built-output verification')
        pages, links = check_built(BUILT)
        expected = (len(self.catalogue['records']) + len({r['service'] for r in self.catalogue['records']})
                    + len(gen.inputs(ROOT)) + len(gen.LANGUAGES) + len(gen.HUBS) + 1 + 1)  # hubs, natives, home
        self.assertEqual(pages, expected + 1 + len(SHORT_ROUTES))  # Starlight's 404 page and the short-URL pages.
        for identifier in PINNED_ROUTES + tuple('hubs/' + key for key, _, _ in gen.HUBS) + ('natives',):
            self.assertTrue((BUILT / identifier / 'index.html').is_file(), identifier)
        for short, target in SHORT_ROUTES.items():
            self.assertTrue((BUILT / target / 'index.html').is_file(), target)
            self.assertIn(gen.route(target), (BUILT / short / 'index.html').read_text(), short)
        index = json.loads((ROOT / gen.NATIVE_INDEX).read_text())
        self.assertEqual((BUILT / gen.NATIVE_DOWNLOAD).read_bytes(), (ROOT / gen.NATIVE_INDEX).read_bytes())
        natives = (BUILT / 'natives/index.html').read_text()
        self.assertEqual(sorted(re.findall(r'<tr id="(_0x[0-9A-F]{16})"', natives)),
                         sorted(entry['hash'] for entry in index['entries']))
        scripts = re.findall(r'<script[^>]*>', natives)
        self.assertTrue(all('src=' not in tag or gen.BASE + '/_astro/' in tag for tag in scripts), scripts)
        print('Built site:', pages, 'HTML pages;', links, 'internal links resolved; Pagefind present')

    def test_hubs_group_every_page_without_moving_routes(self):
        with tempfile.TemporaryDirectory(prefix='site-hubs-') as work:
            output = Path(work)
            self.generate(ROOT, output)
            docs = output / 'src/content/docs'
            groups = json.loads((output / gen.SIDEBAR).read_text())
            self.assertEqual(groups[0], {'label': 'Home', 'slug': 'index'})
            self.assertEqual([group['label'] for group in groups[1:]],
                             ['Start & SDK profiles', 'Language SDKs & cookbooks', 'Native & service reference',
                              'Host integration & AI agents'])
            self.assertTrue(all(group.get('collapsed') is True for group in groups[1:]))

            def items(entries):
                for entry in entries:
                    yield from items(entry['items']) if 'items' in entry else [entry]
            links = list(items(groups))
            slugs = [entry['slug'] for entry in links]
            self.assertEqual(len(slugs), len(set(slugs)))
            for entry in links:
                self.assertTrue((docs / (entry['slug'] + '.md')).is_file(), entry['slug'])
                self.assertNotIn('`', entry['label'])
            expected = ({gen.route_key(gen.guide_id(name)) for name in gen.inputs(ROOT)}
                        | {'services/' + gen.service_id(r['service']) for r in self.catalogue['records']}
                        | {'languages/' + language for language in gen.LANGUAGES} | {'natives', 'index'}
                        | {'hubs/' + key for key, _, _ in gen.HUBS})
            self.assertEqual(set(slugs), expected)
            for identifier in PINNED_ROUTES:
                self.assertTrue((docs / (identifier + '.md')).is_file(), identifier)
            for page in docs.rglob('*.md'):
                titles = re.findall(r'^title: (.+)$', page.read_text(), re.M)
                self.assertEqual(len(titles), 1, page)
                self.assertNotIn('`', titles[0], page)
            api = (docs / 'guides/api-v1.md').read_text()
            self.assertIn('title: "Plugin API v1 reference (sco_api.h)"', api)
            home = (docs / 'index.md').read_text()
            self.assertEqual(re.findall(r'data-hub="([a-z]+)"', home), [key for key, _, _ in gen.HUBS])
            self.assertLess(home.index('data-hub="integration"'), home.index('data-track="published"'))
            self.assertLess(home.index('data-hub="integration"'), home.index('data-profile='))
            start = (docs / 'hubs/start.md').read_text()
            self.assertEqual(len(re.findall(r'<li class="sco-profile"', start)),
                             len(re.findall(r'<li class="sco-profile"', home)))
            check_markdown_links(docs, output / 'public')
        docs = gen.inputs(ROOT)
        for bad in (docs + ['docs/unassigned.md'], docs[1:]):
            with self.assertRaises(ValueError):
                gen.hub_docs(bad)

    def test_native_index_page_and_download(self):
        index = json.loads((ROOT / gen.NATIVE_INDEX).read_text())
        with tempfile.TemporaryDirectory(prefix='site-natives-') as work:
            output = Path(work)
            self.generate(ROOT, output)
            text = (output / 'src/content/docs/natives.md').read_text()
            self.assertEqual((output / 'public' / gen.NATIVE_DOWNLOAD).read_bytes(),
                             (ROOT / gen.NATIVE_INDEX).read_bytes())
            rows = re.findall(r'<tr id="(_0x[0-9A-F]{16})" tabindex="-1" data-subsystem="([a-z0-9_-]+)" data-tier="([1-4])"', text)
            self.assertEqual(len(rows), index['row_count'])
            self.assertEqual(rows, [(e['hash'], e['subsystem'], str(e['tier'])) for e in index['entries']])
            for entry in index['entries']:
                self.assertIn('href="' + gen.route('natives') + '?' + entry['hash'] + '"', text)
            known = {entry['hash'] for entry in index['entries']}
            for hexrun in re.finditer(r'0x[0-9A-Fa-f]+', text):
                self.assertEqual(text[hexrun.start() - 1], '_')
                self.assertIn('_0x' + hexrun.group()[2:].upper(), known)
            self.assertIn('Entries list row names and readiness only: no byte patterns, masks, offsets or addresses.', text)
            self.assertIn('Nothing here is verified in game unless an entry is marked so', text)
            for tier, about in index['tiers'].items():
                self.assertIn('sco-tier--' + tier + '">' + tier + ' · ' + about.split(':', 1)[0], text)
            self.assertEqual(re.findall(r'<script[^>]*>', text), ['<script>'])
            body = text[text.index('Every signature row'):]
            self.assertNotRegex(body, r'https?://(?!github\.com/scuba-sc/sco-core/blob/[0-9a-f]{40}/)')
            self.assertIn('tableOfContents: false', text)

    def test_native_index_fails_closed(self):
        good = json.loads((ROOT / gen.NATIVE_INDEX).read_text())

        def mutate(change):
            data = deepcopy(good)
            change(data)
            return data
        cases = {
            'pattern key': mutate(lambda d: d['entries'][0].update(pattern='48 8B ??')),
            'address hash': mutate(lambda d: d['entries'][0].update(hash='0x140001000')),
            'lowercase hash': mutate(lambda d: d['entries'][0].update(hash=d['entries'][0]['hash'].lower())),
            'row count': mutate(lambda d: d.update(row_count=d['row_count'] + 1)),
            'unknown tier': mutate(lambda d: d['entries'][0].update(tier=5)),
            'duplicate row': mutate(lambda d: d['entries'].append(deepcopy(d['entries'][0])) or d.update(row_count=len(d['entries']))),
            'source outside game': mutate(lambda d: d['entries'][0].update(source='../private/x_sigs.cpp')),
            'markup in id': mutate(lambda d: d['entries'][0].update(id='<script>')),
            'schema': mutate(lambda d: d.update(schema_version=2)),
        }
        for label, data in cases.items():
            with self.subTest(case=label), tempfile.TemporaryDirectory(prefix='site-native-bad-') as work:
                root = Path(work)
                (root / 'sdk').mkdir()
                (root / gen.NATIVE_INDEX).write_text(json.dumps(data))
                with self.assertRaises(ValueError):
                    gen.native_index(root)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--built', type=Path)
    args, rest = parser.parse_known_args()
    BUILT = args.built.resolve() if args.built else None
    unittest.main(argv=[__file__] + rest, verbosity=2)
