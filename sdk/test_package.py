#!/usr/bin/env python3
"""Tests for sdk/package.py and sdk/components.json. Run: python3 sdk/test_package.py (CI's sdk job does).

1. The real manifest: valid, every profile builds twice to the same bytes, the legacy profile is the
   pre-split zip, core holds no game file, and every zip's SHA256SUMS matches its files.
2. Synthetic manifests that must be refused: each check in load_manifest / select fails closed.
"""
import copy
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
import unittest.mock
import zipfile

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("package", os.path.join(HERE, "package.py"))
assert spec and spec.loader
pkg = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pkg)

GAME_MARKERS = ("sc_actors", "sc_vehicles", "sc_world", "sc_entities", "sc_creative", "sc_spawn", "sc_spatial",
                "sc_game_events", "sc_game_pack", "sco_datacore", "scosdk/game/", "scosdk/datacore",
                "Sco.Sdk/Game/", "DataCore.cs", "quantum_pack", "travel_pack", "game-pack.md", "game-services.md",
                "docs/game/")


def sha(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


class RealManifest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.m = pkg.load_manifest()
        cls.tmp = tempfile.mkdtemp()

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp)

    def test_every_profile_reproducible(self):
        for prof in self.m["profiles"]:
            a = pkg.build(self.m, prof, os.path.join(self.tmp, "a"))
            b = pkg.build(self.m, prof, os.path.join(self.tmp, "b"))
            self.assertEqual(sha(a), sha(b), prof)

    def test_core_has_no_game_file(self):
        z = zipfile.ZipFile(pkg.build(self.m, "core", os.path.join(self.tmp, "c")))
        bad = [n for n in z.namelist() if any(k in n for k in GAME_MARKERS)]
        self.assertEqual(bad, [])
        versions = pkg.metadata_names(self.m, "core")[0]
        self.assertNotIn("game pack", z.read(next(n for n in z.namelist() if n.endswith(versions))).decode())

    def test_core_and_game_sc_partition_legacy(self):
        names = {}
        for prof in ("core", "game-sc", "legacy"):
            z = zipfile.ZipFile(pkg.build(self.m, prof, os.path.join(self.tmp, prof)))
            names[prof] = {n.split("/", 1)[1] for n in z.namelist()} - set(pkg.metadata_names(self.m, prof))
        self.assertEqual(names["core"] | names["game-sc"], names["legacy"])
        self.assertEqual(names["core"] & names["game-sc"], {"LICENSE"})

    def test_overlay_keeps_each_archives_own_metadata(self):
        """game-sc unpacks over core: its VERSIONS.txt and SHA256SUMS carry the profile name, so
        the overlay overwrites no file of the core (LICENSE is the one shared file, byte-identical)."""
        held = {}
        for prof in ("core", "game-sc"):
            z = zipfile.ZipFile(pkg.build(self.m, prof, os.path.join(self.tmp, "m" + prof)))
            held[prof] = {n.split("/", 1)[1]: z.read(n) for n in z.namelist() if not n.endswith("/")}
        self.assertIn("SHA256SUMS.game-sc", held["game-sc"])
        self.assertIn("VERSIONS.txt.game-sc", held["game-sc"])
        self.assertNotIn("SHA256SUMS.game-sc", held["core"])
        for name in set(held["core"]) & set(held["game-sc"]):
            self.assertEqual(held["core"][name], held["game-sc"][name], f"{name} differs between the zips")

    def test_sha256sums(self):
        for prof in self.m["profiles"]:
            z = zipfile.ZipFile(pkg.build(self.m, prof, os.path.join(self.tmp, "s" + prof)))
            top = z.namelist()[0].split("/")[0]
            sums_name = pkg.metadata_names(self.m, prof)[1]
            listed = {}
            for line in z.read(f"{top}/{sums_name}").decode().splitlines():
                h, name = line.split("  ", 1)
                listed[name] = h
            files = {n.split("/", 1)[1] for n in z.namelist()} - {sums_name}
            self.assertEqual(set(listed), files, prof)
            for name, h in listed.items():
                self.assertEqual(hashlib.sha256(z.read(f"{top}/{name}")).hexdigest(), h, f"{prof}: {name}")

    def test_core_source_closure(self):
        selected = pkg.select(self.m, "core-source")
        sources = {c["source"] for c in selected}
        for path in ("CMakeLists.txt", "src/host/sco_host.cpp", "src/app/sco_app.cpp",
                     "src/api/sco_tasks.cpp", "plugins/lua/sco_lua.c",
                     "third_party/sqlite/sqlite3.c", "tests/test_host.cpp",
                     "tools/abi_snapshot.py", "tests/abi/released/sco_api.h"):
            self.assertIn(path, sources)
        self.assertFalse(any(s.startswith(("src/game/", "src/datacore/")) for s in sources))
        self.assertTrue(all(pkg.destination(self.m, "core-source", c) == c["source"] for c in selected))
        # Metadata must not depend on the omitted game-pack header/version.
        with unittest.mock.patch.object(pkg, "game_pack", side_effect=AssertionError("game version read")):
            self.assertIn("-DSCO_GAME_SC=OFF", pkg.versions_txt("core-source", "1.2.0"))

    def test_contract_catalogue_inventory(self):
        methods = set()
        for header in sorted(Path(pkg.ROOT, "include").glob("*.h")):
            text = re.sub(r"/\*.*?\*/|//[^\n]*", "", header.read_text(), flags=re.S)
            for body, table in re.findall(r"typedef\s+struct\s+\w+\s*\{(.*?)\}\s*(\w+)\s*;", text, re.S):
                methods.update(f"{table}.{name}" for name in re.findall(r"\(\s*\*\s*(\w+)\s*\)\s*\(", body))
        methods.update(("sco_plugin_query", "sco_plugin_load", "sco_plugin_unload"))
        with open(os.path.join(pkg.ROOT, "docs", "contracts.md")) as f:
            catalogue = f.read()
        rows = re.findall(r"^\| `(\w+(?:\.\w+)?)` \| (.*)$", catalogue, re.M)
        found = [name for name, cells in rows if name in methods]
        self.assertEqual(set(found), methods)
        self.assertEqual(len(found), len(methods), "duplicate method rows")
        for name, cells in rows:
            if name in methods:
                # Six required facets beyond method name, none empty.
                facets = cells.rstrip().rstrip("|").split("|")
                self.assertEqual(len(facets), 6, name)
                self.assertTrue(all(cell.strip() for cell in facets), name)

    def test_public_headers(self):
        core = pkg.public_headers(self.m, ["core"])
        both = pkg.public_headers(self.m, ["core", "game-sc"])
        self.assertIn("include/sco_api.h", core)
        self.assertNotIn("include/sc_actors.h", core)
        self.assertIn("include/sc_actors.h", both)


