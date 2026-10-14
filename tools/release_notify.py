#!/usr/bin/env python3
"""Verify that a published SDK release is complete, then announce it exactly once.

Approval, publication and completion are distinct. Completion requires:
- a unique published GitHub Release (not draft, not prerelease) for the tag;
- the tag peeling, remotely and locally, to the approval commit that carries the
  signed docs/legal/release-decisions/<tag>.json, whose source.sha is a distinct
  identity that must be an ancestor of that commit;
- every release asset matching the signed manifest by name, size and SHA-256, and
  release.json / release.json.sig equal byte for byte to the committed approval;
- the deployed Pages catalogue reporting this SDK and game-pack version from a
  source commit that contains the release source (and --docs-sha, when given),
  plus the allowlisted routes under the Pages base answering 200.

Delivery is serialized per tag by workflow concurrency and recorded in workflow
artifacts: an attempt marker is persisted before the POST and a receipt after it.
A valid delivered receipt turns later runs into a no-op. An attempt without a
definitive outcome halts every later run, because a timeout after sending is not
permission to resend; a maintainer confirms the channel by hand first.

Exit codes: 0 verified, delivered or already delivered; 1 refused or definitively
not delivered; 3 ambiguous delivery state (halt, never retried automatically).
The webhook URL is read only from DISCORD_WEBHOOK_URL and is never printed or stored.
"""
import argparse
import datetime
import hashlib
import http.client
import io
import json
import os
from pathlib import Path
import re
import socket
import subprocess
import sys
import urllib.error
import urllib.parse
import urllib.request
import zipfile

sys.dont_write_bytecode = True

TAG = re.compile(r"sdk-v(\d+\.\d+\.\d+)\Z")
SHA = re.compile(r"[0-9a-f]{40}\Z")
REPO = re.compile(r"[A-Za-z0-9][A-Za-z0-9-]*/[A-Za-z0-9._-]+\Z")
GAME_PACK = re.compile(r"sco-sdk-game-sc-(\d+\.\d+\.\d+)\.zip\Z")
ROUTE_PART = re.compile(r"[a-z0-9][a-z0-9._-]*\Z")
MESSAGE_ID = re.compile(r"[0-9]{1,25}\Z")
TIMESTAMP = re.compile(r"\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ\Z")
WEBHOOK_PATH = re.compile(r"/api/webhooks/[0-9]{1,25}/[A-Za-z0-9_-]{1,200}\Z")
WEBHOOK_HOSTS = {"discord.com", "discordapp.com", "ptb.discord.com", "canary.discord.com"}
ATTEMPT_SCHEMA = "sco-release-notify-attempt/1"
RECEIPT_SCHEMA = "sco-release-notify-receipt/1"
# Only these workflow runs can hold the webhook secret; artifacts from any other run
# (for example a fork pull request) can neither suppress nor block an announcement.
TRUSTED_RUNS = {(".github/workflows/release.yml", "push"),
                (".github/workflows/discord.yml", "workflow_dispatch")}
MAX_BODY = 8 * 1024 * 1024
MAX_RECORD = 1024 * 1024
TIMEOUT = 20
USER_AGENT = "sco-core-release-notify (+https://github.com/scubamount/sco-core)"


class Refusal(Exception):
    """Completion could not be established, or nothing was delivered. Exit 1."""


class Ambiguous(Exception):
    """A message may or may not have been delivered. Halt with exit 3."""


class NotSent(Exception):
    """The connection failed before any request bytes could be delivered."""


def github(*args):
    try:
        return subprocess.check_output(["gh", *args], stderr=subprocess.PIPE, timeout=300)
    except (OSError, subprocess.SubprocessError) as exc:
        raise Refusal("GitHub request unavailable") from exc


def git(*args):
    try:
        result = subprocess.run(["git", *args], capture_output=True, timeout=60)
    except (OSError, subprocess.SubprocessError) as exc:
        raise Refusal("git unavailable") from exc
    return result.returncode, result.stdout


class _NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        return None  # A redirect surfaces as an HTTPError status; nothing is followed.


