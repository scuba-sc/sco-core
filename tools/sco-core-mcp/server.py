#!/usr/bin/env python3
"""Read-only, tools-only MCP 2025-11-25 server for SDK catalogue schema 1.

No SDK invocation, subprocesses, network access, or filesystem writes. Stdout is
reserved for newline-delimited UTF-8 JSON-RPC; diagnostics go to stderr.
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
STATUS_CAVEAT = (
    "Language mappings describe source-backed exposure, not live availability. "
    "Capability gating, signature resolution and game-session evidence are separate; "
    "an enabled capability does not prove an operation worked in game. "
    "Consult docs/game/status.md and the contract context. No works flag is inferred."
)


def string_schema(maximum=512):
    return {"type": "string", "minLength": 1, "maxLength": maximum}


def tool(name, description, properties=None, required=()):
    return {"name": name, "description": description,
            "inputSchema": {"type": "object", "properties": properties or {},
                            "required": list(required), "additionalProperties": False},
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
    tool("get_capability_status", "Read language exposure and absent-capability contracts, not running-game status.", SYMBOL, ("symbol_or_id",)),
    tool("check_compatibility", "Compare exact SDK/game-pack version strings with catalogue coordinates; not runtime certification.",
         {"sdk_version": string_schema(), "game_pack_version": string_schema()}),
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
            require(isinstance(mapping, dict) and set(mapping) == {"status", "evidence"}
                    and isinstance(mapping["status"], str)
                    and mapping["status"] in {"direct", "typed-wrapper", "restricted", "unsupported"}, "Invalid mapping status")
            evidence = mapping["evidence"]
            require(isinstance(evidence, list) and evidence, "Missing language evidence")
            require(all(isinstance(e, dict) and set(e) == {"path", "token"}
                        and all(nonempty(v) for v in e.values()) for e in evidence), "Invalid language evidence")
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
            return {"requested": args, "catalogue_coordinates": {k: self.catalogue[k] for k in PROVENANCE_KEYS[1:]},
                    "coordinate_matches": {key: value == self.catalogue[key] for key, value in args.items()},
                    "conclusions": [
                        "Exact version-string equality identifies catalogue coordinates only; no version ordering or ABI compatibility is inferred.",
                        "source_sha identifies the supplied source snapshot, not a verified checkout or running binary.",
                        "Without a requested coordinate no comparison is made. A mismatch is not proof of incompatibility.",
                        "This cannot establish plugin loading, live table size/non-NULL members, enabled capabilities, semantic behavior or game-build support.",
                        "Service-table packed major/minor versions and complete member extents must be checked in the runtime; SDK/game-pack strings are separate."]}
        record = self.symbol(args["symbol_or_id"])
        if name == "get_symbol":
            return {"record": record, "table_context": self.catalogue["tables"].get(record["table"]),
                    "contract_context": self.catalogue["contract_context"]}
        if name == "get_capability_status":
            return {key: record[key] for key in ("language_mappings", "absent_capability", "limitations")} | {"status_caveat": STATUS_CAVEAT}
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
                    item["content"] = read_example(self.repo_root, parts)
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
