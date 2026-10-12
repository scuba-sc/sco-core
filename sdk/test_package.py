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
        runs = []
        for label, tz, locale, mask in (("a", "UTC", "C", "022"),
                                        ("b", "Pacific/Honolulu", "C.UTF-8", "077")):
            cwd = os.path.join(self.tmp, "cwd-" + label)
            os.mkdir(cwd)
            env = dict(os.environ, TZ=tz, LC_ALL=locale, PYTHONHASHSEED=label == "a" and "1" or "123")
            hashes = {}
            for prof in self.m["profiles"]:
                out = os.path.join(cwd, "dist", prof)
                wrapper = ("import os,runpy,sys; os.umask(int(sys.argv[1],8)); "
                           "sys.argv=sys.argv[2:]; runpy.run_path(sys.argv[0],run_name='__main__')")
                r = subprocess.run([sys.executable, "-c", wrapper, mask, os.path.join(HERE, "package.py"),
                                    "--profile", prof, "--out", out], cwd=cwd, env=env,
                                   capture_output=True, text=True)
                self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
                archive, = Path(out).glob("*.zip")
                hashes[prof] = sha(archive)
                with zipfile.ZipFile(archive) as z:
                    for info in z.infolist():
                        self.assertEqual(info.date_time, pkg.FIXED_TIME)
                        self.assertIn(info.external_attr >> 16, (0o100644, 0o100755))
                        data = z.read(info)
                        self.assertNotIn(os.fsencode(pkg.ROOT), data, info.filename)
                        self.assertNotIn(os.fsencode(cwd), data, info.filename)
            runs.append(hashes)
        self.assertEqual(runs[0], runs[1])
        for prof in self.m["profiles"]:
            print(f"double-build {prof}: {runs[0][prof]} {runs[1][prof]}")

    def test_packaged_repository_links_are_commit_pinned(self):
        commit = subprocess.check_output(["git", "-C", pkg.ROOT, "rev-parse", "HEAD"], text=True).strip()
        for prof in self.m["profiles"]:
            with zipfile.ZipFile(pkg.build(self.m, prof, os.path.join(self.tmp, "links"))) as z:
                count = 0
                for name in z.namelist():
                    if not name.endswith(".md"):
                        continue
                    text = z.read(name).decode()
                    refs = re.findall(r"https://github.com/scubamount/sco-core/(?:blob|tree)/([^/\s)]+)", text)
                    for ref in refs:
                        self.assertEqual(ref, commit, name)
                        count += 1
                self.assertGreater(count, 0, prof)

    def test_compatibility_descriptors(self):
        core = pkg.build(self.m, "core", os.path.join(self.tmp, "compat"))
        for prof in self.m["profiles"]:
            archive = pkg.build(self.m, prof, os.path.join(self.tmp, "compat"))
            with zipfile.ZipFile(archive) as z:
                name = next((n for n in z.namelist() if "/COMPATIBILITY.json" in n), None)
                self.assertIsNotNone(name, f"{prof}: missing compatibility descriptor")
                assert name is not None
                desc = json.loads(z.read(name))
            self.assertEqual(desc["schema_version"], 1)
            self.assertEqual(desc["profile"], prof)
            self.assertEqual(desc["core_sdk_version"], pkg.sdk_version())
            self.assertEqual(desc["plugin_abi_version"], pkg.abi_version())
            self.assertEqual(desc["source_commits"]["core"], subprocess.check_output(
                ["git", "-C", pkg.ROOT, "rev-parse", "HEAD"], text=True).strip())
            self.assertEqual(desc["game_overlay_version"], pkg.game_pack()[0]
                             if prof in ("game-sc", "legacy", "host-source-sc") else None)
            self.assertEqual(desc["base_core_sha256"], sha(core) if prof == "game-sc" else None)

    def test_overlay_rejects_different_core_before_build(self):
        out = os.path.join(self.tmp, "mismatch")
        overlay = pkg.build(self.m, "game-sc", out)
        with unittest.mock.patch.object(pkg, "source_commit", return_value="a" * 40):
            other_core = pkg.build(self.m, "core", out)
        r = subprocess.run(["bash", os.path.join(HERE, "test-zip.sh"), other_core, overlay],
                           capture_output=True, text=True)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("base core SHA-256 mismatch", r.stdout + r.stderr)
        self.assertNotIn("unpacked over", r.stdout + r.stderr)
        self.assertNotIn("-- The C", r.stdout + r.stderr)

    def test_per_artifact_notices_and_embedded_sbom(self):
        for prof in self.m["profiles"]:
            with zipfile.ZipFile(pkg.build(self.m, prof, os.path.join(self.tmp, "sbom"))) as z:
                files = {n.split("/", 1)[1]: z.read(n) for n in z.namelist()}
            suffix = ".game-sc" if prof == "game-sc" else ""
            inventory_name, sbom_name = "ATTRIBUTIONS.json" + suffix, "SBOM.spdx.json" + suffix
            self.assertTrue(inventory_name in files, f"{prof}: missing per-artifact attributions")
            self.assertTrue(sbom_name in files, f"{prof}: missing embedded SPDX SBOM")
            inventory = json.loads(files[inventory_name])
            self.assertEqual({f["path"] for f in inventory["files"]}, set(files))
            selected = {pkg.destination(self.m, prof, c): c for c in pkg.select(self.m, prof)}
            for record in inventory["files"]:
                self.assertTrue(record["copyright"])
                self.assertTrue(record["license"])
                if record["path"] in selected:
                    self.assertEqual(record["license"], selected[record["path"]]["license"])
            sbom = json.loads(files[sbom_name])
            self.assertEqual(sbom["spdxVersion"], "SPDX-2.3")
            self.assertEqual(sbom["dataLicense"], "CC0-1.0")
            self.assertEqual(sbom["SPDXID"], "SPDXRef-DOCUMENT")
            analyzed = {f["fileName"][2:]: f for f in sbom["files"]}
            exclusions = {sbom_name, "SHA256SUMS" + suffix}
            self.assertEqual(set(analyzed), set(files) - exclusions)
            for name, record in analyzed.items():
                self.assertEqual(record["checksums"][0], {"algorithm": "SHA256",
                                 "checksumValue": hashlib.sha256(files[name]).hexdigest()})
            pkg.validate_sbom(sbom, files, sbom_name, "SHA256SUMS" + suffix)

    def test_archive_audit_refuses_invalid_embedded_sbom(self):
        audit_spec = importlib.util.spec_from_file_location("ip_audit", os.path.join(pkg.ROOT, "tools/ip_audit.py"))
        assert audit_spec and audit_spec.loader
        audit = importlib.util.module_from_spec(audit_spec)
        audit_spec.loader.exec_module(audit)
        archive = pkg.build(self.m, "core", os.path.join(self.tmp, "invalid-sbom"))
        with zipfile.ZipFile(archive) as z:
            top = z.namelist()[0].split("/")[0]
            entries = {n.split("/", 1)[1]: (z.read(n), False) for n in z.namelist()}
        sbom = json.loads(entries["SBOM.spdx.json"][0])
        del sbom["creationInfo"]
        entries["SBOM.spdx.json"] = (pkg.json_bytes(sbom), False)
        entries["SHA256SUMS"] = ("".join(f"{hashlib.sha256(data).hexdigest()}  {name}\n"
                                        for name, (data, _) in sorted(entries.items()) if name != "SHA256SUMS").encode(), False)
        with open(archive, "wb") as f:
            f.write(pkg.archive_bytes(top, entries))
        report = audit.audit_zip(archive, "core")
        self.assertTrue(any("SPDX 2.3" in f for f in report.failures), report.failures)

    def test_archive_refuses_private_credentials_before_writing(self):
        fixtures = [b"-----BEGIN " + kind + b" PRIVATE KEY-----\nsynthetic\n"
                    for kind in (b"RSA", b"EC", b"OPENSSH")]
        fixtures += [b"-----BEGIN " + b"PRIVATE KEY-----\nsynthetic\n",
                     b"ghp" + b"_" + b"A" * 36,
                     b"github_pat" + b"_" + b"A" * 82]
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp, "out")
            for data in fixtures:
                with self.subTest(kind=data[:8]), unittest.mock.patch.object(
                        pkg, "archive_entries", return_value=("synthetic", {"hidden.txt": (data, False)})):
                    with self.assertRaisesRegex(pkg.ManifestError, "private credential.*hidden.txt") as cm:
                        pkg.build(self.m, "core", str(out))
                    self.assertNotIn(data.decode(), str(cm.exception))
                    self.assertFalse(out.exists())

    def test_archive_audit_refuses_false_attributions(self):
        audit_spec = importlib.util.spec_from_file_location("ip_audit", os.path.join(pkg.ROOT, "tools/ip_audit.py"))
        assert audit_spec and audit_spec.loader
        audit = importlib.util.module_from_spec(audit_spec)
        audit_spec.loader.exec_module(audit)
        top, original = pkg.archive_entries(self.m, "core")
        records = pkg.attributions(self.m, "core")
        good = json.loads(original["ATTRIBUTIONS.json"][0])
        for mutate in (lambda d: d.clear(), lambda d: d["files"].pop(),
                       lambda d: d["files"].append(d["files"][0]),
                       lambda d: d["files"][0].update(license="MIT")):
            bad = copy.deepcopy(good)
            mutate(bad)
            entries = dict(original)
            entries["ATTRIBUTIONS.json"] = (pkg.json_bytes(bad), False)
            analyzed = {n: entry for n, entry in entries.items() if n not in ("SBOM.spdx.json", "SHA256SUMS")}
            sbom = pkg.sbom_document(top, pkg.sdk_version(), analyzed, records, "SBOM.spdx.json", "SHA256SUMS", pkg.source_commit())
            entries["SBOM.spdx.json"] = (pkg.json_bytes(sbom), False)
            entries["SHA256SUMS"] = ("".join(f"{hashlib.sha256(data).hexdigest()}  {name}\n"
                                            for name, (data, _) in sorted(entries.items()) if name != "SHA256SUMS").encode(), False)
            archive = os.path.join(self.tmp, "false-notices.zip")
            with open(archive, "wb") as f:
                f.write(pkg.archive_bytes(top, entries))
            report = audit.audit_zip(archive, "core")
            self.assertTrue(any("attribution inventory" in f for f in report.failures), report.failures)
            self.assertFalse(any("embedded SPDX" in f or "SHA256SUMS (Gate 6)" in f for f in report.failures), report.failures)

    def test_sbom_validator_refuses_corruption(self):
        archive = pkg.build(self.m, "core", os.path.join(self.tmp, "sbom-negative"))
        with zipfile.ZipFile(archive) as z:
            files = {n.split("/", 1)[1]: z.read(n) for n in z.namelist()}
        good = json.loads(files["SBOM.spdx.json"])
        mutations = [lambda d: d.pop("spdxVersion"), lambda d: d.pop("creationInfo"),
                     lambda d: d["creationInfo"].update(creators=[]),
                     lambda d: d["packages"][0].pop("downloadLocation"),
                     lambda d: d["packages"][0].update(filesAnalyzed=False),
                     lambda d: d["files"][0].pop("licenseInfoInFiles"),
                     lambda d: d["files"].append(d["files"][0]),
                     lambda d: d["files"][0]["checksums"][0].update(checksumValue="0" * 64),
                     lambda d: d["files"].pop(), lambda d: d["relationships"].pop(),
                     lambda d: d["relationships"][0].update(relatedSpdxElement="SPDXRef-Unknown"),
                     lambda d: d["packages"][0]["packageVerificationCode"].update(packageVerificationCodeValue="0" * 40),
                     lambda d: d["packages"][0]["packageVerificationCode"].update(packageVerificationCodeExcludedFiles=[])]
        for mutate in mutations:
            bad = copy.deepcopy(good)
            mutate(bad)
            with self.assertRaisesRegex(pkg.ManifestError, "SPDX 2.3"):
                pkg.validate_sbom(bad, files, "SBOM.spdx.json", "SHA256SUMS")

    def test_archive_build_refuses_checkout_include(self):
        archive = pkg.build(self.m, "core", os.path.join(self.tmp, "include-leak"))
        env = dict(os.environ, CFLAGS="-I" + os.path.join(pkg.ROOT, "include"), SCO_SDK_NO_LUA="1")
        r = subprocess.run(["bash", os.path.join(HERE, "test-zip.sh"), archive],
                           capture_output=True, text=True, env=env)
        self.assertNotEqual(r.returncode, 0, "archive test silently used checkout include path")
        self.assertIn("archive-only include violation", r.stdout + r.stderr)

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

    def test_core_source_has_no_game_contracts(self):
        game = {c["source"] for c in self.m["components"]
                if c.get("group") == "game-contract" or
                ("game-sc" in c["profiles"] and c["source"] != "LICENSE")}
        # Profile membership classifies game files; no filename-pattern allowlist.
        selected = {c["source"] for c in pkg.select(self.m, "core-source")}
        self.assertFalse(selected & game, sorted(selected & game))
        for path in ("tests/abi_datacore.c", "tests/abi_game_actors.c",
                     "tests/abi_spatial.c", "tests/abi_spawn.c"):
            self.assertIn(path, game)
        with zipfile.ZipFile(pkg.build(self.m, "core-source", self.tmp)) as z:
            files = {n.split("/", 1)[1] for n in z.namelist()}
            self.assertFalse(files & game, sorted(files & game))

    def test_core_source_builds_from_extracted_root(self):
        """CI and local runs exercise every neutral CTest, with no checkout include paths."""
        with tempfile.TemporaryDirectory(prefix="core-source-") as tmp:
            archive = pkg.build(self.m, "core-source", os.path.join(tmp, "dist"))
            with zipfile.ZipFile(archive) as z:
                z.extractall(os.path.join(tmp, "empty"))
                root = Path(tmp, "empty", z.namelist()[0].split("/", 1)[0])
            game = {c["source"] for c in self.m["components"]
                    if c.get("group") == "game-contract" or
                    ("game-sc" in c["profiles"] and c["source"] != "LICENSE")}
            self.assertFalse(any((root / path).exists() for path in game))
            env = os.environ.copy()
            for key in ("CPATH", "CPLUS_INCLUDE_PATH", "C_INCLUDE_PATH", "LIBRARY_PATH",
                        "CFLAGS", "CXXFLAGS", "CPPFLAGS", "LDFLAGS", "CMAKE_PREFIX_PATH"):
                env.pop(key, None)
            self.assertFalse((root / ".git").exists())
            build = os.path.join(tmp, "build")
            for command in (["cmake", "-S", str(root), "-B", build, "-DSCO_GAME_SC=OFF",
                             "-DSCO_BUILD_TESTS=ON", "-DCMAKE_BUILD_TYPE=Debug",
                             "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"],
                            ["cmake", "--build", build, "--config", "Debug", "--parallel", "4"],
                            ["ctest", "--test-dir", build, "-C", "Debug", "--output-on-failure"]):
                result = subprocess.run(command, cwd=root, env=env, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(result.stdout)
            commands = Path(build, "compile_commands.json")
            if commands.exists():  # Make/Ninja; Visual Studio does not export this file.
                self.assertNotIn(pkg.ROOT, commands.read_text())
            refusal = subprocess.run(["cmake", "-S", str(root), "-B", os.path.join(tmp, "game"),
                                      "-DSCO_GAME_SC=ON"], cwd=root, env=env,
                                     capture_output=True, text=True)
            self.assertNotEqual(refusal.returncode, 0)
            self.assertIn("Configure with -DSCO_GAME_SC=OFF", refusal.stdout + refusal.stderr)

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

    def installed_fixture(self, profile, tmp):
        root = Path(tmp, "installed SDK")
        root.mkdir()
        for required in pkg.closure(self.m, profile):
            with zipfile.ZipFile(pkg.build(self.m, required, str(Path(tmp, "dist")))) as z:
                for name in z.namelist():
                    dest = root / name.split("/", 1)[1]
                    dest.parent.mkdir(parents=True, exist_ok=True)
                    dest.write_bytes(z.read(name))
        return root

    def test_external_example_headers_are_shipped(self):
        for profile in self.m["profiles"]:
            with self.subTest(profile=profile), tempfile.TemporaryDirectory() as tmp:
                root = self.installed_fixture(profile, tmp)
                proof = pkg.check_installed_example_headers(self.m, profile, root, Path(tmp, "proof"))
                self.assertEqual(proof["translation_units"], 4)  # template, hello, cpp_hello, widgets
                self.assertEqual(set(proof["languages"]), {"C", "CXX"})
                self.assertIn("include/sco_api.h", proof["public_headers"])
                self.assertIn("include/scosdk/scosdk.hpp", proof["public_headers"])
                self.assertIn("include/sco_widgets.h", proof["public_headers"])

    def test_windows_archive_verifier_checkout_has_manifest_inputs(self):
        """Replay the Windows job's checkout; verifier tools must exist outside the archive."""
        workflow = Path(pkg.ROOT, ".github/workflows/sdk.yml").read_text()
        windows = workflow.split("\n  windows:\n", 1)[1].split("\n  csharp-host-sim:\n", 1)[0]
        sparse = re.search(r"^          sparse-checkout: (.+)$", windows, re.M)
        with tempfile.TemporaryDirectory(prefix="archive-verifier-") as tmp:
            checkout = Path(tmp, "checkout")
            subprocess.run(["git", "clone", "--quiet", "--shared", "--no-checkout",
                            pkg.ROOT, str(checkout)], check=True)
            if sparse:
                subprocess.run(["git", "-C", str(checkout), "sparse-checkout", "set",
                                "--no-cone", sparse.group(1)], check=True)
            subprocess.run(["git", "-C", str(checkout), "checkout", "--quiet", "HEAD"], check=True)
            root = self.installed_fixture("core", tmp)
            # Use an unrelated cwd and a path containing spaces, like a native Git Bash invocation.
            result = subprocess.run([sys.executable, str(checkout / "sdk/package.py"),
                                     "--profile", "core", "--check-installed-headers", str(root)],
                                    cwd=tmp, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("exact installed public header dependencies approved", result.stdout)

    def test_installed_header_cli(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = self.installed_fixture("core", tmp)
            result = subprocess.run([sys.executable, str(Path(HERE, "package.py")),
                                     "--profile", "core", "--check-installed-headers", str(root)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("4 external C/C++ examples compiled", result.stdout)
            self.assertIn("exact installed public header dependencies approved", result.stdout)

    def test_msvc_system_headers_use_evaluated_include_path(self):
        """Visual Studio's toolchain paths need not exist in the invoking shell's INCLUDE."""
        import xml.etree.ElementTree as ET
        with tempfile.TemporaryDirectory() as tmp:
            root = self.installed_fixture("core", tmp)
            work = Path(tmp, "proof")
            pkg.check_installed_example_headers(self.m, "core", root, work)
            ns = {"ms": "http://schemas.microsoft.com/developer/msbuild/2003"}
            for lang in ("C", "CXX"):
                # Only parse XML generated by the verifier in this disposable workspace.
                props = ET.parse(work / f"system-{lang}.props")
                target = props.find("ms:Target", ns)
                assert target is not None
                self.assertEqual(target.get("BeforeTargets"), "ClCompile")
                write = target.find("ms:WriteLinesToFile", ns)
                assert write is not None
                self.assertEqual(write.get("Lines"), "$(IncludePath)")
                # The verifier may write the canonical path (macOS: /private/var vs /var).
                self.assertEqual(Path(write.attrib["File"]).resolve(),
                                 (work / "build" / f"system-{lang}.txt").resolve())
                self.assertEqual(write.get("Overwrite"), "true")
                cmake = (work / "CMakeLists.txt").read_text()
                self.assertIn("VS_USER_PROPS", cmake)
                self.assertIn(f"system-{lang}.props", cmake)

    def test_compiler_refuses_hidden_checkout_source_include(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = self.installed_fixture("core", tmp)
            example = root / "examples/hello/hello.c"
            example.write_text('#define HIDDEN_HEADER "' + Path(pkg.ROOT, "include/sco_api.h").as_posix() +
                               '"\n#include HIDDEN_HEADER\n' + example.read_text())
            with self.assertRaisesRegex(pkg.ManifestError, "resolved dependency.*" +
                                        re.escape(str(Path(pkg.ROOT, "include/sco_api.h"))) +
                                        " outside approved installed files"):
                pkg.check_installed_example_headers(self.m, "core", root, Path(tmp, "proof"))

    def test_zip_refuses_compiler_resolved_checkout_header(self):
        top, entries = pkg.archive_entries(self.m, "core")
        data, executable = entries["examples/hello/hello.c"]
        hidden = Path(pkg.ROOT, "include/sco_api.h").as_posix()
        entries["examples/hello/hello.c"] = (
            f'#define HIDDEN_HEADER "{hidden}"\n#include HIDDEN_HEADER\n'.encode() + data, executable)
        entries["SHA256SUMS"] = ("".join(f"{hashlib.sha256(data).hexdigest()}  {name}\n"
                                        for name, (data, _) in sorted(entries.items())
                                        if name != "SHA256SUMS").encode(), False)
        with tempfile.TemporaryDirectory() as tmp:
            archive = Path(tmp, "hidden-source.zip")
            archive.write_bytes(pkg.archive_bytes(top, entries))
            env = dict(os.environ, SCO_SDK_NO_LUA="1")
            for key in ("CPATH", "CPLUS_INCLUDE_PATH", "C_INCLUDE_PATH", "CFLAGS", "CXXFLAGS", "CPPFLAGS"):
                env.pop(key, None)
            result = subprocess.run(["bash", str(Path(HERE, "test-zip.sh")), str(archive)],
                                    env=env, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("native target includes resolve only inside extracted SDK", result.stdout)
            self.assertIn(f"resolved dependency {hidden} outside approved installed files", result.stderr)

    def test_compiler_refuses_unshipped_nested_public_header(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = self.installed_fixture("core", tmp)
            hidden = root / "include/scosdk/nested/unshipped.hpp"
            hidden.parent.mkdir()
            hidden.write_text("/* Valid but not an approved public manifest destination. */\n"
                              "#if defined(__GNUC__)\n#pragma GCC system_header\n#endif\n")
            public = root / "include/scosdk/scosdk.hpp"
            public.write_text(public.read_text() +
                              '\n#define HIDDEN_HEADER "scosdk/nested/unshipped.hpp"\n#include HIDDEN_HEADER\n')
            with self.assertRaisesRegex(pkg.ManifestError, "resolved dependency.*scosdk/nested/unshipped.hpp"):
                pkg.check_installed_example_headers(self.m, "core", root, Path(tmp, "proof"))

    def test_unshipped_nested_public_header_is_refused(self):
        with self.assertRaisesRegex(pkg.ManifestError, "scosdk/nested/unshipped.hpp"):
            pkg.check_example_headers(self.m, "core", '#include "scosdk/nested/unshipped.hpp"', "external.cpp")

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

    def test_source_refuses_game_contract_membership(self):
        for source in ("include/sc_actors.h", "tests/abi_game_actors.c"):
            m = copy.deepcopy(self.good)
            self.comp(m, source)["profiles"].append("core-source")
            with self.assertRaisesRegex(pkg.ManifestError, "contracts"):
                pkg.select(m, "core-source")

    def test_public_header_source_does_not_prove_destination(self):
        m = copy.deepcopy(self.good)
        self.comp(m, "include/scosdk/scosdk.hpp")["destination"] = "include/scosdk/renamed.hpp"
        with self.assertRaisesRegex(pkg.ManifestError, "unshipped public header"):
            pkg.check_example_headers(m, "core", '#include "scosdk/scosdk.hpp"', "external.cpp")

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
