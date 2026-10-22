#!/usr/bin/env python3
"""Check that the published documentation site is fresh: built from main and for the right SDK.

Fetches the live sdk-catalogue.json from GitHub Pages and refuses unless
  - its source_sha is the commit at --ref (default origin/main) or an ancestor of it at most
    --max-behind commits back, and
  - its sdk_version equals sdk/VERSION as recorded at that source_sha (read with git show).

Read-only: it fetches one public file and reads local git history; it never deploys or writes.
The clone needs history back to the published commit (actions/checkout fetch-depth: 0).

Exit 0 when the site is fresh. Otherwise exit 1 with one `refused: ` line per failure on stderr.
"""
import argparse
import json
import re
import subprocess
import sys
import urllib.error
import urllib.request

sys.dont_write_bytecode = True

CATALOGUE_URL = "https://scuba-sc.github.io/sco-core/sdk-catalogue.json"
DEFAULT_REF = "origin/main"
DEFAULT_MAX_BEHIND = 25
MAX_BYTES = 8 * 1024 * 1024
TIMEOUT_SECONDS = 30
SHA = re.compile(r"[0-9a-f]{40}")
SEMVER = re.compile(r"\d+\.\d+\.\d+")


class Refusal(Exception):
    """One failed check, reported as one `refused: ` line."""


def fetch_url(url):
    request = urllib.request.Request(url, headers={"User-Agent": "sco-core-live-site-check",
                                                   "Cache-Control": "no-cache"})
    try:
        with urllib.request.urlopen(request, timeout=TIMEOUT_SECONDS) as response:
            data = response.read(MAX_BYTES + 1)
    except (urllib.error.URLError, OSError, ValueError) as exc:
        raise Refusal(f"could not fetch {url}: {exc}") from exc
    if len(data) > MAX_BYTES:
        raise Refusal(f"{url} is larger than {MAX_BYTES} bytes")
    return data


def run_git(repo, *args):
    result = subprocess.run(["git", "-C", str(repo), *args], capture_output=True, text=True)
    return result.returncode, result.stdout


def parse_catalogue(data, url):
    try:
        catalogue = json.loads(data)
    except (ValueError, UnicodeError, RecursionError) as exc:
        raise Refusal(f"{url} is not valid JSON") from exc
    if not isinstance(catalogue, dict):
        raise Refusal(f"{url} is not a JSON object")
    source_sha, sdk_version = catalogue.get("source_sha"), catalogue.get("sdk_version")
    if not isinstance(source_sha, str) or not SHA.fullmatch(source_sha):
        raise Refusal(f"{url} source_sha is missing or not a full lowercase commit SHA")
    if not isinstance(sdk_version, str) or not SEMVER.fullmatch(sdk_version):
        raise Refusal(f"{url} sdk_version is missing or not <major>.<minor>.<patch>")
    return source_sha, sdk_version


def check(repo, ref=DEFAULT_REF, max_behind=DEFAULT_MAX_BEHIND, url=CATALOGUE_URL,
          fetch=fetch_url, git=run_git):
    try:
        source_sha, sdk_version = parse_catalogue(fetch(url), url)
    except Refusal as exc:
        return [str(exc)], []

    code, head = git(repo, "rev-parse", "--verify", "--quiet", f"{ref}^{{commit}}")
    if code != 0:
        return [f"--ref {ref!r} does not name a commit in this clone"], []
    head = head.strip()
    if git(repo, "cat-file", "-e", f"{source_sha}^{{commit}}")[0] != 0:
        return [f"live source_sha {source_sha} is not a commit in this clone "
                f"(not on {ref}, or the clone is shallow)"], []
    if git(repo, "merge-base", "--is-ancestor", source_sha, head)[0] != 0:
        return [f"live source_sha {source_sha} is not {ref} or an ancestor of it"], []
    code, count = git(repo, "rev-list", "--count", f"{source_sha}..{head}")
    if code != 0 or not count.strip().isdigit():
        return [f"could not count commits from {source_sha} to {ref}"], []
    behind = int(count)

    refusals = []
    if behind > max_behind:
        refusals.append(f"live source_sha {source_sha} is {behind} commits behind {ref} "
                        f"({head}); the limit is {max_behind}")
    code, version = git(repo, "show", f"{source_sha}:sdk/VERSION")
    if code != 0:
        refusals.append(f"sdk/VERSION does not exist at {source_sha}")
    elif version.strip() != sdk_version:
        refusals.append(f"live sdk_version {sdk_version} != sdk/VERSION {version.strip()!r} "
                        f"at {source_sha}")
    notes = [f"live source_sha {source_sha} is {behind} commits behind {ref} ({head}), "
             f"limit {max_behind}; sdk_version {sdk_version}"]
    return refusals, notes


class Parser(argparse.ArgumentParser):
    def error(self, message):
        raise Refusal("invalid command arguments: " + message)


def main(argv=None, fetch=fetch_url, git=run_git):
    parser = Parser(description="Refuse when the published documentation site is not built "
                                "from a recent main commit for the matching SDK version.")
    parser.add_argument("--repo", default=".", help="Git checkout to read history from (default: .)")
    parser.add_argument("--ref", default=DEFAULT_REF, help=f"Branch head to compare with "
                                                           f"(default: {DEFAULT_REF})")
    parser.add_argument("--max-behind", type=int, default=DEFAULT_MAX_BEHIND,
                        help=f"Largest allowed commit distance from --ref (default: {DEFAULT_MAX_BEHIND})")
    parser.add_argument("--url", default=CATALOGUE_URL, help="https URL of the live catalogue")
    try:
        args = parser.parse_args(argv)
        if args.max_behind < 0:
            raise Refusal("--max-behind must be 0 or more")
        if not args.url.startswith("https://"):
            raise Refusal("--url must be an https URL")
    except Refusal as exc:
        print("refused: " + str(exc), file=sys.stderr)
        return 1
    refusals, notes = check(args.repo, args.ref, args.max_behind, args.url, fetch, git)
    for line in notes:
        print(line)
    for line in refusals:
        print("refused: " + line, file=sys.stderr)
    if refusals:
        return 1
    print(f"live site is fresh: {args.url}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
