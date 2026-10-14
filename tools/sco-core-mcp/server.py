#!/usr/bin/env python3
"""Read-only, tools-only MCP 2025-11-25 server for SDK catalogue schema 1.

No SDK invocation, subprocesses, network access, or filesystem writes. Stdout is
reserved for newline-delimited UTF-8 JSON-RPC; diagnostics go to stderr. Besides
the catalogue and record-listed examples, only docs/game/status.md and
docs/legal/release-decisions/ are read, and only beneath --repo-root.
"""
import argparse
from collections import deque
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import sys
import time

PROTOCOL_VERSION = "2025-11-25"
MAX_FRAME = 64 * 1024
MAX_RESPONSE = 4 * 1024 * 1024
MAX_CATALOGUE = 16 * 1024 * 1024
MAX_EXAMPLE = 256 * 1024
MAX_EXAMPLES = 16
LANGUAGES = ("c", "cpp", "csharp", "lua")
PROVENANCE_KEYS = ("schema_version", "sdk_version", "game_pack_version", "source_sha")
RECORD_KEYS = {
    "id", "kind", "symbol", "table", "service", "version_macro", "source_header",
    "signature", "member_extent", "table_size", "since", "purpose",
    "buffer_contract", "ownership", "thread_affinity", "callback_context",
    "reentrancy", "results", "absent_capability", "cleanup", "language_mappings",
    "examples", "tests", "limitations",
}
TABLE_KEYS = {"source_header", "version_macro", "members", "size", "version", "service", "context"}
ANNOTATIONS = {"readOnlyHint": True, "destructiveHint": False,
               "idempotentHint": True, "openWorldHint": False}
TAG_PATTERN = r"^sdk-v[0-9]+\.[0-9]+\.[0-9]+$"
SHA_PATTERN = r"^[0-9a-f]{40}$"
VERSION_PATTERN = r"^(0|[1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})$"
RELEASE_DIR = ("docs", "legal", "release-decisions")
MAX_RELEASE_RECORDS = 64
DISTRIBUTION_STATUSES = {"approved-for-publication", "internal-only", "hold"}
GAME_STATUS = ("docs", "game", "status.md")
GAME_STATUS_HEADER = "| Table | Version | Capability | Default | Evidence |"
GAME_STATUS_SEPARATOR = "|---|---|---|---|---|"
GAME_STATUS_TABLE = re.compile(r"`([^`]+)` \(\[`([^`/]+)`\]\([^)\s]+\)\)")
SIGCHECK_LEVEL = re.compile(r"row resolved on ([0-9][0-9.]*) \(sigcheck\)")
RELEASE_CONCLUSIONS = [
    "Approval records are read from the --repo-root checkout; a record committed only at its release tag is absent elsewhere, so no matching record is not proof that no release exists.",
    "built_source_sha is the approved candidate's built source commit. The release tag names a later approval-only commit that adds the record; the two are distinct and this server does not resolve tags.",
    "No signature is verified here: tools/release_verify.py checks the detached SSH signature against independently trusted allowed signers, the payload and the approval-only descendant.",
    "approved-for-publication records a decision, not that a release was published; publication is not checked."]
STATUS_CAVEAT = (
    "Language mappings describe source-backed exposure, not live availability. "
    "Capability gating, signature resolution and game-session evidence are separate; "
    "an enabled capability does not prove an operation worked in game. "
    "Consult docs/game/status.md and the contract context. No works flag is inferred."
)


def string_schema(maximum=512):
    return {"type": "string", "minLength": 1, "maxLength": maximum}


def tool(name, description, properties=None, required=(), dependent=None):
    schema = {"type": "object", "properties": properties or {},
              "required": list(required), "additionalProperties": False}
    if dependent:
        schema["dependentRequired"] = dependent
    return {"name": name, "description": description, "inputSchema": schema,
            "annotations": dict(ANNOTATIONS)}