class Web:
    def __init__(self):
        self.opener = urllib.request.build_opener(_NoRedirect)

    def get(self, url):
        request = urllib.request.Request(url, headers={"Cache-Control": "no-cache",
                                                       "User-Agent": USER_AGENT})
        try:
            with self.opener.open(request, timeout=TIMEOUT) as response:
                return response.status, response.read(MAX_BODY + 1)
        except urllib.error.HTTPError as exc:
            exc.close()
            return exc.code, b""
        except (urllib.error.URLError, OSError, http.client.HTTPException, ValueError) as exc:
            raise Refusal("Pages unavailable") from exc

    def post(self, url, body):
        request = urllib.request.Request(url, data=body, method="POST", headers={
            "Content-Type": "application/json", "User-Agent": USER_AGENT})
        try:
            with self.opener.open(request, timeout=TIMEOUT) as response:
                return response.status, response.read(65536)
        except urllib.error.HTTPError as exc:
            try:
                data = exc.read(65536)
            except (OSError, http.client.HTTPException):
                data = b""
            exc.close()
            return exc.code, data
        except urllib.error.URLError as exc:
            # Name resolution and refused connections happen before any byte is sent.
            if isinstance(exc.reason, (socket.gaierror, ConnectionRefusedError)):
                raise NotSent() from None
            raise Ambiguous("webhook connection failed after it may have sent") from None
        except (OSError, http.client.HTTPException):
            raise Ambiguous("webhook connection failed after it may have sent") from None


def api(client, path):
    return json.loads(client("api", path))


def api_pages(client, path):
    pages = json.loads(client("api", "--paginate", "--slurp", path))
    if not isinstance(pages, list):
        raise Refusal("unexpected GitHub response")
    return pages


def positive_int(value):
    return type(value) is int and value > 0


def blob(git_run, sha, path):
    code, out = git_run("cat-file", "blob", f"{sha}:{path}")
    if code:
        raise Refusal("committed file unavailable at the approval commit: " + path)
    return out


def is_ancestor(git_run, ancestor, descendant):
    code, _ = git_run("merge-base", "--is-ancestor", ancestor, descendant)
    if code not in (0, 1):
        raise Refusal("git ancestry unavailable")
    return code == 0


def has_commit(git_run, sha):
    code, out = git_run("cat-file", "-t", sha)
    return code == 0 and out.strip() == b"commit"


def peeled_tag(repo, tag, client):
    ref = api(client, f"repos/{repo}/git/ref/tags/{tag}")
    if not isinstance(ref, dict) or ref.get("ref") != f"refs/tags/{tag}":
        raise Refusal("remote tag ref unavailable")
    target = ref.get("object") or {}
    for _ in range(4):
        if target.get("type") == "commit":
            sha = target.get("sha")
            if not isinstance(sha, str) or not SHA.fullmatch(sha):
                raise Refusal("remote tag does not peel to a commit")
            return sha
        if target.get("type") != "tag" or not isinstance(target.get("sha"), str) \
                or not SHA.fullmatch(target["sha"]):
            raise Refusal("remote tag does not peel to a commit")
        target = api(client, f"repos/{repo}/git/tags/{target['sha']}").get("object") or {}
    raise Refusal("remote tag does not peel to a commit")


def load_manifest(data, tag):
    try:
        record = json.loads(data)
    except ValueError as exc:
        raise Refusal("signed manifest is not valid JSON") from exc
    if (not isinstance(record, dict) or record.get("schema_version") != 2
            or record.get("tag") != tag
            or (record.get("distribution") or {}).get("status") != "approved-for-publication"):
        raise Refusal("signed manifest does not approve this tag for publication")
    source = (record.get("source") or {}).get("sha")
    if not isinstance(source, str) or not SHA.fullmatch(source):
        raise Refusal("signed manifest source SHA is invalid")
    artifacts = record.get("artifacts")
    if not isinstance(artifacts, list) or not artifacts:
        raise Refusal("signed manifest has no artifacts")
    names = set()
    for artifact in artifacts:
        name = artifact.get("name") if isinstance(artifact, dict) else None
        if (not isinstance(name, str) or name in names or name in ("release.json", "release.json.sig")
                or not positive_int(artifact.get("size"))
                or not isinstance(artifact.get("sha256"), str)
                or not re.fullmatch(r"[0-9a-f]{64}", artifact["sha256"])):
            raise Refusal("signed manifest artifact inventory is invalid")
        names.add(name)
    return record


