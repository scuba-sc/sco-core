#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Offline tests for tools/release_docs.py over a synthetic repository root."""
import contextlib
import io
import json
import re
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
import doc_release_gate as gate
import release_docs as rd

REPO_ROOT = Path(__file__).resolve().parent.parent
PROFILES = ["core", "game-sc", "legacy", "core-source", "host-source-sc"]

HEADER = """#define SCO_GAME_PACK_VERSION "0.8.1"
#define SCO_GAME_PACK_VERSION_MAJOR 0
#define SCO_GAME_PACK_VERSION_MINOR 8
#define SCO_GAME_PACK_VERSION_PATCH 1
#define SCO_GAME_PACK_VERIFIED_BUILD "4.10.200.58746"
#define SCO_GAME_PACK_VERIFIED_BUILD_PARTS_0 4
#define SCO_GAME_PACK_VERIFIED_BUILD_PARTS_1 10
#define SCO_GAME_PACK_VERIFIED_BUILD_PARTS_2 200
#define SCO_GAME_PACK_VERIFIED_BUILD_PARTS_3 58746
#define SCO_GAME_PACK_VERIFIED_PE_TIMESTAMP "0x6af4861d"
"""
API = "#define SCO_API_MAJOR 1\n#define SCO_API_MINOR 1\n"
GEN = """HUB = [
    ('docs/releases/sdk-v2.4.1.md', 'SDK 2.4.1 release notes.'),
    ('docs/releases/sdk-v2.4.0.md', 'SDK 2.4.0 release notes.'),
]
"""


def region(name, body="stale\n"):
    return f"<!-- release-block:{name} -->\n{body}<!-- /release-block:{name} -->\n"


def build_entry(build, patch, packs, rows=(), name_source="community"):
    return {"build": build, "patch": patch, "channel": "LIVE", "changelist": "13165946",
            "live_date": "2026-11-12", "name_source": name_source, "name_note": "Launcher string.",
            "game_packs": list(packs), "sdk_releases": ["sdk-v2.4.1"], "changed_rows": list(rows),
            "changes": ["What moved."]}


class Fixture(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.saved_root = rd.ROOT
        rd.ROOT = self.root
        self.write("sdk/VERSION", "2.4.1\n")
        self.write("sdk/DEVELOPMENT_VERSION", "2.4.2-dev.1\n")
        self.write("sdk/CANDIDATE_TAG", "# Candidate target.\nsdk-v2.4.1\n")
        self.write("sdk/components.json",
                   json.dumps({"schema_version": 1, "profiles": {p: {} for p in PROFILES}}))
        self.write("include/sc_game_pack.h", HEADER)
        self.write("include/sco_api.h", API)
        self.write("docs/releases/sdk-v2.4.0.md", "# SDK 2.4.0 release notes\n")
        self.write("docs/releases/sdk-v2.4.1.md", "# SDK 2.4.1 release notes\n")
        self.write("CHANGELOG.md", "# Changelog\n\n## Unreleased\n\n## sdk-v2.4.1\n\nText.\n")
        self.write("site/public-inputs.json",
                   json.dumps({"schema_version": 1, "docs": ["docs/README.md",
                                                             "docs/releases/sdk-v2.4.1.md"]},
                              indent=2) + "\n")
        self.write("site/scripts/gen.py", GEN)
        self.write(rd.REGISTER, json.dumps({"schema_version": 1, "builds": [
            build_entry("4.10.197.2878", "Alpha 4.10.2 hotfix", ["0.8.0"]),
            build_entry("4.10.200.58746", "Alpha 4.10.3", ["0.8.1"], rows=["quantum.on_action"]),
        ]}))
        blocks = self.root / "docs/release-blocks"
        blocks.mkdir(parents=True, exist_ok=True)
        for template in (REPO_ROOT / "docs/release-blocks").glob("*.md"):
            shutil.copy(template, blocks / template.name)

    def tearDown(self):
        rd.ROOT = self.saved_root
        self.tmp.cleanup()

    def write(self, rel, content, newline="\n"):
        path = self.root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content.replace("\n", newline).encode("utf-8"))

    def add_build(self, build, patch, packs):
        register = json.loads(self.read(rd.REGISTER))
        register["builds"].append(build_entry(build, patch, packs))
        self.write(rd.REGISTER, json.dumps(register))

    def read(self, rel):
        return (self.root / rel).read_bytes().decode("utf-8")

    def run_main(self, *argv):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = rd.main(list(argv))
        return code, out.getvalue(), err.getvalue()


