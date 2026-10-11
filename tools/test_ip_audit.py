#!/usr/bin/env python3
"""Tests for tools/ip_audit.py. Run: python3 tools/test_ip_audit.py (CI's ip-compliance job does).

1. The real repository and the three real SDK zips pass, and the SBOM binds the commit and zip hash.
2. Gate 7: each synthetic defect, in a scratch copy of the repository or a rebuilt zip, must make the
   audit exit non-zero with a message naming the problem. Nothing here touches the real tree.
"""
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


class Real(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.out = tempfile.mkdtemp(prefix="ipaudit-real-")
        cls.zips = {p: package(ROOT, p, os.path.join(cls.out, p)) for p in ("core", "game-sc", "legacy")}

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
            d = json.loads(read(sbom, "r"))
            zsha = hashlib.sha256(read(z)).hexdigest()
            self.assertEqual(d["packages"][0]["checksums"][0]["checksumValue"], zsha)
            self.assertIn(audit.git_head(ROOT), d["packages"][0]["downloadLocation"])
            with zipfile.ZipFile(z) as zz:
                n = len([i for i in zz.infolist() if not i.filename.endswith("/")])
            self.assertEqual(len(d["files"]), n, "the SBOM lists every archive entry")

    def test_the_sbom_is_reproducible(self):
        a, b = os.path.join(self.out, "a.json"), os.path.join(self.out, "b.json")
        run("zip", self.zips["core"], "--profile", "core", "--sbom", a)
        run("zip", self.zips["core"], "--profile", "core", "--sbom", b)
        self.assertEqual(read(a), read(b))

    def test_core_zip_checked_as_game_sc_fails(self):
        code, out = run("zip", self.zips["core"], "--profile", "game-sc")
        self.assertEqual(code, 1, out)
        self.assertIn("not selected by", out)


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
    def test_unclassified_file(self):
        s = self.scratch()
        s.edit_json(audit.PROVENANCE, lambda d: d["entries"].__setitem__(slice(None), [e for e in d["entries"] if e["path"] != "tools/**"]))
        self.refused_repo("tools/ip_audit.py: no entry in")

    def test_unknown_origin(self):
        s = self.scratch()
        s.edit_json(audit.PROVENANCE, lambda d: d["entries"][-1].__setitem__("origin", "found-somewhere"))
        code, out = run("repo", root=s.dir)
        self.assertEqual(code, 2, out)
        self.assertIn("unknown origin", out)

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

    # Release approval.
    def test_missing_approval_record(self):
        code, out = run("release", "--approval", os.path.join(self.out, "none.md"))
        self.assertEqual(code, 2, out)
        self.assertIn("can't read it", out)

    def test_approval_for_another_commit(self):
        p = os.path.join(self.out, "approval.md")
        with open(p, "w") as f:
            f.write("Decision: approve\nApproved by: maintainer\nDate: 2026-10-11\nCommit: " + "a" * 40 + "\n")
        code, out = run("release", "--approval", p)
        self.assertEqual(code, 1, out)
        self.assertIn("approves aaaa", out)

    def test_approval_of_an_ancestor_with_only_the_record_added(self):
        s = self.scratch()
        env = dict(os.environ, GIT_AUTHOR_NAME="t", GIT_AUTHOR_EMAIL="t@example.com",
                   GIT_COMMITTER_NAME="t", GIT_COMMITTER_EMAIL="t@example.com")
        approved = audit.git_head(s.dir)
        rec = "docs/legal/release-decisions/sdk-v9.9.9.md"
        s.add(rec, f"Decision: approve\nApproved by: maintainer\nDate: 2026-10-11\nCommit: {approved}\n".encode())
        subprocess.run(["git", "-C", s.dir, "commit", "-q", "-m", "approve"], check=True, env=env)
        code, out = run("release", "--approval", s.path(rec), root=s.dir)
        self.assertEqual(code, 0, out)
        s.add("README.md", b"changed after approval\n")
        subprocess.run(["git", "-C", s.dir, "commit", "-q", "-m", "late change"], check=True, env=env)
        code, out = run("release", "--approval", s.path(rec), root=s.dir)
        self.assertEqual(code, 1, out)
        self.assertIn("changed since outside", out)

    def test_approval_that_is_not_approve(self):
        p = os.path.join(self.out, "pending.md")
        with open(p, "w") as f:
            f.write(f"Decision: pending\nApproved by: -\nDate: -\nCommit: {audit.git_head(ROOT)}\n")
        code, out = run("release", "--approval", p)
        self.assertEqual(code, 1, out)
        self.assertIn("isn't 'approve'", out)


if __name__ == "__main__":
    unittest.main(verbosity=1)
