#!/usr/bin/env python3
"""IP-compliance checks for sco-core: evidence collection, not a legal opinion (IP_POLICY.md).

    tools/ip_audit.py repo [--report FILE]
        Gate 1: every tracked file matches an entry of docs/legal/provenance.json, with an origin,
                a licence and a status; none is 'unresolved'.
        Gate 2: no tracked file is a binary or carries a refused extension (.exe .dll .so .dylib
                .zip .7z .p4k .pak .dcb .dmp ...) unless allowlisted by exact path and sha256.
        Gate 4: every vendored third-party file matches its pinned sha256; each third-party
                component's licence text is present; sdk/components.json and provenance.json
                agree on the licence of every SDK file.
    tools/ip_audit.py zip <archive.zip> --profile core|game-sc|legacy [--sbom FILE] [--report FILE]
        The archive is exactly what sdk/components.json selects for the profile (no missing, no
        extra file, byte-identical to the sources), SHA256SUMS matches every entry, entries are
        safe paths without symlinks, reserved Windows names, case-fold twins or duplicates, and
        within the entry-count, per-entry, total-size and compression limits, no entry is
        unresolved or refused (Gate 2), the licence and notices ship, and for core: no game-sc
        component and no file that names one. --sbom writes an SPDX 2.3 JSON SBOM of the archive
        bound to the commit and the archive's hash.
    tools/ip_audit.py reuse-toml
        Writes REUSE.toml from docs/legal/provenance.json (the repo audit fails when it is stale);
        `reuse lint` then checks the REUSE specification.
    tools/ip_audit.py release --approval FILE --tag TAG [--artifacts DIR]
        Gate G: the JSON record (docs/legal/release-decisions/<tag>.json) validates against
        docs/legal/release-approval.schema.json and the semantics hold too: decision 'approve',
        distribution.status 'approved-for-publication', unexpired (approved_at < expires_at, now <
        expires_at, every exception unexpired), release_tag == TAG, source.commit this commit or an
        ancestor with only release decisions changed since, profiles covering every artifact role
        (sdk-core -> core, sdk-game-sc -> game-sc, sdk-legacy -> legacy), unique artifact names, and
        --artifacts DIR exactly the listed set (bytes and sha256). The record is not yet signed:
        key_fingerprint is recorded, no signature is verified. CI never writes one.

Exit 0 = every check passed; 1 = a check failed (each failure is printed with what to do);
2 = the audit couldn't run (missing input). A check that can't run is a failure, never a pass.
Tests: python3 tools/test_ip_audit.py
"""
import argparse
import datetime
import fnmatch
import hashlib
import json
import os
import re
import subprocess
import sys
import zipfile

sys.dont_write_bytecode = True

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROVENANCE = "docs/legal/provenance.json"
COMPONENTS = "sdk/components.json"
STATUSES = {"reviewed", "needs-review", "unresolved"}
ORIGINS = {"project-authored", "third-party", "interoperability-derived", "generated"}
REFUSED_EXT = {".exe", ".dll", ".so", ".dylib", ".lib", ".a", ".o", ".obj", ".pdb", ".zip", ".7z", ".rar",
               ".tar", ".gz", ".xz", ".bz2", ".p4k", ".pak", ".dcb", ".dmp", ".mdmp", ".bin", ".img", ".iso",
               ".socpak", ".dds", ".png", ".jpg", ".jpeg", ".ico", ".wav", ".ogg", ".mp4"}
# Magic numbers of executables and archives, whatever the file is called.
MAGIC = ((b"MZ", "a Windows executable (MZ)"), (b"\x7fELF", "an ELF executable"), (b"PK\x03\x04", "a zip archive"),
         (b"\xcf\xfa\xed\xfe", "a Mach-O executable"), (b"\xca\xfe\xba\xbe", "a Mach-O universal binary"),
         (b"7z\xbc\xaf\x27\x1c", "a 7-Zip archive"), (b"\x1f\x8b", "a gzip stream"), (b"Rar!", "a RAR archive"),
         (b"MDMP", "a minidump"), (b"\x89PNG", "a PNG image"))
GAME_REF = re.compile(r'#\s*include\s*[<"](sc_(actors|vehicles|entities|world|creative|spawn|spatial|game_events|game_pack)\.h'
                      r'|sco_datacore\.h|scosdk/(datacore\.hpp|game/[^>"]+))[>"]|\bSco\.Sdk\.Game\b|\bScoDatacoreV1\b|\bGameAbi\.')