def verify_assets(repo, release_id, manifest, manifest_bytes, sig_bytes, client):
    listed = {artifact["name"]: artifact for artifact in manifest["artifacts"]}
    approval = {"release.json": manifest_bytes, "release.json.sig": sig_bytes}
    assets = [asset for page in api_pages(client, f"repos/{repo}/releases/{release_id}/assets?per_page=100")
              for asset in page]
    names = [asset.get("name") for asset in assets]
    if len(names) != len(set(names)) or set(names) != set(listed) | set(approval):
        raise Refusal("release assets do not exactly match the signed manifest")
    for asset in sorted(assets, key=lambda item: item["name"]):
        name = asset["name"]
        if asset.get("state") != "uploaded" or not positive_int(asset.get("id")):
            raise Refusal("release asset is incomplete: " + name)
        data = client("api", f"repos/{repo}/releases/assets/{asset['id']}",
                      "-H", "Accept: application/octet-stream")
        digest = hashlib.sha256(data).hexdigest()
        if name in approval:
            if data != approval[name]:
                raise Refusal("published approval differs from the signed commit: " + name)
        elif len(data) != listed[name]["size"] or digest != listed[name]["sha256"]:
            raise Refusal("release asset differs from the signed manifest: " + name)
        if asset.get("size") != len(data) or asset.get("digest") not in (None, "sha256:" + digest):
            raise Refusal("release asset metadata differs from its bytes: " + name)
    return len(assets)


def pages_location(repo):
    owner, name = repo.split("/", 1)
    return f"https://{owner.lower()}.github.io", "/" + name


def notes_route(base, tag):
    # Same slug rule as site/scripts/gen.py guide_id(): Starlight drops '.' and other punctuation.
    return f"{base}/guides/releases/{re.sub(r'[^a-z0-9_-]', '', tag.lower())}/"


def default_routes(base, tag):
    return [base + "/", notes_route(base, tag)]


def safe_route(route, base):
    if not route.startswith(base + "/") or not route.endswith("/"):
        return False
    parts = route[len(base) + 1:-1]
    return parts == "" or all(ROUTE_PART.fullmatch(part) for part in parts.split("/"))


def verify_pages(repo, tag, version, game_pack, source_sha, docs_sha, routes, git_run, web):
    origin, base = pages_location(repo)
    status, body = web.get(origin + base + "/sdk-catalogue.json")
    if status != 200 or len(body) > MAX_BODY:
        raise Refusal("deployed Pages catalogue unavailable")
    try:
        catalogue = json.loads(body)
    except ValueError as exc:
        raise Refusal("deployed Pages catalogue is not valid JSON") from exc
    if not isinstance(catalogue, dict):
        raise Refusal("deployed Pages catalogue is not valid JSON")
    if catalogue.get("sdk_version") != version:
        raise Refusal("deployed Pages catalogue reports a different SDK version (stale Pages)")
    if catalogue.get("game_pack_version") != game_pack:
        raise Refusal("deployed Pages catalogue reports a different game pack version (stale Pages)")
    pages_sha = catalogue.get("source_sha")
    if not isinstance(pages_sha, str) or not SHA.fullmatch(pages_sha) or not has_commit(git_run, pages_sha):
        raise Refusal("deployed Pages source commit is unknown to this checkout")
    if not is_ancestor(git_run, source_sha, pages_sha):
        raise Refusal("deployed Pages predates the release source (stale Pages)")
    if docs_sha is not None:
        if not has_commit(git_run, docs_sha):
            raise Refusal("required docs commit is unknown to this checkout")
        if not is_ancestor(git_run, docs_sha, pages_sha):
            raise Refusal("deployed Pages predates the required docs commit (stale Pages)")
    notes = notes_route(base, tag)
    for route in dict.fromkeys(default_routes(base, tag) + list(routes)):
        if not safe_route(route, base):
            raise Refusal("route is not an allowlisted Pages route under " + base + "/")
        status, page = web.get(origin + route)
        if status != 200 or not page or len(page) > MAX_BODY:
            raise Refusal("deployed Pages route unavailable: " + route)
        if route == notes and tag.encode() not in page:
            raise Refusal("deployed release notes do not name the tag: " + route)
    return pages_sha, origin + base + "/", origin + notes