SYMBOL = {"symbol_or_id": string_schema()}
LANGUAGE = {"type": "string", "enum": list(LANGUAGES)}
TOOLS = [
    tool("search_api", "Rank contracts by symbol, stable ID, service, purpose and source-derived aliases.",
         {"query": string_schema(), "language": LANGUAGE, "service": string_schema(),
          "limit": {"type": "integer", "minimum": 1, "maximum": 50, "default": 10}}, ("query",)),
    tool("get_symbol", "Return the complete contract, table narrative and catalogue reading context.", SYMBOL, ("symbol_or_id",)),
    tool("list_services", "List catalogue table names, services, packed table versions, x64 sizes and member counts."),
    tool("get_example", "Return record-listed examples only; without a repository root return paths only.",
         {**SYMBOL, "language": LANGUAGE}, ("symbol_or_id",)),
    tool("get_capability_status", "Read language exposure, absent-capability contracts and recorded evidence state, not running-game status.", SYMBOL, ("symbol_or_id",)),
    tool("check_compatibility", "Compare exact SDK/game-pack/source coordinates with catalogue metadata, optionally read a release approval record "
         "(no signature verification) and compare a service's minimum version and member extent with the catalogue table; not runtime certification.",
         {"sdk_version": string_schema(), "game_pack_version": string_schema(),
          "tag": {**string_schema(32), "pattern": TAG_PATTERN},
          "source_sha": {**string_schema(40), "pattern": SHA_PATTERN},
          "service": string_schema(128),
          "min_version": {**string_schema(11), "pattern": VERSION_PATTERN},
          "member_extent": {"type": "integer", "minimum": 1, "maximum": 65535}},
         dependent={"min_version": ["service"], "member_extent": ["service"]}),
]
TOOL_INDEX = {definition["name"]: definition for definition in TOOLS}


class ToolError(ValueError):
    pass


class RpcError(ValueError):
    def __init__(self, code, message):
        super().__init__(message)
        self.code = code


def require(condition, message):
    if not condition:
        raise ValueError(message)


def nonempty(value):
    return isinstance(value, str) and bool(value.strip())


def integer(value):
    return type(value) is int and value >= 0


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, "Duplicate JSON object key")
        result[key] = value
    return result


def parse_json(value):
    def reject_constant(_):
        raise ValueError("Non-finite JSON number")
    return json.loads(value, object_pairs_hook=unique_object, parse_constant=reject_constant)


def encoded(value):
    # ASCII escapes also keep lone surrogates and control characters off the wire.
    return json.dumps(value, ensure_ascii=True, separators=(",", ":"), allow_nan=False).encode("utf-8")


