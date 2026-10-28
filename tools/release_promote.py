#!/usr/bin/env python3
"""Promote candidate bytes without rebuilding.

All GitHub I/O passes through one injectable function accepting gh arguments and
returning stdout bytes. Payload verification remains release_verify's
responsibility; workflows retain those checks before and after the draft round trip.
"""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import sys
import zipfile

sys.dont_write_bytecode = True
from doc_release_gate import base_tag
import release_verify as verify


class Refusal(Exception):
    """A promotion prerequisite could not be established."""


def github(*args):
    """The only GitHub client boundary, for JSON, binary downloads and writes."""
    try:
        return subprocess.check_output(["gh", *args], stderr=subprocess.PIPE)
    except (OSError, subprocess.CalledProcessError) as exc:
        raise Refusal("GitHub command unavailable") from exc


def api(client, path):
    return json.loads(client("api", path))


def api_pages(client, path):
    return json.loads(client("api", "--paginate", "--slurp", path))


def positive_id(value):
    if type(value) is not int or value <= 0:
        raise Refusal("invalid builder run ID or artifact ID")
    return value


def fetch_candidate(repo, tag, source_sha, run_id, artifact_id, payload, manifest, client=github):
    """Fetch a single immutable candidate and generate its unsigned inventory."""
    run_id, artifact_id = positive_id(run_id), positive_id(artifact_id)
    run = api(client, f"repos/{repo}/actions/runs/{run_id}")
    if (run["path"] != verify.WORKFLOW or run["event"] != "workflow_dispatch"
            or run["head_branch"] != "main" or run["status"] != "completed"
            or run["conclusion"] != "success"):
        raise Refusal("builder is not a successful main release-candidate run")
    # Artifact metadata identifies the run but not its attempt. A fresh dispatch
    # removes ambiguity between artifacts from different attempts of the same run.
    if run.get("head_sha") != source_sha or run.get("run_attempt") != 1:
        raise Refusal("builder source or attempt mismatch; use a fresh dispatch")
    metadata = api(client, f"repos/{repo}/actions/artifacts/{artifact_id}")
    if (metadata["expired"] or metadata["workflow_run"]["id"] != run_id
            or metadata["name"] != "release-candidate"):
        raise Refusal("artifact is expired, wrong name, or belongs to another run")
    expected = verify.expected_payload(Path.cwd(), source_sha, tag)
    if type(metadata.get("size_in_bytes")) is not int or not 0 < metadata["size_in_bytes"] <= 2 * 1024**3:
        raise Refusal("candidate artifact archive size invalid")
    data = client("api", f"repos/{repo}/actions/artifacts/{artifact_id}/zip")
    if (metadata.get("size_in_bytes") != len(data) or
            metadata.get("digest") != "sha256:" + hashlib.sha256(data).hexdigest()):
        raise Refusal("candidate artifact archive digest or size mismatch")
    payload.mkdir()
    downloaded = set()
    with zipfile.ZipFile(io.BytesIO(data)) as bundle:
        members = bundle.infolist()
        if len(members) != len(expected) or sum(m.file_size for m in members) > 2 * 1024**3:
            raise Refusal("candidate archive has invalid member count or size")
        for member in members:
            mode = member.external_attr >> 16
            if (member.filename not in expected or member.filename in downloaded
                    or member.is_dir() or member.file_size > 1024**3
                    or stat.S_IFMT(mode) not in (0, stat.S_IFREG)):
                raise Refusal("unexpected, duplicate or unsafe candidate member")
            (payload / member.filename).write_bytes(bundle.read(member))
            downloaded.add(member.filename)
    if downloaded != set(expected):
        raise Refusal("candidate does not contain exactly the expected file set")
    artifacts = []
    for name in sorted(expected):
        size, digest = verify.digest_file(payload / name)
        artifacts.append({"name": name, "role": expected[name], "size": size,
                          "sha256": digest, "artifact_id": artifact_id})
    dependencies = verify.dependencies(Path.cwd(), source_sha)
    record = {"schema_version": 3, "tag": tag,
              "source": {"repo": repo, "sha": source_sha,
                         "dependencies": [{"path": path, "sha": sha} for path, sha in sorted(dependencies.items())]},
              "components_manifest_sha256": hashlib.sha256(verify.git(Path.cwd(), "show", source_sha + ":sdk/components.json")).hexdigest(),
              "builder": {"workflow_path": verify.WORKFLOW, "workflow_sha": source_sha,
                          "run_id": run_id, "run_attempt": run["run_attempt"]},
              "profiles": verify.PROFILES, "artifacts": artifacts}
    manifest.write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    return run_id


