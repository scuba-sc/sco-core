#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Keep the release coordinates in the public docs in step with the source, without hand edits.

Every coordinate comes from a file that already defines it:

    sdk_version        sdk/VERSION
    game_pack_version  SCO_GAME_PACK_VERSION in include/sc_game_pack.h
    game_build         SCO_GAME_PACK_VERIFIED_BUILD in include/sc_game_pack.h
    game_patch         the public patch name of game_build in docs/game/builds.json, which must
                       list game_build as its newest build (the generated block `game-builds`
                       renders that register: every build, its pins and the rows that changed)
    plugin_abi         SCO_API_MAJOR.SCO_API_MINOR in include/sco_api.h
    previous_version   the newest docs/releases/sdk-v<x.y.z>.md older than sdk_version

Docs carry generated regions in two forms:

    <!-- release-block:NAME -->            whole paragraphs, tables or commands, rendered from
    ...                                    docs/release-blocks/NAME.md
    <!-- /release-block:NAME -->

    <!--rv:KEY-->2.4.1<!--/rv-->           one inline value (KEY is a coordinate below)

Templates use {{KEY}}. Keys: sdk_version, tag, download_tag, candidate_label, game_pack_version,
game_build, game_patch, plugin_abi, previous_version, previous_tag, release_page,
previous_release_page, notes, previous_notes, notes_url, previous_notes_url,
archive_core, archive_game_sc, archive_combined, archive_core_source, archive_host_source.
`tag` is always the final tag sdk-v<sdk_version>. While sdk/CANDIDATE_TAG names one of its release
candidates (sdk-v<sdk_version>-rc.<n>), `download_tag` is that candidate tag and `release_page`
and `notes_url` point at it, because the final release page does not exist yet; `candidate_label`
then says so in the sentence that introduces the release. Once the file names the final tag,
`download_tag` equals `tag` and `candidate_label` is empty.
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
REGISTER = "docs/game/builds.json"
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


def candidate_number(sdk):
    """The release candidate number sdk/CANDIDATE_TAG names for this SDK version; None for the final tag."""
    path = ROOT / "sdk/CANDIDATE_TAG"
    if not path.is_file():
        return None
    lines = [line.strip() for line in text("sdk/CANDIDATE_TAG").splitlines()
             if line.strip() and not line.lstrip().startswith("#")]
    if len(lines) != 1:
        raise Refusal("sdk/CANDIDATE_TAG must contain exactly one tag")
    match = re.fullmatch(r"sdk-v" + re.escape(sdk) + r"(?:-rc\.([1-9][0-9]*))?", lines[0])
    if not match:
        raise Refusal(f"sdk/CANDIDATE_TAG names {lines[0]!r}, not sdk-v{sdk} or one of its -rc.<number> tags")
    return match.group(1)


def coordinates():
    sdk = text("sdk/VERSION").strip()
    version_tuple(sdk)
    candidate = candidate_number(sdk)
    download = "sdk-v" + sdk + (f"-rc.{candidate}" if candidate else "")
    label = (f" (release candidate {candidate}, a pre-release for testing; the final release follows)"
             if candidate else "")
    game = macro("include/sc_game_pack.h", "SCO_GAME_PACK_VERSION")
    version_tuple(game)
    abi = macro("include/sco_api.h", "SCO_API_MAJOR") + "." + macro("include/sco_api.h", "SCO_API_MINOR")
    older = [v for v in released_versions() if version_tuple(v) < version_tuple(sdk)]
    if not older:
        raise Refusal("no earlier docs/releases/sdk-v<version>.md to name as the previous release")
    previous = older[-1]
    game_build = macro("include/sc_game_pack.h", "SCO_GAME_PACK_VERIFIED_BUILD")
    current = current_build_entry(game_build, game)
    return {
        "sdk_version": sdk,
        "tag": "sdk-v" + sdk,
        "download_tag": download,
        "candidate_label": label,
        "game_pack_version": game,
        "game_build": game_build,
        "game_patch": current["patch"],
        "plugin_abi": abi,
        "previous_version": previous,
        "previous_tag": "sdk-v" + previous,
        "release_page": RELEASES + download,
        "previous_release_page": RELEASES + "sdk-v" + previous,
        "notes": "docs/releases/sdk-v" + sdk + ".md",
        "previous_notes": "docs/releases/sdk-v" + previous + ".md",
        # Absolute links pinned to the tag: they also work in packaged kits, whose layout differs.
        "notes_url": SOURCE + download + "/docs/releases/sdk-v" + sdk + ".md",
        "previous_notes_url": SOURCE + "sdk-v" + previous + "/docs/releases/sdk-v" + previous + ".md",
        "archive_core": f"sco-sdk-core-{sdk}.zip",
        "archive_game_sc": f"sco-sdk-game-sc-{game}.zip",
        "archive_combined": f"sco-sdk-{sdk}.zip",
        "archive_core_source": f"sco-sdk-core-source-{sdk}.zip",
        "archive_host_source": f"sco-sdk-host-source-sc-{sdk}.zip",
    }


BUILD_FIELDS = ("build", "patch", "channel", "changelist", "live_date", "name_source", "name_note",
                "game_packs", "sdk_releases", "changed_rows", "changes")
DATE = re.compile(r"[0-9]{4}-[0-9]{2}-[0-9]{2}")


