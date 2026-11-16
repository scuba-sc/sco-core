#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Keep the release coordinates in the public docs in step with the source, without hand edits.

Every coordinate comes from a file that already defines it:

    sdk_version        sdk/VERSION
    game_pack_version  SCO_GAME_PACK_VERSION in include/sc_game_pack.h
    game_build         SCO_GAME_PACK_VERIFIED_BUILD in include/sc_game_pack.h
    plugin_abi         SCO_API_MAJOR.SCO_API_MINOR in include/sco_api.h
    previous_version   the newest docs/releases/sdk-v<x.y.z>.md older than sdk_version

Docs carry generated regions in two forms:

    <!-- release-block:NAME -->            whole paragraphs, tables or commands, rendered from
    ...                                    docs/release-blocks/NAME.md
    <!-- /release-block:NAME -->

    <!--rv:KEY-->2.4.1<!--/rv-->           one inline value (KEY is a coordinate below)

Templates use {{KEY}}. Keys: sdk_version, tag, game_pack_version, game_build, plugin_abi,
previous_version, previous_tag, release_page, previous_release_page, notes, previous_notes,
notes_url, previous_notes_url,
archive_core, archive_game_sc, archive_combined, archive_core_source, archive_host_source.
`notes` and `previous_notes` are repository paths (docs/releases/...). `root` is the relative path
from the file being rendered to the repository root (`../` from docs/, `` from the root), so a
template links a file as `{{root}}{{notes}}` and the link works from every folder.

    release_docs.py --check            exit 1 when any region differs from its rendering
    release_docs.py --write            rewrite every region in place (line endings preserved)
    release_docs.py --list             print the coordinates and every region found
    release_docs.py --bump X.Y.Z [--game-pack A.B.C] [--game-build B]
                                       start release X.Y.Z: version files, candidate tag,
                                       development coordinate, changelog heading, release-notes
                                       skeleton, site allowlist and hub entry, then --write

