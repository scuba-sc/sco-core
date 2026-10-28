#!/usr/bin/env python3
"""Verify an unsigned inventory and its exact candidate payload."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import stat
import subprocess
import sys

sys.dont_write_bytecode = True
from ip_audit import AuditError, load_json_strict, reserved_problem, trailing_problem
from doc_release_gate import base_tag

SHA = re.compile(r"[0-9a-f]{40}\Z")
BASENAME = re.compile(r"[A-Za-z0-9][A-Za-z0-9._+-]{0,179}\Z")


class Refusal(Exception):
    """A check could not be established; never expose raw subprocess output."""


def safe_name(name):
    return bool(BASENAME.fullmatch(name)) and not (reserved_problem(name) or trailing_problem(name))


def safe_path(name):
    return (bool(name) and not name.startswith("/") and "\\" not in name and ":" not in name
            and all(part not in ("", ".", "..") for part in name.split("/")))


def git(root, *args):
    try:
        result = subprocess.run(["git", "-C", str(root), *args], capture_output=True, timeout=30)
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise Refusal("git results unavailable (" + args[0] + ")") from exc
    if result.returncode:
        raise Refusal("git results unavailable (" + args[0] + ")")
    return result.stdout


def commit(root, sha):
    if not SHA.fullmatch(sha) or git(root, "cat-file", "-t", sha).strip() != b"commit":
        raise Refusal("git commit unavailable")


def publication_source(root, built_sha, published_sha):
    """The tag must name exactly the candidate source commit."""
    commit(root, built_sha)
    commit(root, published_sha)
    if built_sha != published_sha:
        raise Refusal("tag source SHA differs from candidate source")


def dependencies(root, sha, prefix=""):
    """Enumerate gitlinks at the pinned commit, then recurse into pinned child objects."""
    found = {}
    for entry in git(root, "ls-tree", "-rz", sha).split(b"\0"):
        if not entry:
            continue
        header, raw_path = entry.split(b"\t", 1)
        mode, kind, raw_sha = header.split(b" ")
        if mode != b"160000":
            continue
        if kind != b"commit":
            raise Refusal("invalid git dependency")
        path = raw_path.decode("utf-8")
        dep_sha = raw_sha.decode("ascii")
        if not safe_path(path) or not SHA.fullmatch(dep_sha):
            raise Refusal("invalid git dependency")
        child = root / path
        if child.is_symlink() or not child.is_dir():
            raise Refusal("recursive dependency checkout unavailable")
        # Without this check, git -C on an empty submodule can silently discover its parent.
        top = Path(os.fsdecode(git(child, "rev-parse", "--show-toplevel")).strip()).resolve()
        if top != child.resolve():
            raise Refusal("recursive dependency checkout unavailable")
        commit(child, dep_sha)
        found[prefix + path] = dep_sha
        found.update(dependencies(child, dep_sha, prefix + path + "/"))
    return found


def payload_files(directory):
    """A payload has only safe, regular basenames: no nesting or symlink following."""
    if directory.is_symlink() or not directory.is_dir():
        raise Refusal("payload directory unavailable")
    files, folded = {}, set()
    for p in sorted(directory.iterdir()):
        if not safe_name(p.name):
            raise Refusal("unsafe payload name")
        if not stat.S_ISREG(p.lstat().st_mode):
            raise Refusal("payload entry is not a regular file")
        if p.name.casefold() in folded:
            raise Refusal("case-colliding payload names")
        folded.add(p.name.casefold())
        files[p.name] = p
    return files


def digest_file(path):
    """Hash with bounded memory and refuse symlinks even after directory discovery."""
    flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_NONBLOCK", 0)
    with os.fdopen(os.open(path, flags), "rb") as stream:
        if not stat.S_ISREG(os.fstat(stream.fileno()).st_mode):
            raise Refusal("payload entry is not a regular file")
        digest, size = hashlib.sha256(), 0
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
            size += len(block)
    return size, digest.hexdigest()


PROFILES = ["core", "game-sc", "legacy", "core-source", "host-source-sc"]
WORKFLOW = ".github/workflows/release-candidate.yml"


def expected_payload(root, sha, tag):
    version = git(root, "show", sha + ":sdk/VERSION").decode().strip()
    header = git(root, "show", sha + ":include/sc_game_pack.h").decode()
    match = re.search(r'^#define SCO_GAME_PACK_VERSION "([0-9.]+)"$', header, re.M)
    if not match or base_tag(tag) != "sdk-v" + version:
        raise Refusal("tag or game pack version mismatch")
    bases = [f"sco-sdk-core-{version}", f"sco-sdk-game-sc-{match.group(1)}",
             f"sco-sdk-{version}", f"sco-sdk-core-source-{version}",
             f"sco-sdk-host-source-sc-{version}"]
    return {**{base + ".zip": ("source" if base in bases[3:] else "zip") for base in bases},
            **{base + ".spdx.json": "sbom" for base in bases}, "SHA256SUMS": "checksums"}


def record_shape(record):
    """Reject malformed inventories without relying on an approval schema."""
    def fields(value, keys):
        return type(value) is dict and set(value) == set(keys)
    if not fields(record, ("schema_version", "tag", "source", "components_manifest_sha256", "builder", "profiles", "artifacts")) or type(record["schema_version"]) is not int or record["schema_version"] != 3:
        raise Refusal("invalid inventory fields")
    source, builder = record["source"], record["builder"]
    if not fields(source, ("repo", "sha", "dependencies")) or not isinstance(source["repo"], str) or not isinstance(source["sha"], str) or not SHA.fullmatch(source["sha"]) or type(source["dependencies"]) is not list:
        raise Refusal("invalid source fields")
    if not fields(builder, ("workflow_path", "workflow_sha", "run_id", "run_attempt")) or builder["workflow_path"] != WORKFLOW or builder["workflow_sha"] != source["sha"] or type(builder["run_id"]) is not int or builder["run_id"] <= 0 or type(builder["run_attempt"]) is not int or builder["run_attempt"] != 1:
        raise Refusal("invalid builder fields")
    if record["profiles"] != PROFILES or not isinstance(record["tag"], str) or base_tag(record["tag"]) is None or not isinstance(record["components_manifest_sha256"], str) or not re.fullmatch(r"[0-9a-f]{64}", record["components_manifest_sha256"]) or type(record["artifacts"]) is not list:
        raise Refusal("invalid inventory fields")
    for dep in source["dependencies"]:
        if not fields(dep, ("path", "sha")) or not isinstance(dep["path"], str) or not isinstance(dep["sha"], str) or not SHA.fullmatch(dep["sha"]):
            raise Refusal("invalid dependency fields")
    for item in record["artifacts"]:
        if not fields(item, ("name", "role", "size", "sha256", "artifact_id")) or not isinstance(item["name"], str) or not isinstance(item["role"], str) or type(item["size"]) is not int or item["size"] < 0 or not isinstance(item["sha256"], str) or not re.fullmatch(r"[0-9a-f]{64}", item["sha256"]) or type(item["artifact_id"]) is not int or item["artifact_id"] <= 0:
            raise Refusal("invalid artifact fields")


def verify(args):
    failures = []
    try:
        record = load_json_strict(args.manifest.read_bytes())
        record_shape(record)
    except (OSError, AuditError, Refusal) as exc:
        return [str(exc) if isinstance(exc, Refusal) or isinstance(exc, AuditError) and str(exc) in
                ("duplicate JSON key", "non-finite JSON number", "invalid JSON") else "inventory unavailable"]

    def check(action):
        try:
            action()
        except (Refusal, OSError, ValueError, UnicodeError):
            exc = sys.exc_info()[1]
            failures.append(str(exc) if isinstance(exc, Refusal) else "check unavailable")

    if record["tag"] != args.tag:
        failures.append("tag mismatch")
    if record["source"]["repo"] != args.repo:
        failures.append("repository mismatch")
    if not SHA.fullmatch(args.source_sha):
        failures.append("source-sha must be a full lowercase SHA")
    else:
        check(lambda: publication_source(Path.cwd(), record["source"]["sha"], args.source_sha))
    if record["builder"]["run_id"] != args.run_id:
        failures.append("builder run id mismatch")
    if any(a["artifact_id"] != args.artifact_id for a in record["artifacts"]):
        failures.append("artifact id mismatch")

    def source():
        root, sha = Path.cwd(), record["source"]["sha"]
        commit(root, sha)
        if hashlib.sha256(git(root, "show", sha + ":sdk/components.json")).hexdigest() != record["components_manifest_sha256"]:
            raise Refusal("components manifest SHA mismatch")
        builder = record["builder"]
        commit(root, builder["workflow_sha"])
        tree = git(root, "ls-tree", "-z", builder["workflow_sha"], "--", builder["workflow_path"])
        if not tree.startswith(b"100644 blob "):
            raise Refusal("builder workflow unavailable or not a regular file")
        # The promotion fetch binds the dispatch run's head SHA and workflow path
        # to this source. Require the inventory workflow blob to be that source's
        # regular file, while allowing a distinct commit with identical contents.
        source_tree = git(root, "ls-tree", "-z", sha, "--", builder["workflow_path"])
        if not source_tree.startswith(b"100644 blob ") or source_tree != tree:
            raise Refusal("builder workflow differs from source workflow")
        declared = {}
        for dep in record["source"]["dependencies"]:
            if not safe_path(dep["path"]) or dep["path"] in declared:
                raise Refusal("unsafe or duplicate dependency path")
            declared[dep["path"]] = dep["sha"]
        if dependencies(root, sha) != declared:
            raise Refusal("recursive source dependencies mismatch")

    check(source)

    def payload():
        listed, folded = {}, set()
        for artifact in record["artifacts"]:
            name = artifact["name"]
            if not safe_name(name):
                raise Refusal("unsafe artifact name")
            if name in listed:
                raise Refusal("duplicate artifact filename")
            if name.casefold() in folded:
                raise Refusal("case-colliding artifact names")
            folded.add(name.casefold())
            listed[name] = artifact
        expected = expected_payload(Path.cwd(), record["source"]["sha"], args.tag)
        if {name: item["role"] for name, item in listed.items()} != expected:
            raise Refusal("candidate payload does not match expected SDK profiles")
        files = payload_files(args.payload_dir)
        if files.keys() - listed.keys():
            failures.append("extra payload file")
        if listed.keys() - files.keys():
            failures.append("missing payload file")
        for name in sorted(listed.keys() & files.keys()):
            size, digest = digest_file(files[name])
            if size != listed[name]["size"]:
                failures.append("artifact size mismatch")
            if digest != listed[name]["sha256"]:
                failures.append("artifact SHA256 mismatch")
        if "SHA256SUMS" in files and listed.keys() <= files.keys():
            expected_sums = "".join(f"{listed[name]['sha256']}  {name}\n" for name in sorted(listed) if name != "SHA256SUMS")
            if files["SHA256SUMS"].read_bytes() != expected_sums.encode():
                failures.append("SHA256SUMS mismatch")

    check(payload)
    return failures


def inventory(args):
    files = payload_files(args.payload_dir)
    # --out is a path, not a basename to be silently relocated into the payload directory.
    if args.out.is_symlink():
        raise Refusal("inventory output must not be a symlink")
    lines = []
    for name, path in files.items():
        if path.resolve() == args.out.resolve():
            continue
        _size, digest = digest_file(path)
        lines.append(f"{digest}  {name}\n")
    args.out.write_text("".join(lines), encoding="utf-8", newline="\n")


class Parser(argparse.ArgumentParser):
    def error(self, message):
        raise Refusal("invalid command arguments")


def main(argv=None):
    parser = Parser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True, parser_class=Parser)
    v = sub.add_parser("verify")
    for option in ("manifest", "payload-dir"):
        v.add_argument("--" + option, type=Path, required=True)
    v.add_argument("--tag", required=True)
    v.add_argument("--source-sha", required=True)
    v.add_argument("--repo", required=True)
    v.add_argument("--run-id", type=int, required=True)
    v.add_argument("--artifact-id", type=int, required=True)
    i = sub.add_parser("inventory")
    i.add_argument("--payload-dir", type=Path, required=True)
    i.add_argument("--out", type=Path, required=True)
    try:
        args = parser.parse_args(argv)
        if args.command == "verify":
            failures = verify(args)
        else:
            inventory(args)
            failures = []
    except (Refusal, OSError, AuditError, ValueError, OverflowError):
        exc = sys.exc_info()[1]
        failures = [str(exc) if isinstance(exc, Refusal) else "verification input unavailable"]
    for failure in failures:
        print("refused: " + failure, file=sys.stderr)
    return int(bool(failures))


if __name__ == "__main__":
    sys.exit(main())
