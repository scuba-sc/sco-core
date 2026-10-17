#!/usr/bin/env python3
"""Promote approved candidate bytes without rebuilding.

All GitHub I/O passes through one injectable function accepting gh arguments and
returning stdout bytes. Payload/signature verification remains release_verify's
responsibility; workflows retain those checks before and after the draft round trip.
"""
import argparse
import fnmatch
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
from typing import TypedDict

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


class ReadinessJob(TypedDict):
    workflow: str
    path: str
    run_id: int
    attempt: int
    head_sha: str
    id: int
    job: str
    status: str
    conclusion: str


class ReadinessEvidence(TypedDict):
    schema_version: int
    repository: str
    source_sha: str
    jobs: list[ReadinessJob]


def require_prerequisites(repo, sha, ref, client=github, evidence_out=None):
    """Require each mandatory job and its owning main/push workflow on this SHA."""
    if ref != "refs/heads/main":
        raise Refusal("release candidates must be built from main")
    if evidence_out is not None:
        evidence = verified_readiness(repo, sha, client)
        Path(evidence_out).write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
        for job in evidence["jobs"]:
            print(f"{job['workflow']}/{job['job']}: success ({sha})")
        return evidence
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


def verified_readiness(repo, sha, client) -> ReadinessEvidence:
    """Select each latest main-push run and inspect its exact workflow attempt."""
    if not re.fullmatch(r"[0-9a-f]{40}", sha):
        raise Refusal("source SHA is invalid")
    jobs: list[ReadinessJob] = []
    for workflow, required_jobs in REQUIRED.items():
        path = f".github/workflows/{workflow}.yml"
        pages = api_pages(client, f"repos/{repo}/actions/workflows/{workflow}.yml/runs?branch=main&event=push&head_sha={sha}&per_page=100")
        runs = [run for page in pages for run in page["workflow_runs"]
                if run.get("path") == path and run.get("head_sha") == sha
                and run.get("head_branch") == "main" and run.get("event") == "push"
                and run.get("repository", {}).get("full_name") == repo
                and run.get("head_repository", {}).get("full_name") == repo]
        if not runs:
            raise Refusal(f"mandatory workflow missing: {workflow}")
        run = max(runs, key=lambda item: (item["run_number"], item["run_attempt"]))
        run_id, attempt = positive_id(run["id"]), positive_id(run["run_attempt"])
        if run.get("status") != "completed" or run.get("conclusion") != "success":
            raise Refusal(f"latest mandatory workflow is not successful: {workflow}")
        pages = api_pages(client, f"repos/{repo}/actions/runs/{run_id}/attempts/{attempt}/jobs?per_page=100")
        owned_jobs = [job for page in pages for job in page["jobs"]]
        for name in sorted(required_jobs):
            matches = [job for job in owned_jobs if job.get("name") == name]
            if len(matches) != 1:
                raise Refusal(f"mandatory job missing or duplicated: {workflow}/{name}")
            job = matches[0]
            if job.get("status") != "completed" or job.get("conclusion") != "success":
                raise Refusal(f"mandatory job is not successful: {workflow}/{name}")
            jobs.append({"workflow": workflow, "path": path, "run_id": run_id,
                         "attempt": attempt, "head_sha": sha, "id": positive_id(job["id"]),
                         "job": name, "status": "completed", "conclusion": "success"})
    return {"schema_version": 1, "repository": repo, "source_sha": sha, "jobs": jobs}


