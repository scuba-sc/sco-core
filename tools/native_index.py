"""Generate and check sdk/native_index.json, the public index of every signature row.

  python3 tools/native_index.py --write   # rebuild the index from src/game/*_sigs.cpp
  python3 tools/native_index.py --check   # fail when the committed index drifted

A row is one element of an `extern const SigDef k*Signatures[]` table in src/game/*_sigs.cpp,
macro rows expanded (SCO_HS(1) is "offline.handshake_gate.1"). Every table must be registered
by RegisterGameSignatures in src/game/signatures.cpp, and every registered table must exist.

Each entry carries only: id (as written), subsystem (sigs file stem), hash ("_0x" + the first
16 uppercase hex digits of sha256("sco-core:native:" + id)), tier, capability (tiers 1 and 3)
and source. Patterns, masks, offsets, addresses and resolver code are never read into an entry.

Tiers come from sdk/status_registry.json sig_rows. A name that is a row means that row; any
other name is a kCaps capability group (spawn.helpers) and means its rows. 1 promoted = backs a capability that is neither held nor blocked; 3 spike
hold = backs a capability with a spike_ref and default OFF; 4 blocked = backs a capability
whose evidence_level is blocked; 2 resolved-unexposed = everything else. A row backing several
capabilities takes the lowest tier number.
"""
import argparse
import hashlib
import json
import re
import sys
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parent.parent
INDEX = PurePosixPath("sdk/native_index.json")
REGISTRY = PurePosixPath("sdk/status_registry.json")
GAME = PurePosixPath("src/game")
REGISTRATION = GAME / "signatures.cpp"
HASH_SALT = "sco-core:native:"
ENTRY_KEYS = ("id", "subsystem", "hash", "tier", "capability", "source")
TIERS = {
    1: "promoted: backs a capability in sdk/status_registry.json sig_rows",
    2: "resolved-unexposed: a registered row no registry capability names",
    3: "spike hold: backs a capability with a spike_ref and default OFF",
    4: "blocked: backs a capability whose evidence_level is blocked",
}
ROW_ID = re.compile(r"[a-z][a-z0-9_]*(\.[a-z0-9_]+)+")
TOKEN = re.compile(r'"(?:[^"\\\n]|\\.)*"|[A-Za-z_]\w*|\d\w*|::|.', re.S)


class NativeIndexError(Exception):
    pass


def strip_comments(text):
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
        elif text.startswith("//", i):
            while i < n and text[i] != "\n":
                i += 1
        elif text.startswith("/*", i):
            end = text.find("*/", i + 2)
            i = n if end < 0 else end + 2
            out.append(" ")
        else:
            out.append(c)
            i += 1
    return "".join(out)


def tokenize(text):
    return [t for t in TOKEN.findall(text) if not t.isspace()]


def is_string(token):
    return len(token) >= 2 and token[0] == '"' and token[-1] == '"'


def string_value(tokens, where):
    if not tokens or not all(is_string(t) for t in tokens):
        raise NativeIndexError("%s: expected a string literal, got %s" % (where, " ".join(tokens) or "nothing"))
    return "".join(json.loads(t) for t in tokens)


def split_top(tokens):
    parts, cur, depth = [], [], 0
    for t in tokens:
        if t in "({[":
            depth += 1
        elif t in ")}]":
            depth -= 1
        if t == "," and depth == 0:
            parts.append(cur)
            cur = []
        else:
            cur.append(t)
    if cur:
        parts.append(cur)
    return parts


def matching(tokens, start):
    depth = 0
    for i in range(start, len(tokens)):
        if tokens[i] in "({[":
            depth += 1
        elif tokens[i] in ")}]":
            depth -= 1
            if depth == 0:
                return i
    raise NativeIndexError("unbalanced bracket")


def macros(text):
    """Function-like #define macros, continuation lines joined: name -> (params, body tokens)."""
    found = {}
    for line in re.sub(r"\\\n", " ", text).splitlines():
        m = re.match(r"\s*#\s*define\s+(\w+)\(([^)]*)\)\s*(.*)$", line)
        if m:
            params = [p.strip() for p in m.group(2).split(",") if p.strip()]
            found[m.group(1)] = (params, tokenize(m.group(3)))
    return found


