#!/usr/bin/env python3
"""Packages the sco SDK. What goes in a zip is decided by sdk/components.json and nothing else.

    sdk/package.py [--profile core|core-source|game-sc|legacy] [--out DIR]   # default: legacy, sdk/out
    sdk/package.py --check                                        # validate the manifest only
    sdk/package.py --list-headers core,game-sc                    # public headers of those profiles

Profiles (sdk/components.json "profiles"):
  core     sco-sdk-core-<sdk/VERSION>.zip: the game-neutral plugin SDK. Its version comes from
           sdk/VERSION only; it never reads a game-pack header.
  game-sc  sco-sdk-game-sc-<SCO_GAME_PACK_VERSION>.zip: the optional Star Citizen extension (game
           service tables and events, DataCore, their wrappers, game examples and docs). Unpacked
           over the core SDK of the version its VERSIONS.txt names.
  core-source sco-sdk-core-source-<sdk/VERSION>.zip: the kernel source-development kit,
           keeping repository paths, with synthetic tests and no game implementation.
  legacy   sco-sdk-<sdk/VERSION>.zip: core and game-sc in one zip, as sdk-v1.1.0 shipped. Not
           game-neutral; kept until the consumers move to core + game-sc.

A zip holds one folder with the selected files, VERSIONS.txt and SHA256SUMS. A profile that is
unpacked over another one (game-sc, over core) names its own copies VERSIONS.txt.<profile> and
SHA256SUMS.<profile>, so the overlay keeps both and overwrites nothing. The same sources always
give the same bytes: entries are sorted and carry a fixed timestamp and fixed modes.

The manifest is checked before anything is written, and any doubt stops the build:
  - every file under the folders in "covers" is a component or an "excluded" entry, and every
    component's source is a regular file in the repository (no folders, no symlinks);
  - every component has an id, source, destination, profiles, origin and license, and no key
    beyond the known ones; ids and destinations are unique; destinations are safe relative paths;
  - a profile names only known profiles in "includes" / "requires", and they form no cycle;
  - within any profile's selection (its own, included and required profiles) each file is
    written once;
  - the core profile contains no game-sc component.

Run by .github/workflows/sdk.yml; CI then builds the examples from the unpacked zip alone.
Tests: python3 sdk/test_package.py
"""
import argparse
import fnmatch
import hashlib
import json
import os
import re
import subprocess
import sys
import zipfile

SDK = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(SDK)
MANIFEST = os.path.join(SDK, "components.json")
REPO_URL = "https://github.com/scubamount/sco-core/blob/main/"
FIXED_TIME = (2026, 1, 1, 0, 0, 0)
ORIGINS = {"project-authored", "third-party", "generated", "interoperability-derived"}
REQUIRED = ("id", "source", "destination", "profiles", "origin", "license")
COMPONENT_KEYS = REQUIRED + ("public_header", "group", "note", "upstream")


class ManifestError(Exception):
    pass


def tracked_files(root):
    """Files git tracks under root, or every file when root isn't a git checkout (an unpacked zip)."""
    try:
        out = subprocess.run(["git", "-C", root, "ls-files", "-z"], capture_output=True, check=True)
        return [p for p in out.stdout.decode("utf-8").split("\0") if p]
    except (OSError, subprocess.CalledProcessError):
        files = []
        for dirpath, dirnames, filenames in os.walk(root):
            dirnames[:] = [d for d in dirnames if d != ".git"]
            files += [os.path.relpath(os.path.join(dirpath, n), root).replace(os.sep, "/") for n in filenames]
        return files


def safe_rel(path):
    return (isinstance(path, str) and path and not path.startswith("/") and "\\" not in path and ":" not in path
            and all(part not in ("", ".", "..") for part in path.split("/")))


def profile_refs(m, name):
    """A profile's 'includes' plus 'requires', as profile names."""
    p = m["profiles"][name]
    refs = []
    for key in ("includes", "requires"):
        r = p.get(key, [])
        refs += [r] if isinstance(r, str) else list(r)
    return refs


def closure(m, name):
    """Every profile a profile's zip touches: itself, what it includes and what it requires,
    transitively. Refuses a cycle with the path that closes it."""
    seen, path = set(), []

    def walk(n):
        if n in path:
            cycle = path[path.index(n):] + [n]
            raise ManifestError(f"profiles form a cycle: {' -> '.join(cycle)}")
        if n in seen:
            return
        path.append(n)
        for r in profile_refs(m, n):
            if r not in m["profiles"]:
                raise ManifestError(f"profile {n}: names unknown profile {r}")
            walk(r)
        path.pop()
        seen.add(n)

    walk(name)
    return seen