def auto_candidate(repo, event, root, client=github):
    """Revalidate one trusted workflow_run hint before candidate dispatch."""
    payload = event["workflow_run"]
    event_id, event_attempt = positive_id(payload["id"]), positive_id(payload["run_attempt"])
    sha = payload["head_sha"]
    allowed = {f".github/workflows/{name}.yml" for name in REQUIRED}
    if (not re.fullmatch(r"[0-9a-f]{40}", sha)
            or event.get("repository", {}).get("full_name") != repo
            or payload.get("head_repository", {}).get("full_name") != repo
            or payload.get("path") not in allowed or payload.get("event") != "push"
            or payload.get("head_branch") != "main" or payload.get("status") != "completed"
            or payload.get("conclusion") != "success"):
        raise Refusal("workflow_run event is not an eligible trusted main push")
    trigger = api(client, f"repos/{repo}/actions/runs/{event_id}")
    for key, expected in (("id", event_id), ("run_attempt", event_attempt), ("head_sha", sha),
                          ("path", payload["path"]), ("event", "push"),
                          ("head_branch", "main"), ("status", "completed"),
                          ("conclusion", "success")):
        if trigger.get(key) != expected:
            raise Refusal("workflow_run event is stale or does not match its API run")
    if (trigger.get("repository", {}).get("full_name") != repo
            or trigger.get("head_repository", {}).get("full_name") != repo):
        raise Refusal("workflow_run repository is not the trusted repository")
    readiness = verified_readiness(repo, sha, client)
    source_jobs = [job for job in readiness["jobs"] if job["path"] == payload["path"]]
    if (not source_jobs or any(job["run_id"] != event_id or job["attempt"] != event_attempt
                               for job in source_jobs)):
        raise Refusal("workflow_run event is not the latest run attempt")
    version = (root / "sdk/VERSION").read_text(encoding="utf-8").strip()
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version):
        raise Refusal("sdk/VERSION is not strict X.Y.Z")
    tag = f"sdk-v{version}"
    gate = subprocess.run([sys.executable, "tools/doc_release_gate.py", "--tag", tag],
                          cwd=root, capture_output=True)
    if gate.returncode:
        raise Refusal("documentation release gate failed")
    refs = [ref for page in api_pages(client, f"repos/{repo}/git/matching-refs/tags/{tag}") for ref in page]
    if any(ref.get("ref") == f"refs/tags/{tag}" for ref in refs):
        raise Refusal("release tag already exists")
    if tagged_releases(repo, tag, client):
        raise Refusal("a draft or published release already exists for this tag")
    pages = api_pages(client, f"repos/{repo}/actions/workflows/release-candidate.yml/runs?branch=main&head_sha={sha}&per_page=100")
    candidates = [run for page in pages for run in page["workflow_runs"]
                  if run.get("path") == ".github/workflows/release-candidate.yml"
                  and run.get("head_branch") == "main" and run.get("head_sha") == sha]
    if any(run.get("status") in ("queued", "in_progress")
           or run.get("status") == "completed" and run.get("conclusion") == "success"
           for run in candidates):
        return "duplicate-suppressed"
    if candidates:
        latest = max(candidates, key=lambda item: (item["run_number"], item["run_attempt"]))
        if latest.get("status") == "completed" and latest.get("conclusion") in ("failure", "cancelled"):
            raise Refusal("candidate failed or was cancelled; deliberately retry with manual workflow_dispatch or rerun the failed candidate workflow run, not readiness")
        raise Refusal("latest same-SHA candidate state is not eligible for automatic retry")
    main = api(client, f"repos/{repo}/git/ref/heads/main")
    if main["object"]["sha"] != sha:
        raise Refusal("main moved after readiness validation")
    try:
        client("api", f"repos/{repo}/actions/workflows/release-candidate.yml/dispatches",
               "-X", "POST", "-f", "ref=main", "-f", f"inputs[tag]={tag}",
               "-f", f"inputs[expected_source_sha]={sha}")
    except Refusal as exc:
        raise Refusal("candidate dispatch outcome unknown; inspect workflow runs before retry") from exc
    return "dispatched"


def safe_relative(name):
    """Reject unsafe archive member paths without extracting them."""
    return (isinstance(name, str) and bool(name) and not name.startswith("/")
            and "\\" not in name and ":" not in name
            and all(part not in ("", ".", "..") for part in name.split("/")))