class RenderTests(Fixture):
    def test_check_detects_drift_and_write_fixes_it(self):
        self.write("README.md", "# Title\n\n" + region("current-release") + "\nTail.\n")
        code, _, err = self.run_main("--check")
        self.assertEqual(code, 1)
        self.assertIn("README.md", err)
        before = self.read("README.md")
        self.assertEqual(self.run_main("--check")[0], 1)
        self.assertEqual(self.read("README.md"), before, "--check must not write")
        code, out, _ = self.run_main("--write")
        self.assertEqual(code, 0)
        self.assertIn("1 file(s) updated", out)
        text = self.read("README.md")
        self.assertIn("SDK **2.4.1** uses Star Citizen game pack **0.8.1**", text)
        self.assertIn("Star Citizen\nAlpha 4.10.3, build 4.10.200.58746; see [game builds](docs/game/builds.md))", text)
        self.assertIn("[SDK 2.4.0](https://github.com/scuba-sc/sco-core/blob/sdk-v2.4.0/docs/releases/sdk-v2.4.0.md)", text)
        self.assertTrue(text.startswith("# Title\n\n<!-- release-block:current-release -->\nSDK"))
        self.assertTrue(text.endswith("\nTail.\n"))
        code, out, _ = self.run_main("--check")
        self.assertEqual(code, 0)
        self.assertIn("checked 1 release region(s); 0 file(s) stale", out)

    def test_write_is_idempotent(self):
        self.write("README.md", region("download-core"))
        self.run_main("--write")
        once = self.read("README.md")
        self.assertIn("gh release download sdk-v2.4.1 -R scuba-sc/sco-core -p sco-sdk-core-2.4.1.zip", once)
        self.run_main("--write")
        self.assertEqual(self.read("README.md"), once)

    def test_crlf_files_stay_crlf_and_lf_files_stay_lf(self):
        self.write("README.md", "# T\n\n" + region("download-combined") + "\nTail.\n", "\r\n")
        self.write("docs/guide.md", "# G\n\n" + region("download-core") + "\nTail.\n")
        self.assertEqual(self.run_main("--write")[0], 0)
        crlf, lf = self.read("README.md"), self.read("docs/guide.md")
        self.assertIn("sco-sdk-2.4.1.zip", crlf)
        self.assertEqual(crlf.count("\n"), crlf.count("\r\n"), "lone LF in a CRLF file")
        self.assertNotIn("\r", lf)
        self.assertEqual(self.run_main("--check")[0], 0)

    def test_stray_line_ending_does_not_flip_a_file(self):
        self.write("docs/mixed.md", "a\nb\nc\r\nd\n\n" + region("download-core"))
        self.run_main("--write")
        text = self.read("docs/mixed.md")
        self.assertEqual(text.count("\r\n"), 1)
        self.assertIn("sco-sdk-core-2.4.1.zip", text)

    def test_inline_markers(self):
        self.write("docs/a.md",
                   "SDK <!--rv:sdk_version-->0.0.1<!--/rv--> with pack **<!--rv:game_pack_version-->x<!--/rv-->**"
                   " on <!--rv:game_build-->y<!--/rv-->, ABI <!--rv:plugin_abi--><!--/rv-->.\n")
        self.assertEqual(self.run_main("--write")[0], 0)
        self.assertEqual(self.read("docs/a.md"),
                         "SDK <!--rv:sdk_version-->2.4.1<!--/rv--> with pack **<!--rv:game_pack_version-->0.8.1<!--/rv-->**"
                         " on <!--rv:game_build-->4.10.200.58746<!--/rv-->, ABI <!--rv:plugin_abi-->1.1<!--/rv-->.\n")
        code, out, _ = self.run_main("--check")
        self.assertEqual(code, 0)
        self.assertIn("4 release region(s)", out)

    def test_inline_marker_drift_is_stale(self):
        self.write("docs/a.md", "SDK <!--rv:sdk_version-->2.3.0<!--/rv-->\n")
        code, _, err = self.run_main("--check")
        self.assertEqual(code, 1)
        self.assertIn("docs/a.md", err)

    def test_root_prefix_follows_folder_depth(self):
        self.write("docs/release-blocks/rootlink.md", "[release notes]({{root}}{{notes}})\n")
        for rel in ("README.md", "docs/x.md", "docs/sub/y.md"):
            self.write(rel, region("rootlink"))
        self.assertEqual(self.run_main("--write")[0], 0)
        self.assertIn("[release notes](docs/releases/sdk-v2.4.1.md)", self.read("README.md"))
        self.assertNotIn("](../", self.read("README.md"))
        self.assertIn("[release notes](../docs/releases/sdk-v2.4.1.md)", self.read("docs/x.md"))
        self.assertIn("[release notes](../../docs/releases/sdk-v2.4.1.md)", self.read("docs/sub/y.md"))

    def test_release_notes_and_template_folder_are_not_scanned(self):
        stale = "<!--rv:sdk_version-->0.0.0<!--/rv-->\n"
        self.write("docs/releases/sdk-v2.4.1.md", stale)
        self.write("docs/release-blocks/extra.md", stale)
        code, out, _ = self.run_main("--check")
        self.assertEqual(code, 0, out)
        self.assertEqual(self.read("docs/releases/sdk-v2.4.1.md"), stale)

    def test_previous_version_is_newest_older_release(self):
        self.write("docs/releases/sdk-v2.3.0.md", "# 2.3.0\n")
        self.write("docs/releases/sdk-v2.10.0.md", "# newer than the current one\n")
        self.assertEqual(rd.coordinates()["previous_version"], "2.4.0")

    def test_list_prints_coordinates_and_regions(self):
        self.write("README.md", region("download-core") + "SDK <!--rv:tag-->x<!--/rv-->\n")
        code, out, _ = self.run_main("--list")
        self.assertEqual(code, 0)
        self.assertIn("sdk_version = 2.4.1", out)
        self.assertIn("plugin_abi = 1.1", out)
        self.assertIn("README.md: block download-core", out)
        self.assertIn("README.md: inline tag", out)