REQUIRED = {
    "cmake": {"linux", "kernel-only", "windows"},
    "sdk": {"package", "windows", "csharp-host-sim"},
    "test": {"test"},
    "ip-compliance": {"audit"},
    "docs-site": {"docs-site"},
}


def require_prerequisites(repo, sha, ref, client=github):
    """Require each mandatory job and its owning main/push workflow on this SHA."""
    if ref != "refs/heads/main":
        raise Refusal("release candidates must be built from main")
    pages = api_pages(client, f"repos/{repo}/commits/{sha}/check-runs?filter=latest&per_page=100")
    runs, latest = {}, {}
    for page in pages:
        for check in page["check_runs"]:
            if check["app"]["slug"] != "github-actions" or check["head_sha"] != sha:
                continue
            match = re.search(r"/actions/runs/(\d+)/job/", check.get("details_url") or "")
            if not match:
                continue
            run_id = match.group(1)
            if run_id not in runs:
                runs[run_id] = api(client, f"repos/{repo}/actions/runs/{run_id}")
            run = runs[run_id]
            if run["head_sha"] != sha or run["event"] != "push" or run["head_branch"] != "main":
                continue
            for workflow, jobs in REQUIRED.items():
                if run["path"] != f".github/workflows/{workflow}.yml" or check["name"] not in jobs:
                    continue
                key = f"{workflow}/{check['name']}"
                if key not in latest or check["id"] > latest[key][0]["id"]:
                    latest[key] = (check, run)
    failures = []
    for workflow, jobs in REQUIRED.items():
        for job in sorted(jobs):
            key = f"{workflow}/{job}"
            pair = latest.get(key)
            passed = (pair is not None and pair[0]["status"] == "completed"
                      and pair[0]["conclusion"] == "success" and pair[1]["status"] == "completed"
                      and pair[1]["conclusion"] == "success")
            print(f"{key}: {'success' if passed else 'REFUSED'} ({sha})")
            if not passed:
                failures.append(key)
    if failures:
        raise Refusal("mandatory checks missing or not successful: " + ", ".join(failures))


def tagged_releases(repo, tag, client):
    return [release for page in api_pages(client, f"repos/{repo}/releases?per_page=100")
            for release in page if release["tag_name"] == tag]


def release_assets(repo, release_id, client):
    return [asset for page in api_pages(client, f"repos/{repo}/releases/{release_id}/assets?per_page=100")
            for asset in page]


