import contextlib
import copy
import importlib.util
import io
import shutil
import tempfile
import unittest
from pathlib import Path

import status_registry as sr

ROOT = Path(__file__).resolve().parent.parent


def load_mcp_server():
    spec = importlib.util.spec_from_file_location("sco_core_mcp_server", ROOT / "tools/sco-core-mcp/server.py")
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def quiet_main(args):
    with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
        return sr.main(args)


class StatusRegistryTest(unittest.TestCase):
    registry = sr.load(ROOT)

    def entry(self, registry, entry_id):
        return next(e for e in registry["services"] if e["id"] == entry_id)

    def errors_after(self, mutate):
        registry = copy.deepcopy(self.registry)
        mutate(registry)
        return sr.validate(registry, ROOT)

    def assertRefused(self, mutate, fragment):
        errors = self.errors_after(mutate)
        self.assertTrue(any(fragment in e for e in errors), errors)

    def test_repository_registry_is_valid_and_rendered(self):
        self.assertEqual(sr.validate(self.registry, ROOT), [])
        self.assertIn("table matches", sr.run(ROOT, write=False))

    def test_every_header_service_has_a_row(self):
        services = {e["service"] for e in self.registry["services"]}
        self.assertLessEqual(set(sr.header_services(ROOT)), services)
        self.assertTrue(all(e["capabilities"] for e in self.registry["services"]))

    def test_bad_enums_are_refused(self):
        self.assertRefused(lambda r: self.entry(r, "game.creative")["capabilities"][0].update(default="MAYBE"), "default")
        self.assertRefused(lambda r: self.entry(r, "game.loadout")["capabilities"][0].update(evidence_level="works"),
                           "evidence_level")
        self.assertRefused(lambda r: self.entry(r, "game.world").update(kind="table"), "kind")

    def test_unknown_sig_row_is_refused(self):
        self.assertRefused(lambda r: self.entry(r, "game.creative")["capabilities"][0]["sig_rows"].append("spawn.no_such_row"),
                           "spawn.no_such_row")

    def test_missing_service_is_refused(self):
        self.assertRefused(lambda r: r["services"].remove(self.entry(r, "game.creative")),
                           "no registry entry for service game.creative")

    def test_header_version_mismatch_is_refused(self):
        self.assertRefused(lambda r: self.entry(r, "game.actors").update(version="1.0"), "header include/sc_actors.h says 1.1")

    def test_evidence_cannot_be_upgraded(self):
        def claim_in_game(r):
            self.entry(r, "game.loadout")["capabilities"][0].update(
                evidence="verified in game (2026-10-14)", evidence_level="synthetic_only")
        self.assertRefused(claim_in_game, "in_game_verified")

        def claim_sigcheck(r):
            self.entry(r, "game.quantum")["capabilities"][0].update(evidence="row resolved on 4.10.196.36804 (sigcheck)")
        self.assertRefused(claim_sigcheck, "sigcheck_resolved")
        self.assertRefused(lambda r: self.entry(r, "game.actors")["capabilities"][4].update(default="ON"), "off_pending_probe")

    def test_stability_labels_follow_the_owner_rule(self):
        labels = {e["id"]: e["stability"] for e in self.registry["services"]}
        self.assertEqual(sorted(set(labels.values())), ["experimental", "stable"])
        for entry in self.registry["services"]:
            with self.subTest(entry=entry["id"]):
                qualifying = any(cap["evidence_level"] in sr.STABLE_LEVELS for cap in entry["capabilities"])
                self.assertEqual(entry["stability"], "stable" if qualifying else "experimental")
                self.assertEqual(sr.rule_stability(entry["capabilities"]), entry["stability"])
        self.assertEqual(sr.STABLE_LEVELS, {"sigcheck_resolved", "off_pending_probe", "in_game_verified"})
        self.assertEqual((labels["game.creative"], labels["game.hangar"], labels["game.loadout"]),
                         ("stable", "stable", "experimental"))

    def test_bad_or_missing_stability_is_refused(self):
        self.assertRefused(lambda r: self.entry(r, "game.creative").update(stability="beta"), "stability 'beta' not in")
        self.assertRefused(lambda r: self.entry(r, "game.creative").update(stability="Stable"), "stability 'Stable' not in")
        self.assertRefused(lambda r: self.entry(r, "game.creative").update(stability=["stable"]), "stability ['stable'] not in")
        self.assertRefused(lambda r: self.entry(r, "game.creative").pop("stability"), "game.creative: stability missing")
        self.assertRefused(lambda r: self.entry(r, "game.mining.rows").pop("stability"), "game.mining.rows: stability missing")

    def test_stable_without_qualifying_evidence_is_refused(self):
        self.assertRefused(lambda r: self.entry(r, "game.loadout").update(stability="stable"),
                           "game.loadout: stability \"stable\" needs a capability with evidence_level")

        def demote_evidence(r):
            entry = self.entry(r, "game.actor_stats_write")
            entry["capabilities"][0].update(evidence_level="synthetic_only", default="OFF",
                                            evidence="Synthetic fixtures only; not run in game.")
        self.assertRefused(demote_evidence, "game.actor_stats_write: stability \"stable\" needs")
        self.assertRefused(lambda r: self.entry(r, "game.creative").update(stability="experimental"), "the rule says stable")

        def promote(r):
            entry = self.entry(r, "game.loadout")
            entry["stability"] = "stable"
            entry["capabilities"][0].update(evidence_level="off_pending_probe", default="OFF",
                                            evidence=sr.OFF_PENDING)
        self.assertNotIn("game.loadout", " ".join(e for e in self.errors_after(promote) if "stability" in e))

    def test_rendered_table_has_a_stability_column(self):
        table = sr.render(self.registry).splitlines()
        self.assertEqual(table[:2], ["| Table | Version | Stability | Capability | Default | Evidence |",
                                     "|---|---|---|---|---|---|"])
        self.assertTrue(all(len(line.split("|")) == 8 for line in table))
        first = {line.split("|")[1].strip(): line.split("|")[3].strip() for line in table[2:] if line.split("|")[1].strip()}
        self.assertEqual(first["`game.creative` ([`sc_creative.h`](../../include/sc_creative.h))"], "Stable")
        self.assertEqual(first["`game.loadout` ([`sc_loadout.h`](../../include/sc_loadout.h))"], "Experimental")
        self.assertEqual(sorted(set(first.values())), ["Experimental", "Stable"])
        self.assertEqual(len(first), len(self.registry["services"]))
        self.assertTrue(all(line.split("|")[3].strip() == "" for line in table[2:] if not line.split("|")[1].strip()))

    def test_drift_between_registry_and_markdown_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for part in ("include", "docs"):
                shutil.copytree(ROOT / part, root / part)
            (root / "src/game").mkdir(parents=True)
            for sigs in (ROOT / "src/game").glob("*_sigs.cpp"):
                shutil.copy(sigs, root / "src/game" / sigs.name)
            (root / "sdk").mkdir()
            shutil.copy(ROOT / sr.REGISTRY, root / sr.REGISTRY)
            self.assertEqual(quiet_main(["--check", "--root", str(root)]), 0)
            status = root / sr.STATUS
            original = status.read_text(encoding="utf-8")
            status.write_text(original.replace("| `game.creative.ammo` (`set_infinite_ammo`) | ON |",
                                               "| `game.creative.ammo` (`set_infinite_ammo`) | OFF |"), encoding="utf-8")
            with self.assertRaisesRegex(sr.RegistryError, "drifted"):
                sr.run(root, write=False)
            self.assertEqual(quiet_main(["--check", "--root", str(root)]), 1)
            self.assertEqual(quiet_main(["--write", "--root", str(root)]), 0)
            self.assertEqual(status.read_text(encoding="utf-8"), original)
            status.write_text(original.replace(sr.END, ""), encoding="utf-8")
            with self.assertRaisesRegex(sr.RegistryError, "exactly one"):
                sr.run(root, write=True)

    def test_rendered_table_parses_as_the_mcp_server_expects(self):
        server = load_mcp_server()
        tables = {}
        for entry in self.registry["services"]:
            if entry["kind"] == "service":
                major, minor = (int(x) for x in entry["version"].split("."))
                tables[entry["id"]] = {"service": entry["service"], "source_header": entry["header"],
                                       "version": (major << 16) | minor}
        groups = server.parse_game_status(sr.render(self.registry), tables)
        self.assertEqual(set(groups), set(tables))
        for entry in self.registry["services"]:
            if entry["kind"] != "service":
                continue
            group = groups[entry["id"]]
            self.assertTrue(group["version_matches_catalogue"], entry["id"])
            self.assertEqual(group["stability"], entry["stability"], entry["id"])
            self.assertEqual(len(group["rows"]), len(entry["capabilities"]), entry["id"])
        creative = groups["game.creative"]["rows"]
        self.assertEqual([row["evidence_level"] for row in creative], ["row_resolved_sigcheck"] * 4)
        status_text = (ROOT / sr.STATUS).read_text(encoding="utf-8")
        self.assertEqual(server.parse_game_status(status_text, tables), groups)


if __name__ == "__main__":
    unittest.main()