def load_manifest(path=MANIFEST, root=ROOT):
    """The parsed manifest after every check. Raises ManifestError."""
    try:
        with open(path, encoding="utf-8") as f:
            m = json.load(f)
    except (OSError, ValueError) as e:
        raise ManifestError(f"{os.path.basename(path)}: can't read it ({e})")
    if not isinstance(m, dict) or m.get("schema_version") != 1:
        raise ManifestError("schema_version must be 1")
    profiles = m.get("profiles")
    if not isinstance(profiles, dict) or not {"core", "game-sc"} <= set(profiles):
        raise ManifestError("profiles must declare core and game-sc")
    for name, p in profiles.items():
        for key in ("includes", "requires"):
            refs = p.get(key, [])
            refs = [refs] if isinstance(refs, str) else refs
            bad = [r for r in refs if r not in profiles or r == name]
            if bad:
                raise ManifestError(f"profile {name}: {key} names unknown profile(s) {', '.join(bad)}")
        if p.get("layout", "sdk") not in ("sdk", "source"):
            raise ManifestError(f"profile {name}: unknown layout")
        if not p.get("zip") or not p.get("version_from"):
            raise ManifestError(f"profile {name}: needs zip and version_from")
    comps = m.get("components")
    if not isinstance(comps, list) or not comps:
        raise ManifestError("no components")
    ids, dsts, srcs = set(), {}, set()
    reserved = {n for prof in profiles for n in metadata_names(m, prof)}   # written by package.py
    for i, c in enumerate(comps):
        if not isinstance(c, dict):
            raise ManifestError(f"component {i} is not an object")
        missing = [k for k in REQUIRED if not c.get(k)]
        if missing:
            raise ManifestError(f"component {c.get('id', i)}: missing {', '.join(missing)}")
        unknown = [k for k in c if k not in COMPONENT_KEYS]
        if unknown:
            raise ManifestError(f"component {c['id']}: unknown key(s) {', '.join(sorted(unknown))}")
        cid = c["id"]
        if cid in ids:
            raise ManifestError(f"component {cid}: id used twice")
        ids.add(cid)
        if c["origin"] not in ORIGINS:
            raise ManifestError(f"component {cid}: origin '{c['origin']}' is not one of {', '.join(sorted(ORIGINS))}")
        if not isinstance(c["profiles"], list) or not set(c["profiles"]) <= set(profiles):
            raise ManifestError(f"component {cid}: profiles must be a list of declared profiles")
        if "core" in c["profiles"] and "game-sc" in c["profiles"] and c["source"] != "LICENSE":
            raise ManifestError(f"component {cid}: in both core and game-sc; core files reach game-sc through 'requires'")
        for key in ("source", "destination"):
            if not safe_rel(c[key]):
                raise ManifestError(f"component {cid}: {key} '{c[key]}' is not a safe relative path")
        if c["destination"] in reserved:
            raise ManifestError(f"component {cid}: destination {c['destination']} is written by package.py")
        full = os.path.join(root, c["source"])
        if os.path.islink(full) or not os.path.isfile(full):
            raise ManifestError(f"component {cid}: source {c['source']} is not a regular file")
        if c["source"] in srcs:
            raise ManifestError(f"component {cid}: source {c['source']} listed twice")
        srcs.add(c["source"])
        for prof in c["profiles"]:
            dst = destination(m, prof, c)
            key = (prof, dst)
            if key in dsts:
                raise ManifestError(f"components {dsts[key]} and {cid}: same destination {dst} in {prof}")
            dsts[key] = cid
    # Coverage: nothing under a covered folder is shipped or skipped by accident.
    excluded = m.get("excluded", [])
    covers = m.get("covers", [])
    def is_excluded(p):
        return any(p == e["path"] or (e["path"].endswith("/") and p.startswith(e["path"])) for e in excluded)
    for e in excluded:
        if not e.get("path") or not e.get("why"):
            raise ManifestError("an excluded entry needs path and why")
    # Closure: within one profile's selection (itself, what it includes and what it requires) each
    # destination is written once. A core file and a game-sc file with one destination would
    # overwrite each other where the extension unpacks over the core.
    closures = {name: closure(m, name) for name in profiles}   # also refuses a cycle, once per profile
    for name, reached in closures.items():
        written = {}
        for c in comps:
            if set(c["profiles"]) & reached:
                dst = destination(m, name, c)
                other = written.get(dst)
                if other is not None and other != c["id"]:
                    raise ManifestError(f"profile {name}: {other} and {c['id']} both write {dst}")
                written[dst] = c["id"]
    unclassified = [p for p in tracked_files(root)
                    if any(p == c or p.startswith(c + "/") for c in covers) and p not in srcs and not is_excluded(p)]
    if unclassified:
        raise ManifestError("not classified in sdk/components.json: " + ", ".join(sorted(unclassified)))
    for name in profiles:
        select(m, name)
    return m


