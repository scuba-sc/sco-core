#!/usr/bin/env python3
"""Signed release verification through the public CLI, with throwaway keys only."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import shlex
import subprocess
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
TOOL = Path(__file__).with_name("release_verify.py")


class ReleaseVerifyTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "checkout"
        self.root.mkdir()
        self.payload = self.root / "payload"
        self.payload.mkdir()
        (self.payload / "sdk.zip").write_bytes(b"archive")
        (self.payload / "sdk.spdx.json").write_bytes(b"sbom-v1")
        (self.root / "sdk").mkdir()
        (self.root / "sdk/components.json").write_bytes(b'{"components":[]}\n')
        (self.root / ".github/workflows").mkdir(parents=True)
        (self.root / ".github/workflows/build.yml").write_text("name: build\n")
        self.command("git", "init", "-q")
        self.command("git", "add", "sdk", ".github")
        self.command("git", "-c", "user.name=maintainer", "-c",
                     "user.email=maintainer@example.invalid", "commit", "-qm", "fixture")
        self.sha = self.command("git", "rev-parse", "HEAD").stdout.decode().strip()
        self.manifest = self.root / "release.json"
        self.signers = Path(self.temp.name) / "allowed-signers"
        self.record = {
            "schema_version": 2, "tag": "sdk-v9.9.9",
            "source": {"repo": "scuba-sc/sco-core", "sha": self.sha, "dependencies": []},
            "components_manifest_sha256": hashlib.sha256(
                (self.root / "sdk/components.json").read_bytes()).hexdigest(),
            "builder": {"workflow_path": ".github/workflows/build.yml", "workflow_sha": self.sha,
                        "run_id": 123, "run_attempt": 1},
            "artifacts": [{"name": p.name, "role": "sbom" if p.suffix == ".json" else "zip",
                           "size": p.stat().st_size, "sha256": hashlib.sha256(p.read_bytes()).hexdigest(),
                           "artifact_id": 456} for p in sorted(self.payload.iterdir())],
            "profiles": ["core", "game-sc", "legacy"],
            "distribution": {"status": "approved-for-publication"},
            "approved_by": "maintainer", "approved_at": "2026-10-11T12:00:00Z",
        }

    def command(self, *args, **kwargs):
        return subprocess.run(args, cwd=self.root, capture_output=True, check=True, **kwargs)

    def sign(self, namespace="sco-release-v1", key="key"):
        if shutil.which("ssh-keygen") is None:
            self.skipTest("ssh-keygen absent: signature tests require OpenSSH")
        keyfile = self.root / key
        if not keyfile.exists():
            self.command("ssh-keygen", "-t", "ed25519", "-N", "", "-f", str(keyfile))
        (self.root / "release.json.sig").unlink(missing_ok=True)
        self.command("ssh-keygen", "-Y", "sign", "-f", str(keyfile), "-n", namespace, str(self.manifest))
        if not self.signers.exists():
            self.signers.write_text("sco-maintainer " + keyfile.with_suffix(".pub").read_text())

    def verify(self, *, env=None, **overrides):
        options = {"manifest": self.manifest, "signature": self.root / "release.json.sig",
                   "allowed-signers": self.signers, "payload-dir": self.payload,
                   "tag": "sdk-v9.9.9", "source-sha": self.sha, "run-id": 123,
                   "now": "2026-10-11T13:00:00Z"}
        options.update(overrides)
        args = [sys.executable, str(TOOL), "verify"]
        for k, v in options.items():
            if v is not None:
                args.extend(["--" + k, str(v)])
        return subprocess.run(args, cwd=self.root, capture_output=True, text=True, env=env)

    def write_record(self):
        self.manifest.write_text(json.dumps(self.record, indent=2) + "\n")

    def assert_refused(self, result, reason):
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertEqual(result.stdout, "")
        self.assertTrue(result.stderr.splitlines(), "missing refusal reason")
        self.assertTrue(all(s.startswith("refused: ") for s in result.stderr.splitlines()), result.stderr)
        self.assertIn(reason, result.stderr)

    def test_valid_release(self):
        self.write_record()
        self.sign()
        result = self.verify()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")

    def test_signed_rc_tag_is_distinct_from_final(self):
        self.record["tag"] = "sdk-v9.9.9-rc.2"
        self.write_record()
        self.sign()
        self.assertEqual(self.verify(tag="sdk-v9.9.9-rc.2").returncode, 0)
        self.assert_refused(self.verify(tag="sdk-v9.9.9"), "tag mismatch")

    def test_malformed_rc_tag_refused_by_schema(self):
        for tag in ("sdk-v9.9.9-rc.0", "sdk-v9.9.9-rc.02", "sdk-v9.9.9-rc.2-extra"):
            with self.subTest(tag=tag):
                self.record["tag"] = tag
                self.write_record()
                self.assert_refused(self.verify(tag=tag), "$.tag")

    def test_valid_five_profile_release(self):
        self.record["profiles"] = ["core", "game-sc", "legacy", "core-source", "host-source-sc"]
        self.write_record()
        self.sign()
        result = self.verify()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")

    def test_five_profile_release_still_requires_signature(self):
        self.record["profiles"] = ["core", "game-sc", "legacy", "core-source", "host-source-sc"]
        self.write_record()
        self.assert_refused(self.verify(), "signature")

    def test_incomplete_source_profile_sets_are_refused(self):
        for profiles in (["core", "game-sc", "legacy", "host-source-sc"],
                         ["core", "game-sc", "legacy", "core-source"],
                         ["core", "game-sc", "legacy", "host-source-sc", "core-source"]):
            with self.subTest(profiles=profiles):
                self.refused_record(lambda r: r.update(profiles=profiles), "$.profiles")

    def approval_commit(self, *extra_paths):
        self.write_record()
        self.sign()
        decisions = self.root / "docs/legal/release-decisions"
        decisions.mkdir(parents=True)
        for name in ("release.json", "release.json.sig"):
            shutil.copyfile(self.root / name, decisions / name)
        for name in extra_paths:
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("changed\n")
        self.command("git", "add", "docs/legal/release-decisions", *extra_paths)
        self.command("git", "-c", "user.name=maintainer", "-c",
                     "user.email=maintainer@example.invalid", "commit", "-qm", "approval")
        return self.command("git", "rev-parse", "HEAD").stdout.decode().strip()

    def test_approval_only_descendant_passes(self):
        published = self.approval_commit()
        result = self.verify(**{"source-sha": published})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")

    def refused_record(self, edit, reason):
        edit(self.record)
        self.write_record()
        self.sign()
        self.assert_refused(self.verify(), reason)

    def test_missing_sha(self):
        self.refused_record(lambda r: r["source"].pop("sha"), "$.source: invalid or missing field")

    def test_malformed_sha(self):
        for field in ("source", "workflow", "components", "artifact", "dependency"):
            with self.subTest(field=field):
                record = json.loads(json.dumps(self.record))
                if field == "source": record["source"]["sha"] = "bad"
                if field == "workflow": record["builder"]["workflow_sha"] = "bad"
                if field == "components": record["components_manifest_sha256"] = "bad"
                if field == "artifact": record["artifacts"][0]["sha256"] = "bad"
                if field == "dependency": record["source"]["dependencies"] = [{"path": "external/lib", "sha": "bad"}]
                self.manifest.write_text(json.dumps(record))
                self.sign()
                self.assert_refused(self.verify(), "invalid or missing field")

    def test_blank_approver(self):
        self.refused_record(lambda r: r.update(approved_by=" \t"), "$.approved_by")

    def test_blank_date(self):
        self.refused_record(lambda r: r.update(approved_at=""), "$.approved_at")

    def test_duplicate_keys(self):
        self.write_record()
        self.manifest.write_text(self.manifest.read_text().replace('"schema_version": 2', '"schema_version": 2, "schema_version": 2'))
        self.sign()
        self.assert_refused(self.verify(), "duplicate JSON key")

    def test_nested_duplicate_keys(self):
        self.write_record()
        self.manifest.write_text(self.manifest.read_text().replace('"run_id": 123', '"run_id": 123, "run_id": 123'))
        self.sign()
        self.assert_refused(self.verify(), "duplicate JSON key")

    def test_unknown_property(self):
        self.refused_record(lambda r: r.update(unknown="not allowed"), "unknown property")

    def test_nested_unknown_property(self):
        self.refused_record(lambda r: r["builder"].update(unknown="not allowed"), "$.builder: unknown property")

    def test_expired_record(self):
        self.refused_record(lambda r: r.update(approved_at="2026-10-01T12:00:00Z"), "approval expired")

    def test_wrong_key(self):
        self.write_record()
        self.sign()
        self.sign(key="wrong-key")
        self.assert_refused(self.verify(), "signature verification failed")

    def test_revoked_key(self):
        self.write_record()
        self.sign()
        self.signers.write_text("# signing key removed from the independent policy\n")
        self.assert_refused(self.verify(), "signature verification failed")

    def test_checkout_trust_file_refused(self):
        self.write_record()
        self.sign()
        inside = self.root / "allowed-signers"
        inside.write_bytes(self.signers.read_bytes())
        self.assert_refused(self.verify(**{"allowed-signers": inside}), "trust file is inside the checkout")

    def test_symlink_to_checkout_trust_file_refused(self):
        self.write_record()
        self.sign()
        inside = self.root / "allowed-signers"
        inside.write_bytes(self.signers.read_bytes())
        outside_link = Path(self.temp.name) / "trust-link"
        outside_link.symlink_to(inside)
        self.assert_refused(self.verify(**{"allowed-signers": outside_link}), "trust file is inside the checkout")

    def test_checkout_subdirectory_trust_file_refused(self):
        self.write_record()
        self.sign()
        inside = self.root / "sdk/allowed-signers"
        inside.write_bytes(self.signers.read_bytes())
        self.assert_refused(self.verify(**{"allowed-signers": inside}), "trust file is inside the checkout")

    def test_wrong_namespace(self):
        self.write_record()
        self.sign(namespace="wrong-namespace")
        self.assert_refused(self.verify(), "signature verification failed")

    def test_signature_over_modified_bytes(self):
        self.write_record()
        self.sign()
        self.manifest.write_bytes(self.manifest.read_bytes() + b" \n")
        self.assert_refused(self.verify(), "signature verification failed")

    def test_extra_file(self):
        self.write_record()
        self.sign()
        (self.payload / "extra.txt").write_bytes(b"extra")
        self.assert_refused(self.verify(), "extra payload file")

    def test_missing_file(self):
        self.write_record()
        self.sign()
        (self.payload / "sdk.zip").unlink()
        self.assert_refused(self.verify(), "missing payload file")

    def test_swapped_sbom(self):
        self.write_record()
        self.sign()
        (self.payload / "sdk.spdx.json").write_bytes(b"sbom-v2")
        self.assert_refused(self.verify(), "artifact SHA256 mismatch")

    def test_wrong_source_archive(self):
        source = self.payload / "core-source.zip"
        source.write_bytes(b"synthetic source archive v1")
        self.record["artifacts"].append({
            "name": source.name, "role": "source", "size": source.stat().st_size,
            "sha256": hashlib.sha256(source.read_bytes()).hexdigest(), "artifact_id": 456,
        })
        self.write_record()
        self.sign()
        source.write_bytes(b"synthetic source archive v2")
        self.assert_refused(self.verify(), "artifact SHA256 mismatch")

    def test_regenerated_zip_after_approval(self):
        self.write_record()
        self.sign()
        # Same size, different bytes: a rebuilt file is not the approved candidate.
        (self.payload / "sdk.zip").write_bytes(b"rebuilt")
        self.assert_refused(self.verify(), "artifact SHA256 mismatch")

    def test_changed_size(self):
        self.refused_record(lambda r: r["artifacts"][0].update(size=999), "artifact size mismatch")

    def test_duplicate_filename(self):
        self.refused_record(lambda r: r["artifacts"].append(dict(r["artifacts"][0])), "duplicate artifact filename")

    def test_path_traversal_name(self):
        self.refused_record(lambda r: r["artifacts"][0].update(name="../sdk.zip"), "$.artifacts[0].name")

    def test_different_run_id(self):
        self.write_record()
        self.sign()
        self.assert_refused(self.verify(**{"run-id": 999}), "builder run id mismatch")

    def test_unknown_published_object_refused(self):
        self.write_record()
        self.sign()
        self.assert_refused(self.verify(**{"source-sha": "a" * 40}), "git results unavailable")

    def test_equal_source_sha_passes(self):
        self.write_record()
        self.sign()
        result = self.verify(**{"source-sha": self.record["source"]["sha"]})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")

    def test_non_approval_change_refused(self):
        published = self.approval_commit("sdk/components.json")
        self.assert_refused(self.verify(**{"source-sha": published}),
                            "non-approval path changed: 'sdk/components.json'")

    def test_non_ancestor_source_refused(self):
        published = self.approval_commit()
        self.command("git", "checkout", "--detach", self.sha)
        self.command("git", "-c", "user.name=maintainer", "-c",
                     "user.email=maintainer@example.invalid", "commit", "--allow-empty", "-qm", "sibling")
        sibling = self.command("git", "rev-parse", "HEAD").stdout.decode().strip()
        self.record["source"]["sha"] = sibling
        self.write_record()
        self.sign()
        self.assert_refused(self.verify(**{"source-sha": published}), "not an ancestor")

    def test_status_internal_only(self):
        self.refused_record(lambda r: r["distribution"].update(status="internal-only"), "not approved for publication")

    def test_status_hold(self):
        self.refused_record(lambda r: r["distribution"].update(status="hold"), "not approved for publication")

    def test_partial_profiles(self):
        self.refused_record(lambda r: r.update(profiles=["core"]), "$.profiles")

    def test_profile_order_mismatch(self):
        self.refused_record(lambda r: r.update(profiles=["legacy", "game-sc", "core"]), "$.profiles")

    def test_different_tag(self):
        self.write_record()
        self.sign()
        self.assert_refused(self.verify(tag="sdk-v1.2.3"), "tag mismatch")

    def test_components_hash_mismatch(self):
        self.refused_record(lambda r: r.update(components_manifest_sha256="a" * 64), "components manifest SHA mismatch")

    def test_distinct_workflow_commit_passes(self):
        self.command("git", "-c", "user.name=maintainer", "-c", "user.email=maintainer@example.invalid",
                     "commit", "--allow-empty", "-qm", "other commit")
        other = self.command("git", "rev-parse", "HEAD").stdout.decode().strip()
        self.record["builder"]["workflow_sha"] = other
        self.write_record()
        self.sign()
        result = self.verify()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_changed_workflow_commit_refused(self):
        (self.root / ".github/workflows/build.yml").write_text("name: changed workflow\n")
        self.command("git", "add", ".github/workflows/build.yml")
        self.command("git", "-c", "user.name=maintainer", "-c", "user.email=maintainer@example.invalid",
                     "commit", "-qm", "changed workflow")
        self.record["builder"]["workflow_sha"] = self.command("git", "rev-parse", "HEAD").stdout.decode().strip()
        self.write_record()
        self.sign()
        self.assert_refused(self.verify(), "builder workflow differs from source workflow")

    def test_approval_only_descendant_with_same_workflow_passes(self):
        self.command("git", "-c", "user.name=maintainer", "-c", "user.email=maintainer@example.invalid",
                     "commit", "--allow-empty", "-qm", "same workflow")
        self.record["builder"]["workflow_sha"] = self.command("git", "rev-parse", "HEAD").stdout.decode().strip()
        published = self.approval_commit()
        result = self.verify(**{"source-sha": published})
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_missing_workflow_commit(self):
        self.refused_record(lambda r: r["builder"].update(workflow_sha="a" * 40), "git results unavailable")

    def test_missing_workflow(self):
        self.refused_record(lambda r: r["builder"].update(workflow_path=".github/workflows/absent.yml"), "builder workflow unavailable")

    def test_missing_git_results(self):
        self.write_record()
        self.sign()
        shutil.rmtree(self.root / ".git")
        self.assert_refused(self.verify(), "git results unavailable")

    def failed_git_command(self, command):
        published = self.approval_commit()
        binary = shutil.which("git")
        self.assertIsNotNone(binary)
        assert binary is not None
        shim_dir = Path(self.temp.name) / "bin"
        shim_dir.mkdir()
        shim = shim_dir / "git"
        shim.write_text('#!/bin/sh\nif [ "$3" = ' + shlex.quote(command)
                        + ' ]; then exit 2; fi\nexec ' + shlex.quote(binary) + ' "$@"\n')
        shim.chmod(0o755)
        env = dict(os.environ, PATH=str(shim_dir) + os.pathsep + os.environ["PATH"])
        self.assert_refused(self.verify(env=env, **{"source-sha": published}), "git results unavailable")

    def test_failed_ancestry_command(self):
        self.failed_git_command("merge-base")

    def test_failed_diff_command(self):
        self.failed_git_command("diff")

    def test_missing_source_object(self):
        self.record["source"]["sha"] = "a" * 40
        self.write_record()
        self.sign()
        self.assert_refused(self.verify(**{"source-sha": "a" * 40}), "git results unavailable")

    def test_missing_allowed_signers_argument(self):
        self.assert_refused(self.verify(**{"allowed-signers": None}), "invalid command arguments")

    def test_missing_signature(self):
        self.write_record()
        self.sign()
        (self.root / "release.json.sig").unlink()
        self.assert_refused(self.verify(), "signature verification failed")

    def test_future_approval(self):
        self.refused_record(lambda r: r.update(approved_at="2026-10-12T12:00:00Z"), "approval is in the future")

    def test_payload_symlink(self):
        self.write_record()
        self.sign()
        (self.payload / "sdk.zip").unlink()
        (self.payload / "sdk.zip").symlink_to(self.root / "sdk/components.json")
        self.assert_refused(self.verify(), "not a regular file")

    def test_payload_subdirectory(self):
        self.write_record()
        self.sign()
        (self.payload / "nested").mkdir()
        self.assert_refused(self.verify(), "not a regular file")

    def test_secret_not_echoed(self):
        secret = "sensitive-fixture-value"
        self.record["source"]["sha"] = secret
        self.record[secret] = secret
        self.write_record()
        self.sign()
        result = self.verify()
        self.assert_refused(result, "invalid or missing field")
        self.assertNotIn(secret, result.stderr + result.stdout)

    def test_optional_run_id(self):
        self.write_record()
        self.sign()
        result = self.verify(**{"run-id": None})
        self.assertEqual(result.returncode, 0, result.stderr)

    def pin_recursive_dependencies(self):
        child = self.root / "external/library"
        grandchild = child / "vendor/nested"
        for repo in (grandchild, child):
            repo.mkdir(parents=True, exist_ok=True)
            subprocess.run(["git", "init", "-q", str(repo)], check=True, capture_output=True)
            subprocess.run(["git", "-C", str(repo), "-c", "user.name=maintainer", "-c",
                            "user.email=maintainer@example.invalid", "commit", "--allow-empty", "-qm", "dependency"],
                           check=True, capture_output=True)
        nested_sha = self.command("git", "-C", str(grandchild), "rev-parse", "HEAD").stdout.decode().strip()
        self.command("git", "-C", str(child), "update-index", "--add", "--cacheinfo", "160000," + nested_sha + ",vendor/nested")
        self.command("git", "-C", str(child), "-c", "user.name=maintainer", "-c",
                     "user.email=maintainer@example.invalid", "commit", "-qm", "nested pin")
        child_sha = self.command("git", "-C", str(child), "rev-parse", "HEAD").stdout.decode().strip()
        self.command("git", "update-index", "--add", "--cacheinfo", "160000," + child_sha + ",external/library")
        self.command("git", "-c", "user.name=maintainer", "-c", "user.email=maintainer@example.invalid", "commit", "-qm", "dependency pin")
        self.sha = self.command("git", "rev-parse", "HEAD").stdout.decode().strip()
        self.record["source"].update(sha=self.sha, dependencies=[
            {"path": "external/library", "sha": child_sha},
            {"path": "external/library/vendor/nested", "sha": nested_sha}])
        self.record["builder"]["workflow_sha"] = self.sha
        return child

    def test_recursive_dependencies_pass(self):
        self.pin_recursive_dependencies()
        self.write_record()
        self.sign()
        result = self.verify()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_recursive_dependency_missing_from_manifest(self):
        self.pin_recursive_dependencies()
        self.refused_record(lambda r: r["source"]["dependencies"].pop(), "recursive source dependencies mismatch")

    def test_recursive_dependency_wrong_sha(self):
        self.pin_recursive_dependencies()
        self.refused_record(lambda r: r["source"]["dependencies"][1].update(sha="a" * 40), "recursive source dependencies mismatch")

    def test_recursive_dependency_checkout_missing(self):
        child = self.pin_recursive_dependencies()
        shutil.rmtree(child / "vendor/nested/.git")
        self.write_record()
        self.sign()
        self.assert_refused(self.verify(), "recursive dependency checkout unavailable")

    def test_duplicate_dependency_path(self):
        self.pin_recursive_dependencies()
        self.refused_record(lambda r: r["source"]["dependencies"].append(dict(r["source"]["dependencies"][0])), "duplicate dependency path")

    def test_unsafe_dependency_path(self):
        self.refused_record(lambda r: r["source"].update(dependencies=[{"path": "../outside", "sha": "a" * 40}]), "unsafe or duplicate dependency path")

    def test_age_exact_boundary(self):
        self.write_record()
        self.sign()
        result = self.verify(now="2026-10-18T12:00:00Z")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_age_fractional_second_expired(self):
        self.write_record()
        self.sign()
        self.assert_refused(self.verify(now="2026-10-18T12:00:00.001Z"), "approval expired")

    def test_nonfinite_json(self):
        self.write_record()
        self.manifest.write_text(self.manifest.read_text().replace('"run_id": 123', '"run_id": NaN'))
        self.sign()
        self.assert_refused(self.verify(), "non-finite JSON number")

    def test_invalid_now(self):
        self.write_record()
        self.sign()
        self.assert_refused(self.verify(now="not a timestamp"), "invalid verification time")

    def test_non_utc_approval(self):
        self.refused_record(lambda r: r.update(approved_at="2026-10-11T12:00:00+01:00"), "$.approved_at")

    def test_negative_age_limit(self):
        self.write_record()
        self.sign()
        self.assert_refused(self.verify(**{"max-age-days": -1}), "invalid verification time or age limit")

    def test_reserved_artifact_name(self):
        self.refused_record(lambda r: r["artifacts"][0].update(name="CON.txt"), "unsafe artifact name")

    def test_boolean_schema_version(self):
        self.refused_record(lambda r: r.update(schema_version=True), "invalid or missing field")

    def test_inventory_refuses_symlinks(self):
        out = self.payload / "SHA256SUMS"
        out.symlink_to(self.root / "sdk/components.json")
        result = subprocess.run([sys.executable, str(TOOL), "inventory", "--payload-dir", str(self.payload),
                                 "--out", str(out)], cwd=self.root, capture_output=True, text=True)
        self.assert_refused(result, "not a regular file")

    def test_multiple_failures_are_reported(self):
        self.record["distribution"]["status"] = "hold"
        self.write_record()
        self.sign()
        (self.payload / "sdk.zip").unlink()
        (self.payload / "extra.txt").write_text("extra")
        result = self.verify(tag="sdk-v1.2.3", **{"run-id": 999})
        self.assert_refused(result, "tag mismatch")
        for reason in ("run id mismatch", "not approved for publication", "missing payload file", "extra payload file"):
            self.assertIn(reason, result.stderr)

    def test_deep_json_refused_without_traceback(self):
        self.manifest.write_text("[" * 20000 + "0" + "]" * 20000)
        result = self.verify()
        self.assert_refused(result, "invalid JSON")

    def test_inventory_sorted_excludes_itself(self):
        out = self.payload / "SHA256SUMS"
        out.write_text("stale")
        result = self.command(sys.executable, str(TOOL), "inventory", "--payload-dir", str(self.payload), "--out", str(out))
        expected = "".join(f"{a['sha256']}  {a['name']}\n" for a in self.record["artifacts"])
        self.assertEqual(out.read_text(), expected)
        self.assertEqual(result.stderr, b"")
        self.command(sys.executable, str(TOOL), "inventory", "--payload-dir", str(self.payload), "--out", str(out))
        self.assertEqual(out.read_text(), expected)


if __name__ == "__main__":
    unittest.main(verbosity=2)
