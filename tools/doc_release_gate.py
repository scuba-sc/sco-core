#!/usr/bin/env python3
"""Pre-candidate documentation gate: release notes must declare what ships.

The release notes `docs/releases/<tag>.md` carry exactly one machine-consumed scope block:

    <!-- release-scope:start -->
    ```json
    {"tag":"sdk-v2.1.0","sdk_version":"2.1.0","game_pack_version":"0.4.0","plugin_abi":"1.1",
     "profiles":["core","game-sc","legacy","core-source","host-source-sc"]}
    ```
    <!-- release-scope:end -->

The gate parses that JSON (never prose) and compares it with sdk/VERSION, SCO_GAME_PACK_VERSION
in include/sc_game_pack.h, SCO_API_MAJOR/MINOR in include/sco_api.h, the profile keys of
sdk/components.json and the profile loop of release-candidate.yml. It checks that the active
download/extraction coordinates in docs/getting-started.md name the declared versions, and refuses
pre-release scope denials ("not published", "publication is excluded") in the active sections of
the release notes and of docs/host-source-sc.md. Sections under a historical heading (for example
"Previous release") and older release-notes files are not read.

The MCP documents (MCP_DOCS: the discovery and runtime server READMEs, docs/agent-integration.md
and docs/sdk-mcp-tutorial.md) are held to the same coordinates in their active sections: each
release tag, SDK archive name, `SDK x.y.z`, `game-pack x.y.z`, `sdk_version` and
`game_pack_version` they quote must name the declared versions. They must not quote a catalogue
snapshot's identity at all (a 64-hex digest, a 40-hex source SHA, or a count of records, tables,
services or natives): the public catalogue is redeployed from main, so such a value goes stale
with the next deploy. The tool and argument names they document are checked against the
server's tools/list by tools/sco-core-mcp/test_server.py (DocumentationTests).

The public documentation set (the docs listed in site/public-inputs.json, the release notes,
README.md and sdk/README.md) must not cite source by line number (`src/x.cpp:120`): those numbers
move with every edit. Name the function, table or test instead.

With --approval PATH the declared profiles are also compared with that approval record's profiles;
the record and its detached PATH.sig must exist. Signature cryptography stays with
tools/release_verify.py. Without --approval the comparison is reported as not requested.

Exit 0 when every check passes. Otherwise exit 1 with one `refused: ` line per failure on stderr.
This runs before candidate generation; it never changes tools/release_verify.py semantics.
"""
import argparse
import json
from pathlib import Path
import re
import sys

sys.dont_write_bytecode = True

SCOPE_START = "<!-- release-scope:start -->"
SCOPE_END = "<!-- release-scope:end -->"
SCOPE_KEYS = ("tag", "sdk_version", "game_pack_version", "plugin_abi", "profiles")
SEMVER = r"\d+\.\d+\.\d+"

VERSION_FILE = "sdk/VERSION"
COMPONENTS = "sdk/components.json"
GAME_PACK_HEADER = "include/sc_game_pack.h"
API_HEADER = "include/sco_api.h"
CANDIDATE_WORKFLOW = ".github/workflows/release-candidate.yml"
GETTING_STARTED = "docs/getting-started.md"
HOST_SOURCE_DOC = "docs/host-source-sc.md"
PUBLIC_INPUTS = "site/public-inputs.json"
READMES = ("README.md", "sdk/README.md")
LINE_CITATION = re.compile(r"(?<![\w./-])(?:[\w.-]+/)*[\w.-]+\.(?:cpp|hpp|cs|py|lua|c|h):\d+")

# Heading prefixes whose sections describe earlier releases and are exempt from every prose check.
HISTORICAL_HEADINGS = ("previous release", "earlier release", "historical", "superseded",
                       "release history")
DENIALS = (
    re.compile(r"\bnot (?:yet )?(?:be )?published\b"),
    re.compile(r"\bpublication (?:is )?excluded\b"),
    re.compile(r"\bexcluded from publication\b"),
)
TAG_COORDINATE = re.compile(r"\bsdk-v(" + SEMVER + r")\b")
ARCHIVE_COORDINATE = re.compile(
    r"\bsco-sdk-(?:(game-sc|core-source|host-source-sc|core)-)?(" + SEMVER + r")\b")
MCP_DOCS = ("tools/sco-core-mcp/README.md", "tools/sco-core-mcp-runtime/README.md",
            "docs/agent-integration.md", "docs/sdk-mcp-tutorial.md")
