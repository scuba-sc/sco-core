#!/usr/bin/env python3
"""Tests for tools/ip_audit.py. Run: python3 tools/test_ip_audit.py (CI's ip-compliance job does).

1. The real repository and all five SDK profiles pass, and the SBOM binds the commit and zip hash.
2. Gate 7: each synthetic defect, in a scratch copy of the repository or a rebuilt zip, must make the
   audit exit non-zero with a message naming the problem. Nothing here touches the real tree.
"""
import fnmatch
import hashlib
import importlib.util
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
spec = importlib.util.spec_from_file_location("ip_audit", os.path.join(HERE, "ip_audit.py"))
assert spec and spec.loader
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)
# sdk/package.py: its manifest rules and safe-path predicate, the same module ip_audit.py loads.
pspec = importlib.util.spec_from_file_location("package", os.path.join(ROOT, "sdk", "package.py"))
assert pspec and pspec.loader
pkg = importlib.util.module_from_spec(pspec)
pspec.loader.exec_module(pkg)


def read(path, mode="rb"):
    with open(path, mode) as f:
        return f.read()


def run(*argv, root=None):
    """Exit code and output of the real CLI."""
    cmd = [sys.executable, os.path.join(HERE, "ip_audit.py"), *argv]
    if root:
        cmd += ["--root", root]
    p = subprocess.run(cmd, capture_output=True, text=True)
    return p.returncode, p.stdout + p.stderr


def package(root, profile, out):
    p = subprocess.run([sys.executable, os.path.join(root, "sdk", "package.py"), "--profile", profile, "--out", out],
                       capture_output=True, text=True, cwd=root)
    if p.returncode != 0:
        raise AssertionError(p.stdout + p.stderr)
    return [os.path.join(out, f) for f in os.listdir(out) if f.endswith(".zip")][0]


class Scratch:
    """A git copy of the repository's tracked files (working-tree content), committed once."""

    def __init__(self):
        self.dir = tempfile.mkdtemp(prefix="ipaudit-")
        files = audit.tracked(ROOT)
        for f in files:
            os.makedirs(os.path.join(self.dir, os.path.dirname(f)), exist_ok=True)
            shutil.copy2(os.path.join(ROOT, f), os.path.join(self.dir, f))
        env = dict(os.environ, GIT_AUTHOR_NAME="t", GIT_AUTHOR_EMAIL="t@example.com",
                   GIT_COMMITTER_NAME="t", GIT_COMMITTER_EMAIL="t@example.com")
        for c in (["init", "-q"], ["add", "-A"], ["commit", "-q", "-m", "scratch"]):
            subprocess.run(["git", "-C", self.dir, *c], check=True, env=env, capture_output=True)

    def path(self, rel):
        return os.path.join(self.dir, rel)

    def add(self, rel, data):
        os.makedirs(os.path.dirname(self.path(rel)), exist_ok=True)
        with open(self.path(rel), "wb") as f:
            f.write(data)
        subprocess.run(["git", "-C", self.dir, "add", "-f", rel], check=True, capture_output=True)

    def edit_json(self, rel, fn):
        with open(self.path(rel), encoding="utf-8") as f:
            d = json.load(f)
        fn(d)
        with open(self.path(rel), "w", encoding="utf-8") as f:
            json.dump(d, f, indent=1)

    def close(self):
        shutil.rmtree(self.dir, ignore_errors=True)


def rezip(src, dst, change):
    """Copy a zip, letting change(entries: dict name -> bytes) edit it."""
    with zipfile.ZipFile(src) as z:
        entries = {i.filename: z.read(i) for i in z.infolist()}
    change(entries)
    with zipfile.ZipFile(dst, "w") as z:
        for n in sorted(entries):
            z.writestr(n, entries[n])


def resum(entries):
    """Rewrite SHA256SUMS so a test reaches the check after it."""
    top = next(iter(entries)).split("/", 1)[0]
    lines = [f"{hashlib.sha256(d).hexdigest()}  {n[len(top) + 1:]}\n"
             for n, d in sorted(entries.items()) if not n.endswith("/SHA256SUMS")]
    entries[f"{top}/SHA256SUMS"] = "".join(lines).encode()


def refresh_generated_metadata(entries, profile):
    """Rebuild hashes/SBOM after a mutation, so semantic checks must catch the defect."""
    top = next(iter(entries)).split("/", 1)[0]
    content = {name[len(top) + 1:]: data for name, data in entries.items()}
    manifest = pkg.load_manifest()
    _, sums_name, _, inventory_name, sbom_name = pkg.metadata_names(manifest, profile)
    records = {row["path"]: row for row in json.loads(content[inventory_name])["files"]}
    payload = {name: (data, False) for name, data in content.items() if name not in (sums_name, sbom_name)}
    for name in payload:
        records.setdefault(name, {"path": name, "source": "tools/sdk_workflows.py", "origin": "generated",
                                  "license": "GPL-3.0-only", "copyright": "2026 the sco-core contributors",
                                  "status": "needs-review"})
    version = pkg.game_pack()[0] if profile == "game-sc" else pkg.sdk_version()
    content[sbom_name] = pkg.json_bytes(pkg.sbom_document(
        top, version, payload, records, sbom_name, sums_name, audit.git_head(ROOT)))
    content[sums_name] = "".join(f"{hashlib.sha256(data).hexdigest()}  {name}\n"
                               for name, data in sorted(content.items()) if name != sums_name).encode("ascii")
    entries.clear()
    entries.update({top + "/" + name: data for name, data in content.items()})