def expand(name, args, macro_table, where):
    params, body = macro_table[name]
    if len(args) != len(params):
        raise NativeIndexError("%s: %s takes %d arguments, got %d" % (where, name, len(params), len(args)))
    bound = dict(zip(params, args))
    out, i = [], 0
    while i < len(body):
        t = body[i]
        if t == "#" and i + 1 < len(body) and body[i + 1] in bound:
            out.append(json.dumps(" ".join(bound[body[i + 1]])))
            i += 2
            continue
        out.extend(bound.get(t, [t]))
        i += 1
    return out


def arrays(tokens, type_name):
    """`<type_name> ... kName[] = { ... };` -> {kName: [element token lists]}."""
    found = {}
    for i, t in enumerate(tokens):
        if t != type_name:
            continue
        j = i + 1
        while j < len(tokens) and tokens[j] in ("*", "const"):
            j += 1
        if tokens[j + 1:j + 5] == ["[", "]", "=", "{"]:
            close = matching(tokens, j + 4)
            found[tokens[j]] = split_top(tokens[j + 5:close])
    return found


def parse_sigs(path, rel):
    text = strip_comments(path.read_text(encoding="utf-8"))
    tokens = tokenize(text)
    macro_table = macros(text)
    tables = arrays(tokens, "SigDef")
    if not tables:
        raise NativeIndexError("%s: no SigDef table" % rel)
    rows = []
    for table, elements in tables.items():
        for n, element in enumerate(elements, 1):
            where = "%s %s[%d]" % (rel, table, n)
            if element[0] in macro_table and len(element) > 1 and element[1] == "(":
                element = expand(element[0], split_top(element[2:matching(element, 1)]), macro_table, where)
            if element[0] != "{" or matching(element, 0) != len(element) - 1:
                raise NativeIndexError("%s: not a row initializer: %s" % (where, " ".join(element[:6])))
            fields = split_top(element[1:-1])
            row_id = string_value(fields[0] if fields else [], where)
            if not ROW_ID.fullmatch(row_id):
                raise NativeIndexError("%s: row id %r is not <feature>.<thing>" % (where, row_id))
            rows.append((row_id, table))
    lists = {name: [json.loads(e[0]) for e in elements]
             for name, elements in arrays(tokens, "char").items()
             if all(len(e) == 1 and is_string(e[0]) for e in elements)}
    groups = {}
    for elements in arrays(tokens, "Capability").values():
        for element in elements:
            fields = split_top(element[1:-1]) if element and element[0] == "{" else []
            if len(fields) < 2 or len(fields[1]) != 1 or fields[1][0] not in lists:
                raise NativeIndexError("%s: capability group not understood: %s" % (rel, " ".join(element[:6])))
            groups[string_value(fields[0], rel)] = lists[fields[1][0]]
    return rows, set(tables), groups


def registered_tables(root):
    text = strip_comments((root / REGISTRATION).read_text(encoding="utf-8"))
    return set(re.findall(r"\bRegisterSignatures\(\s*(\w+)\s*,", text))


def collect(root):
    rows, tables, groups, errors = [], set(), {}, []
    for path in sorted((root / GAME).glob("*_sigs.cpp")):
        rel = str(GAME / path.name)
        file_rows, file_tables, file_groups = parse_sigs(path, rel)
        subsystem = path.name[:-len("_sigs.cpp")]
        rows.extend((row_id, subsystem, rel) for row_id, _ in file_rows)
        tables |= file_tables
        for name, members in file_groups.items():
            if name in groups:
                errors.append("capability group %s is defined twice" % name)
            groups[name] = members
    registered = registered_tables(root)
    for table in sorted(tables - registered):
        errors.append("table %s is not registered in %s" % (table, REGISTRATION))
    for table in sorted(registered - tables):
        errors.append("%s registers %s, which no *_sigs.cpp defines" % (REGISTRATION, table))
    seen = {}
    for row_id, _, source in rows:
        if row_id in seen:
            errors.append("row %s is defined in %s and %s" % (row_id, seen[row_id], source))
        seen[row_id] = source
    for name, members in sorted(groups.items()):
        for member in members:
            if member not in seen:
                errors.append("capability group %s names unknown row %s" % (name, member))
    if errors:
        raise NativeIndexError("\n".join(errors))
    return rows, groups


def symbol_hash(row_id):
    return "_0x" + hashlib.sha256((HASH_SALT + row_id).encode("utf-8")).hexdigest()[:16].upper()


