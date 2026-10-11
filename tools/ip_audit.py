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
        safe paths without symlinks, no entry is unresolved or refused (Gate 2), the licence and
        notices ship, and for core: no game-sc component and no file that names one. --sbom
        writes an SPDX 2.3 JSON SBOM of the archive bound to the commit and the archive's hash.
    tools/ip_audit.py reuse-toml
        Writes REUSE.toml from docs/legal/provenance.json (the repo audit fails when it is stale);
        `reuse lint` then checks the REUSE specification.
    tools/ip_audit.py release --approval FILE
        Gate G: the record says 'Decision: approve' for a named commit, which is this commit or an
        ancestor with nothing but docs/legal/release-decisions/ changed since. CI never writes one.

Exit 0 = every check passed; 1 = a check failed (each failure is printed with what to do);
2 = the audit couldn't run (missing input). A check that can't run is a failure, never a pass.
Tests: python3 tools/test_ip_audit.py
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
        names, content = [], {}
        for i in infos:
            n = i.filename
            mode = (i.external_attr >> 16) & 0o170000
            rel = n[len(top) + 1:] if n.startswith(top + "/") else n
            if n.endswith("/"):
                continue
            if not pkg.safe_rel(rel) or "\\" in n:
                fails.append(f"{n}: unsafe path")
                continue
            if mode == 0o120000:
                fails.append(f"{n}: a symlink")
                continue
            names.append(rel)
            content[rel] = z.read(i)
        r.check("archive structure", len(infos), fails)

        # Manifest equality: exactly the selected components, byte-identical (links are rewritten
        # in .md files, so those are compared after package.py's own rewrite).
        fails = []
        expected = {c["destination"]: c for c in chosen}
        generated = {"VERSIONS.txt", "SHA256SUMS"}
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
        for line in content.get("SHA256SUMS", b"").decode("ascii", "replace").splitlines():
            mt = re.fullmatch(r"([0-9a-f]{64})  (.+)", line)
            if not mt:
                fails.append(f"SHA256SUMS: malformed line {line!r}")
                continue
            if mt.group(2) in sums:
                fails.append(f"SHA256SUMS: {mt.group(2)} listed twice")
            sums[mt.group(2)] = mt.group(1)
        for rel in names:
            if rel == "SHA256SUMS":
                continue
            if rel not in sums:
                fails.append(f"{rel}: not in SHA256SUMS")
            elif sums[rel] != sha256(content[rel]):
                fails.append(f"{rel}: SHA256SUMS says {sums[rel]}, the file is {sha256(content[rel])}")
        for s in sums:
            if s not in content:
                fails.append(f"SHA256SUMS lists {s}, which isn't in the archive")
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
            r.notes.append(f"SBOM written: {os.path.basename(sbom_path)}")   # a name, never a local path
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


def audit_release(approval, root=ROOT):
    r = Report("release approval")
    fails = []
    try:
        with open(approval, encoding="utf-8") as f:
            text = f.read()
    except OSError as e:
        raise AuditError(f"{approval}: can't read it ({e})")
    head = git_head(root)
    for field in ("Decision:", "Approved by:", "Date:", "Commit:"):
        if field not in text:
            fails.append(f"{approval}: no '{field}' line")
    mt = re.search(r"^Commit:\s*([0-9a-f]{40})\s*$", text, re.M)
    if mt and head and mt.group(1) != head:
        # The record can't name the commit that adds it: the approved commit may be an ancestor,
        # provided nothing but release decisions changed since.
        approved = mt.group(1)
        anc = subprocess.run(["git", "-C", root, "merge-base", "--is-ancestor", approved, head], capture_output=True)
        if anc.returncode != 0:
            fails.append(f"{approval}: approves {approved}, which isn't this commit ({head}) or an ancestor of it")
        else:
            changed = subprocess.run(["git", "-C", root, "diff", "--name-only", approved, head],
                                     capture_output=True, text=True).stdout.split()
            other = [c for c in changed if not c.startswith("docs/legal/release-decisions/")]
            if other:
                fails.append(f"{approval}: approves {approved}, but {len(other)} file(s) changed since outside "
                             f"docs/legal/release-decisions/ ({', '.join(other[:5])}); approve the new commit")
    if re.search(r"^Decision:\s*approve", text, re.M | re.I) is None:
        fails.append(f"{approval}: the decision isn't 'approve'")
    r.check("human approval (Gate G)", 1, fails)
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
            r = audit_release(args.approval, args.root)
    except AuditError as e:
        print(f"ip_audit: can't run: {e}")
        return 2
    return r.finish()


if __name__ == "__main__":
    sys.exit(main())
