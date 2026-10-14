#!/usr/bin/env python3
"""Offline tests for tools/check_live_site.py: a local git fixture and a fake fetch, no network."""
import contextlib
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import urllib.error

sys.dont_write_bytecode = True
import check_live_site as live

GIT_ENV = {**os.environ, "GIT_CONFIG_GLOBAL": os.devnull, "GIT_CONFIG_NOSYSTEM": "1"}
IDENTITY = ("-c", "user.name=Fixture", "-c", "user.email=fixture@example.com",
            "-c", "commit.gpgsign=false", "-c", "init.defaultBranch=main")


class Repo:
    def __init__(self, root):
        self.root = Path(root)
        self.git("init", "-q")

    def git(self, *args):
        result = subprocess.run(["git", *IDENTITY, "-C", str(self.root), *args], env=GIT_ENV,
                                capture_output=True, text=True, check=True)
        return result.stdout.strip()

    def commit(self, message, version=None):
        if version is not None:
            (self.root / "sdk").mkdir(exist_ok=True)
            (self.root / "sdk/VERSION").write_text(version + "\n")
        else:
            (self.root / "notes.txt").write_text(message + "\n")
        return self.commit_all(message)

    def commit_all(self, message):
        self.git("add", "-A")
        self.git("commit", "-q", "-m", message)
        return self.git("rev-parse", "HEAD")


def catalogue(source_sha, sdk_version="2.1.0", **extra):
    return json.dumps({"schema_version": 1, "sdk_version": sdk_version,
                       "source_sha": source_sha, **extra}).encode()


class CheckLiveSiteTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory()
        repo = Repo(cls.directory.name)
        cls.repo = repo
        cls.old = repo.commit("2.0.0", version="2.0.0")
        cls.released = repo.commit("2.1.0", version="2.1.0")
        cls.recent = [repo.commit(f"change {n}") for n in range(4)]
        cls.head = cls.recent[-1]
        repo.git("checkout", "-q", "-b", "side", cls.released)
        cls.side = repo.commit("side work")
        repo.git("checkout", "-q", "-b", "no-version", cls.head)
        (repo.root / "sdk/VERSION").unlink()
        cls.no_version = repo.commit_all("drop sdk/VERSION")
        repo.git("checkout", "-q", "main")

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def run_cli(self, body, *args):
        fetched = []

        def fake_fetch(url):
            fetched.append(url)
            if isinstance(body, Exception):
                raise body
            return body

        out, err = io.StringIO(), io.StringIO()
        argv = ["--repo", str(self.repo.root), "--ref", "main", *args]
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = live.main(argv, fetch=fake_fetch)
        return code, out.getvalue(), err.getvalue().splitlines(), fetched

    def assert_passes(self, body, *args):
        code, out, err, fetched = self.run_cli(body, *args)
        self.assertEqual((code, err), (0, []))
        self.assertEqual(fetched, [live.CATALOGUE_URL])
        return out

    def assert_refused(self, token, body, *args):
        code, _, err, _ = self.run_cli(body, *args)
        self.assertEqual(code, 1)
        self.assertTrue(err and all(line.startswith("refused: ") for line in err), err)
        self.assertTrue(any(token in line for line in err), (token, err))
        return err

    def test_head_of_main_passes(self):
        out = self.assert_passes(catalogue(self.head))
        self.assertIn("0 commits behind main", out)

    def test_recent_ancestor_within_window_passes(self):
        out = self.assert_passes(catalogue(self.released), "--max-behind", "4")
        self.assertIn("4 commits behind main", out)

    def test_ancestor_beyond_window_refused(self):
        self.assert_refused("5 commits behind main", catalogue(self.old, "2.0.0"),
                            "--max-behind", "4")

    def test_commit_not_on_main_refused(self):
        self.assert_refused("not main or an ancestor", catalogue(self.side))

    def test_unknown_commit_refused(self):
        self.assert_refused("not a commit in this clone", catalogue("0" * 40))

    def test_version_mismatch_refused(self):
        self.assert_refused("sdk_version 2.2.0", catalogue(self.head, "2.2.0"))
        self.assert_refused("sdk_version 2.1.0", catalogue(self.old, "2.1.0"))

    def test_version_read_at_source_sha_not_at_head(self):
        self.assert_passes(catalogue(self.old, "2.0.0"))

    def test_missing_version_file_refused(self):
        self.assert_refused("sdk/VERSION does not exist", catalogue(self.no_version),
                            "--ref", "no-version")

    def test_malformed_catalogue_refused(self):
        cases = {b"not json": "not valid JSON",
                 b"[]": "not a JSON object",
                 catalogue(self.head[:12]): "source_sha",
                 catalogue(self.head.upper()): "source_sha",
                 json.dumps({"sdk_version": "2.1.0"}).encode(): "source_sha",
                 catalogue(self.head, "2.1"): "sdk_version",
                 json.dumps({"sdk_version": 2, "source_sha": self.head}).encode(): "sdk_version"}
        for body, token in cases.items():
            with self.subTest(token=token, body=body[:40]):
                self.assert_refused(token, body)

    def test_fetch_failure_refused(self):
        self.assert_refused("could not fetch", live.Refusal("could not fetch the catalogue"))

    def test_unknown_ref_refused(self):
        self.assert_refused("--ref", catalogue(self.head), "--ref", "no-such-branch")

    def test_argument_refusals(self):
        self.assert_refused("https", catalogue(self.head), "--url", "http://example.com/x.json")
        self.assert_refused("--max-behind", catalogue(self.head), "--max-behind", "-1")
        self.assert_refused("invalid command arguments", catalogue(self.head), "--max-behind", "x")

    def test_url_override_is_fetched(self):
        url = "https://example.com/sdk-catalogue.json"
        code, _, err, fetched = self.run_cli(catalogue(self.head), "--url", url)
        self.assertEqual((code, err, fetched), (0, [], [url]))

    def test_fetch_url_wraps_network_errors(self):
        with mock.patch.object(live.urllib.request, "urlopen",
                               side_effect=urllib.error.URLError("offline")):
            with self.assertRaisesRegex(live.Refusal, "could not fetch"):
                live.fetch_url(live.CATALOGUE_URL)

    def test_fetch_url_refuses_oversized_body(self):
        response = mock.MagicMock()
        response.__enter__.return_value.read.return_value = b"x" * (live.MAX_BYTES + 1)
        with mock.patch.object(live.urllib.request, "urlopen", return_value=response):
            with self.assertRaisesRegex(live.Refusal, "larger than"):
                live.fetch_url(live.CATALOGUE_URL)


if __name__ == "__main__":
    unittest.main()