def create_draft(repo, tag, manifest, payload, runner_temp, downloaded, version, client=github):
    """Create a complete draft and round-trip its exact asset set by numeric ID.

    The caller must verify the downloaded payload before calling publish_draft.
    Refusals deliberately leave an unpublished draft available for investigation.
    """
    record = json.loads(manifest.read_bytes())
    if base_tag(tag) != "sdk-v" + version:
        raise Refusal("release tag does not match SDK version")
    prerelease = tag != base_tag(tag)
    names = [artifact["name"] for artifact in record["artifacts"]]
    staging = runner_temp / "release-inventory"
    staging.mkdir()
    (staging / "release.json").write_bytes(manifest.read_bytes())
    # gh release create does not itself refuse an existing draft.
    if tagged_releases(repo, tag, client):
        raise Refusal("a draft or published release already exists for this tag")
    client("release", "create", tag, *[str(payload / name) for name in names],
           str(staging / "release.json"),
           "--repo", repo, "--draft", "--verify-tag", "--title", f"sco-core SDK {tag[5:]}",
           "--notes-file", str(runner_temp / "notes.md"), *(["--prerelease"] if prerelease else []))
    # GET /releases/tags/{tag} retrieves published releases, not drafts.
    matches = tagged_releases(repo, tag, client)
    if (len(matches) != 1 or not matches[0]["draft"]
            or matches[0].get("prerelease") is not prerelease):
        raise Refusal("release is not the unique expected draft")
    release_id = matches[0]["id"]
    assets = release_assets(repo, release_id, client)
    (runner_temp / "draft-assets.json").write_text(json.dumps(assets))
    expected = set(names) | {"release.json"}
    if len(assets) != len(expected) or {asset["name"] for asset in assets} != expected:
        raise Refusal("draft assets do not exactly match the verified release set")
    downloaded.mkdir()
    for asset in assets:
        if asset["state"] != "uploaded":
            raise Refusal("draft asset upload is incomplete")
        print(f"Draft asset {asset['id']}: {asset['name']}", flush=True)
        data = client("api", f"repos/{repo}/releases/assets/{asset['id']}",
                      "-H", "Accept: application/octet-stream")
        (downloaded / asset["name"]).write_bytes(data)
    # The inventory itself is transported and round-tripped byte-for-byte.
    path = downloaded / "release.json"
    if path.read_bytes() != manifest.read_bytes():
        raise Refusal("draft inventory differs from verified inventory")
    path.unlink()
    return release_id


def asset_identity(items):
    return sorted((a["id"], a["name"], a["size"], a["state"], a["updated_at"], a.get("digest")) for a in items)


def remote_tag(repo, tag, client):
    """Read the ref and peel annotated tags, with a finite bound and no guessing."""
    ref = api(client, f"repos/{repo}/git/ref/tags/{tag}")["object"]
    initial = (ref["type"], ref["sha"])
    seen = set()
    for _ in range(16):
        kind, sha = ref["type"], ref["sha"]
        if not isinstance(sha, str) or not verify.SHA.fullmatch(sha) or sha in seen:
            raise Refusal("remote tag object is invalid")
        seen.add(sha)
        if kind == "commit":
            return initial, sha
        if kind != "tag":
            raise Refusal("remote tag does not point to a commit")
        ref = api(client, f"repos/{repo}/git/tags/{sha}")["object"]
    raise Refusal("remote tag nesting exceeds limit")


def publish_draft(repo, tag, release_id, source_sha, runner_temp, client=github):
    """Publish by ID, then recheck the tag and assets.

    Concurrent contents writers remain trusted: checks detect observed changes but
    cannot prevent or reverse a transient publication between API calls.
    """
    base = base_tag(tag)
    if base is None:
        raise Refusal("invalid release tag")
    prerelease = tag != base
    releases = tagged_releases(repo, tag, client)
    if (len(releases) != 1 or releases[0]["id"] != release_id or not releases[0]["draft"]
            or releases[0].get("prerelease") is not prerelease):
        raise Refusal("verified draft changed or its tag is ambiguous")
    assets = release_assets(repo, release_id, client)
    original = json.loads((runner_temp / "draft-assets.json").read_bytes())
    if asset_identity(assets) != asset_identity(original):
        raise Refusal("draft asset inventory changed after verification")
    tag_identity, peeled = remote_tag(repo, tag, client)
    if peeled != source_sha:
        raise Refusal("remote tag does not match verified source")
    # An error on this request may occur after GitHub has applied it. Never
    # claim the release is still a draft when publication outcome is uncertain.
    try:
        client("api", "--method", "PATCH", f"repos/{repo}/releases/{release_id}", "-F", "draft=false")
        published = api(client, f"repos/{repo}/releases/{release_id}")
        if (published["id"] != release_id or published["draft"] or published["tag_name"] != tag
                or published.get("prerelease") is not prerelease):
            raise Refusal("the verified draft was not published")
        post_tag, post_sha = remote_tag(repo, tag, client)
        if (post_tag, post_sha) != (tag_identity, source_sha):
            raise Refusal("remote tag changed during publication")
        if asset_identity(release_assets(repo, release_id, client)) != asset_identity(original):
            raise Refusal("release assets changed during publication")
        releases = tagged_releases(repo, tag, client)
        if len(releases) != 1 or releases[0]["id"] != release_id or releases[0]["draft"]:
            raise Refusal("published release is no longer unique")
        return published["html_url"]
    except (Refusal, OSError, ValueError, KeyError, TypeError) as exc:
        detail = str(exc) if isinstance(exc, Refusal) else "publication verification unavailable"
        raise Refusal(detail + "; release may already be public; investigate immediately") from exc