def load_catalogue(path):
    descriptor = os.open(path, os.O_RDONLY | getattr(os, "O_NONBLOCK", 0))
    with os.fdopen(descriptor, "rb") as stream:
        require(stat.S_ISREG(os.fstat(stream.fileno()).st_mode), "Catalogue must be a regular file")
        raw = stream.read(MAX_CATALOGUE + 1)
    require(len(raw) <= MAX_CATALOGUE, "Catalogue exceeds 16 MiB")
    data = parse_json(raw.decode("utf-8"))
    require(isinstance(data, dict), "Catalogue must be an object")
    require(type(data.get("schema_version")) is int and data["schema_version"] == 1,
            "Unsupported catalogue schema_version; expected 1")
    require(set(data) == set(PROVENANCE_KEYS) | {"contract_context", "tables", "records"}, "Invalid catalogue keys")
    require(all(nonempty(data[k]) and len(data[k]) <= 128 for k in ("sdk_version", "game_pack_version")), "Invalid version coordinates")
    require(isinstance(data["source_sha"], str) and re.fullmatch(r"[0-9a-f]{40}", data["source_sha"]), "Invalid source SHA")
    context = data["contract_context"]
    require(isinstance(context, dict) and set(context) == {"source", "reading_rules", "entrypoint_context", "remaining_gaps"}
            and all(isinstance(v, str) for v in context.values()), "Invalid contract context")
    tables = data["tables"]
    require(isinstance(tables, dict) and len(tables) <= 256, "Invalid tables")
    for name, table in tables.items():
        require(nonempty(name) and isinstance(table, dict) and set(table) == TABLE_KEYS, "Invalid table keys")
        require(integer(table["size"]) and integer(table["version"]), "Invalid table coordinates")
        require(isinstance(table["members"], list) and all(nonempty(m) for m in table["members"])
                and len(set(table["members"])) == len(table["members"]), "Invalid table members")
        require(all(isinstance(table[k], str) for k in TABLE_KEYS - {"size", "version", "members"}), "Invalid table text")
    require(isinstance(data["records"], list) and len(data["records"]) <= 5000, "Invalid records")
    identifiers = set()
    for record in data["records"]:
        require(isinstance(record, dict) and set(record) == RECORD_KEYS, "Invalid record keys")
        for key in RECORD_KEYS - {"table", "member_extent", "table_size", "examples", "tests", "limitations", "language_mappings"}:
            require(nonempty(record[key]), "Invalid record text: " + key)
        for key in ("id", "symbol"):
            require(record[key] not in identifiers, "Duplicate catalogue identifier")
            identifiers.add(record[key])
        require(record["kind"] in {"table-method", "entrypoint"}, "Invalid record kind")
        if record["kind"] == "entrypoint":
            require(all(record[k] is None for k in ("table", "member_extent", "table_size")), "Invalid entrypoint layout")
        else:
            require(isinstance(record["table"], str) and record["table"] in tables, "Unknown record table")
            table = tables[record["table"]]
            require(integer(record["member_extent"]) and integer(record["table_size"])
                    and record["member_extent"] <= table["size"] == record["table_size"]
                    and record["service"] == table["service"]
                    and record["symbol"].startswith(record["table"] + ".")
                    and record["symbol"].rsplit(".", 1)[-1] in table["members"], "Invalid record layout")
        for key in ("examples", "tests", "limitations"):
            require(isinstance(record[key], list) and all(nonempty(v) for v in record[key]), "Invalid record array")
        mappings = record["language_mappings"]
        require(isinstance(mappings, dict) and set(mappings) == set(LANGUAGES), "Invalid language mappings")
        for mapping in mappings.values():
            require(isinstance(mapping, dict) and set(mapping) in ({"status", "evidence"}, {"status", "evidence", "tests"})
                    and isinstance(mapping["status"], str)
                    and mapping["status"] in {"direct", "typed-wrapper", "restricted", "unsupported"}, "Invalid mapping status")
            evidence = mapping["evidence"]
            require(isinstance(evidence, list) and evidence, "Missing language evidence")
            require(all(isinstance(e, dict) and set(e) == {"path", "token"}
                        and all(nonempty(v) for v in e.values()) for e in evidence), "Invalid language evidence")
            if "tests" in mapping:
                tests = mapping["tests"]
                require(isinstance(tests, list) and all(nonempty(p) and p in record["tests"] for p in tests),
                        "Invalid language tests")
                require(mapping["status"] == "unsupported" or tests, "Missing language tests")
    return data


def validate_arguments(name, arguments):
    schema = TOOL_INDEX[name]["inputSchema"]
    if set(arguments) - set(schema["properties"]) or not set(schema["required"]) <= set(arguments):
        raise ToolError("Unknown or missing argument; consult tools/list inputSchema")
    for key, value in arguments.items():
        spec = schema["properties"][key]
        if spec["type"] == "string":
            if not nonempty(value) or not spec.get("minLength", 0) <= len(value) <= spec.get("maxLength", 512):
                raise ToolError(key + " must be a nonempty bounded string")
        elif type(value) is not int or not spec["minimum"] <= value <= spec["maximum"]:
            raise ToolError(key + " must be an integer within the advertised range")
        if "enum" in spec and value not in spec["enum"]:
            raise ToolError(key + " is not an advertised language")
        if "pattern" in spec and not re.fullmatch(spec["pattern"], value):
            raise ToolError(key + " does not match the advertised pattern")
    for key, needed in schema.get("dependentRequired", {}).items():
        if key in arguments and not set(needed) <= set(arguments):
            raise ToolError(key + " requires " + ", ".join(needed))


def example_parts(name):
    # Reject Windows separators/drives on every host, not only on Windows.
    if not nonempty(name) or "\\" in name or ":" in name or "\x00" in name:
        raise ToolError("Invalid repository-relative example path")
    path = PurePosixPath(name)
    if path.is_absolute() or ".." in path.parts or not path.parts:
        raise ToolError("Example path must stay inside the repository root")
    return path.parts


def reject_symlinks(base, parts):
    current = base
    for part in parts:
        current = current / part
        if current.is_symlink():
            raise ToolError("Symlink example paths are refused")


