#!/usr/bin/env python3
"""Offline tests for tools/doc_release_gate.py over synthetic repository fixtures."""
import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
import doc_release_gate as gate

REPO_ROOT = Path(__file__).resolve().parent.parent
FIVE = ["core", "game-sc", "legacy", "core-source", "host-source-sc"]


def scope_block(**overrides):
    scope = {"tag": "sdk-v2.1.0", "sdk_version": "2.1.0", "game_pack_version": "0.4.0",
             "plugin_abi": "1.1", "profiles": FIVE}
    scope.update(overrides)
    return ("<!-- release-scope:start -->\n```json\n" + json.dumps(scope)
            + "\n```\n<!-- release-scope:end -->\n")


SYMBOL_CITATIONS = ("# API\n\n`QueryService` (`src/api/sco_services.cpp`) records the hand-out.\n"
                    "A script error reads `main.lua:<line>: message`; the report is "
                    "`docs/plugins.md:205`.\n\n```sh\ncurl http://localhost:8080/x.py\n```\n")


def getting_started(version, game_pack):
    return (f"# Start\n\n| `sco-sdk-core-{version}.zip` | core |\n"
            f"| `sco-sdk-game-sc-{game_pack}.zip` | overlay |\n\n"
            f"```sh\n# download\ngh release download sdk-v{version} -p 'sco-sdk-core-{version}.zip'\n"
            f"cmake -S sco-sdk-core-{version} -B sdk-build\n```\n")


def mcp_doc(version, game_pack):
    return (f"# MCP\n\nCheck the hash of the catalogue you download; its `sdk_version` must be "
            f"your archive's `VERSION`.\n\n```sh\ngh release download sdk-v{version}\n```\n\n"
            f"Extract `sco-sdk-core-{version}` and pair it with game-pack {game_pack}.\n")


class Fixture:
    def __init__(self, root, version="2.1.0", game_pack="0.4.0", profiles=FIVE):
        self.root = Path(root)
        self.version = version
        self.files = {
            "sdk/VERSION": version + "\n",
            "sdk/components.json": json.dumps({"schema_version": 1,
                                               "profiles": {p: {} for p in profiles}}),
            "include/sc_game_pack.h": f'#define SCO_GAME_PACK_VERSION "{game_pack}"\n',
            "include/sco_api.h": "#define SCO_API_MAJOR 1\n#define SCO_API_MINOR 1\n",
            ".github/workflows/release-candidate.yml":
                "steps:\n  - run: |\n      for profile in " + " ".join(profiles) + "; do\n"
                "        true\n      done\n",
            "docs/getting-started.md": getting_started(version, game_pack),
            "docs/host-source-sc.md": f"# Host source\n\nPublished as sco-sdk-host-source-sc-{version}.zip.\n",
            "site/public-inputs.json": json.dumps({"schema_version": 1, "docs": [
                "docs/getting-started.md", "docs/api.md", "docs/host-source-sc.md",
                f"docs/releases/sdk-v{version}.md"]}),
            "docs/api.md": SYMBOL_CITATIONS,
            **{name: mcp_doc(version, game_pack) for name in gate.MCP_DOCS},
            "README.md": "# sco-core\n\nSee `QueryService` in `src/api/sco_services.cpp`.\n",
            "sdk/README.md": "# SDK\n\nStart from `sdk/examples/cpp_hello/cpp_hello.cpp`.\n",
            f"docs/releases/sdk-v{version}.md":
                f"# SDK {version}\n\n" + scope_block(tag=f"sdk-v{version}", sdk_version=version,
                                                     game_pack_version=game_pack,
                                                     profiles=list(profiles))
                + "\n## Coordinates\n\nAll declared profiles are published.\n",
        }
        self.write()

    def write(self):
        for name, text in self.files.items():
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)

    def set(self, name, text):
        self.files[name] = text
        self.write()

    @property
    def notes(self):
        return f"docs/releases/sdk-v{self.version}.md"


class DocReleaseGateTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.tmp = directory.name
        self.repo = Fixture(self.tmp)

    def run_cli(self, *args, tag="sdk-v2.1.0"):
        out, err = io.StringIO(), io.StringIO()
        argv = ["--root", self.tmp, *args] + (["--tag", tag] if tag else [])
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = gate.main(argv)
        return code, out.getvalue(), err.getvalue().splitlines()

    def assert_passes(self, *args, tag="sdk-v2.1.0"):
        code, out, err = self.run_cli(*args, tag=tag)
        self.assertEqual((code, err), (0, []))
        return out

    def assert_refused(self, token, *args, tag="sdk-v2.1.0"):
        code, _, err = self.run_cli(*args, tag=tag)
        self.assertEqual(code, 1)
        self.assertTrue(err and all(line.startswith("refused: ") for line in err), err)
        self.assertTrue(any(token in line for line in err), err)
        return err

    def test_current_release_passes(self):
        out = self.assert_passes()
        self.assertIn("approval: not compared", out)

    def test_next_release_passes(self):
        self.repo = Fixture(tempfile.mkdtemp(dir=self.tmp), version="2.2.0", game_pack="0.5.0")
        self.tmp = str(self.repo.root)
        self.assert_passes(tag="sdk-v2.2.0")

    def test_historical_prose_and_older_notes_allowed(self):
        self.repo.set("docs/releases/sdk-v2.0.0.md",
                      "# SDK 2.0.0\n\n## Coordinates\n\nNot published: the camera field of view.\n"
                      "host-source-sc publication is excluded.\n")
        self.repo.set(self.repo.notes, self.repo.files[self.repo.notes]
                      + "\n## Previous release (2.0.0)\n\nhost-source-sc publication is excluded;"
                      " the kit was not published.\n\n### Detail\n\nnot published\n")
        self.repo.set("docs/getting-started.md", self.repo.files["docs/getting-started.md"]
                      + "\n## Previous releases\n\n`sco-sdk-core-2.0.0.zip` from sdk-v2.0.0.\n")
        self.assert_passes()

    def test_section_after_historical_section_is_active_again(self):
        self.repo.set(self.repo.notes, self.repo.files[self.repo.notes]
                      + "\n## Previous release\n\nold\n\n## Limits\n\nhost-source-sc is not published.\n")
        self.assert_refused("not published")

    def test_missing_profile_refused(self):
        self.repo.set(self.repo.notes, scope_block(profiles=FIVE[:4]))
        self.assert_refused("profiles")

    def test_wrong_or_reordered_profiles_refused(self):
        for profiles in (FIVE[:4] + ["host-source"], FIVE[1:] + FIVE[:1]):
            with self.subTest(profiles=profiles):
                self.repo.set(self.repo.notes, scope_block(profiles=profiles))
                self.assert_refused("profiles")

    def test_workflow_profiles_must_match_components(self):
        self.repo.set(".github/workflows/release-candidate.yml",
                      "for profile in core game-sc legacy core-source; do\ndone\n")
        self.assert_refused(gate.CANDIDATE_WORKFLOW)

    def test_version_mismatches_refused(self):
        cases = (("sdk/VERSION", "2.1.1\n", "sdk_version"),
                 ("include/sc_game_pack.h", '#define SCO_GAME_PACK_VERSION "0.3.0"\n',
                  "game_pack_version"),
                 ("include/sco_api.h", "#define SCO_API_MAJOR 1\n#define SCO_API_MINOR 2\n",
                  "plugin_abi"))
        for name, text, token in cases:
            with self.subTest(name=name):
                original = self.repo.files[name]
                self.repo.set(name, text)
                self.assert_refused(token)
                self.repo.set(name, original)

    def test_tag_mismatches_refused(self):
        self.repo.set("docs/releases/sdk-v2.1.1.md", self.repo.files[self.repo.notes])
        self.assert_refused("--tag", tag="sdk-v2.1.1")
        self.repo.set(self.repo.notes, scope_block(tag="sdk-v2.1.1"))
        self.assert_refused("sdk_version")

    def test_missing_version_sources_refused(self):
        for name in ("include/sc_game_pack.h", "include/sco_api.h", "sdk/VERSION"):
            with self.subTest(name=name):
                (self.repo.root / name).unlink()
                self.assert_refused(name)
                self.repo.write()

    def test_scope_block_structure_refused(self):
        valid = scope_block()
        cases = {
            "duplicate block": valid + valid,
            "missing block": "# SDK 2.1.0\n",
            "missing end": valid.replace(gate.SCOPE_END, ""),
            "end before start": gate.SCOPE_END + "\n" + valid.replace(gate.SCOPE_END, "")
                                .replace(gate.SCOPE_START, "") + gate.SCOPE_START,
            "prose inside": valid.replace("```json", "Scope:\n```json"),
            "two fences": valid.replace("\n<!-- release-scope:end", "\n```json\n{}\n```\n<!-- release-scope:end"),
            "duplicate key": valid.replace('{"tag": "sdk-v2.1.0",', '{"tag": "sdk-v2.1.0", "tag": "sdk-v2.1.0",'),
            "unknown key": scope_block(extra=True),
            "missing key": valid.replace('"plugin_abi": "1.1", ', ""),
            "non-finite": valid.replace('"plugin_abi": "1.1"', '"plugin_abi": NaN'),
            "malformed version": scope_block(sdk_version="2.1"),
            "duplicate profile": scope_block(profiles=FIVE + ["core"]),
        }
        for name, text in cases.items():
            with self.subTest(name=name):
                self.repo.set(self.repo.notes, text)
                self.assert_refused(self.repo.notes)

    def test_forbidden_phrases_refused_in_active_notes(self):
        for phrase in ("host-source-sc publication is excluded;",
                       "The kit is **not\npublished** in this release.",
                       "It will not be published."):
            with self.subTest(phrase=phrase):
                self.repo.set(self.repo.notes, self.repo.files[self.repo.notes] + phrase + "\n")
                self.assert_refused(self.repo.notes)

    def test_forbidden_phrase_refused_in_host_source_doc(self):
        self.repo.set("docs/host-source-sc.md", "# Host source\n\n## Limits\n\nThe kit is not published.\n")
        self.assert_refused("docs/host-source-sc.md")

    def test_missing_host_source_doc_refused_when_profile_declared(self):
        (self.repo.root / "docs/host-source-sc.md").unlink()
        self.assert_refused("docs/host-source-sc.md")

    def test_stale_getting_started_coordinates_refused(self):
        cases = ("cmake -S sco-sdk-core-2.0.0 -B b\n",
                 "gh release download sdk-v2.0.0\n",
                 "`sco-sdk-game-sc-0.3.0.zip`\n",
                 "`sco-sdk-2.0.0.zip`\n",
                 "```sh\n# Previous release\nunzip sco-sdk-core-source-2.0.0.zip\n```\n")
        for line in cases:
            with self.subTest(line=line):
                self.repo.set("docs/getting-started.md",
                              getting_started("2.1.0", "0.4.0") + line)
                self.assert_refused(gate.GETTING_STARTED)

    def test_getting_started_without_coordinates_refused(self):
        self.repo.set("docs/getting-started.md", "# Start\n\nDownload the latest release.\n")
        self.assert_refused(gate.GETTING_STARTED)

    def write_approval(self, record, signed=True):
        path = self.repo.root / "docs/legal/release-decisions/sdk-v2.1.0.json"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(record))
        if signed:
            Path(str(path) + ".sig").write_text("signature")
        return "docs/legal/release-decisions/sdk-v2.1.0.json"

    def test_signed_approval_profiles_match(self):
        for record in ({"schema_version": 2, "tag": "sdk-v2.1.0", "profiles": FIVE},
                       {"schema_version": 1, "release_tag": "sdk-v2.1.0", "profiles": FIVE}):
            with self.subTest(version=record["schema_version"]):
                out = self.assert_passes("--approval", self.write_approval(record))
                self.assertIn("approval: profiles match", out)

    def test_approval_refusals(self):
        approval = "docs/legal/release-decisions/sdk-v2.1.0.json"
        self.assert_refused("not found", "--approval", approval)
        self.write_approval({"tag": "sdk-v2.1.0", "profiles": FIVE}, signed=False)
        self.assert_refused(".sig", "--approval", approval)
        cases = ({"tag": "sdk-v2.1.0", "profiles": FIVE[:3]},
                 {"tag": "sdk-v2.0.0", "profiles": FIVE},
                 ["not", "an", "object"])
        for record in cases:
            with self.subTest(record=record):
                self.assert_refused(approval, "--approval", self.write_approval(record))
        path = self.repo.root / approval
        path.write_text('{"tag": "sdk-v2.1.0", "tag": "sdk-v2.1.0", "profiles": []}')
        self.assert_refused("duplicate JSON key", "--approval", approval)

    def test_tag_is_required_and_validated(self):
        self.assert_refused("--tag", tag="")
        self.assert_refused("--tag", tag="v2.1.0")
        self.assert_refused("docs/releases/sdk-v9.9.9.md", tag="sdk-v9.9.9")

    def test_source_line_citations_refused(self):
        self.repo.set("docs/api.md", "# API\n\n`ProvideService` (`src/api/sco_services.cpp:42`).\n"
                                     "Range `include/sc_ipc.h:85-112`.\n")
        self.repo.set("README.md", "# sco-core\n\nSee quantum.cpp:417 and sco-plugin-check.c:891.\n")
        self.repo.set("sdk/README.md", "`Plugin.cs:446`, `main.lua:3` and `plugin.hpp:447`\n")
        self.repo.set(self.repo.notes, self.repo.files[self.repo.notes] + "Fixed in `tools/x.py:7`.\n")
        err = self.assert_refused("line number")
        cited = [line for line in err if "cites source by line number" in line]
        self.assertEqual(len(cited), 8, err)
        for token in ("docs/api.md line 3", "docs/api.md line 4", "README.md line 3",
                      "sdk/README.md line 1", f"{self.repo.notes} line ", "'src/api/sco_services.cpp:42'",
                      "'include/sc_ipc.h:85'", "'quantum.cpp:417'", "'main.lua:3'"):
            self.assertTrue(any(token in line for line in cited), (token, err))

    def test_symbol_citations_pass(self):
        self.assertEqual(gate.citation_refusals(self.repo.root, self.repo.notes), [])
        self.assert_passes()

    def test_unlisted_documents_are_not_scanned(self):
        self.repo.set("docs/design/notes.md", "`src/api/sco_services.cpp:42`\n")
        self.assert_passes()

    def test_public_inputs_list_refused_when_missing_or_malformed(self):
        for text in (None, "[]", '{"docs": []}', '{"docs": ["../outside.md"]}',
                     '{"docs": ["/abs.md"]}'):
            with self.subTest(text=text):
                path = self.repo.root / gate.PUBLIC_INPUTS
                if text is None:
                    path.unlink()
                else:
                    path.write_text(text)
                self.assert_refused(gate.PUBLIC_INPUTS)
                self.repo.write()

    def test_listed_public_doc_missing_refused(self):
        (self.repo.root / "docs/api.md").unlink()
        self.assert_refused("docs/api.md")

    def test_mcp_docs_stale_versions_refused(self):
        doc = gate.MCP_DOCS[0]
        for stale, token in (
                ("gh release download sdk-v2.0.0", "quotes version 2.0.0, not the declared 2.1.0"),
                ("Unzip sco-sdk-core-2.0.0.zip.", "quotes version 2.0.0"),
                ("Unzip sco-sdk-game-sc-0.3.0.zip.", "quotes version 0.3.0, not the declared 0.4.0"),
                ("The snapshot current for SDK 2.0.0 is below.", "quotes version 2.0.0"),
                ("Use SDK `2.0.0` here.", "quotes version 2.0.0"),
                ("With `sdk_version` `2.0.0`.", "quotes version 2.0.0"),
                ('{"sdk_version": "2.0.0"}', "quotes version 2.0.0"),
                ("Use game-pack `0.3.0`.", "quotes version 0.3.0"),
                ("`game_pack_version` `0.3.0`", "quotes version 0.3.0")):
            with self.subTest(stale=stale):
                self.repo.set(doc, mcp_doc("2.1.0", "0.4.0") + "\n" + stale + "\n")
                err = self.assert_refused(token)
                self.assertTrue(any(f"{doc} line 11 " in line for line in err), err)

    def test_mcp_docs_snapshot_identity_refused(self):
        doc = gate.MCP_DOCS[2]
        for pinned, token in (("sha256 `" + "29bb" * 16 + "`", "pins a sha256 digest"),
                              ("source_sha `" + "77c3" * 10 + "`", "pins a source SHA"),
                              ("It has 167 records and 30 tables.", "'167 records'"),
                              ("There are 317 engine natives.", "'317 engine natives'"),
                              ("The live build has 35 services.", "'35 services'")):
            with self.subTest(pinned=pinned):
                self.repo.set(doc, mcp_doc("2.1.0", "0.4.0") + pinned + "\n")
                self.assert_refused(token)

    def test_mcp_docs_current_and_historical_sections_pass(self):
        self.repo.set(gate.MCP_DOCS[1], mcp_doc("2.1.0", "0.4.0")
                      + "\nExtract `sco-sdk-game-sc-0.4.0`; tool runs at most 120 per 60 seconds.\n"
                      + "\n## Previous release\n\nSDK 2.0.0 shipped sco-sdk-2.0.0.zip, sha256 "
                      + "ab" * 32 + ", with 160 records.\n")
        self.assert_passes()

    def test_mcp_doc_missing_refused(self):
        (self.repo.root / gate.MCP_DOCS[3]).unlink()
        self.assert_refused(f"{gate.MCP_DOCS[3]} is missing or unreadable")

    def test_repository_mcp_docs_are_current(self):
        version = (REPO_ROOT / "sdk/VERSION").read_text().strip()
        notes = f"docs/releases/sdk-v{version}.md"
        scope = gate.parse_scope(gate.read_text(REPO_ROOT, notes), notes)
        self.assertEqual(gate.mcp_doc_refusals(REPO_ROOT, scope), [])

    def test_repository_public_docs_cite_no_source_lines(self):
        notes = f"docs/releases/sdk-v{(REPO_ROOT / 'sdk/VERSION').read_text().strip()}.md"
        self.assertEqual(gate.citation_refusals(REPO_ROOT, notes), [])

    def test_repository_workflow_and_components_profiles_agree(self):
        self.assertEqual(gate.workflow_profiles(REPO_ROOT), gate.components_profiles(REPO_ROOT))


if __name__ == "__main__":
    unittest.main()