def verify_completion(repo, tag, docs_sha=None, routes=(), client=github, git_run=git, web=None):
    match = TAG.fullmatch(tag or "")
    if not match:
        raise Refusal("release tag must look like sdk-vX.Y.Z")
    if not REPO.fullmatch(repo or ""):
        raise Refusal("invalid repository")
    if docs_sha is not None and not SHA.fullmatch(docs_sha):
        raise Refusal("docs SHA must be a full lowercase SHA")
    version = match.group(1)
    web = web or Web()

    release = api(client, f"repos/{repo}/releases/tags/{tag}")
    if not isinstance(release, dict) or not positive_int(release.get("id")):
        raise Refusal("published release has no numeric ID")
    if release.get("draft") is not False:
        raise Refusal("release is still a draft")
    if release.get("prerelease") is not False:
        raise Refusal("release is a prerelease")
    if release.get("tag_name") != tag:
        raise Refusal("release tag does not match")
    published_at = release.get("published_at")
    if not isinstance(published_at, str) or not TIMESTAMP.fullmatch(published_at):
        raise Refusal("release has no publication time")
    release_url = f"https://github.com/{repo}/releases/tag/{tag}"
    if release.get("html_url") != release_url:
        raise Refusal("release URL does not match the tag")
    same_tag = [item for page in api_pages(client, f"repos/{repo}/releases?per_page=100")
                for item in page if isinstance(item, dict) and item.get("tag_name") == tag]
    if len(same_tag) != 1 or same_tag[0].get("id") != release["id"]:
        raise Refusal("release for this tag is not unique")

    approval_sha = peeled_tag(repo, tag, client)
    code, local = git_run("rev-parse", "--verify", "--quiet", f"refs/tags/{tag}^{{commit}}")
    if code or local.decode("ascii", "replace").strip() != approval_sha:
        raise Refusal("local tag does not peel to the published approval commit")
    manifest_bytes = blob(git_run, approval_sha, f"docs/legal/release-decisions/{tag}.json")
    sig_bytes = blob(git_run, approval_sha, f"docs/legal/release-decisions/{tag}.json.sig")
    manifest = load_manifest(manifest_bytes, tag)
    source_sha = manifest["source"]["sha"]
    if not is_ancestor(git_run, source_sha, approval_sha):
        raise Refusal("manifest source is not an ancestor of the approval commit")
    if blob(git_run, approval_sha, "sdk/VERSION").decode("utf-8", "replace").strip() != version:
        raise Refusal("sdk/VERSION at the approval commit does not match the tag")
    packs = {m.group(1) for m in (GAME_PACK.fullmatch(a["name"]) for a in manifest["artifacts"]) if m}
    if len(packs) != 1:
        raise Refusal("signed manifest does not name exactly one game pack version")
    game_pack = packs.pop()
    assets = verify_assets(repo, release["id"], manifest, manifest_bytes, sig_bytes, client)

    pages_sha, pages_url, notes_url = verify_pages(repo, tag, version, game_pack, source_sha,
                                                   docs_sha, routes, git_run, web)
    return {"repo": repo, "tag": tag, "sdk_version": version, "game_pack_version": game_pack,
            "release_id": release["id"], "release_url": release_url, "published_at": published_at,
            "approval_sha": approval_sha, "source_sha": source_sha, "docs_sha": pages_sha,
            "pages_url": pages_url, "notes_url": notes_url, "asset_count": assets}