# Prose and JSON-key spellings of the two release coordinates, each mapped to its scope key.
MCP_COORDINATES = (
    (re.compile(r"\bSDK\s+`?v?(" + SEMVER + r")\b"), "sdk_version"),
    (re.compile(r"\bsdk_version\b[`\"']?\s*[:=]?\s*[`\"']?(" + SEMVER + r")\b"), "sdk_version"),
    (re.compile(r"\bgame[- ]pack\s+`?(" + SEMVER + r")\b", re.IGNORECASE), "game_pack_version"),
    (re.compile(r"\bgame_pack_version\b[`\"']?\s*[:=]?\s*[`\"']?(" + SEMVER + r")\b"),
     "game_pack_version"),
)
# A catalogue snapshot's identity changes with every site deploy, so these docs never quote one.
SNAPSHOT_IDENTITIES = (
    (re.compile(r"(?<![0-9A-Fa-f])[0-9a-f]{64}(?![0-9A-Fa-f])"), "a sha256 digest"),
    (re.compile(r"(?<![0-9A-Fa-f])[0-9a-f]{40}(?![0-9A-Fa-f])"), "a source SHA"),
    (re.compile(r"\b\d+\s+(?:records|tables|services|(?:engine\s+)?natives)\b"),
     "a catalogue count"),
)


class Refusal(Exception):
    """One failed check, reported as one `refused: ` line."""


def load_json_strict(data, label):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise Refusal(f"{label}: duplicate JSON key")
            result[key] = value
        return result

    def constant(_value):
        raise Refusal(f"{label}: non-finite JSON number")

    try:
        return json.loads(data, object_pairs_hook=pairs, parse_constant=constant)
    except (ValueError, UnicodeError, RecursionError) as exc:
        raise Refusal(f"{label}: invalid JSON") from exc


def read_text(root, relative):
    try:
        return (root / relative).read_text(encoding="utf-8")
    except (OSError, UnicodeError) as exc:
        raise Refusal(f"{relative} is missing or unreadable") from exc


def parse_scope(notes, label):
    starts, ends = notes.count(SCOPE_START), notes.count(SCOPE_END)
    if starts == 0 and ends == 0:
        raise Refusal(f"{label} has no release-scope block")
    if starts != 1 or ends != 1:
        raise Refusal(f"{label} must contain exactly one release-scope block")
    begin = notes.index(SCOPE_START) + len(SCOPE_START)
    end = notes.index(SCOPE_END)
    if end < begin:
        raise Refusal(f"{label} release-scope end precedes its start")
    match = re.fullmatch(r"\s*```json\n(.*?)\n```\s*", notes[begin:end], re.DOTALL)
    if not match or "```" in match.group(1):
        raise Refusal(f"{label} release-scope block must hold exactly one json fence")
    scope = load_json_strict(match.group(1), f"{label} release-scope block")
    if not isinstance(scope, dict) or set(scope) != set(SCOPE_KEYS):
        raise Refusal(f"{label} release-scope block must have exactly the keys "
                      + ", ".join(SCOPE_KEYS))
    for key, pattern in (("tag", r"sdk-v" + SEMVER), ("sdk_version", SEMVER),
                         ("game_pack_version", SEMVER), ("plugin_abi", r"\d+\.\d+")):
        if not isinstance(scope[key], str) or not re.fullmatch(pattern, scope[key]):
            raise Refusal(f"{label} release-scope {key} is malformed")
    profiles = scope["profiles"]
    if (not isinstance(profiles, list) or not profiles
            or not all(isinstance(p, str) and p for p in profiles)
            or len(set(profiles)) != len(profiles)):
        raise Refusal(f"{label} release-scope profiles must be a nonempty list of unique names")
    return scope


def active_text(text):
    """Text outside the scope block and outside historical sections; headings in fences ignored.

    Dropped lines become empty lines, so line numbers in the result match the source file.
    """
    if text.count(SCOPE_START) == 1 and text.count(SCOPE_END) == 1:
        start, end = text.index(SCOPE_START), text.index(SCOPE_END) + len(SCOPE_END)
        if start < end:
            text = text[:start] + "\n" * text.count("\n", start, end) + text[end:]
    kept, skip_level, fenced = [], None, False
    for line in text.splitlines():
        if line.lstrip().startswith("```"):
            fenced = not fenced
        heading = None if fenced else re.match(r"^(#{1,6})\s+(.*?)\s*#*\s*$", line)
        if heading:
            level = len(heading.group(1))
            title = re.sub(r"[*_`]", "", heading.group(2)).strip().lower()
            if skip_level is not None and level <= skip_level:
                skip_level = None
            if skip_level is None and title.startswith(HISTORICAL_HEADINGS):
                skip_level = level
        kept.append(line if skip_level is None else "")
    return "\n".join(kept)