def read_example(root, parts):
    """Open a catalogue-supplied relative path inside the configured root.

    The root is operator-configured and therefore trusted: symlinks in it are
    followed. Every component the catalogue supplies is opened without following
    symlinks, so a record cannot name a link that escapes the root.
    """
    if os.open in os.supports_dir_fd and hasattr(os, "O_NOFOLLOW") and hasattr(os, "O_DIRECTORY"):
        directory = os.open(root, os.O_RDONLY | os.O_DIRECTORY)
        try:
            for part in parts[:-1]:
                child = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=directory)
                os.close(directory)
                directory = child
            descriptor = os.open(parts[-1], os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=directory)
        finally:
            os.close(directory)
    else:
        # Platforms without descriptor-relative opens: check the supplied
        # components, then verify the opened file identity. Best effort on a
        # trusted, stable checkout; not race-free.
        reject_symlinks(root, parts)
        path = root.joinpath(*parts)
        if not path.resolve().is_relative_to(root.resolve()):
            raise ToolError("Example escapes repository root")
        before = path.stat()
        if not stat.S_ISREG(before.st_mode):
            raise ToolError("Example must be a regular file")
        descriptor = os.open(path, os.O_RDONLY | getattr(os, "O_NONBLOCK", 0) | getattr(os, "O_NOFOLLOW", 0))
        after = os.fstat(descriptor)
        if (before.st_dev, before.st_ino) != (after.st_dev, after.st_ino):
            os.close(descriptor)
            raise ToolError("Example changed while opening")
    with os.fdopen(descriptor, "rb") as stream:
        info = os.fstat(stream.fileno())
        if not stat.S_ISREG(info.st_mode):
            raise ToolError("Example must be a regular file")
        if info.st_size > MAX_EXAMPLE:
            raise ToolError("Example exceeds 256 KiB")
        raw = stream.read(MAX_EXAMPLE + 1)
    if len(raw) > MAX_EXAMPLE:
        raise ToolError("Example exceeds 256 KiB")
    return raw.decode("utf-8")


def read_recorded_example(root, parts):
    """Read a catalogue example from a repository checkout or extracted SDK archive.

    Catalogue paths are repository-relative (sdk/examples/...), while the core and
    legacy archives store examples/... directly under their top-level directory.
    The recorded path is always tried first. Only when it is absent and the root
    has no sdk entry at all (not even a symlink) is the leading sdk/ component
    dropped, and the remainder goes through the same no-follow containment. A root
    with an sdk entry is a repository layout: a file missing there is never served
    from a second layout.
    """
    try:
        return read_example(root, parts)
    except FileNotFoundError:
        if len(parts) < 2 or parts[0] != "sdk" or os.path.lexists(os.path.join(root, "sdk")):
            raise
    return read_example(root, parts[1:])


def list_contained_directory(root, parts):
    """List a fixed directory beneath the trusted root without following symlinks."""
    if (os.open in os.supports_dir_fd and os.listdir in os.supports_fd
            and hasattr(os, "O_NOFOLLOW") and hasattr(os, "O_DIRECTORY")):
        directory = os.open(root, os.O_RDONLY | os.O_DIRECTORY)
        try:
            for part in parts:
                child = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=directory)
                os.close(directory)
                directory = child
            return os.listdir(directory)
        finally:
            os.close(directory)
    reject_symlinks(root, parts)
    return os.listdir(root.joinpath(*parts))


def parse_game_status(text, tables):
    """Strictly parse the five-column service table of docs/game/status.md.

    Rows are attributed to a catalogue table only when the backticked service
    and the linked header both match it. Any malformed row refuses the whole
    table, so evidence is reported unknown rather than half-read.
    """
    lines = [line.strip() for line in text.splitlines()]
    starts = [index for index, line in enumerate(lines) if line == GAME_STATUS_HEADER]
    require(len(starts) == 1, "service table header not found exactly once")
    start = starts[0] + 1
    require(start < len(lines) and lines[start] == GAME_STATUS_SEPARATOR, "service table separator missing")
    by_service = {table["service"]: (name, table) for name, table in tables.items()}
    groups, current, started = {}, None, False
    for line in lines[start + 1:]:
        if not line.startswith("|"):
            break
        require(line.endswith("|"), "unterminated service table row")
        cells = [cell.strip() for cell in line[1:-1].split("|")]
        require(len(cells) == 5, "service table row does not have five cells")
        table_cell, version, capability, default, evidence = cells
        if table_cell:
            require(version, "service table row has no version")
            match = GAME_STATUS_TABLE.fullmatch(table_cell)
            current, started = None, True
            if match and match[1] in by_service:
                name, table = by_service[match[1]]
                if PurePosixPath(table["source_header"]).name == match[2]:
                    require(name not in groups, "service table repeats a service")
                    current = name
                    catalogue_version = "%d.%d" % (table["version"] >> 16, table["version"] & 0xFFFF)
                    groups[name] = {"documented_version": version,
                                    "version_matches_catalogue": version == catalogue_version, "rows": []}
        else:
            require(started and not version, "continuation row without a table")
        require(capability and evidence and default in {"ON", "OFF", "none"}, "unrecognized service table row")
        if current is None:
            continue
        row = {"capability": capability, "default": default, "evidence": evidence}
        level = SIGCHECK_LEVEL.match(evidence)
        if level:
            row.update(evidence_level="row_resolved_sigcheck", game_build=level[1])
        elif evidence == "OFF pending in-game confirmation":
            row["evidence_level"] = "off_pending_in_game_confirmation"
        elif evidence == "BLOCKED":
            row["evidence_level"] = "blocked"
        else:
            # Includes any in-game claim: this server never reports gameplay verification.
            row["evidence_level"] = "unclassified"
        groups[current]["rows"].append(row)
    return groups