def build_register():
    """The game-build register, checked: every field present, builds strictly ascending."""
    if not (ROOT / REGISTER).is_file():
        raise Refusal(f"{REGISTER} is missing: the register of verified game builds")
    builds = json.loads(text(REGISTER)).get("builds")
    if not isinstance(builds, list) or not builds:
        raise Refusal(f"{REGISTER}: no builds")
    previous = None
    for entry in builds:
        label = f"{REGISTER}: {entry.get('build')!r}"
        missing = [f for f in BUILD_FIELDS if f not in entry]
        if missing:
            raise Refusal(f"{label}: missing {', '.join(missing)}")
        match = BUILD.fullmatch(entry["build"])
        if not match:
            raise Refusal(f"{label}: not a four-part game build")
        parts = tuple(int(p) for p in match.groups())
        if previous is not None and parts <= previous:
            raise Refusal(f"{label}: builds must be listed oldest first, each newer than the last")
        previous = parts
        if not entry["patch"].strip() or not entry["changelist"].isdigit():
            raise Refusal(f"{label}: needs a patch name and a numeric changelist")
        if not DATE.fullmatch(entry["live_date"]):
            raise Refusal(f"{label}: live_date must be YYYY-MM-DD")
        if entry["name_source"] not in ("official", "community"):
            raise Refusal(f"{label}: name_source must be official or community")
        for field in ("game_packs", "sdk_releases", "changed_rows", "changes"):
            if not isinstance(entry[field], list) or not all(isinstance(v, str) for v in entry[field]):
                raise Refusal(f"{label}: {field} must be a list of strings")
        for pack in entry["game_packs"]:
            version_tuple(pack)
        if not entry["changes"]:
            raise Refusal(f"{label}: record what changed for the rows, even when nothing moved")
    return builds


def current_build_entry(game_build, game_pack):
    """The register entry for the verified build; it must be the newest and name the game pack."""
    builds = build_register()
    newest = builds[-1]
    if newest["build"] != game_build:
        raise Refusal(f"SCO_GAME_PACK_VERIFIED_BUILD is {game_build}, but the newest build in {REGISTER} "
                      f"is {newest['build']}: add the build with its public patch name first")
    if game_pack not in newest["game_packs"]:
        raise Refusal(f"{REGISTER}: {game_build} does not list game pack {game_pack} in game_packs")
    return newest


def render_builds(values):
    """The game-builds region: a table of every build, newest first, then each build's row changes."""
    builds = list(reversed(build_register()))
    lines = ["| Patch | Game build | Changelist | On LIVE | Game pack | SDK releases |",
             "|---|---|---|---|---|---|"]
    for entry in builds:
        packs = ", ".join(entry["game_packs"]) or "none (before the verified-build pin)"
        releases = ", ".join(f"`{tag}`" + (" (this release)" if tag == values["tag"] else "")
                             for tag in entry["sdk_releases"]) or "none"
        patch = entry["patch"] + ("" if entry["name_source"] == "official" else "\\*")
        lines.append(f"| {patch} | {entry['build']} | {entry['changelist']} | {entry['live_date']} "
                     f"| {packs} | {releases} |")
    lines.append("")
    if any(entry["name_source"] != "official" for entry in builds):
        lines += ["\\* Patch name from community patch coverage of the launcher build string, "
                  "not yet matched against the official notes.", ""]
    older = {e["build"]: o for e, o in zip(builds, builds[1:] + [None])}
    for entry in builds:
        before = older[entry["build"]]
        since = f" (since {before['build']}, {before['patch']})" if before else ""
        lines += [f"### {entry['patch']}: {entry['build']}{since}", ""]
        if entry["changed_rows"]:
            lines += ["Rows that changed: " + ", ".join(f"`{r}`" for r in entry["changed_rows"]) + ".", ""]
        lines += [f"- {change}" for change in entry["changes"]]
        lines += [f"- Name: {entry['name_note']}", ""]
    return "\n".join(lines).rstrip("\n")


GENERATED = {"game-builds": render_builds}


def render(template, values, label):
    def sub(match):
        key = match.group(1)
        if key not in values:
            raise Refusal(f"{label}: unknown placeholder {{{{{key}}}}}")
        return values[key]
    return PLACEHOLDER.sub(sub, template)


def template(name, values):
    if name in GENERATED:
        return GENERATED[name](values)
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
        if build_register()[-1]["build"] != game_build:
            raise Refusal(f"{game_build} is not the newest build in {REGISTER}: add it there first "
                          "with its patch name, changelist and the rows that changed")
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


BUMP_FILES = ("sdk/VERSION", "sdk/DEVELOPMENT_VERSION", "sdk/CANDIDATE_TAG", "include/sc_game_pack.h",
              "CHANGELOG.md", "site/public-inputs.json", "site/scripts/gen.py")


def transactional_bump(version, game_pack, game_build):
    """bump() then rewrite every region; any failure restores every file it could have touched."""
    notes = Path(f"docs/releases/sdk-v{version}.md")
    paths = [Path(p) for p in BUMP_FILES] + doc_files() + [notes]
    saved = {p: (ROOT / p).read_bytes() if (ROOT / p).is_file() else None for p in paths}
    try:
        bump(version, game_pack, game_build)
        return run(write=True)
    except BaseException:
        for p, data in saved.items():
            if data is None:
                (ROOT / p).unlink(missing_ok=True)
            else:
                (ROOT / p).write_bytes(data)
        raise


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
            values, stale, regions = transactional_bump(args.bump, args.game_pack, args.game_build)
        else:
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
