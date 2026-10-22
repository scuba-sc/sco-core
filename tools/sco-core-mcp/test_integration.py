#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the documented launchers; no client settings are changed.

docs/agent-integration.md is the one place that holds client configuration. This test
reads every `mcpServers` / `servers` JSON block in it and checks that each is a valid
launcher of one of two layouts:

* kit: SDK/tools/sco-core-mcp/server.py, --catalogue SDK/sdk-catalogue.json, --repo-root SDK
  (SDK is the folder that directly holds sdk-catalogue.json; the combined kit has no dist/
  and no sdk/ folder);
* checkout: the maintainer layout, with dist/sdk-catalogue.json under the repository root.

A block either offers the SDK developer profile (--extended-tools, twelve tools) or the
minimal profile (no flag, six tools), and a `tools` allowlist must name exactly that
profile's tools. The Claude Code and Codex commands (which quote `$SDK`, set by the guide's
"Find the SDK folder" block) and the Codex TOML table must repeat the same arguments. Each
block is then started for real: a kit block against a kit-shaped folder assembled from this
repository, the checkout block against the repository itself. The failure modes and the
verification signals the guide describes (the server path, the catalogue path, --repo-root
one level too high or left out) are reproduced against the same folder.
The interpreter is replaced by this interpreter, because `python3` and `py` are not present
on every host; only the arguments are the documented ones.

