"""Validate sdk/status_registry.json and render the service table of docs/game/status.md.

  python3 tools/status_registry.py --check   # validate; fail if the rendered table drifted
  python3 tools/status_registry.py --write   # validate; rewrite the table between the markers

The registry is the source of truth; the markdown table between
"<!-- status-registry:start -->" and "<!-- status-registry:end -->" is generated.
Validation: schema and enums, every include/sc_*.h service has an entry whose
version matches the header's highest version macro, every sig_rows id is defined in
src/game/*_sigs.cpp, every evidence_source exists, and no entry claims in-game
verification without the in_game_verified level. The rendered table keeps the exact
five-column format tools/sco-core-mcp/server.py parses.
"""
import argparse
import json
import os
import re
import sys
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parent.parent
REGISTRY = PurePosixPath("sdk/status_registry.json")
STATUS = PurePosixPath("docs/game/status.md")
START = "<!-- status-registry:start -->"
END = "<!-- status-registry:end -->"
HEADER = "| Table | Version | Capability | Default | Evidence |"
SEPARATOR = "|---|---|---|---|---|"

KINDS = {"service", "event_bus", "signature_rows"}
DEFAULTS = {"ON", "OFF", "none"}
LEVELS = {"sigcheck_resolved", "in_game_verified", "off_pending_probe",
          "synthetic_only", "blocked", "undocumented"}
ENTRY_KEYS = {"id", "kind", "service", "header", "doc", "title", "version", "capabilities"}
CAP_KEYS = {"name", "display", "methods", "default", "evidence_level", "evidence", "game_build",
            "sig_rows", "spike_ref", "evidence_source"}
OFF_PENDING = "OFF pending in-game confirmation"
SIGCHECK = re.compile(r"row resolved on ([0-9][0-9.]*) \(sigcheck\)")
IN_GAME = re.compile(r"verified in game \((\d{4}-\d{2}-\d{2})\)")
SERVICE_NAME = re.compile(r'#define\s+SC_\w*NAME\s+"([^"]+)"')
SERVICE_VERSION = re.compile(r"#define\s+SC_\w*VERSION\w*\s+0x([0-9A-Fa-f]{8})u")
SIG_ROW = re.compile(r'\{\s*"([a-z0-9_]+(?:\.[a-z0-9_]+)+)"\s*,')
IDENT = re.compile(r"[a-z_][a-z0-9_]*(\.[a-z_][a-z0-9_]*)*")


class RegistryError(Exception):
    pass


def header_services(root):
    result = {}
    for path in sorted((root / "include").glob("sc_*.h")):
        text = path.read_text(encoding="utf-8")
        names = SERVICE_NAME.findall(text)
        if not names:
            continue
        versions = [int(v, 16) for v in SERVICE_VERSION.findall(text)]
        best = max(versions) if versions else None
        version = "%d.%d" % (best >> 16, best & 0xFFFF) if best is not None else None
        for name in names:
            result[name] = ("include/" + path.name, version)
    return result


def sig_row_ids(root):
    ids = set()
    for path in sorted((root / "src/game").glob("*_sigs.cpp")):
        ids.update(SIG_ROW.findall(path.read_text(encoding="utf-8")))
    return ids