def destination(m, profile, component):
    """Source-development archives retain repository layout; plugin kits retain SDK layout."""
    return component["source"] if m["profiles"][profile].get("layout") == "source" else component["destination"]


def select(m, profile):
    """The components a profile's zip holds: its own, plus those of the profiles it 'includes'."""
    p = m["profiles"].get(profile)
    if p is None:
        raise ManifestError(f"unknown profile {profile}")
    wanted = {profile, *p.get("includes", [])}
    chosen = [c for c in m["components"] if wanted & set(c["profiles"])]
    if profile == "core":
        game = [c["id"] for c in chosen if "game-sc" in c["profiles"] and c["source"] != "LICENSE"]
        if game:
            raise ManifestError(f"core would hold game-sc components: {', '.join(game)}")
    if profile == "core-source":
        forbidden = ("src/game/", "src/datacore/", "include/sco/game/", "include/sco/datacore",
                     "plugins/lua/sco_lua_game.", "third_party/tomlplusplus/")
        game = [c["source"] for c in chosen if c["source"].startswith(forbidden)]
        if game:
            raise ManifestError("core-source would hold game implementation: " + ", ".join(game))
    seen = {}
    for c in chosen:
        dst = destination(m, profile, c)
        if dst in seen and seen[dst] != c["id"]:
            raise ManifestError(f"{profile}: {seen[dst]} and {c['id']} both write {dst}")
        seen[dst] = c["id"]
    return chosen


def public_headers(m, profiles):
    """Destinations of the public headers (public_header: true) in any of the given profiles."""
    return sorted({c["destination"] for c in m["components"] if c.get("public_header") and set(profiles) & set(c["profiles"])})


def define(text, name, pattern, where):
    mt = re.search(rf'#define {name} "({pattern})"', text)
    if not mt:
        raise ManifestError(f"{where}: no {name}")
    return mt.group(1)


def read(rel):
    with open(os.path.join(ROOT, rel), encoding="utf-8") as f:
        return f.read()


def sdk_version():
    v = read("sdk/VERSION").strip()
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(-[a-z0-9.]+)?", v):
        raise ManifestError(f"sdk/VERSION: '{v}' is not a version")
    return v


def abi_version():
    api = read("include/sco_api.h")
    major = re.search(r"#define SCO_API_MAJOR ([0-9]+)", api)
    minor = re.search(r"#define SCO_API_MINOR ([0-9]+)", api)
    if not (major and minor):
        raise ManifestError("include/sco_api.h: no SCO_API_MAJOR / SCO_API_MINOR")
    return f"{major.group(1)}.{minor.group(1)}"


def game_pack():
    """(version, verified build, PE timestamp, how) from include/sc_game_pack.h. game-sc and legacy only."""
    pack = read("include/sc_game_pack.h")
    w = "include/sc_game_pack.h"
    return (define(pack, "SCO_GAME_PACK_VERSION", r"[0-9]+\.[0-9]+\.[0-9]+", w),
            define(pack, "SCO_GAME_PACK_VERIFIED_BUILD", r"[0-9]+(?:\.[0-9]+)+", w),
            define(pack, "SCO_GAME_PACK_VERIFIED_PE_TIMESTAMP", r"0x[0-9a-f]+", w),
            define(pack, "SCO_GAME_PACK_VERIFICATION", r"[^\"]+", w))


def versions_txt(profile, version):
    """VERSIONS.txt: every line comes from a header the zip ships (or, for game-sc, the core it needs)."""
    abi = abi_version()
    if profile == "core-source":
        return (f"sco SDK core source {version}\n\nplugin ABI (sco_api.h): {abi}\n"
                "Configure with -DSCO_GAME_SC=OFF. Game implementation is not included.\n"
                "The frozen release header is hash-checked by tools/abi_snapshot.py.\n")
    if profile == "core":
        return (f"sco SDK core {version}\n\nkernel (sdk/VERSION, tag sdk-v{version}): {version}\n"
                f"plugin ABI (sco_api.h): {abi}\n\nThe Star Citizen game services are a separate extension, "
                f"sco-sdk-game-sc-<version>.zip.\n")
    gp, build, stamp, how = game_pack()
    if profile == "game-sc":
        return (f"sco SDK Star Citizen extension {gp}\n\ngame pack (SCO_GAME_PACK_VERSION, Star Citizen): {gp}\n"
                f"needs: sco SDK core {sdk_version()} (plugin ABI {abi}); unpack this over it\n\n"
                f"verified game builds:\n  {build} (StarCitizen.exe PE timestamp {stamp}): {how}\n")
    return (f"sco SDK {version}\n\nkernel (sdk/VERSION, tag sdk-v{version}): {version}\n"
            f"plugin ABI (sco_api.h): {abi}\ngame pack (SCO_GAME_PACK_VERSION, Star Citizen): {gp}\n\n"
            f"verified game builds:\n  {build} (StarCitizen.exe PE timestamp {stamp}): {how}\n")