APPROVAL_SCHEMA = "docs/legal/release-approval.schema.json"
# Artifact role -> the profile whose zip it must be listed under; profiles must cover every role.
ROLE_PROFILE = {"sdk-core": "core", "sdk-game-sc": "game-sc", "sdk-legacy": "legacy"}
# RFC 3339 date-time with a mandatory zone designator (Z or ±HH:MM). 'T' may be lowercase; seconds
# may carry a fraction. A bare local time is refused: an expiry must be unambiguous.
RFC3339 = re.compile(r"^(\d{4})-(\d{2})-(\d{2})[Tt](\d{2}):(\d{2}):(\d{2})(?:\.\d+)?(Z|z|[+-]\d{2}:\d{2})$")
# The JSON Schema keywords tools/ip_audit.py implements for docs/legal/release-approval.schema.json
# (a test walks the shipped schema and fails on any keyword not in this set).
SCHEMA_KEYWORDS = {"$schema", "$id", "title", "type", "properties", "required", "additionalProperties",
                   "items", "const", "enum", "pattern", "format", "minLength", "maxLength",
                   "minimum", "maximum", "minItems", "maxItems", "uniqueItems"}
# Archive limits, four times what the legacy zip actually holds (sdk/package.py --profile legacy:
# 116 entries, 1,334,638 bytes uncompressed in total, its largest entry 426,845 bytes and its best
# compression 8.2:1). A release that outgrows them raises these constants on purpose.
MAX_ENTRIES = 464            # 4 x 116
MAX_ENTRY_BYTES = 1_707_380  # 4 x 426,845
MAX_TOTAL_BYTES = 5_338_552  # 4 x 1,334,638
MAX_RATIO = 33               # 4 x 8.2:1
# Device names Windows refuses in any path segment, with or without an extension.
RESERVED_NAMES = {"CON", "PRN", "AUX", "NUL"} | {f"COM{i}" for i in range(1, 10)} | {f"LPT{i}" for i in range(1, 10)}


def reserved_problem(rel):
    """A Windows device name in a path segment ('CON', 'nul.log', 'com1'), or None."""
    for seg in rel.split("/"):
        if seg.split(".", 1)[0].upper() in RESERVED_NAMES:
            return f"'{seg}' is a reserved Windows device name"
    return None


def trailing_problem(rel):
    """A path segment ending in a dot or a space, which Windows silently drops, or None."""
    for seg in rel.split("/"):
        if seg != seg.rstrip(". "):
            return f"'{seg}' ends in a dot or a space, which Windows drops"
    return None


def structure_problems(infos, top, safe_rel):
    """(problems, readable): every structural refusal for an archive's entries, and the entries
    (relative name -> info) that passed and may be read.

    Refused: an unsafe path ('..', absolute, a backslash, a drive letter or an NTFS stream), a
    Windows reserved or trailing-dot name, a name listed twice, two names equal except for case,
    a symlink, and the entry-count, per-entry, total-size and compression limits. The limits are
    checked on the declared sizes first, so a zip bomb is refused before anything is unpacked;
    when the count or the total is over its limit nothing is read at all.
    """
    problems = []
    readable = {}
    if len(infos) > MAX_ENTRIES:
        problems.append(f"{len(infos)} entries, over the {MAX_ENTRIES}-entry limit")
        return problems, readable
    seen, folded, total = set(), {}, 0
    for i in infos:
        n = i.filename
        if n.endswith("/"):
            continue
        rel = n[len(top) + 1:] if n.startswith(top + "/") else n
        # The raw name is checked too: an absolute path, a backslash or a colon (a drive letter or
        # an NTFS stream) must be refused whatever the top folder turns out to be.
        if not safe_rel(n) or not safe_rel(rel):
            problems.append(f"{n}: unsafe path")
            continue
        why = reserved_problem(rel) or trailing_problem(rel)
        if why:
            problems.append(f"{n}: {why}")
            continue
        if n in seen:
            problems.append(f"{n}: listed twice")
            continue
        fold = n.casefold()
        if fold in folded:
            problems.append(f"{n}: the same name as {folded[fold]} except for case")
            continue
        if (i.external_attr >> 16) & 0o170000 == 0o120000:
            problems.append(f"{n}: a symlink")
            continue
        if i.file_size > MAX_ENTRY_BYTES:
            problems.append(f"{n}: {i.file_size} bytes uncompressed, over the {MAX_ENTRY_BYTES}-byte limit")
            continue
        if i.compress_size and i.file_size / i.compress_size > MAX_RATIO:
            problems.append(f"{n}: {i.file_size} bytes compress to {i.compress_size} "
                            f"({i.file_size // i.compress_size}:1), over the {MAX_RATIO}:1 limit")
            continue
        seen.add(n)
        folded[fold] = n
        readable[rel] = i
        total += i.file_size
    if total > MAX_TOTAL_BYTES:
        problems.append(f"{total} bytes uncompressed in total, over the {MAX_TOTAL_BYTES}-byte limit")
        return problems, {}
    return problems, readable


class Report:
    def __init__(self, scope):
        self.scope, self.failures, self.checks, self.notes = scope, [], [], []

    def check(self, name, ok_items, fails):
        self.checks.append({"check": name, "checked": ok_items, "failed": len(fails)})
        for f in fails:
            self.failures.append(f"{name}: {f}")

    def write(self, path):
        if path:
            with open(path, "w", encoding="utf-8") as f:
                json.dump({"scope": self.scope, "commit": git_head(), "passed": not self.failures,
                           "checks": self.checks, "failures": self.failures, "notes": self.notes}, f, indent=1)
                f.write("\n")

    def finish(self):
        for c in self.checks:
            print(f"{'ok  ' if not c['failed'] else 'FAIL'} {c['check']}: {c['checked']} checked, {c['failed']} failed")
        for f in self.failures:
            print(f"  FAIL {f}")
        for n in self.notes:
            print(f"  note {n}")
        print(f"ip_audit {self.scope}: {'OK' if not self.failures else 'FAILED'}")
        return 1 if self.failures else 0