def changelog_section(text, tag):
    lines, on = [], False
    for line in text.splitlines():
        if line.startswith("## "):
            if on:
                break
            on = line == "## " + tag or line.startswith("## " + tag + " ")
        elif on:
            lines.append(line)
    return "\n".join(lines).strip()


def build_payload(completion, notes):
    repo, tag = completion["repo"], completion["tag"]
    description = notes or "See the release page."
    if len(description) > 3500:
        description = description[:3500] + "\n…"

    def commit(sha):
        return f"[`{sha[:7]}`](https://github.com/{repo}/commit/{sha})"

    embed = {
        "title": f"sco-core {tag}"[:256], "url": completion["release_url"], "color": 3066993,
        "timestamp": completion["published_at"], "description": description,
        "fields": [
            {"name": "SDK", "value": completion["sdk_version"], "inline": True},
            {"name": "Game pack", "value": completion["game_pack_version"], "inline": True},
            {"name": "Approval commit", "value": commit(completion["approval_sha"]), "inline": True},
            {"name": "Source commit", "value": commit(completion["source_sha"]), "inline": True},
            {"name": "Docs", "value": completion["notes_url"], "inline": False}],
        "footer": {"text": "sco-core · game core and plugin SDK for sc-offline"}}
    return {"username": "sco-core", "allowed_mentions": {"parse": []}, "embeds": [embed]}


def webhook_url(value):
    """Validate the secret's shape and add wait=true. Errors never include the value."""
    if not isinstance(value, str) or not value.strip():
        raise Refusal("DISCORD_WEBHOOK_URL is not configured; nothing was sent")
    try:
        parts = urllib.parse.urlsplit(value.strip())
        port = parts.port
    except ValueError:
        raise Refusal("DISCORD_WEBHOOK_URL is not a Discord webhook URL; nothing was sent") from None
    if (parts.scheme != "https" or parts.hostname not in WEBHOOK_HOSTS or port is not None
            or parts.username is not None or parts.fragment or not WEBHOOK_PATH.fullmatch(parts.path)):
        raise Refusal("DISCORD_WEBHOOK_URL is not a Discord webhook URL; nothing was sent")
    query = [(k, v) for k, v in urllib.parse.parse_qsl(parts.query, keep_blank_values=True) if k != "wait"]
    query.append(("wait", "true"))
    return urllib.parse.urlunsplit(("https", parts.netloc, parts.path, urllib.parse.urlencode(query), ""))


def artifact_name(kind, tag):
    return f"release-notify-{kind}-{tag}"


def read_record(repo, artifact_id, member, client):
    try:
        with zipfile.ZipFile(io.BytesIO(client("api", f"repos/{repo}/actions/artifacts/{artifact_id}/zip"))) as bundle:
            infos = bundle.infolist()
            if len(infos) != 1 or infos[0].filename != member or infos[0].file_size > MAX_RECORD:
                return None
            record = json.loads(bundle.read(infos[0]))
    except (zipfile.BadZipFile, ValueError, OSError):
        return None
    return record if isinstance(record, dict) else None