def capability_tiers(root, row_ids, groups):
    """row id -> {tier: set(capability names)} from the registry's sig_rows."""
    registry = json.loads((root / REGISTRY).read_text(encoding="utf-8"))
    backing, errors = {}, []
    for service in registry["services"]:
        for cap in service["capabilities"]:
            if not cap.get("sig_rows"):
                continue
            name = cap.get("name") or service["id"]
            if cap.get("evidence_level") == "blocked":
                tier = 4
            elif cap.get("spike_ref") and cap.get("default") == "OFF":
                tier = 3
            else:
                tier = 1
            for ref in cap["sig_rows"]:
                members = [ref] if ref in row_ids else groups.get(ref, [ref])
                for row_id in members:
                    if row_id not in row_ids:
                        errors.append("%s sig_rows names %s, which is neither a row nor a group" % (name, ref))
                        continue
                    backing.setdefault(row_id, {}).setdefault(tier, set()).add(name)
    if errors:
        raise NativeIndexError("\n".join(sorted(set(errors))))
    return backing


def build(root):
    rows, groups = collect(root)
    backing = capability_tiers(root, {r[0] for r in rows}, groups)
    entries, hashes = [], {}
    for row_id, subsystem, source in rows:
        h = symbol_hash(row_id)
        if h in hashes:
            raise NativeIndexError("hash collision: %s and %s both hash to %s" % (hashes[h], row_id, h))
        hashes[h] = row_id
        tiers = backing.get(row_id, {})
        tier = min(tiers) if tiers else 2
        entry = {"id": row_id, "subsystem": subsystem, "hash": h, "tier": tier}
        if tier in (1, 3):
            entry["capability"] = sorted(tiers[tier])
        entry["source"] = source
        entries.append(entry)
    return {
        "schema_version": 1,
        "about": ("Public index of every signature row registered by src/game/signatures.cpp, generated by "
                  "tools/native_index.py from src/game/*_sigs.cpp and sdk/status_registry.json; do not edit. "
                  "hash is _0x plus the first 16 uppercase hex digits of sha256(\"" + HASH_SALT + "\" + id), "
                  "a stable symbolic name, not an address. No pattern, mask, offset or address is recorded. "
                  "Nothing here has been run in game."),
        "tiers": {str(k): v for k, v in sorted(TIERS.items())},
        "row_count": len(entries),
        "entries": entries,
    }


def render(index):
    return json.dumps(index, indent=2, ensure_ascii=True) + "\n"


def summary(index):
    counts = {}
    for e in index["entries"]:
        counts.setdefault(e["subsystem"], {1: 0, 2: 0, 3: 0, 4: 0})[e["tier"]] += 1
    lines = ["%-10s %5s %5s %5s %5s %5s" % ("subsystem", "rows", "t1", "t2", "t3", "t4")]
    for name in sorted(counts):
        c = counts[name]
        lines.append("%-10s %5d %5d %5d %5d %5d" % (name, sum(c.values()), c[1], c[2], c[3], c[4]))
    totals = [sum(c[t] for c in counts.values()) for t in (1, 2, 3, 4)]
    lines.append("%-10s %5d %5d %5d %5d %5d" % ("total", sum(totals), *totals))
    return "\n".join(lines)


def run(root, write):
    index = build(root)
    text = render(index)
    path = root / INDEX
    current = path.read_text(encoding="utf-8") if path.exists() else None
    if current == text:
        return "%s: up to date, %d rows\n%s" % (INDEX, index["row_count"], summary(index))
    if not write:
        raise NativeIndexError("%s drifted from src/game/*_sigs.cpp; run python3 tools/native_index.py --write" % INDEX)
    path.write_text(text, encoding="utf-8")
    return "%s: written, %d rows\n%s" % (INDEX, index["row_count"], summary(index))


def main(argv=None):
    parser = argparse.ArgumentParser(description="Generate or check sdk/native_index.json.")
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true", help="fail when the committed index drifted")
    mode.add_argument("--write", action="store_true", help="rewrite the index")
    parser.add_argument("--root", type=Path, default=ROOT, help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    try:
        print(run(args.root.resolve(), args.write))
    except (NativeIndexError, OSError, ValueError, KeyError) as error:
        print("native_index: %s" % error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