def validate(registry, root):
    errors = []
    if not isinstance(registry, dict) or registry.get("schema_version") != 1:
        return ["schema_version must be 1"]
    entries = registry.get("services")
    if not isinstance(entries, list) or not entries:
        return ["services must be a non-empty list"]
    rows = sig_row_ids(root)
    headers = header_services(root)
    seen_ids, seen_services = set(), {}
    for index, entry in enumerate(entries):
        where = "services[%d]" % index
        if not isinstance(entry, dict):
            errors.append(where + ": not an object")
            continue
        where = entry["id"] if isinstance(entry.get("id"), str) else where
        extra = set(entry) - ENTRY_KEYS
        if extra:
            errors.append("%s: unknown keys %s" % (where, sorted(extra)))
        if not isinstance(entry.get("id"), str) or entry["id"] in seen_ids:
            errors.append("%s: id missing or repeated" % where)
        seen_ids.add(entry.get("id"))
        kind = entry.get("kind")
        if kind not in KINDS:
            errors.append("%s: kind %r not in %s" % (where, kind, sorted(KINDS)))
            continue
        service, version = entry.get("service"), entry.get("version")
        if not isinstance(service, str) or not service:
            errors.append(where + ": service missing")
        if kind == "signature_rows":
            doc = entry.get("doc")
            if not isinstance(entry.get("title"), str) or not entry["title"]:
                errors.append(where + ": signature_rows entry needs a title")
            if not isinstance(doc, str) or not (root / doc).is_file():
                errors.append("%s: doc %r does not exist" % (where, doc))
            if version != "row":
                errors.append(where + ": signature_rows version must be 'row'")
        else:
            header = entry.get("header")
            if not isinstance(header, str) or not re.fullmatch(r"include/sc_\w+\.h", header) \
                    or not (root / header).is_file():
                errors.append("%s: header %r is not an existing include/sc_*.h" % (where, header))
            if kind == "event_bus":
                if version != "bus":
                    errors.append(where + ": event_bus version must be 'bus'")
            else:
                if not isinstance(version, str) or not re.fullmatch(r"\d+\.\d+", version):
                    errors.append("%s: version %r is not M.m" % (where, version))
                if service in seen_services:
                    errors.append("%s: service %s repeats %s" % (where, service, seen_services[service]))
                seen_services[service] = where
                declared = headers.get(service)
                if declared is None:
                    errors.append("%s: %s is not a service name defined in include/sc_*.h" % (where, service))
                elif declared[0] != header:
                    errors.append("%s: %s is defined in %s, not %s" % (where, service, declared[0], header))
                elif declared[1] != version:
                    errors.append("%s: version %s, header %s says %s" % (where, version, header, declared[1]))
        caps = entry.get("capabilities")
        if not isinstance(caps, list) or not caps:
            errors.append(where + ": needs at least one capability row")
            continue
        for cap in caps:
            errors.extend(validate_capability(cap, where, root, rows))
    for service, (header, _) in sorted(headers.items()):
        if service not in seen_services:
            errors.append("no registry entry for service %s (%s)" % (service, header))
    return errors


def validate_capability(cap, where, root, rows):
    errors = []
    if not isinstance(cap, dict):
        return [where + ": capability is not an object"]
    name = cap.get("name")
    where = "%s/%s" % (where, name or cap.get("display"))
    extra = set(cap) - CAP_KEYS
    if extra:
        errors.append("%s: unknown keys %s" % (where, sorted(extra)))
    if name is None:
        if not isinstance(cap.get("display"), str):
            errors.append(where + ": a row without a capability name needs display text")
    elif not isinstance(name, str) or not IDENT.fullmatch(name):
        errors.append("%s: capability name %r is not a dotted identifier" % (where, name))
    for key in ("display", "evidence", "evidence_source"):
        if key in cap and (not isinstance(cap[key], str) or not cap[key] or "|" in cap[key] or "\n" in cap[key]):
            errors.append("%s: %s must be one non-empty line without '|'" % (where, key))
    methods = cap.get("methods")
    if not isinstance(methods, list) or not all(isinstance(m, str) and re.fullmatch(r"[a-z_][a-z0-9_]*", m) for m in methods):
        errors.append(where + ": methods must be a list of method names")
    default, level, evidence = cap.get("default"), cap.get("evidence_level"), cap.get("evidence")
    if default not in DEFAULTS:
        errors.append("%s: default %r not in %s" % (where, default, sorted(DEFAULTS)))
    if level not in LEVELS:
        errors.append("%s: evidence_level %r not in %s" % (where, level, sorted(LEVELS)))
    if not isinstance(evidence, str) or not evidence:
        errors.append(where + ": evidence text missing")
        return errors
    source = cap.get("evidence_source")
    if not isinstance(source, str) or not (root / source).is_file():
        errors.append("%s: evidence_source %r does not exist" % (where, source))
    build = cap.get("game_build")
    if level == "sigcheck_resolved":
        if not isinstance(build, str) or not re.fullmatch(r"\d+(\.\d+)+", build):
            errors.append(where + ": sigcheck_resolved needs game_build")
        elif not evidence.startswith("row resolved on %s (sigcheck)" % build):
            errors.append(where + ": sigcheck_resolved evidence must start 'row resolved on <game_build> (sigcheck)'")
    elif build is not None:
        errors.append(where + ": game_build belongs only to sigcheck_resolved")
    elif SIGCHECK.search(evidence):
        errors.append(where + ": sigcheck evidence text needs evidence_level sigcheck_resolved")
    if level == "off_pending_probe" and (evidence != OFF_PENDING or default != "OFF"):
        errors.append("%s: off_pending_probe renders OFF / %r" % (where, OFF_PENDING))
    if level == "blocked" and (not evidence.startswith("BLOCKED") or default != "none"):
        errors.append(where + ": blocked renders none / BLOCKED")
    if level == "undocumented" and not evidence.lower().startswith("undocumented"):
        errors.append(where + ": undocumented evidence must say so ('undocumented ...')")
    if (level == "in_game_verified") != bool(IN_GAME.search(evidence)):
        errors.append(where + ": 'verified in game (date)' only with evidence_level in_game_verified")
    if level not in ("off_pending_probe",) and evidence == OFF_PENDING:
        errors.append(where + ": OFF-pending text needs evidence_level off_pending_probe")
    if level != "blocked" and evidence.startswith("BLOCKED"):
        errors.append(where + ": BLOCKED text needs evidence_level blocked")
    sig = cap.get("sig_rows")
    if sig is not None:
        if not isinstance(sig, list) or not sig or not all(isinstance(r, str) for r in sig):
            errors.append(where + ": sig_rows must be a non-empty list of row ids")
        else:
            for row in sig:
                if row not in rows:
                    errors.append("%s: sig row %s is not defined in src/game/*_sigs.cpp" % (where, row))
    spike = cap.get("spike_ref")
    if spike is not None and (not isinstance(spike, str) or not re.fullmatch(r"B\d+", spike)):
        errors.append("%s: spike_ref %r is not B<n>" % (where, spike))
    return errors


