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
import warnings
import zipfile


HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("sco_plugin_verify", HERE / "tools" / "sco-plugin-verify.py")
assert spec and spec.loader
verifier = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verifier)

parser = argparse.ArgumentParser()
parser.add_argument("--checker", required=True)
CHECKER = str(Path(parser.parse_args().checker).resolve())
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
        for bad_path in ("../escape", "pilot/../../escape", "/pilot/plugin.ini", "pilot\\escape", "C:/escape"):
            with self.subTest(path=bad_path):
                write_archive(self.archive_path, [(bad_path, b"x")])
                with self.assertRaises(verifier.VerifyError):
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
        with self.assertRaisesRegex(verifier.VerifyError, "compression ratio"):
            self.verify()

        entries = [(f"p{i}/x", b"") for i in range(verifier.MAX_ENTRIES + 1)]
        write_archive(self.archive_path, entries)
        with self.assertRaisesRegex(verifier.VerifyError, "entries; limit"):
            self.verify()

    def test_rejects_invalid_plugin_manifest(self):
        invalid = native_manifest().replace(b"requires = sco.storage,teleport", b"requires = Missing")
        write_archive(self.archive_path, [("pilot/plugin.ini", invalid), ("pilot/pilot.dll", b"opaque")])
        with self.assertRaisesRegex(verifier.VerifyError, "manifest parser"):
            self.verify()


if __name__ == "__main__":
    unittest.main(verbosity=2)