class RefusalTests(Fixture):
    def refuses(self, needle, *argv):
        code, _, err = self.run_main(*(argv or ("--write",)))
        self.assertEqual(code, 1, err)
        self.assertIn("refused:", err)
        self.assertIn(needle, err)

    def test_game_builds_block_renders_the_register_newest_first(self):
        self.write("docs/game/builds.md", "# Builds\n\n" + region("game-builds"))
        self.write("docs/a.md", "Patch <!--rv:game_patch-->x<!--/rv-->.\n")
        self.assertEqual(self.run_main("--write")[0], 0)
        page = self.read("docs/game/builds.md")
        self.assertLess(page.index("| Alpha 4.10.3\\* | 4.10.200.58746"), page.index("| Alpha 4.10.2 hotfix"))
        self.assertIn("### Alpha 4.10.3: 4.10.200.58746 (since 4.10.197.2878, Alpha 4.10.2 hotfix)", page)
        self.assertIn("Rows that changed: `quantum.on_action`.", page)
        self.assertIn("`sdk-v2.4.1` (this release)", page)
        self.assertIn("-->Alpha 4.10.3<!--", self.read("docs/a.md"))

    def test_official_names_drop_the_community_footnote(self):
        self.write(rd.REGISTER, json.dumps({"builds": [
            build_entry("4.10.200.58746", "Alpha 4.10.3", ["0.8.1"], name_source="official")]}))
        self.assertNotIn("community", rd.render_builds(rd.coordinates()))

    def test_verified_build_must_be_the_newest_register_entry(self):
        self.write(rd.REGISTER, json.dumps({"builds": [
            build_entry("4.10.197.2878", "Alpha 4.10.2 hotfix", ["0.8.1"])]}))
        code, _, err = self.run_main("--check")
        self.assertNotEqual(code, 0)
        self.assertIn("add the build with its public patch name first", err)

    def test_register_must_name_the_game_pack(self):
        self.write(rd.REGISTER, json.dumps({"builds": [
            build_entry("4.10.200.58746", "Alpha 4.10.3", ["0.8.0"])]}))
        with self.assertRaisesRegex(rd.Refusal, "does not list game pack 0.8.1"):
            rd.coordinates()

    def test_register_refuses_malformed_entries(self):
        good = build_entry("4.10.200.58746", "Alpha 4.10.3", ["0.8.1"])
        older = build_entry("4.10.197.2878", "Alpha 4.10.2 hotfix", ["0.8.0"])
        cases = {
            "missing changes": dict(good, changes=[]),
            "missing field": {k: v for k, v in good.items() if k != "changelist"},
            "bad date": dict(good, live_date="12 Nov"),
            "bad source": dict(good, name_source="rumour"),
        }
        for label, entry in cases.items():
            with self.subTest(label):
                self.write(rd.REGISTER, json.dumps({"builds": [entry]}))
                with self.assertRaises(rd.Refusal):
                    rd.build_register()
        with self.subTest("order"):
            self.write(rd.REGISTER, json.dumps({"builds": [good, older]}))
            with self.assertRaisesRegex(rd.Refusal, "oldest first"):
                rd.build_register()
        with self.subTest("missing file"):
            (self.root / rd.REGISTER).unlink()
            with self.assertRaisesRegex(rd.Refusal, "missing"):
                rd.build_register()

    def test_repository_register_matches_the_header(self):
        rd.ROOT = REPO_ROOT
        values = rd.coordinates()
        self.assertEqual(values["game_build"], rd.build_register()[-1]["build"])

    def test_unknown_placeholder_refuses_and_writes_nothing(self):
        self.write("docs/release-blocks/bad.md",
                   "<!-- Template for bad. -->\nSDK {{sdk_version}} and {{nope}}\n")
        self.write("docs/a.md", region("bad"))
        before = self.read("docs/a.md")
        self.refuses("unknown placeholder {{nope}}")
        self.assertEqual(self.read("docs/a.md"), before)

    def test_unknown_inline_key_refuses(self):
        self.write("docs/a.md", "<!--rv:nope-->x<!--/rv-->\n")
        self.refuses("unknown inline key nope")

    def test_unterminated_inline_marker_refuses(self):
        self.write("docs/a.md", "SDK <!--rv:sdk_version-->2.4.1 and more\n")
        self.refuses("unterminated inline rv marker")

    def test_missing_template_refuses(self):
        self.write("docs/a.md", region("no-such-block"))
        self.refuses("has no template")

    def test_mismatched_block_marker_refuses(self):
        self.write("docs/a.md",
                   "<!-- release-block:current-release -->\nx\n<!-- /release-block:archives -->\n")
        self.refuses("mismatched release-block marker")
        self.refuses("mismatched release-block marker", "--check")

    def test_unterminated_block_marker_refuses(self):
        self.write("docs/a.md", "<!-- release-block:current-release -->\nx\n")
        self.refuses("mismatched release-block marker")

    def test_no_earlier_release_refuses(self):
        (self.root / "docs/releases/sdk-v2.4.0.md").unlink()
        self.refuses("no earlier docs/releases", "--check")

    def test_game_flags_need_bump(self):
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as ctx:
            rd.main(["--check", "--game-pack", "0.9.0"])
        self.assertEqual(ctx.exception.code, 2)