def denial_refusals(text, label):
    normalized = " ".join(text.replace("*", "").lower().split())
    found = sorted({m.group(0) for pattern in DENIALS for m in pattern.finditer(normalized)})
    return [f"{label} active sections contain pre-release scope denial {phrase!r}"
            for phrase in found]


def coordinate_refusals(text, scope):
    refusals, tags, archives = [], 0, 0
    for match in TAG_COORDINATE.finditer(text):
        tags += 1
        if match.group(1) != scope["sdk_version"]:
            refusals.append(f"{GETTING_STARTED} downloads tag sdk-v{match.group(1)}, "
                            f"not {scope['tag']}")
    for match in ARCHIVE_COORDINATE.finditer(text):
        archives += 1
        profile, version = match.group(1), match.group(2)
        expected = scope["game_pack_version"] if profile == "game-sc" else scope["sdk_version"]
        if version != expected:
            refusals.append(f"{GETTING_STARTED} names {match.group(0)}, not version {expected}")
    if not tags or not archives:
        refusals.append(f"{GETTING_STARTED} has no active download tag and archive coordinate")
    return refusals


def mcp_doc_refusals(root, scope):
    refusals = []
    for relative in MCP_DOCS:
        try:
            text = active_text(read_text(root, relative))
        except Refusal as exc:
            refusals.append(str(exc))
            continue
        for number, line in enumerate(text.splitlines(), 1):
            quoted = [(m.group(1), scope["sdk_version"]) for m in TAG_COORDINATE.finditer(line)]
            quoted += [(m.group(2), scope["game_pack_version"] if m.group(1) == "game-sc"
                        else scope["sdk_version"]) for m in ARCHIVE_COORDINATE.finditer(line)]
            quoted += [(m.group(1), scope[key]) for pattern, key in MCP_COORDINATES
                       for m in pattern.finditer(line)]
            refusals += [f"{relative} line {number} quotes version {found}, not the "
                         f"declared {expected}" for found, expected in quoted if found != expected]
            refusals += [f"{relative} line {number} pins {what} {m.group(0)!r} of a "
                         "catalogue snapshot; tell the reader to check the file they download"
                         for pattern, what in SNAPSHOT_IDENTITIES for m in pattern.finditer(line)]
    return refusals


def public_docs(root, notes_path):
    data = load_json_strict(read_text(root, PUBLIC_INPUTS), PUBLIC_INPUTS)
    docs = data.get("docs") if isinstance(data, dict) else None
    if (not isinstance(docs, list) or not docs
            or not all(isinstance(d, str) and d and not Path(d).is_absolute()
                       and ".." not in Path(d).parts for d in docs)):
        raise Refusal(f"{PUBLIC_INPUTS} must list docs as nonempty repository-relative paths")
    return list(dict.fromkeys([*docs, notes_path, GETTING_STARTED, *READMES]))


def citation_refusals(root, notes_path):
    try:
        documents = public_docs(root, notes_path)
    except Refusal as exc:
        return [str(exc)]
    refusals = []
    for relative in documents:
        try:
            text = read_text(root, relative)
        except Refusal as exc:
            refusals.append(str(exc))
            continue
        for number, line in enumerate(text.splitlines(), 1):
            refusals += [f"{relative} line {number} cites source by line number {m.group(0)!r}; "
                         "name the function, table or test instead"
                         for m in LINE_CITATION.finditer(line)]
    return refusals


def header_define(root, relative, pattern):
    match = re.search(pattern, read_text(root, relative), re.MULTILINE)
    if not match:
        raise Refusal(f"{relative} does not define the expected version macro")
    return match.groups()


def components_profiles(root):
    data = load_json_strict(read_text(root, COMPONENTS), COMPONENTS)
    profiles = data.get("profiles") if isinstance(data, dict) else None
    if not isinstance(profiles, dict) or not profiles:
        raise Refusal(f"{COMPONENTS} has no profiles map")
    return list(profiles)


def workflow_profiles(root):
    loops = re.findall(r"^\s*for profile in ([A-Za-z0-9 _.-]+); do\s*$",
                       read_text(root, CANDIDATE_WORKFLOW), re.MULTILINE)
    if len(loops) != 1:
        raise Refusal(f"{CANDIDATE_WORKFLOW} must contain exactly one profile packaging loop")
    return loops[0].split()


