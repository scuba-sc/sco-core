#!/usr/bin/env python3
"""Standard-library generation, publication-boundary and built-output checks."""
import argparse
from copy import deepcopy
import gzip
from html.parser import HTMLParser
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest
from urllib.parse import unquote, urlsplit

import gen

ROOT = Path(__file__).resolve().parents[2]
BUILT = None
FORBIDDEN = ('/' + 'Users' + '/', 'task' + '-register', 'sco' + '-private')
SENTINELS = ('PRIVATE_SENTINEL_73c61ae9', 'PRIVATE_FILENAME_93b4167a')


def snapshot(root):
    return {p.relative_to(root).as_posix(): p.read_bytes()
            for p in sorted(root.rglob('*')) if p.is_file()}


def check_leaks(root, forbidden=FORBIDDEN):
    files = snapshot(root)
    if not files:
        raise AssertionError('Output tree is empty')
    for name, data in files.items():
        payloads = [data]
        # Pagefind fragments/indexes are gzip-compressed even without a .gz suffix.
        if data.startswith(b'\x1f\x8b'):
            payloads.append(gzip.decompress(data))
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
        for target in re.findall(r'(?<!!)\[[^\]\n]*\]\(([^\s)]+)\)', data.decode()):
            url = urlsplit(target)
            if target.startswith('#'):
                continue
            if url.scheme:
                if target.startswith(gen.SOURCE_ROOT):
                    if not re.match(re.escape(gen.SOURCE_ROOT) + r'[0-9a-f]{40}/', target):
                        raise AssertionError('Unpinned source link: ' + target)
                continue
            if url.path not in routes and url.path != gen.BASE + '/sdk-catalogue.json':
                raise AssertionError('Unresolved generated link in ' + name + ': ' + target)
    if not (public / 'sdk-catalogue.json').is_file():
        raise AssertionError('Missing catalogue download')


class Links(HTMLParser):
    def __init__(self):
        super().__init__()
        self.urls = []
        self.ids = set()

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if 'id' in attrs:
            self.ids.add(attrs['id'])
        for attr in ('href', 'src'):
            if attr in attrs:
                self.urls.append(attrs[attr])


def check_built(root):
    check_leaks(root)
    pages = list(root.rglob('*.html'))
    if not pages or not list((root / 'pagefind').rglob('*.pf_index')):
        raise AssertionError('Missing HTML or Pagefind search index')
    parsed = {}
    for path in pages:
        parser = Links()
        parser.feed(path.read_text())
        parsed[path] = parser
    checked = 0
    for path, parser in parsed.items():
        for target in parser.urls:
            url = urlsplit(target)
            if url.scheme or url.netloc or not url.path.startswith('/'):
                continue
            if not url.path.startswith(gen.BASE + '/'):
                raise AssertionError('Link lost site base: ' + target)
            dest = root / unquote(url.path[len(gen.BASE) + 1:])
            if dest.is_dir():
                dest /= 'index.html'
            if not dest.is_file():
                raise AssertionError('Missing built target: ' + target)
            if url.fragment and dest in parsed and unquote(url.fragment) not in parsed[dest].ids:
                raise AssertionError('Missing built anchor: ' + target)
            checked += 1
    if not (root / 'sdk-catalogue.json').is_file():
        raise AssertionError('Missing built catalogue')
    return len(pages), checked


class SiteTests(unittest.TestCase):
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
                for key in ('sdk_version', 'game_pack_version', 'source_sha'):
                    self.assertIn(self.catalogue[key], text)
            self.assertEqual(json.loads((output / 'public/sdk-catalogue.json').read_text()), self.catalogue)
            print('Verified records:', len(records), 'services:', len(services))

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
        for key, value in [('source_sha', 'main'), ('schema_version', 2)]:
            bad = deepcopy(self.catalogue)
            bad[key] = value
            with tempfile.TemporaryDirectory(prefix='site-test-') as work:
                with self.assertRaises(ValueError):
                    gen.generate(ROOT, bad, Path(work))
        with tempfile.TemporaryDirectory(prefix='site-test-') as work:
            root = Path(work)
            (root / 'site').mkdir()
            (root / 'site/public-inputs.json').write_text(json.dumps({'schema_version': 1, 'docs': ['docs/design/hidden.md']}))
            with self.assertRaises(ValueError):
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
            output = root / 'site'
            output.mkdir()
            for name in ('public-inputs.json', 'package.json', 'package-lock.json', 'astro.config.mjs'):
                shutil.copyfile(ROOT / 'site' / name, output / name)
            (output / 'src').mkdir()
            (output / 'scripts').mkdir()
            shutil.copyfile(ROOT / 'site/scripts/sanitize_search.py', output / 'scripts/sanitize_search.py')
            shutil.copyfile(ROOT / 'site/src/content.config.ts', output / 'src/content.config.ts')
            self.generate(root, output)
            shutil.copyfile(ROOT / 'site/public/favicon.svg', output / 'public/favicon.svg')
            check_leaks(output / 'src/content/docs', FORBIDDEN + SENTINELS)
            check_leaks(output / 'public', FORBIDDEN + SENTINELS)
            if shutil.which('node') and (ROOT / 'site/node_modules/.bin/astro').is_file():
                (output / 'node_modules').symlink_to(ROOT / 'site/node_modules', target_is_directory=True)
                result = subprocess.run(['npm', 'run', 'build'], cwd=output, capture_output=True, text=True,
                                        env={**os.environ, 'ASTRO_TELEMETRY_DISABLED': '1'})
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                pages, links = check_built(output / 'dist')
                check_leaks(output / 'dist', FORBIDDEN + SENTINELS)
                print('Sentinel build: clean HTML, static JSON and compressed Pagefind;', pages, 'pages;', links, 'links')
            elif shutil.which('node'):
                self.fail('Node available but dependencies missing; run npm ci in site first')
            else:
                print('Sentinel generated content clean; built sentinel check skipped: Node unavailable')

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

    def test_supplied_build(self):
        if BUILT is None:
            self.skipTest('Use --built site/dist for built-output verification')
        pages, links = check_built(BUILT)
        expected = len(self.catalogue['records']) + len({r['service'] for r in self.catalogue['records']}) + len(gen.inputs(ROOT)) + 1
        self.assertEqual(pages, expected + 1)  # Starlight's generated 404 page.
        print('Built site:', pages, 'HTML pages;', links, 'internal links resolved; Pagefind present')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--built', type=Path)
    args, rest = parser.parse_known_args()
    BUILT = args.built.resolve() if args.built else None
    unittest.main(argv=[__file__] + rest, verbosity=2)