Prose outside regions must not quote the current coordinates; tools/doc_release_gate.py refuses
stale ones in active sections. Digests and source commits are never generated here: they belong
in the release notes after the release workflow has produced them.
"""
import argparse
import json
import re
import sys
from pathlib import Path

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[1]
TEMPLATES = Path("docs/release-blocks")
SEMVER = re.compile(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)")
BUILD = re.compile(r"([0-9]+)\.([0-9]+)\.([0-9]+)\.([0-9]+)")
BLOCK = re.compile(r"(<!-- release-block:([a-z0-9-]+) -->)(.*?)(<!-- /release-block:\2 -->)", re.S)
INLINE = re.compile(r"(<!--rv:([a-z_]+)-->)(.*?)(<!--/rv-->)")
PLACEHOLDER = re.compile(r"\{\{([a-z_]+)\}\}")
# Files searched for regions: every Markdown file under these roots, plus these files.
SCAN_ROOTS = ("docs", "sdk/docs", "tools/sco-core-mcp", "tools/sco-core-mcp-runtime")
SCAN_FILES = ("README.md", "sdk/README.md")
SKIP_PARTS = {"node_modules", "release-blocks", "releases"}
RELEASES = "https://github.com/scuba-sc/sco-core/releases/tag/"
SOURCE = "https://github.com/scuba-sc/sco-core/blob/"


class Refusal(Exception):
    pass


def text(rel):
    return (ROOT / rel).read_text(encoding="utf-8")


def macro(rel, name):
    match = re.search(r"#define\s+" + name + r"\s+\"?([^\"\s]+)\"?", text(rel))
    if not match:
        raise Refusal(f"{rel}: {name} not found")
    return match.group(1)


def newline_of(raw):
    """The line ending a file mostly uses; a stray line of the other kind does not flip it."""
    return "\r\n" if raw.count("\r\n") * 2 > raw.count("\n") else "\n"


def version_tuple(value):
    match = SEMVER.fullmatch(value)
    if not match:
        raise Refusal(f"not a version: {value!r}")
    return tuple(int(part) for part in match.groups())


def released_versions():
    found = []
    for path in (ROOT / "docs/releases").glob("sdk-v*.md"):
        version = path.stem[len("sdk-v"):]
        if SEMVER.fullmatch(version):
            found.append(version)
    return sorted(found, key=version_tuple)


def coordinates():
    sdk = text("sdk/VERSION").strip()
    version_tuple(sdk)
    game = macro("include/sc_game_pack.h", "SCO_GAME_PACK_VERSION")
    version_tuple(game)
    abi = macro("include/sco_api.h", "SCO_API_MAJOR") + "." + macro("include/sco_api.h", "SCO_API_MINOR")
    older = [v for v in released_versions() if version_tuple(v) < version_tuple(sdk)]
    if not older:
        raise Refusal("no earlier docs/releases/sdk-v<version>.md to name as the previous release")
    previous = older[-1]
    return {
        "sdk_version": sdk,
        "tag": "sdk-v" + sdk,
        "game_pack_version": game,
        "game_build": macro("include/sc_game_pack.h", "SCO_GAME_PACK_VERIFIED_BUILD"),
        "plugin_abi": abi,
        "previous_version": previous,
        "previous_tag": "sdk-v" + previous,
        "release_page": RELEASES + "sdk-v" + sdk,
        "previous_release_page": RELEASES + "sdk-v" + previous,
        "notes": "docs/releases/sdk-v" + sdk + ".md",
        "previous_notes": "docs/releases/sdk-v" + previous + ".md",
        # Absolute links pinned to the tag: they also work in packaged kits, whose layout differs.
        "notes_url": SOURCE + "sdk-v" + sdk + "/docs/releases/sdk-v" + sdk + ".md",
        "previous_notes_url": SOURCE + "sdk-v" + previous + "/docs/releases/sdk-v" + previous + ".md",
        "archive_core": f"sco-sdk-core-{sdk}.zip",
        "archive_game_sc": f"sco-sdk-game-sc-{game}.zip",
        "archive_combined": f"sco-sdk-{sdk}.zip",
        "archive_core_source": f"sco-sdk-core-source-{sdk}.zip",
        "archive_host_source": f"sco-sdk-host-source-sc-{sdk}.zip",
    }


def render(template, values, label):
    def sub(match):
        key = match.group(1)
        if key not in values:
            raise Refusal(f"{label}: unknown placeholder {{{{{key}}}}}")
        return values[key]
    return PLACEHOLDER.sub(sub, template)


def template(name, values):
    path = ROOT / TEMPLATES / (name + ".md")
    if not path.is_file():
        raise Refusal(f"release block '{name}' has no template {TEMPLATES.as_posix()}/{name}.md")
    body = path.read_text(encoding="utf-8").replace("\r\n", "\n")
    # Templates may start with an HTML comment explaining them; it is not rendered.
    body = re.sub(r"\A<!--.*?-->\n", "", body, flags=re.S)
    return render(body.strip("\n"), values, f"{TEMPLATES.as_posix()}/{name}.md")


def doc_files():
    seen = []
    for rel in SCAN_FILES:
        if (ROOT / rel).is_file():
            seen.append(Path(rel))
    for root in SCAN_ROOTS:
        for path in sorted((ROOT / root).rglob("*.md")):
            rel = path.relative_to(ROOT)
            if SKIP_PARTS.intersection(rel.parts):
                continue
            if rel not in seen:
                seen.append(rel)
    return seen


def regenerate(source, values, label):
    """Return (new text, region names) for one file's text; line endings follow the file."""
    depth = len(Path(label).parts) - 1
    values = dict(values, root="../" * depth)
    newline = newline_of(source)
    names = []

    def block(match):
        names.append("block " + match.group(2))
        body = template(match.group(2), values).replace("\n", newline)
        return match.group(1) + newline + body + newline + match.group(4)

    def inline(match):
        key = match.group(2)
        if key not in values:
            raise Refusal(f"{label}: unknown inline key {key}")
        names.append("inline " + key)
        return match.group(1) + values[key] + match.group(4)

    if source.count("<!-- release-block:") != len(BLOCK.findall(source)):
        raise Refusal(f"{label}: unterminated or mismatched release-block marker")
    out = BLOCK.sub(block, source)
    if out.count("<!--rv:") != len(INLINE.findall(out)):
        raise Refusal(f"{label}: unterminated inline rv marker (close it with <!--/rv--> on the same line)")
    out = INLINE.sub(inline, out)
    return out, names


