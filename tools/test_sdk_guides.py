#!/usr/bin/env python3
"""Tests for tools/sdk_guides.py: determinism, the allowlist, bounds, parsing and check mode.

Run: python3 tools/test_sdk_guides.py. Synthetic trees only, except the tests that read this
checkout's real allowlist; nothing is written outside a temporary directory.
"""
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
import sdk_guides as guides  # noqa: E402

SHA = '0123456789abcdef0123456789abcdef01234567'
DOC = """# Sample guide

Intro with a [link](other.md), `code_span`, **bold
across lines** and *emphasis*.

<!-- hidden
comment -->

## Build it

| Step | Command |
|---|---|
| Configure | `cmake -S . -B build` |

```sh
# not a heading
cmake --build build
```

## Build it

Second section with the same heading.

### Ports & `sco.net` (1.0) →

Deep text.
"""


def load_generator():
    spec = importlib.util.spec_from_file_location('site_gen', ROOT / 'site/scripts/gen.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class SyntheticTree(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='sdk_guides_test_')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        (self.root / 'sdk').mkdir()
        (self.root / 'sdk/VERSION').write_text('9.9.9\n', encoding='utf-8')
        self.write('docs/guide.md', DOC)
        self.allow(['docs/guide.md'])
        self.components([])

    def write(self, name, text, newline='\n'):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(text.replace('\n', newline).encode('utf-8'))

    def allow(self, docs):
        self.write('site/public-inputs.json', json.dumps({'schema_version': 1, 'docs': docs}))

    def components(self, entries):
        self.write('sdk/components.json', json.dumps({'schema_version': 1, 'components': entries}))

    def built(self):
        return guides.build(self.root, SHA)

    def refused(self, token):
        with self.assertRaises(guides.GuidesError) as caught:
            self.built()
        self.assertIn(token, str(caught.exception))


class BuildTests(SyntheticTree):
    def test_sections_anchors_and_plain_text(self):
        data = self.built()
        self.assertEqual(set(data), guides.TOP_KEYS)
        self.assertEqual((data['schema_version'], data['sdk_version'], data['source_sha'], data['source']),
                         (1, '9.9.9', SHA, 'site/public-inputs.json'))
        sections = data['sections']
        self.assertEqual([(s['level'], s['heading'], s['anchor']) for s in sections], [
            (1, 'Sample guide', 'sample-guide'), (2, 'Build it', 'build-it'), (2, 'Build it', 'build-it-1'),
            (3, 'Ports & sco.net (1.0) →', 'ports--sconet-10-')])
        self.assertEqual({s['title'] for s in sections}, {'Sample guide'})
        self.assertEqual({s['url'] for s in sections}, {'https://scuba-sc.github.io/sco-core/guides/guide/'})
        self.assertEqual(sections[0]['text'],
                         'Intro with a link, code_span, bold\nacross lines and emphasis.')
        self.assertEqual(sections[1]['text'], 'Step | Command\nConfigure | cmake -S . -B build\n\n'
                                              '# not a heading\ncmake --build build')
        self.assertNotIn('hidden', json.dumps(data))
        self.assertEqual(data['docs'], [{'doc': 'docs/guide.md', 'title': 'Sample guide', 'sections': 4,
                                         'url': 'https://scuba-sc.github.io/sco-core/guides/guide/'}])
        guides.validate(data)

    def test_bytes_are_deterministic_and_line_ending_neutral(self):
        first = guides.encoded(self.built())
        self.assertEqual(first, guides.encoded(self.built()))
        self.assertTrue(first.endswith(b'}\n') and b'\r' not in first)
        self.assertNotIn(os.fsencode(self.root), first)
        self.assertNotIn(str(self.root).replace('\\', '\\\\').encode(), first)
        self.write('docs/guide.md', DOC, newline='\r\n')
        self.assertEqual(guides.encoded(self.built()), first)
        self.write('docs/guide.md', '﻿' + DOC)
        self.assertEqual(guides.encoded(self.built()), first)

    def test_text_is_bounded(self):
        self.write('docs/guide.md', '# Long\n\n' + ('word ' * 3000) + '\n\n## After\n\nshort\n')
        long, after = self.built()['sections']
        self.assertTrue(long['truncated'])
        self.assertLessEqual(len(long['text']), guides.TEXT_LIMIT)
        self.assertGreater(len(long['text']), guides.TEXT_LIMIT - 200)
        self.assertEqual((after['text'], after['truncated']), ('short', False))

    def test_preamble_joins_the_first_section(self):
        self.write('docs/guide.md', 'Lead paragraph.\n\n# Title\n\nBody.\n')
        only, = self.built()['sections']
        self.assertEqual((only['heading'], only['text']), ('Title', 'Lead paragraph.\n\nBody.'))

    def test_comment_opener_in_a_code_span_is_text(self):
        self.write('docs/guide.md', '# A\n\nUse `<!--` to start.\n\n## B\n\nText <!-- gone --> kept.\n\n## C\n')
        self.assertEqual([(s['heading'], s['text']) for s in self.built()['sections']],
                         [('A', 'Use <!-- to start.'), ('B', 'Text  kept.'), ('C', '')])

    def test_unterminated_comment_is_refused(self):
        self.write('docs/guide.md', '# A\n\n<!-- never closed\n\n## B\n')
        self.refused('unterminated HTML comment: docs/guide.md')

    def test_source_sha_must_be_full_lowercase_hex(self):
        for value in ('', SHA[:12], SHA.upper(), None):
            with self.subTest(value=value), self.assertRaises(guides.GuidesError):
                guides.build(self.root, value)


class AllowlistTests(SyntheticTree):
    def test_blocked_and_malformed_entries_are_refused(self):
        for name in ('docs/legal/notes.md', 'docs/design/plan.md', 'docs/Legal/notes.md', 'docs/DESIGN/plan.md',
                     'docs/guide.txt', 'README.md',
                     'sdk/README.md', 'docs/../README.md', '/docs/guide.md', 'docs\\guide.md', 'docs/*.md'):
            with self.subTest(name=name):
                self.allow(['docs/guide.md', name])
                self.refused('not a public guide input')

    def test_duplicate_entries_and_missing_files_are_refused(self):
        self.allow(['docs/guide.md', 'docs/guide.md'])
        self.refused('unique explicit paths')
        self.allow(['docs/missing.md'])
        self.refused('guide input missing')
        self.write('site/public-inputs.json', '{"schema_version": 2, "docs": []}')
        self.refused('invalid public-input allowlist')

    def test_sdk_docs_are_admitted_with_a_repository_url(self):
        self.write('sdk/docs/extra.md', '# Extra\n\nText.\n')
        self.allow(['sdk/docs/extra.md', 'docs/guide.md'])
        data = self.built()
        self.assertEqual([d['doc'] for d in data['docs']], ['docs/guide.md', 'sdk/docs/extra.md'])
        self.assertEqual(data['docs'][1]['url'], 'sdk/docs/extra.md')

    def test_shipped_sdk_docs_come_from_the_component_manifest(self):
        self.write('sdk/docs/plugin-ini.md', '# plugin.ini\n\nText.\n')
        self.write('sdk/docs/vendor.md', '# Vendor\n')
        self.write('sdk/docs/unshipped.md', '# Unshipped\n')
        self.components([
            {'source': 'sdk/docs/plugin-ini.md', 'profiles': ['core'], 'origin': 'project-authored'},
            {'source': 'sdk/docs/vendor.md', 'profiles': ['core'], 'origin': 'third-party'},
            {'source': 'sdk/docs/unshipped.md', 'profiles': [], 'origin': 'project-authored'},
            {'source': 'sdk/include/x.h', 'profiles': ['core'], 'origin': 'project-authored'}])
        data = self.built()
        self.assertEqual([(d['doc'], d['url']) for d in data['docs']], [
            ('docs/guide.md', 'https://scuba-sc.github.io/sco-core/guides/guide/'),
            ('sdk/docs/plugin-ini.md', 'sdk/docs/plugin-ini.md')])
        self.components([{'source': 'sdk/docs/../x.md', 'profiles': ['core'], 'origin': 'project-authored'}])
        self.refused('not a public guide input: sdk/docs/../x.md')
        (self.root / 'sdk/components.json').unlink()
        self.refused('sdk/components.json missing')

    def test_case_mismatched_name_is_refused(self):
        self.allow(['docs/Guide.md'])
        self.refused('not exactly beneath the root' if (self.root / 'docs/Guide.md').exists() else 'guide input missing')

    def test_a_doc_without_heading_is_refused(self):
        self.write('docs/guide.md', 'No heading here.\n')
        self.refused('has no heading')

    @unittest.skipIf(not hasattr(os, 'symlink'), 'no symlink support')
    def test_symlinked_input_is_refused(self):
        try:
            os.symlink(self.root / 'docs/guide.md', self.root / 'docs/link.md')
        except OSError:
            self.skipTest('symlinks need privileges on this host')
        self.allow(['docs/link.md'])
        self.refused('crosses a symlink')


class CheckTests(SyntheticTree):
    def run_cli(self, *argv):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = guides.main(list(argv))
        return code, out.getvalue() + err.getvalue()

    def test_build_then_check_round_trip_and_stale_file(self):
        out = self.root / 'out/sdk-guides.json'
        code, text = self.run_cli('build', '--root', str(self.root), '--out', str(out), '--source-sha', SHA)
        self.assertEqual(code, 0, text)
        self.assertEqual(out.read_bytes(), guides.encoded(self.built()))
        code, text = self.run_cli('check', '--root', str(self.root), '--file', str(out))
        self.assertEqual(code, 0, text)
        self.assertIn('matches the tree', text)
        self.write('docs/guide.md', DOC.replace('Deep text.', 'Changed text.'))
        code, text = self.run_cli('check', '--root', str(self.root), '--file', str(out))
        self.assertEqual(code, 1)
        self.assertIn('differs from the tree: docs/guide.md', text)

    def test_check_refuses_malformed_files(self):
        data = self.built()
        cases = {'over text_limit': lambda d: d['sections'][0].update(text='x' * (guides.TEXT_LIMIT + 1)),
                 'invalid section keys': lambda d: d['sections'][0].pop('anchor'),
                 'duplicate or undeclared section': lambda d: d['sections'].append(dict(d['sections'][0])),
                 'schema_version': lambda d: d.update(schema_version=2),
                 'section counts': lambda d: d['docs'][0].update(sections=9)}
        for token, change in cases.items():
            with self.subTest(token=token):
                broken = json.loads(json.dumps(data))
                change(broken)
                path = self.root / 'broken.json'
                path.write_bytes(guides.encoded(broken))
                code, text = self.run_cli('check', '--root', str(self.root), '--file', str(path))
                self.assertEqual(code, 1)
                self.assertIn(token, text)

    def test_check_without_file_validates_the_tree(self):
        code, text = self.run_cli('check', '--root', str(self.root))
        self.assertEqual(code, 0, text)
        self.assertIn('4 sections from 1 docs', text)
        self.allow(['docs/legal/x.md'])
        self.assertEqual(self.run_cli('check', '--root', str(self.root))[0], 1)


class RealTreeTests(unittest.TestCase):
    def test_real_allowlist_builds_within_bounds(self):
        data = guides.build(ROOT, SHA)
        guides.validate(data)
        listed = json.loads((ROOT / 'site/public-inputs.json').read_text(encoding='utf-8'))['docs']
        self.assertEqual([d['doc'] for d in data['docs']], sorted(set(listed) | guides.sdk_guides(ROOT)))
        self.assertLessEqual({'sdk/docs/plugin-ini.md', 'sdk/docs/plugin-rules.md', 'sdk/docs/lua.md',
                              'sdk/docs/widgets.md', 'sdk/docs/data-packs.md'}, guides.sdk_guides(ROOT))
        self.assertFalse([s for s in data['sections'] if s['doc'].startswith(guides.BLOCKED)])
        self.assertTrue(all(len(s['text']) <= guides.TEXT_LIMIT for s in data['sections']))
        self.assertEqual(guides.encoded(data), guides.encoded(guides.build(ROOT, SHA)))

    def test_urls_are_the_site_generator_routes(self):
        generator = load_generator()
        for doc in guides.allowed_inputs(ROOT):
            with self.subTest(doc=doc):
                route = generator.route(generator.route_key(generator.guide_id(doc)))
                self.assertEqual(guides.guide_url(doc), guides.SITE + route)
        self.assertEqual(generator.BASE, guides.BASE)


if __name__ == '__main__':
    unittest.main()