def review_bundle(repo, tag, sha, run_id, attempt, artifact_id, readiness_artifact_id,
                  dist, output, root, client=github):
    """Write an unsigned, run-bound review copy outside the candidate payload."""
    run_id, attempt = positive_id(run_id), positive_id(attempt)
    artifact_id, readiness_artifact_id = positive_id(artifact_id), positive_id(readiness_artifact_id)
    if (not re.fullmatch(r"[0-9a-f]{40}", sha)
            or not re.fullmatch(r"sdk-v[0-9]+\.[0-9]+\.[0-9]+", tag)):
        raise Refusal("candidate identity is malformed")
    candidate = api(client, f"repos/{repo}/actions/runs/{run_id}")
    if (candidate.get("path") != ".github/workflows/release-candidate.yml"
            or candidate.get("event") != "workflow_dispatch" or candidate.get("head_branch") != "main"
            or candidate.get("head_sha") != sha or candidate.get("run_attempt") != attempt
            or candidate.get("repository", {}).get("full_name") != repo
            or candidate.get("head_repository", {}).get("full_name") != repo):
        raise Refusal("candidate run identity does not match the review bundle")
    candidate_meta = api(client, f"repos/{repo}/actions/artifacts/{artifact_id}")
    owner = candidate_meta.get("workflow_run", {})
    if (positive_id(candidate_meta.get("id")) != artifact_id or candidate_meta.get("expired")
            or candidate_meta.get("name") != "release-candidate"
            or owner.get("id") != run_id or owner.get("head_sha") != sha
            or type(owner.get("repository_id")) is not int
            or owner["repository_id"] != candidate.get("repository", {}).get("id")
            or type(owner.get("head_repository_id")) is not int
            or owner["head_repository_id"] != candidate.get("head_repository", {}).get("id")):
        raise Refusal("candidate artifact does not belong to this source run")
    readiness_meta = api(client, f"repos/{repo}/actions/artifacts/{readiness_artifact_id}")
    readiness_owner = readiness_meta.get("workflow_run", {})
    if (positive_id(readiness_meta.get("id")) != readiness_artifact_id or readiness_meta.get("expired")
            or readiness_meta.get("name") != "readiness-evidence"
            or readiness_owner.get("id") != run_id or readiness_owner.get("head_sha") != sha
            or type(readiness_owner.get("repository_id")) is not int
            or readiness_owner["repository_id"] != candidate.get("repository", {}).get("id")
            or type(readiness_owner.get("head_repository_id")) is not int
            or readiness_owner["head_repository_id"] != candidate.get("head_repository", {}).get("id")):
        raise Refusal("readiness artifact does not belong to this candidate run")
    with zipfile.ZipFile(io.BytesIO(client("api", f"repos/{repo}/actions/artifacts/{readiness_artifact_id}/zip"))) as bundle:
        evidence_files = [item for item in bundle.infolist() if item.filename == "readiness-evidence.json"]
        if (len(bundle.infolist()) != 1 or len(evidence_files) != 1
                or evidence_files[0].is_dir() or stat.S_ISLNK(evidence_files[0].external_attr >> 16)):
            raise Refusal("readiness artifact does not contain one safe evidence file")
        evidence = json.loads(bundle.read(evidence_files[0]))
    jobs = evidence.get("jobs")
    if (evidence.get("schema_version") != 1 or evidence.get("repository") != repo
            or evidence.get("source_sha") != sha or not isinstance(jobs, list)):
        raise Refusal("readiness evidence does not match this candidate source")
    identities = []
    for workflow, required_jobs in REQUIRED.items():
        for name in sorted(required_jobs):
            matches = [job for job in jobs if job.get("workflow") == workflow and job.get("job") == name]
            if len(matches) != 1:
                raise Refusal("readiness evidence has missing or duplicate jobs")
            job = matches[0]
            if (job.get("path") != f".github/workflows/{workflow}.yml"
                    or job.get("head_sha") != sha or job.get("status") != "completed"
                    or job.get("conclusion") != "success"):
                raise Refusal("readiness evidence job identity is invalid")
            identities.append({"workflow": workflow, "path": job["path"],
                               "run_id": positive_id(job["run_id"]),
                               "attempt": positive_id(job["attempt"]),
                               "job_id": positive_id(job["id"]), "job": name,
                               "head_sha": sha, "status": "completed", "conclusion": "success"})
    if len(jobs) != len(identities):
        raise Refusal("readiness evidence has extra jobs")

    if dist.is_symlink() or not dist.is_dir():
        raise Refusal("candidate payload directory unavailable")
    files = {}
    for path in dist.iterdir():
        if (not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._+-]{0,179}", path.name)
                or not stat.S_ISREG(path.lstat().st_mode)):
            raise Refusal("candidate payload contains an unsafe or non-regular file")
        if path.name.casefold() in {name.casefold() for name in files}:
            raise Refusal("candidate payload contains case-colliding names")
        files[path.name] = path.read_bytes()
    if "SHA256SUMS" not in files:
        raise Refusal("candidate payload is missing SHA256SUMS")
    sums = {}
    for line in files["SHA256SUMS"].decode("ascii").splitlines():
        match = re.fullmatch(r"([0-9a-f]{64})  ([A-Za-z0-9][A-Za-z0-9._+-]{0,179})", line)
        if not match or match.group(2) in sums:
            raise Refusal("candidate SHA256SUMS is malformed")
        sums[match.group(2)] = match.group(1)
    payload_names = set(files) - {"SHA256SUMS"}
    if set(sums) != payload_names or any(hashlib.sha256(files[name]).hexdigest() != sums[name]
                                          for name in payload_names):
        raise Refusal("candidate payload does not match SHA256SUMS")
    with zipfile.ZipFile(io.BytesIO(client("api", f"repos/{repo}/actions/artifacts/{artifact_id}/zip"))) as bundle:
        candidate_members = {}
        for info in bundle.infolist():
            if (not safe_relative(info.filename) or info.filename not in files
                    or info.filename in candidate_members or info.is_dir()
                    or stat.S_ISLNK(info.external_attr >> 16)):
                raise Refusal("candidate artifact contains an unsafe, extra or duplicate member")
            candidate_members[info.filename] = bundle.read(info)
    if candidate_members != files:
        raise Refusal("candidate artifact bytes differ from local payload")

    components = json.loads((root / "sdk/components.json").read_bytes())
    provenance = json.loads((root / "docs/legal/provenance.json").read_bytes())
    profiles, archives = components["profiles"], {}
    for name, data in files.items():
        if not name.endswith(".zip"):
            continue
        matched = []
        for profile, spec in profiles.items():
            expression = re.escape(spec["zip"])
            for token in ("{version}", "{game_pack_version}"):
                expression = expression.replace(re.escape(token), r"[0-9]+\.[0-9]+\.[0-9]+")
            if re.fullmatch(expression + r"\.zip", name):
                matched.append(profile)
        if len(matched) != 1:
            raise Refusal("candidate ZIP does not identify exactly one SDK profile")
        profile = matched[0]
        with zipfile.ZipFile(io.BytesIO(data)) as archive:
            actual, roots, seen = set(), set(), set()
            for info in archive.infolist():
                member = info.filename.rstrip("/")
                mode = info.external_attr >> 16
                if (not safe_relative(member) or stat.S_ISLNK(mode)
                        or mode and not info.is_dir() and not stat.S_ISREG(mode)
                        or member in seen):
                    raise Refusal("candidate SDK archive has an unsafe or duplicate member")
                seen.add(member)
                parts = member.split("/")
                roots.add(parts[0])
                if not info.is_dir():
                    if len(parts) < 2:
                        raise Refusal("candidate SDK archive member has no root directory")
                    actual.add("/".join(parts[1:]))
            if len(roots) != 1 or not actual:
                raise Refusal("candidate SDK archive has an invalid root or no files")
        archives[profile] = (name, actual)
    if set(archives) != set(profiles):
        raise Refusal("candidate SDK archive set does not match declared profiles")

    notes = []
    for profile, (archive_name, actual) in archives.items():
        included, pending = {profile}, [profile]
        while pending:
            current = pending.pop()
            for key in ("includes", "requires"):
                refs = profiles[current].get(key, [])
                for child in ([refs] if isinstance(refs, str) else refs):
                    if child not in included:
                        included.add(child)
                        pending.append(child)
        for component in components["components"]:
            if not included.intersection(component["profiles"]):
                continue
            source = component["source"]
            destination = source if profiles[profile].get("layout") == "source" else component["destination"]
            if destination not in actual:
                continue
            entry = next((item for item in provenance["entries"]
                          if (item["path"].endswith("/**") and source.startswith(item["path"][:-2])
                              or fnmatch.fnmatchcase(source, item["path"])
                              and ("/" in item["path"] or "/" not in source))), None)
            if entry and entry.get("status") == "needs-review":
                notes.append({"profile": profile, "archive": archive_name,
                              "path": source, "note": entry["note"]})
    payload = [{"name": name, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()}
               for name, data in sorted(files.items(), key=lambda item: item[0])]
    review = {"format_version": 1, "repository": repo, "source_sha": sha,
              "source_ref": "refs/heads/main", "tag": tag,
              "candidate_workflow": ".github/workflows/release-candidate.yml",
              "candidate_run_id": run_id, "candidate_run_attempt": attempt,
              "candidate_artifact_id": artifact_id,
              "readiness_artifact_id": readiness_artifact_id,
              "artifact_attempt_evidence": "Candidate run API verifies run_attempt; workflow outputs supply this attempt's artifact IDs because artifact metadata has no run_attempt field.",
              "readiness_jobs": identities, "payload": payload, "provenance_notes": notes}
    if output.resolve().is_relative_to(dist.resolve()) or output.exists():
        raise Refusal("review output must be a new directory outside dist")
    output.mkdir(parents=True)
    for name, data in files.items():
        (output / name).write_bytes(data)
    (output / "candidate-review.json").write_text(json.dumps(review, indent=2) + "\n", encoding="utf-8")
    checklist = (f"# Candidate review: {tag}\n\nSource: {sha}\n\n"
                 f"Candidate run: {run_id} attempt {attempt}; candidate artifact ID {artifact_id}.\n\n"
                 "This bundle is unsigned, subordinate to the immutable candidate artifact, and does not authorize release.\n\n"
                 "- [ ] Inspect source and scope against the change and changelog.\n"
                 "- [ ] Review shipped provenance notes and exact payload identities and hashes.\n"
                 "- [ ] Check evidence limits and release checklist Gates F, G, and H.\n"
                 "- [ ] Complete human authorization and signing separately.\n")
    (output / "CANDIDATE-REVIEW.md").write_text(checklist, encoding="utf-8")
    return review


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
        for path in sorted(dist.iterdir(), key=lambda item: item.name):
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            summary.write(f"| {path.name} | {path.stat().st_size} | `{digest}` |\n")