def run(write):
    values = coordinates()
    stale, regions = [], 0
    for rel in doc_files():
        raw = (ROOT / rel).read_bytes().decode("utf-8")
        new, names = regenerate(raw, values, rel.as_posix())
        regions += len(names)
        if new != raw:
            stale.append(rel.as_posix())
            if write:
                (ROOT / rel).write_bytes(new.encode("utf-8"))
    return values, stale, regions


def replace_once(rel, old, new):
    path = ROOT / rel
    raw = path.read_bytes().decode("utf-8")
    newline = newline_of(raw)
    old, new = old.replace("\n", newline), new.replace("\n", newline)
    if raw.count(old) != 1:
        raise Refusal(f"{rel}: expected exactly one {old.strip()[:60]!r}")
    path.write_bytes(raw.replace(old, new, 1).encode("utf-8"))


def write_keep_newline(rel, content):
    path = ROOT / rel
    newline = newline_of(path.read_bytes().decode("utf-8")) if path.is_file() else "\n"
    path.write_bytes(content.replace("\n", newline).encode("utf-8"))


def set_define(rel, name, value):
    """Set the value of `#define NAME value` in a header; refuses unless it is defined exactly once."""
    path = ROOT / rel
    raw = path.read_bytes().decode("utf-8")
    pattern = re.compile(r"(#define[ \t]+" + name + r"[ \t]+)\S+")
    if len(pattern.findall(raw)) != 1:
        raise Refusal(f"{rel}: expected exactly one '#define {name}'")
    path.write_bytes(pattern.sub(lambda match: match.group(1) + value, raw).encode("utf-8"))


def has_define(rel, name):
    return re.search(r"#define[ \t]+" + name + r"[ \t]", text(rel)) is not None


def preflight(version, game_pack, game_build):
    """Check everything bump() will touch before it writes anything, so a refusal leaves no half-bump."""
    older = [v for v in released_versions() if version_tuple(v) < version_tuple(version)]
    if not older:
        raise Refusal("no earlier docs/releases/sdk-v<version>.md to name as the previous release")
    notes, tag = f"docs/releases/sdk-v{version}.md", "sdk-v" + version
    if game_pack:
        version_tuple(game_pack)
        macro("include/sc_game_pack.h", "SCO_GAME_PACK_VERSION")
    if game_build:
        if not BUILD.fullmatch(game_build):
            raise Refusal(f"not a game build (four dot-separated numbers): {game_build!r}")
        macro("include/sc_game_pack.h", "SCO_GAME_PACK_VERIFIED_BUILD")
    if not isinstance(json.loads(text("sdk/components.json")).get("profiles"), dict):
        raise Refusal("sdk/components.json has no profiles")
    if not re.search(r"(?m)^sdk-v\S+$", text("sdk/CANDIDATE_TAG")):
        raise Refusal("sdk/CANDIDATE_TAG has no sdk-v<version> line")
    changelog = text("CHANGELOG.md")
    if f"\n## {tag}\n" not in changelog and "## Unreleased\n\n" not in changelog:
        raise Refusal("CHANGELOG.md has neither a '## Unreleased' heading nor the new section")
    inputs = json.loads(text("site/public-inputs.json"))
    if not isinstance(inputs.get("docs"), list):
        raise Refusal("site/public-inputs.json has no docs list")
    gen = text("site/scripts/gen.py")
    if f"('{notes}'," not in gen and not re.search(
            r"\n\s*\('" + re.escape(f"docs/releases/sdk-v{older[-1]}.md") + r"', '[^']*'\),", gen):
        raise Refusal(f"site/scripts/gen.py: no hub entry for docs/releases/sdk-v{older[-1]}.md "
                      f"to insert {notes} before")


