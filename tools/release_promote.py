#!/usr/bin/env python3
"""Promote approved candidate bytes without rebuilding.

All GitHub I/O passes through one injectable function accepting gh arguments and
returning stdout bytes. Payload/signature verification remains release_verify's
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


def fetch_candidate(approval, repo, payload, client=github):
    """Download only the signed inventory's exact immutable artifact IDs."""
    run_id = positive_id(approval["builder"]["run_id"])
    names, artifact_ids = set(), {}
    for artifact in approval["artifacts"]:
        name = artifact["name"]
        if (not isinstance(name, str) or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", name)
                or name in {"release.json", "release.json.sig"} or name in names):
            raise Refusal("unsafe, duplicate or reserved payload name")
        names.add(name)
        artifact_ids.setdefault(positive_id(artifact["artifact_id"]), set()).add(name)
    if not names:
        raise Refusal("empty approved payload")
    run = api(client, f"repos/{repo}/actions/runs/{run_id}")
    if (run["path"] != ".github/workflows/release-candidate.yml"
            or run["event"] != "workflow_dispatch" or run["head_branch"] != "main"
            or run["status"] != "completed" or run["conclusion"] != "success"):
        raise Refusal("approved builder is not a successful main release-candidate run")
    payload.mkdir()
    downloaded = set()
    for artifact_id in sorted(artifact_ids):
        metadata = api(client, f"repos/{repo}/actions/artifacts/{artifact_id}")
        if metadata["expired"] or metadata["workflow_run"]["id"] != run_id:
            raise Refusal("artifact is expired or belongs to another builder run")
        data = client("api", f"repos/{repo}/actions/artifacts/{artifact_id}/zip")
        with zipfile.ZipFile(io.BytesIO(data)) as bundle:
            for member in bundle.infolist():
                # upload-artifact stores a flat payload. Never extract arbitrary paths.
                if (member.filename not in artifact_ids[artifact_id] or member.filename in downloaded
                        or member.is_dir() or stat.S_ISLNK(member.external_attr >> 16)):
                    raise Refusal("unexpected, duplicate or unsafe candidate member")
                (payload / member.filename).write_bytes(bundle.read(member))
                downloaded.add(member.filename)
    if downloaded != names:
        raise Refusal("candidate does not contain exactly the approved file set")
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


def create_draft(repo, tag, manifest, signature, payload, runner_temp, downloaded, version, client=github):
    """Create a complete draft and round-trip its exact asset set by numeric ID.

    The caller must verify the downloaded payload before calling publish_draft.
    Refusals deliberately leave an unpublished draft available for investigation.
    """
    approval = json.loads(manifest.read_bytes())
    names = [artifact["name"] for artifact in approval["artifacts"]]
    staging = runner_temp / "release-approval"
    staging.mkdir()
    (staging / "release.json").write_bytes(manifest.read_bytes())
    (staging / "release.json.sig").write_bytes(signature.read_bytes())
    # gh release create does not itself refuse an existing draft.
    if tagged_releases(repo, tag, client):
        raise Refusal("a draft or published release already exists for this tag")
    client("release", "create", tag, *[str(payload / name) for name in names],
           str(staging / "release.json"), str(staging / "release.json.sig"),
           "--repo", repo, "--draft", "--verify-tag", "--title", f"sco-core SDK {version}",
           "--notes-file", str(runner_temp / "notes.md"))
    # GET /releases/tags/{tag} retrieves published releases, not drafts.
    matches = tagged_releases(repo, tag, client)
    if len(matches) != 1 or not matches[0]["draft"]:
        raise Refusal("release is not the unique expected draft")
    release_id = matches[0]["id"]
    assets = release_assets(repo, release_id, client)
    (runner_temp / "draft-assets.json").write_text(json.dumps(assets))
    expected = set(names) | {"release.json", "release.json.sig"}
    if len(assets) != len(expected) or {asset["name"] for asset in assets} != expected:
        raise Refusal("draft assets do not exactly match the approved release set")
    downloaded.mkdir()
    for asset in assets:
        if asset["state"] != "uploaded":
            raise Refusal("draft asset upload is incomplete")
        print(f"Draft asset {asset['id']}: {asset['name']}", flush=True)
        data = client("api", f"repos/{repo}/releases/assets/{asset['id']}",
                      "-H", "Accept: application/octet-stream")
        (downloaded / asset["name"]).write_bytes(data)
    # The downloaded approval must equal the committed approval byte for byte.
    for name, original in (("release.json", manifest), ("release.json.sig", signature)):
        path = downloaded / name
        if path.read_bytes() != original.read_bytes():
            raise Refusal("draft approval differs from the committed approval")
        path.unlink()  # release_verify requires only manifest payload files here.
    return release_id


def asset_identity(items):
    return sorted((a["id"], a["name"], a["size"], a["state"], a["updated_at"]) for a in items)


def publish_draft(repo, tag, release_id, runner_temp, client=github):
    """Publish only the same unique draft with its unchanged verified inventory."""
    releases = tagged_releases(repo, tag, client)
    if len(releases) != 1 or releases[0]["id"] != release_id or not releases[0]["draft"]:
        raise Refusal("verified draft changed or its tag is ambiguous")
    assets = release_assets(repo, release_id, client)
    original = json.loads((runner_temp / "draft-assets.json").read_bytes())
    if asset_identity(assets) != asset_identity(original):
        raise Refusal("draft asset inventory changed after verification")
    client("release", "edit", tag, "--repo", repo, "--draft=false")
    published = api(client, f"repos/{repo}/releases/{release_id}")
    if published["draft"] or published["tag_name"] != tag:
        raise Refusal("the verified draft was not published")
    return published["html_url"]


def candidate_summary(summary_path, dist, tag, sha, run_id, artifact_id):
    """Append every candidate file's approval inputs to the workflow summary."""
    with summary_path.open("a") as summary:
        summary.write(f"## Candidate for {tag}\n\n")
        summary.write(f"Source SHA: `{sha}`\n\n")
        summary.write(f"builder.run_id: `{run_id}`\n\n")
        summary.write(f"artifacts[].artifact_id (same bundle for every file): `{artifact_id}`\n\n")
        summary.write("| artifacts[].name | bytes | sha256 |\n|---|---:|---|\n")
        for path in sorted(dist.iterdir()):
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
            repo, tag = env["GITHUB_REPOSITORY"], env["GITHUB_REF_NAME"]
            manifest = Path("docs/legal/release-decisions") / f"{tag}.json"
            if command == "fetch":
                run_id = fetch_candidate(json.loads(manifest.read_bytes()), repo, Path("payload"), client)
                with open(env["GITHUB_OUTPUT"], "a") as output:
                    output.write(f"run-id={run_id}\n")
            elif command == "draft":
                release_id = create_draft(repo, tag, manifest, manifest.with_suffix(".json.sig"),
                                          Path("payload"), Path(env["RUNNER_TEMP"]), Path("downloaded"),
                                          env["VERSION"], client)
                with open(env["GITHUB_ENV"], "a") as output:
                    output.write(f"DRAFT_RELEASE_ID={release_id}\n")
            else:
                print(publish_draft(repo, tag, int(env["DRAFT_RELEASE_ID"]), Path(env["RUNNER_TEMP"]), client))
    except (Refusal, OSError, ValueError, KeyError, TypeError, zipfile.BadZipFile):
        exc = sys.exc_info()[1]
        print("refused: " + (str(exc) if isinstance(exc, Refusal) else "promotion input unavailable"),
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