def prior_delivery(repo, completion, client):
    tag, trusted = completion["tag"], {}

    def trusted_run(run_id):
        if run_id not in trusted:
            run = api(client, f"repos/{repo}/actions/runs/{run_id}")
            key = (run.get("path"), run.get("event"))
            ok = (key in TRUSTED_RUNS and (run.get("head_repository") or {}).get("full_name") == repo
                  and (key[1] != "workflow_dispatch" or run.get("head_branch") == "main"))
            trusted[run_id] = ok
        return trusted[run_id]

    def artifacts(kind):
        name = artifact_name(kind, tag)
        found = []
        for page in api_pages(client, f"repos/{repo}/actions/artifacts?name={name}&per_page=100"):
            for artifact in (page.get("artifacts") or []) if isinstance(page, dict) else []:
                run_id = (artifact.get("workflow_run") or {}).get("id")
                if artifact.get("name") != name or not positive_int(run_id) or not positive_int(artifact.get("id")):
                    continue
                if not trusted_run(run_id):
                    print(f"ignored {kind} artifact {artifact['id']} from an untrusted run", flush=True)
                    continue
                found.append((run_id, artifact))
        return found

    receipts = {}
    for run_id, artifact in artifacts("receipt"):
        record = None if artifact.get("expired") else read_record(repo, artifact["id"], "receipt.json", client)
        receipts.setdefault(run_id, []).append(record)
    identity = ("tag", "release_id", "approval_sha", "source_sha")
    for run_id, records in sorted(receipts.items()):
        for record in records:
            if (record and record.get("schema") == RECEIPT_SCHEMA and record.get("outcome") == "delivered"
                    and record.get("run_id") == run_id
                    and all(record.get(key) == completion[key] for key in identity)
                    and isinstance(record.get("message_id"), str) and MESSAGE_ID.fullmatch(record["message_id"])
                    and type(record.get("http_status")) is int and 200 <= record["http_status"] < 300):
                return "delivered", record
    for run_id, artifact in artifacts("attempt"):
        resolved = any(record and record.get("schema") == RECEIPT_SCHEMA and record.get("run_id") == run_id
                       and record.get("tag") == tag and record.get("outcome") == "not-delivered"
                       for record in receipts.get(run_id, []))
        if not resolved:
            raise Ambiguous(f"workflow run {run_id} recorded a delivery attempt for {tag} without a "
                            "definitive outcome; confirm the channel by hand before any replay")
    return "none", None


def utc_now():
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def run_identity(env):
    run_id, attempt = env.get("GITHUB_RUN_ID", ""), env.get("GITHUB_RUN_ATTEMPT", "")
    if not (run_id.isdigit() and attempt.isdigit() and int(run_id) > 0 and int(attempt) > 0):
        raise Refusal("workflow run identity unavailable")
    return int(run_id), int(attempt)


def write_outputs(env, values):
    path = env.get("GITHUB_OUTPUT")
    if path:
        with open(path, "a", encoding="utf-8") as output:
            for key, value in values.items():
                output.write(f"{key}={value}\n")


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


IDENTITY = ("tag", "release_id", "approval_sha", "source_sha", "docs_sha")


def prepare(repo, tag, work, env, docs_sha=None, routes=(), client=github, git_run=git, web=None, now=utc_now):
    webhook_url(env.get("DISCORD_WEBHOOK_URL"))  # Refuse before any marker: nothing to resolve later.
    run_id, run_attempt = run_identity(env)
    completion = verify_completion(repo, tag, docs_sha, routes, client, git_run, web)
    _state, receipt = prior_delivery(repo, completion, client)
    work.mkdir(parents=True, exist_ok=True)
    write_json(work / "completion.json", completion)
    if receipt is not None:
        write_outputs(env, {"action": "skip"})
        print(f"already delivered for {tag}: message {receipt['message_id']} "
              f"(workflow run {receipt['run_id']}); nothing sent")
        return 0
    notes = changelog_section(blob(git_run, completion["approval_sha"], "CHANGELOG.md")
                              .decode("utf-8", "replace"), tag)
    payload = json.dumps(build_payload(completion, notes), ensure_ascii=False, sort_keys=True).encode("utf-8")
    (work / "payload.json").write_bytes(payload)
    attempt = {key: completion[key] for key in IDENTITY}
    attempt.update({"schema": ATTEMPT_SCHEMA, "run_id": run_id, "run_attempt": run_attempt,
                    "payload_sha256": hashlib.sha256(payload).hexdigest(), "created_at": now()})
    write_json(work / "attempt.json", attempt)
    write_outputs(env, {"action": "send", "attempt-artifact": artifact_name("attempt", tag),
                        "receipt-artifact": artifact_name("receipt", tag)})
    print(f"completion verified for {tag}: release {completion['release_id']}, approval "
          f"{completion['approval_sha']}, source {completion['source_sha']}, docs {completion['docs_sha']}")
    return 0


