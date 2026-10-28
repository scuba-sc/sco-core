#!/usr/bin/env python3
"""Offline completion and delivery checks with synthetic GitHub, git and HTTP fakes."""
import contextlib
import hashlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile

sys.dont_write_bytecode = True
import release_notify as notify

REPO = "example/sdk"
TAG = "sdk-v9.9.9"
TAG_OBJECT = "f" * 40
APPROVAL = "a" * 40
SOURCE = APPROVAL
PAGES = "c" * 40
DOCS = "d" * 40
OTHER = "e" * 40
RELEASE_ID = 4242
TOKEN = "s3cr3t-token_value"
WEBHOOK = "https://discord.com/api/webhooks/123456/" + TOKEN
ORIGIN = "https://example.github.io"
NOTES_ROUTE = "/sdk/guides/releases/" + TAG.replace(".", "") + "/"
NOW = "2026-10-14T00:00:00Z"
CHANGELOG = (f"# Changelog\n\n## {TAG} (2026-10-14)\n\n"
             "Quote \"double\" and 'single' \\ backslash\n"
             "Line two @everyone <@123> `$(touch /tmp/x)` ${{ secrets.X }}\n\n"
             "## sdk-v9.9.8\n\nolder text\n")
EXPECTED_NOTES = ("Quote \"double\" and 'single' \\ backslash\n"
                  "Line two @everyone <@123> `$(touch /tmp/x)` ${{ secrets.X }}")


def archive(name, record):
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, "w") as bundle:
        bundle.writestr(name, json.dumps(record))
    return stream.getvalue()


class FakeGitHub:
    """Exact command responses; an unplanned call fails rather than reaching gh."""

    def __init__(self):
        self.responses, self.calls = {}, []

    def set(self, args, result):
        self.responses[tuple(args)] = result

    def __call__(self, *args):
        self.calls.append(args)
        if args not in self.responses:
            raise AssertionError("unexpected GitHub command: " + repr(args))
        result = self.responses[args]
        return result if isinstance(result, bytes) else json.dumps(result).encode()


class FakeGit:
    def __init__(self):
        self.responses = {}

    def set(self, args, code, out=b""):
        self.responses[tuple(args)] = (code, out)

    def __call__(self, *args):
        if args not in self.responses:
            raise AssertionError("unexpected git command: " + repr(args))
        return self.responses[args]


class FakeWeb:
    def __init__(self):
        self.pages, self.posts = {}, []
        self.post_result = (200, b'{"id": "987654321", "channel_id": "1"}')

    def get(self, url):
        return self.pages.get(url, (404, b""))

    def post(self, url, body):
        self.posts.append((url, body))
        if isinstance(self.post_result, BaseException):
            raise self.post_result
        return self.post_result


class NotifyTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.work = self.root / "work"
        self.files = {"SHA256SUMS": b"sums\n", "sco-sdk-9.9.9.zip": b"sdk zip bytes",
                      "sco-sdk-game-sc-1.2.3.zip": b"game pack bytes"}
        self.manifest = {
            "schema_version": 3, "tag": TAG,
            "source": {"repo": REPO, "sha": SOURCE, "dependencies": []},
            "artifacts": [{"name": name, "role": "zip", "size": len(data),
                           "sha256": hashlib.sha256(data).hexdigest(), "artifact_id": 9}
                          for name, data in sorted(self.files.items())],
            "profiles": ["core", "game-sc"]}
        self.manifest_bytes = json.dumps(self.manifest, indent=2).encode() + b"\n"
        self.gh, self.git, self.web = FakeGitHub(), FakeGit(), FakeWeb()
        self.release = {"id": RELEASE_ID, "tag_name": TAG, "draft": False, "prerelease": False,
                        "published_at": "2026-10-13T22:42:09Z",
                        "html_url": f"https://github.com/{REPO}/releases/tag/{TAG}"}
        self.gh.set(("api", f"repos/{REPO}/releases/tags/{TAG}"), self.release)
        self.gh.set(("api", "--paginate", "--slurp", f"repos/{REPO}/releases?per_page=100"),
                    [[self.release, {"id": 1, "tag_name": "sdk-v9.9.8", "draft": False}]])
        self.gh.set(("api", f"repos/{REPO}/git/ref/tags/{TAG}"),
                    {"ref": f"refs/tags/{TAG}", "object": {"type": "tag", "sha": TAG_OBJECT}})
        self.gh.set(("api", f"repos/{REPO}/git/tags/{TAG_OBJECT}"),
                    {"object": {"type": "commit", "sha": APPROVAL}})
        blobs = dict(self.files, **{"release.json": self.manifest_bytes})
        self.assets = []
        for index, (name, data) in enumerate(sorted(blobs.items()), start=1):
            asset = {"id": 100 + index, "name": name, "size": len(data), "state": "uploaded"}
            if index % 2:
                asset["digest"] = "sha256:" + hashlib.sha256(data).hexdigest()
            self.assets.append(asset)
            self.set_asset_bytes(asset["id"], data)
        self.gh.set(("api", "--paginate", "--slurp", f"repos/{REPO}/releases/{RELEASE_ID}/assets?per_page=100"),
                    [self.assets])
        for kind in ("receipt", "attempt"):
            self.set_artifacts(kind, [])
        self.git.set(("rev-parse", "--verify", "--quiet", f"refs/tags/{TAG}^{{commit}}"), 0, APPROVAL.encode() + b"\n")
        for path, data in (("sdk/VERSION", b"9.9.9\n"), ("CHANGELOG.md", CHANGELOG.encode())):
            self.git.set(("cat-file", "blob", f"{APPROVAL}:{path}"), 0, data)
        for sha in (PAGES, DOCS, OTHER):
            self.git.set(("cat-file", "-t", sha), 0, b"commit\n")
        self.git.set(("merge-base", "--is-ancestor", SOURCE, PAGES), 0)
        self.git.set(("merge-base", "--is-ancestor", DOCS, PAGES), 0)
        self.catalogue = {"schema_version": 1, "sdk_version": "9.9.9", "game_pack_version": "1.2.3",
                          "source_sha": PAGES}
        self.set_catalogue()
        self.web.pages[ORIGIN + "/sdk/"] = (200, b"<html>index</html>")
        self.web.pages[ORIGIN + NOTES_ROUTE] = (200, f"<html><h1>{TAG}</h1></html>".encode())
        self.env = {"GITHUB_REPOSITORY": REPO, "GITHUB_RUN_ID": "500", "GITHUB_RUN_ATTEMPT": "1",
                    "GITHUB_OUTPUT": str(self.root / "output"), "DISCORD_WEBHOOK_URL": WEBHOOK}

    def set_asset_bytes(self, asset_id, data):
        self.gh.set(("api", f"repos/{REPO}/releases/assets/{asset_id}", "-H", "Accept: application/octet-stream"), data)

    def set_catalogue(self):
        self.web.pages[ORIGIN + "/sdk/sdk-catalogue.json"] = (200, json.dumps(self.catalogue).encode())

    def set_artifacts(self, kind, artifacts):
        self.gh.set(("api", "--paginate", "--slurp",
                     f"repos/{REPO}/actions/artifacts?name=release-notify-{kind}-{TAG}&per_page=100"),
                    [{"total_count": len(artifacts), "artifacts": artifacts}])

    def prior_run(self, run_id, path=".github/workflows/release.yml", event="workflow_dispatch", branch="main"):
        self.gh.set(("api", f"repos/{REPO}/actions/runs/{run_id}"),
                    {"path": path, "event": event, "head_branch": branch,
                     "head_repository": {"full_name": REPO}})

    def artifact(self, artifact_id, kind, run_id, record=None, expired=False):
        if record is not None:
            self.gh.set(("api", f"repos/{REPO}/actions/artifacts/{artifact_id}/zip"),
                        archive(f"{kind}.json", record))
        return {"id": artifact_id, "name": f"release-notify-{kind}-{TAG}", "expired": expired,
                "workflow_run": {"id": run_id}}

    def receipt(self, run_id, outcome="delivered", **changes):
        record = {"schema": notify.RECEIPT_SCHEMA, "outcome": outcome, "tag": TAG, "release_id": RELEASE_ID,
                  "approval_sha": APPROVAL, "source_sha": SOURCE, "docs_sha": PAGES, "run_id": run_id,
                  "run_attempt": 1, "http_status": 200 if outcome == "delivered" else None,
                  "message_id": "1234" if outcome == "delivered" else None, "recorded_at": NOW}
        record.update(changes)
        return record

    def invoke(self, *argv):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = notify.main(list(argv), self.env, self.gh, self.git, self.web, now=lambda: NOW)
        self.assertNotIn(TOKEN, out.getvalue() + err.getvalue())
        return code, out.getvalue(), err.getvalue()

    def prepare(self, *extra):
        return self.invoke("prepare", "--tag", TAG, "--work", str(self.work), *extra)

    def outputs(self):
        path = Path(self.env["GITHUB_OUTPUT"])
        return dict(line.split("=", 1) for line in path.read_text().splitlines()) if path.exists() else {}

    def stored_text(self):
        return "".join(p.read_text() for p in sorted(self.work.iterdir())) if self.work.exists() else ""

    def assert_refused(self, fragment, *extra):
        code, _out, err = self.prepare(*extra)
        self.assertEqual(code, 1, err)
        self.assertIn(fragment, err)
        self.assertFalse((self.work / "attempt.json").exists())
        self.assertEqual(self.web.posts, [])

    def test_verify_reports_tag_source_and_docs_identities(self):
        code, out, err = self.invoke("verify", "--tag", TAG)
        self.assertEqual(code, 0, err)
        record = json.loads(out)
        self.assertEqual((record["approval_sha"], record["source_sha"], record["docs_sha"]), (APPROVAL, SOURCE, PAGES))
        self.assertEqual((record["release_id"], record["asset_count"]), (RELEASE_ID, 4))
        self.assertEqual((record["sdk_version"], record["game_pack_version"]), ("9.9.9", "1.2.3"))
        self.assertEqual(self.web.posts, [])

    def test_success_marks_attempt_before_one_post_and_records_receipt(self):
        code, _out, err = self.prepare()
        self.assertEqual(code, 0, err)
        self.assertEqual(self.outputs(), {"action": "send", "attempt-artifact": f"release-notify-attempt-{TAG}",
                                          "receipt-artifact": f"release-notify-receipt-{TAG}"})
        attempt = json.loads((self.work / "attempt.json").read_text())
        self.assertEqual((attempt["run_id"], attempt["approval_sha"], attempt["source_sha"], attempt["docs_sha"]),
                         (500, APPROVAL, SOURCE, PAGES))
        self.assertEqual(self.web.posts, [])
        code, out, err = self.invoke("send", "--work", str(self.work))
        self.assertEqual(code, 0, err)
        self.assertIn("987654321", out)
        self.assertEqual(len(self.web.posts), 1)
        url, body = self.web.posts[0]
        self.assertTrue(url.endswith("?wait=true"))
        payload = json.loads(body)
        self.assertEqual(payload["allowed_mentions"], {"parse": []})
        receipt = json.loads((self.work / "receipt.json").read_text())
        self.assertEqual({k: receipt[k] for k in ("outcome", "http_status", "message_id", "tag", "source_sha",
                                                  "docs_sha", "approval_sha", "run_id")},
                         {"outcome": "delivered", "http_status": 200, "message_id": "987654321", "tag": TAG,
                          "source_sha": SOURCE, "docs_sha": PAGES, "approval_sha": APPROVAL, "run_id": 500})
        self.assertNotIn(TOKEN, self.stored_text())
        self.assertNotIn("discord.com", self.stored_text())

    def test_payload_escapes_quotes_newlines_and_untrusted_markup(self):
        self.assertEqual(self.prepare()[0], 0)
        payload = json.loads((self.work / "payload.json").read_bytes())
        embed = payload["embeds"][0]
        self.assertEqual(embed["description"], EXPECTED_NOTES)
        self.assertNotIn("older text", embed["description"])
        self.assertEqual(payload["allowed_mentions"], {"parse": []})
        self.assertEqual(embed["title"], "sco-core " + TAG)
        values = [field["value"] for field in embed["fields"]]
        self.assertTrue(any(APPROVAL in value for value in values) and any(SOURCE in value for value in values))

    def test_draft_prerelease_and_wrong_tag_are_refused(self):
        for key, value, fragment in (("draft", True, "draft"), ("prerelease", True, "prerelease"),
                                     ("tag_name", "sdk-v9.9.8", "tag does not match"),
                                     ("id", "4242", "numeric ID")):
            with self.subTest(key=key):
                original = self.release[key]
                self.release[key] = value
                self.assert_refused(fragment)
                self.release[key] = original

    def test_malformed_tag_and_local_peel_mismatch_are_refused(self):
        code, _out, err = self.invoke("prepare", "--tag", "sdk-v9.9", "--work", str(self.work))
        self.assertEqual(code, 1)
        self.assertIn("sdk-vX.Y.Z", err)
        self.git.set(("rev-parse", "--verify", "--quiet", f"refs/tags/{TAG}^{{commit}}"), 0, OTHER.encode())
        self.assert_refused("tagged commit")

    def test_duplicate_release_for_tag_is_refused(self):
        self.gh.set(("api", "--paginate", "--slurp", f"repos/{REPO}/releases?per_page=100"),
                    [[self.release, {"id": 9, "tag_name": TAG, "draft": True}]])
        self.assert_refused("not unique")

    def test_asset_digest_mismatches_are_refused(self):
        zip_asset = next(a for a in self.assets if a["name"] == "sco-sdk-9.9.9.zip")
        self.set_asset_bytes(zip_asset["id"], b"sdk zip bytez")
        self.assert_refused("differs from the manifest: sco-sdk-9.9.9.zip")

    def update_manifest(self, record):
        data = json.dumps(record).encode()
        asset = next(a for a in self.assets if a["name"] == "release.json")
        asset["size"] = len(data)
        asset.pop("digest", None)
        self.set_asset_bytes(asset["id"], data)

    def test_manifest_source_and_inventory_mismatches_are_refused(self):
        self.manifest["source"]["sha"] = OTHER
        self.update_manifest(self.manifest)
        self.assert_refused("source does not match the tagged commit")
        self.manifest["source"]["sha"] = SOURCE
        self.manifest["source"]["repo"] = "other/repo"
        self.update_manifest(self.manifest)
        self.assert_refused("source repository or SHA")
        self.manifest["source"]["repo"] = REPO
        self.update_manifest(self.manifest)
        self.assets.append({"id": 999, "name": "release.json.sig", "size": 1, "state": "uploaded"})
        self.assert_refused("do not exactly match")

    def test_manifest_schema_and_duplicate_keys_are_refused(self):
        self.manifest["schema_version"] = 4
        self.update_manifest(self.manifest)
        self.assert_refused("schema or tag")
        asset = next(a for a in self.assets if a["name"] == "release.json")
        self.set_asset_bytes(asset["id"], b'{"schema_version":3,"tag":"a","tag":"b"}')
        self.assert_refused("duplicate JSON keys")

    def test_historical_release_and_receipt_are_verified(self):
        historical_source = "b" * 40
        self.manifest["schema_version"] = 2
        self.manifest["source"]["sha"] = historical_source
        self.manifest["distribution"] = {"status": "approved-for-publication"}
        self.update_manifest(self.manifest)
        manifest_bytes = json.dumps(self.manifest).encode()
        signature = b"-----BEGIN SSH SIGNATURE-----\nsynthetic\n"
        path = f"docs/legal/release-decisions/{TAG}.json"
        self.git.set(("cat-file", "blob", f"{APPROVAL}:{path}"), 0, manifest_bytes)
        self.git.set(("cat-file", "blob", f"{APPROVAL}:{path}.sig"), 0, signature)
        self.git.set(("merge-base", "--is-ancestor", historical_source, APPROVAL), 0)
        self.git.set(("merge-base", "--is-ancestor", historical_source, PAGES), 0)
        self.assets.append({"id": 999, "name": "release.json.sig", "size": len(signature), "state": "uploaded"})
        self.set_asset_bytes(999, signature)
        self.prior_run(400, event="push", branch=TAG)
        self.set_artifacts("receipt", [self.artifact(77, "receipt", 400,
                            self.receipt(400, source_sha=historical_source))])
        code, out, err = self.prepare()
        self.assertEqual(code, 0, err)
        self.assertIn("already delivered", out)
        self.assertEqual(self.outputs()["action"], "skip")
        self.assertEqual(self.web.posts, [])
        self.set_asset_bytes(999, b"changed")
        self.assert_refused("signature differs from the tagged commit")
        self.set_asset_bytes(999, signature)
        self.git.set(("merge-base", "--is-ancestor", historical_source, APPROVAL), 1)
        self.assert_refused("not an ancestor")
        self.git.set(("merge-base", "--is-ancestor", historical_source, APPROVAL), 0)
        self.git.set(("cat-file", "blob", f"{APPROVAL}:{path}"), 0, b"different")
        self.assert_refused("manifest differs from the tagged commit")

    def test_manifest_metadata_mismatch_is_refused(self):
        asset = next(a for a in self.assets if a["name"] == "release.json")
        asset["size"] += 1
        self.assert_refused("metadata differs from its bytes: release.json")

    def test_stale_or_unknown_pages_is_refused(self):
        cases = (("sdk_version", "9.9.8", "stale Pages"), ("game_pack_version", "1.2.2", "stale Pages"),
                 ("source_sha", "not-a-sha", "unknown to this checkout"))
        for key, value, fragment in cases:
            with self.subTest(key=key):
                original = self.catalogue[key]
                self.catalogue[key] = value
                self.set_catalogue()
                self.assert_refused(fragment)
                self.catalogue[key] = original
                self.set_catalogue()
        self.catalogue["source_sha"] = OTHER
        self.set_catalogue()
        self.git.set(("merge-base", "--is-ancestor", SOURCE, OTHER), 1)
        self.assert_refused("predates the release source")

    def test_docs_floor_and_routes_are_enforced(self):
        self.git.set(("merge-base", "--is-ancestor", DOCS, PAGES), 1)
        self.assert_refused("predates the required docs commit", "--docs-sha", DOCS)
        self.git.set(("merge-base", "--is-ancestor", DOCS, PAGES), 0)
        self.assert_refused("route unavailable: /sdk/guides/host-source-sc/", "--route", "/sdk/guides/host-source-sc/")
        self.assert_refused("allowlisted Pages route", "--route", "/sdk/../private/")
        self.assert_refused("allowlisted Pages route", "--route", "/elsewhere/")
        self.web.pages[ORIGIN + NOTES_ROUTE] = (200, b"<html>no tag here</html>")
        self.assert_refused("do not name the tag")
        del self.web.pages[ORIGIN + "/sdk/sdk-catalogue.json"]
        self.assert_refused("catalogue unavailable")

    def test_missing_or_malformed_secret_refuses_before_anything(self):
        for value in (None, "", "http://discord.com/api/webhooks/1/" + TOKEN,
                      "https://evil.example/api/webhooks/1/" + TOKEN):
            with self.subTest(value=value):
                self.gh.calls.clear()
                if value is None:
                    self.env.pop("DISCORD_WEBHOOK_URL", None)
                else:
                    self.env["DISCORD_WEBHOOK_URL"] = value
                self.assert_refused("nothing was sent")
                self.assertEqual(self.gh.calls, [])
                self.assertFalse(self.work.exists())

    def test_existing_valid_receipt_skips_without_attempt(self):
        self.prior_run(400)
        self.set_artifacts("receipt", [self.artifact(77, "receipt", 400, self.receipt(400))])
        self.set_artifacts("attempt", [self.artifact(76, "attempt", 400)])
        code, out, err = self.prepare()
        self.assertEqual(code, 0, err)
        self.assertIn("already delivered", out)
        self.assertEqual(self.outputs(), {"action": "skip"})
        self.assertFalse((self.work / "attempt.json").exists())
        self.assertEqual(self.web.posts, [])

    def test_receipt_from_untrusted_run_is_ignored(self):
        self.prior_run(401, ".github/workflows/test.yml", "pull_request", "fork-branch")
        self.set_artifacts("receipt", [self.artifact(78, "receipt", 401, self.receipt(401))])
        self.set_artifacts("attempt", [self.artifact(79, "attempt", 401)])
        code, out, err = self.prepare()
        self.assertEqual(code, 0, err)
        self.assertIn("ignored receipt artifact 78", out)
        self.assertEqual(self.outputs()["action"], "send")

    def test_dispatch_receipt_from_non_main_is_ignored(self):
        self.prior_run(401, branch="feature")
        self.set_artifacts("receipt", [self.artifact(78, "receipt", 401, self.receipt(401))])
        code, out, err = self.prepare()
        self.assertEqual(code, 0, err)
        self.assertIn("ignored receipt artifact 78", out)
        self.assertEqual(self.outputs()["action"], "send")

    def test_legacy_push_receipt_still_prevents_duplicate(self):
        self.prior_run(400, event="push", branch=TAG)
        self.set_artifacts("receipt", [self.artifact(77, "receipt", 400, self.receipt(400))])
        code, out, err = self.prepare()
        self.assertEqual(code, 0, err)
        self.assertIn("already delivered", out)

    def test_mismatched_receipt_identity_does_not_count_as_delivered(self):
        self.prior_run(400)
        self.set_artifacts("receipt", [self.artifact(77, "receipt", 400, self.receipt(400, release_id=1))])
        self.set_artifacts("attempt", [self.artifact(76, "attempt", 400)])
        code, _out, err = self.prepare()
        self.assertEqual(code, 3, err)
        self.assertFalse((self.work / "attempt.json").exists())

    def test_unresolved_or_expired_prior_attempt_halts(self):
        self.prior_run(400)
        self.set_artifacts("attempt", [self.artifact(76, "attempt", 400)])
        code, _out, err = self.prepare()
        self.assertEqual(code, 3)
        self.assertIn("without a definitive outcome", err)
        self.set_artifacts("receipt", [self.artifact(77, "receipt", 400, expired=True)])
        self.assertEqual(self.prepare()[0], 3)
        self.assertEqual(self.web.posts, [])

    def test_definitive_prior_failure_allows_a_fresh_attempt(self):
        self.prior_run(400, ".github/workflows/discord.yml", "workflow_dispatch", "main")
        self.set_artifacts("receipt", [self.artifact(77, "receipt", 400, self.receipt(400, "not-delivered"))])
        self.set_artifacts("attempt", [self.artifact(76, "attempt", 400)])
        code, _out, err = self.prepare()
        self.assertEqual(code, 0, err)
        self.assertEqual(self.outputs()["action"], "send")

    def send_with(self, result):
        self.assertEqual(self.prepare()[0], 0)
        self.web.post_result = result
        code, _out, err = self.invoke("send", "--work", str(self.work))
        receipt = json.loads((self.work / "receipt.json").read_text())
        return code, err, receipt

    def test_timeout_after_send_halts_without_retry(self):
        code, err, receipt = self.send_with(TimeoutError("timed out"))
        self.assertEqual(code, 3)
        self.assertIn("confirm the channel", err)
        self.assertEqual((receipt["outcome"], receipt["message_id"]), ("ambiguous", None))
        self.assertEqual(len(self.web.posts), 1)
        self.prior_run(500)
        self.set_artifacts("attempt", [self.artifact(80, "attempt", 500)])
        self.set_artifacts("receipt", [self.artifact(81, "receipt", 500, receipt)])
        self.env["GITHUB_RUN_ID"] = "501"
        self.assertEqual(self.prepare()[0], 3)
        self.assertEqual(len(self.web.posts), 1)

    def test_server_error_or_missing_message_id_is_ambiguous(self):
        for result in ((502, b"bad gateway"), (200, b"not json")):
            with self.subTest(result=result):
                code, _err, receipt = self.send_with(result)
                self.assertEqual((code, receipt["outcome"]), (3, "ambiguous"))
                self.web.posts.clear()

    def test_rate_limit_and_unreachable_are_definitive_failures(self):
        code, err, receipt = self.send_with((429, b'{"retry_after": 1}'))
        self.assertEqual((code, receipt["outcome"], receipt["http_status"]), (1, "not-delivered", 429))
        self.assertIn("rate limited", err)
        self.web.posts.clear()
        code, _err, receipt = self.send_with(notify.NotSent())
        self.assertEqual((code, receipt["outcome"]), (1, "not-delivered"))
        self.assertEqual(len(self.web.posts), 1)

    def test_payload_changed_after_marker_is_not_sent(self):
        self.assertEqual(self.prepare()[0], 0)
        (self.work / "payload.json").write_text('{"content": "@everyone"}')
        code, _out, err = self.invoke("send", "--work", str(self.work))
        self.assertEqual(code, 1)
        self.assertIn("changed after the attempt marker", err)
        self.assertEqual(self.web.posts, [])
        self.assertEqual(json.loads((self.work / "receipt.json").read_text())["outcome"], "not-delivered")

    def test_webhook_url_keeps_thread_and_forces_wait(self):
        url = notify.webhook_url(WEBHOOK + "?thread_id=5&wait=false")
        self.assertTrue(url.startswith("https://discord.com/api/webhooks/123456/"))
        self.assertTrue(url.endswith("?thread_id=5&wait=true"))
        for bad in ("https://discord.com:8443/api/webhooks/1/x", "https://user@discord.com/api/webhooks/1/x",
                    "https://discord.com/api/other/1/x"):
            with self.subTest(bad=bad), self.assertRaises(notify.Refusal) as caught:
                notify.webhook_url(bad)
            self.assertNotIn(bad, str(caught.exception))


if __name__ == "__main__":
    unittest.main()
