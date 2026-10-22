#!/usr/bin/env python3
"""Narrow offline archive proof: python3 sdk/test_host_source.py -v.

Build only the host adapter targets and synthetic fixtures; no game runtime,
checkout includes, release workflow or full CTest suite is involved.
"""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import zipfile

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("package", HERE / "package.py")
assert spec and spec.loader
pkg = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pkg)
ADAPTER_FILES = {"include/sco/game/host.h", "src/game/host.cpp",
                 "src/game/host_testing.h", "tests/game_host_data.cpp",
                 "tests/game_host.cpp", "docs/game/host.md"}


class HostSourceArchive(unittest.TestCase):
    def test_supplied_archive_requires_host_inventory(self):
        m = pkg.load_manifest()
        with tempfile.TemporaryDirectory(prefix="host-source-wrong-profile-") as tmp:
            archive = pkg.build(m, "core-source", str(Path(tmp, "dist")))
            env = os.environ.copy()
            env["SCO_HOST_SOURCE_ARCHIVE"] = archive
            result = subprocess.run(
                [sys.executable, str(Path(__file__).resolve()),
                 "HostSourceArchive.test_standalone_game_source_archive"],
                env=env, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_adapter_manifest_requires_all_real_sources(self):
        script = HERE / "add_host_adapter_components.py"
        self.assertTrue(script.is_file(), "G1 manifest integration script is missing")
        if all(Path(pkg.ROOT, p).is_file() for p in ADAPTER_FILES):
            self.skipTest("Adapter already integrated; standalone archive verifies its private membership")
        before = (HERE / "components.json").read_bytes()
        result = subprocess.run([sys.executable, str(script)], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("missing real adapter files", result.stdout + result.stderr)
        self.assertEqual(before, (HERE / "components.json").read_bytes())

    def test_standalone_game_source_archive(self):
        m = pkg.load_manifest()
        self.assertIn("host-source-sc", m["profiles"], "G16 host source profile is missing")
        profile = m["profiles"]["host-source-sc"]
        self.assertEqual(profile["layout"], "source")
        self.assertEqual(profile["includes"], ["core-source"])
        self.assertFalse(profile.get("requires"), "host source must not require a plugin overlay")
        chosen = pkg.select(m, "host-source-sc")
        sources = {c["source"] for c in chosen}
        neutral = {c["source"] for c in pkg.select(m, "core-source")}
        tracked = set(pkg.tracked_files(pkg.ROOT))
        required = {p for p in tracked if p.startswith(("src/", "include/", "tests/",
                    "third_party/tomlplusplus/", "docs/game/"))}
        required.update(("plugins/lua/sco_lua_game.c", "plugins/lua/sco_lua_game.h",
                         "tools/sco-dcb.cpp", "tools/sco-sigcheck.cpp"))
        self.assertFalse(required - sources, f"missing tracked source dependencies: {sorted(required - sources)}")
        self.assertTrue(neutral <= sources)
        self.assertTrue(all(pkg.destination(m, "host-source-sc", c) == c["source"] for c in chosen))
        game = {c["source"] for c in chosen if c.get("group") in ("game-contract", "host-source-sc")
                or "game-sc" in c["profiles"]}
        self.assertFalse((game - {"LICENSE"}) & neutral)
        # Integrated G1 files must be private host-source components, never plugin headers.
        for path in ADAPTER_FILES & tracked:
            c, = [c for c in m["components"] if c["source"] == path]
            self.assertEqual(c["profiles"], ["host-source-sc"])
            self.assertFalse(c.get("public_header", False))
        env = os.environ.copy()
        for key in ("CPATH", "CPLUS_INCLUDE_PATH", "C_INCLUDE_PATH", "LIBRARY_PATH",
                    "CFLAGS", "CXXFLAGS", "CPPFLAGS", "LDFLAGS", "CMAKE_PREFIX_PATH"):
            env.pop(key, None)
        with tempfile.TemporaryDirectory(prefix="host-source-sc-") as tmp:
            archive = os.environ.get("SCO_HOST_SOURCE_ARCHIVE")
            if archive is None:
                archive = pkg.build(m, "host-source-sc", str(Path(tmp, "dist")))
            with zipfile.ZipFile(archive) as z:
                files = {n.split("/", 1)[1]: z.read(n) for n in z.namelist()}
                self.assertEqual(set(files), sources | set(pkg.metadata_names(m, "host-source-sc"))
                                 | {"sdk-catalogue.json", "sdk-guides.json"})
                self.assertEqual(json.loads(files["sdk-catalogue.json"])["source_sha"], pkg.source_commit())
                self.assertEqual(json.loads(files["sdk-guides.json"])["source_sha"], pkg.source_commit())
                sums = dict(line.split("  ", 1)[::-1] for line in files["SHA256SUMS"].decode().splitlines())
                self.assertEqual(set(sums), set(files) - {"SHA256SUMS"})
                for path, digest in sums.items():
                    self.assertEqual(hashlib.sha256(files[path]).hexdigest(), digest, path)
                pkg.validate_sbom(json.loads(files["SBOM.spdx.json"]), files,
                                  "SBOM.spdx.json", "SHA256SUMS")
                records = json.loads(files["ATTRIBUTIONS.json"])["files"]
                self.assertEqual({r["path"] for r in records}, set(files))
                self.assertFalse(any(r["status"] == "unresolved" for r in records))
                for c in chosen:
                    r, = [r for r in records if r["path"] == c["source"]]
                    self.assertEqual((r["origin"], r["license"]), (c["origin"], c["license"]))
                compatibility = json.loads(files["COMPATIBILITY.json"])
                self.assertEqual(compatibility["profile"], "host-source-sc")
                self.assertEqual(compatibility["game_overlay_version"], pkg.game_pack()[0])
                self.assertEqual(compatibility["source_commits"]["game-sc"], pkg.source_commit())
                self.assertIsNone(compatibility["base_core_sha256"])
                self.assertIn(b"-DSCO_GAME_SC=ON", files["VERSIONS.txt"])
                z.extractall(Path(tmp, "empty"))
                root = Path(tmp, "empty", z.namelist()[0].split("/", 1)[0]).resolve()
            self.assertFalse((root / ".git").exists())
            build = Path(tmp, "build").resolve()
            commands = [["cmake", "-S", str(root), "-B", str(build), "-DSCO_GAME_SC=ON",
                         "-DSCO_BUILD_TESTS=ON", "-DCMAKE_BUILD_TYPE=Debug",
                         "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"],
                        ["cmake", "--build", str(build), "--config", "Debug", "--parallel", "4",
                         "--target", "test_game_host", "test_game_host_data"],
                        ["ctest", "--test-dir", str(build), "-C", "Debug", "--output-on-failure",
                         "-R", "^game_host(_loss|_quit(_missing)?|_data)?$"]]
            for command in commands:
                result = subprocess.run(command, cwd=root, env=env, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("5/5", result.stdout)
            self.assertIn("SCO_GAME_SC:BOOL=ON", (build / "CMakeCache.txt").read_text())
            compile_commands = build / "compile_commands.json"
            if compile_commands.exists():
                self.assertNotIn(pkg.ROOT, compile_commands.read_text())
                units = json.loads(compile_commands.read_text())
                # CMake also emits ABI C++ pins/probes inside the fresh build root.
                self.assertTrue(all(Path(u["file"]).resolve().is_relative_to(root)
                                    or Path(u["file"]).resolve().is_relative_to(build) for u in units))
                self.assertTrue(any(Path(u["file"]) == root / "src/game/pak_hooks.cpp" for u in units))
            print(result.stdout)
            # The neutral archive physically excludes the same classified game inventory.
            with zipfile.ZipFile(pkg.build(m, "core-source", str(Path(tmp, "neutral")))) as z:
                held = {n.split("/", 1)[1] for n in z.namelist()}
                self.assertFalse(held & (game - {"LICENSE"}))
                self.assertFalse(any(p.startswith(("src/game/", "src/datacore/")) for p in held))


if __name__ == "__main__":
    unittest.main()
