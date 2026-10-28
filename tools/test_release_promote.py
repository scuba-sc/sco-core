#!/usr/bin/env python3
"""Offline promotion checks with synthetic public-safe GitHub fixtures."""
import contextlib
import copy
import hashlib
import io
import stat
import warnings
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock
import zipfile

sys.dont_write_bytecode = True
import release_promote as promote
import test_release_verify as verify_tests

REPO = "example/sdk"
SHA = "a" * 40
TAG = "sdk-v9.9.9"


def archive(members):
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, "w") as bundle:
        for name, data in members:
            bundle.writestr(name, data)
    return stream.getvalue()


class FakeGitHub:
    """Exact command responses; an unplanned call fails rather than reaching gh."""
    def __init__(self):
        self.responses = {}
        self.calls = []

    def add(self, args, *results):
        self.responses[tuple(args)] = list(results)

    def __call__(self, *args):
        self.calls.append(args)
        if args not in self.responses or not self.responses[args]:
            raise AssertionError("unexpected GitHub command: " + repr(args))
        result = self.responses[args].pop(0)
        return result if isinstance(result, bytes) else json.dumps(result).encode()


class PromotionTests(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(promote, "promotion module has not been extracted")
        blocker = mock.patch.object(promote.subprocess, "check_output",
                                    side_effect=AssertionError("real GitHub I/O forbidden in tests"))
        blocker.start()
        self.addCleanup(blocker.stop)
        output = contextlib.redirect_stdout(io.StringIO())
        output.__enter__()
        self.addCleanup(output.__exit__, None, None, None)
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.github = FakeGitHub()
        self.approval = {"source": {"sha": SHA}, "builder": {"run_id": 123, "run_attempt": 1, "workflow_path": ".github/workflows/release-candidate.yml"}, "artifacts": [
            {"name": "sdk.zip", "artifact_id": 456},
            {"name": "sdk.spdx.json", "artifact_id": 456},
        ]}
        self.builder_run = {"path": ".github/workflows/release-candidate.yml", "event": "workflow_dispatch",
                    "head_branch": "main", "head_sha": SHA, "run_attempt": 1, "status": "completed", "conclusion": "success"}
        self.metadata = {"expired": False, "workflow_run": {"id": 123}}
        self.members = [("sdk.zip", b"archive"), ("sdk.spdx.json", b"sbom")]

    def prerequisite_fixtures(self):
        checks, runs = [], {}
        required = {"cmake": ("linux", "kernel-only", "windows"),
                    "sdk": ("package", "windows", "csharp-host-sim"),
                    "test": ("test",), "ip-compliance": ("audit",),
                    "docs-site": ("docs-site",)}
        for run_id, (workflow, jobs) in enumerate(required.items(), 200):
            runs[str(run_id)] = {"path": f".github/workflows/{workflow}.yml", "head_sha": SHA,
                                 "event": "push", "head_branch": "main", "status": "completed",
                                 "conclusion": "success"}
            for job in jobs:
                checks.append({"id": len(checks) + 1, "name": job, "app": {"slug": "github-actions"},
                               "head_sha": SHA, "details_url": f"https://github.com/{REPO}/actions/runs/{run_id}/job/1",
                               "status": "completed", "conclusion": "success"})
        return checks, runs

    def prerequisites(self, checks, runs, ref="refs/heads/main", configure_only=False):
        self.github.add(("api", "--paginate", "--slurp",
                         f"repos/{REPO}/commits/{SHA}/check-runs?filter=latest&per_page=100"),
                        [{"check_runs": checks[:4]}, {"check_runs": checks[4:]}])
        for run_id, run in runs.items():
            self.github.add(("api", f"repos/{REPO}/actions/runs/{run_id}"), run)
        if not configure_only:
            promote.require_prerequisites(REPO, SHA, ref, self.github)

    def test_prerequisites_all_exact_sha_workflows_succeed(self):
        self.assertTrue(callable(getattr(promote, "require_prerequisites", None)),
                        "prerequisite validation has not been extracted")
        self.prerequisites(*self.prerequisite_fixtures())
        self.assertEqual(len(self.github.calls), 6)  # Each workflow run is fetched only once.

    def docs_site_fixture(self):
        checks, runs = self.prerequisite_fixtures()
        run_id = next(key for key, run in runs.items()
                      if run["path"] == ".github/workflows/docs-site.yml")
        return checks, runs, run_id

    def test_prerequisites_docs_site_build_missing(self):
        checks, runs, run_id = self.docs_site_fixture()
        checks = [check for check in checks if f"/runs/{run_id}/" not in check["details_url"]]
        self.assert_prerequisites_refused(checks, runs, "docs-site/docs-site")

    def test_prerequisites_docs_site_built_from_other_sha(self):
        checks, runs, run_id = self.docs_site_fixture()
        runs[run_id]["head_sha"] = "b" * 40
        self.assert_prerequisites_refused(checks, runs, "docs-site/docs-site")

    def test_prerequisites_docs_site_pull_request_build_does_not_count(self):
        checks, runs, run_id = self.docs_site_fixture()
        runs[run_id]["event"] = "pull_request"
        self.assert_prerequisites_refused(checks, runs, "docs-site/docs-site")


    def assert_prerequisites_refused(self, checks, runs, key="cmake/linux"):
        with self.assertRaisesRegex(promote.Refusal, key):
            self.prerequisites(checks, runs)

    def prerequisite_job_conclusion(self, conclusion):
        checks, runs = self.prerequisite_fixtures()
        checks[0]["conclusion"] = conclusion
        self.assert_prerequisites_refused(checks, runs)

    def test_prerequisites_skipped_job(self):
        self.prerequisite_job_conclusion("skipped")

    def test_prerequisites_cancelled_job(self):
        self.prerequisite_job_conclusion("cancelled")

    def test_prerequisites_neutral_job(self):
        self.prerequisite_job_conclusion("neutral")

    def test_prerequisites_failed_job(self):
        self.prerequisite_job_conclusion("failure")

    def test_prerequisites_missing_conclusion(self):
        self.prerequisite_job_conclusion(None)

    def test_prerequisites_missing_job(self):
        checks, runs = self.prerequisite_fixtures()
        self.assert_prerequisites_refused(checks[1:], runs)

    def test_prerequisites_unfinished_job(self):
        checks, runs = self.prerequisite_fixtures()
        checks[0]["status"] = "in_progress"
        self.assert_prerequisites_refused(checks, runs)

    def test_prerequisites_wrong_check_sha(self):
        checks, runs = self.prerequisite_fixtures()
        checks[0]["head_sha"] = "b" * 40
        self.assert_prerequisites_refused(checks, runs)

    def test_prerequisites_wrong_run_sha(self):
        checks, runs = self.prerequisite_fixtures()
        runs["200"]["head_sha"] = "b" * 40
        self.assert_prerequisites_refused(checks, runs)

    def test_prerequisites_partial_job_success_in_failed_workflow(self):
        checks, runs = self.prerequisite_fixtures()
        runs["200"]["conclusion"] = "failure"
        self.assert_prerequisites_refused(checks, runs)

    def test_prerequisites_unfinished_workflow(self):
        checks, runs = self.prerequisite_fixtures()
        runs["200"]["status"] = "in_progress"
        self.assert_prerequisites_refused(checks, runs)

    def test_prerequisites_skipped_cancelled_neutral_workflow(self):
        for conclusion in ("skipped", "cancelled", "neutral", None):
            with self.subTest(conclusion=conclusion):
                checks, runs = self.prerequisite_fixtures()
                runs["200"]["conclusion"] = conclusion
                self.assert_prerequisites_refused(checks, runs)

    def test_prerequisites_wrong_workflow_does_not_satisfy_same_named_job(self):
        checks, runs = self.prerequisite_fixtures()
        runs["200"]["path"] = ".github/workflows/other.yml"
        self.assert_prerequisites_refused(checks, runs)

    def test_prerequisites_jobs_cannot_cross_workflow_boundaries(self):
        checks, runs = self.prerequisite_fixtures()
        # Both cmake and sdk own 'windows', but one cannot stand in for the other.
        checks = [check for check in checks if check["id"] != 5]
        self.assert_prerequisites_refused(checks, runs, "sdk/windows")

    def test_prerequisites_wrong_event(self):
        checks, runs = self.prerequisite_fixtures()
        runs["200"]["event"] = "pull_request"
        self.assert_prerequisites_refused(checks, runs)

    def test_prerequisites_wrong_branch(self):
        checks, runs = self.prerequisite_fixtures()
        runs["200"]["head_branch"] = "feature"
        self.assert_prerequisites_refused(checks, runs)

    def test_prerequisites_candidate_not_on_main(self):
        with self.assertRaisesRegex(promote.Refusal, "built from main"):
            self.prerequisites(*self.prerequisite_fixtures(), ref="refs/heads/feature")
        self.assertEqual(self.github.calls, [])

    def test_prerequisites_wrong_app(self):
        checks, runs = self.prerequisite_fixtures()
        checks[0]["app"]["slug"] = "other-app"
        self.assert_prerequisites_refused(checks, runs)

    def test_prerequisites_unattributable_check(self):
        for details in (None, "", "https://example.invalid/job/1"):
            with self.subTest(details=details):
                checks, runs = self.prerequisite_fixtures()
                checks[0]["details_url"] = details
                self.assert_prerequisites_refused(checks, runs)

    def test_prerequisites_newer_failure_overrides_older_success(self):
        checks, runs = self.prerequisite_fixtures()
        newer = dict(checks[0], id=999, conclusion="failure")
        checks.insert(0, newer)
        self.assert_prerequisites_refused(checks, runs)

    def test_prerequisites_newer_success_overrides_older_failure(self):
        checks, runs = self.prerequisite_fixtures()
        newer = dict(checks[0], id=999)
        checks[0]["conclusion"] = "failure"
        checks.insert(0, newer)
        self.prerequisites(checks, runs)

    def draft_fixtures(self):
        self.payload = self.root / "payload"
        self.payload.mkdir()
        self.runner = self.root / "runner"
        self.runner.mkdir()
        self.manifest = self.root / "approval.json"
        self.manifest.write_text(json.dumps(self.approval) + "\n")
        self.asset_bytes = dict(self.members)
        for name, data in self.members:
            (self.payload / name).write_bytes(data)
        self.asset_bytes.update({"release.json": self.manifest.read_bytes()})
        self.assets = [{"id": i, "name": name, "size": len(data), "state": "uploaded",
                        "updated_at": "2026-01-01T00:00:00Z", "digest": "sha256:" + hashlib.sha256(data).hexdigest()}
                       for i, (name, data) in enumerate(self.asset_bytes.items(), 700)]
        self.draft = {"id": 600, "tag_name": TAG, "draft": True, "prerelease": False}
        self.releases_command = ("api", "--paginate", "--slurp", f"repos/{REPO}/releases?per_page=100")
        self.assets_command = ("api", "--paginate", "--slurp", f"repos/{REPO}/releases/600/assets?per_page=100")
        self.create_command = ("release", "create", TAG,
                               *[str(self.payload / name) for name, _ in self.members],
                               str(self.runner / "release-inventory/release.json"),
                               "--repo", REPO, "--draft", "--verify-tag", "--title", "sco-core SDK 9.9.9",
                               "--notes-file", str(self.runner / "notes.md"))

    def create_draft(self, before=None, after=None, configure_only=False):
        self.github.add(self.releases_command, [[]] if before is None else before,
                        [[self.draft]] if after is None else after)
        self.github.add(self.create_command, b"")
        self.github.add(self.assets_command, [self.assets[:2], self.assets[2:]])
        for asset in self.assets:
            self.github.add(("api", f"repos/{REPO}/releases/assets/{asset['id']}",
                             "-H", "Accept: application/octet-stream"),
                            self.asset_bytes[asset["name"]])
        if not configure_only:
            return promote.create_draft(REPO, TAG, self.manifest, self.payload,
                                        self.runner, self.root / "downloaded", "9.9.9", self.github)

    def test_draft_exact_assets_round_trip(self):
        self.assertTrue(callable(getattr(promote, "create_draft", None)),
                        "draft promotion has not been extracted")
        self.draft_fixtures()
        self.assertEqual(self.create_draft(), 600)
        self.assertEqual({p.name: p.read_bytes() for p in (self.root / "downloaded").iterdir()},
                         dict(self.members))
        self.assertEqual(json.loads((self.runner / "draft-assets.json").read_text()), self.assets)
        self.assertIn(self.create_command, self.github.calls)


    def assert_draft_refused(self, message, **kwargs):
        with self.assertRaisesRegex(promote.Refusal, message):
            self.create_draft(**kwargs)
        self.assertFalse(any(args[:2] == ("release", "edit") for args in self.github.calls))

    def test_rc_draft_and_publish_preserve_prerelease(self):
        self.draft_fixtures()
        rc = TAG + "-rc.2"
        self.draft.update(tag_name=rc, prerelease=True)
        self.create_command = tuple("sco-core SDK 9.9.9-rc.2" if part == "sco-core SDK 9.9.9" else part for part in (self.create_command[0], self.create_command[1], rc, *self.create_command[3:], "--prerelease"))
        self.create_draft(configure_only=True)
        self.assertEqual(promote.create_draft(REPO, rc, self.manifest, self.payload,
                                             self.runner, self.root / "downloaded", "9.9.9", self.github), 600)
        self.publication(tag=rc, published=dict(self.draft, draft=False, html_url="https://example.invalid/rc"))

    def test_wrong_prerelease_state_refused(self):
        self.draft_fixtures()
        self.assert_draft_refused("unique expected draft", after=[[dict(self.draft, prerelease=True)]])

    def test_draft_existing_draft_refused_before_create(self):
        self.draft_fixtures()
        self.assert_draft_refused("already exists", before=[[], [self.draft]])
        self.assertNotIn(self.create_command, self.github.calls)

    def test_draft_existing_published_release_refused_before_create(self):
        self.draft_fixtures()
        self.assert_draft_refused("already exists", before=[[dict(self.draft, draft=False)]])
        self.assertNotIn(self.create_command, self.github.calls)

    def test_draft_missing_after_create(self):
        self.draft_fixtures()
        self.assert_draft_refused("unique expected draft", after=[[]])

    def test_draft_duplicate_tag_after_create(self):
        self.draft_fixtures()
        self.assert_draft_refused("unique expected draft", after=[[self.draft], [dict(self.draft, id=601)]])

    def test_draft_not_draft_after_create(self):
        self.draft_fixtures()
        self.assert_draft_refused("unique expected draft", after=[[dict(self.draft, draft=False)]])

    def test_draft_extra_asset(self):
        self.draft_fixtures()
        self.assets.append(dict(self.assets[0], id=999, name="extra.zip"))
        self.asset_bytes["extra.zip"] = b"extra"
        self.assert_draft_refused("exactly match")

    def test_draft_missing_asset(self):
        self.draft_fixtures()
        self.assets.pop()
        self.assert_draft_refused("exactly match")

    def test_draft_duplicate_asset_name(self):
        self.draft_fixtures()
        self.assets[-1] = dict(self.assets[0], id=999)
        self.assert_draft_refused("exactly match")

    def test_draft_unsafe_asset_name(self):
        self.draft_fixtures()
        self.assets[0]["name"] = "../escape.zip"
        self.asset_bytes["../escape.zip"] = b"extra"
        self.assert_draft_refused("exactly match")
        self.assertFalse((self.root / "escape.zip").exists())

    def test_draft_incomplete_upload(self):
        self.draft_fixtures()
        self.assets[0]["state"] = "starter"
        self.assert_draft_refused("upload is incomplete")
        self.assertFalse(any("releases/assets/" in args[1] for args in self.github.calls if args[0] == "api"))

    def test_draft_manifest_bytes_mismatch(self):
        self.draft_fixtures()
        # Even JSON-equivalent bytes must match the verified inventory.
        self.asset_bytes["release.json"] = json.dumps(self.approval, indent=2).encode()
        self.assert_draft_refused("inventory differs")

    def publication(self, releases=None, assets=None, published=None, configure_only=False,
                    tag=TAG, post_assets=None, before_tag=SHA, after_tag=SHA, post_releases=None):
        (self.runner / "draft-assets.json").write_text(json.dumps(self.assets))
        released = dict(self.draft, draft=False, html_url=f"https://github.com/{REPO}/releases/tag/{tag}") if published is None else published
        self.github.add(self.releases_command, [[self.draft]] if releases is None else releases,
                        [[released]] if post_releases is None else post_releases)
        self.github.add(self.assets_command, [self.assets] if assets is None else assets,
                        [self.assets] if post_assets is None else post_assets)
        self.edit_command = ("api", "--method", "PATCH", f"repos/{REPO}/releases/600", "-F", "draft=false")
        self.github.add(self.edit_command, released)
        self.github.add(("api", f"repos/{REPO}/git/ref/tags/{tag}"),
                        {"object": {"type": "commit", "sha": before_tag}},
                        {"object": {"type": "commit", "sha": after_tag}})
        self.github.add(("api", f"repos/{REPO}/releases/600"), released)
        if not configure_only:
            return promote.publish_draft(REPO, tag, 600, SHA, self.runner, self.github)

    def test_publish_same_verified_draft(self):
        self.assertTrue(callable(getattr(promote, "publish_draft", None)),
                        "publication has not been extracted")
        self.draft_fixtures()
        self.assertEqual(self.publication(), f"https://github.com/{REPO}/releases/tag/{TAG}")
        self.assertIn(self.edit_command, self.github.calls)
        self.assertEqual(self.github.calls[-1], self.releases_command)


    def assert_publish_refused(self, message, **kwargs):
        with self.assertRaisesRegex(promote.Refusal, message):
            self.publication(**kwargs)
        self.assertNotIn(self.edit_command, self.github.calls)

    def test_publish_wrong_prerelease_state_refused(self):
        self.draft_fixtures()
        self.assert_publish_refused("draft changed", releases=[[dict(self.draft, prerelease=True)]])

    def test_publish_readback_wrong_prerelease_state_refused(self):
        self.draft_fixtures()
        with self.assertRaisesRegex(promote.Refusal, "was not published"):
            self.publication(published=dict(self.draft, draft=False, prerelease=True))

    def test_publish_missing_draft(self):
        self.draft_fixtures()
        self.assert_publish_refused("draft changed or its tag is ambiguous", releases=[[]])

    def test_publish_replaced_draft_id(self):
        self.draft_fixtures()
        self.assert_publish_refused("draft changed or its tag is ambiguous", releases=[[dict(self.draft, id=601)]])

    def test_publish_duplicate_release_tag(self):
        self.draft_fixtures()
        self.assert_publish_refused("draft changed or its tag is ambiguous",
                                    releases=[[self.draft], [dict(self.draft, id=601)]])

    def test_publish_already_published_draft(self):
        self.draft_fixtures()
        self.assert_publish_refused("draft changed or its tag is ambiguous",
                                    releases=[[dict(self.draft, draft=False)]])

    def test_publish_draft_tag_changed(self):
        self.draft_fixtures()
        self.assert_publish_refused("draft changed or its tag is ambiguous",
                                    releases=[[dict(self.draft, tag_name="sdk-v0.0.0")]])

    def changed_inventory(self, field, value):
        self.draft_fixtures()
        changed = copy.deepcopy(self.assets)
        changed[0][field] = value
        self.assert_publish_refused("inventory changed after verification", assets=[changed])

    def test_publish_asset_id_changed(self):
        self.changed_inventory("id", 999)

    def test_publish_asset_name_changed(self):
        self.changed_inventory("name", "other.zip")

    def test_publish_asset_size_changed(self):
        self.changed_inventory("size", 999)

    def test_publish_asset_state_changed(self):
        self.changed_inventory("state", "starter")

    def test_publish_asset_digest_changed(self):
        self.changed_inventory("digest", "sha256:" + "0" * 64)

    def test_publish_asset_updated_at_changed(self):
        self.changed_inventory("updated_at", "2026-02-01T00:00:00Z")

    def test_publish_asset_removed(self):
        self.draft_fixtures()
        self.assert_publish_refused("inventory changed after verification", assets=[self.assets[:-1]])

    def test_publish_asset_added(self):
        self.draft_fixtures()
        self.assert_publish_refused("inventory changed after verification",
                                    assets=[self.assets + [dict(self.assets[0], id=999, name="extra.zip")]])

    def test_publish_asset_reordered_is_unchanged(self):
        self.draft_fixtures()
        self.publication(assets=[list(reversed(self.assets))])
        self.assertIn(self.edit_command, self.github.calls)

    def test_publish_readback_still_draft_is_refused(self):
        self.draft_fixtures()
        with self.assertRaisesRegex(promote.Refusal, "was not published"):
            self.publication(published=self.draft)
        self.assertIn(self.edit_command, self.github.calls)

    def test_publish_readback_wrong_tag_is_refused(self):
        self.draft_fixtures()
        with self.assertRaisesRegex(promote.Refusal, "was not published"):
            self.publication(published=dict(self.draft, draft=False, tag_name="sdk-v0.0.0"))

    def test_publish_refuses_remote_tag_mismatch_before_patch(self):
        self.draft_fixtures()
        self.assert_publish_refused("remote tag does not match", before_tag="b" * 40)

    def test_publish_detects_tag_move_during_patch(self):
        self.draft_fixtures()
        with self.assertRaisesRegex(promote.Refusal, "remote tag changed.*may already be public"):
            self.publication(after_tag="b" * 40)
        self.assertIn(self.edit_command, self.github.calls)

    def test_publish_detects_asset_changes_during_patch(self):
        for field, value in (("id", 999), ("digest", "sha256:" + "0" * 64),
                             ("size", 999), ("updated_at", "2026-02-01T00:00:00Z")):
            with self.subTest(field=field):
                if not hasattr(self, 'assets'):
                    self.draft_fixtures()
                changed = copy.deepcopy(self.assets)
                changed[0][field] = value
                with self.assertRaisesRegex(promote.Refusal, "release assets changed.*may already be public"):
                    self.publication(post_assets=[changed])
                self.assertIn(self.edit_command, self.github.calls)
                self.github = FakeGitHub()

    def test_publish_detects_duplicate_release_after_patch(self):
        self.draft_fixtures()
        published = dict(self.draft, draft=False)
        with self.assertRaisesRegex(promote.Refusal, "no longer unique.*may already be public"):
            self.publication(post_releases=[[published, dict(published, id=601)]])
        self.assertIn(self.edit_command, self.github.calls)

    def test_annotated_remote_tag_peeling_and_replacement(self):
        sha = "c" * 40
        self.github.add(("api", f"repos/{REPO}/git/ref/tags/{TAG}"), {"object": {"type": "tag", "sha": sha}})
        self.github.add(("api", f"repos/{REPO}/git/tags/{sha}"), {"object": {"type": "commit", "sha": SHA}})
        self.assertEqual(promote.remote_tag(REPO, TAG, self.github), (("tag", sha), SHA))

    def test_publish_reports_uncertain_patch_outcome(self):
        self.draft_fixtures()
        self.publication(configure_only=True)
        github = self.github
        def interrupted(*args):
            if args == self.edit_command:
                raise promote.Refusal("GitHub command unavailable")
            return github(*args)
        with self.assertRaisesRegex(promote.Refusal, "may already be public; investigate immediately"):
            promote.publish_draft(REPO, TAG, 600, SHA, self.runner, interrupted)

    def test_changed_annotated_tag_object_with_same_commit_refused(self):
        self.draft_fixtures()
        self.publication(configure_only=True)
        first, second = 'b' * 40, 'c' * 40
        self.github.add(("api", f"repos/{REPO}/git/ref/tags/{TAG}"),
                        {"object": {"type": "tag", "sha": first}},
                        {"object": {"type": "tag", "sha": second}})
        for tag_sha in (first, second):
            self.github.add(("api", f"repos/{REPO}/git/tags/{tag_sha}"),
                            {"object": {"type": "commit", "sha": SHA}})
        with self.assertRaisesRegex(promote.Refusal, "remote tag changed.*may already be public"):
            promote.publish_draft(REPO, TAG, 600, SHA, self.runner, self.github)

    def test_candidate_summary_inventory(self):
        self.assertTrue(callable(getattr(promote, "candidate_summary", None)),
                        "candidate summary has not been extracted")
        dist = self.root / "dist"
        dist.mkdir()
        files = {"sdk.zip": b"archive", "sdk.spdx.json": b"sbom", "SHA256SUMS": b"hashes\n"}
        for name, data in files.items():
            (dist / name).write_bytes(data)
        summary = self.root / "summary"
        summary.write_text("Existing summary\n")
        promote.candidate_summary(summary, dist, TAG, SHA, "123", "456")
        content = summary.read_text()
        self.assertTrue(content.startswith("Existing summary\n## Candidate for " + TAG))
        self.assertIn(f"Source SHA: `{SHA}`", content)
        self.assertIn("builder.run_id: `123`", content)
        self.assertIn("`456`", content)
        for name, data in files.items():
            self.assertIn(f"| {name} | {len(data)} | `{hashlib.sha256(data).hexdigest()}` |", content)
        self.assertLess(content.index("| SHA256SUMS |"), content.index("| sdk.spdx.json |"))


    def cli_environment(self):
        return {"GITHUB_REPOSITORY": REPO, "GITHUB_REF_NAME": TAG, "GITHUB_SHA": SHA,
                "GITHUB_REF": "refs/heads/main", "GITHUB_OUTPUT": str(self.root / "output"),
                "GITHUB_ENV": str(self.root / "env"), "RUNNER_TEMP": str(self.root / "runner"),
                "VERSION": "9.9.9", "DRAFT_RELEASE_ID": "600", "RELEASE_TAG": TAG,
                "GITHUB_RUN_ID": "123", "ARTIFACT_ID": "456",
                "GITHUB_STEP_SUMMARY": str(self.root / "summary")}

    def test_cli_prerequisites(self):
        self.prerequisites(*self.prerequisite_fixtures(), configure_only=True)
        self.assertEqual(promote.main(["prerequisites"], self.cli_environment(), self.github), 0)
        self.assertEqual(len(self.github.calls), 6)

    def test_cli_draft_records_id_only_after_round_trip(self):
        self.draft_fixtures()
        (self.root / "release.json").write_bytes(self.manifest.read_bytes())
        # Workflows use relative payload paths, and an absolute runner temporary path.
        paths = {str(self.payload / name): str(Path("payload") / name) for name, _ in self.members}
        self.create_command = tuple(paths.get(arg, arg) for arg in self.create_command)
        self.create_draft(configure_only=True)
        (self.root / "env").write_text("existing=kept\n")
        with contextlib.chdir(self.root):
            self.assertEqual(promote.main(["draft"], self.cli_environment(), self.github), 0)
        self.assertEqual((self.root / "env").read_text(), "existing=kept\nDRAFT_RELEASE_ID=600\n")
        self.assertEqual((self.root / "downloaded/sdk.zip").read_bytes(), b"archive")

    def test_cli_publish_prints_verified_numeric_id_readback_url(self):
        self.draft_fixtures()
        self.publication(configure_only=True)
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            self.assertEqual(promote.main(["publish"], self.cli_environment(), self.github), 0)
        self.assertEqual(output.getvalue(), f"https://github.com/{REPO}/releases/tag/{TAG}\n")
        self.assertEqual(self.github.calls[-1], self.releases_command)

    def test_cli_summary(self):
        dist = self.root / "dist"
        dist.mkdir()
        (dist / "sdk.zip").write_bytes(b"archive")
        with contextlib.chdir(self.root):
            self.assertEqual(promote.main(["summary"], self.cli_environment(), self.github), 0)
        self.assertIn(f"## Candidate for {TAG}", (self.root / "summary").read_text())
        self.assertEqual(self.github.calls, [])

    def test_cli_missing_environment_is_refused_without_values(self):
        error = io.StringIO()
        with contextlib.redirect_stderr(error):
            self.assertEqual(promote.main(["fetch"], {}, self.github), 1)
        self.assertEqual(error.getvalue(), "refused: promotion input unavailable\n")
        self.assertEqual(self.github.calls, [])

    def test_cli_invalid_command_is_refused_without_echo(self):
        error = io.StringIO()
        with contextlib.redirect_stderr(error):
            self.assertEqual(promote.main(["untrusted-input"], {}, self.github), 1)
        self.assertEqual(error.getvalue(), "refused: invalid command arguments\n")

    def test_github_client_uses_binary_boundary(self):
        with mock.patch.object(promote.subprocess, "check_output", return_value=b"binary data") as command:
            self.assertEqual(promote.github("api", "endpoint"), b"binary data")
            command.assert_called_once_with(["gh", "api", "endpoint"], stderr=promote.subprocess.PIPE)

    def test_github_client_scrubs_subprocess_failure(self):
        failure = promote.subprocess.CalledProcessError(1, ["gh", "untrusted"],
                                                       stderr=b"sensitive-output")
        with mock.patch.object(promote.subprocess, "check_output", side_effect=failure):
            with self.assertRaisesRegex(promote.Refusal, "^GitHub command unavailable$"):
                promote.github("api", "endpoint")


class CandidateFetchTests(unittest.TestCase):
    def setUp(self):
        self.fixture = verify_tests.ReleaseVerifyTests('test_valid_unsigned_inventory')
        self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)
        self.github = FakeGitHub()
        self.run = {'path': promote.verify.WORKFLOW, 'event': 'workflow_dispatch',
                    'head_branch': 'main', 'head_sha': self.fixture.sha, 'run_attempt': 1,
                    'status': 'completed', 'conclusion': 'success'}
        self.members = [(p.name, p.read_bytes()) for p in self.fixture.payload.iterdir()]
        self.metadata = {'name': 'release-candidate', 'expired': False, 'workflow_run': {'id': 123}}

    def fetch(self, data=None):
        data = archive(self.members) if data is None else data
        self.metadata.setdefault('size_in_bytes', len(data))
        self.metadata.setdefault('digest', 'sha256:' + hashlib.sha256(data).hexdigest())
        self.github.add(('api', f'repos/{REPO}/actions/runs/123'), self.run)
        self.github.add(('api', f'repos/{REPO}/actions/artifacts/456'), self.metadata)
        self.github.add(('api', f'repos/{REPO}/actions/artifacts/456/zip'), data)
        with contextlib.chdir(self.fixture.root):
            return promote.fetch_candidate(REPO, TAG, self.fixture.sha, 123, 456,
                                           self.fixture.root / 'candidate', self.fixture.manifest, self.github)

    def test_valid_unsigned_download_and_inventory(self):
        self.assertEqual(self.fetch(), 123)
        self.fixture.payload = self.fixture.root / 'candidate'
        self.fixture.record = json.loads(self.fixture.manifest.read_text())
        self.assertEqual(self.fixture.run_verify().returncode, 0)
        self.assertEqual(self.fixture.record['builder']['run_attempt'], 1)
        self.assertFalse((self.fixture.root / 'release.json.sig').exists())

    def test_run_identity_and_failed_build_refused(self):
        for field, value in [('path', 'other'), ('event', 'push'), ('head_branch', 'other'),
                             ('head_sha', 'a' * 40), ('run_attempt', 2),
                             ('status', 'in_progress'), ('conclusion', 'failure')]:
            with self.subTest(field=field):
                original = self.run[field]
                self.run[field] = value
                with self.assertRaises(promote.Refusal): self.fetch()
                self.assertFalse(any(c[-1].endswith('/zip') for c in self.github.calls))
                self.github = FakeGitHub()
                self.run[field] = original

    def test_artifact_identity_and_integrity_refused(self):
        for field, value in [('expired', True), ('name', 'other'), ('workflow_run', {'id': 999}),
                             ('digest', 'sha256:' + '0' * 64), ('size_in_bytes', 1)]:
            with self.subTest(field=field):
                original = dict(self.metadata)
                self.metadata[field] = value
                with self.assertRaises(promote.Refusal): self.fetch()
                self.metadata = original
                self.github = FakeGitHub()

    def test_extra_missing_duplicate_traversal_and_symlink_refused(self):
        for mode in ('extra', 'missing', 'duplicate', 'traversal', 'symlink'):
            with self.subTest(mode=mode):
                members = self.members.copy()
                if mode == 'extra': members.append(('extra.zip', b'extra'))
                if mode == 'missing': members.pop()
                if mode == 'duplicate': members[-1] = members[0]
                if mode == 'traversal': members[-1] = ('../escape', b'bad')
                if mode == 'symlink':
                    member = zipfile.ZipInfo(members[-1][0])
                    member.create_system = 3
                    member.external_attr = (stat.S_IFLNK | 0o777) << 16
                    members[-1] = (member, b'target')
                with warnings.catch_warnings():
                    warnings.simplefilter('ignore', UserWarning)
                    self.members = members
                    with self.assertRaises(promote.Refusal): self.fetch()
                import shutil
                shutil.rmtree(self.fixture.root / 'candidate', ignore_errors=True)
                self.members = [(p.name, p.read_bytes()) for p in self.fixture.payload.iterdir()]
                self.metadata = {'name': 'release-candidate', 'expired': False, 'workflow_run': {'id': 123}}
                self.github = FakeGitHub()
        self.assertFalse((self.fixture.root.parent / 'escape').exists())

if __name__ == '__main__':
    unittest.main()
