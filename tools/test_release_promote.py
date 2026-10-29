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


class ReviewBundleFixture:
    """Per-test filesystem and API fixtures for review-bundle cases."""
    def __init__(self, test_root: Path):
        self.review_root = Path(tempfile.mkdtemp(prefix="bundle-repo-", dir=test_root))
        (self.review_root / "sdk").mkdir(parents=True)
        (self.review_root / "docs/legal").mkdir(parents=True)
        profiles = {
            "core": {"zip": "core-{version}"},
            "game-sc": {"zip": "game-{game_pack_version}"},
            "legacy": {"zip": "legacy-{version}"},
            "core-source": {"zip": "source-{version}", "layout": "source"},
            "host-source-sc": {"zip": "host-{version}", "layout": "source"}}
        component = {"source": "src/review.c", "destination": "src/review.c",
                     "profiles": list(profiles)}
        (self.review_root / "sdk/components.json").write_text(json.dumps(
            {"profiles": profiles, "components": [component]}))
        (self.review_root / "docs/legal/provenance.json").write_text(json.dumps(
            {"entries": [{"path": "src/review.c", "status": "needs-review",
                          "note": "synthetic review note"}]}))
        self.files: dict[str, bytes] = {}
        for profile, spec in profiles.items():
            name = spec["zip"].replace("{version}", "9.9.9").replace(
                "{game_pack_version}", "9.9.9") + ".zip"
            stream = io.BytesIO()
            with zipfile.ZipFile(stream, "w") as bundle:
                info = zipfile.ZipInfo(f"{profile}/src/review.c")
                info.create_system = 3
                info.external_attr = (stat.S_IFREG | 0o644) << 16
                bundle.writestr(info, b"synthetic payload")
            self.files[name] = stream.getvalue()
        self._write_sums()
        self.dist = self.review_root / "dist"
        self.dist.mkdir()
        self._write_dist()
        jobs = []
        for index, (workflow, required) in enumerate(promote.REQUIRED.items(), 200):
            for job_index, name in enumerate(sorted(required), 1):
                jobs.append({"workflow": workflow, "path": f".github/workflows/{workflow}.yml",
                             "run_id": index, "attempt": 1, "head_sha": SHA,
                             "id": index * 10 + job_index, "job": name,
                             "status": "completed", "conclusion": "success"})
        self.evidence = {"schema_version": 1, "repository": REPO, "source_sha": SHA, "jobs": jobs}
        self.github = self._configure_bundle_api()

    def _write_sums(self):
        payload = {name: data for name, data in self.files.items() if name != "SHA256SUMS"}
        self.files["SHA256SUMS"] = "".join(
            f"{hashlib.sha256(data).hexdigest()}  {name}\n"
            for name, data in sorted(payload.items())).encode()

    def _write_dist(self):
        for path in self.dist.iterdir():
            if path.is_file() or path.is_symlink():
                path.unlink()
        for name, data in self.files.items():
            (self.dist / name).write_bytes(data)

    def _configure_bundle_api(self, evidence=None, candidate_members=None):
        github = FakeGitHub()
        evidence = self.evidence if evidence is None else evidence
        candidate_members = self.files if candidate_members is None else candidate_members
        evidence_zip = archive([("readiness-evidence.json", json.dumps(evidence).encode())])
        github.add(("api", f"repos/{REPO}/actions/runs/123"), {
            "path": ".github/workflows/release-candidate.yml", "event": "workflow_dispatch",
            "head_branch": "main", "head_sha": SHA, "run_attempt": 1,
            "repository": {"full_name": REPO, "id": 10},
            "head_repository": {"full_name": REPO, "id": 10}})
        for artifact_id, name, data in ((456, "release-candidate", archive(candidate_members.items())),
                                        (457, "readiness-evidence", evidence_zip)):
            github.add(("api", f"repos/{REPO}/actions/artifacts/{artifact_id}"), {
                "id": artifact_id, "name": name, "expired": False,
                "workflow_run": {"id": 123, "head_sha": SHA,
                                 "repository_id": 10, "head_repository_id": 10}})
            github.add(("api", f"repos/{REPO}/actions/artifacts/{artifact_id}/zip"), data)
        return github


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

    def readiness_fixtures(self):
        runs = []
        for index, (workflow, required) in enumerate(promote.REQUIRED.items(), 200):
            run = {"id": index, "run_number": index, "run_attempt": 2,
                   "path": f".github/workflows/{workflow}.yml", "head_sha": SHA,
                   "head_branch": "main", "event": "push", "status": "completed",
                   "conclusion": "success", "repository": {"full_name": REPO},
                   "head_repository": {"full_name": REPO}}
            runs.append(run)
            checks = [{"id": index * 10 + job_index, "name": name, "status": "completed",
                       "conclusion": "success"}
                      for job_index, name in enumerate(sorted(required), 1)]
            self.github.add(("api", "--paginate", "--slurp",
                             f"repos/{REPO}/actions/workflows/{workflow}.yml/runs?branch=main&event=push&head_sha={SHA}&per_page=100"),
                            [{"workflow_runs": [run]}])
            self.github.add(("api", "--paginate", "--slurp",
                             f"repos/{REPO}/actions/runs/{index}/attempts/2/jobs?per_page=100"),
                            [{"jobs": checks}])
        return runs

    def auto_fixture(self, candidate_runs=None):
        runs = self.readiness_fixtures()
        trigger = runs[0]
        (self.root / "sdk").mkdir(exist_ok=True)
        (self.root / "sdk/VERSION").write_text("9.9.9\n")
        event = {"repository": {"full_name": REPO}, "workflow_run": trigger}
        self.github.add(("api", f"repos/{REPO}/actions/runs/{trigger['id']}"), trigger)
        self.github.add(("api", "--paginate", "--slurp", f"repos/{REPO}/git/matching-refs/tags/sdk-v9.9.9"), [[]])
        self.github.add(("api", "--paginate", "--slurp", f"repos/{REPO}/releases?per_page=100"), [[]])
        self.github.add(("api", "--paginate", "--slurp",
                         f"repos/{REPO}/actions/workflows/release-candidate.yml/runs?branch=main&head_sha={SHA}&per_page=100"),
                        [{"workflow_runs": [] if candidate_runs is None else candidate_runs}])
        if candidate_runs is None:
            self.github.add(("api", f"repos/{REPO}/git/ref/heads/main"), {"object": {"sha": SHA}})
        return event

    def test_verified_readiness_records_exact_nine_job_attempts(self):
        self.readiness_fixtures()
        evidence = promote.verified_readiness(REPO, SHA, self.github)
        self.assertEqual(len(evidence["jobs"]), 9)
        self.assertEqual({job["attempt"] for job in evidence["jobs"]}, {2})
        self.assertEqual({job["head_sha"] for job in evidence["jobs"]}, {SHA})
        self.assertTrue(all("/attempts/2/jobs?" in call[1]
                            for call in self.github.calls if "/jobs?" in call[1]))

    def test_verified_readiness_refuses_missing_duplicate_and_failed_jobs(self):
        for mutation, message in (("missing", "missing or duplicated"),
                                  ("duplicate", "missing or duplicated"),
                                  ("failure", "not successful")):
            with self.subTest(mutation=mutation):
                self.github = FakeGitHub()
                self.readiness_fixtures()
                key = ("api", "--paginate", "--slurp",
                       f"repos/{REPO}/actions/runs/200/attempts/2/jobs?per_page=100")
                jobs = self.github.responses[key][0][0]["jobs"]
                if mutation == "missing":
                    jobs.pop()
                elif mutation == "duplicate":
                    jobs.append(dict(jobs[0]))
                else:
                    jobs[0]["conclusion"] = "neutral"
                pattern = "cmake/" if mutation == "missing" else "cmake/"
                with self.assertRaisesRegex(promote.Refusal, message + ": " + pattern):
                    promote.verified_readiness(REPO, SHA, self.github)

    def test_verified_readiness_refuses_failed_newer_workflow_run(self):
        self.readiness_fixtures()
        workflow = ".github/workflows/cmake.yml"
        key = ("api", "--paginate", "--slurp",
               f"repos/{REPO}/actions/workflows/cmake.yml/runs?branch=main&event=push&head_sha={SHA}&per_page=100")
        failed = {"id": 999, "run_number": 999, "run_attempt": 1, "path": workflow,
                  "head_sha": SHA, "head_branch": "main", "event": "push", "status": "completed",
                  "conclusion": "failure", "repository": {"full_name": REPO},
                  "head_repository": {"full_name": REPO}}
        self.github.responses[key] = [[{"workflow_runs": [failed]}]]
        with self.assertRaisesRegex(promote.Refusal, "latest mandatory workflow is not successful: cmake"):
            promote.verified_readiness(REPO, SHA, self.github)

    def test_auto_candidate_refuses_pr_fork_wrong_repo_branch_sha_and_stale_attempt(self):
        base = {"id": 123, "run_attempt": 1, "head_sha": SHA,
                "path": ".github/workflows/cmake.yml", "event": "push", "head_branch": "main",
                "status": "completed", "conclusion": "success",
                "repository": {"full_name": REPO}, "head_repository": {"full_name": REPO}}
        cases = (("event", "pull_request"), ("head_branch", "feature"),
                 ("head_repository", {"full_name": "fork/sdk"}),
                 ("path", ".github/workflows/other.yml"))
        for field, value in cases:
            with self.subTest(field=field):
                client = FakeGitHub()
                event = {"repository": {"full_name": REPO}, "workflow_run": dict(base, **{field: value})}
                with self.assertRaisesRegex(promote.Refusal, "eligible trusted main push"):
                    promote.auto_candidate(REPO, event, self.root, client)
                self.assertEqual(client.calls, [])
        event = {"repository": {"full_name": "fork/sdk"}, "workflow_run": base}
        with self.assertRaisesRegex(promote.Refusal, "eligible trusted main push"):
            promote.auto_candidate(REPO, event, self.root, FakeGitHub())
        changed = dict(base, head_sha="b" * 40)
        event = {"repository": {"full_name": REPO}, "workflow_run": changed}
        client = FakeGitHub()
        client.add(("api", f"repos/{REPO}/actions/runs/123"), dict(changed, head_sha=SHA))
        with self.assertRaisesRegex(promote.Refusal, "stale or does not match"):
            promote.auto_candidate(REPO, event, self.root, client)

    def test_auto_candidate_refuses_stale_run_attempt_and_api_identity(self):
        event = self.auto_fixture()
        trigger_key = ("api", f"repos/{REPO}/actions/runs/200")
        self.github.responses[trigger_key] = [dict(event["workflow_run"], run_attempt=1)]
        with self.assertRaisesRegex(promote.Refusal, "stale or does not match"):
            promote.auto_candidate(REPO, event, self.root, self.github)

    def test_auto_candidate_dispatches_once_with_main_and_expected_source_sha(self):
        event = self.auto_fixture()
        dispatch = ("api", f"repos/{REPO}/actions/workflows/release-candidate.yml/dispatches",
                    "-X", "POST", "-f", "ref=main", "-f", f"inputs[tag]={TAG}",
                    "-f", f"inputs[expected_source_sha]={SHA}")
        self.github.add(dispatch, b"")
        with mock.patch.object(promote.subprocess, "run", return_value=mock.Mock(returncode=0)):
            self.assertEqual(promote.auto_candidate(REPO, event, self.root, self.github), "dispatched")
        self.assertEqual(self.github.calls.count(dispatch), 1)

    def test_auto_candidate_suppresses_any_visible_active_or_successful_candidate(self):
        for status, conclusion in (("queued", None), ("in_progress", None), ("completed", "success")):
            with self.subTest(status=status):
                self.github = FakeGitHub()
                run = {"id": 900, "run_number": 1, "run_attempt": 1, "event": "workflow_dispatch",
                       "path": ".github/workflows/release-candidate.yml", "head_branch": "main",
                       "head_sha": SHA, "status": status, "conclusion": conclusion}
                event = self.auto_fixture([run])
                with mock.patch.object(promote.subprocess, "run", return_value=mock.Mock(returncode=0)):
                    self.assertEqual(promote.auto_candidate(REPO, event, self.root, self.github),
                                     "duplicate-suppressed")
                self.assertFalse(any(call[1].endswith("/dispatches") for call in self.github.calls))

    def test_auto_candidate_waits_while_a_mandatory_workflow_is_still_running(self):
        # Every mandatory workflow triggers readiness as it completes, so the first triggers
        # arrive before the last sibling finishes. That is a wait, not a failure: a red run on
        # main for a normal intermediate state trains people to ignore the workflow.
        event = self.auto_fixture()
        key = ("api", "--paginate", "--slurp",
               f"repos/{REPO}/actions/workflows/cmake.yml/runs?branch=main&event=push&head_sha={SHA}&per_page=100")
        run = dict(self.readiness_fixtures()[0], status="in_progress", conclusion=None)
        self.github.responses[key] = [[{"workflow_runs": [run]}]]
        result = promote.auto_candidate(REPO, event, self.root, self.github)
        self.assertTrue(result.startswith("waiting: "), result)
        self.assertIn("still running: cmake", result)
        self.assertFalse(any(call[1].endswith("/dispatches") for call in self.github.calls))

    def test_auto_candidate_ignores_its_own_readiness_run_when_looking_for_candidates(self):
        # This readiness run is itself a release-candidate.yml run on main at the same SHA, so
        # counting workflow_run entries as candidates suppressed every dispatch. Only a
        # dispatched (workflow_dispatch) run may suppress, and never the current run.
        own = {"id": 901, "run_number": 2, "run_attempt": 1, "event": "workflow_run",
               "path": ".github/workflows/release-candidate.yml", "head_branch": "main",
               "head_sha": SHA, "status": "in_progress", "conclusion": None}
        event = self.auto_fixture([own])
        self.github.add(("api", f"repos/{REPO}/git/ref/heads/main"), {"object": {"sha": SHA}})
        dispatch = ("api", f"repos/{REPO}/actions/workflows/release-candidate.yml/dispatches",
                    "-X", "POST", "-f", "ref=main", "-f", f"inputs[tag]={TAG}",
                    "-f", f"inputs[expected_source_sha]={SHA}")
        self.github.add(dispatch, b"")
        with mock.patch.object(promote.subprocess, "run", return_value=mock.Mock(returncode=0)), \
                mock.patch.dict("os.environ", {"GITHUB_RUN_ID": "901"}):
            self.assertEqual(promote.auto_candidate(REPO, event, self.root, self.github), "dispatched")
        self.assertEqual(self.github.calls.count(dispatch), 1)

    def test_auto_candidate_failed_or_cancelled_candidate_requires_deliberate_retry(self):
        for conclusion in ("failure", "cancelled"):
            with self.subTest(conclusion=conclusion):
                self.github = FakeGitHub()
                run = {"id": 900, "run_number": 1, "run_attempt": 1, "event": "workflow_dispatch",
                       "path": ".github/workflows/release-candidate.yml", "head_branch": "main",
                       "head_sha": SHA, "status": "completed", "conclusion": conclusion}
                event = self.auto_fixture([run])
                with mock.patch.object(promote.subprocess, "run", return_value=mock.Mock(returncode=0)):
                    with self.assertRaisesRegex(promote.Refusal,
                            "manual workflow_dispatch or rerun the failed candidate workflow run, not readiness"):
                        promote.auto_candidate(REPO, event, self.root, self.github)
                self.assertFalse(any(call[1].endswith("/dispatches") for call in self.github.calls))

    def test_auto_candidate_refuses_invalid_version_existing_tag_or_release_and_main_race(self):
        event = self.auto_fixture()
        (self.root / "sdk/VERSION").write_text("2.4.0-rc1\n")
        with self.assertRaisesRegex(promote.Refusal, "strict X.Y.Z"):
            promote.auto_candidate(REPO, event, self.root, self.github)
        for kind in ("tag", "release", "main"):
            with self.subTest(kind=kind):
                self.github = FakeGitHub()
                event = self.auto_fixture()
                if kind == "tag":
                    key = ("api", "--paginate", "--slurp", f"repos/{REPO}/git/matching-refs/tags/sdk-v9.9.9")
                    self.github.responses[key] = [[[{"ref": "refs/tags/sdk-v9.9.9"}]]]
                elif kind == "release":
                    key = ("api", "--paginate", "--slurp", f"repos/{REPO}/releases?per_page=100")
                    self.github.responses[key] = [[[{"tag_name": "sdk-v9.9.9", "draft": True}]]]
                else:
                    self.github.responses[("api", f"repos/{REPO}/git/ref/heads/main")] = [{"object": {"sha": "b" * 40}}]
                with mock.patch.object(promote.subprocess, "run", return_value=mock.Mock(returncode=0)):
                    with self.assertRaises(promote.Refusal):
                        promote.auto_candidate(REPO, event, self.root, self.github)

    def test_auto_candidate_unknown_dispatch_outcome_is_not_retried(self):
        event = self.auto_fixture()
        dispatch = ("api", f"repos/{REPO}/actions/workflows/release-candidate.yml/dispatches",
                    "-X", "POST", "-f", "ref=main", "-f", f"inputs[tag]={TAG}",
                    "-f", f"inputs[expected_source_sha]={SHA}")
        def client(*args):
            if args == dispatch:
                self.github.calls.append(args)
                raise promote.Refusal("GitHub command unavailable")
            return self.github(*args)
        with mock.patch.object(promote.subprocess, "run", return_value=mock.Mock(returncode=0)):
            with self.assertRaisesRegex(promote.Refusal, "outcome unknown"):
                promote.auto_candidate(REPO, event, self.root, client)
        self.assertEqual(self.github.calls.count(dispatch), 1)

    def review_bundle_fixtures(self):
        return ReviewBundleFixture(self.root)

    def test_review_bundle_copies_exact_hashes_readiness_and_scoped_provenance(self):
        fixture = self.review_bundle_fixtures()
        output = self.root / "review-output"
        env = self.cli_environment()
        env.update({"GITHUB_RUN_ATTEMPT": "1", "READINESS_ARTIFACT_ID": "457",
                    "REVIEW_OUTPUT": str(output)})
        with contextlib.chdir(fixture.review_root):
            self.assertEqual(promote.main(["review-bundle"], env, fixture.github), 0)
        review = json.loads((output / "candidate-review.json").read_bytes())
        self.assertEqual((review["candidate_run_id"], review["candidate_run_attempt"]), (123, 1))
        self.assertEqual((review["candidate_artifact_id"], review["readiness_artifact_id"]), (456, 457))
        self.assertEqual(len(review["readiness_jobs"]), 9)
        self.assertEqual({item["profile"] for item in review["provenance_notes"]},
                         {"core", "game-sc", "legacy", "core-source", "host-source-sc"})
        self.assertEqual({item["path"] for item in review["provenance_notes"]}, {"src/review.c"})
        self.assertEqual([(item["name"], item["size"], item["sha256"]) for item in review["payload"]],
                         [(name, len(data), hashlib.sha256(data).hexdigest())
                          for name, data in sorted(fixture.files.items())])
        self.assertEqual({path.name: path.read_bytes() for path in output.iterdir()
                          if path.name in fixture.files}, fixture.files)
        self.assertFalse({"approved_by", "approved_at", "signature"} & set(review))
        self.assertIn("does not authorize release", (output / "CANDIDATE-REVIEW.md").read_text())
        self.assertFalse(any(call[0] == "release" for call in fixture.github.calls))

    def test_review_bundle_refuses_readiness_from_another_run_or_attempt(self):
        fixture = self.review_bundle_fixtures()
        fixture.github.responses[("api", f"repos/{REPO}/actions/artifacts/457")][0]["workflow_run"]["id"] = 999
        with self.assertRaisesRegex(promote.Refusal, "readiness artifact does not belong"):
            promote.review_bundle(REPO, TAG, SHA, 123, 1, 456, 457,
                                  fixture.dist, self.root / "review", fixture.review_root, fixture.github)

    def test_review_bundle_refuses_bad_readiness_jobs_and_duplicate_identity(self):
        for mutate in ("missing", "duplicate", "wrong-sha", "failed"):
            with self.subTest(mutate=mutate):
                fixture = self.review_bundle_fixtures()
                evidence = json.loads(json.dumps(fixture.evidence))
                if mutate == "missing":
                    evidence["jobs"].pop()
                elif mutate == "duplicate":
                    evidence["jobs"].append(dict(evidence["jobs"][0]))
                elif mutate == "wrong-sha":
                    evidence["jobs"][0]["head_sha"] = "b" * 40
                else:
                    evidence["jobs"][0]["conclusion"] = "neutral"
                fixture.github = fixture._configure_bundle_api(evidence=evidence)
                with self.assertRaises(promote.Refusal):
                    promote.review_bundle(REPO, TAG, SHA, 123, 1, 456, 457,
                                          fixture.dist, self.root / "review", fixture.review_root, fixture.github)

    def test_review_bundle_refuses_malformed_missing_extra_and_hash_mismatched_payloads(self):
        for mutation in ("malformed-sums", "missing-sums", "extra-member", "missing-member", "hash"):
            with self.subTest(mutation=mutation):
                fixture = self.review_bundle_fixtures()
                if mutation == "malformed-sums":
                    fixture.files["SHA256SUMS"] = b"not-a-checksum\n"
                elif mutation == "missing-sums":
                    del fixture.files["SHA256SUMS"]
                elif mutation == "hash":
                    name = next(name for name in fixture.files if name.endswith(".zip"))
                    fixture.files[name] += b"changed"
                fixture._write_dist()
                candidate = dict(fixture.files)
                if mutation == "extra-member":
                    candidate["extra.bin"] = b"unlisted"
                elif mutation == "missing-member":
                    candidate.pop(next(name for name in candidate if name.endswith(".zip")))
                fixture.github = fixture._configure_bundle_api(candidate_members=candidate)
                with self.assertRaises(promote.Refusal):
                    promote.review_bundle(REPO, TAG, SHA, 123, 1, 456, 457,
                                          fixture.dist, self.root / "review", fixture.review_root, fixture.github)

    def test_review_bundle_refuses_archive_path_traversal_before_output(self):
        fixture = self.review_bundle_fixtures()
        name = next(name for name in fixture.files if name.endswith(".zip"))
        stream = io.BytesIO()
        with zipfile.ZipFile(stream, "w") as bundle:
            bundle.writestr("../escape.h", b"unsafe")
        fixture.files[name] = stream.getvalue()
        fixture._write_sums()
        fixture._write_dist()
        fixture.github = fixture._configure_bundle_api()
        output = self.root / "review"
        with self.assertRaisesRegex(promote.Refusal, "unsafe or duplicate member"):
            promote.review_bundle(REPO, TAG, SHA, 123, 1, 456, 457,
                                  fixture.dist, output, fixture.review_root, fixture.github)
        self.assertFalse(output.exists())

    def test_review_bundle_refuses_nonregular_entry_inside_payload_directory(self):
        fixture = self.review_bundle_fixtures()
        (fixture.dist / "unsafe-directory").mkdir()
        with self.assertRaisesRegex(promote.Refusal, "unsafe or non-regular"):
            promote.review_bundle(REPO, TAG, SHA, 123, 1, 456, 457,
                                  fixture.dist, self.root / "review", fixture.review_root, fixture.github)
    def test_cli_prerequisites_writes_all_nine_readiness_identities(self):
        self.github = FakeGitHub()
        self.readiness_fixtures()
        evidence_path = self.root / "readiness-evidence.json"
        env = self.cli_environment()
        env["EXPECTED_SOURCE_SHA"] = SHA
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(promote.main(["--evidence-out", str(evidence_path), "prerequisites"],
                                          env, self.github), 0)
        evidence = json.loads(evidence_path.read_bytes())
        self.assertEqual((evidence["repository"], evidence["source_sha"]), (REPO, SHA))
        self.assertEqual(len(evidence["jobs"]), 9)
        self.assertEqual({(item["workflow"], item["job"]) for item in evidence["jobs"]},
                         {(workflow, job) for workflow, jobs in promote.REQUIRED.items() for job in jobs})
        self.assertEqual({item["run_id"] for item in evidence["jobs"]}, set(range(200, 205)))
        self.assertEqual({item["attempt"] for item in evidence["jobs"]}, {2})

    def test_review_bundle_refuses_foreign_nonmain_and_wrong_attempt_run(self):
        for field, value in (("event", "push"), ("head_branch", "feature"),
                             ("head_sha", "b" * 40), ("run_attempt", 2),
                             ("repository", {"full_name": "other/sdk", "id": 11}),
                             ("head_repository", {"full_name": "fork/sdk", "id": 12})):
            with self.subTest(field=field):
                fixture = self.review_bundle_fixtures()
                fixture.github.responses[("api", f"repos/{REPO}/actions/runs/123")][0][field] = value
                with self.assertRaisesRegex(promote.Refusal, "candidate run identity"):
                    promote.review_bundle(REPO, TAG, SHA, 123, 1, 456, 457,
                                          fixture.dist, self.root / "review", fixture.review_root, fixture.github)

    def test_review_bundle_refuses_candidate_artifact_repo_sha_and_run_mismatch(self):
        for field, value in (("head_sha", "b" * 40), ("id", 999),
                             ("repository_id", 11), ("head_repository_id", 12)):
            with self.subTest(field=field):
                fixture = self.review_bundle_fixtures()
                if field == "id":
                    fixture.github.responses[("api", f"repos/{REPO}/actions/artifacts/456")][0]["workflow_run"][field] = value
                else:
                    fixture.github.responses[("api", f"repos/{REPO}/actions/artifacts/456")][0]["workflow_run"][field] = value
                with self.assertRaisesRegex(promote.Refusal, "candidate artifact does not belong"):
                    promote.review_bundle(REPO, TAG, SHA, 123, 1, 456, 457,
                                          fixture.dist, self.root / "review", fixture.review_root, fixture.github)

    def test_review_bundle_refuses_readiness_artifact_repo_sha_and_run_mismatch(self):
        for field, value in (("head_sha", "b" * 40), ("id", 999),
                             ("repository_id", 11), ("head_repository_id", 12)):
            with self.subTest(field=field):
                fixture = self.review_bundle_fixtures()
                fixture.github.responses[("api", f"repos/{REPO}/actions/artifacts/457")][0]["workflow_run"][field] = value
                with self.assertRaisesRegex(promote.Refusal, "readiness artifact does not belong"):
                    promote.review_bundle(REPO, TAG, SHA, 123, 1, 456, 457,
                                          fixture.dist, self.root / "review", fixture.review_root, fixture.github)

    def test_review_bundle_refuses_duplicate_candidate_artifact_members(self):
        fixture = self.review_bundle_fixtures()
        name = next(name for name in fixture.files if name.endswith(".zip"))
        fixture.github = fixture._configure_bundle_api()
        with warnings.catch_warnings():
            warnings.simplefilter("ignore", UserWarning)
            duplicate_outer = archive([(name, fixture.files[name]), (name, fixture.files[name])])
        fixture.github.responses[("api", f"repos/{REPO}/actions/artifacts/456/zip")] = [duplicate_outer]
        with self.assertRaisesRegex(promote.Refusal, "unsafe, extra or duplicate member"):
            promote.review_bundle(REPO, TAG, SHA, 123, 1, 456, 457,
                                  fixture.dist, self.root / "review", fixture.review_root, fixture.github)



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
