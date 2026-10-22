import contextlib
import copy
import datetime
import io
import shutil
import tempfile
import unittest
from pathlib import Path

import plugin_directory as pd

ROOT = Path(__file__).resolve().parent.parent
# Dates are relative to the real UTC day, so maintainers adding or updating entries never trip
# the "in the future" check here; the mutation tests run on a copy of the index with no community
# entries, so real listings never change what they test.
TODAY = datetime.datetime.now(datetime.timezone.utc).date()
YESTERDAY = (TODAY - datetime.timedelta(days=1)).isoformat()
TOMORROW = (TODAY + datetime.timedelta(days=1)).isoformat()
NEXT_YEAR = (TODAY + datetime.timedelta(days=400)).isoformat()


def quiet_main(args):
    with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
        return pd.main(args)


def community_entry(**changes):
    entry = {
        "name": "Example waypoints",
        "ids": ["example_waypoints"],
        "repository": "https://example.com/author/example-waypoints",
        "author": "Example Author",
        "licence": "MIT",
        "languages": ["c"],
        "sdk_version": "2.3.0",
        "game_pack_version": None,
        "checker": {"full": "pass", "short": "pass", "date": YESTERDAY},
        "offline": True,
        "in_game": "not-run",
        "description": "Synthetic test entry: keeps a list of waypoints in sco.storage.",
        "listed": TODAY.isoformat(),
    }
    entry.update(changes)
    return entry