class SpdxIdentifiers(unittest.TestCase):
    def test_path_encoding_is_injective_and_spdx_valid(self):
        paths = ("include/sco/content.h", "include/sco_content.h",
                 "include-sco-content.h", "include/sco-content.h",
                 "include/é.h", "include/-.h", "include/_.h", "")
        ids = [audit.spdx_id("File-" + path) for path in paths]
        self.assertEqual(len(set(ids)), len(paths))
        for path, identifier in zip(paths, ids):
            self.assertRegex(identifier, r"^SPDXRef-[A-Za-z0-9.-]+$")
            self.assertEqual(identifier, audit.spdx_id("File-" + path))


class ReuseGeneration(unittest.TestCase):
    def test_distinct_copyright_holders_remain_separate(self):
        import tomllib
        entries = [
            {'path': 'one', 'copyright': '2026 project contributors', 'license': 'GPL-3.0-only'},
            {'path': 'mixed', 'copyright': '2023 upstream contributors\n2026 project contributors',
             'license': 'MIT AND GPL-3.0-only'},
        ]
        annotations = tomllib.loads(audit.reuse_toml(entries))['annotations']
        self.assertEqual(annotations[0]['SPDX-FileCopyrightText'],
                         ['2023 upstream contributors', '2026 project contributors'])
        self.assertEqual(annotations[0]['SPDX-License-Identifier'], 'MIT AND GPL-3.0-only')
        self.assertEqual(annotations[1]['SPDX-FileCopyrightText'], '2026 project contributors')


class Real(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.out = tempfile.mkdtemp(prefix="ipaudit-real-")
        cls.zips = {p: package(ROOT, p, os.path.join(cls.out, p))
                    for p in ("core", "game-sc", "legacy", "core-source", "host-source-sc")}

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.out, ignore_errors=True)

    def test_repository_passes(self):
        code, out = run("repo")
        self.assertEqual(code, 0, out)

    def test_zips_pass_and_sbom_binds_commit_and_hash(self):
        for prof, z in self.zips.items():
            sbom = os.path.join(self.out, prof + ".spdx.json")
            code, out = run("zip", z, "--profile", prof, "--sbom", sbom)
            self.assertEqual(code, 0, out)
            self.assertNotIn(self.out, out, "the report names the SBOM, never a local path")
            d = json.loads(read(sbom, "r"))
            zsha = hashlib.sha256(read(z)).hexdigest()
            self.assertEqual(d["packages"][0]["checksums"][0]["checksumValue"], zsha)
            self.assertIn(audit.git_head(ROOT), d["packages"][0]["downloadLocation"])
            self.assertTrue(d["documentNamespace"].endswith(zsha + "-spdx-v2"))
            with zipfile.ZipFile(z) as zz:
                n = len([i for i in zz.infolist() if not i.filename.endswith("/")])
                workflows = [name for name in zz.namelist() if name.endswith("/sdk-workflows.json")]
                self.assertEqual(len(workflows), 0 if prof == "game-sc" else 1)
            if prof != "game-sc":
                self.assertIn("workflow index = selected payload: 1 checked, 0 failed", out)
            self.assertEqual(len(d["files"]), n, "the SBOM lists every archive entry")
            file_ids = [f["SPDXID"] for f in d["files"]]
            self.assertEqual(len(set(file_ids)), n, "distinct paths need distinct SPDX IDs")
            self.assertEqual({r["relatedSpdxElement"] for r in d["relationships"]
                              if r["relationshipType"] == "CONTAINS"}, set(file_ids))
            self.assertEqual(len(d["relationships"]), n + 1)
            for identifier in file_ids + [d["packages"][0]["SPDXID"]]:
                self.assertRegex(identifier, r"^SPDXRef-[A-Za-z0-9.-]+$")

    def assert_rehashed_semantic_failure(self, profile, change, check_name, needle):
        bad = os.path.join(self.out, "bad-workflows-" + profile + ".zip")
        def mutate(entries):
            change(entries)
            refresh_generated_metadata(entries, profile)
        rezip(self.zips[profile], bad, mutate)
        report = audit.audit_zip(bad, profile)
        for required in ("archive structure", "SHA256SUMS (Gate 6)", "embedded SPDX 2.3 SBOM"):
            check, = [row for row in report.checks if row["check"] == required]
            self.assertEqual(check["failed"], 0, report.failures)
        check, = [row for row in report.checks if row["check"] == check_name]
        self.assertGreater(check["failed"], 0, report.failures)
        self.assertTrue(any(needle in failure for failure in report.failures), report.failures)

    def test_overlay_refuses_unselected_workflow_index(self):
        with zipfile.ZipFile(self.zips["core"]) as archive:
            index, = [name for name in archive.namelist() if name.endswith("/sdk-workflows.json")]
            data = archive.read(index)
        def change(entries):
            top = next(iter(entries)).split("/", 1)[0]
            entries[top + "/sdk-workflows.json"] = data
        self.assert_rehashed_semantic_failure("game-sc", change, "archive = manifest selection",
                                              "sdk-workflows.json: in the archive but not selected")

    def test_selected_profiles_refuse_missing_workflow_index(self):
        for profile in ("core", "core-source", "legacy", "host-source-sc"):
            with self.subTest(profile=profile):
                def change(entries):
                    name, = [name for name in entries if name.endswith("/sdk-workflows.json")]
                    del entries[name]
                self.assert_rehashed_semantic_failure(profile, change, "archive = manifest selection",
                                                      "sdk-workflows.json: missing")

    def test_workflow_index_source_and_embedded_text_are_bound_to_payload(self):
        for profile in ("core", "core-source", "legacy", "host-source-sc"):
            for defect in ("source", "text", "manifest"):
                with self.subTest(profile=profile, defect=defect):
                    def change(entries):
                        name, = [name for name in entries if name.endswith("/sdk-workflows.json")]
                        index = json.loads(entries[name])
                        if defect == "source":
                            index["source_sha"] = "0" * 40
                        elif defect == "manifest":
                            index["manifest_sha256"] = "0" * 64
                        else:
                            record = index["workflows"][0]["files"][0]
                            record["text"] += "\nAltered embedded instructions.\n"
                            record["sha256"] = hashlib.sha256(record["text"].encode()).hexdigest()
                            index["content_sha256"] = hashlib.sha256(pkg.json_bytes(index["workflows"])).hexdigest()
                        entries[name] = pkg.json_bytes(index)
                    self.assert_rehashed_semantic_failure(profile, change, "workflow index = selected payload",
                                                          "sdk-workflows.json: differs from the selected payload")

    def test_the_sbom_is_reproducible(self):
        a, b = os.path.join(self.out, "a.json"), os.path.join(self.out, "b.json")
        run("zip", self.zips["core"], "--profile", "core", "--sbom", a)
        run("zip", self.zips["core"], "--profile", "core", "--sbom", b)
        self.assertEqual(read(a), read(b))

    def test_core_zip_checked_as_game_sc_fails(self):
        code, out = run("zip", self.zips["core"], "--profile", "game-sc")
        self.assertEqual(code, 1, out)
        self.assertIn("not selected by", out)

    def test_source_kits_refuse_missing_dependency_licences(self):
        for profile in ("core-source", "host-source-sc"):
            for removed in (("LICENSES/MIT.txt", "plugins/lua/third_party/lua/LICENSE"),
                            ("LICENSES/blessing.txt",)):
                with self.subTest(profile=profile, removed=removed):
                    bad = os.path.join(self.out, "missing-licence.zip")
                    def change(entries):
                        top = next(iter(entries)).split("/", 1)[0]
                        for rel in removed:
                            del entries[f"{top}/{rel}"]
                        resum(entries)
                    rezip(self.zips[profile], bad, change)
                    report = audit.audit_zip(bad, profile)
                    check, = [c for c in report.checks if c["check"] == "licences and notices (Gate C/D)"]
                    self.assertGreater(check["failed"], 0)

    def test_source_kits_refuse_missing_notices(self):
        for profile in ("core-source", "host-source-sc"):
            with self.subTest(profile=profile):
                bad = os.path.join(self.out, "missing-notices.zip")
                def change(entries):
                    top = next(iter(entries)).split("/", 1)[0]
                    del entries[f"{top}/THIRD_PARTY_NOTICES.md"]
                    resum(entries)
                rezip(self.zips[profile], bad, change)
                report = audit.audit_zip(bad, profile)
                check, = [c for c in report.checks if c["check"] == "licences and notices (Gate C/D)"]
                self.assertGreater(check["failed"], 0)


