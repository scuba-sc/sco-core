#!/usr/bin/env python3
"""Fail-closed verification of a maintainer-signed release and its flat payload.

Run from the source checkout (including initialized recursive submodules).
The exact manifest bytes, not a reserialized JSON object, are signed.
Allowed signers must be supplied explicitly from an independent trust source.
"""
import argparse
import datetime
import hashlib
import os
from pathlib import Path
import re
import stat
import subprocess
import sys

sys.dont_write_bytecode = True
from ip_audit import (APPROVAL_SCHEMA, AuditError, ROOT, load_json_strict,
                      parse_rfc3339, reserved_problem, trailing_problem, validate_schema)

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
    """Publish the built commit or an approval-only descendant, never different source."""
    commit(root, built_sha)
    commit(root, published_sha)
    if built_sha == published_sha:
        return
    try:
        result = subprocess.run(
            ["git", "-C", str(root), "merge-base", "--is-ancestor", built_sha, published_sha],
            capture_output=True, timeout=30)
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise Refusal("git results unavailable (merge-base --is-ancestor)") from exc
    if result.returncode == 1:
        raise Refusal("source SHA is not an ancestor of the published commit")
    if result.returncode:
        raise Refusal("git results unavailable (merge-base --is-ancestor)")
    changed = git(root, "diff", "--no-ext-diff", "--no-renames", "--name-only", "-z",
                  built_sha, published_sha, "--")
    for path in changed.split(b"\0"):
        if path and not path.startswith(b"docs/legal/release-decisions/"):
            raise Refusal("non-approval path changed: " + repr(os.fsdecode(path)))


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


def signature(args, data):
    try:
        result = subprocess.run(
            ["ssh-keygen", "-Y", "verify", "-f", str(args.allowed_signers),
             "-I", "sco-maintainer", "-n", "sco-release-v1", "-s", str(args.signature)],
            input=data, capture_output=True, timeout=30)
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise Refusal("signature verification unavailable") from exc
    if result.returncode:
        raise Refusal("maintainer signature verification failed")


def timestamp(text):
    """Validate RFC 3339 with the shared parser, preserving fractional seconds for age checks."""
    if parse_rfc3339(text) is None:
        return None
    return datetime.datetime.fromisoformat(text[:-1] + "+00:00" if text[-1:] in ("Z", "z") else text)


def verify(args):
    failures = []
    try:
        data = args.manifest.read_bytes()
        record = load_json_strict(data)
        schema = load_json_strict((Path(ROOT) / APPROVAL_SCHEMA).read_bytes())
        errors = []
        validate_schema(record, schema, "$", errors)
    except (OSError, AuditError) as exc:
        # Input can contain secrets. Do not echo values, paths or parser/subprocess messages.
        return [str(exc) if isinstance(exc, AuditError) and str(exc) in
                ("duplicate JSON key", "non-finite JSON number", "invalid JSON")
                else "manifest or schema unavailable"]
    if errors:
        # Validator errors include raw values. Replace them with stable, value-free diagnostics.
        for error in errors:
            where = error.split(":", 1)[0]
            if not re.fullmatch(r"\$(?:\.[A-Za-z_][A-Za-z_0-9]*|\[\d+\])*", where):
                where = "$"
            reason = "unknown property" if "unknown property" in error else "invalid or missing field"
            failures.append(f"{where}: {reason}")
        return failures
    if record["schema_version"] != 2:
        return ["signed manifest schema_version must be 2"]

    def check(action):
        try:
            action()
        except (Refusal, OSError, ValueError, UnicodeError):
            exc = sys.exc_info()[1]
            failures.append(str(exc) if isinstance(exc, Refusal) else "check unavailable")

    check(lambda: signature(args, data))
    if record["tag"] != args.tag:
        failures.append("tag mismatch")
    if not SHA.fullmatch(args.source_sha):
        failures.append("source-sha must be a full lowercase SHA")
    else:
        check(lambda: publication_source(Path.cwd(), record["source"]["sha"], args.source_sha))
    if args.run_id is not None and record["builder"]["run_id"] != args.run_id:
        failures.append("builder run id mismatch")
    if record["distribution"]["status"] != "approved-for-publication":
        failures.append("distribution is not approved for publication")
    now = timestamp(args.now) if args.now is not None else datetime.datetime.now(datetime.timezone.utc)
    approved = timestamp(record["approved_at"])
    if now is None or approved is None or args.max_age_days < 0:
        failures.append("invalid verification time or age limit")
    elif approved > now:
        failures.append("approval is in the future")
    elif now - approved > datetime.timedelta(days=args.max_age_days):
        failures.append("approval expired")

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
    for option in ("manifest", "signature", "allowed-signers", "payload-dir"):
        v.add_argument("--" + option, type=Path, required=True)
    v.add_argument("--tag", required=True)
    v.add_argument("--source-sha", required=True,
                   help="full SHA of the commit being published (tagged commit); the manifest source "
                        "must equal it or be its ancestor with only docs/legal/release-decisions/ changed")
    v.add_argument("--run-id", type=int)
    v.add_argument("--max-age-days", type=int, default=7)
    v.add_argument("--now")
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