class PluginDirectoryTest(unittest.TestCase):
    directory = pd.load(ROOT)

    def base(self):
        """The live first-party entries with an empty community section."""
        directory = copy.deepcopy(self.directory)
        directory["community"] = []
        return directory

    def errors_after(self, mutate):
        directory = self.base()
        mutate(directory)
        return pd.validate(directory, ROOT, TODAY)

    def assertRefused(self, mutate, fragment):
        errors = self.errors_after(mutate)
        self.assertTrue(any(fragment in e for e in errors), errors)

    def assertAccepted(self, mutate):
        self.assertEqual(self.errors_after(mutate), [])

    def add(self, **changes):
        return lambda d: d["community"].append(community_entry(**changes))

    def test_repository_index_is_valid_and_rendered(self):
        self.assertEqual(pd.validate(self.directory, ROOT), [])
        self.assertIn("tables match", pd.run(ROOT, write=False))

    def test_first_party_lists_only_the_public_repositories(self):
        repositories = {e["repository"] for e in self.directory["first_party"]}
        self.assertEqual(repositories, {"https://github.com/scuba-sc/sc-offline",
                                        "https://github.com/scuba-sc/sc-mp"})
        for entry in self.directory["first_party"]:
            self.assertIn(entry["in_game"], ("not-run", "not-verified"))

    def test_fields_document_exactly_the_entry_keys(self):
        self.assertEqual(set(self.directory["fields"]), pd.ENTRY_KEYS)
        self.assertRefused(lambda d: d["fields"].pop("licence"), "fields must describe")
        self.assertRefused(lambda d: d["fields"].update(stars="popularity"), "fields must describe")
        self.assertRefused(lambda d: d.update(extra=1), "top-level keys")
        self.assertRefused(lambda d: d.update(schema_version=2), "schema_version")

    def test_valid_community_entry_is_accepted_and_rendered(self):
        self.assertAccepted(self.add())
        directory = self.base()
        self.assertIn("No community plugins are listed yet", pd.render(directory))
        directory["community"].append(community_entry(note="Synthetic caveat."))
        text = pd.render(directory)
        self.assertIn("| [Example waypoints](https://example.com/author/example-waypoints) |", text)
        self.assertIn("| full pass, short pass (%s) | confirmed | not run in game | %s |" % (YESTERDAY, TODAY), text)
        self.assertIn("Synthetic caveat.", text)
        self.assertNotIn("No community plugins", text)

    def test_repository_must_be_https_without_extras(self):
        for url, fragment in (("http://example.com/a/b", "not an https URL"),
                              ("git@github.com:a/b.git", "not an https URL"),
                              ("https://user:token@example.com/a/b", "credentials"),
                              ("https://example.com/a/b?ref=x", "query or fragment"),
                              ("https://example.com/a/b#readme", "query or fragment"),
                              ("https://example.com/", "names no repository"),
                              ("https://example.com/a b", "one https URL"),
                              ("https://example.com/a)(b", "one https URL")):
            self.assertRefused(self.add(repository=url), fragment)

    def test_ids_are_well_formed_and_unique_across_sections(self):
        self.assertRefused(self.add(ids=["Bad-Id"]), "is not [a-z0-9_]")
        self.assertRefused(self.add(ids=["x" * 32]), "is not [a-z0-9_]")
        self.assertRefused(self.add(ids=[]), "non-empty list")
        self.assertRefused(self.add(ids=["multiplayer"]), "id multiplayer is already listed")
        self.assertRefused(self.add(ids=["a", "a"]), "id a is already listed")
        self.assertRefused(self.add(name=self.directory["first_party"][1]["name"].upper()), "listed twice")
        self.assertRefused(self.add(ids=["quantum_drive"]), "id quantum_drive is already listed")
        for reserved in ("sco", "host", "menu", "game"):
            self.assertRefused(self.add(ids=[reserved]), "id %s is reserved" % reserved)

    def test_licence_must_be_stated_as_spdx(self):
        self.assertRefused(lambda d: d["community"].append(
            {k: v for k, v in community_entry().items() if k != "licence"}), "missing keys ['licence']")
        self.assertRefused(self.add(licence=""), "licence missing")
        self.assertRefused(self.add(licence="NOASSERTION"), "states no licence")
        self.assertRefused(self.add(licence="MIT or whatever you like"), "not an SPDX expression")
        self.assertRefused(self.add(licence="(MIT OR"), "not an SPDX expression")
        for licence in ("GPL-3.0-or-later", "MIT OR Apache-2.0", "(MIT AND BSD-2-Clause)",
                        "GPL-2.0-only WITH Classpath-exception-2.0", "LicenseRef-Author-1"):
            self.assertAccepted(self.add(licence=licence))

    def test_languages_and_versions(self):
        self.assertRefused(self.add(languages=["rust"]), "languages")
        self.assertRefused(self.add(languages=["c", "c"]), "languages")
        self.assertRefused(self.add(sdk_version="2.4"), "is not x.y.z")
        self.assertRefused(self.add(sdk_version="99.0.0"), "newer than this SDK")
        self.assertRefused(self.add(game_pack_version="0.6"), "is not x.y.z or null")
        self.assertRefused(self.add(game_pack_version="9.0.0"), "newer than this game pack")
        self.assertAccepted(self.add(languages=["cpp", "csharp", "lua", "data"], game_pack_version="0.6.0"))

    def test_checker_results_and_dates(self):
        self.assertRefused(self.add(checker={"full": "fail", "short": "pass", "date": YESTERDAY}),
                           "passing full-mode")
        self.assertRefused(self.add(checker={"full": "not-recorded", "short": "not-recorded", "date": None}),
                           "passing full-mode")
        self.assertRefused(self.add(checker={"full": "ok", "short": "pass", "date": YESTERDAY}), "checker full")
        self.assertRefused(self.add(checker={"full": ["pass"], "short": "pass", "date": YESTERDAY}), "checker full")
        self.assertRefused(self.add(checker={"full": "pass", "short": {"x": 1}, "date": YESTERDAY}), "checker short")
        self.assertRefused(self.add(checker={"full": "pass", "short": "pass"}), "exactly")
        self.assertRefused(self.add(checker={"full": "pass", "short": "fail", "date": None}), "checker date")
        self.assertRefused(self.add(checker={"full": "pass", "short": "pass", "date": "2026-02-30"}), "not a real date")
        self.assertRefused(self.add(checker={"full": "pass", "short": "pass", "date": "20/10/2026"}), "YYYY-MM-DD")
        self.assertRefused(self.add(checker={"full": "pass", "short": "pass", "date": TOMORROW}), "in the future")
        self.assertAccepted(self.add(checker={"full": "pass", "short": "fail", "date": TODAY.isoformat()}))
        self.assertAccepted(self.add(languages=["lua"], checker={"full": "pass", "short": "not-recorded",
                                                                 "date": YESTERDAY}))
        self.assertRefused(self.add(checker={"full": "not-recorded", "short": "not-recorded", "date": YESTERDAY}),
                           "must be null")
        self.assertRefused(self.add(listed=NEXT_YEAR), "listed %s is in the future" % NEXT_YEAR)

    def test_offline_and_in_game_cannot_be_upgraded(self):
        self.assertRefused(self.add(offline=False), "offline must be true")
        self.assertRefused(self.add(offline="yes"), "offline must be true")
        self.assertRefused(self.add(in_game="verified"), "never records in-game verification")
        self.assertRefused(self.add(in_game=["not-run"]), "never records in-game verification")
        self.assertRefused(self.add(in_game={"state": "not-run"}), "never records in-game verification")
        self.assertRefused(lambda d: d["first_party"][1].update(in_game="verified in game (2026-10-21)"),
                           "never records in-game verification")

    def test_text_cells_stay_single_line_table_safe(self):
        self.assertRefused(self.add(description="a | b"), "description")
        self.assertRefused(self.add(description="x" * 201), "description")
        self.assertRefused(self.add(name="two\nlines"), "name must be")
        self.assertRefused(self.add(author="[link](x)"), "author must be")
        self.assertRefused(self.add(note=""), "note must be")
        for text in (pd.END, pd.START, "<img src=x onerror=alert(1)>", "[click](javascript:alert(1))",
                     "a `code` span", "a > b"):
            self.assertRefused(self.add(description=text), "description must be")
            self.assertRefused(self.add(note=text), "note must be")
            self.assertRefused(self.add(author=text), "author must be")
            self.assertRefused(self.add(name=text), "name must be")
        self.assertAccepted(self.add(description="Plain text (with parentheses), commas & 'quotes'."))
        self.assertRefused(self.add(stars=5), "unknown keys ['stars']")
        self.assertRefused(lambda d: d.update(community={}), "community must be a list")

    def test_drift_between_index_and_markdown_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for relative in (pd.INDEX, pd.PAGE, pd.VERSION_FILE, pd.GAME_PACK_HEADER):
                (root / relative).parent.mkdir(parents=True, exist_ok=True)
                shutil.copy(ROOT / relative, root / relative)
            self.assertEqual(quiet_main(["--check", "--root", str(root)]), 0)
            page = root / pd.PAGE
            original = page.read_text(encoding="utf-8")
            page.write_text(original.replace("| not run in game |", "| verified in game |"), encoding="utf-8")
            with self.assertRaisesRegex(pd.DirectoryError, "drifted"):
                pd.run(root, write=False)
            self.assertEqual(quiet_main(["--check", "--root", str(root)]), 1)
            self.assertEqual(quiet_main(["--write", "--root", str(root)]), 0)
            self.assertEqual(page.read_text(encoding="utf-8"), original)
            page.write_text(original.replace(pd.END, ""), encoding="utf-8")
            with self.assertRaisesRegex(pd.DirectoryError, "exactly one"):
                pd.run(root, write=True)
            (root / pd.INDEX).write_text("{not json", encoding="utf-8")
            with self.assertRaisesRegex(pd.DirectoryError, "unreadable"):
                pd.run(root, write=False)

    def test_invalid_index_is_refused_before_writing(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for relative in (pd.INDEX, pd.PAGE, pd.VERSION_FILE, pd.GAME_PACK_HEADER):
                (root / relative).parent.mkdir(parents=True, exist_ok=True)
                shutil.copy(ROOT / relative, root / relative)
            index = root / pd.INDEX
            index.write_text(index.read_text(encoding="utf-8").replace('"offline": true', '"offline": false', 1),
                             encoding="utf-8")
            before = (root / pd.PAGE).read_text(encoding="utf-8")
            self.assertEqual(quiet_main(["--write", "--root", str(root)]), 1)
            self.assertEqual((root / pd.PAGE).read_text(encoding="utf-8"), before)


if __name__ == "__main__":
    unittest.main()