The catalogue is dist/sdk-catalogue.json, or the file named by SCO_CORE_MCP_TEST_CATALOGUE
(the variable tools/sco-core-mcp/test_server.py reads).
"""
from copy import deepcopy
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest

try:
    import tomllib
except ImportError:  # Python 3.9 and 3.10: codex_table() below reads the table instead
    tomllib = None

ROOT = Path(__file__).resolve().parents[2]
GUIDE = ROOT / 'docs/agent-integration.md'
README = ROOT / 'tools/sco-core-mcp/README.md'
CATALOGUE = Path(os.environ.get('SCO_CORE_MCP_TEST_CATALOGUE') or ROOT / 'dist/sdk-catalogue.json')
MINIMAL_TOOLS = ['search_api', 'get_symbol', 'list_services', 'get_example',
                 'get_capability_status', 'check_compatibility']
EXTENDED_TOOLS = MINIMAL_TOOLS + ['list_engine_natives', 'get_engine_native',
                                  'validate_manifest_and_widgets', 'generate_plugin_scaffold',
                                  'search_guides', 'get_guide_section']
SCRIPT = '/tools/sco-core-mcp/server.py'
SERVER_NAME = 'sco-core-sdk'
KIT_FOLDER = 'sco-sdk-2.4.0'
# Wording the setup rewrite removed because it contradicted the rest of the setup docs.
RETIRED_WORDING = ('enable only the six', 'enable only those six', 'four additional',
                   'combined `legacy` developer kit', '/absolute/sdk', 'requires **only the six',
                   'in legacy, game-sc', 'README.md:187-211')
SETUP_DOCS = ('docs/agent-integration.md', 'tools/sco-core-mcp/README.md', 'docs/sdk-mcp-tutorial.md',
              'docs/sdk-developer-mvp.md', 'docs/design/runtime-mcp.md', 'docs/releases/sdk-v2.4.0.md')
KIT_PAGE = ROOT / 'docs/sdk-developer-mvp.md'


def read(path):
    return Path(path).read_text(encoding='utf-8')


def fenced(text, language):
    return re.findall(r'```' + language + r'\n(.*?)\n```', text, re.S)


def launchers(text):
    """[(envelope, name, entry)] for every `mcpServers` or `servers` JSON block."""
    found = []
    for block in fenced(text, 'json'):
        data = json.loads(block)
        for envelope in ('mcpServers', 'servers'):
            if envelope in data:
                (name, entry), = data[envelope].items()
                found.append((envelope, name, entry))
    return found


def codex_table(block):
    """(name, {command, args}) of a Codex `[mcp_servers.<name>]` block.

    It reads the one layout the guide uses (a `command` string and a multi-line `args` array of
    strings), so that Python 3.9 and 3.10, which have no tomllib, still compare the table. With
    tomllib present, a test checks that this reader and a real TOML parser agree.
    """
    match = re.fullmatch(r'\[mcp_servers\.([A-Za-z0-9_-]+)\]\ncommand = ("[^"\n]*")\n'
                         r'args = \[\n((?:  "[^"\n]*",\n)+)\]\n', block.strip() + '\n')
    if not match:
        raise ValueError('Expected a [mcp_servers.<name>] table: a command, then args one element per line')
    args = [json.loads(line.strip().rstrip(',')) for line in match.group(3).splitlines()]
    return match.group(1), {'command': json.loads(match.group(2)), 'args': args}


def arguments(entry):
    """The server arguments after the interpreter, which is python3 or `py -3`."""
    args = list(entry['args'])
    if entry['command'] == 'py' and args[:1] == ['-3']:
        return args[1:]
    if entry['command'] != 'python3':
        raise ValueError('Expected command python3, or py with -3 as the first argument')
    return args


def validate(envelope, name, entry):
    """Check one launcher block; return (layout, base path, developer profile)."""
    if name != SERVER_NAME:
        raise ValueError('Expected the server name ' + SERVER_NAME)
    if not {'command', 'args'} <= set(entry) <= {'type', 'command', 'args', 'tools'}:
        raise ValueError('Expected command and args only, with optional type and tools and no secrets')
    if entry.get('type', 'stdio') != 'stdio' or (envelope == 'servers' and 'type' not in entry):
        raise ValueError('Expected type stdio (required in a `servers` envelope)')
    args = arguments(entry)
    if any(not isinstance(arg, str) or not arg or re.search(r'\s', arg) for arg in args):
        raise ValueError('Every flag and every value must be its own element')
    script = args[0]
    if not script.endswith(SCRIPT) or not re.match(r'(/|[A-Za-z]:/)', script):
        raise ValueError('Expected an absolute path to ' + SCRIPT)
    base = script[:-len(SCRIPT)]
    if args[1:5] == ['--catalogue', base + '/sdk-catalogue.json', '--repo-root', base]:
        layout = 'kit'
    elif args[1:5] == ['--catalogue', base + '/dist/sdk-catalogue.json', '--repo-root', base]:
        layout = 'checkout'
    else:
        raise ValueError('Unexpected --catalogue or --repo-root')
    developer = args[5:] == ['--extended-tools']
    if args[5:] and not developer:
        raise ValueError('Unexpected launcher arguments')
    if 'tools' in entry and entry['tools'] != (EXTENDED_TOOLS if developer else MINIMAL_TOOLS):
        raise ValueError('The tools allowlist must name exactly the six or twelve tools of the profile')
    return layout, base, developer


def messages(*calls):
    sequence = [
        {'jsonrpc': '2.0', 'id': 1, 'method': 'initialize', 'params': {
            'protocolVersion': '2025-11-25', 'capabilities': {},
            'clientInfo': {'name': 'documented-launcher-test', 'version': '1'}}},
        {'jsonrpc': '2.0', 'method': 'notifications/initialized'},
        {'jsonrpc': '2.0', 'id': 2, 'method': 'tools/list'},
    ]
    for index, (name, call_arguments) in enumerate(calls, 3):
        sequence.append({'jsonrpc': '2.0', 'id': index, 'method': 'tools/call',
                         'params': {'name': name, 'arguments': call_arguments}})
    return ''.join(json.dumps(m) + '\n' for m in sequence)


class DocumentedBlockTests(unittest.TestCase):
    """What the page says, without starting a server."""

    @classmethod
    def setUpClass(cls):
        cls.guide = read(GUIDE)
        cls.blocks = launchers(cls.guide)

    def test_guide_documents_each_layout_and_every_block_is_valid(self):
        self.assertTrue(self.blocks)
        seen = []
        for block in self.blocks:
            with self.subTest(block=block[2]['command'] + ' ' + arguments(block[2])[0]):
                seen.append(validate(*block))
        layouts = [layout for layout, _, _ in seen]
        self.assertGreaterEqual(layouts.count('kit'), 4, 'generic POSIX and Windows, VS Code and Copilot CLI')
        self.assertEqual(layouts.count('checkout'), 1, 'the one maintainer block')
        self.assertEqual([developer for layout, _, developer in seen if layout == 'checkout'], [False])
        self.assertTrue(any(envelope == 'servers' for envelope, _, _ in self.blocks), 'VS Code envelope')
        windows = [entry for _, _, entry in self.blocks if entry['command'] == 'py']
        self.assertTrue(windows, 'a Windows launcher')
        for entry in windows:
            self.assertEqual(entry['args'][:1], ['-3'])
            self.assertRegex(entry['args'][1], r'^[A-Za-z]:/')

    def generic_entry(self):
        """The first block: the generic `mcpServers` entry for macOS and Linux."""
        envelope, name, entry = self.blocks[0]
        self.assertEqual((envelope, entry['command'], validate(envelope, name, entry)[0]),
                         ('mcpServers', 'python3', 'kit'))
        self.assertFalse({'type', 'tools'} & set(entry))
        return entry

    def test_shell_commands_and_toml_repeat_the_json_arguments(self):
        posix = self.generic_entry()
        windows = next(e for _, _, e in self.blocks if e['command'] == 'py')
        commands = []
        for language in ('sh', 'powershell'):
            for block in fenced(self.guide, language):
                for line in block.splitlines():
                    if re.match(r'(claude|codex) mcp add ', line):
                        # Quoted, so a path with a space stays one argument.
                        self.assertIn('"$SDK/tools/sco-core-mcp/server.py" --catalogue '
                                      '"$SDK/sdk-catalogue.json" --repo-root "$SDK"', line)
                        words = shlex.split(line)
                        self.assertEqual(words[words.index('--') - 1], SERVER_NAME, line)
                        commands.append((words[0], language, words[words.index('--') + 1:]))
        self.assertEqual(sorted((tool, language) for tool, language, _ in commands),
                         [('claude', 'powershell'), ('claude', 'sh'), ('codex', 'sh')])
        for tool, language, command in commands:
            expected = windows if language == 'powershell' else posix
            base = validate('mcpServers', SERVER_NAME, expected)[1]
            self.assertEqual([word.replace('$SDK', base) for word in command],
                             [expected['command']] + expected['args'], tool + ' ' + language)
        # $SDK is defined by the check blocks the commands follow, in the same shell.
        self.assertTrue(any(re.search(r'^SDK=', block, re.M) for block in fenced(self.guide, 'sh')))
        self.assertTrue(any(re.search(r'^\$SDK = ', block, re.M) for block in fenced(self.guide, 'powershell')))
        tables = [codex_table(block) for block in fenced(self.guide, 'toml')]
        self.assertEqual(tables, [(SERVER_NAME, {'command': posix['command'], 'args': posix['args']})])

    @unittest.skipUnless(tomllib, 'tomllib needs Python 3.11 or newer')
    def test_codex_table_reader_agrees_with_tomllib(self):
        blocks = fenced(self.guide, 'toml')
        self.assertTrue(blocks)
        for block in blocks:
            name, table = codex_table(block)
            self.assertEqual(tomllib.loads(block)['mcp_servers'], {name: table})

    def test_messages_the_guide_quotes_are_the_servers(self):
        server = read(ROOT / 'tools/sco-core-mcp/server.py')
        for message in ('sco-core-mcp: catalogue refused: ', '"Unknown tool"',
                        'Example unavailable: requires a contained regular UTF-8 file without symlinks',
                        'unavailable beneath --repo-root'):
            with self.subTest(message=message):
                self.assertIn(message, server)
        guide = ' '.join(self.guide.split())
        for message in ('sco-core-mcp: catalogue refused: FileNotFoundError', "can't open file",
                        'Example unavailable: requires a contained regular UTF-8 file without symlinks',
                        'sdk/native_index.json unavailable beneath --repo-root',
                        'stability_context.game_state', 'Unknown tool'):
            with self.subTest(message=message):
                self.assertIn(message, guide)

    def test_invalid_launchers_are_refused(self):
        envelope, name, good = next(b for b in self.blocks if b[2].get('tools') == EXTENDED_TOOLS)
        self.assertEqual(validate(envelope, name, good)[0], 'kit')
        catalogue = good['args'].index('--catalogue')
        changes = {
            'wildcard': lambda e: e.update(tools=['*']),
            'unknown tool': lambda e: e.update(tools=EXTENDED_TOOLS + ['write_file']),
            'six names for twelve tools': lambda e: e.update(tools=MINIMAL_TOOLS),
            'twelve names without the flag': lambda e: e.update(args=e['args'][:-1]),
            'secret': lambda e: e.update(env={'API_KEY': 'forbidden-config-value'}),
            'wrong command': lambda e: e.update(command='python'),
            'py without -3': lambda e: e.update(command='py'),
            'flag and value in one element': lambda e: e['args'].__setitem__(
                catalogue, '--catalogue ' + e['args'][catalogue + 1]),
            'relative script': lambda e: e['args'].__setitem__(0, 'tools/sco-core-mcp/server.py'),
            'catalogue beside nothing': lambda e: e['args'].__setitem__(
                catalogue + 1, e['args'][catalogue + 1].replace('sdk-catalogue.json', 'other.json')),
            'repo root above the SDK folder': lambda e: e['args'].__setitem__(
                e['args'].index('--repo-root') + 1, e['args'][0].split('/' + KIT_FOLDER)[0]),
        }
        for label, change in changes.items():
            bad = deepcopy(good)
            change(bad)
            with self.subTest(label=label), self.assertRaises(ValueError):
                validate(envelope, name, bad)
        with self.assertRaises(ValueError):
            validate(envelope, 'sco-core', good)

    def test_kit_page_repeats_the_generic_entry(self):
        # docs/sdk-developer-mvp.md ships in the kit; it must not carry a variant of its own.
        self.assertEqual([entry for _, _, entry in launchers(read(KIT_PAGE))], [self.generic_entry()])
        self.assertEqual([name for _, name, _ in launchers(read(KIT_PAGE))], [SERVER_NAME])

    def test_readme_repository_setting_is_the_guides_checkout_block(self):
        checkout = [b for b in self.blocks if validate(*b)[0] == 'checkout']
        self.assertEqual(launchers(read(README)), checkout,
                         'the README repeats no client block except the repository setting')

    def test_retired_contradictions_stay_out_of_the_setup_docs(self):
        for relative in SETUP_DOCS:
            text = ' '.join(read(ROOT / relative).split())
            for phrase in RETIRED_WORDING:
                with self.subTest(document=relative, phrase=phrase):
                    self.assertNotIn(phrase, text)


class IntegrationTests(unittest.TestCase):
    """Start the server with the documented arguments."""

    @classmethod
    def setUpClass(cls):
        cls.blocks = launchers(read(GUIDE))

    def setUp(self):
        if not CATALOGUE.is_file():
            self.fail('no catalogue: build dist/sdk-catalogue.json (tools/sdk_catalogue.py build) '
                      'or set SCO_CORE_MCP_TEST_CATALOGUE')
        self.catalogue = json.loads(CATALOGUE.read_text(encoding='utf-8'))
        self.work = Path(tempfile.mkdtemp(prefix='sco_mcp_doc_'))
        self.addCleanup(shutil.rmtree, self.work, True)
        # A kit-shaped folder: no dist/ and no sdk/; everything sits at the root of sco-sdk-2.4.0.
        self.kit = self.work / KIT_FOLDER
        (self.kit / 'tools' / 'sco-core-mcp').mkdir(parents=True)
        for source, target in (('tools/sco-core-mcp/server.py', 'tools/sco-core-mcp/server.py'),
                               ('sdk/native_index.json', 'native_index.json'),
                               ('sdk/status_registry.json', 'status_registry.json'),
                               ('sdk/tools/sco-new-plugin.py', 'tools/sco-new-plugin.py'),
                               ('sdk/examples/hello/hello.c', 'examples/hello/hello.c'),
                               ('sdk/examples/cpp_hello/cpp_hello.cpp', 'examples/cpp_hello/cpp_hello.cpp'),
                               ('sdk/examples/cs_hello/CsHello.cs', 'examples/cs_hello/CsHello.cs')):
            (self.kit / target).parent.mkdir(parents=True, exist_ok=True)
            shutil.copy(ROOT / source, self.kit / target)
        shutil.copy(CATALOGUE, self.kit / 'sdk-catalogue.json')

    def launch(self, block, replacement, *calls, drop=(), omit=(), **overrides):
        """Run a documented block with its placeholder base path replaced by a real folder.

        `drop` removes arguments by their documented text, `omit` removes a flag and its value.
        """
        envelope, name, entry = block
        base = validate(envelope, name, entry)[1]
        args = [a.replace(base, replacement.as_posix()) for a in arguments(entry) if a not in drop]
        for flag in omit:
            del args[args.index('--' + flag):args.index('--' + flag) + 2]
        for flag, value in overrides.items():
            args[args.index('--' + flag) + 1] = Path(value).as_posix()
        result = subprocess.run([sys.executable, *args], input=messages(*calls), capture_output=True,
                                text=True, encoding='utf-8', timeout=30, cwd=self.work)
        replies = {}
        for line in result.stdout.splitlines():
            reply = json.loads(line)
            replies[reply['id']] = reply
        return result, replies

    def test_each_documented_block_serves_its_profile_tools(self):
        for block in self.blocks:
            layout, _, developer = validate(*block)
            overrides = {'catalogue': CATALOGUE} if layout == 'checkout' else {}
            with self.subTest(layout=layout, developer=developer, command=block[2]['command']):
                result, replies = self.launch(block, self.kit if layout == 'kit' else ROOT,
                                              ('list_services', {}), **overrides)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(replies[1]['result']['protocolVersion'], '2025-11-25')
                tools = replies[2]['result']['tools']
                self.assertEqual([t['name'] for t in tools], EXTENDED_TOOLS if developer else MINIMAL_TOOLS)
                for tool in tools:
                    self.assertIs(tool['annotations']['readOnlyHint'], True)
                self.assertNotIn('error', replies[3])
                self.assertFalse(replies[3]['result'].get('isError', False))
                self.assertEqual(replies[3]['result']['structuredContent']['provenance']['source_sha'],
                                 self.catalogue['source_sha'])

    def test_kit_failure_modes_look_as_the_guide_says(self):
        block = next(b for b in self.blocks if validate(*b)[0] == 'kit' and validate(*b)[2])
        # The kit stores examples/hello/hello.c; the catalogue records sdk/examples/hello/hello.c.
        example = next(r['id'] for r in self.catalogue['records']
                       if 'sdk/examples/hello/hello.c' in r.get('examples', ()))
        calls = (('list_engine_natives', {'limit': 1}),
                 ('generate_plugin_scaffold', {'id': 'doc_probe', 'lang': 'c'}),
                 ('list_services', {}),
                 ('get_example', {'symbol_or_id': example}))

        def stability(reply):
            context = reply['result']['structuredContent']['stability_context']
            services = reply['result']['structuredContent']['services']
            return context['game_state'], {s['stability'] for s in services}

        # SDK as the guide defines it: every verification signal of the guide is green.
        result, replies = self.launch(block, self.kit, *calls)
        self.assertEqual((result.returncode, result.stderr), (0, ''), 'a healthy server prints nothing')
        for index in (3, 4):
            self.assertEqual(replies[index]['result']['structuredContent']['state'], 'available')
        game_state, labels = stability(replies[5])
        self.assertEqual(game_state, 'read')
        self.assertTrue(labels <= {'stable', 'experimental'}, labels)
        served = replies[6]['result']['structuredContent']
        self.assertEqual(served['examples'][0]['path'], 'sdk/examples/hello/hello.c')
        self.assertTrue(served['examples'][0]['content'])
        self.assertIs(served['paths_only'], False)
        # --repo-root one level too high: the server starts and lists its tools, then reports
        # unavailable with checkout paths in the reason, and the stability labels are unknown.
        result, replies = self.launch(block, self.kit, *calls, **{'repo-root': self.work})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(replies[2]['result']['tools']), 12)
        self.assertTrue(replies[3]['result']['structuredContent']['reason'].startswith(
            'sdk/native_index.json unavailable beneath --repo-root'))
        self.assertTrue(replies[4]['result']['structuredContent']['reason'].startswith(
            'sdk/tools/sco-new-plugin.py unavailable beneath --repo-root'))
        for index in (3, 4):
            self.assertEqual(replies[index]['result']['structuredContent']['state'], 'unavailable')
        game_state, labels = stability(replies[5])
        self.assertEqual(game_state, 'unavailable')
        self.assertIn('unknown', labels)
        self.assertEqual(replies[6]['result']['structuredContent']['error'],
                         'Example unavailable: requires a contained regular UTF-8 file without symlinks')
        # --repo-root left out: the same signals, and get_example lists paths without contents.
        result, replies = self.launch(block, self.kit, *calls, omit=('repo-root',))
        self.assertEqual(result.returncode, 0, result.stderr)
        game_state, labels = stability(replies[5])
        self.assertEqual((game_state, 'unknown' in labels), ('unavailable', True))
        served = replies[6]['result']['structuredContent']
        self.assertIs(served['paths_only'], True)
        self.assertNotIn('content', served['examples'][0])
        # A path to server.py that does not exist: Python itself fails, before the server starts.
        result, replies = self.launch(block, self.work / 'not-the-sdk-folder')
        self.assertEqual((result.returncode, replies), (2, {}))
        self.assertIn("can't open file", result.stderr)
        self.assertNotIn('catalogue refused', result.stderr)
        # A checkout-style catalogue path in a kit: server.py starts, one line on stderr, exit 1.
        result, _ = self.launch(block, self.kit, catalogue=self.kit / 'dist/sdk-catalogue.json')
        self.assertEqual(result.returncode, 1)
        self.assertTrue(result.stderr.startswith('sco-core-mcp: catalogue refused: FileNotFoundError'),
                        result.stderr)
        # The minimal profile is the same arguments without --extended-tools: six tools, extended names unknown.
        _, replies = self.launch(block, self.kit, ('list_engine_natives', {'limit': 1}),
                                 drop=('--extended-tools',))
        self.assertEqual([t['name'] for t in replies[2]['result']['tools']], MINIMAL_TOOLS)
        self.assertEqual(replies[3]['error'], {'code': -32602, 'message': 'Unknown tool'})


if __name__ == '__main__':
    unittest.main(verbosity=2)
