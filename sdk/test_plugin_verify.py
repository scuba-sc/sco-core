#!/usr/bin/env python3
"""Synthetic tests for the read-only SDK plugin archive verifier and folder no-load mode.

    python3 sdk/test_plugin_verify.py --checker build/sco-plugin-check

The checker is built from the SDK source. All plugin payloads below are inert test bytes; the
verifier must only hash them and must not extract or execute them.
"""
import argparse
import hashlib
import importlib.util
import json
import os
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

    def test_manifest_parser_round_trips_unicode_entry_names(self):
        entry = "pilot-é.dll"
        write_archive(self.archive_path, [
            ("pilot/plugin.ini", native_manifest(entry=entry, api="1.0")),
            (f"pilot/{entry}", b"opaque"),
        ])
        result = self.verify()
        self.assertEqual(result["plugin"]["entry"], entry)

    def test_manifest_only_entry_output_escapes_del(self):
        manifest = (b"id = pilot\nname = Pilot\nversion = 1.0\napi = 1.0\nkind = native\n"
                    b"entry = pilot\x7f.dll\n")
        completed = subprocess.run(
            [CHECKER, "--manifest-stdin", "pilot"], input=manifest, capture_output=True, check=False
        )
        self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
        self.assertNotIn(b"\x7f", completed.stdout + completed.stderr)
        self.assertIn(b"entry\tpilot\\x7F.dll", completed.stdout.splitlines())

    def test_archive_parser_diagnostics_escape_untrusted_setting_values(self):
        manifest = (b"id = pilot\nname = Pilot\nversion = 1.0\napi = 1.0\nkind = native\n"
                    b"entry = pilot.dll\n[settings]\nenabled = bool default \x1b[2J\n")
        write_archive(self.archive_path, [("pilot/plugin.ini", manifest), ("pilot/pilot.dll", b"opaque")])
        completed = subprocess.run(
            [sys.executable, str(HERE / "tools" / "sco-plugin-verify.py"), str(self.archive_path),
             "--checker", CHECKER],
            capture_output=True,
            check=False,
        )
        output = completed.stdout + completed.stderr
        self.assertEqual(completed.returncode, 1, output)
        self.assertNotIn(b"\x1b", output)
        self.assertIn(b"\\x1B[2J", output)

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
        for bad_path in ("../escape", "pilot/../../escape", "/pilot/plugin.ini", "C:/escape",
                         "pilot/bad?.txt", "pilot/bad*.txt", 'pilot/bad".txt', "pilot/bad<.txt",
                         "pilot/bad>.txt", "pilot/bad|.txt", "pilot/CONIN$.txt", "pilot/CONOUT$"):
            with self.subTest(path=bad_path):
                write_archive(self.archive_path, [(bad_path, b"x")])
                with self.assertRaisesRegex(verifier.VerifyError,
                                            "unsafe archive path|portable to Windows|reserved Windows device"):
                    self.verify()

    def test_rejects_backslash_in_raw_zip_member_name(self):
        # zipfile normalizes backslashes when creating ZipInfo on Windows. First
        # make a valid archive, then change both equal-length names in its bytes.
        write_archive(self.archive_path, [
            ("pilot/plugin.ini", native_manifest()), ("pilot/pilot.dll", b"opaque"),
            ("pilot/escape", b"x"),
        ])
        self.assertEqual(self.verify()["status"], "archive-structure-and-manifest-verified")
        archive_bytes = self.archive_path.read_bytes()
        self.assertEqual(archive_bytes.count(b"pilot/escape"), 2)
        archive_bytes = archive_bytes.replace(b"pilot/escape", b"pilot\\escape")
        self.assertEqual(archive_bytes.count(b"pilot\\escape"), 2)
        self.assertNotIn(b"pilot/escape", archive_bytes)
        self.archive_path.write_bytes(archive_bytes)
        with zipfile.ZipFile(self.archive_path) as archive:
            self.assertEqual(archive.infolist()[-1].orig_filename, "pilot\\escape")
        # Windows zipfile may normalize filename while retaining orig_filename.
        with self.assertRaisesRegex(verifier.VerifyError, "unsafe archive path|otherwise truncated"):
            self.verify()

    def test_rejects_nul_truncated_zip_member_name(self):
        manifest = b"id = pilot\nname = Pilot\nversion = 1.0\napi = 1.0\nkind = data\n"
        write_archive(self.archive_path, [("pilot/plugin.ini", manifest), ("pilot/asset.txt", b"opaque")])
        archive_bytes = self.archive_path.read_bytes()
        self.assertEqual(archive_bytes.count(b"pilot/asset.txt"), 2)
        self.archive_path.write_bytes(archive_bytes.replace(b"pilot/asset.txt", b"pilot/asset\x00txt"))
        with self.assertRaisesRegex(verifier.VerifyError, "NUL|unsafe archive path"):
            self.verify()

    def test_rejects_directory_local_header_name_mismatch(self):
        write_archive(self.archive_path, [
            ("pilot/", b""), ("pilot/plugin.ini", native_manifest()), ("pilot/pilot.dll", b"opaque")
        ])
        data = self.archive_path.read_bytes()
        # Replace the first local directory name only; the central directory still says pilot/.
        self.assertEqual(data[:4], b"PK\x03\x04")
        self.assertEqual(data[30:36], b"pilot/")
        self.archive_path.write_bytes(data[:30] + b"../xx/" + data[36:])
        with self.assertRaisesRegex(verifier.VerifyError, "could not read ZIP entry"):
            self.verify()

    def test_manifest_stdin_reads_ctrl_z_and_rejects_trailing_invalid_line(self):
        # Windows CRT text-mode stdin treats Ctrl-Z as EOF, unlike the host's binary read.
        result = subprocess.run([CHECKER, "--manifest-stdin", "pilot"],
                                input=native_manifest() + b"\x1a\n", capture_output=True, check=False)
        self.assertNotEqual(result.returncode, 0, result.stdout.decode(errors="replace"))

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

    def test_collision_prefix_work_has_a_total_bound_before_hashing(self):
        deep = "a/" * 480
        entries = [(f"p/{i:03}/{deep}f{i:03}.txt", b"") for i in range(8)]
        write_archive(self.archive_path, entries)
        with mock.patch.object(verifier, "_archive_sha256", side_effect=AssertionError("hashed before prefix bound")):
            with self.assertRaisesRegex(verifier.VerifyError, "collision checks exceed 4,194,304 aggregate prefix characters"):
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
            "README.md": "sdk/README.md",
            "CMakeLists.txt": "sdk/CMakeLists.txt",
            "cmake/sco-plugin.cmake": "sdk/cmake/sco-plugin.cmake",
        }
        for destination, source in expected.items():
            with self.subTest(destination=destination):
                self.assertEqual(selected.get(destination), source)
        checker_source = (HERE / "tools" / "sco-plugin-check.c").read_text(encoding="utf-8")
        cmake_source = (HERE / "CMakeLists.txt").read_text(encoding="utf-8")
        sdk_readme = (HERE / "README.md").read_text(encoding="utf-8")
        self.assertIn('"sco_api.h"', checker_source)
        self.assertIn('"sco_widgets.h"', checker_source)
        self.assertIn("--no-load", checker_source)
        self.assertIn("does not establish code safety", sdk_readme)
        self.assertIn("16,384 filesystem entries", sdk_readme)
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


class PluginFolderNoLoadTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.plugin = self.root / "pilot"
        self.plugin.mkdir()

    @staticmethod
    def manifest(plugin_id="pilot", kind="native", entry="pilot.dll"):
        lines = [f"id = {plugin_id}", "name = Pilot", "version = 1.0", "api = 1.0", f"kind = {kind}"]
        if kind != "data":
            lines.append(f"entry = {entry}")
        return ("\n".join(lines) + "\n").encode()

    def write_plugin(self, manifest=None, entry_bytes=b"not a loadable native library", include_entry=True):
        (self.plugin / "plugin.ini").write_bytes(manifest or self.manifest())
        if include_entry:
            (self.plugin / "pilot.dll").write_bytes(entry_bytes)

    def run_check(self, *args):
        return subprocess.run([CHECKER, str(self.plugin), *args], capture_output=True, text=True, check=False)

    def test_no_load_accepts_fake_native_binary_while_normal_mode_still_attempts_loading(self):
        self.write_plugin(entry_bytes=b"not a loadable native library")

        static = self.run_check("--no-load")
        self.assertEqual(static.returncode, 0, static.stdout + static.stderr)
        self.assertIn("NO-LOAD OK", static.stdout)
        self.assertIn("pilot.dll present; not loaded or executed", static.stdout)
        self.assertNotIn("load    ok", static.stdout)

        dynamic = self.run_check()
        self.assertEqual(dynamic.returncode, 1, dynamic.stdout + dynamic.stderr)
        self.assertIn("can't load", dynamic.stdout)

    def test_no_load_rejects_malformed_manifest(self):
        self.write_plugin(manifest=b"id = pilot\nthis is malformed\n")
        result = self.run_check("--no-load")
        self.assertEqual(result.returncode, 1)
        self.assertIn("expected key = value", result.stdout)
        self.assertIn("NO-LOAD FAILED", result.stdout)

    def test_no_load_rejects_manifest_id_that_differs_from_folder(self):
        self.write_plugin(manifest=self.manifest(plugin_id="different"))
        result = self.run_check("--no-load")
        self.assertEqual(result.returncode, 1)
        self.assertIn("id 'different' does not match folder 'pilot'", result.stdout)

    def test_no_load_rejects_missing_entry_file(self):
        self.write_plugin(include_entry=False)
        result = self.run_check("--no-load")
        self.assertEqual(result.returncode, 1)
        self.assertIn("entry 'pilot.dll' is not a regular non-symlink file or was not found", result.stdout)

    def test_no_load_rejects_parent_traversal_entry(self):
        self.write_plugin(manifest=self.manifest(entry="../outside.dll"))
        (self.root / "outside.dll").write_bytes(b"not executable")
        result = self.run_check("--no-load")
        self.assertEqual(result.returncode, 1)
        self.assertIn("entry must be a file name in the plugin folder", result.stdout)

    def test_no_load_refuses_truncated_manifest_paths(self):
        self.plugin = self.root / ("x" * 1100) / "pilot"
        result = self.run_check("--no-load")
        self.assertEqual(result.returncode, 1)
        self.assertIn("plugin.ini path exceeds the checker path-buffer limit", result.stdout)

    def test_no_load_rejects_symlinked_manifest_and_entry(self):
        outside_manifest = self.root / "outside.ini"
        outside_manifest.write_bytes(self.manifest())
        try:
            (self.plugin / "plugin.ini").symlink_to(outside_manifest)
        except (OSError, NotImplementedError) as exc:
            self.skipTest(f"symlink creation unavailable: {exc}")
        result = self.run_check("--no-load")
        self.assertEqual(result.returncode, 1)
        self.assertIn("plugin.ini is not a regular non-symlink file", result.stdout)

        (self.plugin / "plugin.ini").unlink()
        self.write_plugin(include_entry=False)
        outside_entry = self.root / "outside.dll"
        outside_entry.write_bytes(b"not executable")
        try:
            (self.plugin / "pilot.dll").symlink_to(outside_entry)
        except (OSError, NotImplementedError) as exc:
            self.skipTest(f"symlink creation unavailable: {exc}")
        result = self.run_check("--no-load")
        self.assertEqual(result.returncode, 1)
        self.assertIn("entry 'pilot.dll' is not a regular non-symlink file", result.stdout)

    def test_no_load_rejects_symlinked_plugin_folder_with_or_without_trailing_separator(self):
        self.write_plugin()
        link = self.root / "pilot-link"
        try:
            link.symlink_to(self.plugin, target_is_directory=True)
        except (OSError, NotImplementedError) as exc:
            self.skipTest(f"symlink creation unavailable: {exc}")
        for folder in (str(link), str(link) + os.sep):
            with self.subTest(folder=folder):
                result = subprocess.run([CHECKER, folder, "--no-load"], capture_output=True, text=True, check=False)
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                self.assertIn("plugin folder is not a regular non-symlink directory", result.stdout)
                self.assertNotIn("  ini     id=", result.stdout)

    def test_no_load_reports_data_pack_inventory(self):
        (self.plugin / "plugin.ini").write_bytes(self.manifest(kind="data"))
        (self.plugin / "lists").mkdir()
        (self.plugin / "lists" / "routes.txt").write_text("Lorville\n")
        (self.plugin / "scripts" / "nested").mkdir(parents=True)
        (self.plugin / "scripts" / "nested" / "events.xml").write_text("<events/>\n")
        result = self.run_check("--no-load")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("content lists/routes.txt", result.stdout)
        self.assertIn("content scripts/nested/events.xml", result.stdout)
        self.assertIn("pack    2 file(s)", result.stdout)
        self.assertIn("NO-LOAD OK", result.stdout)

    def test_no_load_inventories_mission_declaration_content(self):
        (self.plugin / "plugin.ini").write_bytes(self.manifest(kind="data"))
        (self.plugin / "missions").mkdir()
        (self.plugin / "missions" / "pilot_route.scom").write_text("mission declaration\n")
        result = self.run_check("--no-load")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("content missions/pilot_route.scom", result.stdout)
        self.assertIn("pack    1 file(s)", result.stdout)

    def test_no_load_ignores_symlinked_content_roots_and_nested_directories(self):
        (self.plugin / "plugin.ini").write_bytes(self.manifest(kind="data"))
        (self.plugin / "lists").mkdir()
        (self.plugin / "lists" / "routes.txt").write_text("local\n")
        outside = self.root / "outside"
        outside.mkdir()
        (outside / "external.xml").write_text("<outside/>\n")
        try:
            (self.plugin / "scripts").symlink_to(outside, target_is_directory=True)
        except (OSError, NotImplementedError) as exc:
            self.skipTest(f"symlink creation unavailable: {exc}")

        result = self.run_check("--no-load")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotIn("scripts/external.xml", result.stdout)
        self.assertIn("pack    1 file(s)", result.stdout)

        (self.plugin / "scripts").unlink()
        (self.plugin / "scripts").mkdir()
        try:
            (self.plugin / "scripts" / "nested").symlink_to(outside, target_is_directory=True)
        except (OSError, NotImplementedError) as exc:
            self.skipTest(f"symlink creation unavailable: {exc}")
        result = self.run_check("--no-load")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotIn("scripts/nested/external.xml", result.stdout)
        self.assertIn("pack    1 file(s)", result.stdout)

    def test_no_load_escapes_control_bytes_in_content_filenames(self):
        (self.plugin / "plugin.ini").write_bytes(self.manifest(kind="data"))
        listings = self.plugin / "lists"
        listings.mkdir()
        try:
            (listings / "spoof\x1b[2J\n.txt").write_text("fixture\n")
        except OSError as exc:
            self.skipTest(f"platform does not permit control bytes in file names: {exc}")
        result = self.run_check("--no-load")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotIn("\x1b", result.stdout)
        self.assertNotIn("\n.txt", result.stdout)
        self.assertIn(r"content lists/spoof\x1B[2J\x0A.txt", result.stdout)

    def test_no_load_escapes_control_bytes_in_entry_metadata(self):
        self.write_plugin(manifest=self.manifest(entry="pilot\x7f.dll"), include_entry=False)
        result = self.run_check("--no-load")
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertNotIn("\x7f", result.stdout)
        self.assertIn(r"pilot\x7F.dll", result.stdout)

    def test_dynamic_loader_failure_escapes_caller_path_and_loader_detail(self):
        self.plugin = self.root / "parent\x1b[2J" / "pilot"
        try:
            self.plugin.mkdir(parents=True)
        except OSError as exc:
            self.skipTest(f"platform does not permit control bytes in paths: {exc}")
        self.write_plugin(entry_bytes=b"not a loadable native library")
        result = self.run_check()
        self.assertEqual(result.returncode, 1)
        self.assertIn("can't load", result.stdout)
        self.assertNotIn("\x1b", result.stdout)
        self.assertIn(r"parent\x1B[2J/pilot/pilot.dll", result.stdout)

    def test_no_load_limits_script_depth_to_host_inventory_depth(self):
        (self.plugin / "plugin.ini").write_bytes(self.manifest(kind="data"))
        (self.plugin / "lists").mkdir()
        (self.plugin / "lists" / "routes.txt").write_text("local\n")
        current = self.plugin / "scripts"
        for depth in range(1, 18):
            current = current / f"d{depth}"
        current.mkdir(parents=True)
        (current / "too_deep.xml").write_text("<ignored/>\n")
        result = self.run_check("--no-load")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotIn("too_deep.xml", result.stdout)
        self.assertIn("pack    1 file(s); scan limit 16384 entries, scripts depth 16", result.stdout)

    def test_no_load_bounds_total_filesystem_entries_scanned(self):
        (self.plugin / "plugin.ini").write_bytes(self.manifest(kind="data"))
        listings = self.plugin / "lists"
        listings.mkdir()
        for i in range(16385):
            (listings / f"noise_{i:05}.bin").touch()
        result = self.run_check("--no-load")
        self.assertEqual(result.returncode, 1)
        self.assertIn("data pack inventory exceeds scan limit of 16384 filesystem entries", result.stdout)

    def test_no_load_rejects_invoke_flag_conflict(self):
        self.write_plugin()
        result = self.run_check("--no-load", "--invoke", "pilot.ping")
        self.assertEqual(result.returncode, 2)
        self.assertIn("--no-load cannot be combined with --invoke", result.stderr)
        self.assertNotIn("can't load", result.stdout)

    def test_invoke_accepts_literal_no_load_as_command_argument(self):
        self.write_plugin(entry_bytes=b"not a loadable native library")
        result = self.run_check("--invoke", "pilot.ping", "--no-load")
        # It must reach normal dynamic checking, not interpret the argument as an option.
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("can't load", result.stdout)
        self.assertNotIn("cannot be combined", result.stderr)

    def test_no_load_rejects_dynamic_only_flags(self):
        self.write_plugin()
        for flag, value in (("--cap", "teleport"), ("--short-tables", None)):
            with self.subTest(flag=flag):
                args = ("--no-load", flag) + ((value,) if value else ())
                result = self.run_check(*args)
                self.assertEqual(result.returncode, 2)
                self.assertIn("--no-load cannot be combined with --cap or --short-tables", result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