class Negative(unittest.TestCase):
    """Gate 7: every defect is refused (exit 1) with a message that says what is wrong."""

    @classmethod
    def setUpClass(cls):
        cls.out = tempfile.mkdtemp(prefix="ipaudit-neg-")
        cls.core = package(ROOT, "core", os.path.join(cls.out, "core"))

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.out, ignore_errors=True)

    def setUp(self):
        self.s = None

    def tearDown(self):
        if self.s:
            self.s.close()

    def scratch(self):
        self.s = Scratch()
        return self.s

    def refused_repo(self, needle):
        assert self.s is not None
        code, out = run("repo", root=self.s.dir)
        self.assertEqual(code, 1, out)
        self.assertIn(needle, out)

    def refused_zip(self, change, needle, profile="core"):
        bad = os.path.join(self.out, f"bad-{self.id().rsplit('.', 1)[-1]}.zip")
        rezip(self.core, bad, change)
        code, out = run("zip", bad, "--profile", profile)
        self.assertEqual(code, 1, out)
        self.assertIn(needle, out)

    # Repository.
    def test_needs_review_requires_no_per_file_release_decision(self):
        s = self.scratch()
        def mark_pending(doc):
            audit.match(doc["entries"], "include/sco_api.h")["status"] = "needs-review"
        s.edit_json(audit.PROVENANCE, mark_pending)
        code, out = run("repo", root=s.dir)
        self.assertEqual(code, 0, out)
        self.assertNotIn("release decision", out)
        archive = package(s.dir, "core", os.path.join(self.out, "pending"))
        code, out = run("zip", archive, "--profile", "core", root=s.dir)
        self.assertEqual(code, 0, out)
        self.assertNotIn("release decision", out)
        with zipfile.ZipFile(archive) as z:
            name, = [n for n in z.namelist() if n.endswith("/ATTRIBUTIONS.json")]
            records = json.loads(z.read(name))["files"]
        record, = [r for r in records if r["source"] == "include/sco_api.h"]
        self.assertEqual(record["status"], "needs-review")
        self.assertTrue(record["license"] and record["copyright"])

    def test_unclassified_file(self):
        s = self.scratch()
        s.edit_json(audit.PROVENANCE, lambda d: d["entries"].__setitem__(slice(None),
            [e for e in d["entries"] if not fnmatch.fnmatchcase("tools/ip_audit.py", e["path"])]))
        self.refused_repo("tools/ip_audit.py: no entry in")

    def test_unknown_origin(self):
        s = self.scratch()
        s.edit_json(audit.PROVENANCE, lambda d: d["entries"][-1].__setitem__("origin", "found-somewhere"))
        code, out = run("repo", root=s.dir)
        self.assertEqual(code, 2, out)
        self.assertIn("unknown origin", out)

    def test_unknown_status(self):
        s = self.scratch()
        s.edit_json(audit.PROVENANCE, lambda d: d["entries"][-1].__setitem__("status", "unknown"))
        code, out = run("repo", root=s.dir)
        self.assertEqual(code, 2, out)
        self.assertIn("unknown status", out)

    def test_unresolved_provenance(self):
        s = self.scratch()
        s.edit_json(audit.PROVENANCE, lambda d: d["entries"].insert(0, {"path": "include/sco_api.h", "origin": "project-authored",
                    "license": "GPL-3.0-only", "copyright": "x", "status": "unresolved", "issue": "test"}))
        self.refused_repo("include/sco_api.h: provenance unresolved")

    def test_binary_with_a_refused_extension(self):
        s = self.scratch()
        s.add("tests/fixtures/dump.dmp", b"synthetic, harmless\n")
        self.refused_repo("tests/fixtures/dump.dmp: refused extension .dmp")

    def test_binary_content_under_an_innocent_name(self):
        s = self.scratch()
        s.add("docs/notes.txt", b"MZ\x90\x00synthetic")
        self.refused_repo("docs/notes.txt: content is a Windows executable")

    def test_allowlisted_fixture_with_a_changed_hash(self):
        s = self.scratch()
        s.add("tests/fixtures/plugins/tree/hello/hello.dll", b"changed\n")
        self.refused_repo("allowlisted, but its sha256")

    def test_changed_vendored_file(self):
        s = self.scratch()
        with open(s.path("third_party/tomlplusplus/toml.hpp"), "ab") as f:
            f.write(b"// local change\n")
        self.refused_repo("third_party/tomlplusplus/toml.hpp: differs from its pinned sha256")

    def test_missing_licence_notice(self):
        s = self.scratch()
        os.remove(s.path("sdk/third_party/imgui/LICENSE.txt"))
        subprocess.run(["git", "-C", s.dir, "rm", "-q", "--cached", "sdk/third_party/imgui/LICENSE.txt"], check=True)
        code, out = run("repo", root=s.dir)
        self.assertEqual(code, 1, out)
        self.assertIn("sdk/third_party/imgui/LICENSE.txt: missing licence or notice file", out)

    def test_manifest_and_provenance_disagree_on_a_licence(self):
        s = self.scratch()
        def f(d):
            for c in d["components"]:
                if c["source"] == "include/sco_api.h":
                    c["license"] = "MIT"
        s.edit_json(audit.COMPONENTS, f)
        self.refused_repo("include/sco_api.h: sdk/components.json says MIT")

    def test_malformed_provenance(self):
        s = self.scratch()
        with open(s.path(audit.PROVENANCE), "w") as f:
            f.write("{ not json")
        code, out = run("repo", root=s.dir)
        self.assertEqual(code, 2, out)
        self.assertIn("can't read it", out)

    def test_malformed_manifest(self):
        s = self.scratch()
        with open(s.path(audit.COMPONENTS), "w") as f:
            f.write("[]")
        code, out = run("zip", self.core, "--profile", "core", root=s.dir)
        self.assertEqual(code, 2, out)
        self.assertIn("sdk/components.json", out)

    def test_broken_dependency_declaration(self):
        s = self.scratch()
        s.edit_json(audit.COMPONENTS, lambda d: d["profiles"]["game-sc"].__setitem__("requires", "nonexistent"))
        code, out = run("zip", self.core, "--profile", "core", root=s.dir)
        self.assertEqual(code, 2, out)
        self.assertIn("unknown profile", out)

    # Archives.
    def test_unknown_file_in_core_zip(self):
        def f(e):
            top = next(iter(e)).split("/", 1)[0]
            e[f"{top}/extra.txt"] = b"x"
            resum(e)
        self.refused_zip(f, "extra.txt: in the archive but not selected")

    def test_game_header_in_core_zip(self):
        def f(e):
            top = next(iter(e)).split("/", 1)[0]
            e[f"{top}/include/sc_actors.h"] = read(os.path.join(ROOT, "include/sc_actors.h"))
            resum(e)
        self.refused_zip(f, "include/sc_actors.h: in the archive but not selected")

    def test_core_file_naming_a_game_header(self):
        def f(e):
            top = next(iter(e)).split("/", 1)[0]
            e[f"{top}/include/scosdk/plugin.hpp"] += b'\n#include "sc_world.h"\n'
            resum(e)
        bad = os.path.join(self.out, "bad-include.zip")
        rezip(self.core, bad, f)
        code, out = run("zip", bad, "--profile", "core")
        self.assertEqual(code, 1, out)
        self.assertIn("which only the Star Citizen extension ships", out)
        self.assertIn("differs from include/scosdk/plugin.hpp", out)

    def test_missing_licence_in_zip(self):
        def f(e):
            top = next(iter(e)).split("/", 1)[0]
            del e[f"{top}/third_party/imgui/LICENSE.txt"]
            resum(e)
        self.refused_zip(f, "no licence file ships beside it")

    def test_unexpected_binary_entry(self):
        def f(e):
            top = next(iter(e)).split("/", 1)[0]
            e[f"{top}/tools/helper.exe"] = b"MZ synthetic"
            resum(e)
        self.refused_zip(f, "tools/helper.exe: refused extension .exe")

    def test_wrong_sha256_entry(self):
        def f(e):
            top = next(iter(e)).split("/", 1)[0]
            e[f"{top}/SHA256SUMS"] = e[f"{top}/SHA256SUMS"].replace(b"  README.md", b"  README.md", 1)
            lines = e[f"{top}/SHA256SUMS"].decode().splitlines(True)
            lines[0] = "0" * 64 + lines[0][64:]
            e[f"{top}/SHA256SUMS"] = "".join(lines).encode()
        self.refused_zip(f, "SHA256SUMS says 0000")

    def test_unsafe_path(self):
        def f(e):
            e["../evil.txt"] = b"x"
        self.refused_zip(f, "one top folder")

    def test_not_a_zip(self):
        bad = os.path.join(self.out, "not.zip")
        with open(bad, "wb") as f:
            f.write(b"not a zip")
        code, out = run("zip", bad, "--profile", "core")
        self.assertEqual(code, 2, out)
        self.assertIn("not a readable zip", out)

    # Archive structure.
    def test_parent_directory_entry(self):
        def f(e):
            top = next(iter(e)).split("/", 1)[0]
            e[f"{top}/../../evil.txt"] = b"x"
            resum(e)
        self.refused_zip(f, "unsafe path")

    def test_absolute_path_entry(self):
        def f(e):
            e["/etc/passwd"] = b"x"
            resum(e)
        self.refused_zip(f, "unsafe path")

    def test_backslash_in_entry_name(self):
        bad = os.path.join(self.out, "bad-raw-backslash.zip")
        with zipfile.ZipFile(self.core) as source, zipfile.ZipFile(bad, "w") as target:
            top = source.namelist()[0].split("/", 1)[0]
            for info in source.infolist():
                target.writestr(info, source.read(info))
            # Construct safely, then retain the deliberate raw name: ZipInfo's
            # constructor otherwise normalizes backslashes on Windows.
            entry = zipfile.ZipInfo(f"{top}/dir/evil.txt")
            entry.filename = entry.orig_filename = f"{top}/dir\\evil.txt"
            target.writestr(entry, b"x")
        with zipfile.ZipFile(bad) as archive:
            self.assertIn(entry.orig_filename, [info.orig_filename for info in archive.infolist()])
        code, out = run("zip", bad, "--profile", "core")
        self.assertEqual(code, 1, out)
        self.assertIn("unsafe path", out)

    def test_drive_letter_entry(self):
        def f(e):
            top = next(iter(e)).split("/", 1)[0]
            e[f"{top}/C:evil.txt"] = b"x"
            resum(e)
        self.refused_zip(f, "unsafe path")

    def test_ntfs_alternate_data_stream(self):
        def f(e):
            top = next(iter(e)).split("/", 1)[0]
            e[f"{top}/README.md:evil"] = b"x"
            resum(e)
        self.refused_zip(f, "unsafe path")

    def test_windows_reserved_name(self):
        for name in ("CON", "nul.log", "com1", "LPT9.txt", "aux"):
            with self.subTest(name=name):
                def f(e, name=name):
                    top = next(iter(e)).split("/", 1)[0]
                    e[f"{top}/{name}"] = b"x"
                    resum(e)
                self.refused_zip(f, "reserved Windows device name")

    def test_trailing_dot_or_space_segment(self):
        for name in ("trailing.", "trailing ", "dir./file.txt"):
            with self.subTest(name=name):
                def f(e, name=name):
                    top = next(iter(e)).split("/", 1)[0]
                    e[f"{top}/{name}"] = b"x"
                    resum(e)
                self.refused_zip(f, "ends in a dot or a space")

    def test_case_fold_collision(self):
        def f(e):
            top = next(iter(e)).split("/", 1)[0]
            e[f"{top}/readme.md"] = e[f"{top}/README.md"]
            resum(e)
        self.refused_zip(f, "except for case")

    def test_duplicate_entry_name(self):
        def f(e):
            top = next(iter(e)).split("/", 1)[0]
            e[f"{top}/README.md"] = b"first"
        bad = os.path.join(self.out, "bad-dup.zip")
        with zipfile.ZipFile(self.core) as zsrc, zipfile.ZipFile(bad, "w") as zdst:
            for i in zsrc.infolist():
                zdst.writestr(i.filename, zsrc.read(i))
            top = next(iter(zsrc.namelist())).split("/", 1)[0]
            zdst.writestr(f"{top}/README.md", b"second")
        code, out = run("zip", bad, "--profile", "core")
        self.assertEqual(code, 1, out)
        self.assertIn("listed twice", out)

    def test_nested_archive(self):
        def f(e):
            top = next(iter(e)).split("/", 1)[0]
            e[f"{top}/tools/inner.zip"] = b"PK\x03\x04synthetic, harmless"
            resum(e)
        self.refused_zip(f, "refused extension .zip")

    def test_symlink_entry(self):
        bad = os.path.join(self.out, "bad-link.zip")
        with zipfile.ZipFile(self.core) as zsrc, zipfile.ZipFile(bad, "w") as zdst:
            for i in zsrc.infolist():
                data = zsrc.read(i)
                info = zipfile.ZipInfo(i.filename)
                info.external_attr = i.external_attr
                zdst.writestr(info, data)
            top = next(iter(zsrc.namelist())).split("/", 1)[0]
            link = zipfile.ZipInfo(f"{top}/link.txt")
            link.create_system = 3
            link.external_attr = (0o120777 << 16)
            zdst.writestr(link, b"README.md")
        code, out = run("zip", bad, "--profile", "core")
        self.assertEqual(code, 1, out)
        self.assertIn("a symlink", out)

    def test_entry_count_limit(self):
        def f(e):
            top = next(iter(e)).split("/", 1)[0]
            for i in range(audit.MAX_ENTRIES - len(e) + 1):
                e[f"{top}/pad/{i}.txt"] = b"x"
            resum(e)
        self.refused_zip(f, "over the 464-entry limit")

    def test_entry_size_limit(self):
        def f(e):
            top = next(iter(e)).split("/", 1)[0]
            e[f"{top}/huge.txt"] = b"x" * (audit.MAX_ENTRY_BYTES + 1)
            resum(e)
        self.refused_zip(f, "over the 1707380-byte limit")

    def test_total_size_limit(self):
        def f(e):
            top = next(iter(e)).split("/", 1)[0]
            full, remainder = divmod(audit.MAX_TOTAL_BYTES + 1, audit.MAX_ENTRY_BYTES)
            sizes = [audit.MAX_ENTRY_BYTES] * full + ([remainder] if remainder else [])
            for index, size in enumerate(sizes):
                e[f"{top}/huge{index}.txt"] = b"x" * size
            resum(e)
        self.refused_zip(f, f"in total, over the {audit.MAX_TOTAL_BYTES}-byte limit")

    def test_compression_ratio_limit(self):
        bad = os.path.join(self.out, "bad-bomb.zip")
        top = next(iter(zipfile.ZipFile(self.core).namelist())).split("/", 1)[0]
        with zipfile.ZipFile(self.core) as zsrc, zipfile.ZipFile(bad, "w", zipfile.ZIP_DEFLATED) as zdst:
            for i in zsrc.infolist():
                zdst.writestr(i.filename, zsrc.read(i))
            # 33:1 is the limit; 64 KiB of zeros is far past it (and under the per-entry limit).
            zdst.writestr(f"{top}/zeros.bin", b"\0" * 65536)
        code, out = run("zip", bad, "--profile", "core")
        self.assertEqual(code, 1, out)
        self.assertIn("over the 33:1 limit", out)

    # Limits accept what the real zips hold, and refuse one past it.
    def test_real_archive_is_inside_every_limit(self):
        with zipfile.ZipFile(self.core) as z:
            problems, readable = audit.structure_problems(z.infolist(), next(iter(z.namelist())).split("/", 1)[0],
                                                          pkg.safe_rel)
        self.assertEqual(problems, [])
        self.assertGreater(len(readable), 0)

    def test_entry_count_at_the_limit_is_accepted(self):
        with zipfile.ZipFile(self.core) as z:
            top = next(iter(z.namelist())).split("/", 1)[0]
            infos = list(z.infolist())
        infos += [zipfile.ZipInfo(f"{top}/pad/{i}.txt") for i in range(audit.MAX_ENTRIES - len(infos))]
        problems, _ = audit.structure_problems(infos, top, pkg.safe_rel)
        self.assertEqual(problems, [], "exactly at the entry-count limit must pass")
        problems, readable = audit.structure_problems(infos + [zipfile.ZipInfo(f"{top}/pad/one-more.txt")],
                                                      top, pkg.safe_rel)
        self.assertTrue(any("464-entry limit" in p for p in problems), problems)
        self.assertEqual(readable, {}, "nothing is read from an archive over the count limit")

    def test_entry_size_at_the_limit_is_accepted(self):
        with zipfile.ZipFile(self.core) as z:
            top = next(iter(z.namelist())).split("/", 1)[0]
            base = list(z.infolist())
        at = zipfile.ZipInfo(f"{top}/huge.txt")
        at.file_size, at.compress_size = audit.MAX_ENTRY_BYTES, audit.MAX_ENTRY_BYTES
        problems, _ = audit.structure_problems(base + [at], top, pkg.safe_rel)
        self.assertEqual(problems, [], "exactly at the per-entry limit must pass")
        over = zipfile.ZipInfo(f"{top}/huge.txt")
        over.file_size, over.compress_size = audit.MAX_ENTRY_BYTES + 1, audit.MAX_ENTRY_BYTES + 1
        problems, _ = audit.structure_problems(base + [over], top, pkg.safe_rel)
        self.assertTrue(any("1707380-byte limit" in p for p in problems), problems)

    def test_total_size_at_the_limit_is_accepted(self):
        with zipfile.ZipFile(self.core) as z:
            top = next(iter(z.namelist())).split("/", 1)[0]
            base = list(z.infolist())
        base_total = sum(i.file_size for i in base)
        room = audit.MAX_TOTAL_BYTES - base_total
        # Split across entries: one entry may not exceed the per-entry limit, so several are used.
        chunk, extra = divmod(room, audit.MAX_ENTRY_BYTES)
        pads = []
        for k in range(chunk):
            p = zipfile.ZipInfo(f"{top}/pad{k}.bin")
            p.file_size = p.compress_size = audit.MAX_ENTRY_BYTES
            pads.append(p)
        if extra:
            p = zipfile.ZipInfo(f"{top}/pad-last.bin")
            p.file_size = p.compress_size = extra
            pads.append(p)
        problems, _ = audit.structure_problems(base + pads, top, pkg.safe_rel)
        self.assertEqual(problems, [], "exactly at the total-size limit must pass")
        over = list(pads)
        over[-1].file_size = over[-1].compress_size = over[-1].file_size + 1
        problems, readable = audit.structure_problems(base + over, top, pkg.safe_rel)
        self.assertTrue(any(f"{audit.MAX_TOTAL_BYTES}-byte limit" in p for p in problems), problems)
        self.assertEqual(readable, {}, "nothing is read from an archive over the total limit")

    def test_ratio_at_the_limit_is_accepted(self):
        with zipfile.ZipFile(self.core) as z:
            top = next(iter(z.namelist())).split("/", 1)[0]
            base = list(z.infolist())
        at = zipfile.ZipInfo(f"{top}/packed.bin")
        at.file_size, at.compress_size = audit.MAX_RATIO * 1000, 1000
        problems, _ = audit.structure_problems(base + [at], top, pkg.safe_rel)
        self.assertEqual(problems, [], "exactly at the ratio limit must pass")
        over = zipfile.ZipInfo(f"{top}/packed.bin")
        over.file_size, over.compress_size = (audit.MAX_RATIO + 1) * 1000, 1000
        problems, _ = audit.structure_problems(base + [over], top, pkg.safe_rel)
        self.assertTrue(any("33:1 limit" in p for p in problems), problems)

    # Release approval (Gate G): the JSON record, its schema and its semantics.
    def base_record(self, **over):
        """A synthetic valid record; every value is fake ('a'*40 commits, 'b'*64 hashes)."""
        rec = {
            "schema_version": 1,
            "decision": "approve",
            "release_tag": "sdk-v9.9.9",
            "approved_at": "2026-10-11T00:00:00Z",
            "expires_at": "2099-01-01T00:00:00Z",
            "approver": {"identity": "maintainer", "key_fingerprint": "SHA256:" + "A" * 43},
            "source": {"repository": "scuba-sc/sco-core", "commit": "a" * 40,
                       "components_sha256": "b" * 64, "dependencies": []},
            "build": {"workflow_commit": "a" * 40, "run_id": 1, "run_attempt": 1,
                      "artifact_ids": [1], "evidence_sha256": "b" * 64},
            "profiles": ["core", "game-sc", "legacy"],
            "artifacts": [{"name": "sco-sdk-9.9.9.zip", "role": "sdk-legacy", "bytes": 4,
                           "sha256": hashlib.sha256(b"zip!").hexdigest()}],
            "distribution": {"status": "approved-for-publication", "scope": ["public"],
                             "rights_record_digest": "b" * 64, "source_plan_digest": "b" * 64,
                             "external_counsel": "not_obtained"},
            "exceptions": [],
        }
        rec.update(over)
        return rec

    def refused_release(self, record, needle, artifacts=None, tag="sdk-v9.9.9"):
        p = os.path.join(self.out, f"approval-{self.id().rsplit('.', 1)[-1]}.json")
        with open(p, "w") as f:
            json.dump(record, f)
        argv = ["release", "--approval", p, "--tag", tag]
        if artifacts:
            argv += ["--artifacts", artifacts]
        code, out = run(*argv)
        self.assertEqual(code, 1, out)
        self.assertIn(needle, out)

    def test_missing_approval_record(self):
        code, out = run("release", "--approval", os.path.join(self.out, "none.json"), "--tag", "sdk-v9.9.9")
        self.assertEqual(code, 2, out)
        self.assertIn("can't read it", out)

    def test_approval_that_is_not_json(self):
        p = os.path.join(self.out, "bad.json")
        with open(p, "w") as f:
            f.write("{ not json")
        code, out = run("release", "--approval", p, "--tag", "sdk-v9.9.9")
        self.assertEqual(code, 2, out)
        self.assertIn("not valid JSON", out)

    def test_a_valid_record_is_accepted(self):
        s = self.scratch()
        approved = audit.git_head(s.dir)
        rec = self.base_record(source={"repository": "scuba-sc/sco-core", "commit": approved,
                                       "components_sha256": "b" * 64, "dependencies": []})
        path = "docs/legal/release-decisions/sdk-v9.9.9.json"
        s.add(path, json.dumps(rec).encode())
        env = dict(os.environ, GIT_AUTHOR_NAME="t", GIT_AUTHOR_EMAIL="t@example.com",
                   GIT_COMMITTER_NAME="t", GIT_COMMITTER_EMAIL="t@example.com")
        subprocess.run(["git", "-C", s.dir, "commit", "-q", "-m", "approve"], check=True, env=env)
        code, out = run("release", "--approval", s.path(path), "--tag", "sdk-v9.9.9", root=s.dir)
        self.assertEqual(code, 0, out)

    def test_a_valid_record_checks_its_artifacts_directory(self):
        s = self.scratch()
        approved = audit.git_head(s.dir)
        d = os.path.join(self.out, "artifacts-ok")
        os.makedirs(d, exist_ok=True)
        with open(os.path.join(d, "sco-sdk-9.9.9.zip"), "wb") as f:
            f.write(b"zip!")
        rec = self.base_record(source={"repository": "scuba-sc/sco-core", "commit": approved,
                                       "components_sha256": "b" * 64, "dependencies": []})
        path = "docs/legal/release-decisions/sdk-v9.9.9.json"
        s.add(path, json.dumps(rec).encode())
        env = dict(os.environ, GIT_AUTHOR_NAME="t", GIT_AUTHOR_EMAIL="t@example.com",
                   GIT_COMMITTER_NAME="t", GIT_COMMITTER_EMAIL="t@example.com")
        subprocess.run(["git", "-C", s.dir, "commit", "-q", "-m", "approve"], check=True, env=env)
        code, out = run("release", "--approval", s.path(path), "--tag", "sdk-v9.9.9",
                        "--artifacts", d, root=s.dir)
        self.assertEqual(code, 0, out)

    def test_every_schema_keyword_is_implemented(self):
        """A keyword the validator doesn't implement would silently pass; the shipped schema must
        use only implemented ones."""
        schema = json.loads(read(os.path.join(ROOT, audit.APPROVAL_SCHEMA), "r"))
        self.assertEqual(audit.schema_keywords(schema) - audit.SCHEMA_KEYWORDS, set())

    # Each defect must be refused with its reason (the old Markdown parser accepted all of these).
    def test_empty_artifacts(self):
        self.refused_release(self.base_record(artifacts=[]), "fewer than minItems 1")

    def test_malformed_commit(self):
        self.refused_release(self.base_record(source={"repository": "scuba-sc/sco-core", "commit": "zz",
                                                       "components_sha256": "b" * 64, "dependencies": []}),
                             "does not match pattern")

    def test_repository_name_from_before_the_move_is_refused(self):
        self.refused_release(self.base_record(source={"repository": "scubamount/sco-core", "commit": "a" * 40,
                                                       "components_sha256": "b" * 64, "dependencies": []}),
                             'is not the const "scuba-sc/sco-core"')

    def test_empty_approver_identity(self):
        self.refused_release(self.base_record(approver={"identity": "", "key_fingerprint": "SHA256:" + "A" * 43}),
                             "shorter than minLength 1")

    def test_missing_approved_at(self):
        rec = self.base_record()
        del rec["approved_at"]
        self.refused_release(rec, "required property 'approved_at' is missing")

    def test_decision_approve_ish(self):
        self.refused_release(self.base_record(decision="approve-ish"), "is not one of")

    def test_decision_hold(self):
        self.refused_release(self.base_record(decision="hold"), "the decision is 'hold', not 'approve'")

    def test_distribution_internal_only(self):
        dist = self.base_record()["distribution"]
        dist["status"] = "internal-only"
        self.refused_release(self.base_record(distribution=dist),
                             "distribution.status is 'internal-only'")

    def test_expired_approval(self):
        self.refused_release(self.base_record(approved_at="2026-01-01T00:00:00Z",
                                              expires_at="2026-02-01T00:00:00Z"),
                             "the approval expired at 2026-02-01T00:00:00Z")

    def test_expired_exception(self):
        self.refused_release(self.base_record(exceptions=[{"id": "EX-1", "scope": "docs", "reason": "r",
                                                           "expires_at": "2026-02-01T00:00:00Z"}]),
                             "exceptions[0] (EX-1) expired")

    def test_approved_at_after_expires_at(self):
        self.refused_release(self.base_record(approved_at="2027-01-01T00:00:00Z",
                                              expires_at="2026-11-01T00:00:00Z"),
                             "is not before expires_at")

    def test_tag_mismatch(self):
        self.refused_release(self.base_record(), "release_tag is 'sdk-v9.9.9', but the release tag is 'sdk-v1.0.0'",
                             tag="sdk-v1.0.0")

    def test_extra_unknown_field(self):
        rec = self.base_record()
        rec["not_a_field"] = 1
        self.refused_release(rec, "unknown property 'not_a_field'")

    def test_artifact_sha_mismatch(self):
        d = os.path.join(self.out, "artifacts-bad-sha")
        os.makedirs(d, exist_ok=True)
        with open(os.path.join(d, "sco-sdk-9.9.9.zip"), "wb") as f:
            f.write(b"other")
        rec = self.base_record()
        rec["artifacts"][0]["bytes"] = 5   # the bytes match; only the sha256 is wrong
        rec["artifacts"][0]["sha256"] = "c" * 64
        self.refused_release(rec, "sha256", artifacts=d)

    def test_extra_file_in_artifacts_dir(self):
        d = os.path.join(self.out, "artifacts-extra")
        os.makedirs(d, exist_ok=True)
        with open(os.path.join(d, "sco-sdk-9.9.9.zip"), "wb") as f:
            f.write(b"zip!")
        with open(os.path.join(d, "extra.txt"), "wb") as f:
            f.write(b"x")
        self.refused_release(self.base_record(), "extra.txt: in", artifacts=d)

    def test_missing_file_in_artifacts_dir(self):
        d = os.path.join(self.out, "artifacts-missing")
        os.makedirs(d, exist_ok=True)
        self.refused_release(self.base_record(), "listed in the record but not in", artifacts=d)

    def test_duplicate_artifact_names(self):
        a = {"name": "sco-sdk-9.9.9.zip", "role": "sdk-legacy", "bytes": 4,
             "sha256": hashlib.sha256(b"zip!").hexdigest()}
        self.refused_release(self.base_record(artifacts=[a, dict(a)]),
                             "artifact name 'sco-sdk-9.9.9.zip' is listed more than once")

    def test_profile_missing_for_a_role(self):
        self.refused_release(self.base_record(profiles=["core"]),
                             "an artifact has role 'sdk-legacy', but profiles does not list 'legacy'")

    def test_approval_for_another_commit(self):
        self.refused_release(self.base_record(), "is not this commit")

    def test_approval_of_an_ancestor_with_only_the_record_added(self):
        s = self.scratch()
        env = dict(os.environ, GIT_AUTHOR_NAME="t", GIT_AUTHOR_EMAIL="t@example.com",
                   GIT_COMMITTER_NAME="t", GIT_COMMITTER_EMAIL="t@example.com")
        approved = audit.git_head(s.dir)
        path = "docs/legal/release-decisions/sdk-v9.9.9.json"
        s.add(path, json.dumps(self.base_record(source={"repository": "scuba-sc/sco-core",
                                                        "commit": approved,
                                                        "components_sha256": "b" * 64,
                                                        "dependencies": []})).encode())
        subprocess.run(["git", "-C", s.dir, "commit", "-q", "-m", "approve"], check=True, env=env)
        code, out = run("release", "--approval", s.path(path), "--tag", "sdk-v9.9.9", root=s.dir)
        self.assertEqual(code, 0, out)
        s.add("README.md", b"changed after approval\n")
        subprocess.run(["git", "-C", s.dir, "commit", "-q", "-m", "late change"], check=True, env=env)
        code, out = run("release", "--approval", s.path(path), "--tag", "sdk-v9.9.9", root=s.dir)
        self.assertEqual(code, 1, out)
        self.assertIn("changed since outside", out)


if __name__ == "__main__":
    unittest.main(verbosity=1)