class AuditError(Exception):
    pass


def git_head(root=ROOT):
    try:
        return subprocess.run(["git", "-C", root, "rev-parse", "HEAD"], capture_output=True, text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return None


def tracked(root):
    try:
        out = subprocess.run(["git", "-C", root, "ls-files", "-z"], capture_output=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError) as e:
        raise AuditError(f"git ls-files failed in {root}: {e}")
    return sorted(p for p in out.decode("utf-8").split("\0") if p)


def load_json(root, rel):
    try:
        with open(os.path.join(root, rel), encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError) as e:
        raise AuditError(f"{rel}: can't read it ({e})")


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def provenance_entries(prov):
    if not isinstance(prov, dict) or prov.get("schema_version") != 1 or not isinstance(prov.get("entries"), list):
        raise AuditError(f"{PROVENANCE}: schema_version must be 1 with a list of entries")
    for i, e in enumerate(prov["entries"]):
        missing = [k for k in ("path", "origin", "license", "copyright", "status") if not e.get(k)]
        if missing:
            raise AuditError(f"{PROVENANCE}: entry {i} ({e.get('path')}) lacks {', '.join(missing)}")
        if e["status"] not in STATUSES:
            raise AuditError(f"{PROVENANCE}: entry {e['path']}: unknown status {e['status']!r}")
        if e["origin"] not in ORIGINS:
            raise AuditError(f"{PROVENANCE}: entry {e['path']}: unknown origin {e['origin']!r}")
    return prov["entries"]


def match(entries, path):
    """The first entry whose pattern matches: 'dir/**' = under dir; else fnmatch, where a pattern
    without '/' matches only top-level files."""
    for e in entries:
        pat = e["path"]
        if pat.endswith("/**"):
            if path.startswith(pat[:-2]):
                return e
        elif fnmatch.fnmatchcase(path, pat) and ("/" in pat or "/" not in path):
            return e
    return None


def artifact_problem(name, data):
    """Why a file would be a refused artifact, or None."""
    ext = os.path.splitext(name.lower())[1]
    if ext in REFUSED_EXT:
        return f"refused extension {ext}"
    for magic, what in MAGIC:
        if data.startswith(magic):
            return f"content is {what}"
    if b"\0" in data[:8192]:
        return "binary content (NUL bytes)"
    return None


def check_artifacts(report, files, read, allow):
    allowed = {a["path"]: a["sha256"] for a in allow}
    fails = []
    for f in files:
        data = read(f)
        why = artifact_problem(f, data)
        if why is None:
            continue
        if f in allowed:
            if allowed[f] == sha256(data):
                continue
            why += f"; allowlisted, but its sha256 is {sha256(data)}, not {allowed[f]}"
        fails.append(f"{f}: {why}. Remove it, or allowlist exact path + sha256 in {PROVENANCE} with a reason")
    report.check("artifacts (Gate 2)", len(files), fails)


def reuse_toml(entries):
    """REUSE.toml for the provenance entries. provenance.json is first-match-wins; in REUSE.toml
    the last matching annotation wins, so the order is reversed. 'override': the record, not a
    file's own header, is what REUSE reports (vendored files are never edited to add one)."""
    def q(v):
        return json.dumps(v, ensure_ascii=False)
    out = ["# Generated from docs/legal/provenance.json by tools/ip_audit.py reuse-toml; don't edit.",
           "# tools/ip_audit.py repo fails when this file is out of date. Checked by `reuse lint` in CI.",
           "version = 1", ""]
    for e in reversed(entries):
        out += ["[[annotations]]", f"path = {q(e['path'])}", 'precedence = "override"',
                f"SPDX-FileCopyrightText = {q(e['copyright'])}", f"SPDX-License-Identifier = {q(e['license'])}", ""]
    return "\n".join(out)


def audit_repo(root=ROOT, report_path=None):
    r = Report("repository")
    files = tracked(root)
    prov = load_json(root, PROVENANCE)
    entries = provenance_entries(prov)
    def read(f):
        with open(os.path.join(root, f), "rb") as fh:
            return fh.read()

    # Gate 1: coverage and status.
    fails, need = [], 0
    for f in files:
        e = match(entries, f)
        if e is None:
            fails.append(f"{f}: no entry in {PROVENANCE}; add its origin, licence and status")
        elif e["status"] == "unresolved":
            fails.append(f"{f}: provenance unresolved ({e.get('issue') or e.get('note') or 'no issue named'}); resolve, rewrite or remove it")
        elif e["status"] == "needs-review":
            need += 1
    r.check("provenance coverage (Gate 1)", len(files), fails)
    if need:
        r.notes.append(f"{need} files are needs-review (provenance recorded, a question open); a release that ships one needs its decision in docs/legal/release-decisions/")

    # Gate 2.
    check_artifacts(r, files, read, prov.get("artifact_allowlist", {}).get("files", []))

    # Gate 4: vendored files pinned, licence texts present, manifest and provenance agree.
    pinned = prov.get("pinned", {}).get("files", {})
    fails = []
    third = [f for f in files if (match(entries, f) or {}).get("origin") == "third-party" and f != "LICENSE"]
    for f in third:
        if f not in pinned:
            fails.append(f"{f}: third-party file with no pinned sha256 in {PROVENANCE}")
        elif pinned[f] != sha256(read(f)):
            fails.append(f"{f}: differs from its pinned sha256; an upstream update or local change updates the pin, the inventory and the notices together")
    for f in pinned:
        if f not in files:
            fails.append(f"{f}: pinned in {PROVENANCE} but not in the repository")
    for lic in ("LICENSE", "THIRD_PARTY_NOTICES.md", "plugins/lua/third_party/lua/LICENSE",
                "third_party/tomlplusplus/LICENSE", "sdk/third_party/imgui/LICENSE.txt"):
        if lic not in files:
            fails.append(f"{lic}: missing licence or notice file")
    notices = read("THIRD_PARTY_NOTICES.md").decode("utf-8") if "THIRD_PARTY_NOTICES.md" in files else ""
    for name in ("Lua", "SQLite", "toml++", "Dear ImGui"):
        if name not in notices:
            fails.append(f"THIRD_PARTY_NOTICES.md: no section for {name}")
    comps = load_json(root, COMPONENTS)
    for c in comps.get("components", []):
        e = match(entries, c["source"])
        if e is None:
            continue   # coverage already failed
        if c["license"] != e["license"] and c["source"] != "LICENSE":
            fails.append(f"{c['source']}: {COMPONENTS} says {c['license']}, {PROVENANCE} says {e['license']}")
        if c["origin"] != e["origin"] and c["source"] != "LICENSE":
            fails.append(f"{c['source']}: {COMPONENTS} origin {c['origin']}, {PROVENANCE} origin {e['origin']}")
    want = reuse_toml(entries)
    try:
        with open(os.path.join(root, "REUSE.toml"), encoding="utf-8") as f:
            have = f.read()
    except OSError:
        have = None
    if have != want:
        fails.append("REUSE.toml: missing or out of date with docs/legal/provenance.json; run tools/ip_audit.py reuse-toml")
    r.check("licences and vendored pins (Gate 4)", len(third) + len(comps.get("components", [])), fails)
    r.write(report_path)
    return r


def load_package(root):
    import importlib.util
    spec = importlib.util.spec_from_file_location("sco_package", os.path.join(root, "sdk", "package.py"))
    if spec is None or spec.loader is None:
        raise AuditError("sdk/package.py: can't load it")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def audit_zip(archive, profile, root=ROOT, sbom_path=None, report_path=None):
    r = Report(f"zip {os.path.basename(archive)} ({profile})")
    pkg = load_package(root)
    try:
        m = pkg.load_manifest(os.path.join(root, COMPONENTS), root)
        chosen = pkg.select(m, profile)
    except pkg.ManifestError as e:
        raise AuditError(f"{COMPONENTS}: {e}")
    prov = load_json(root, PROVENANCE)
    entries = provenance_entries(prov)
    try:
        z = zipfile.ZipFile(archive)
    except (OSError, zipfile.BadZipFile) as e:
        raise AuditError(f"{archive}: not a readable zip ({e})")
    with z:
        infos = z.infolist()
        tops = {i.filename.split("/", 1)[0] for i in infos}
        fails = []
        if len(tops) != 1:
            fails.append(f"the archive must hold one top folder, holds {sorted(tops)}")
        top = sorted(tops)[0] if tops else ""
        problems, readable = structure_problems(infos, top, pkg.safe_rel)
        fails += problems
        names, content = [], {}
        for rel, i in readable.items():
            # Bounded read: an entry that unpacks to more than its header claims is stopped at the
            # limit, so a lying header can't decompress a bomb past the check.
            with z.open(i) as f:
                data = f.read(MAX_ENTRY_BYTES + 1)
            if len(data) > MAX_ENTRY_BYTES:
                fails.append(f"{i.filename}: unpacks past the {MAX_ENTRY_BYTES}-byte limit")
                continue
            if len(data) != i.file_size:
                fails.append(f"{i.filename}: declares {i.file_size} bytes, holds {len(data)}")
                continue
            names.append(rel)
            content[rel] = data
        r.check("archive structure", len(infos), fails)

        # Manifest equality: exactly the selected components, byte-identical (links are rewritten
        # in .md files, so those are compared after package.py's own rewrite).
        fails = []
        expected = {c["destination"]: c for c in chosen}
        generated = set(pkg.metadata_names(m, profile))
        for rel in names:
            if rel not in expected and rel not in generated:
                fails.append(f"{rel}: in the archive but not selected by {COMPONENTS} for {profile}")
        zip_paths = {c["source"]: c["destination"] for c in chosen}
        dirs = {}
        for src, dst in zip_paths.items():
            s, d = os.path.dirname(src), os.path.dirname(dst)
            while s and d and os.path.basename(s) == os.path.basename(d):
                dirs.setdefault(s, d)
                s, d = os.path.dirname(s), os.path.dirname(d)
        for dst, c in expected.items():
            if dst not in content:
                fails.append(f"{dst}: selected for {profile} but missing from the archive")
                continue
            with open(os.path.join(root, c["source"]), "rb") as f:
                want = pkg.fix_links(c["source"], dst, f.read(), zip_paths, dirs)
            if want != content[dst]:
                fails.append(f"{dst}: differs from {c['source']} at {git_head(root) or 'the working tree'}")
        for g in generated:
            if g not in content:
                fails.append(f"{g}: missing")
        r.check("archive = manifest selection", len(expected), fails)

        # SHA256SUMS: every entry once, every hash right.
        fails = []
        sums = {}
        sums_name = pkg.metadata_names(m, profile)[1]
        for line in content.get(sums_name, b"").decode("ascii", "replace").splitlines():
            mt = re.fullmatch(r"([0-9a-f]{64})  (.+)", line)
            if not mt:
                fails.append(f"{sums_name}: malformed line {line!r}")
                continue
            if mt.group(2) in sums:
                fails.append(f"{sums_name}: {mt.group(2)} listed twice")
            sums[mt.group(2)] = mt.group(1)
        for rel in names:
            if rel == sums_name:
                continue
            if rel not in sums:
                fails.append(f"{rel}: not in {sums_name}")
            elif sums[rel] != sha256(content[rel]):
                fails.append(f"{rel}: {sums_name} says {sums[rel]}, the file is {sha256(content[rel])}")
        for s in sums:
            if s not in content:
                fails.append(f"{sums_name} lists {s}, which isn't in the archive")
        r.check("SHA256SUMS (Gate 6)", len(names), fails)

        # Provenance and artifacts of what ships.
        fails = []
        for dst, c in expected.items():
            e = match(entries, c["source"])
            if e is None:
                fails.append(f"{dst} ({c['source']}): no provenance entry")
            elif e["status"] == "unresolved":
                fails.append(f"{dst} ({c['source']}): provenance unresolved; it may not ship")
            elif e["status"] == "needs-review":
                r.notes.append(f"{dst}: needs-review ({e.get('issue', '')}); its release decision must exist before publication")
        r.check("shipped provenance (Gate 1/B)", len(expected), fails)
        check_artifacts(r, names, lambda f: content[f], prov.get("artifact_allowlist", {}).get("files", []))

        # Licences ship.
        fails = []
        if "LICENSE" not in content:
            fails.append("LICENSE: missing")
        third = [d for d, c in expected.items() if c["origin"] == "third-party" and c["source"] != "LICENSE"]
        for d in third:
            folder = os.path.dirname(d)
            if not any(n.startswith(folder + "/") and re.search(r"(?i)/(LICENSE|COPYING)[^/]*$", n) for n in names):
                fails.append(f"{d}: third-party, but no licence file ships beside it in {folder}/")
        if third and "THIRD_PARTY_NOTICES.md" not in content:
            fails.append("THIRD_PARTY_NOTICES.md: third-party code ships without the notices file")
        r.check("licences and notices (Gate C/D)", len(third) + 1, fails)

        # Core isolation.
        if profile == "core":
            fails = []
            for d, c in expected.items():
                if "game-sc" in c["profiles"] and c["source"] != "LICENSE":
                    fails.append(f"{d}: a game-sc component in core")
            for rel in names:
                if os.path.splitext(rel)[1] in (".h", ".hpp", ".c", ".cpp", ".cs", ".cmake", ".txt") or rel.endswith("CMakeLists.txt"):
                    for mt in GAME_REF.finditer(content[rel].decode("utf-8", "replace")):
                        fails.append(f"{rel}: names {mt.group(0)!r}, which only the Star Citizen extension ships")
            r.check("core isolation (Gate 3/A)", len(names), fails)

        if sbom_path:
            write_sbom(sbom_path, archive, profile, top, names, content, expected, entries, root)
            r.notes.append(f"SBOM written to {sbom_path}")
    r.write(report_path)
    return r


def spdx_id(s):
    return "SPDXRef-" + re.sub(r"[^A-Za-z0-9.-]", "-", s)


def write_sbom(path, archive, profile, top, names, content, expected, entries, root):
    with open(archive, "rb") as f:
        zsha = sha256(f.read())
    commit = git_head(root) or "NOASSERTION"
    pkg_id = spdx_id("Package-" + top)
    files, rels = [], []
    for rel in sorted(names):
        c = expected.get(rel)
        e = match(entries, c["source"]) if c else None
        lic = e["license"] if e else "NOASSERTION"
        cp = e["copyright"] if e else "NOASSERTION"
        fid = spdx_id("File-" + rel)
        files.append({"SPDXID": fid, "fileName": "./" + rel,
                      "checksums": [{"algorithm": "SHA256", "checksumValue": sha256(content[rel])},
                                    {"algorithm": "SHA1", "checksumValue": hashlib.sha1(content[rel]).hexdigest()}],
                      "licenseConcluded": lic if c else "NOASSERTION", "licenseInfoInFiles": ["NOASSERTION"],
                      "copyrightText": cp,
                      "comment": (f"source {c['source']}; origin {c['origin']}; provenance status {e['status'] if e else 'none'}"
                                  if c else "generated by sdk/package.py")})
        rels.append({"spdxElementId": pkg_id, "relationshipType": "CONTAINS", "relatedSpdxElement": fid})
    verification = hashlib.sha1("".join(sorted(f["checksums"][1]["checksumValue"] for f in files)).encode()).hexdigest()
    doc = {
        "spdxVersion": "SPDX-2.3", "dataLicense": "CC0-1.0", "SPDXID": "SPDXRef-DOCUMENT",
        "name": f"{top} ({profile})",
        "documentNamespace": f"https://github.com/scubamount/sco-core/sbom/{top}-{zsha}",
        "creationInfo": {"created": "2026-01-01T00:00:00Z", "creators": ["Tool: sco-core tools/ip_audit.py"],
                         "comment": f"Built from commit {commit}. The fixed creation time keeps the SBOM reproducible; the commit and archive hash bind it."},
        "packages": [{"SPDXID": pkg_id, "name": top, "versionInfo": top.rsplit("-", 1)[-1],
                      "packageFileName": os.path.basename(archive), "supplier": "Organization: sco-core (scubamount)",
                      "downloadLocation": f"git+https://github.com/scubamount/sco-core@{commit}",
                      "filesAnalyzed": True, "packageVerificationCode": {"packageVerificationCodeValue": verification},
                      "checksums": [{"algorithm": "SHA256", "checksumValue": zsha}],
                      "licenseConcluded": "NOASSERTION", "licenseDeclared": "NOASSERTION", "copyrightText": "NOASSERTION",
                      "comment": "Per-file licences are concluded from docs/legal/provenance.json. The package-level licence is left NOASSERTION until the plugin-licence decision (docs/legal/plugin-licensing-options.md)."}],
        "files": files,
        "relationships": [{"spdxElementId": "SPDXRef-DOCUMENT", "relationshipType": "DESCRIBES", "relatedSpdxElement": pkg_id}] + rels,
    }
    with open(path, "w", encoding="utf-8") as f:
        json.dump(doc, f, indent=1)
        f.write("\n")


def parse_rfc3339(text):
    """An RFC 3339 date-time that names 'Z' or a numeric offset (never a bare local time), or None."""
    if not isinstance(text, str):
        return None
    mt = RFC3339.match(text)
    if not mt:
        return None
    y, mo, d, h, mi, s, off = mt.groups()
    try:
        if off in ("Z", "z"):
            tz = datetime.timezone.utc
        else:
            oh, om = int(off[1:3]), int(off[4:6])
            if oh > 23 or om > 59:
                return None
            delta = datetime.timedelta(hours=oh, minutes=om)
            tz = datetime.timezone(-delta if off[0] == "-" else delta)
        return datetime.datetime(int(y), int(mo), int(d), int(h), int(mi), int(s), tzinfo=tz)
    except ValueError:
        return None


def parse_rfc3339_strict(text, what):
    dt = parse_rfc3339(text)
    if dt is None:
        raise AuditError(f"{what}: {text!r} is not an RFC 3339 date-time with Z or an offset")
    return dt


def json_type_ok(instance, t):
    if t == "object":
        return isinstance(instance, dict)
    if t == "array":
        return isinstance(instance, list)
    if t == "string":
        return isinstance(instance, str)
    if t == "integer":
        return isinstance(instance, int) and not isinstance(instance, bool)
    if t == "number":
        return isinstance(instance, (int, float)) and not isinstance(instance, bool)
    if t == "boolean":
        return isinstance(instance, bool)
    if t == "null":
        return instance is None
    raise AuditError(f"{APPROVAL_SCHEMA}: unknown type {t!r}")


def validate_schema(instance, schema, where, fails):
    """Check instance against the JSON Schema (draft 2020-12) subset this tool implements, in the
    standard library: no jsonschema dependency in CI. Appends one message per failure to fails.
    Raises AuditError on a keyword nothing implements, because a schema keyword no code checks is
    a hole in the gate, not a pass."""
    for kw in schema:
        if kw not in SCHEMA_KEYWORDS:
            raise AuditError(f"{APPROVAL_SCHEMA}: keyword {kw!r} at {where} is not implemented by this validator")
    t = schema.get("type")
    if t is not None and not json_type_ok(instance, t):
        fails.append(f"{where}: {short_json(instance)} is not of type {t!r}")
        return
    if "const" in schema and instance != schema["const"]:
        fails.append(f"{where}: {short_json(instance)} is not the const {short_json(schema['const'])}")
    if "enum" in schema and instance not in schema["enum"]:
        fails.append(f"{where}: {short_json(instance)} is not one of {short_json(schema['enum'])}")
    if isinstance(instance, str):
        if "minLength" in schema and len(instance) < schema["minLength"]:
            fails.append(f"{where}: {short_json(instance)} is shorter than minLength {schema['minLength']}")
        if "maxLength" in schema and len(instance) > schema["maxLength"]:
            fails.append(f"{where}: {short_json(instance)} is longer than maxLength {schema['maxLength']}")
        if "pattern" in schema and re.search(schema["pattern"], instance) is None:
            fails.append(f"{where}: {short_json(instance)} does not match pattern {schema['pattern']}")
        if schema.get("format") == "date-time" and parse_rfc3339(instance) is None:
            fails.append(f"{where}: {short_json(instance)} is not an RFC 3339 date-time with Z or an offset")
    elif isinstance(instance, bool):
        pass   # bool is an int in Python; JSON Schema's integer excludes it (the type check above)
    elif isinstance(instance, int):
        if "minimum" in schema and instance < schema["minimum"]:
            fails.append(f"{where}: {instance} is below minimum {schema['minimum']}")
        if "maximum" in schema and instance > schema["maximum"]:
            fails.append(f"{where}: {instance} is above maximum {schema['maximum']}")
    if isinstance(instance, list):
        if "minItems" in schema and len(instance) < schema["minItems"]:
            fails.append(f"{where}: {len(instance)} item(s), fewer than minItems {schema['minItems']}")
        if "maxItems" in schema and len(instance) > schema["maxItems"]:
            fails.append(f"{where}: {len(instance)} item(s), more than maxItems {schema['maxItems']}")
        if schema.get("uniqueItems"):
            seen = []
            for it in instance:
                if it in seen:
                    fails.append(f"{where}: {short_json(it)} appears more than once (uniqueItems)")
                    break
                seen.append(it)
        for i, it in enumerate(instance):
            validate_schema(it, schema["items"], f"{where}[{i}]", fails)
    if isinstance(instance, dict):
        for k in schema.get("required", []):
            if k not in instance:
                fails.append(f"{where}: required property {k!r} is missing")
        if schema.get("additionalProperties") is False:
            for k in instance:
                if k not in schema.get("properties", {}):
                    fails.append(f"{where}: unknown property {k!r} (additionalProperties is false)")
        for k, sub in schema.get("properties", {}).items():
            if k in instance:
                validate_schema(instance[k], sub, f"{where}.{k}", fails)


def schema_keywords(schema):
    """Every keyword the shipped schema uses, nested subschemas included."""
    out = set()
    def walk(node):
        out.update(node)
        for sub in node.get("properties", {}).values():
            walk(sub)
        for group in ("$defs", "definitions"):
            for sub in node.get(group, {}).values():
                walk(sub)
        items = node.get("items")
        for sub in (items if isinstance(items, list) else [items] if isinstance(items, dict) else []):
            walk(sub)
    walk(schema)
    return out


def short_json(value, limit=70):
    s = json.dumps(value, ensure_ascii=False)
    return s if len(s) <= limit else s[:limit - 3] + "..."


def audit_release(approval, tag, root=ROOT, artifacts_dir=None):
    """Gate G: the JSON approval record (docs/legal/release-decisions/<tag>.json) validates against
    docs/legal/release-approval.schema.json and holds its semantics. The record is not yet signed:
    approver.key_fingerprint is recorded, but no signature is checked (that comes later); until then
    the gate rests on the git history and the maintainer's own commit."""
    r = Report("release approval")
    try:
        with open(approval, encoding="utf-8") as f:
            record = json.load(f)
    except OSError as e:
        raise AuditError(f"{approval}: can't read it ({e})")
    except ValueError as e:
        raise AuditError(f"{approval}: not valid JSON ({e})")
    schema = load_json(ROOT, APPROVAL_SCHEMA)   # the validator's schema ships with the tool, not the repo under test
    fails = []
    validate_schema(record, schema, "$", fails)
    r.check(f"record against {APPROVAL_SCHEMA}", 1, fails)
    if fails:
        return r   # semantics on a record that failed the schema would only read missing keys
    sem, now = [], datetime.datetime.now(datetime.timezone.utc)
    if record["decision"] != "approve":
        sem.append(f"the decision is {record['decision']!r}, not 'approve'; CI never publishes on a hold")
    if record["distribution"]["status"] != "approved-for-publication":
        sem.append(f"distribution.status is {record['distribution']['status']!r}, not 'approved-for-publication'")
    if record["release_tag"] != tag:
        sem.append(f"release_tag is {record['release_tag']!r}, but the release tag is {tag!r}")
    approved = parse_rfc3339_strict(record["approved_at"], f"{approval}: approved_at")
    expires = parse_rfc3339_strict(record["expires_at"], f"{approval}: expires_at")
    if approved >= expires:
        sem.append(f"approved_at ({record['approved_at']}) is not before expires_at ({record['expires_at']})")
    if now >= expires:
        sem.append(f"the approval expired at {record['expires_at']} (now {now.isoformat()}); re-approve the release")
    for i, ex in enumerate(record["exceptions"]):
        if now >= parse_rfc3339_strict(ex["expires_at"], f"{approval}: exceptions[{i}].expires_at"):
            sem.append(f"exceptions[{i}] ({ex['id']}) expired at {ex['expires_at']} (now {now.isoformat()})")
    # The record can't name the commit that adds it: the approved commit may be an ancestor,
    # provided nothing but release decisions changed since.
    head, commit = git_head(root), record["source"]["commit"]
    if head and commit != head:
        anc = subprocess.run(["git", "-C", root, "merge-base", "--is-ancestor", commit, head], capture_output=True)
        if anc.returncode != 0:
            sem.append(f"source.commit {commit} is not this commit ({head}) or an ancestor of it")
        else:
            changed = subprocess.run(["git", "-C", root, "diff", "--name-only", commit, head],
                                     capture_output=True, text=True).stdout.split()
            other = [c for c in changed if not c.startswith("docs/legal/release-decisions/")]
            if other:
                sem.append(f"source.commit {commit}, but {len(other)} file(s) changed since outside "
                           f"docs/legal/release-decisions/ ({', '.join(other[:5])}); approve the new commit")
    for role in sorted({a["role"] for a in record["artifacts"]}):
        profile = ROLE_PROFILE.get(role)
        if profile and profile not in record["profiles"]:
            sem.append(f"an artifact has role {role!r}, but profiles does not list {profile!r}")
    names = [a["name"] for a in record["artifacts"]]
    for n in sorted({x for x in names if names.count(x) > 1}):
        sem.append(f"artifact name {n!r} is listed more than once")
    if artifacts_dir:
        if not os.path.isdir(artifacts_dir):
            raise AuditError(f"{artifacts_dir}: can't read it (not a directory)")
        on_disk = {}
        for dirpath, _dirs, filenames in os.walk(artifacts_dir):
            for fn in filenames:
                rel = os.path.relpath(os.path.join(dirpath, fn), artifacts_dir).replace(os.sep, "/")
                with open(os.path.join(dirpath, fn), "rb") as f:
                    data = f.read()
                on_disk[rel] = (len(data), sha256(data))
        listed = {a["name"]: a for a in record["artifacts"]}
        for name in sorted(set(on_disk) - set(listed)):
            sem.append(f"{name}: in {artifacts_dir} but not listed in the record's artifacts")
        for name in sorted(set(listed) - set(on_disk)):
            sem.append(f"{name}: listed in the record but not in {artifacts_dir}")
        for name in sorted(set(on_disk) & set(listed)):
            nbytes, digest = on_disk[name]
            a = listed[name]
            if nbytes != a["bytes"]:
                sem.append(f"{name}: {nbytes} bytes in {artifacts_dir}, the record says {a['bytes']}")
            elif digest != a["sha256"]:
                sem.append(f"{name}: sha256 {digest} in {artifacts_dir}, the record says {a['sha256']}")
    r.check("release approval semantics (Gate G)", 1, sem)
    r.notes.append("the record is not yet signed: approver.key_fingerprint is recorded, but no signature "
                   "is checked; Gate G rests on the git history and the maintainer's own commit")
    return r


def main(argv=None):
    ap = argparse.ArgumentParser(description="IP-compliance checks for sco-core (IP_POLICY.md)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    a = sub.add_parser("repo")
    a.add_argument("--report")
    a.add_argument("--root", default=ROOT)
    b = sub.add_parser("zip")
    b.add_argument("archive")
    b.add_argument("--profile", required=True, choices=["core", "game-sc", "legacy"])
    b.add_argument("--sbom")
    b.add_argument("--report")
    b.add_argument("--root", default=ROOT)
    t = sub.add_parser("reuse-toml", help="write REUSE.toml from docs/legal/provenance.json")
    t.add_argument("--root", default=ROOT)
    c = sub.add_parser("release")
    c.add_argument("--approval", required=True)
    c.add_argument("--tag", required=True, help="the release tag; must equal the record's release_tag")
    c.add_argument("--artifacts", help="directory of the release artifacts; must match the record exactly")
    c.add_argument("--root", default=ROOT)
    args = ap.parse_args(argv)
    try:
        if args.cmd == "reuse-toml":
            with open(os.path.join(args.root, "REUSE.toml"), "w", encoding="utf-8") as f:
                f.write(reuse_toml(provenance_entries(load_json(args.root, PROVENANCE))))
            print("REUSE.toml written")
            return 0
        if args.cmd == "repo":
            r = audit_repo(args.root, args.report)
        elif args.cmd == "zip":
            r = audit_zip(args.archive, args.profile, args.root, args.sbom, args.report)
        else:
            r = audit_release(args.approval, args.tag, args.root, args.artifacts)
    except AuditError as e:
        print(f"ip_audit: can't run: {e}")
        return 2
    return r.finish()


if __name__ == "__main__":
    sys.exit(main())