def aliases(record):
    # Schema 1 has no aliases field: derive searchable names from source evidence,
    # retaining the full original mapping evidence in get_symbol.
    result = {record["symbol"].rsplit(".", 1)[-1], record["id"].replace("-", "_")}
    for mapping in record["language_mappings"].values():
        for evidence in mapping["evidence"]:
            result.update(re.findall(r"\b([A-Za-z_]\w*)\s*\(", evidence["token"]))
    return " ".join(sorted(result)).casefold()


class Server:
    def __init__(self, catalogue, repo_root=None):
        self.catalogue = catalogue
        self.provenance = {key: catalogue[key] for key in PROVENANCE_KEYS}
        self.repo_root = Path(os.path.abspath(repo_root)) if repo_root is not None else None
        self.records = {key: record for record in catalogue["records"] for key in (record["id"], record["symbol"])}
        self.search_fields = [(r, [(r["symbol"].casefold(), 100), (r["id"].casefold(), 90),
                                  (r["service"].casefold(), 50), (aliases(r), 40), (r["purpose"].casefold(), 10)])
                              for r in catalogue["records"]]
        self.state = "new"
        self.calls = deque()
        self.game_status, self.game_status_reason = self.load_game_status()

    def load_game_status(self):
        """Return (rows by catalogue table, None) or (None, reason)."""
        if self.repo_root is None:
            return None, "No --repo-root: docs/game/status.md is not read"
        try:
            text = read_example(self.repo_root, GAME_STATUS)
        except (OSError, UnicodeError, ToolError):
            return None, "docs/game/status.md unavailable beneath --repo-root"
        try:
            return parse_game_status(text, self.catalogue["tables"]), None
        except ValueError as error:
            return None, "docs/game/status.md service table refused: " + str(error)

    def evidence_state(self, record):
        """Per-record evidence from the catalogue and, for game-pack records, status rows."""
        game = record["source_header"].startswith("include/sc_")
        state = {"scope": "game-pack" if game else "neutral",
                 "contract_status": {"state": "documented", "source": self.catalogue["contract_context"]["source"]},
                 "default_runtime_state": {"state": "unknown", "reason": "No machine-readable per-method default is recorded"}}
        if not game:
            absent = {"state": "not_applicable", "reason": "Game-independent contract; game-build evidence does not apply"}
            return state | {"sigcheck_receipt": absent, "gameplay_receipt": dict(absent)}
        state["sigcheck_receipt"] = {"state": "unknown", "reason": "No per-method sigcheck receipt is recorded; see service_status"}
        state["gameplay_receipt"] = {"state": "unknown", "reason": "No machine-readable gameplay receipt exists; this server never reports in-game verification"}
        base = {"scope": "service", "source": "/".join(GAME_STATUS),
                "snapshot": "Read from --repo-root at server start; not bound to the catalogue source_sha"}
        if self.game_status is None:
            state["service_status"] = {**base, "state": "unavailable", "reason": self.game_status_reason, "rows": []}
        elif record["table"] not in self.game_status:
            state["service_status"] = {**base, "state": "no_rows", "rows": []}
        else:
            state["service_status"] = {**base, "state": "rows_found", **self.game_status[record["table"]]}
        return state

    def release_record(self, name):
        """Shape-check one approval record; never verifies its signature."""
        entry = {"path": "/".join(RELEASE_DIR + (name,))}
        try:
            text = read_example(self.repo_root, RELEASE_DIR + (name,))
        except FileNotFoundError:
            return None
        except (OSError, UnicodeError, ToolError):
            return entry | {"format": "unreadable"}
        try:
            data = parse_json(text)
        except (ValueError, RecursionError):
            return entry | {"format": "unparseable"}
        if not isinstance(data, dict) or type(data.get("schema_version")) is not int:
            return entry | {"format": "unrecognized"}
        if data["schema_version"] == 1:
            return entry | {"format": "legacy_unsigned_v1"}
        source, distribution = data.get("source"), data.get("distribution")
        if not (data["schema_version"] == 2
                and isinstance(data.get("tag"), str) and re.fullmatch(TAG_PATTERN, data["tag"])
                and isinstance(source, dict) and isinstance(source.get("sha"), str) and re.fullmatch(SHA_PATTERN, source["sha"])
                and isinstance(distribution, dict) and distribution.get("status") in DISTRIBUTION_STATUSES
                and nonempty(data.get("approved_at")) and len(data["approved_at"]) <= 64):
            return entry | {"format": "unrecognized"}
        try:
            signature = read_example(self.repo_root, RELEASE_DIR + (name + ".sig",))
            signature_file = "present" if signature.startswith("-----BEGIN SSH SIGNATURE-----") else "unrecognized"
        except FileNotFoundError:
            signature_file = "absent"
        except (OSError, UnicodeError, ToolError):
            signature_file = "unreadable"
        return entry | {"format": "approval_schema_v2", "tag": data["tag"], "tag_matches_filename": data["tag"] + ".json" == name,
                        "built_source_sha": source["sha"], "distribution_status": distribution["status"],
                        "approved_at": data["approved_at"], "signature_file": signature_file}

    def release_lookup(self, tag, source_sha):
        result = {"state": "unavailable", "records": [], "signature_verification": "not_performed",
                  "publication": "not_checked", "conclusions": RELEASE_CONCLUSIONS}
        if self.repo_root is None:
            return result | {"reason": "No --repo-root: approval records are read only from a configured checkout"}
        if tag is not None:
            names = [tag + ".json"]
        else:
            try:
                names = sorted(n for n in list_contained_directory(self.repo_root, RELEASE_DIR)
                               if re.fullmatch(r"sdk-v[0-9]+\.[0-9]+\.[0-9]+\.json", n))
            except (OSError, ToolError):
                return result | {"reason": "docs/legal/release-decisions unavailable beneath --repo-root"}
            if len(names) > MAX_RELEASE_RECORDS:
                return result | {"reason": "More than 64 approval records; pass tag"}
        records = []
        for name in names:
            entry = self.release_record(name)
            if entry is None:
                continue
            approved = entry["format"] == "approval_schema_v2"
            if approved and tag is not None:
                entry["tag_matches"] = entry["tag"] == tag
            if approved and source_sha is not None:
                entry["built_source_sha_matches"] = entry["built_source_sha"] == source_sha
            # A source-only scan reports matches plus records it could not read.
            if tag is None and (entry.get("built_source_sha_matches") is False or entry["format"] == "legacy_unsigned_v1"):
                continue
            records.append(entry)
        matching = any(entry["format"] == "approval_schema_v2" and entry["tag_matches_filename"]
                       and entry.get("tag_matches", True) and entry.get("built_source_sha_matches", True)
                       for entry in records)
        return result | {"state": "matching_approval_record" if matching else "no_matching_approval_record",
                         "records": records}

    def service_table(self, args):
        matches = [(name, table) for name, table in sorted(self.catalogue["tables"].items())
                   if table["service"] == args["service"]]
        if len(matches) != 1:
            raise ToolError("Unknown or ambiguous service; use list_services")
        name, table = matches[0]
        version = table["version"]
        result = {"table": name, "service": table["service"],
                  "catalogue_version": "%d.%d" % (version >> 16, version & 0xFFFF), "catalogue_size": table["size"]}
        if "min_version" in args:
            major, minor = (int(part) for part in args["min_version"].split("."))
            if major > 0xFFFF or minor > 0xFFFF:
                raise ToolError("min_version components must be at most 65535")
            result["version_satisfies"] = major == version >> 16 and minor <= version & 0xFFFF
        if "member_extent" in args:
            result["extent_within_catalogue_size"] = args["member_extent"] <= table["size"]
        result["conclusions"] = [
            "Compared with the catalogue's compiled x64 header snapshot, using query_service's rule: equal major, provider minor at least the requested minor.",
            "A runtime provider table can be older or shorter: still check table->size covers the member extent and the pointer is non-NULL before calling."]
        return result

    def symbol(self, key):
        if key not in self.records:
            raise ToolError("Unknown symbol or stable ID; use search_api")
        return self.records[key]

    def tool_result(self, data, is_error=False):
        data = {**data, "provenance": self.provenance}
        return {"content": [{"type": "text", "text": encoded(data).decode("utf-8")}],
                "structuredContent": data, "isError": is_error}

    def execute(self, name, args):
        now = time.monotonic()
        while self.calls and self.calls[0] <= now - 60:
            self.calls.popleft()
        if len(self.calls) >= 120:
            raise ToolError("Tool rate limit: 120 calls per 60 seconds; retry later")
        self.calls.append(now)
        validate_arguments(name, args)
        if name == "search_api":
            query = args["query"].casefold().strip()
            terms = query.split()
            hits = []
            for record, fields in self.search_fields:
                if "service" in args and record["service"] != args["service"]:
                    continue
                if "language" in args and record["language_mappings"][args["language"]]["status"] == "unsupported":
                    continue
                if not all(any(term in text for text, _ in fields) for term in terms):
                    continue
                score = sum(weight * (3 if query == text else 2 if query in text else 1)
                            for text, weight in fields if any(term in text for term in terms))
                hits.append({"id": record["id"], "symbol": record["symbol"], "service": record["service"],
                             "purpose": record["purpose"], "score": score})
            hits.sort(key=lambda hit: (-hit["score"], hit["id"]))
            limit = args.get("limit", 10)
            return {"records": hits[:limit], "total_matches": len(hits), "limit": limit}
        if name == "list_services":
            return {"services": [{"table": name, "service": table["service"], "version": table["version"],
                                  "size": table["size"], "member_count": len(table["members"])}
                                 for name, table in sorted(self.catalogue["tables"].items())]}
        if name == "check_compatibility":
            coordinates = {k: self.catalogue[k] for k in PROVENANCE_KEYS[1:]}
            result = {"requested": args, "catalogue_coordinates": coordinates,
                      "coordinate_matches": {key: value == coordinates[key] for key, value in args.items() if key in coordinates},
                      "conclusions": [
                        "Exact version-string equality identifies catalogue coordinates only; no version ordering or ABI compatibility is inferred.",
                        "source_sha identifies the supplied source snapshot, not a verified checkout or running binary.",
                        "Without a requested coordinate no comparison is made. A mismatch is not proof of incompatibility.",
                        "This cannot establish plugin loading, live table size/non-NULL members, enabled capabilities, semantic behavior or game-build support.",
                        "Service-table packed major/minor versions and complete member extents must be checked in the runtime; SDK/game-pack strings are separate."]}
            if "service" in args:
                result["service_table"] = self.service_table(args)
            if "tag" in args or "source_sha" in args:
                result["release"] = self.release_lookup(args.get("tag"), args.get("source_sha"))
            return result
        record = self.symbol(args["symbol_or_id"])
        if name == "get_symbol":
            return {"record": record, "table_context": self.catalogue["tables"].get(record["table"]),
                    "contract_context": self.catalogue["contract_context"], "evidence_state": self.evidence_state(record)}
        if name == "get_capability_status":
            return ({key: record[key] for key in ("language_mappings", "absent_capability", "limitations")}
                    | {"status_caveat": STATUS_CAVEAT, "evidence_state": self.evidence_state(record)})
        examples = record["examples"]
        if len(examples) > MAX_EXAMPLES:
            raise ToolError("Example result exceeds 16 files")
        extensions = {"c": {".c", ".h"}, "cpp": {".cpp", ".cc", ".cxx", ".hpp", ".hxx"},
                      "csharp": {".cs"}, "lua": {".lua"}}
        result = []
        for path in examples:
            parts = example_parts(path)
            if "language" in args and PurePosixPath(path).suffix.casefold() not in extensions[args["language"]]:
                continue
            item = {"path": path}
            if self.repo_root is not None:
                try:
                    item["content"] = read_recorded_example(self.repo_root, parts)
                except (OSError, UnicodeError):
                    # Do not leak absolute host paths from filesystem exception text.
                    raise ToolError("Example unavailable: requires a contained regular UTF-8 file without symlinks") from None
            result.append(item)
        return {"examples": result, "paths_only": self.repo_root is None}

    def dispatch(self, message):
        if not isinstance(message, dict) or message.get("jsonrpc") != "2.0" or not isinstance(message.get("method"), str):
            raise RpcError(-32600, "Invalid JSON-RPC request")
        if "id" in message and type(message["id"]) not in (int, str):
            raise RpcError(-32600, "Invalid request ID")
        method = message["method"]
        if "id" not in message:
            if method == "notifications/initialized" and self.state == "initializing":
                self.state = "ready"
            return None
        params = message.get("params", {})
        if not isinstance(params, dict):
            raise RpcError(-32602, "Parameters must be an object")
        if method == "ping":
            return {}
        if method == "initialize":
            if self.state != "new":
                raise RpcError(-32600, "Already initialized")
            info = params.get("clientInfo")
            if not nonempty(params.get("protocolVersion")) or not isinstance(params.get("capabilities"), dict) or not isinstance(info, dict) or not all(nonempty(info.get(k)) for k in ("name", "version")):
                raise RpcError(-32602, "initialize requires protocolVersion, capabilities and clientInfo")
            self.state = "initializing"
            return {"protocolVersion": PROTOCOL_VERSION, "capabilities": {"tools": {}},
                    "serverInfo": {"name": "sco-core-mcp", "version": "1.0.0"}}
        if method not in {"tools/list", "tools/call"}:
            raise RpcError(-32601, "Method not found")
        if self.state != "ready":
            raise RpcError(-32600, "Complete initialize and notifications/initialized before using tools")
        if method == "tools/list":
            if "cursor" in params:
                raise RpcError(-32602, "No pagination cursor: the complete static tool list fits one response")
            return {"tools": TOOLS}
        name = params.get("name")
        if not isinstance(name, str) or name not in TOOL_INDEX:
            raise RpcError(-32602, "Unknown tool")
        args = params.get("arguments", {})
        if not isinstance(args, dict):
            raise RpcError(-32602, "Tool arguments must be an object")
        try:
            return self.tool_result(self.execute(name, args))
        except ToolError as error:
            return self.tool_result({"error": str(error)}, is_error=True)

    def serve(self, source, target):
        while True:
            line = source.readline(MAX_FRAME + 1)
            if not line:
                return
            message = None
            try:
                if len(line) > MAX_FRAME:
                    while not line.endswith(b"\n"):
                        line = source.readline(MAX_FRAME + 1)
                        if not line:
                            break
                    raise RpcError(-32600, "Input frame exceeds 64 KiB")
                try:
                    message = parse_json(line.decode("utf-8"))
                except (ValueError, UnicodeError, RecursionError):
                    raise RpcError(-32700, "Parse error") from None
                result = self.dispatch(message)
                if result is None:
                    continue
                response = {"jsonrpc": "2.0", "id": message["id"], "result": result}
                if len(encoded(response)) + 1 > MAX_RESPONSE:
                    response["result"] = self.tool_result({"error": "Result exceeds 4 MiB; narrow the request"}, is_error=True)
            except RpcError as error:
                # Invalid IDs cannot be echoed as response identifiers.
                request_id = message.get("id") if isinstance(message, dict) else None
                if type(request_id) not in (int, str):
                    request_id = None
                response = {"jsonrpc": "2.0", "id": request_id,
                            "error": {"code": error.code, "message": str(error)}}
            target.write(encoded(response) + b"\n")
            target.flush()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--catalogue", default=os.environ.get("SCO_CORE_CATALOGUE"))
    parser.add_argument("--repo-root")
    args = parser.parse_args()
    if not args.catalogue:
        parser.error("--catalogue or SCO_CORE_CATALOGUE is required")
    try:
        catalogue = load_catalogue(args.catalogue)
    except (OSError, ValueError, RecursionError) as error:
        print("sco-core-mcp: catalogue refused: " + type(error).__name__ +
              (": " + str(error) if isinstance(error, ValueError) else ""), file=sys.stderr)
        return 1
    try:
        Server(catalogue, args.repo_root).serve(sys.stdin.buffer, sys.stdout.buffer)
    except BrokenPipeError:
        return 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
