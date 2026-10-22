#!/usr/bin/env python3
"""Searchable SDK guide sections from the public docs allowlist; Python standard library only.

    tools/sdk_guides.py build --out FILE --source-sha SHA   # write sdk-guides.json
    tools/sdk_guides.py check [--file FILE]                 # validate the tree, or FILE against it

Inputs are the site/public-inputs.json entries under docs/ or sdk/docs/ that end in .md, never
docs/legal/ or docs/design/ (compared case-insensitively), plus the SDK's own plugin-authoring
guides: every sdk/docs/*.md that sdk/components.json lists as project-authored and shipped in a
profile. Each Markdown heading starts one record: doc, title, heading, anchor (GitHub-style slug),
level, plain text bounded to TEXT_LIMIT characters, truncated, and url (the docs-site page when
the site generator renders the doc, else the repository path). Output bytes depend only on the
input bytes and the supplied source SHA: sorted keys, ASCII escapes, LF newlines, no timestamps
and no absolute paths.

The allowlist, sdk/components.json and every guide are read only beneath the root: no symlink or
junction component, and the real path must be exactly root/<name>, which also refuses a name whose
case differs from the file's on a case-insensitive filesystem.

The module is also executed in memory by tools/sco-core-mcp/server.py in source mode, so it
imports nothing outside the standard library and does not depend on __file__ at import.
"""
import argparse
import json
import os
from pathlib import Path, PurePosixPath
import re
import sys
import unicodedata

ROOT = Path(__file__).resolve().parents[1] if '__file__' in globals() else None
SCHEMA_VERSION = 1
ALLOWLIST = 'site/public-inputs.json'
COMPONENTS = 'sdk/components.json'
SDK_DOCS = 'sdk/docs/'
TEXT_LIMIT = 4000
MAX_DOC_BYTES = 2 * 1024 * 1024
MAX_COMPONENTS_BYTES = 8 * 1024 * 1024
# The docs site: site/scripts/gen.py serves docs/<path>.md at BASE/guides/<slug>/.
SITE = 'https://scuba-sc.github.io'
BASE = '/sco-core'
BLOCKED = ('docs/legal/', 'docs/design/')  # lowercase; names are casefolded before the comparison
# Private-use placeholders that stand in for code spans while the prose rules run.
OPEN, CLOSE = '\ue000', '\ue001'
SECTION_KEYS = {'doc', 'title', 'heading', 'anchor', 'level', 'text', 'truncated', 'url'}
TOP_KEYS = {'schema_version', 'sdk_version', 'source_sha', 'source', 'text_limit', 'docs', 'sections'}
DOC_KEYS = {'doc', 'title', 'url', 'sections'}

FENCE = re.compile(r'^ {0,3}(`{3,}|~{3,})')
HEADING = re.compile(r'^ {0,3}(#{1,6})(?:[ \t]+(.*?))?[ \t]*$')
CLOSING_HASHES = re.compile(r'(?:^|[ \t]+)#+[ \t]*$')
TABLE_RULE = re.compile(r'^\s*\|?\s*:?-{2,}:?\s*(\|\s*:?-{2,}:?\s*)*\|?\s*$')
CODE_SPAN = re.compile(r'(`+)(.+?)\1', re.S)
IMAGE = re.compile(r'!\[([^\]]*)\]\([^)\s]*(?:\s+"[^"]*")?\)')
LINK = re.compile(r'\[([^\]]*)\](?:\([^)\s]*(?:\s+"[^"]*")?\)|\[[^\]]*\])')
AUTOLINK = re.compile(r'<((?:https?|mailto):[^>\s]+)>')
# Only HTML tags Markdown docs actually use are removed, so prose such as <version> survives.
HTML_TAG = re.compile(r'</?(?:a|b|br|code|details|div|em|i|img|kbd|li|ol|p|span|strong|sub|summary|sup|table|'
                      r'tbody|td|th|thead|tr|ul)(?:\s[^<>]*)?/?>', re.I)