class Parser(argparse.ArgumentParser):
    def error(self, message):
        raise Refusal("invalid command arguments")


def main(argv=None, environ=None, client=github):
    """Thin workflow CLI; environment and GitHub client are injectable offline."""
    env = os.environ if environ is None else environ
    parser = Parser(description=__doc__)
    parser.add_argument("command", choices=("fetch", "prerequisites", "draft", "publish", "summary",
                                             "auto-candidate", "review-bundle"))
    parser.add_argument("--evidence-out")
    try:
        args = parser.parse_args(argv)
        command = args.command
        if command == "prerequisites":
            expected = env.get("EXPECTED_SOURCE_SHA", "")
            if expected and (not re.fullmatch(r"[0-9a-f]{40}", expected)
                             or expected != env["GITHUB_SHA"]
                             or env["GITHUB_REF"] != "refs/heads/main"):
                raise Refusal("expected source SHA does not match main checkout")
            require_prerequisites(env["GITHUB_REPOSITORY"], env["GITHUB_SHA"], env["GITHUB_REF"],
                                  client, args.evidence_out)
        elif command == "auto-candidate":
            event = json.loads(Path(env["GITHUB_EVENT_PATH"]).read_bytes())
            print(auto_candidate(env["GITHUB_REPOSITORY"], event, Path.cwd(), client))
        elif command == "review-bundle":
            review = review_bundle(env["GITHUB_REPOSITORY"], env["RELEASE_TAG"], env["GITHUB_SHA"],
                                   int(env["GITHUB_RUN_ID"]), int(env["GITHUB_RUN_ATTEMPT"]),
                                   int(env["ARTIFACT_ID"]), int(env["READINESS_ARTIFACT_ID"]),
                                   Path("dist"), Path(env["REVIEW_OUTPUT"]), Path.cwd(), client)
            print(f"candidate artifact {review['candidate_artifact_id']}: immutable release-candidate; "
                  f"readiness artifact {review['readiness_artifact_id']}: same-run readiness evidence; "
                  f"review output {env['REVIEW_OUTPUT']}: unsigned review copy")
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