LINK = re.compile(r"\]\(([^)#\s]+)((?:#[^)]*)?)\)")


def fix_links(src, dst, data, zip_paths, dirs):
    """Markdown links are written to work in the repository. In the zip, a link to a file (or
    folder) this zip ships points at its zip path; a link to anything else points at GitHub."""
    if not dst.endswith(".md"):
        return data
    src_dir, dst_dir = os.path.dirname(src), os.path.dirname(dst)

    def repl(mt):
        target, anchor = mt.group(1), mt.group(2)
        if re.match(r"[a-z]+:", target):
            return mt.group(0)
        repo_path = os.path.normpath(os.path.join(src_dir, target)).replace(os.sep, "/")
        zip_path = zip_paths.get(repo_path) or dirs.get(repo_path)
        if zip_path is None:
            return f"]({REPO_URL}{repo_path}{anchor})"
        return f"]({os.path.relpath(zip_path, dst_dir or '.').replace(os.sep, '/')}{anchor})"

    return LINK.sub(repl, data.decode("utf-8")).encode("utf-8")


def metadata_names(m, profile):
    """The two files package.py writes itself. A profile that unpacks over another one (game-sc,
    over core) keeps its own copies under distinct names, so the overlay overwrites nothing."""
    suffix = f".{profile}" if m["profiles"][profile].get("requires") else ""
    return f"VERSIONS.txt{suffix}", f"SHA256SUMS{suffix}"


def build(m, profile, out):
    chosen = select(m, profile)
    p = m["profiles"][profile]
    version = sdk_version() if p["version_from"] == "sdk/VERSION" else game_pack()[0]
    top = p["zip"].format(version=version, game_pack_version=version)
    zip_paths = {c["source"]: destination(m, profile, c) for c in chosen}
    dirs = {}
    for src, dst in zip_paths.items():   # folders: the source folder maps to the destination folder
        s, d = os.path.dirname(src), os.path.dirname(dst)
        while s and d and os.path.basename(s) == os.path.basename(d):
            dirs.setdefault(s, d)
            s, d = os.path.dirname(s), os.path.dirname(d)
    entries = {}
    for c in chosen:
        full = os.path.join(ROOT, c["source"])
        with open(full, "rb") as f:
            dst = destination(m, profile, c)
            entries[dst] = (fix_links(c["source"], dst, f.read(), zip_paths, dirs),
                                         os.access(full, os.X_OK))
    versions_name, sums_name = metadata_names(m, profile)
    entries[versions_name] = (versions_txt(profile, version).encode("ascii"), False)
    sums = "".join(f"{hashlib.sha256(data).hexdigest()}  {name}\n" for name, (data, _) in sorted(entries.items()))
    entries[sums_name] = (sums.encode("ascii"), False)

    os.makedirs(out, exist_ok=True)
    zip_path = os.path.join(out, f"{top}.zip")
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for name in sorted(entries):
            data, executable = entries[name]
            info = zipfile.ZipInfo(f"{top}/{name}", FIXED_TIME)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.create_system = 3  # unix, so the mode bits below are honored
            info.external_attr = (0o100755 if executable else 0o100644) << 16
            z.writestr(info, data)
    with open(zip_path, "rb") as f:
        digest = hashlib.sha256(f.read()).hexdigest()
    print(f"{zip_path}: {len(entries)} files, sha256 {digest}")
    return zip_path


def main():
    ap = argparse.ArgumentParser(description="Package the sco SDK from sdk/components.json.")
    ap.add_argument("--profile", default="legacy")
    ap.add_argument("--out", default=os.path.join(SDK, "out"))
    ap.add_argument("--check", action="store_true", help="validate the manifest and every profile; write nothing")
    ap.add_argument("--list-headers", metavar="PROFILES", help="comma list; print their public headers")
    args = ap.parse_args()
    try:
        m = load_manifest()
        if args.list_headers:
            print("\n".join(public_headers(m, args.list_headers.split(","))))
            return
        if args.check:
            for prof in m["profiles"]:
                print(f"{prof}: {len(select(m, prof))} components")
            print("sdk/components.json: ok")
            return
        build(m, args.profile, args.out)
    except ManifestError as e:
        sys.exit(f"package.py: {e}")


if __name__ == "__main__":
    main()
