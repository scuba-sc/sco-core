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
        self.approval = {"builder": {"run_id": 123}, "artifacts": [
            {"name": "sdk.zip", "artifact_id": 456},
            {"name": "sdk.spdx.json", "artifact_id": 456},
        ]}
        self.builder_run = {"path": ".github/workflows/release-candidate.yml", "event": "workflow_dispatch",
                    "head_branch": "main", "status": "completed", "conclusion": "success"}
        self.metadata = {"expired": False, "workflow_run": {"id": 123}}
        self.members = [("sdk.zip", b"archive"), ("sdk.spdx.json", b"sbom")]

    def fetch(self):
        self.github.add(("api", f"repos/{REPO}/actions/runs/123"), self.builder_run)
        self.github.add(("api", f"repos/{REPO}/actions/artifacts/456"), self.metadata)
        self.github.add(("api", f"repos/{REPO}/actions/artifacts/456/zip"), archive(self.members))
        return promote.fetch_candidate(self.approval, REPO, self.root / "payload", self.github)

    def test_fetch_exact_approved_artifact_bytes(self):
        self.assertEqual(self.fetch(), 123)
        self.assertEqual({p.name: p.read_bytes() for p in (self.root / "payload").iterdir()},
                         dict(self.members))
        self.assertEqual(len(self.github.calls), 3)


    def assert_fetch_refused(self, message):
        with self.assertRaisesRegex(promote.Refusal, message):
            self.fetch()

    def test_fetch_extra_member(self):
        self.members.append(("extra.zip", b"unapproved"))
        self.assert_fetch_refused("unexpected, duplicate or unsafe")

    def test_fetch_missing_member(self):
        self.members.pop()
        self.assert_fetch_refused("exactly the approved file set")

    def test_fetch_duplicate_member(self):
        self.members.append(self.members[0])
        with warnings.catch_warnings():
            warnings.simplefilter("ignore", UserWarning)
            self.assert_fetch_refused("unexpected, duplicate or unsafe")

    def test_fetch_path_traversal_member(self):
        self.members.append(("../escape.zip", b"unapproved"))
        self.assert_fetch_refused("unexpected, duplicate or unsafe")
        self.assertFalse((self.root / "escape.zip").exists())

    def test_fetch_absolute_path_member(self):
        self.members.append(("/escape.zip", b"unapproved"))
        self.assert_fetch_refused("unexpected, duplicate or unsafe")

    def test_fetch_nested_member(self):
        self.members.append(("nested/sdk.zip", b"unapproved"))
        self.assert_fetch_refused("unexpected, duplicate or unsafe")

    def test_fetch_backslash_member(self):
        self.members.append(("..\\escape.zip", b"unapproved"))
        self.assert_fetch_refused("unexpected, duplicate or unsafe")

    def test_fetch_symlink_member(self):
        member = zipfile.ZipInfo("sdk.zip")
        member.create_system = 3
        member.external_attr = (stat.S_IFLNK | 0o777) << 16
        self.members[0] = (member, b"../escape.zip")
        self.assert_fetch_refused("unexpected, duplicate or unsafe")
        self.assertFalse((self.root / "payload/sdk.zip").exists())

    def test_fetch_directory_member(self):
        self.members.append(("folder/", b""))
        self.assert_fetch_refused("unexpected, duplicate or unsafe")

    def test_fetch_expired_artifact(self):
        self.metadata["expired"] = True
        self.assert_fetch_refused("expired or belongs")
        self.assertFalse(any(args[-1].endswith("/zip") for args in self.github.calls))

    def test_fetch_different_builder_run(self):
        self.metadata["workflow_run"]["id"] = 999
        self.assert_fetch_refused("expired or belongs")

    def test_fetch_wrong_builder_workflow(self):
        self.builder_run["path"] = ".github/workflows/other.yml"
        self.assert_fetch_refused("not a successful main")

    def test_fetch_wrong_builder_branch(self):
        self.builder_run["head_branch"] = "feature"
        self.assert_fetch_refused("not a successful main")

    def test_fetch_wrong_builder_event(self):
        self.builder_run["event"] = "push"
        self.assert_fetch_refused("not a successful main")

    def test_fetch_unfinished_builder(self):
        self.builder_run["status"] = "in_progress"
        self.assert_fetch_refused("not a successful main")

    def test_fetch_failed_builder(self):
        self.builder_run["conclusion"] = "failure"
        self.assert_fetch_refused("not a successful main")

    def test_fetch_duplicate_approved_name(self):
        self.approval["artifacts"].append(self.approval["artifacts"][0])
        self.assert_fetch_refused("unsafe, duplicate or reserved")
        self.assertEqual(self.github.calls, [])

    def test_fetch_reserved_approved_names(self):
        for name in ("release.json", "release.json.sig"):
            with self.subTest(name=name):
                self.approval["artifacts"][0]["name"] = name
                self.assert_fetch_refused("unsafe, duplicate or reserved")

    def test_fetch_unsafe_approved_names(self):
        for name in ("../escape.zip", "/escape.zip", "nested/file", "..\\file", "", 1):
            with self.subTest(name=name):
                self.approval["artifacts"][0]["name"] = name
                self.assert_fetch_refused("unsafe, duplicate or reserved")

    def test_fetch_empty_inventory(self):
        self.approval["artifacts"] = []
        self.assert_fetch_refused("empty approved payload")

    def test_fetch_invalid_run_ids(self):
        for value in (True, False, 0, -1, "123", 1.5, None):
            with self.subTest(value=value):
                self.approval["builder"]["run_id"] = value
                self.assert_fetch_refused("invalid builder run ID or artifact ID")
        self.assertEqual(self.github.calls, [])

    def test_fetch_invalid_artifact_ids(self):
        for value in (True, False, 0, -1, "456", 1.5, None):
            with self.subTest(value=value):
                self.approval["artifacts"][0]["artifact_id"] = value
                self.assert_fetch_refused("invalid builder run ID or artifact ID")

    def prerequisite_fixtures(self):
        checks, runs = [], {}
        required = {"cmake": ("linux", "kernel-only", "windows"),
                    "sdk": ("package", "windows", "csharp-host-sim"),
                    "test": ("test",), "ip-compliance": ("audit",)}
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
        self.assertEqual(len(self.github.calls), 5)  # Each workflow run is fetched only once.


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
        self.signature = self.root / "approval.json.sig"
        self.manifest.write_text(json.dumps(self.approval) + "\n")
        self.signature.write_bytes(b"synthetic signature\n")
        self.asset_bytes = dict(self.members)
        for name, data in self.members:
            (self.payload / name).write_bytes(data)
        self.asset_bytes.update({"release.json": self.manifest.read_bytes(),
                                 "release.json.sig": self.signature.read_bytes()})
        self.assets = [{"id": i, "name": name, "size": len(data), "state": "uploaded",
                        "updated_at": "2026-01-01T00:00:00Z"}
                       for i, (name, data) in enumerate(self.asset_bytes.items(), 700)]
        self.draft = {"id": 600, "tag_name": TAG, "draft": True}
        self.releases_command = ("api", "--paginate", "--slurp", f"repos/{REPO}/releases?per_page=100")
        self.assets_command = ("api", "--paginate", "--slurp", f"repos/{REPO}/releases/600/assets?per_page=100")
        self.create_command = ("release", "create", TAG,
                               *[str(self.payload / name) for name, _ in self.members],
                               str(self.runner / "release-approval/release.json"),
                               str(self.runner / "release-approval/release.json.sig"),
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
            return promote.create_draft(REPO, TAG, self.manifest, self.signature, self.payload,
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
        # Even JSON-equivalent bytes must match the committed signature input exactly.
        self.asset_bytes["release.json"] = json.dumps(self.approval, indent=2).encode()
        self.assert_draft_refused("approval differs")

    def test_draft_signature_bytes_mismatch(self):
        self.draft_fixtures()
        self.asset_bytes["release.json.sig"] = b"different synthetic signature"
        self.assert_draft_refused("approval differs")

    def publication(self, releases=None, assets=None, published=None, configure_only=False):
        (self.runner / "draft-assets.json").write_text(json.dumps(self.assets))
        self.github.add(self.releases_command, [[self.draft]] if releases is None else releases)
        self.github.add(self.assets_command, [self.assets] if assets is None else assets)
        self.edit_command = ("release", "edit", TAG, "--repo", REPO, "--draft=false")
        self.github.add(self.edit_command, b"")
        self.github.add(("api", f"repos/{REPO}/releases/600"),
                        dict(self.draft, draft=False, html_url=f"https://github.com/{REPO}/releases/tag/{TAG}")
                        if published is None else published)
        if not configure_only:
            return promote.publish_draft(REPO, TAG, 600, self.runner, self.github)

    def test_publish_same_verified_draft(self):
        self.assertTrue(callable(getattr(promote, "publish_draft", None)),
                        "publication has not been extracted")
        self.draft_fixtures()
        self.assertEqual(self.publication(), f"https://github.com/{REPO}/releases/tag/{TAG}")
        self.assertIn(self.edit_command, self.github.calls)
        self.assertEqual(self.github.calls[-1], ("api", f"repos/{REPO}/releases/600"))


    def assert_publish_refused(self, message, **kwargs):
        with self.assertRaisesRegex(promote.Refusal, message):
            self.publication(**kwargs)
        self.assertNotIn(self.edit_command, self.github.calls)

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

    def test_cli_fetch_writes_run_id_output(self):
        self.assertTrue(callable(getattr(promote, "main", None)), "CLI has not been extracted")
        decisions = self.root / "docs/legal/release-decisions"
        decisions.mkdir(parents=True)
        (decisions / f"{TAG}.json").write_text(json.dumps(self.approval))
        self.github.add(("api", f"repos/{REPO}/actions/runs/123"), self.builder_run)
        self.github.add(("api", f"repos/{REPO}/actions/artifacts/456"), self.metadata)
        self.github.add(("api", f"repos/{REPO}/actions/artifacts/456/zip"), archive(self.members))
        (self.root / "output").write_text("existing=kept\n")
        with contextlib.chdir(self.root):
            self.assertEqual(promote.main(["fetch"], self.cli_environment(), self.github), 0)
        self.assertEqual((self.root / "output").read_text(), "existing=kept\nrun-id=123\n")
        self.assertEqual((self.root / "payload/sdk.zip").read_bytes(), b"archive")


    def test_cli_prerequisites(self):
        self.prerequisites(*self.prerequisite_fixtures(), configure_only=True)
        self.assertEqual(promote.main(["prerequisites"], self.cli_environment(), self.github), 0)
        self.assertEqual(len(self.github.calls), 5)

    def test_cli_draft_records_id_only_after_round_trip(self):
        self.draft_fixtures()
        decisions = self.root / "docs/legal/release-decisions"
        decisions.mkdir(parents=True)
        (decisions / f"{TAG}.json").write_bytes(self.manifest.read_bytes())
        (decisions / f"{TAG}.json.sig").write_bytes(self.signature.read_bytes())
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
        self.assertEqual(self.github.calls[-1], ("api", f"repos/{REPO}/releases/600"))

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

    def test_cli_corrupt_zip_is_refused_without_traceback(self):
        decisions = self.root / "docs/legal/release-decisions"
        decisions.mkdir(parents=True)
        (decisions / f"{TAG}.json").write_text(json.dumps(self.approval))
        self.github.add(("api", f"repos/{REPO}/actions/runs/123"), self.builder_run)
        self.github.add(("api", f"repos/{REPO}/actions/artifacts/456"), self.metadata)
        self.github.add(("api", f"repos/{REPO}/actions/artifacts/456/zip"), b"not a zip")
        error = io.StringIO()
        with contextlib.chdir(self.root), contextlib.redirect_stderr(error):
            self.assertEqual(promote.main(["fetch"], self.cli_environment(), self.github), 1)
        self.assertEqual(error.getvalue(), "refused: promotion input unavailable\n")
        self.assertFalse((self.root / "output").exists())

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

    def test_fetch_multiple_artifacts_are_sorted_and_exact(self):
        self.approval["artifacts"][0]["artifact_id"] = 457
        self.github.add(("api", f"repos/{REPO}/actions/runs/123"), self.builder_run)
        for artifact_id, members in ((456, [self.members[1]]), (457, [self.members[0]])):
            self.github.add(("api", f"repos/{REPO}/actions/artifacts/{artifact_id}"), self.metadata)
            self.github.add(("api", f"repos/{REPO}/actions/artifacts/{artifact_id}/zip"), archive(members))
        self.assertEqual(promote.fetch_candidate(self.approval, REPO, self.root / "payload", self.github), 123)
        self.assertEqual([args[-1] for args in self.github.calls if args[-1].endswith("/zip")],
                         [f"repos/{REPO}/actions/artifacts/456/zip", f"repos/{REPO}/actions/artifacts/457/zip"])

    def test_fetch_member_from_wrong_approved_artifact_is_refused(self):
        self.approval["artifacts"][1]["artifact_id"] = 457
        # A globally approved name still cannot come from a different approved artifact ID.
        self.assert_fetch_refused("unexpected, duplicate or unsafe")


class SignedPromotionTests(unittest.TestCase):
    """Exercise fake GitHub round trips against the real signed-verifier CLI."""
    def setUp(self):
        self.fixture = verify_tests.ReleaseVerifyTests("test_valid_release")
        self.addCleanup(self.fixture.doCleanups)
        self.fixture.setUp()
        self.fixture.write_record()
        self.fixture.sign()
        self.root = self.fixture.root
        self.client = FakeGitHub()
        self.runner = self.root / "runner"
        self.runner.mkdir()
        self.payload = self.root / "candidate"
        self.downloaded = self.root / "downloaded"
        self.bytes = {p.name: p.read_bytes() for p in self.fixture.payload.iterdir()}
        self.assets_command = ("api", "--paginate", "--slurp", f"repos/{REPO}/releases/600/assets?per_page=100")
        self.releases_command = ("api", "--paginate", "--slurp", f"repos/{REPO}/releases?per_page=100")
        self.draft = {"id": 600, "tag_name": TAG, "draft": True}
        output = contextlib.redirect_stdout(io.StringIO())
        output.__enter__()
        self.addCleanup(output.__exit__, None, None, None)

    def fetch(self, changed=False):
        self.client.add(("api", f"repos/{REPO}/actions/runs/123"), {
            "path": ".github/workflows/release-candidate.yml", "head_branch": "main",
            "event": "workflow_dispatch", "status": "completed", "conclusion": "success",
        })
        self.client.add(("api", f"repos/{REPO}/actions/artifacts/456"),
                        {"expired": False, "workflow_run": {"id": 123}})
        members = dict(self.bytes)
        if changed:
            members["sdk.zip"] = b"rebuilt"
        self.client.add(("api", f"repos/{REPO}/actions/artifacts/456/zip"), archive(members.items()))
        return promote.fetch_candidate(self.fixture.record, REPO, self.payload, self.client)

    def draft_round_trip(self, changed=False):
        members = dict(self.bytes)
        if changed:
            members["sdk.zip"] = b"rebuilt"
        members.update({"release.json": self.fixture.manifest.read_bytes(),
                        "release.json.sig": (self.root / "release.json.sig").read_bytes()})
        self.assets = [{"id": i, "name": name, "size": len(data), "state": "uploaded",
                        "updated_at": "2026-01-01T00:00:00Z"}
                       for i, (name, data) in enumerate(members.items(), 700)]
        self.client.add(self.releases_command, [[]], [[self.draft]])
        self.client.add(("release", "create", TAG,
                         *[str(self.payload / a["name"]) for a in self.fixture.record["artifacts"]],
                         str(self.runner / "release-approval/release.json"),
                         str(self.runner / "release-approval/release.json.sig"),
                         "--repo", REPO, "--draft", "--verify-tag", "--title", "sco-core SDK 9.9.9",
                         "--notes-file", str(self.runner / "notes.md")), b"")
        self.client.add(self.assets_command, [self.assets])
        for asset in self.assets:
            self.client.add(("api", f"repos/{REPO}/releases/assets/{asset['id']}",
                             "-H", "Accept: application/octet-stream"), members[asset["name"]])
        return promote.create_draft(REPO, TAG, self.fixture.manifest, self.root / "release.json.sig",
                                    self.payload, self.runner, self.downloaded, "9.9.9", self.client)

    def test_offline_signed_candidate_draft_publish_round_trip(self):
        # The GitHub calls are fixtures; signature, git, inventory and hash checks are real.
        self.assertEqual(self.fetch(), 123)
        first = self.fixture.verify(**{"payload-dir": self.payload})
        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertEqual(self.draft_round_trip(), 600)
        second = self.fixture.verify(**{"payload-dir": self.downloaded})
        self.assertEqual(second.returncode, 0, second.stderr)
        self.client.add(self.releases_command, [[self.draft]])
        self.client.add(self.assets_command, [self.assets])
        self.client.add(("release", "edit", TAG, "--repo", REPO, "--draft=false"), b"")
        self.client.add(("api", f"repos/{REPO}/releases/600"),
                        dict(self.draft, draft=False, html_url="https://example.invalid/release"))
        self.assertEqual(promote.publish_draft(REPO, TAG, 600, self.runner, self.client),
                         "https://example.invalid/release")

    def test_fetch_changed_candidate_bytes(self):
        self.fetch(changed=True)
        self.fixture.assert_refused(self.fixture.verify(**{"payload-dir": self.payload}),
                                    "artifact SHA256 mismatch")
        self.assertFalse(any(call[:2] == ("release", "create") for call in self.client.calls))

    def test_draft_payload_bytes_mismatch(self):
        self.fetch()
        self.assertEqual(self.fixture.verify(**{"payload-dir": self.payload}).returncode, 0)
        self.draft_round_trip(changed=True)
        self.fixture.assert_refused(self.fixture.verify(**{"payload-dir": self.downloaded}),
                                    "artifact SHA256 mismatch")
        self.assertFalse(any(call[:2] == ("release", "edit") for call in self.client.calls))


if __name__ == "__main__":
    unittest.main(verbosity=2)