class BumpTests(Fixture):
    def setUp(self):
        super().setUp()
        self.write("README.md", "# T\n\n" + region("current-release"))
        self.write("docs/a.md", "SDK <!--rv:sdk_version-->2.4.1<!--/rv-->, previous <!--rv:previous_version-->2.4.0<!--/rv-->\n")
        self.run_main("--write")

    def snapshot(self):
        return {p.relative_to(self.root).as_posix(): p.read_bytes()
                for p in sorted(self.root.rglob("*")) if p.is_file()}

    def test_bump_writes_every_input_and_renders_previous(self):
        code, out, err = self.run_main("--bump", "2.4.2")
        self.assertEqual(code, 0, err)
        self.assertEqual(self.read("sdk/VERSION"), "2.4.2\n")
        self.assertEqual(self.read("sdk/DEVELOPMENT_VERSION"), "2.4.3-dev.1\n")
        self.assertEqual(self.read("sdk/CANDIDATE_TAG"), "# Candidate target.\nsdk-v2.4.2\n")
        changelog = self.read("CHANGELOG.md")
        self.assertIn("## Unreleased\n\n## sdk-v2.4.2\n\n## sdk-v2.4.1\n", changelog)
        notes = self.read("docs/releases/sdk-v2.4.2.md")
        self.assertTrue(notes.startswith("# sco-core SDK 2.4.2 release notes\n"))
        self.assertIn("sco-sdk-2.4.2.zip", notes)
        self.assertIn("[SDK 2.4.1 release notes](sdk-v2.4.1.md)", notes)
        scope = gate.parse_scope(notes, "docs/releases/sdk-v2.4.2.md")
        self.assertEqual(scope, {"tag": "sdk-v2.4.2", "sdk_version": "2.4.2", "game_pack_version": "0.8.1",
                                 "plugin_abi": "1.1", "profiles": PROFILES})
        inputs = json.loads(self.read("site/public-inputs.json"))
        self.assertEqual(inputs["docs"], ["docs/README.md", "docs/releases/sdk-v2.4.1.md",
                                          "docs/releases/sdk-v2.4.2.md"])
        gen = self.read("site/scripts/gen.py")
        self.assertIn("    ('docs/releases/sdk-v2.4.2.md', 'SDK 2.4.2 release notes.'),\n"
                      "    ('docs/releases/sdk-v2.4.1.md',", gen)
        readme = self.read("README.md")
        self.assertIn("SDK **2.4.2** uses", readme)
        self.assertIn("The previous release is\n[SDK 2.4.1](https://github.com/scuba-sc/sco-core/blob/sdk-v2.4.1/docs/releases/sdk-v2.4.1.md).", readme)
        self.assertEqual(self.read("docs/a.md"),
                         "SDK <!--rv:sdk_version-->2.4.2<!--/rv-->, previous <!--rv:previous_version-->2.4.1<!--/rv-->\n")
        self.assertEqual(self.run_main("--check")[0], 0)

    def test_bump_is_repeatable_without_duplicating_entries(self):
        self.run_main("--bump", "2.4.2")
        self.write("sdk/VERSION", "2.4.1\n")  # as if the bump were re-run from the same start
        self.assertEqual(self.run_main("--bump", "2.4.2")[0], 0)
        self.assertEqual(self.read("CHANGELOG.md").count("## sdk-v2.4.2\n"), 1)
        self.assertEqual(self.read("site/public-inputs.json").count("sdk-v2.4.2.md"), 1)
        self.assertEqual(self.read("site/scripts/gen.py").count("sdk-v2.4.2.md"), 1)

    def test_bump_keeps_an_existing_notes_file(self):
        self.write("docs/releases/sdk-v2.4.2.md", "# hand-written\n")
        self.assertEqual(self.run_main("--bump", "2.4.2")[0], 0)
        self.assertEqual(self.read("docs/releases/sdk-v2.4.2.md"), "# hand-written\n")

    def test_bump_game_pack_and_build_update_the_numeric_mirrors(self):
        self.add_build("4.11.0.60123", "Alpha 4.11.0", ["0.9.2"])
        code, _, err = self.run_main("--bump", "2.5.0", "--game-pack", "0.9.2", "--game-build", "4.11.0.60123")
        self.assertEqual(code, 0, err)
        self.assertIn("SCO_GAME_PACK_VERIFIED_PE_TIMESTAMP", err)
        header = self.read("include/sc_game_pack.h")
        for line in ('#define SCO_GAME_PACK_VERSION "0.9.2"', "#define SCO_GAME_PACK_VERSION_MAJOR 0",
                     "#define SCO_GAME_PACK_VERSION_MINOR 9", "#define SCO_GAME_PACK_VERSION_PATCH 2",
                     '#define SCO_GAME_PACK_VERIFIED_BUILD "4.11.0.60123"',
                     "#define SCO_GAME_PACK_VERIFIED_BUILD_PARTS_0 4", "#define SCO_GAME_PACK_VERIFIED_BUILD_PARTS_1 11",
                     "#define SCO_GAME_PACK_VERIFIED_BUILD_PARTS_2 0", "#define SCO_GAME_PACK_VERIFIED_BUILD_PARTS_3 60123"):
            self.assertIn(line + "\n", header)
        self.assertIn('"0x6af4861d"', header)
        self.assertEqual(self.read("sdk/DEVELOPMENT_VERSION"), "2.5.1-dev.1\n")
        self.assertIn("game pack **0.9.2**", self.read("README.md"))
        self.assertIn("4.11.0.60123", self.read("README.md"))
        notes = self.read("docs/releases/sdk-v2.5.0.md")
        self.assertEqual(gate.parse_scope(notes, "n")["game_pack_version"], "0.9.2")

    def test_bump_keeps_crlf_in_every_file_it_edits(self):
        for rel in ("CHANGELOG.md", "site/public-inputs.json", "site/scripts/gen.py", "sdk/VERSION",
                    "sdk/CANDIDATE_TAG", "include/sc_game_pack.h"):
            self.write(rel, self.read(rel), "\r\n")
        register = json.loads(self.read(rd.REGISTER))
        register["builds"][-1]["game_packs"].append("0.8.2")
        self.write(rd.REGISTER, json.dumps(register))
        code, _, err = self.run_main("--bump", "2.4.2", "--game-pack", "0.8.2")
        self.assertEqual(code, 0, err)
        for rel in ("CHANGELOG.md", "site/public-inputs.json", "site/scripts/gen.py", "sdk/VERSION",
                    "sdk/CANDIDATE_TAG", "include/sc_game_pack.h"):
            text = self.read(rel)
            self.assertEqual(text.count("\n"), text.count("\r\n"), rel)
        json.loads(self.read("site/public-inputs.json"))
        self.assertIn("## sdk-v2.4.2\r\n", self.read("CHANGELOG.md"))

    def test_bump_refuses_a_version_that_does_not_increase(self):
        before = self.snapshot()
        for version in ("2.4.1", "2.4.0", "1.9.9"):
            code, _, err = self.run_main("--bump", version)
            self.assertEqual(code, 1)
            self.assertIn("does not exceed sdk/VERSION 2.4.1", err)
        code, _, err = self.run_main("--bump", "2.4")
        self.assertEqual(code, 1)
        self.assertIn("not a version", err)
        self.assertEqual(self.snapshot(), before)

    def test_bump_refusal_leaves_the_tree_untouched(self):
        self.write("site/scripts/gen.py", "HUB = []\n")
        before = self.snapshot()
        code, _, err = self.run_main("--bump", "2.4.2", "--game-pack", "0.9.0")
        self.assertEqual(code, 1)
        self.assertIn("site/scripts/gen.py: no hub entry", err)
        self.assertEqual(self.snapshot(), before)

    def test_late_refusal_rolls_back_every_write(self):
        # Passes preflight but refuses in set_define, after the version files would be written.
        header = self.read("include/sc_game_pack.h")
        self.write("include/sc_game_pack.h", header + "#define SCO_GAME_PACK_VERSION \"0.0.0\"\n")
        before = self.snapshot()
        code, _, err = self.run_main("--bump", "2.4.2", "--game-pack", "0.9.0")
        self.assertEqual(code, 1, err)
        self.assertEqual(self.snapshot(), before)

    def test_bump_refuses_a_build_missing_from_the_register(self):
        before = self.read("include/sc_game_pack.h")
        code, _, err = self.run_main("--bump", "2.5.0", "--game-build", "4.11.0.60123")
        self.assertEqual(code, 1)
        self.assertIn("not the newest build in docs/game/builds.json", err)
        self.assertEqual(self.read("include/sc_game_pack.h"), before)

    def test_bump_refuses_a_malformed_game_build(self):
        before = self.snapshot()
        for build in ("4.10.200", "4.10.200.x", '4.10.200.1"\n#define X 1'):
            code, _, err = self.run_main("--bump", "2.4.2", "--game-build", build)
            self.assertEqual(code, 1)
            self.assertIn("not a game build", err)
        self.assertEqual(self.snapshot(), before)

    def test_bump_refuses_without_an_unreleased_heading(self):
        self.write("CHANGELOG.md", "# Changelog\n\n## sdk-v2.4.1\n")
        before = self.snapshot()
        self.assertEqual(self.run_main("--bump", "2.4.2")[0], 1)
        self.assertEqual(self.snapshot(), before)


if __name__ == "__main__":
    unittest.main()
