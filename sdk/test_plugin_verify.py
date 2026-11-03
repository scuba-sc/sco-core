#!/usr/bin/env python3
"""Synthetic tests for the read-only SDK plugin archive verifier.

    python3 sdk/test_plugin_verify.py --checker build/sco-plugin-check

The checker is built from the SDK source. All plugin payloads below are inert test bytes; the
verifier must only hash them and must not extract or execute them.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import warnings
import zipfile


HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("sco_plugin_verify", HERE / "tools" / "sco-plugin-verify.py")
assert spec and spec.loader
verifier = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verifier)

parser = argparse.ArgumentParser()
parser.add_argument("--checker", required=True)
parser.add_argument("--packaged-sdk", help="extracted SDK root; also exercise its packaged verifier")
ARGS = parser.parse_args()
CHECKER = str(Path(ARGS.checker).resolve())
PACKAGED_SDK = Path(ARGS.packaged_sdk).resolve() if ARGS.packaged_sdk else None
sys.argv = [sys.argv[0]]


def write_archive(path, entries, compression=zipfile.ZIP_STORED):
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", UserWarning)
        with zipfile.ZipFile(path, "w", compression=compression, allowZip64=False) as archive:
            for entry in entries:
                if isinstance(entry, tuple) and isinstance(entry[0], zipfile.ZipInfo):
                    archive.writestr(entry[0], entry[1])
                else:
                    name, content = entry
                    archive.writestr(name, content)


def native_manifest(entry="pilot.dll", api="99.7", requires="sco.storage,teleport"):
    return (f"id = pilot\nname = Pilot\nversion = 1.0\napi = {api}\nkind = native\n"
            f"entry = {entry}\nrequires = {requires}\n").encode()


class PluginArchiveVerifierTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.archive_path = self.root / "pilot.zip"

    def tearDown(self):
        self.temp.cleanup()

    def verify(self, path=None):
        return verifier.verify_archive(path or self.archive_path, CHECKER)

    def test_valid_plugin_reports_hashes_dependencies_and_declared_api_without_running_entry(self):
        marker = self.root / "executed.txt"
        payload = f"#!/bin/sh\nprintf executed > '{marker}'\n".encode()
        write_archive(self.archive_path, [
            ("pilot/", b""),
            ("pilot/plugin.ini", native_manifest()),
            ("pilot/pilot.dll", payload),
            ("pilot/data/info.txt", b"fixture bytes\n"),
        ])

        result = self.verify()

        self.assertEqual(result["status"], "archive-structure-and-manifest-verified")
        self.assertEqual(result["plugin"]["id"], "pilot")
        self.assertEqual(result["plugin"]["declared_api"], "99.7")
        self.assertEqual(result["plugin"]["requires"], ["sco.storage", "teleport"])
        self.assertEqual(result["archive"]["sha256"], hashlib.sha256(self.archive_path.read_bytes()).hexdigest())
        artifacts = {artifact["path"]: artifact for artifact in result["artifacts"]}
        self.assertEqual(artifacts["pilot/pilot.dll"]["sha256"], hashlib.sha256(payload).hexdigest())
        self.assertEqual(artifacts["pilot/plugin.ini"]["sha256"], hashlib.sha256(native_manifest()).hexdigest())
        self.assertFalse(marker.exists(), "entry payload must never run")

    def test_manifest_parser_accepts_settings_and_reports_unresolved_dependency_names(self):
        manifest = (b"id = pilot\nname = Pilot\nversion = 1.0\napi = 1.0\nkind = native\n"
                    b"entry = pilot.dll\nrequires = future.service\n[settings]\nenabled = bool default true\n")
        write_archive(self.archive_path, [("pilot/plugin.ini", manifest), ("pilot/pilot.dll", b"opaque")])
        result = self.verify()
        self.assertEqual(result["plugin"]["requires"], ["future.service"])

    def test_manifest_id_must_match_single_archive_folder(self):
        manifest = native_manifest().replace(b"id = pilot", b"id = other")
        write_archive(self.archive_path, [("pilot/plugin.ini", manifest), ("pilot/pilot.dll", b"opaque")])
        with self.assertRaisesRegex(verifier.VerifyError, "manifest parser|does not match"):
            self.verify()

    def test_declared_entry_must_be_present_as_a_regular_file(self):
        write_archive(self.archive_path, [("pilot/plugin.ini", native_manifest()), ("pilot/other.dll", b"opaque")])
        with self.assertRaisesRegex(verifier.VerifyError, "declared entry"):
            self.verify()

    def test_declared_entry_match_is_case_sensitive_even_on_windows(self):
        write_archive(self.archive_path, [
            ("pilot/plugin.ini", native_manifest(entry="PILOT.DLL")),
            ("pilot/pilot.dll", b"opaque"),
        ])
        with self.assertRaisesRegex(verifier.VerifyError, "declared entry"):
            self.verify()

    def test_data_pack_has_no_declared_entry_requirement(self):
        manifest = b"id = pilot\nname = Pilot\nversion = 1.0\napi = 500.1\nkind = data\nrequires = teleport\n"
        write_archive(self.archive_path, [("pilot/plugin.ini", manifest), ("pilot/lists/places.txt", b"Lorville\n")])
        result = self.verify()
        self.assertIsNone(result["plugin"]["entry"])
        self.assertEqual(result["plugin"]["requires"], ["teleport"])

    def test_json_cli_emits_machine_readable_hashes_and_dependencies(self):
        write_archive(self.archive_path, [
            ("pilot/plugin.ini", native_manifest()),
            ("pilot/pilot.dll", b"opaque"),
        ])
        completed = subprocess.run(
            [sys.executable, str(HERE / "tools" / "sco-plugin-verify.py"), str(self.archive_path),
             "--checker", CHECKER, "--json"],
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
        result = json.loads(completed.stdout)
        self.assertEqual(result["plugin"]["requires"], ["sco.storage", "teleport"])
        self.assertIn("sha256", result["archive"])
        self.assertEqual({item["path"] for item in result["artifacts"]},
                         {"pilot/plugin.ini", "pilot/pilot.dll"})

    def test_rejects_unsafe_paths(self):
        for bad_path in ("../escape", "pilot/../../escape", "/pilot/plugin.ini", "pilot\\escape", "C:/escape",
                         "pilot/bad?.txt", "pilot/bad*.txt", 'pilot/bad".txt', "pilot/bad<.txt",
                         "pilot/bad>.txt", "pilot/bad|.txt", "pilot/CONIN$.txt", "pilot/CONOUT$"):
            with self.subTest(path=bad_path):
                write_archive(self.archive_path, [(bad_path, b"x")])
                with self.assertRaisesRegex(verifier.VerifyError,
                                            "unsafe archive path|portable to Windows|reserved Windows device"):
                    self.verify()

    def test_rejects_nul_truncated_zip_member_name(self):
        manifest = b"id = pilot\nname = Pilot\nversion = 1.0\napi = 1.0\nkind = data\n"
        write_archive(self.archive_path, [("pilot/plugin.ini", manifest), ("pilot/asset.txt", b"opaque")])
        archive_bytes = self.archive_path.read_bytes()
        self.assertEqual(archive_bytes.count(b"pilot/asset.txt"), 2)
        self.archive_path.write_bytes(archive_bytes.replace(b"pilot/asset.txt", b"pilot/asset\x00txt"))
        with self.assertRaisesRegex(verifier.VerifyError, "NUL|unsafe archive path"):
            self.verify()

    def test_rejects_duplicate_and_case_colliding_paths(self):
        write_archive(self.archive_path, [
            ("pilot/plugin.ini", native_manifest()),
            ("pilot/plugin.ini", native_manifest()),
            ("pilot/pilot.dll", b"opaque"),
        ])
        with self.assertRaisesRegex(verifier.VerifyError, "duplicate"):
            self.verify()

        write_archive(self.archive_path, [
            ("pilot/plugin.ini", native_manifest()),
            ("Pilot/pilot.dll", b"opaque"),
        ])
        with self.assertRaisesRegex(verifier.VerifyError, "collision"):
            self.verify()

        write_archive(self.archive_path, [
            ("pilot/plugin.ini", native_manifest()),
            ("pilot/data/é.txt", b"one"),
            ("pilot/data/e\u0301.txt", b"two"),
        ])
        with self.assertRaisesRegex(verifier.VerifyError, "collision"):
            self.verify()

        write_archive(self.archive_path, [
            ("pilot/plugin.ini", native_manifest()),
            ("pilot/data/K.txt", b"one"),
            ("pilot/data/K.txt", b"two"),
        ])
        with self.assertRaisesRegex(verifier.VerifyError, "collision"):
            self.verify()

    def test_rejects_symlinks_and_special_files(self):
        link = zipfile.ZipInfo("pilot/entry-link")
        link.create_system = 3
        link.external_attr = (stat.S_IFLNK | 0o777) << 16
        write_archive(self.archive_path, [("pilot/plugin.ini", native_manifest(entry="entry-link")), (link, b"pilot.dll")])
        with self.assertRaisesRegex(verifier.VerifyError, "symlink"):
            self.verify()

    def test_rejects_multi_root_archives_and_file_parent_conflicts(self):
        write_archive(self.archive_path, [
            ("pilot/plugin.ini", native_manifest()), ("pilot/pilot.dll", b"opaque"), ("extra/readme.txt", b"x")
        ])
        with self.assertRaisesRegex(verifier.VerifyError, "exactly one plugin folder"):
            self.verify()

        write_archive(self.archive_path, [
            ("pilot", b"file"), ("pilot/plugin.ini", native_manifest()), ("pilot/pilot.dll", b"opaque")
        ])
        with self.assertRaisesRegex(verifier.VerifyError, "file is used as a directory|both a file and directory"):
            self.verify()

    def test_rejects_compression_bombs_and_entry_count_over_limit(self):
        bomb = b"0" * (1024 * 1024)
        write_archive(self.archive_path, [("pilot/plugin.ini", native_manifest()), ("pilot/pilot.dll", bomb)],
                      compression=zipfile.ZIP_DEFLATED)
        with mock.patch.object(verifier, "_archive_sha256", side_effect=AssertionError("hashed before bounds")), \
                mock.patch.object(zipfile.ZipFile, "open", side_effect=AssertionError("decompressed before bounds")):
            with self.assertRaisesRegex(verifier.VerifyError, "compression ratio"):
                self.verify()

        entries = [(f"p{i}/x", b"") for i in range(verifier.MAX_ENTRIES + 1)]
        write_archive(self.archive_path, entries)
        with mock.patch.object(verifier, "_archive_sha256", side_effect=AssertionError("hashed before bounds")):
            with self.assertRaisesRegex(verifier.VerifyError, "entries; limit"):
                self.verify()

    def test_member_bounds_are_checked_before_archive_hashing_or_decompression(self):
        write_archive(self.archive_path, [
            ("pilot/plugin.ini", native_manifest()),
            ("pilot/pilot.dll", b"opaque"),
        ])
        with mock.patch.object(verifier, "MAX_FILE_BYTES", 1), \
                mock.patch.object(verifier, "_archive_sha256", side_effect=AssertionError("hashed before bounds")), \
                mock.patch.object(zipfile.ZipFile, "open", side_effect=AssertionError("decompressed before bounds")):
            with self.assertRaisesRegex(verifier.VerifyError, "file exceeds"):
                self.verify()

    def test_archive_size_bound_is_checked_before_archive_hashing(self):
        write_archive(self.archive_path, [("pilot/plugin.ini", native_manifest())])
        with mock.patch.object(verifier, "MAX_ARCHIVE_BYTES", 1), \
                mock.patch.object(verifier, "_archive_sha256", side_effect=AssertionError("hashed before bounds")):
            with self.assertRaisesRegex(verifier.VerifyError, "archive exceeds"):
                self.verify()

    def test_rejects_invalid_plugin_manifest(self):
        invalid = native_manifest().replace(b"requires = sco.storage,teleport", b"requires = Missing")
        write_archive(self.archive_path, [("pilot/plugin.ini", invalid), ("pilot/pilot.dll", b"opaque")])
        with self.assertRaisesRegex(verifier.VerifyError, "manifest parser"):
            self.verify()

    def test_core_sdk_package_contains_verifier_checker_and_parser_build_dependencies(self):
        package_spec = importlib.util.spec_from_file_location("sco_sdk_package", HERE / "package.py")
        self.assertIsNotNone(package_spec)
        package = importlib.util.module_from_spec(package_spec)
        package_spec.loader.exec_module(package)
        manifest = package.load_manifest()
        selected = {package.destination(manifest, "core", component): component["source"]
                    for component in package.select(manifest, "core")}
        expected = {
            "tools/sco-plugin-check.c": "sdk/tools/sco-plugin-check.c",
            "tools/sco-plugin-verify.py": "sdk/tools/sco-plugin-verify.py",
            "include/sco_api.h": "include/sco_api.h",
            "include/sco_widgets.h": "include/sco_widgets.h",
            "CMakeLists.txt": "sdk/CMakeLists.txt",
            "cmake/sco-plugin.cmake": "sdk/cmake/sco-plugin.cmake",
        }
        for destination, source in expected.items():
            with self.subTest(destination=destination):
                self.assertEqual(selected.get(destination), source)
        checker_source = (HERE / "tools" / "sco-plugin-check.c").read_text(encoding="utf-8")
        cmake_source = (HERE / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn('"sco_api.h"', checker_source)
        self.assertIn('"sco_widgets.h"', checker_source)
        self.assertIn("add_executable(sco-plugin-check tools/sco-plugin-check.c)", cmake_source)

    @unittest.skipUnless(PACKAGED_SDK, "requires an extracted SDK archive")
    def test_extracted_sdk_verifier_runs_with_its_built_manifest_parser(self):
        packaged_verifier = PACKAGED_SDK / "tools" / "sco-plugin-verify.py"
        self.assertTrue(packaged_verifier.is_file(), f"SDK archive omitted {packaged_verifier}")
        write_archive(self.archive_path, [
            ("pilot/plugin.ini", native_manifest()),
            ("pilot/pilot.dll", b"opaque, never loaded"),
        ])
        completed = subprocess.run(
            [sys.executable, str(packaged_verifier), str(self.archive_path), "--checker", CHECKER, "--json"],
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
        self.assertEqual(json.loads(completed.stdout)["plugin"]["id"], "pilot")


if __name__ == "__main__":
    unittest.main(verbosity=2)