def table_cell(entry):
    if entry["kind"] == "signature_rows":
        doc = PurePosixPath(entry["doc"])
        link = PurePosixPath(os.path.relpath(doc, STATUS.parent)).as_posix()
        return "%s ([`%s`](%s))" % (entry["title"], doc.name, link)
    header = PurePosixPath(entry["header"])
    suffix = " events" if entry["kind"] == "event_bus" else ""
    return "`%s`%s ([`%s`](../../%s))" % (entry["service"], suffix, header.name, header)


def capability_cell(cap):
    if cap.get("display"):
        return cap["display"]
    text = "`%s`" % cap["name"]
    if cap["methods"]:
        text += " (%s)" % ", ".join("`%s`" % m for m in cap["methods"])
    return text


def render(registry):
    lines = [HEADER, SEPARATOR]
    for entry in registry["services"]:
        for index, cap in enumerate(entry["capabilities"]):
            lead = "| %s | %s |" % (table_cell(entry), entry["version"]) if index == 0 else "| | |"
            lines.append("%s %s | %s | %s |" % (lead, capability_cell(cap), cap["default"], cap["evidence"]))
    return "\n".join(lines) + "\n"


def splice(text, table):
    if text.count(START) != 1 or text.count(END) != 1 or text.index(START) > text.index(END):
        raise RegistryError("%s needs exactly one %s ... %s block" % (STATUS, START, END))
    head, rest = text.split(START, 1)
    _, tail = rest.split(END, 1)
    return head + START + "\n" + table + END + tail


def load(root):
    try:
        return json.loads((root / REGISTRY).read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise RegistryError("%s unreadable: %s" % (REGISTRY, error))


def run(root, write):
    registry = load(root)
    errors = validate(registry, root)
    if errors:
        raise RegistryError("\n".join(["%s invalid:" % REGISTRY] + ["  " + e for e in errors]))
    path = root / STATUS
    text = path.read_text(encoding="utf-8")
    expected = splice(text, render(registry))
    if expected == text:
        return "%s: table matches %s (%d tables)" % (STATUS, REGISTRY, len(registry["services"]))
    if not write:
        raise RegistryError("%s drifted from %s; run python3 tools/status_registry.py --write" % (STATUS, REGISTRY))
    path.write_text(expected, encoding="utf-8")
    return "%s: table rewritten from %s" % (STATUS, REGISTRY)


def main(argv=None):
    parser = argparse.ArgumentParser(description="Validate sdk/status_registry.json and render docs/game/status.md.")
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true", help="validate and fail on drift")
    mode.add_argument("--write", action="store_true", help="validate and rewrite the table")
    parser.add_argument("--root", type=Path, default=ROOT, help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    try:
        print(run(args.root.resolve(), args.write))
    except RegistryError as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