STRONG = re.compile(r'(\*\*|__)(?=\S)(.+?)(?<=\S)\1', re.S)
EMPHASIS = re.compile(r'(?<![\w*])\*(?=\S)([^*]+?)(?<=\S)\*(?![\w*])')
STRIKE = re.compile(r'~~(?=\S)(.+?)(?<=\S)~~', re.S)
ESCAPE = re.compile(r'\\([\\`*_{}\[\]()#+\-.!|<>~])')


class GuidesError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise GuidesError(message)


def encoded(value):
    """The one byte encoding of sdk-guides.json (package.py's json_bytes)."""
    return (json.dumps(value, sort_keys=True, indent=2, ensure_ascii=True) + '\n').encode('ascii')


def guide_url(doc):
    """site/scripts/gen.py's served route for an allowlisted docs/ page; else the repository path."""
    path = PurePosixPath(doc)
    if path.parts[0] != 'docs':
        return doc
    slugs = [re.sub(r'[^a-z0-9_-]', '', part.lower()) for part in path.relative_to('docs').with_suffix('').parts]
    require(all(slugs), 'input name has no routable slug: ' + doc)
    identifier = 'guides/' + '/'.join(slugs)
    if identifier.endswith('/index'):
        identifier = identifier[:-len('/index')]
    return SITE + BASE + '/' + identifier + '/'


def is_junction(path):
    check = getattr(os.path, 'isjunction', None)  # Python 3.12+; the real-path check covers older ones
    return bool(check and check(path))