def candidate_summary(summary_path, dist, tag, sha, run_id, artifact_id):
    """Append candidate IDs and file hashes to the workflow summary."""
    with summary_path.open("a") as summary:
        summary.write(f"## Candidate for {tag}\n\n")
        summary.write(f"Source SHA: `{sha}`\n\n")
        summary.write(f"builder.run_id: `{run_id}`\n\n")
        summary.write(f"artifacts[].artifact_id (same bundle for every file): `{artifact_id}`\n\n")
        summary.write("| artifacts[].name | bytes | sha256 |\n|---|---:|---|\n")
        for path in sorted(dist.iterdir(), key=lambda path: path.name):
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            summary.write(f"| {path.name} | {path.stat().st_size} | `{digest}` |\n")


class Parser(argparse.ArgumentParser):
    def error(self, message):
        raise Refusal("invalid command arguments")


def main(argv=None, environ=None, client=github):
    """Thin workflow CLI; environment and GitHub client are injectable offline."""
    env = os.environ if environ is None else environ
    parser = Parser(description=__doc__)
    parser.add_argument("command", choices=("fetch", "prerequisites", "draft", "publish", "summary"))
    try:
        command = parser.parse_args(argv).command
        if command == "prerequisites":
            require_prerequisites(env["GITHUB_REPOSITORY"], env["GITHUB_SHA"], env["GITHUB_REF"], client)
        elif command == "summary":
            candidate_summary(Path(env["GITHUB_STEP_SUMMARY"]), Path("dist"), env["RELEASE_TAG"],
                              env["GITHUB_SHA"], env["GITHUB_RUN_ID"], env["ARTIFACT_ID"])
        else:
            repo, tag = env["GITHUB_REPOSITORY"], env["RELEASE_TAG"]
            manifest = Path("release.json")
            if command == "fetch":
                run_id = fetch_candidate(repo, tag, env["GITHUB_SHA"], int(env["RUN_ID"]),
                                         int(env["ARTIFACT_ID"]), Path("payload"), manifest, client)
                with open(env["GITHUB_OUTPUT"], "a") as output:
                    output.write(f"run-id={run_id}\n")
            elif command == "draft":
                release_id = create_draft(repo, tag, manifest, Path("payload"), Path(env["RUNNER_TEMP"]), Path("downloaded"),
                                          env["VERSION"], client)
                with open(env["GITHUB_ENV"], "a") as output:
                    output.write(f"DRAFT_RELEASE_ID={release_id}\n")
            else:
                print(publish_draft(repo, tag, int(env["DRAFT_RELEASE_ID"]), env["GITHUB_SHA"], Path(env["RUNNER_TEMP"]), client))
    except (Refusal, verify.Refusal, OSError, ValueError, KeyError, TypeError, zipfile.BadZipFile):
        exc = sys.exc_info()[1]
        print("refused: " + (str(exc) if isinstance(exc, Refusal) else "promotion input unavailable"),
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