class Refused(unittest.TestCase):
    """A broken manifest stops packaging with a message naming the problem."""

    @classmethod
    def setUpClass(cls):
        with open(pkg.MANIFEST, encoding="utf-8") as f:
            cls.good = json.load(f)

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.path = os.path.join(self.tmp.name, "components.json")

    def tearDown(self):
        self.tmp.cleanup()

    def refused(self, mutate, why, root=pkg.ROOT):
        m = copy.deepcopy(self.good)
        mutate(m)
        with open(self.path, "w", encoding="utf-8") as f:
            f.write(m if isinstance(m, str) else json.dumps(m))
        with self.assertRaises(pkg.ManifestError) as cm:
            pkg.load_manifest(self.path, root)
        self.assertIn(why, str(cm.exception))

    def comp(self, m, src):
        return next(c for c in m["components"] if c["source"] == src)

    def test_source_refuses_game_implementation(self):
        m = copy.deepcopy(self.good)
        m["components"].append({"id": "game.source", "source": "src/game/rows.cpp",
                                "destination": "src/game/rows.cpp", "profiles": ["core-source"],
                                "origin": "project-authored", "license": "GPL-3.0-only"})
        with self.assertRaisesRegex(pkg.ManifestError, "game implementation"):
            pkg.select(m, "core-source")

    def test_unknown_layout(self):
        self.refused(lambda m: m["profiles"]["core-source"].update(layout="unknown"), "unknown layout")

    def test_wrong_schema(self):
        self.refused(lambda m: m.update(schema_version=2), "schema_version must be 1")

    def test_malformed(self):
        with open(self.path, "w", encoding="utf-8") as f:
            f.write("{oops")
        with self.assertRaises(pkg.ManifestError):
            pkg.load_manifest(self.path)

    def test_unknown_origin(self):
        self.refused(lambda m: self.comp(m, "include/sco_api.h").update(origin="found-online"), "origin 'found-online'")

    def test_missing_license(self):
        self.refused(lambda m: self.comp(m, "include/sco_api.h").pop("license"), "missing license")

    def test_duplicate_id(self):
        self.refused(lambda m: self.comp(m, "include/sco_ui.h").update(id=self.comp(m, "include/sco_api.h")["id"]),
                     "id used twice")

    def test_destination_collision(self):
        self.refused(lambda m: self.comp(m, "include/sco_ui.h").update(destination="include/sco_api.h"), "same destination")

    def test_unsafe_destination(self):
        self.refused(lambda m: self.comp(m, "include/sco_ui.h").update(destination="../evil.h"), "not a safe relative path")

    def test_reserved_destination(self):
        self.refused(lambda m: self.comp(m, "include/sco_ui.h").update(destination="SHA256SUMS"), "written by package.py")

    def test_missing_source(self):
        self.refused(lambda m: self.comp(m, "include/sco_ui.h").update(source="include/nope.h"), "not a regular file")

    def test_unknown_profile(self):
        self.refused(lambda m: self.comp(m, "include/sco_ui.h").update(profiles=["core", "mystery"]), "declared profiles")

    def test_unknown_include(self):
        self.refused(lambda m: m["profiles"]["legacy"].update(includes=["core", "nope"]), "unknown profile(s) nope")

    def test_both_core_and_game(self):
        self.refused(lambda m: self.comp(m, "include/sco_ui.h").update(profiles=["core", "game-sc"]), "in both core and game-sc")

    def test_unclassified_file(self):
        self.refused(lambda m: m["components"].remove(self.comp(m, "include/sco_ui.h")),
                     "not classified in sdk/components.json: include/sco_ui.h")

    def test_game_header_in_core(self):
        m = copy.deepcopy(self.good)
        self.comp(m, "include/sc_actors.h")["profiles"] = ["core"]
        self.comp(m, "include/sc_actors.h")["public_header"] = True
        # load_manifest accepts it (core-only is a valid profile list); select must not.
        m["components"].append({"id": "x.game", "source": "include/sc_world.h", "destination": "include/sc_world2.h",
                                "profiles": ["core", "game-sc"], "origin": "project-authored", "license": "GPL-3.0-only"})
        with self.assertRaises(pkg.ManifestError):
            pkg.select(m, "core")

    def test_symlink_source(self):
        with tempfile.TemporaryDirectory() as root:
            os.makedirs(os.path.join(root, "include"))
            with open(os.path.join(root, "include", "real.h"), "w") as f:
                f.write("")
            os.symlink("real.h", os.path.join(root, "include", "link.h"))
            m = {"schema_version": 1, "profiles": {"core": {"zip": "a", "version_from": "x"}, "game-sc": {"zip": "b", "version_from": "y"}},
                 "covers": [], "components": [{"id": "l", "source": "include/link.h", "destination": "include/link.h",
                                                "profiles": ["core"], "origin": "project-authored", "license": "GPL-3.0-only"}]}
            with open(self.path, "w") as f:
                json.dump(m, f)
            with self.assertRaises(pkg.ManifestError) as cm:
                pkg.load_manifest(self.path, root)
            self.assertIn("not a regular file", str(cm.exception))

    def test_unknown_component_key(self):
        self.refused(lambda m: self.comp(m, "include/sco_api.h").update(colour="blue"), "unknown key(s) colour")

    def test_destination_collision_across_profiles(self):
        """legacy holds core and game-sc: two components of different profiles may not share a
        destination, or one silently wins in the legacy zip (and where the extension unpacks)."""
        self.refused(lambda m: self.comp(m, "include/sco_ui.h").update(destination="include/sc_actors.h"),
                     "both write include/sc_actors.h")

    def test_profile_cycle(self):
        self.refused(lambda m: m["profiles"]["legacy"].update(includes=["core", "game-sc"]) or
                     m["profiles"]["core"].update(requires=["legacy"]), "cycle")

    def test_requires_unknown_profile(self):
        self.refused(lambda m: m["profiles"]["game-sc"].update(requires="nope"), "names unknown profile")


class Cli(unittest.TestCase):
    def test_check_and_list(self):
        r = subprocess.run([sys.executable, os.path.join(HERE, "package.py"), "--check"], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        r = subprocess.run([sys.executable, os.path.join(HERE, "package.py"), "--list-headers", "core"], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0)
        self.assertIn("include/sco_api.h", r.stdout.split())
        self.assertNotIn("include/sc_actors.h", r.stdout.split())

    def test_unknown_profile_exits_nonzero(self):
        with tempfile.TemporaryDirectory() as out:
            r = subprocess.run([sys.executable, os.path.join(HERE, "package.py"), "--profile", "nope", "--out", out],
                               capture_output=True, text=True)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("unknown profile nope", r.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=1)