def contained(root, name, limit=MAX_DOC_BYTES, what='guide input'):
    """The bytes of root/name, refusing a symlink or junction component, a real path that is not
    exactly root/name (case included) and a file over limit."""
    current = root
    for part in PurePosixPath(name).parts:
        current = current / part
        require(not current.is_symlink() and not is_junction(current), what + ' crosses a symlink or junction: ' + name)
    require(current.is_file(), what + ' missing: ' + name)
    base, real = Path(root).resolve(), current.resolve()
    require(real.is_relative_to(base) and real.relative_to(base).as_posix() == name,
            what + ' is not exactly beneath the root: ' + name)
    with open(real, 'rb') as stream:
        raw = stream.read(limit + 1)
    require(len(raw) <= limit, what + ' over ' + str(limit // (1024 * 1024)) + ' MiB: ' + name)
    return raw


def json_input(root, name, limit):
    raw = contained(root, name, limit, name)
    try:
        return json.loads(raw.decode('utf-8'))
    except ValueError as error:
        raise GuidesError(name + ' unreadable: ' + type(error).__name__) from None


def public_name(name):
    path = PurePosixPath(name)
    return (str(path) == name and not path.is_absolute() and '..' not in path.parts
            and not any(c in name for c in '\\%*?:') and path.suffix == '.md'
            and (name.startswith('docs/') or name.startswith(SDK_DOCS))
            and not name.casefold().startswith(BLOCKED))


def sdk_guides(root):
    """sdk/docs/*.md guides that sdk/components.json lists as project-authored and shipped."""
    data = json_input(root, COMPONENTS, MAX_COMPONENTS_BYTES)
    require(isinstance(data, dict) and isinstance(data.get('components'), list), 'invalid ' + COMPONENTS)
    names = set()
    for component in data['components']:
        source = component.get('source') if isinstance(component, dict) else None
        if not (isinstance(source, str) and source.startswith(SDK_DOCS) and source.endswith('.md')):
            continue
        if component.get('origin') == 'project-authored' and component.get('profiles'):
            require(public_name(source), 'not a public guide input: ' + source)
            names.add(source)
    return names


def inputs(root):
    """Every guide input: the allowlist's entries and the shipped sdk/docs guides, sorted."""
    return sorted(set(allowed_inputs(root)) | sdk_guides(root))


def allowed_inputs(root):
    """The allowlisted guide inputs, refusing any entry outside docs/ or sdk/docs/ Markdown."""
    data = json_input(root, ALLOWLIST, MAX_DOC_BYTES)
    require(isinstance(data, dict) and data.get('schema_version') == 1 and isinstance(data.get('docs'), list),
            'invalid public-input allowlist')
    docs = data['docs']
    require(docs and all(isinstance(name, str) for name in docs) and len(docs) == len(set(docs)),
            'allowlist must contain unique explicit paths')
    for name in docs:
        require(public_name(name), 'not a public guide input: ' + name)
    return sorted(docs)


def read_doc(root, name):
    raw = contained(root, name)
    try:
        text = raw.decode('utf-8-sig')
    except UnicodeDecodeError:
        raise GuidesError('guide input is not UTF-8: ' + name) from None
    return text.replace('\r\n', '\n').replace('\r', '\n')


def inline_text(line):
    """Markdown inline syntax to plain text; code-span contents are kept verbatim.

    Code spans become private-use placeholders while the prose rules run, so emphasis
    or a link around a code span still pairs up."""
    spans = []

    def hold(match):
        spans.append(match[2].strip() or match[2])
        return OPEN + str(len(spans) - 1) + CLOSE
    text = prose(CODE_SPAN.sub(hold, line.replace(OPEN, '').replace(CLOSE, '')))
    return re.sub(OPEN + r'(\d+)' + CLOSE, lambda m: spans[int(m[1])], text)


def prose(text):
    text = IMAGE.sub(lambda m: m[1], text)
    text = LINK.sub(lambda m: m[1], text)
    text = AUTOLINK.sub(lambda m: m[1], text)
    text = HTML_TAG.sub(lambda m: ' ' if m[0].lower().startswith('<br') else '', text)
    text = STRONG.sub(lambda m: m[2], text)
    text = STRIKE.sub(lambda m: m[1], text)
    text = EMPHASIS.sub(lambda m: m[1], text)
    return ESCAPE.sub(lambda m: m[1], text)


def body_line(line):
    """(kind, text): a dropped table rule is None; table rows are converted here, prose later by paragraph."""
    if TABLE_RULE.match(line) and '-' in line and '|' in line:
        return None
    stripped = line.strip()
    if stripped.startswith('|') and stripped.endswith('|') and len(stripped) > 1:
        cells = re.split(r'(?<!\\)\|', stripped[1:-1])
        return 'table', ' | '.join(inline_text(cell.strip()) for cell in cells)
    if stripped.startswith('>'):
        line = re.sub(r'^\s*(>\s?)+', '', line)
    return 'md', line.rstrip()


def render(items):
    """Plain lines: code verbatim, table rows as converted, prose converted one paragraph at a
    time so emphasis, links and code spans that wrap across lines still pair up."""
    lines, paragraph = [], []

    def flush():
        if paragraph:
            lines.extend(inline_text('\n'.join(paragraph)).split('\n'))
            paragraph.clear()
    for kind, text in items:
        if kind == 'md' and text.strip():
            paragraph.append(text)
            continue
        flush()
        lines.append(text)
    flush()
    return lines


def slug(text):
    """github-slugger: lowercase, drop punctuation and symbols, spaces become hyphens."""
    kept = ''.join(c for c in text.lower()
                   if c in '-_ ' or unicodedata.category(c)[0] in 'LNM')
    return kept.replace(' ', '-')


def unique_slug(text, occurrences):
    base = result = slug(text) or 'section'
    while result in occurrences:
        occurrences[base] += 1
        result = base + '-' + str(occurrences[base])
    occurrences[result] = 0
    return result


def bounded(text):
    if len(text) <= TEXT_LIMIT:
        return text, False
    cut = text[:TEXT_LIMIT]
    space = max(cut.rfind(' '), cut.rfind('\n'))
    if space >= TEXT_LIMIT - 200:
        cut = cut[:space]
    return cut.rstrip(), True


def tidy(lines):
    text = re.sub(r'\n{3,}', '\n\n', '\n'.join(line.rstrip() for line in lines))
    return text.strip('\n').strip()


def strip_comments(line):
    """(line without HTML comments, whether a comment is still open); `<!--` inside a code span is text."""
    masked = CODE_SPAN.sub(lambda m: OPEN * len(m[0]), line)
    kept, position = [], 0
    while True:
        start = masked.find('<!--', position)
        if start < 0:
            kept.append(line[position:])
            return ''.join(kept), False
        kept.append(line[position:start])
        end = masked.find('-->', start + 4)
        if end < 0:
            return ''.join(kept), True
        position = end + 3


def doc_sections(name, text):
    """(title, [(level, heading, body lines)]) in document order; fenced code is body, never headings."""
    parts, current, fence, preamble, comment = [], None, None, [], False
    for line in text.split('\n'):
        if fence:
            if re.match(r'^ {0,3}' + re.escape(fence[0]) + '{' + str(len(fence)) + r',}\s*$', line):
                fence = None
            else:
                (current[2] if current else preamble).append(('code', line))
            continue
        if comment:
            if '-->' not in line:
                continue
            line, comment = line.split('-->', 1)[1], False
        line, comment = strip_comments(line)
        opened = FENCE.match(line)
        if opened:
            fence = opened[1]
            continue
        heading = HEADING.match(line)
        if heading:
            title = inline_text(CLOSING_HASHES.sub('', heading[2] or '')).strip()
            if title:
                current = [len(heading[1]), title, []]
                parts.append(current)
                continue
        converted = body_line(line)
        if converted is not None:
            (current[2] if current else preamble).append(converted)
    require(not comment, 'unterminated HTML comment: ' + name)
    require(parts, 'guide input has no heading: ' + name)
    if tidy(render(preamble)):
        parts[0][2][:0] = preamble + [('md', '')]
    title = next((heading for level, heading, _ in parts if level == 1), parts[0][1])
    return title, parts


def sections(root):
    """(docs, sections) built from the tree beneath root."""
    docs, records = [], []
    for name in inputs(root):
        title, parts = doc_sections(name, read_doc(root, name))
        url = guide_url(name)
        occurrences = {}
        for level, heading, lines in parts:
            text, truncated = bounded(tidy(render(lines)))
            records.append({'doc': name, 'title': title, 'heading': heading,
                            'anchor': unique_slug(heading, occurrences), 'level': level,
                            'text': text, 'truncated': truncated, 'url': url})
        docs.append({'doc': name, 'title': title, 'url': url, 'sections': len(parts)})
    return docs, records


def build(root, source_sha):
    require(isinstance(source_sha, str) and re.fullmatch(r'[0-9a-f]{40}', source_sha) is not None,
            '--source-sha must be an explicit full lowercase 40-hex commit')
    docs, records = sections(root)
    version = (root / 'sdk/VERSION').read_text(encoding='utf-8').strip()
    return {'schema_version': SCHEMA_VERSION, 'sdk_version': version, 'source_sha': source_sha,
            'source': ALLOWLIST, 'text_limit': TEXT_LIMIT, 'docs': docs, 'sections': records}


def validate(data):
    """Structural check of a parsed sdk-guides.json document (also used by the MCP server)."""
    require(isinstance(data, dict) and set(data) == TOP_KEYS, 'invalid sdk-guides.json keys')
    require(type(data['schema_version']) is int and data['schema_version'] == SCHEMA_VERSION,
            'unsupported sdk-guides.json schema_version; expected 1')
    require(isinstance(data['source_sha'], str) and re.fullmatch(r'[0-9a-f]{40}', data['source_sha']),
            'invalid source_sha')
    require(isinstance(data['sdk_version'], str) and data['sdk_version'] and data['source'] == ALLOWLIST,
            'invalid sdk_version or source')
    limit = data['text_limit']
    require(type(limit) is int and 1 <= limit <= 65536, 'invalid text_limit')
    require(isinstance(data['docs'], list) and isinstance(data['sections'], list)
            and len(data['sections']) <= 20000, 'invalid docs or sections')
    counts = {}
    for doc in data['docs']:
        require(isinstance(doc, dict) and set(doc) == DOC_KEYS and type(doc['sections']) is int
                and all(isinstance(doc[k], str) and doc[k] for k in ('doc', 'title', 'url'))
                and doc['doc'] not in counts, 'invalid docs entry')
        counts[doc['doc']] = doc['sections']
    seen, found = set(), {}
    for section in data['sections']:
        require(isinstance(section, dict) and set(section) == SECTION_KEYS, 'invalid section keys')
        require(all(isinstance(section[k], str) for k in SECTION_KEYS - {'level', 'truncated'})
                and all(section[k] and len(section[k]) <= 512 for k in ('doc', 'title', 'heading', 'anchor', 'url')),
                'invalid section text')
        require(type(section['level']) is int and 1 <= section['level'] <= 6, 'invalid section level')
        require(type(section['truncated']) is bool and len(section['text']) <= limit, 'section text over text_limit')
        key = (section['doc'], section['anchor'])
        require(section['doc'] in counts and key not in seen, 'duplicate or undeclared section: ' + '#'.join(key))
        seen.add(key)
        found[section['doc']] = found.get(section['doc'], 0) + 1
    require(found == {name: n for name, n in counts.items() if n}, 'docs section counts disagree with sections')
    return data


def check(root, path=None):
    """Validate the tree; with path, also require that file to be byte-identical to a rebuild."""
    if path is None:
        docs, records = sections(root)
        validate({'schema_version': SCHEMA_VERSION, 'sdk_version': 'check', 'source_sha': '0' * 40,
                  'source': ALLOWLIST, 'text_limit': TEXT_LIMIT, 'docs': docs, 'sections': records})
        return docs, records
    try:
        raw = Path(path).read_bytes()
        data = json.loads(raw.decode('utf-8'))
    except (OSError, ValueError) as error:
        raise GuidesError('sdk-guides.json unreadable: ' + type(error).__name__) from None
    validate(data)
    expected = build(root, data['source_sha'])
    if raw != encoded(expected):
        stale = sorted({s['doc'] for s in data['sections']} ^ {s['doc'] for s in expected['sections']}
                       | {s['doc'] for s, t in zip(data['sections'], expected['sections']) if s != t})
        raise GuidesError('sdk-guides.json differs from the tree' + (': ' + ', '.join(stale[:5]) if stale else ''))
    return expected['docs'], expected['sections']


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    subs = parser.add_subparsers(dest='command', required=True)
    for name in ('build', 'check'):
        sub = subs.add_parser(name)
        sub.add_argument('--root', type=Path, default=ROOT)
        if name == 'build':
            sub.add_argument('--out', type=Path, required=True)
            sub.add_argument('--source-sha', required=True)
        else:
            sub.add_argument('--file', type=Path, help='an existing sdk-guides.json to compare with the tree')
    args = parser.parse_args(argv)
    try:
        if args.command == 'build':
            data = build(args.root, args.source_sha)
            args.out.parent.mkdir(parents=True, exist_ok=True)
            args.out.write_bytes(encoded(data))
            print(f"sdk_guides: built {args.out}: {len(data['sections'])} sections from {len(data['docs'])} docs")
        else:
            docs, records = check(args.root, args.file)
            truncated = sum(record['truncated'] for record in records)
            print(f"sdk_guides: {len(records)} sections from {len(docs)} docs; {truncated} truncated to {TEXT_LIMIT}"
                  + (f"; {args.file} matches the tree" if args.file else ''))
        return 0
    except (GuidesError, OSError) as error:
        print('sdk_guides: FAIL ' + str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