def send(work, env, web=None, now=utc_now):
    attempt = json.loads((work / "attempt.json").read_bytes())
    if attempt.get("schema") != ATTEMPT_SCHEMA:
        raise Refusal("attempt marker is invalid")
    run_id, run_attempt = run_identity(env)
    if (attempt.get("run_id"), attempt.get("run_attempt")) != (run_id, run_attempt):
        raise Refusal("attempt marker belongs to another workflow run")
    web = web or Web()

    def record(outcome, status=None, message_id=None):
        receipt = {key: attempt[key] for key in IDENTITY}
        receipt.update({"schema": RECEIPT_SCHEMA, "outcome": outcome, "run_id": run_id,
                        "run_attempt": run_attempt, "http_status": status, "message_id": message_id,
                        "recorded_at": now()})
        write_json(work / "receipt.json", receipt)

    try:
        payload = (work / "payload.json").read_bytes()
        if hashlib.sha256(payload).hexdigest() != attempt.get("payload_sha256"):
            raise Refusal("staged payload changed after the attempt marker; nothing was sent")
        url = webhook_url(env.get("DISCORD_WEBHOOK_URL"))
    except (Refusal, OSError) as exc:
        record("not-delivered")
        raise exc if isinstance(exc, Refusal) else Refusal("staged payload unavailable; nothing was sent")
    try:
        status, body = web.post(url, payload)
    except NotSent:
        record("not-delivered")
        raise Refusal("webhook unreachable before sending; nothing was delivered") from None
    except Exception:  # Timeout, reset, or anything unclassified after the request may have left.
        record("ambiguous")
        raise Ambiguous("delivery outcome unknown (timeout or connection failure after sending); "
                        "confirm the channel by hand before any replay") from None
    if 200 <= status < 300:
        try:
            message_id = json.loads(body).get("id")
        except (ValueError, AttributeError):
            message_id = None
        if not isinstance(message_id, str) or not MESSAGE_ID.fullmatch(message_id):
            record("ambiguous", status)
            raise Ambiguous(f"webhook accepted the message (HTTP {status}) without a message ID; "
                            "confirm the channel by hand before any replay")
        record("delivered", status, message_id)
        print(f"delivered {attempt['tag']}: message {message_id} (HTTP {status}, workflow run {run_id})")
        return 0
    if status >= 500:
        record("ambiguous", status)
        raise Ambiguous(f"webhook answered HTTP {status}; the message may exist, "
                        "confirm the channel by hand before any replay")
    record("not-delivered", status)
    raise Refusal(f"webhook refused the message (HTTP {status}"
                  + (", rate limited" if status == 429 else "") + "); nothing was delivered")


class Parser(argparse.ArgumentParser):
    def error(self, message):
        raise Refusal("invalid command arguments")


def main(argv=None, environ=None, client=github, git_run=git, web=None, now=utc_now):
    env = os.environ if environ is None else environ
    parser = Parser(description="Verify SDK release completion and deliver one receipted announcement.")
    sub = parser.add_subparsers(dest="command", required=True, parser_class=Parser)
    for name in ("verify", "prepare"):
        command = sub.add_parser(name)
        command.add_argument("--tag", required=True)
        command.add_argument("--docs-sha", default="")
        command.add_argument("--route", action="append", default=[])
        if name == "prepare":
            command.add_argument("--work", type=Path, required=True)
    sub.add_parser("send").add_argument("--work", type=Path, required=True)
    try:
        args = parser.parse_args(argv)
        if args.command == "send":
            return send(args.work, env, web, now)
        repo, docs_sha = env.get("GITHUB_REPOSITORY", ""), args.docs_sha or None
        if args.command == "verify":
            print(json.dumps(verify_completion(repo, args.tag, docs_sha, args.route, client, git_run, web),
                             indent=2, sort_keys=True))
            return 0
        return prepare(repo, args.tag, args.work, env, docs_sha, args.route, client, git_run, web, now)
    except Ambiguous as exc:
        print("halted: " + str(exc), file=sys.stderr)
        return 3
    except (Refusal, OSError, ValueError, KeyError, TypeError, AttributeError, UnicodeError):
        exc = sys.exc_info()[1]
        print("refused: " + (str(exc) if isinstance(exc, Refusal) else "completion input unavailable"),
              file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