def bump(version, game_pack, game_build):
    version_tuple(version)
    current = text("sdk/VERSION").strip()
    if version_tuple(version) <= version_tuple(current):
        raise Refusal(f"{version} does not exceed sdk/VERSION {current}")
    preflight(version, game_pack, game_build)
    major, minor, patch = version_tuple(version)
    write_keep_newline("sdk/VERSION", version + "\n")
    write_keep_newline("sdk/DEVELOPMENT_VERSION", f"{major}.{minor}.{patch + 1}-dev.1\n")
    tag_file = text("sdk/CANDIDATE_TAG")
    write_keep_newline("sdk/CANDIDATE_TAG", re.sub(r"(?m)^sdk-v\S+$", "sdk-v" + version, tag_file))
    header = "include/sc_game_pack.h"
    if game_pack:
        set_define(header, "SCO_GAME_PACK_VERSION", f'"{game_pack}"')
        # The numeric mirrors must agree with the string: game.build refuses to start otherwise.
        for part, number in zip(("MAJOR", "MINOR", "PATCH"), version_tuple(game_pack)):
            if has_define(header, "SCO_GAME_PACK_VERSION_" + part):
                set_define(header, "SCO_GAME_PACK_VERSION_" + part, str(number))
    if game_build:
        set_define(header, "SCO_GAME_PACK_VERIFIED_BUILD", f'"{game_build}"')
        for index, number in enumerate(BUILD.fullmatch(game_build).groups()):
            if has_define(header, f"SCO_GAME_PACK_VERIFIED_BUILD_PARTS_{index}"):
                set_define(header, f"SCO_GAME_PACK_VERIFIED_BUILD_PARTS_{index}", str(int(number)))
        print("note: SCO_GAME_PACK_VERIFIED_PE_TIMESTAMP and docs/game-pack.md still describe the "
              "previous build; update them from the sigcheck report", file=sys.stderr)
    values = coordinates()
    notes = values["notes"]
    if not (ROOT / notes).exists():
        scope = json.dumps({"tag": values["tag"], "sdk_version": version,
                            "game_pack_version": values["game_pack_version"],
                            "plugin_abi": values["plugin_abi"],
                            "profiles": list(json.loads(text("sdk/components.json"))["profiles"])},
                           separators=(",", ":"))
        write_keep_newline(notes, template("release-notes-skeleton", values).replace("{scope}", scope) + "\n")
    changelog = text("CHANGELOG.md")
    if f"\n## {values['tag']}\n" not in changelog:
        replace_once("CHANGELOG.md", "## Unreleased\n\n", f"## Unreleased\n\n## {values['tag']}\n\n")
    inputs = json.loads(text("site/public-inputs.json"))
    if notes not in inputs["docs"]:
        inputs["docs"].append(notes)
        write_keep_newline("site/public-inputs.json", json.dumps(inputs, indent=2, ensure_ascii=False) + "\n")
    gen = text("site/scripts/gen.py")
    if f"('{notes}'," not in gen:
        prev = values["previous_notes"]
        line = re.search(r"\n(\s*)\('" + re.escape(prev) + r"', '[^']*'\),", gen)
        replace_once("site/scripts/gen.py", line.group(0),
                     f"\n{line.group(1)}('{notes}', 'SDK {version} release notes.'),{line.group(0)}")
    return values


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true")
    mode.add_argument("--write", action="store_true")
    mode.add_argument("--list", action="store_true")
    mode.add_argument("--bump", metavar="X.Y.Z")
    parser.add_argument("--game-pack", metavar="A.B.C")
    parser.add_argument("--game-build", metavar="BUILD")
    args = parser.parse_args(argv)
    if (args.game_pack or args.game_build) and not args.bump:
        parser.error("--game-pack and --game-build only apply with --bump")
    try:
        if args.bump:
            bump(args.bump, args.game_pack, args.game_build)
            args.write = True
        values, stale, regions = run(write=args.write)
    except Refusal as exc:
        print("refused: " + str(exc), file=sys.stderr)
        return 1
    if args.list:
        for key, value in values.items():
            print(f"{key} = {value}")
        for rel in doc_files():
            _, names = regenerate((ROOT / rel).read_text(encoding="utf-8"), values, rel.as_posix())
            for name in names:
                print(f"{rel.as_posix()}: {name}")
        return 0
    if args.check and stale:
        for rel in stale:
            print(f"refused: {rel} has release regions that differ from tools/release_docs.py --write",
                  file=sys.stderr)
        return 1
    verb = "rewrote" if args.write else "checked"
    print(f"{verb} {regions} release region(s); {len(stale)} file(s) {'updated' if args.write else 'stale'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