def approval_refusals(path, scope):
    if not path.is_file():
        return [f"approval record {path} not found"]
    if not Path(str(path) + ".sig").is_file():
        return [f"approval record {path} has no detached .sig signature"]
    try:
        record = load_json_strict(path.read_bytes(), f"approval record {path}")
    except OSError:
        return [f"approval record {path} is unreadable"]
    except Refusal as exc:
        return [str(exc)]
    if not isinstance(record, dict):
        return [f"approval record {path} is not a JSON object"]
    refusals = []
    # Schema version 2 names the tag `tag`; version 1 names it `release_tag`.
    if record.get("tag", record.get("release_tag")) != scope["tag"]:
        refusals.append(f"approval record {path} is not for {scope['tag']}")
    if record.get("profiles") != scope["profiles"]:
        refusals.append(f"approval record {path} profiles differ from the declared release scope")
    return refusals


def run_gate(root, tag, approval=None):
    root = Path(root)
    if not re.fullmatch(r"sdk-v" + SEMVER, tag):
        return [f"--tag {tag!r} is not sdk-v<major>.<minor>.<patch>"], []
    notes_path = f"docs/releases/{tag}.md"
    try:
        notes = read_text(root, notes_path)
        scope = parse_scope(notes, notes_path)
    except Refusal as exc:
        return [str(exc)], []

    refusals = []

    def compare(declared, actual, what):
        if declared != actual:
            refusals.append(f"release-scope {what} {declared!r} != {actual!r}")

    compare(scope["tag"], tag, "tag vs --tag")
    compare(scope["tag"], "sdk-v" + scope["sdk_version"], "tag vs sdk_version")
    checks = (
        (lambda: read_text(root, VERSION_FILE).strip(), "sdk_version", f"vs {VERSION_FILE}"),
        (lambda: header_define(root, GAME_PACK_HEADER,
                               r'^#define\s+SCO_GAME_PACK_VERSION\s+"(' + SEMVER + r')"')[0],
         "game_pack_version", f"vs {GAME_PACK_HEADER}"),
        (lambda: ".".join(header_define(root, API_HEADER,
                                        r"^#define\s+SCO_API_MAJOR\s+(\d+)\s*$[\s\S]*?"
                                        r"^#define\s+SCO_API_MINOR\s+(\d+)\s*$")),
         "plugin_abi", f"vs {API_HEADER}"),
        (lambda: components_profiles(root), "profiles", f"vs {COMPONENTS}"),
        (lambda: workflow_profiles(root), "profiles", f"vs {CANDIDATE_WORKFLOW}"),
    )
    for actual, key, what in checks:
        try:
            compare(scope[key], actual(), f"{key} {what}")
        except Refusal as exc:
            refusals.append(str(exc))

    try:
        refusals += coordinate_refusals(active_text(read_text(root, GETTING_STARTED)), scope)
    except Refusal as exc:
        refusals.append(str(exc))

    refusals += mcp_doc_refusals(root, scope)
    refusals += denial_refusals(active_text(notes), notes_path)
    refusals += citation_refusals(root, notes_path)
    if "host-source-sc" in scope["profiles"]:
        try:
            refusals += denial_refusals(active_text(read_text(root, HOST_SOURCE_DOC)),
                                        HOST_SOURCE_DOC)
        except Refusal as exc:
            refusals.append(str(exc))

    notes_out = []
    if approval is None:
        notes_out.append("approval: not compared (no --approval given)")
    else:
        approval_path = Path(approval)
        if not approval_path.is_absolute():
            approval_path = root / approval_path
        found = approval_refusals(approval_path, scope)
        refusals += found
        if not found:
            notes_out.append(f"approval: profiles match {approval}")
    return refusals, notes_out


class Parser(argparse.ArgumentParser):
    def error(self, message):
        raise Refusal("invalid command arguments: " + message)


def main(argv=None):
    parser = Parser(description="Refuse a release candidate whose documentation does not "
                                "declare exactly what ships.")
    parser.add_argument("--tag", required=True, help="Release tag, sdk-v<version>")
    parser.add_argument("--approval", help="Approval record (relative to --root) whose profiles "
                                           "must match; its .sig must exist beside it")
    parser.add_argument("--root", default=str(Path(__file__).resolve().parent.parent),
                        help="Repository root (default: this checkout)")
    try:
        args = parser.parse_args(argv)
    except Refusal as exc:
        print("refused: " + str(exc), file=sys.stderr)
        return 1
    refusals, notes = run_gate(args.root, args.tag, args.approval)
    for line in notes:
        print(line)
    for line in refusals:
        print("refused: " + line, file=sys.stderr)
    if refusals:
        return 1
    print(f"documentation gate passed for {args.tag}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
