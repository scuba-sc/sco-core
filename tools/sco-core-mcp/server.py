#!/usr/bin/env python3
"""Read-only, tools-only MCP 2025-11-25 server for SDK catalogue schema 1.

By default: no SDK invocation, subprocesses, network access, or filesystem writes.
Stdout is reserved for newline-delimited UTF-8 JSON-RPC; diagnostics go to stderr.
Besides the catalogue and record-listed examples, only sdk/status_registry.json
(else docs/game/status.md), docs/legal/release-decisions/ and, with
--extended-tools, sdk/native_index.json and sdk/tools/sco-new-plugin.py are read,
and only beneath --repo-root. With --extended-tools the guide tools also read
sdk-guides.json beside the catalogue (or --guides); without one, they build the
guide sections in memory from --repo-root's tools/sdk_guides.py and the public docs
it allowlists. Those two generators are the only repository code this process runs:
with --extended-tools, on the first call that needs them, in-process.

The one exception is opt-in: with --enable-dev-tools and at least one
--project-root, three more tools run operator-configured programs (sco-plugin-check,
cmake, ctest) with fixed arguments on a folder inside a project root, with a
timeout, an output cap and a process-tree kill. Callers never supply commands,
flags or environment.
"""
import argparse
from collections import Counter, deque
import hashlib
import importlib.machinery
import importlib.util
import json
import math
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import signal
import stat
import subprocess
import sys
import threading
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
STATUS_REGISTRY = ("sdk", "status_registry.json")
MAX_REGISTRY = 1024 * 1024
REGISTRY_KINDS = {"service", "event_bus", "signature_rows"}
REGISTRY_DEFAULTS = {"ON", "OFF", "none"}
REGISTRY_LEVELS = {"sigcheck_resolved", "in_game_verified", "off_pending_probe",
                   "synthetic_only", "blocked", "undocumented"}
# schema_version 1: before stability labels. 2: "stability" on service and event_bus entries,
# "stable_since" with a stable label, neither on signature_rows entries.
REGISTRY_ENTRY_KEYS = {1: {"id", "kind", "service", "header", "doc", "title", "version", "capabilities"}}
REGISTRY_ENTRY_KEYS[2] = REGISTRY_ENTRY_KEYS[1] | {"stability", "stable_since"}
REGISTRY_STABILITY = {"stable", "experimental"}
REGISTRY_STABLE_SINCE = re.compile(r"sdk-v[0-9]+\.[0-9]+\.[0-9]+")
REGISTRY_CAP_KEYS = {"name", "display", "methods", "default", "evidence_level", "evidence", "game_build",
                     "sig_rows", "spike_ref", "evidence_source"}
NATIVE_INDEX = ("sdk", "native_index.json")
MAX_NATIVE_INDEX = 4 * 1024 * 1024
NATIVE_KEYS = {"schema_version", "about", "tiers", "row_count", "entries"}
NATIVE_ENTRY_KEYS = {"id", "subsystem", "hash", "tier", "source"}
NATIVE_HASH = re.compile(r"_0x[0-9A-F]{16}")
SCAFFOLDER = ("sdk", "tools", "sco-new-plugin.py")
SCAFFOLD_SERVICE = r"^[a-z0-9_]+(\.[a-z0-9_]+)*(@[0-9]{1,5}\.[0-9]{1,5})?$"
SCAFFOLD_LANGS = ("c", "cpp", "csharp", "lua")
SCAFFOLD_USE = {
    "c": "cmake -S DIR -B DIR/build -DSCO_SDK=<SDK folder> and cmake --build DIR/build --target check.",
    "csharp": "dotnet publish DIR -c Release -r win-x64 -o DIR/out/<id> -p:ScoSdk=<SDK folder>, then sco-plugin-check DIR/out/<id>, "
              "with and without --short-tables (README.md in the files).",
    "lua": "cmake -S DIR -B DIR/build -DSCO_SDK=<SDK folder> and cmake --build DIR/build --target check; "
           "with Lua 5.4, lua5.4 <SDK folder>/tools/lua-check.lua DIR runs the script against a stand-in.",
}


def scaffold_conclusions(lang):
    return [
        "files maps each relative path to the text sdk/tools/sco-new-plugin.py's render_plugin returns for this spec; nothing was written, built or checked.",
        "To use them, write the files into a missing or empty folder, then: " + SCAFFOLD_USE.get(lang, SCAFFOLD_USE["c"]),
        "The generator is read from --repo-root and run in this server process; it is not bound to the catalogue source_sha.",
        "The plugin declares api = 1.0 and treats every service as optional; nothing generated here has been loaded by a host or run in game."]
NATIVE_CONCLUSIONS = [
    "hash is _0x plus 16 hex digits of sha256('sco-core:native:' + id): a stable symbolic name, not an address. The index records no pattern, mask, offset or address.",
    "tier comes from sdk/status_registry.json sig_rows: tier 2 means no registry capability names the row, not that the row is unused.",
    "A registered row is not evidence that it resolves on any game build or that anything ran in game.",
    "Read from --repo-root at server start; not bound to the catalogue source_sha."]
GUIDES_FILE = "sdk-guides.json"
GUIDES_BUILDER = ("tools", "sdk_guides.py")
MAX_GUIDES = 16 * 1024 * 1024
GUIDE_KEYS = {"schema_version", "sdk_version", "source_sha", "source", "text_limit", "docs", "sections"}
GUIDE_DOC_KEYS = {"doc", "title", "url", "sections"}
GUIDE_SECTION_KEYS = {"doc", "title", "heading", "anchor", "level", "text", "truncated", "url"}
GUIDE_SNIPPET = 320
GUIDE_STOPWORDS = frozenset(
    "a an and are as at be by can do does for from how i in is it me my of on or should the this to "
    "what when where which why with you your".split())
GUIDE_WORD = re.compile(r"[\w.+#-]+")
GUIDE_CONCLUSIONS = [
    "Sections are the Markdown guides listed in site/public-inputs.json and the SDK's shipped sdk/docs guides, as plain "
    "text bounded to text_limit characters; a truncated section continues in the guide at url.",
    "anchor is the GitHub-style heading slug in the repository file; url is the docs-site page (or the repository path "
    "for sdk/docs guides), whose heading anchors may differ.",
    "Ranking is whole-word term matching (a term of four or more characters also matches words it starts), heading and "
    "title above body text, not semantic search.",
    "Guides are prose, not contract records (use get_symbol), and nothing in them is evidence that code ran in game."]
GAME_STATUS_HEADER = "| Table | Version | Stability | Capability | Default | Evidence |"
GAME_STATUS_SEPARATOR = "|---|---|---|---|---|---|"
# The table before stability labels (SDK 2.3.0 and earlier archives): read, stability unknown.
GAME_STATUS_HEADER_V1 = "| Table | Version | Capability | Default | Evidence |"
GAME_STATUS_SEPARATOR_V1 = "|---|---|---|---|---|"
# "-" is a signature-row group's cell: not a service table, no label.
GAME_STATUS_STABILITY = {"Stable": "stable", "Experimental": "experimental", "-": None}
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
STABILITY_CONCLUSIONS = [
    "stable: the table is never removed or changed incompatibly within SDK 2.x; it grows only at its tail behind a new minor version macro. "
    "Once a released SDK has carried the label it stays stable for the rest of SDK 2.x, whatever later evidence says. "
    "experimental: a later SDK minor may replace it with a new table under a distinct service name and its own header; the old name then "
    "stays published, marked deprecated, for at least one further SDK minor release. See docs/stability.md.",
    "The sco_api root table, plugin entry points and every sco.* host table are stable by that promise. A game-pack table's label (any include/sc_*.h table) is read "
    "from sdk/status_registry.json (else docs/game/status.md) beneath --repo-root at server start, not bound to the catalogue source_sha.",
    "unknown: no --repo-root, the status source is unavailable, refused or predates stability labels, or it has no entry for the table.",
    "Stability is not availability or evidence: a stable table's capability can be OFF or answer SCO_UNAVAILABLE, and nothing here was run in game."]


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
    tool("list_services", "List catalogue table names, services, packed table versions, x64 sizes, member counts and stability labels "
         "(stable/experimental/unknown)."),
    tool("get_example", "Return record-listed examples only; without a repository root return paths only.",
         {**SYMBOL, "language": LANGUAGE}, ("symbol_or_id",)),
    tool("get_capability_status", "Read language exposure, absent-capability contracts, recorded evidence state and the table's stability label, "
         "not running-game status.", SYMBOL, ("symbol_or_id",)),
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
UINT32 = {"type": "integer", "minimum": 0, "maximum": 0xFFFFFFFF}
WIDGET_KIND_NAMES = ("label", "button", "toggle", "slider", "combo", "list", "text_input", "table", "row", "column")
WIDGET_TEXT = {"type": ["string", "null"], "maxLength": 1024}
WIDGET_NODE = {
    "type": "object", "required": ["kind", "id"], "additionalProperties": False,
    "properties": {
        "kind": {"oneOf": [UINT32, {"type": "string", "enum": list(WIDGET_KIND_NAMES)}]},
        "id": UINT32, "parent": UINT32, "flags": UINT32, "columns": UINT32,
        "index": {"type": "integer", "minimum": -0x80000000, "maximum": 0x7FFFFFFF},
        "label": WIDGET_TEXT, "text": WIDGET_TEXT,
        "value": {"type": "number"}, "minimum": {"type": "number"}, "maximum": {"type": "number"},
        "items": {"type": ["array", "null"], "maxItems": 1024, "items": {"type": "string", "maxLength": 1024}}}}
# Opt-in with --extended-tools, so the default tools/list stays the original six for existing allowlists.
EXTENDED_TOOLS = [
    tool("list_engine_natives", "List signature rows of sdk/native_index.json beneath --repo-root, filtered by subsystem, tier or a "
         "case-insensitive substring of id, hash, subsystem, source or capability, one bounded page per call; symbolic hashes only.",
         {"subsystem": string_schema(64), "tier": {"type": "integer", "minimum": 1, "maximum": 4},
          "query": string_schema(128), "limit": {"type": "integer", "minimum": 1, "maximum": 100, "default": 25},
          "cursor": {**string_schema(6), "pattern": r"^(0|[1-9][0-9]{0,5})$"}}),
    tool("get_engine_native", "Return one signature row of sdk/native_index.json by row id or _0x hash with its tier meaning; "
         "not evidence that the row resolves on a game build or ran in game.",
         {"id_or_hash": string_schema(128)}, ("id_or_hash",)),
    tool("validate_manifest_and_widgets", "Check plugin.ini text with sco-plugin-check's manifest rules and optional sco_widget_node "
         "objects against the sco_widgets.h tree invariants; returns findings and touches no files.",
         {"plugin_ini": string_schema(32768), "folder": {**string_schema(255), "pattern": r"^[^/\\]+$"},
          "widgets": {"type": "array", "minItems": 1, "maxItems": 256, "items": WIDGET_NODE}}, ("plugin_ini",)),
    tool("generate_plugin_scaffold", "Return, as {relative path: text}, the plugin folder sdk/tools/sco-new-plugin.py beneath --repo-root "
         "would generate for this spec (c, cpp or csharp native plugin, or lua script; lua takes no services "
         "beyond sco.widgets with with_menu); writes, builds and runs nothing.",
         {"id": {**string_schema(31), "pattern": r"^[a-z0-9_]{1,31}$"},
          "lang": {"type": "string", "enum": list(SCAFFOLD_LANGS)},
          "with_menu": {"type": "boolean", "default": False},
          "services": {"type": "array", "minItems": 0, "maxItems": 16,
                       "items": {**string_schema(75), "pattern": SCAFFOLD_SERVICE}}}, ("id", "lang")),
    tool("search_guides", "Rank sections of the SDK guides (sdk-guides.json: the public docs allowlist and the shipped sdk/docs "
         "guides) by the query's whole-word terms, heading and title above body text, optionally within one doc; returns "
         "bounded snippets with doc, heading, anchor and url, one page per call. Guide prose is not a contract record or "
         "in-game evidence.",
         {"query": string_schema(256), "doc": string_schema(256),
          "limit": {"type": "integer", "minimum": 1, "maximum": 25, "default": 5},
          "cursor": {**string_schema(6), "pattern": r"^(0|[1-9][0-9]{0,5})$"}}, ("query",)),
    tool("get_guide_section", "Return one SDK guide section's bounded plain text by the doc and anchor search_guides returned.",
         {"doc": string_schema(256), "anchor": string_schema(256)}, ("doc", "anchor")),
]
# Opt-in with --enable-dev-tools plus --project-root. These run programs on this machine, including the
# plugin's and the project's own code, so they do not claim to be read-only.
DEV_ANNOTATIONS = {"readOnlyHint": False, "destructiveHint": True,
                   "idempotentHint": False, "openWorldHint": False}
DEV_PATH = string_schema(1024)


def dev_tool(name, description, properties, required):
    definition = tool(name, description, properties, required)
    definition["annotations"] = dict(DEV_ANNOTATIONS)
    return definition


DEV_TOOLS = [
    dev_tool("run_plugin_check", "Run the operator-configured sco-plugin-check on a built plugin folder inside a --project-root "
             "(mode short adds --short-tables, as a 1.0 host serves); returns exit code, pass/fail and FAIL lines. "
             "Loads and runs the plugin's code against a stand-in host; not run in game.",
             {"folder": DEV_PATH, "mode": {"type": "string", "enum": ["full", "short"], "default": "full"}}, ("folder",)),
    dev_tool("build_plugin_project", "Configure and build a CMake plugin project inside a --project-root with the operator's cmake, "
             "generator and SCO_SDK, into the project's build subfolder; fixed arguments, bounded output tail.",
             {"project": DEV_PATH}, ("project",)),
    dev_tool("test_plugin_project", "Run ctest in the build subfolder of a plugin project inside a --project-root with fixed "
             "arguments; bounded output tail. Build it first with build_plugin_project.",
             {"project": DEV_PATH}, ("project",)),
]
TOOL_INDEX = {definition["name"]: definition for definition in TOOLS + EXTENDED_TOOLS + DEV_TOOLS}
DEV_TIMEOUT = 600
DEV_TIMEOUT_RANGE = (1, 7200)
DEV_OUTPUT = 32 * 1024
DEV_OUTPUT_RANGE = (256, 256 * 1024)
DEV_FINDINGS = 256
DEV_LINE = 2048
DEV_BUILD_CONFIG = "Release"
DEV_EXE = ".exe" if os.name == "nt" else ""
CHECK_SUMMARY = re.compile(r"^(OK|FAILED): [0-9]+ failure\(s\)$")
CTEST_SUMMARY = re.compile(r"^[0-9]+% tests passed, [0-9]+ tests? failed out of [0-9]+$")
DEV_CONCLUSIONS = [
    "Ran on this machine: an operator-configured program with fixed arguments; the caller supplied only a folder inside a --project-root.",
    "The project's CMakeLists.txt and the plugin's own code run with the server's privileges; treat each project root as code you would run yourself.",
    "output is the last output_limit bytes of combined stdout and stderr; findings and summary are collected from the whole stream.",
    "sco-plugin-check is a stand-in host, not the real host. Nothing here loaded the plugin in the game: not run in game."]


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
        elif spec["type"] == "array":
            if not isinstance(value, list) or not spec["minItems"] <= len(value) <= spec["maxItems"]:
                raise ToolError("%s must be an array of %d-%d items" % (key, spec["minItems"], spec["maxItems"]))
            items = spec["items"]
            if items.get("type") == "string" and not all(
                    nonempty(item) and items["minLength"] <= len(item) <= items["maxLength"]
                    and re.fullmatch(items["pattern"], item) for item in value):
                raise ToolError(key + " items must be strings matching the advertised pattern")
        elif spec["type"] == "boolean":
            if type(value) is not bool:
                raise ToolError(key + " must be true or false")
        elif type(value) is not int or not spec["minimum"] <= value <= spec["maximum"]:
            raise ToolError(key + " must be an integer within the advertised range")
        if "enum" in spec and value not in spec["enum"]:
            raise ToolError(key + (" is not an advertised language" if key == "language" else " is not an advertised value"))
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


def read_example(root, parts, limit=MAX_EXAMPLE):
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
        if info.st_size > limit:
            raise ToolError("Example exceeds %d KiB" % (limit // 1024))
        raw = stream.read(limit + 1)
    if len(raw) > limit:
        raise ToolError("Example exceeds %d KiB" % (limit // 1024))
    return raw.decode("utf-8").replace("\r\n", "\n").replace("\r", "\n")


def read_recorded_example(root, parts, limit=MAX_EXAMPLE):
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
        return read_example(root, parts, limit)
    except FileNotFoundError:
        if len(parts) < 2 or parts[0] != "sdk" or os.path.lexists(os.path.join(root, "sdk")):
            raise
    return read_example(root, parts[1:], limit)


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
    """Strictly parse the service table of docs/game/status.md.

    The current table has six columns (Stability after Version); the five-column
    table of older archives is still read, with stability None. Rows are attributed
    to a catalogue table only when the backticked service and the linked header both
    match it. Any malformed row refuses the whole table, so evidence is reported
    unknown rather than half-read.
    """
    lines = [line.strip() for line in text.splitlines()]
    starts = [(index, line == GAME_STATUS_HEADER) for index, line in enumerate(lines)
              if line in (GAME_STATUS_HEADER, GAME_STATUS_HEADER_V1)]
    require(len(starts) == 1, "service table header not found exactly once")
    start, labelled = starts[0][0] + 1, starts[0][1]
    separator = GAME_STATUS_SEPARATOR if labelled else GAME_STATUS_SEPARATOR_V1
    require(start < len(lines) and lines[start] == separator, "service table separator missing")
    width = 6 if labelled else 5
    by_service = {table["service"]: (name, table) for name, table in tables.items()}
    groups, current, started = {}, None, False
    for line in lines[start + 1:]:
        if not line.startswith("|"):
            break
        require(line.endswith("|"), "unterminated service table row")
        cells = [cell.strip() for cell in line[1:-1].split("|")]
        require(len(cells) == width, "service table row does not have %d cells" % width)
        if labelled:
            table_cell, version, label, capability, default, evidence = cells
        else:
            (table_cell, version, capability, default, evidence), label = cells, None
        if table_cell:
            require(version, "service table row has no version")
            require(not labelled or label in GAME_STATUS_STABILITY, "service table row has no Stable, Experimental or - label")
            match = GAME_STATUS_TABLE.fullmatch(table_cell)
            current, started = None, True
            if match and match[1] in by_service:
                name, table = by_service[match[1]]
                if PurePosixPath(table["source_header"]).name == match[2]:
                    require(name not in groups, "service table repeats a service")
                    current = name
                    catalogue_version = "%d.%d" % (table["version"] >> 16, table["version"] & 0xFFFF)
                    groups[name] = {"documented_version": version,
                                    "version_matches_catalogue": version == catalogue_version,
                                    "stability": GAME_STATUS_STABILITY.get(label), "rows": []}
        else:
            require(started and not version and not label, "continuation row without a table")
        require(capability and evidence and default in {"ON", "OFF", "none"}, "unrecognized service table row")
        if current is None:
            continue
        groups[current]["rows"].append({"capability": capability, "default": default, "evidence": evidence,
                                         **classify_evidence(evidence)})
    return groups


def classify_evidence(evidence):
    level = SIGCHECK_LEVEL.match(evidence)
    if level:
        return {"evidence_level": "row_resolved_sigcheck", "game_build": level[1]}
    if evidence == "OFF pending in-game confirmation":
        return {"evidence_level": "off_pending_in_game_confirmation"}
    if evidence == "BLOCKED":
        return {"evidence_level": "blocked"}
    # Includes any in-game claim: this server never reports gameplay verification.
    return {"evidence_level": "unclassified"}


def registry_row(cap):
    """One registry capability as a status row: the rendered cell plus the registry's structured fields."""
    require(isinstance(cap, dict) and set(cap) <= REGISTRY_CAP_KEYS, "capability has unknown keys")
    require(isinstance(cap.get("default"), str) and cap["default"] in REGISTRY_DEFAULTS, "capability default is not ON, OFF or none")
    require(isinstance(cap.get("evidence_level"), str) and cap["evidence_level"] in REGISTRY_LEVELS, "unknown evidence_level")
    require(nonempty(cap.get("evidence")) and "|" not in cap["evidence"], "capability evidence must be text")
    require(cap.get("name") is None or nonempty(cap["name"]), "capability name must be text or null")
    require("display" not in cap or nonempty(cap["display"]), "capability display must be text")
    require(nonempty(cap.get("display")) or nonempty(cap.get("name")), "capability needs a name or display")
    for key in ("methods", "sig_rows"):
        require(key not in cap or (isinstance(cap[key], list) and all(nonempty(v) for v in cap[key])), key + " must be a list of text")
    for key in ("game_build", "spike_ref", "evidence_source"):
        require(key not in cap or nonempty(cap[key]), key + " must be text")
    if cap.get("display"):
        cell = cap["display"]
    else:
        cell = "`%s`" % cap["name"]
        if cap.get("methods"):
            cell += " (%s)" % ", ".join("`%s`" % m for m in cap["methods"])
    row = {"capability": cell, "default": cap["default"], "evidence": cap["evidence"], **classify_evidence(cap["evidence"])}
    # The registry's own level, except that an in-game claim is withheld like any other.
    row["registry_evidence_level"] = "unclassified" if cap["evidence_level"] == "in_game_verified" else cap["evidence_level"]
    for key in ("name", "methods", "sig_rows", "spike_ref", "evidence_source"):
        if key in cap:
            row[key] = cap[key]
    return row


def parse_status_registry(text, tables):
    """Strictly read sdk/status_registry.json into the same groups parse_game_status returns.

    Only kind "service" entries are attributed, and only when the service and the header
    file name both match a catalogue table (the status.md rule). Any malformed entry
    refuses the whole registry.
    """
    data = parse_json(text)
    require(isinstance(data, dict) and set(data) == {"schema_version", "about", "services"}, "unexpected top-level keys")
    schema = data["schema_version"]
    require(type(schema) is int and schema in REGISTRY_ENTRY_KEYS, "unsupported schema_version; expected 1 or 2")
    entries = data["services"]
    require(isinstance(entries, list) and len(entries) <= 512, "services must be a list of at most 512 entries")
    by_service = {table["service"]: (name, table) for name, table in tables.items()}
    groups = {}
    for entry in entries:
        require(isinstance(entry, dict) and set(entry) <= REGISTRY_ENTRY_KEYS[schema], "entry has unknown keys")
        require(isinstance(entry.get("kind"), str) and entry["kind"] in REGISTRY_KINDS, "unknown entry kind")
        require(all(nonempty(entry.get(key)) for key in ("id", "service", "version")), "entry needs id, service and version")
        if schema >= 2:
            # Shape only; tools/status_registry.py applies the evidence rule and the ratchet in the repository.
            if entry["kind"] == "signature_rows":
                require("stability" not in entry and "stable_since" not in entry, "signature_rows entry has a stability label")
            else:
                require(isinstance(entry.get("stability"), str) and entry["stability"] in REGISTRY_STABILITY,
                        "stability is not stable or experimental")
                since = entry.get("stable_since")
                require((since is None) == (entry["stability"] != "stable")
                        and (since is None or (isinstance(since, str) and REGISTRY_STABLE_SINCE.fullmatch(since))),
                        "stable_since must name sdk-vX.Y.Z exactly when stability is stable")
        capabilities = entry.get("capabilities")
        require(isinstance(capabilities, list) and 1 <= len(capabilities) <= 256, "entry needs 1-256 capabilities")
        rows = [registry_row(cap) for cap in capabilities]
        if entry["kind"] != "service":
            continue
        require(nonempty(entry.get("header")), "service entry has no header")
        if entry["service"] not in by_service:
            continue
        name, table = by_service[entry["service"]]
        if PurePosixPath(table["source_header"]).name != PurePosixPath(entry["header"]).name:
            continue
        require(name not in groups, "registry repeats a service")
        catalogue_version = "%d.%d" % (table["version"] >> 16, table["version"] & 0xFFFF)
        groups[name] = {"documented_version": entry["version"],
                        "version_matches_catalogue": entry["version"] == catalogue_version,
                        "stability": entry.get("stability"), "rows": rows}
    return groups


def native_hash(identifier):
    return "_0x" + hashlib.sha256(("sco-core:native:" + identifier).encode("utf-8")).hexdigest()[:16].upper()


def parse_native_index(text):
    """Strictly read sdk/native_index.json; any malformed entry refuses the whole index."""
    data = parse_json(text)
    require(isinstance(data, dict) and set(data) == NATIVE_KEYS, "unexpected top-level keys")
    require(type(data["schema_version"]) is int and data["schema_version"] == 1, "unsupported schema_version; expected 1")
    tiers = data["tiers"]
    require(isinstance(tiers, dict) and set(tiers) == {"1", "2", "3", "4"} and all(nonempty(v) for v in tiers.values()),
            "tiers must describe 1-4")
    entries = data["entries"]
    require(isinstance(entries, list) and len(entries) <= 65536 and type(data["row_count"]) is int
            and data["row_count"] == len(entries), "row_count must equal the number of entries")
    by_key = {}
    for entry in entries:
        require(isinstance(entry, dict) and set(entry) in (NATIVE_ENTRY_KEYS, NATIVE_ENTRY_KEYS | {"capability"}), "entry keys")
        require(all(nonempty(entry[key]) and len(entry[key]) <= 256 for key in ("id", "subsystem", "source")), "entry text")
        require(type(entry["tier"]) is int and 1 <= entry["tier"] <= 4, "entry tier must be 1-4")
        require(entry["hash"] == native_hash(entry["id"]), "entry hash is not derived from its id")
        require("capability" not in entry or (isinstance(entry["capability"], list) and entry["capability"]
                                              and all(nonempty(c) for c in entry["capability"])), "entry capability")
        require(entry["id"] not in by_key and entry["hash"] not in by_key, "duplicate id or hash")
        by_key[entry["id"]] = by_key[entry["hash"]] = entry
    return data, by_key


def parse_guides(data):
    """Strictly check an sdk-guides.json document (tools/sdk_guides.py validate, restated for the
    archive, which ships this server without the builder). source_sha is null only for a document
    built in memory from --repo-root. Any malformed section refuses the whole document."""
    require(isinstance(data, dict) and set(data) == GUIDE_KEYS, "unexpected top-level keys")
    require(type(data["schema_version"]) is int and data["schema_version"] == 1, "unsupported schema_version; expected 1")
    require(data["source_sha"] is None or (isinstance(data["source_sha"], str) and re.fullmatch(SHA_PATTERN, data["source_sha"])),
            "invalid source_sha")
    require(nonempty(data["sdk_version"]) and len(data["sdk_version"]) <= 128 and data["source"] == "site/public-inputs.json",
            "invalid sdk_version or source")
    limit = data["text_limit"]
    require(type(limit) is int and 1 <= limit <= 65536, "invalid text_limit")
    require(isinstance(data["docs"], list) and len(data["docs"]) <= 1024
            and isinstance(data["sections"], list) and len(data["sections"]) <= 20000, "invalid docs or sections")
    counts = {}
    for doc in data["docs"]:
        require(isinstance(doc, dict) and set(doc) == GUIDE_DOC_KEYS and type(doc["sections"]) is int
                and all(nonempty(doc[key]) and len(doc[key]) <= 512 for key in ("doc", "title", "url"))
                and doc["doc"] not in counts, "invalid docs entry")
        counts[doc["doc"]] = doc["sections"]
    by_key, found = {}, {}
    for section in data["sections"]:
        require(isinstance(section, dict) and set(section) == GUIDE_SECTION_KEYS, "section has unknown keys")
        require(all(nonempty(section[key]) and len(section[key]) <= 512 for key in ("doc", "title", "heading", "anchor", "url"))
                and isinstance(section["text"], str) and len(section["text"]) <= limit, "invalid section text")
        require(type(section["level"]) is int and 1 <= section["level"] <= 6 and type(section["truncated"]) is bool,
                "invalid section level or truncated flag")
        key = (section["doc"], section["anchor"])
        require(section["doc"] in counts and key not in by_key, "duplicate or undeclared section")
        by_key[key] = section
        found[section["doc"]] = found.get(section["doc"], 0) + 1
    require(found == {name: count for name, count in counts.items() if count}, "docs section counts disagree with sections")
    return data, by_key


def guide_sequence(text):
    """Casefolded whole words in order (the query tokenizer)."""
    return [word for word in (word.strip(".-") for word in GUIDE_WORD.findall(text.casefold())) if word]


def guide_terms(query):
    """Query words without common question words or single characters, unless nothing else is left."""
    words = list(dict.fromkeys(guide_sequence(query)))
    kept = [word for word in words if word not in GUIDE_STOPWORDS] or words
    return [word for word in kept if len(word) > 1] or kept


def guide_index(text):
    """(word counts, padded word sequence): whole words, plus the parts of dotted or hyphenated ones."""
    sequence = guide_sequence(text)
    counts = Counter(sequence)
    counts.update(part for word in sequence if "." in word or "-" in word
                  for part in re.split(r"[.-]+", word) if part)
    return counts, " " + " ".join(sequence) + " "


def guide_hits(counts, term):
    """Whole-word occurrences of term; a term of four or more characters also counts words it starts."""
    if len(term) < 4:
        return counts.get(term, 0)
    return sum(count for word, count in counts.items() if word.startswith(term))


def guide_snippet(text, terms):
    """A bounded, whitespace-collapsed window of text around the first term it contains."""
    folded = text.casefold()
    found = (re.search(r"(?<![\w])" + re.escape(term) + (r"" if len(term) >= 4 else r"(?![\w])"), folded) for term in terms)
    positions = [match.start() for match in found if match]
    start = max(0, min(positions) - GUIDE_SNIPPET // 4) if positions else 0
    end = min(len(text), start + GUIDE_SNIPPET)
    snippet = " ".join(text[start:end].split())
    return ("..." if start else "") + snippet + ("..." if end < len(text) else "")


# ---- plugin.ini: sdk/tools/sco-plugin-check.c read_manifest/setting_line, rule for rule, on bytes ----
SCO_API_VERSION = (1, 1)  # SCO_API_MAJOR/SCO_API_MINOR in include/sco_api.h; the tests compare them.
MAX_MANIFEST_BYTES = 16 * 1024
MANIFEST_KEYS = (b"id", b"name", b"version", b"author", b"api", b"kind", b"entry", b"requires")
SETTING_FIELDS = (b"default", b"min", b"max", b"label", b"help")
SPACE = b" \t"


class ManifestRefused(ValueError):
    def __init__(self, message, line=None):
        super().__init__(message)
        self.line = line


def _text(raw):
    return raw.decode("utf-8", errors="replace")


def _bad(line, what):
    raise ManifestRefused("plugin.ini line %d: %s" % (line, what), line)


def _trim(s):
    return s.lstrip(SPACE).rstrip(b" \t\r")


def _strip_comment(s):
    for i, c in enumerate(s):
        if c in b";#" and (i == 0 or s[i - 1] in SPACE):
            return s[:i]
    return s


def _strip_settings_comment(s):
    quoted = False
    for i, c in enumerate(s):
        if c == 0x22:
            quoted = not quoted
        elif not quoted and c in b";#" and (i == 0 or s[i - 1] in SPACE):
            return s[:i]
    return s


def _id_chars(s):
    return all(0x61 <= c <= 0x7A or 0x30 <= c <= 0x39 or c == 0x5F for c in s)


def _valid_word(s, maximum):
    return 1 <= len(s) <= maximum and _id_chars(s)


def _valid_capability(s):
    return (1 <= len(s) <= 63 and not s.startswith(b".") and not s.endswith(b".") and b".." not in s
            and _id_chars(s.replace(b".", b"")))


def _valid_text(s, maximum):
    return 1 <= len(s) <= maximum and all(c >= 0x20 and c != 0x7F for c in s)


def _valid_entry(s):
    return (1 <= len(s) <= 63 and s not in (b".", b"..")
            and all(c >= 0x20 and c not in b'/\\:*?"<>|' for c in s))


def _parse_api(s):
    major, dot, minor = s.partition(b".")
    parts = [major, minor]
    if not dot or not all(re.fullmatch(rb"[0-9]{1,5}", p) and int(p) <= 0xFFFF for p in parts):
        return None
    return int(major), int(minor)


def _parse_i64(s):
    if not re.fullmatch(rb"[+-]?[0-9]{1,19}", s):
        return None
    value = int(s)
    return value if -2 ** 63 <= value <= 2 ** 63 - 1 else None


def _parse_f64(s):
    if len(s) > 64 or not re.fullmatch(rb"[+-]?[0-9]+(\.[0-9]+)?([eE][+-]?[0-9]+)?", s):
        return None
    value = float(s)
    return value if math.isfinite(value) else None


def _check_setting(decl, text):
    """None when text is a value of decl (type, range, choice); else why."""
    kind = decl["type"]
    if kind == "bool":
        return None if text in (b"true", b"false") else "'%s' is not true or false" % _text(text)
    if kind in ("int", "float"):
        value = _parse_i64(text) if kind == "int" else _parse_f64(text)
        if value is None:
            return "'%s' is not %s" % (_text(text), "an integer" if kind == "int" else "a number")
        shown = "%d" if kind == "int" else "%.17g"
        if "min" in decl and value < decl["min"]:
            return (shown + " is below min " + shown) % (value, decl["min"])
        if "max" in decl and value > decl["max"]:
            return (shown + " is above max " + shown) % (value, decl["max"])
        return None
    if kind == "string":
        return None if len(text) <= 255 and all(c >= 0x20 and c != 0x7F for c in text) else \
            "a string is at most 255 printable characters"
    if text in decl["choices"]:
        return None
    return "'%s' is not one of %s" % (_text(text), ", ".join(_text(c) for c in decl["choices"]))


class _Lexer:
    def __init__(self, s):
        self.s, self.p, self.bad = s, 0, False

    def skip(self):
        while self.p < len(self.s) and self.s[self.p] in SPACE:
            self.p += 1

    def next(self):
        """(token, quoted), or None at the end or on a malformed quote (then bad is set)."""
        s = self.s
        self.skip()
        if self.p >= len(s):
            return None
        if s[self.p] == 0x22:
            close = s.find(b'"', self.p + 1)
            if close < 0:
                self.bad = True
                return None
            token = s[self.p + 1:close]
            self.p = close + 1
            if self.p < len(s) and s[self.p] not in SPACE:
                self.bad = True
                return None
            return token, True
        begin = self.p
        while self.p < len(s) and s[self.p] not in SPACE:
            if s[self.p] == 0x22:
                self.bad = True
                return None
            self.p += 1
        return s[begin:self.p], False


def _setting_line(line, settings, number):
    eq = line.find(b"=")
    if eq < 0:
        _bad(number, "expected name = type ...")
    name = _trim(line[:eq])
    if not _valid_word(name, 31):
        _bad(number, "setting name must be 1-31 of [a-z0-9_]")

    def refuse(what):
        raise ManifestRefused("plugin.ini line %d: setting '%s': %s" % (number, _text(name), what), number)

    if any(setting["name"] == _text(name) for setting in settings):
        refuse("declared twice")
    if len(settings) == 32:
        refuse("more than 32 settings")
    lexer = _Lexer(line[eq + 1:])
    lexer.skip()
    if lexer.s.startswith(b"enum(", lexer.p):
        close = lexer.s.find(b")", lexer.p)
        if close < 0:
            refuse("enum( needs a closing )")
        kind = lexer.s[lexer.p:close + 1]
        lexer.p = close + 1
        if lexer.p < len(lexer.s) and lexer.s[lexer.p] not in SPACE:
            refuse("expected a space after the type")
    else:
        token = lexer.next()
        if token is None or token[1]:
            refuse("missing type (bool, int, float, string or enum(a,b,c))")
        kind = token[0]
    decl = {"choices": []}
    if kind in (b"bool", b"int", b"float", b"string"):
        decl["type"] = kind.decode("ascii")
    elif kind.startswith(b"enum("):
        decl["type"] = "enum"
        for choice in kind[5:-1].split(b","):
            choice = _trim(choice)
            if not _valid_word(choice, 31):
                refuse("enum choices are a comma list of 1-31 of [a-z0-9_]")
            if choice in decl["choices"]:
                refuse("enum lists a choice twice")
            if len(decl["choices"]) == 16:
                refuse("enum lists more than 16 choices")
            decl["choices"].append(choice)
    else:
        refuse("type must be bool, int, float, string or enum(a,b,c)")
    have = {}
    while True:
        key = lexer.next()
        if key is None:
            break
        if key[1] or key[0] not in SETTING_FIELDS:
            refuse("unknown field '%s' (default, min, max, label, help)" % _text(key[0]))
        field = key[0].decode("ascii")
        if field in have:
            refuse("field '%s' given twice" % field)
        value = lexer.next()
        if value is None:
            if lexer.bad:
                break
            refuse("field '%s' needs a value" % field)
        have[field] = value[0]
    if lexer.bad:
        refuse("a quote is not closed, or not at the start of a value")
    if "min" in have or "max" in have:
        if decl["type"] not in ("int", "float"):
            refuse("min and max apply to int and float only")
        probe = dict(decl)  # bounds are values of the type, checked before any bound exists
        for field in ("min", "max"):
            if field in have:
                why = _check_setting(probe, have[field])
                if why:
                    refuse(field + " " + why)
                decl[field] = (_parse_i64 if decl["type"] == "int" else _parse_f64)(have[field])
        if "min" in decl and "max" in decl and decl["min"] > decl["max"]:
            refuse("min %s is above max %s" % (_text(have["min"]), _text(have["max"])))
    if "label" in have and not _valid_text(have["label"], 63):
        refuse("label must be 1-63 printable characters")
    if "help" in have and not _valid_text(have["help"], 255):
        refuse("help must be 1-255 printable characters")
    if "default" in have:
        why = _check_setting(decl, have["default"])
        if why:
            refuse("default " + why)
    settings.append({"name": _text(name), "type": decl["type"]})


def read_manifest(raw, notes):
    """The parsed manifest, or ManifestRefused at the first failure, as the host's reader stops."""
    if len(raw) > MAX_MANIFEST_BYTES:
        raise ManifestRefused("plugin.ini: too big (over %d bytes)" % MAX_MANIFEST_BYTES)
    if raw.startswith(b"\xef\xbb\xbf"):
        raw = raw[3:]
    lines = raw.split(b"\n")
    if lines[-1] == b"":
        lines.pop()
    values, requires, settings = {}, [], []
    section, seen_settings = 0, False  # 0 top level, 1 [settings], 2 another section
    for number, raw_line in enumerate(lines, 1):
        line = _trim(_strip_comment(raw_line))
        if section == 1:
            line = _trim(_strip_settings_comment(raw_line))  # quotes may hold ';' or '#'
        if not line:
            continue
        if line[:1] == b"[":
            if line[-1:] != b"]":
                _bad(number, "a [section] line ends with ]")
            name = _trim(line[1:-1])
            if name == b"settings":
                if seen_settings:
                    _bad(number, "duplicate section '[settings]'")
                seen_settings, section = True, 1
            else:
                section = 2
                notes.append("plugin.ini line %d: section '[%s]' skipped (a later minor's)" % (number, _text(name)))
            continue
        if section == 1:
            _setting_line(line, settings, number)
            continue
        if section == 2:
            continue
        eq = line.find(b"=")
        if eq < 0:
            _bad(number, "expected key = value")
        key, value = _trim(line[:eq]), _trim(line[eq + 1:])
        if key not in MANIFEST_KEYS:
            notes.append("plugin.ini line %d: unknown key '%s' ignored (a later minor's)" % (number, _text(key)))
            continue
        name = key.decode("ascii")
        if name in values:
            _bad(number, "duplicate key '%s'" % name)
        if key == b"id":
            if not _valid_word(value, 31):
                _bad(number, "id must be 1-31 of [a-z0-9_]")
            if value in (b"sco", b"host", b"menu", b"game"):
                _bad(number, "id is reserved")
        elif key in (b"name", b"author") and not _valid_text(value, 63):
            _bad(number, name + " must be 1-63 printable characters")
        elif key == b"version" and not _valid_text(value, 31):
            _bad(number, "version must be 1-31 printable characters")
        elif key == b"api" and _parse_api(value) is None:
            _bad(number, "api must be <major>.<minor>")
        elif key == b"kind" and value not in (b"native", b"lua", b"data"):
            _bad(number, "kind must be native, lua or data")
        elif key == b"entry" and not _valid_entry(value):
            _bad(number, "entry must be a file name in the plugin folder")
        elif key == b"requires":
            for capability in value.split(b","):
                capability = _trim(capability)
                if not _valid_capability(capability):
                    _bad(number, "requires must be a comma list of capability names")
                if _text(capability) in requires:
                    _bad(number, "requires lists a capability twice")
                if len(requires) == 16:
                    _bad(number, "requires lists more than 16 capabilities")
                requires.append(_text(capability))
        values[name] = _text(value)
    for name in ("id", "name", "version", "api", "kind"):
        if name not in values:
            raise ManifestRefused("plugin.ini: missing key '%s'" % name)
    if values["kind"] == "data" and "entry" in values:
        raise ManifestRefused("plugin.ini: a data pack has no entry")
    if values["kind"] != "data" and "entry" not in values:
        raise ManifestRefused("plugin.ini: missing key 'entry'")
    if values["kind"] == "data" and settings:
        raise ManifestRefused("plugin.ini: a data pack has no settings")
    for capability in requires:
        if capability == values["id"] or capability.startswith(values["id"] + "."):
            raise ManifestRefused("plugin.ini: requires '%s' names the plugin itself (a requires cycle)" % capability)
    major, minor = _parse_api(values["api"].encode("utf-8"))
    if major != SCO_API_VERSION[0] or minor > SCO_API_VERSION[1]:
        raise ManifestRefused("plugin.ini: api %s, but this SDK is %d.%d" % ((values["api"],) + SCO_API_VERSION))
    return {**values, "requires": requires, "settings": settings}


def check_manifest(text, folder):
    findings, notes, parsed = [], [], None
    try:
        parsed = read_manifest(text.encode("utf-8"), notes)
    except UnicodeEncodeError:
        findings.append({"line": None, "message": "plugin.ini text has a lone surrogate; it is not UTF-8"})
    except ManifestRefused as error:
        findings.append({"line": error.line, "message": str(error)})
    if parsed is not None and folder is not None and folder != parsed["id"]:
        findings.append({"line": None, "message": "id '%s' does not match folder '%s'" % (parsed["id"], folder)})
    return {"valid": not findings, "findings": findings, "notes": notes, "parsed": parsed,
            "folder_checked": folder is not None, "sdk_api": "%d.%d" % SCO_API_VERSION}


# ---- sco_widget_node trees: the include/sco_widgets.h invariants ----
WIDGET = {name: number for number, name in enumerate(WIDGET_KIND_NAMES, 1)}
WIDGET_DISABLED, WIDGET_PASSWORD = 1, 2


def widget_node_shape(index, node):
    """Refuse a node outside the advertised item schema; semantic problems are findings instead."""
    where = "widgets[%d]" % index
    properties = WIDGET_NODE["properties"]
    if not isinstance(node, dict) or set(node) - set(properties) or not {"kind", "id"} <= set(node):
        raise ToolError(where + " must be an object with kind and id and only advertised keys; consult tools/list inputSchema")
    for key, value in node.items():
        if key == "kind":
            ok = (type(value) is int and 0 <= value <= 0xFFFFFFFF) or (isinstance(value, str) and value in WIDGET)
        elif key in ("value", "minimum", "maximum"):
            ok = type(value) in (int, float)
        elif key in ("label", "text"):
            ok = value is None or (isinstance(value, str) and len(value) <= 1024)
        elif key == "items":
            ok = value is None or (isinstance(value, list) and len(value) <= 1024
                                   and all(isinstance(item, str) and len(item) <= 1024 for item in value))
        else:
            ok = type(value) is int and properties[key]["minimum"] <= value <= properties[key]["maximum"]
        if not ok:
            raise ToolError("%s.%s does not match the advertised schema" % (where, key))


def widget_text_problem(value, name, required):
    """(message, result) for one string field, or None."""
    if value is None:
        return (name + " is required: interactive nodes need a nonempty label (accessible name)", "BAD_ARG") if required else None
    try:
        raw = value.encode("utf-8")
    except UnicodeEncodeError:
        return name + " is not valid UTF-8", "BAD_ARG"
    if b"\x00" in raw:
        return name + " contains NUL; a C string ends there", "BAD_ARG"
    if len(raw) > 255:
        return "%s is %d bytes; at most 255" % (name, len(raw)), "TOO_MANY"
    if required and not raw:
        return name + " must be nonempty on an interactive node (accessible name)", "BAD_ARG"
    return None


def check_widgets(nodes):
    """Every invariant violation in a node array; the host refuses the panel at the first one."""
    findings, seen, open_layouts = [], set(), []
    for index, node in enumerate(nodes):
        def find(message, result="BAD_ARG"):
            findings.append({"node": index, "id": node["id"], "result": result, "message": message})
        kind = WIDGET[node["kind"]] if isinstance(node["kind"], str) else node["kind"]
        identifier, parent, flags = node["id"], node.get("parent", 0), node.get("flags", 0)
        value, minimum, maximum = node.get("value", 0), node.get("minimum", 0), node.get("maximum", 0)
        selected, columns, items = node.get("index", 0), node.get("columns", 0), node.get("items") or []
        if not WIDGET["label"] <= kind <= WIDGET["column"]:
            find("kind %d is not 1 (label) to 10 (column)" % kind)
        if not identifier:
            find("id must be nonzero")
        elif identifier in seen:
            find("id %d repeats an earlier node's id; ids are unique within a panel" % identifier)
        seen.add(identifier)
        if flags & ~(WIDGET_DISABLED | WIDGET_PASSWORD):
            find("flags has bits other than DISABLED (1) and PASSWORD (2)")
        if flags & WIDGET_PASSWORD and kind != WIDGET["text_input"]:
            find("PASSWORD applies to text_input only")
        if not parent:
            open_layouts.clear()
        elif parent in open_layouts:
            del open_layouts[open_layouts.index(parent) + 1:]
        else:
            find("parent %d is not an open row/column ancestor: a parent precedes its children, "
                 "is a row or column, and each subtree is contiguous in preorder" % parent)
        interactive = WIDGET["button"] <= kind <= WIDGET["text_input"]
        for problem in (widget_text_problem(node.get("label"), "label", interactive),
                        widget_text_problem(node.get("text"), "text", False) if kind == WIDGET["text_input"] else None):
            if problem:
                find(*problem)
        if kind != WIDGET["text_input"] and node.get("text") is not None:
            find("text applies to text_input only; leave it null")
        if kind not in (WIDGET["toggle"], WIDGET["slider"]) and value != 0:
            find("value applies to toggle and slider only; leave it 0")
        if kind != WIDGET["slider"] and (minimum != 0 or maximum != 0):
            find("minimum and maximum apply to slider only; leave them 0")
        if kind not in (WIDGET["combo"], WIDGET["list"]) and selected not in (0, -1):
            find("index applies to combo and list only; leave it 0 or -1")
        if kind != WIDGET["table"] and columns:
            find("columns applies to table only; leave it 0")
        if kind not in (WIDGET["combo"], WIDGET["list"], WIDGET["table"]) and items:
            find("items apply to combo, list and table only; leave them null")
        if kind == WIDGET["toggle"] and value not in (0, 1):
            find("a toggle value is 0 or 1")
        if kind == WIDGET["slider"] and not minimum <= value <= maximum:
            find("a slider needs minimum <= value <= maximum")
        if len(items) > 256:
            find("%d items; at most 256" % len(items), "TOO_MANY")
        if columns > 16:
            find("%d columns; at most 16" % columns, "TOO_MANY")
        if kind in (WIDGET["combo"], WIDGET["list"]) and not -1 <= selected < len(items):
            find("index %d is not -1 or an item index (%d items)" % (selected, len(items)))
        if kind == WIDGET["table"] and (not columns or len(items) % columns):
            find("a table needs columns > 0 and an item count divisible by columns (row-major cells)")
        for position, item in enumerate(items):
            problem = widget_text_problem(item, "items[%d]" % position, False)
            if problem:
                find(*problem)
        if kind in (WIDGET["row"], WIDGET["column"]):
            open_layouts.append(identifier)
    return {"valid": not findings, "node_count": len(nodes), "findings": findings}


VALIDATION_CONCLUSIONS = [
    "Manifest rules are sco-plugin-check's read_manifest (the host's manifest.cpp and settings_ini.cpp): the reader stops at the first failure, so fix and re-check. "
    "It does not check that entry exists, that required capabilities have providers, or load the plugin.",
    "Widget findings restate the sco_widgets.h invariants; the host refuses the whole panel at its first violation. "
    "Panel id/title, node_stride, size fields, pointer readability, renderer markup in labels and the game thread are not checked.",
    "Pure validation of the supplied text: no file is read or written and nothing is executed."]


# ---- opt-in development tools: operator programs, fixed arguments, folders inside --project-root ----
_KERNEL32 = None


def _kernel32():
    """kernel32 with typed job-object calls; loaded only when a development tool first runs on Windows."""
    global _KERNEL32
    if _KERNEL32 is None:
        import ctypes
        from ctypes import wintypes
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        for name, argtypes, restype in (
                ("CreateJobObjectW", (wintypes.LPVOID, wintypes.LPCWSTR), wintypes.HANDLE),
                ("OpenProcess", (wintypes.DWORD, wintypes.BOOL, wintypes.DWORD), wintypes.HANDLE),
                ("AssignProcessToJobObject", (wintypes.HANDLE, wintypes.HANDLE), wintypes.BOOL),
                ("TerminateJobObject", (wintypes.HANDLE, wintypes.UINT), wintypes.BOOL),
                ("CloseHandle", (wintypes.HANDLE,), wintypes.BOOL)):
            function = getattr(kernel32, name)
            function.argtypes, function.restype = argtypes, restype
        _KERNEL32 = kernel32
    return _KERNEL32


def _job_for(pid):
    """A Windows job object holding the process, so a timeout also ends descendants that outlive their parent."""
    try:
        kernel32 = _kernel32()
        job = kernel32.CreateJobObjectW(None, None)
        if not job:
            return None
        handle = kernel32.OpenProcess(0x0100 | 0x0001, False, pid)  # PROCESS_SET_QUOTA | PROCESS_TERMINATE
        assigned = bool(handle) and bool(kernel32.AssignProcessToJobObject(job, handle))
        if handle:
            kernel32.CloseHandle(handle)
        if not assigned:
            kernel32.CloseHandle(job)
            return None
        return job
    except (OSError, AttributeError):
        return None


def kill_tree(process, job):
    """Kill the process and its descendants: taskkill /T and the job object on Windows, the process group elsewhere."""
    if os.name == "nt":
        taskkill = os.path.join(os.environ.get("SystemRoot", "C:\\Windows"), "System32", "taskkill.exe")
        try:
            subprocess.run([taskkill, "/F", "/T", "/PID", str(process.pid)], stdin=subprocess.DEVNULL,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=15,
                           creationflags=subprocess.CREATE_NO_WINDOW)
        except (OSError, subprocess.SubprocessError):
            pass
        if job:
            _kernel32().TerminateJobObject(job, 1)
    else:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except OSError:
            pass
    try:
        process.kill()
    except OSError:
        pass


def run_bounded(argv, cwd, timeout, limit, env=None, summary=None, finding_prefix=None):
    """Run argv without a shell, stdin closed, in its own process group; bounded time and output.

    Returns the exit code, the last `limit` bytes of combined stdout/stderr, and, read from the
    whole stream, lines starting with finding_prefix and the last line matching summary.
    """
    flags = ({"creationflags": subprocess.CREATE_NEW_PROCESS_GROUP | subprocess.CREATE_NO_WINDOW}
             if os.name == "nt" else {"start_new_session": True})
    started = time.monotonic()
    try:
        process = subprocess.Popen(argv, cwd=cwd, env=env, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, **flags)
    except OSError as error:
        return {"state": "not_started", "reason": "the program could not be started: " + type(error).__name__,
                "exit_code": None, "timed_out": False}
    job = _job_for(process.pid) if os.name == "nt" else None
    lock = threading.Lock()
    tail, pending = bytearray(), bytearray()
    seen = {"bytes": 0, "findings": [], "summary": None}

    def take(line):
        text = bytes(line[:DEV_LINE]).decode("utf-8", errors="replace").rstrip("\r")
        if finding_prefix and text.startswith(finding_prefix) and len(seen["findings"]) < DEV_FINDINGS:
            seen["findings"].append(text[len(finding_prefix):])
        if summary and summary.match(text.strip()):
            seen["summary"] = text.strip()

    def read():
        while True:
            try:
                chunk = process.stdout.read1(65536)
            except (OSError, ValueError):
                break
            if not chunk:
                break
            with lock:
                seen["bytes"] += len(chunk)
                tail.extend(chunk)
                if len(tail) > limit:
                    del tail[:len(tail) - limit]
                pending.extend(chunk)
                while True:
                    end = pending.find(b"\n")
                    if end < 0:
                        break
                    take(pending[:end])
                    del pending[:end + 1]
                if len(pending) > 2 * DEV_LINE:  # one very long line: keep its head only
                    del pending[DEV_LINE:]
        with lock:
            if pending:
                take(pending)
                pending.clear()

    reader = threading.Thread(target=read, daemon=True)
    reader.start()
    timed_out = False
    try:
        exit_code = process.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        timed_out = True
        kill_tree(process, job)
        try:
            exit_code = process.wait(timeout=15)
        except subprocess.TimeoutExpired:
            exit_code = None
    finally:
        if job:
            _kernel32().CloseHandle(job)
    reader.join(timeout=5)
    complete = not reader.is_alive()
    if complete:
        process.stdout.close()
    with lock:
        output = bytes(tail).decode("utf-8", errors="replace").replace("\r\n", "\n")
        result = {"state": "timed_out" if timed_out else "completed", "exit_code": exit_code, "timed_out": timed_out,
                  "duration_seconds": round(time.monotonic() - started, 3), "output": output,
                  "output_bytes": seen["bytes"], "output_truncated": seen["bytes"] > len(tail),
                  "output_complete": complete, "output_limit": limit}
        if finding_prefix:
            result["findings"] = list(seen["findings"])
        if summary:
            result["summary"] = seen["summary"]
    return result


def within(path, root):
    path, root = os.path.normcase(str(path)), os.path.normcase(str(root))
    try:
        return os.path.commonpath([path, root]) == root
    except ValueError:  # different drives, or a mix of absolute and relative
        return False


def launch(program):
    """argv prefix for an operator-configured program; a .py wrapper runs with this interpreter."""
    return [sys.executable, program] if program.lower().endswith(".py") else [program]


class DevTools:
    """Operator configuration for the opt-in development tools. Callers supply only a folder and a mode."""

    def __init__(self, project_roots, repo_root=None, plugin_check=None, cmake=None, ctest=None, generator=None,
                 platform=None, sco_sdk=None, timeout=DEV_TIMEOUT, output_limit=DEV_OUTPUT):
        require(project_roots, "--enable-dev-tools needs at least one --project-root")
        self.roots = []
        for root in project_roots:
            configured = Path(os.path.abspath(root))
            resolved = configured.resolve(strict=True)
            require(resolved.is_dir(), "--project-root is not a directory: " + str(configured))
            self.roots.append((configured, resolved))
        require(type(timeout) is int and DEV_TIMEOUT_RANGE[0] <= timeout <= DEV_TIMEOUT_RANGE[1],
                "--dev-timeout must be %d-%d seconds" % DEV_TIMEOUT_RANGE)
        require(type(output_limit) is int and DEV_OUTPUT_RANGE[0] <= output_limit <= DEV_OUTPUT_RANGE[1],
                "--dev-output-limit must be %d-%d bytes" % DEV_OUTPUT_RANGE)
        for name, value in (("--cmake-generator", generator), ("--cmake-platform", platform)):
            require(value is None or (nonempty(value) and len(value) <= 128 and "\x00" not in value), name + " must be short text")
        self.timeout, self.output_limit = timeout, output_limit
        self.generator, self.platform = generator, platform
        self.repo_root = Path(os.path.abspath(repo_root)) if repo_root else None
        self.plugin_check = os.path.abspath(plugin_check) if plugin_check else None
        self.cmake = os.path.abspath(cmake) if cmake else None
        self.ctest = os.path.abspath(ctest) if ctest else None
        self.sco_sdk = Path(os.path.abspath(sco_sdk)) if sco_sdk else None

    # Programs are found at call time, so a checker built after the server started is picked up.
    def sdk(self):
        if self.sco_sdk is not None:
            candidates = [self.sco_sdk]
        elif self.repo_root is not None:
            candidates = [self.repo_root, self.repo_root / "sdk"]
        else:
            return None, "No --sco-sdk or --repo-root: the SDK folder passed as SCO_SDK is not configured"
        for candidate in candidates:
            if (candidate / "cmake" / "sco-plugin.cmake").is_file():
                return str(candidate), None
        return None, "No cmake/sco-plugin.cmake in the configured SDK folder (--sco-sdk, else --repo-root or its sdk/)"

    def checker(self):
        if self.plugin_check is not None:
            if os.path.isfile(self.plugin_check):
                return self.plugin_check, None
            return None, "--plugin-check names no file"
        sdk, _ = self.sdk()
        if sdk is not None:
            for folder in ("bin", os.path.join("out", "bin")):
                candidate = os.path.join(sdk, folder, "sco-plugin-check" + DEV_EXE)
                if os.path.isfile(candidate):
                    return candidate, None
        return None, ("No --plugin-check, and no built sco-plugin-check in the SDK folder's bin/ or out/bin/ "
                      "(cmake --install <build> --prefix out installs it there)")

    def cmake_program(self):
        if self.cmake is not None:
            return (self.cmake, None) if os.path.isfile(self.cmake) else (None, "--cmake names no file")
        found = shutil.which("cmake")
        return (found, None) if found else (None, "No --cmake and no cmake on the server's PATH")

    def ctest_program(self):
        if self.ctest is not None:
            return (self.ctest, None) if os.path.isfile(self.ctest) else (None, "--ctest names no file")
        cmake, _ = self.cmake_program()
        if cmake is not None:
            sibling = Path(cmake).with_name("ctest" + Path(cmake).suffix)
            if sibling.is_file():
                return str(sibling), None
        found = shutil.which("ctest")
        return (found, None) if found else (None, "No --ctest, no ctest beside cmake and none on the server's PATH")

    def contained(self, value, what):
        """Resolve a caller path; refuse it unless it is an existing folder inside a project root.

        A relative path is taken from the first --project-root. The path is checked lexically before
        the filesystem is touched, then again after resolving symlinks, junctions and .. components.
        Outside, missing and not-a-folder get the same refusal, so nothing is learned about other paths.
        """
        refusal = what + " must be an existing folder inside a --project-root (after resolving symlinks and ..)"
        if "\x00" in value or (os.name == "nt" and ":" in value[2:]):
            raise ToolError(refusal)
        candidate = os.path.normpath(os.path.join(str(self.roots[0][0]), value))
        if not any(within(candidate, root) for pair in self.roots for root in pair):
            raise ToolError(refusal)
        try:
            resolved = Path(candidate).resolve(strict=True)
            inside = any(within(resolved, root) for _, root in self.roots) and resolved.is_dir()
        except (OSError, RuntimeError, ValueError):
            inside = False
        if not inside:
            raise ToolError(refusal)
        return resolved

    def project(self, value):
        project = self.contained(value, "project")
        if not (project / "CMakeLists.txt").is_file():
            raise ToolError("project has no CMakeLists.txt")
        build = project / "build"
        if os.path.lexists(build):
            try:
                resolved = build.resolve(strict=True)
                inside = any(within(resolved, root) for _, root in self.roots) and resolved.is_dir()
            except (OSError, RuntimeError, ValueError):
                inside = False
            if not inside:
                raise ToolError("project's build entry must be a folder inside a --project-root")
            build = resolved
        return project, build

    def run(self, program, arguments, cwd, timeout=None, **kwargs):
        return run_bounded(launch(program) + arguments, str(cwd), self.timeout if timeout is None else timeout,
                           self.output_limit, **kwargs)

    def run_plugin_check(self, args):
        folder, mode = self.contained(args["folder"], "folder"), args.get("mode", "full")
        result = {"tool": "sco-plugin-check", "folder": str(folder), "mode": mode, "passed": False,
                  "conclusions": DEV_CONCLUSIONS}
        program, reason = self.checker()
        if program is None:
            return result | {"state": "unavailable", "reason": reason}
        arguments = [str(folder)] + (["--short-tables"] if mode == "short" else [])
        run = self.run(program, arguments, folder, summary=CHECK_SUMMARY, finding_prefix="FAIL ")
        return result | {"program": Path(program).name, "arguments": arguments} | run | {
            "passed": run["state"] == "completed" and run["exit_code"] == 0}

    def build_plugin_project(self, args):
        project, build = self.project(args["project"])
        result = {"tool": "cmake", "project": str(project), "build_dir": str(build), "config": DEV_BUILD_CONFIG,
                  "passed": False, "steps": [], "conclusions": DEV_CONCLUSIONS}
        cmake, reason = self.cmake_program()
        sdk, sdk_reason = self.sdk()
        if cmake is None or sdk is None:
            return result | {"state": "unavailable", "reason": reason or sdk_reason, "exit_code": None}
        configure = (["-S", str(project), "-B", str(build)] + (["-G", self.generator] if self.generator else [])
                     + (["-A", self.platform] if self.platform else []) + ["-DSCO_SDK=" + sdk])
        environment = dict(os.environ, MSBUILDDISABLENODEREUSE="1")  # no MSBuild node outlives the call
        deadline = time.monotonic() + self.timeout
        steps = [{"step": "configure", "arguments": configure} | self.run(cmake, configure, project, env=environment)]
        if steps[0]["state"] == "completed" and steps[0]["exit_code"] == 0:
            arguments = ["--build", str(build), "--config", DEV_BUILD_CONFIG]
            remaining = max(deadline - time.monotonic(), 0.001)
            steps.append({"step": "build", "arguments": arguments}
                         | self.run(cmake, arguments, project, timeout=remaining, env=environment))
        last = steps[-1]
        return result | {"program": Path(cmake).name, "state": last["state"], "exit_code": last["exit_code"], "steps": steps,
                         "passed": len(steps) == 2 and all(s["state"] == "completed" and s["exit_code"] == 0 for s in steps)}

    def test_plugin_project(self, args):
        project, build = self.project(args["project"])
        result = {"tool": "ctest", "project": str(project), "build_dir": str(build), "config": DEV_BUILD_CONFIG,
                  "passed": False, "conclusions": DEV_CONCLUSIONS}
        if not (build / "CMakeCache.txt").is_file():
            raise ToolError("project is not configured: no build/CMakeCache.txt; run build_plugin_project first")
        ctest, reason = self.ctest_program()
        if ctest is None:
            return result | {"state": "unavailable", "reason": reason}
        arguments = ["--test-dir", str(build), "-C", DEV_BUILD_CONFIG, "--output-on-failure", "--no-tests=error"]
        run = self.run(ctest, arguments, project, summary=CTEST_SUMMARY)
        return result | {"program": Path(ctest).name, "arguments": arguments} | run | {
            "passed": run["state"] == "completed" and run["exit_code"] == 0}


def aliases(record):
    # Schema 1 has no aliases field: derive searchable names from source evidence,
    # retaining the full original mapping evidence in get_symbol.
    result = {record["symbol"].rsplit(".", 1)[-1], record["id"].replace("-", "_")}
    for mapping in record["language_mappings"].values():
        for evidence in mapping["evidence"]:
            result.update(re.findall(r"\b([A-Za-z_]\w*)\s*\(", evidence["token"]))
    return " ".join(sorted(result)).casefold()


class Server:
    def __init__(self, catalogue, repo_root=None, extended_tools=False, guides_path=None, guides_explicit=False,
                 dev_tools=None):
        self.catalogue = catalogue
        # sdk-guides.json beside the catalogue (archive mode) or an explicit --guides file.
        self.guides_path, self.guides_explicit = guides_path, guides_explicit
        self.guides = None
        self.guide_words = None
        self.tools = (TOOLS + EXTENDED_TOOLS if extended_tools else TOOLS) + (DEV_TOOLS if dev_tools else [])
        self.dev = dev_tools
        self.enabled = {definition["name"] for definition in self.tools}
        self.provenance = {key: catalogue[key] for key in PROVENANCE_KEYS}
        self.repo_root = Path(os.path.abspath(repo_root)) if repo_root is not None else None
        self.records = {key: record for record in catalogue["records"] for key in (record["id"], record["symbol"])}
        self.search_fields = [(r, [(r["symbol"].casefold(), 100), (r["id"].casefold(), 90),
                                  (r["service"].casefold(), 50), (aliases(r), 40), (r["purpose"].casefold(), 10)])
                              for r in catalogue["records"]]
        self.state = "new"
        self.calls = deque()
        self.game_status, self.game_status_source, self.game_status_reason = self.load_game_status()
        self.natives = self.natives_by_key = None
        self.natives_reason = "Not loaded"
        if extended_tools:
            self.natives, self.natives_by_key, self.natives_reason = self.load_natives()
        self.scaffolder = None

    def load_scaffolder(self):
        """Return ((render_plugin, SpecError), None) or (None, reason).

        The source is read with the examples' no-symlink containment and executed from
        those exact bytes, so nothing else is opened and no __pycache__ is written.
        """
        if self.repo_root is None:
            return None, "No --repo-root: sdk/tools/sco-new-plugin.py is not loaded"
        source_name = "/".join(SCAFFOLDER)
        try:
            source = read_recorded_example(self.repo_root, SCAFFOLDER)
        except (OSError, UnicodeError, ToolError):
            return None, source_name + " unavailable beneath --repo-root (missing, symlink, not a regular file or over 256 KiB)"
        module = importlib.util.module_from_spec(importlib.machinery.ModuleSpec("sco_new_plugin", None, origin=source_name))
        try:
            exec(compile(source, source_name, "exec"), module.__dict__)
        except Exception as error:  # Repository code: report any load failure as unavailable, keep serving.
            return None, source_name + " failed to load: " + type(error).__name__
        render, spec_error = getattr(module, "render_plugin", None), getattr(module, "SpecError", None)
        if not callable(render) or not (isinstance(spec_error, type) and issubclass(spec_error, Exception)):
            return None, source_name + " defines no render_plugin and SpecError"
        return (render, spec_error), None

    def scaffold(self, args):
        if self.scaffolder is None:
            self.scaffolder = self.load_scaffolder()
        loaded, reason = self.scaffolder
        result = {"source": "/".join(SCAFFOLDER), "written": False, "conclusions": scaffold_conclusions(args.get("lang"))}
        if loaded is None:
            return result | {"state": "unavailable", "reason": reason, "files": {}}
        render, spec_error = loaded
        try:
            files = render(dict(args))
        except spec_error as error:
            raise ToolError("Spec refused by sco-new-plugin: " + str(error)) from None
        return result | {"state": "available", "spec": args, "files": files}

    def load_guides(self):
        """Return ((document, sections by (doc, anchor), context), None) or (None, reason).

        sdk-guides.json beside the catalogue, or the --guides file, is preferred; one that
        exists but is refused is reported, not bypassed. Without one, --repo-root's
        tools/sdk_guides.py is read with the examples' no-symlink containment and executed
        from those bytes, in-process, to build the sections in memory from that root.
        """
        path = self.guides_path
        if path is not None and (self.guides_explicit or os.path.lexists(path)):
            name = "--guides file" if self.guides_explicit else GUIDES_FILE + " beside the catalogue"
            try:
                descriptor = os.open(path, os.O_RDONLY | getattr(os, "O_NONBLOCK", 0))
                with os.fdopen(descriptor, "rb") as stream:
                    if not stat.S_ISREG(os.fstat(stream.fileno()).st_mode):
                        return None, name + " is not a regular file"
                    raw = stream.read(MAX_GUIDES + 1)
            except OSError as error:
                return None, name + " unreadable: " + type(error).__name__
            if len(raw) > MAX_GUIDES:
                return None, name + " exceeds 16 MiB"
            try:
                data, by_key = parse_guides(parse_json(raw.decode("utf-8")))
            except (ValueError, UnicodeError, TypeError, RecursionError) as error:
                return None, name + " refused: " + str(error)
            context = {"mode": "file", "source": name, "source_sha": data["source_sha"],
                       "matches_catalogue_source_sha": data["source_sha"] == self.catalogue["source_sha"]}
            return (data, by_key, context), None
        if self.repo_root is None:
            return None, ("no " + GUIDES_FILE + " beside the catalogue and no --repo-root; build it with "
                          "tools/sdk_guides.py build, or use an SDK kit that ships it")
        source_name = "/".join(GUIDES_BUILDER)
        try:
            source = read_example(self.repo_root, GUIDES_BUILDER)
        except (OSError, UnicodeError, ToolError):
            return None, (source_name + " unavailable beneath --repo-root (missing, symlink, not a regular file or over 256 KiB), "
                          "and no " + GUIDES_FILE + " beside the catalogue")
        module = importlib.util.module_from_spec(importlib.machinery.ModuleSpec("sco_sdk_guides", None, origin=source_name))
        try:
            exec(compile(source, source_name, "exec"), module.__dict__)
            docs, sections = module.sections(self.repo_root)
            sdk_version = read_example(self.repo_root, ("sdk", "VERSION")).strip()
            data, by_key = parse_guides({"schema_version": 1, "sdk_version": sdk_version, "source_sha": None,
                                         "source": module.ALLOWLIST, "text_limit": module.TEXT_LIMIT,
                                         "docs": docs, "sections": sections})
        except OSError as error:  # Filesystem messages can carry absolute host paths.
            return None, source_name + " could not read its inputs: " + type(error).__name__
        except Exception as error:  # Repository code: report any failure as unavailable, keep serving.
            return None, source_name + " failed: " + type(error).__name__ + ": " + str(error)[:300]
        context = {"mode": "source", "source": source_name + " beneath --repo-root, built in memory on first use",
                   "source_sha": None, "matches_catalogue_source_sha": None}
        return (data, by_key, context), None

    def guide_data(self):
        if self.guides is None:
            self.guides = self.load_guides()
        loaded, reason = self.guides
        if loaded is None:
            raise ToolError("SDK guides unavailable: " + reason)
        return loaded

    def search_guides(self, args):
        data, _, context = self.guide_data()
        if "doc" in args and not any(doc["doc"] == args["doc"] for doc in data["docs"]):
            raise ToolError("Unknown doc; known docs: " + ", ".join(sorted(doc["doc"] for doc in data["docs"])))
        terms = guide_terms(args["query"])
        if not terms:
            raise ToolError("query has no searchable words")
        if self.guide_words is None:
            self.guide_words = [(guide_index(section["heading"]), guide_index(section["title"] + " " + section["doc"])[0],
                                 guide_index(section["text"])) for section in data["sections"]]
        phrase = " " + " ".join(terms) + " "
        weights = []
        for (heading, _), title, (text, _) in self.guide_words:
            weights.append([(6 if guide_hits(heading, term) else 0) + (3 if guide_hits(title, term) else 0)
                            + min(guide_hits(text, term), 5) for term in terms])
        # A term found in fewer sections counts for more (inverse section frequency over all guides).
        rarity = [math.log(1 + len(weights) / max(1, sum(1 for row in weights if row[index])))
                  for index in range(len(terms))]
        hits = []
        for order, (section, words, row) in enumerate(zip(data["sections"], self.guide_words, weights)):
            if "doc" in args and section["doc"] != args["doc"]:
                continue
            matched = sum(1 for weight in row if weight)
            if not matched:
                continue
            score = sum(weight * factor for weight, factor in zip(row, rarity))
            if len(terms) > 1:
                (_, heading_sequence), _, (_, text_sequence) = words
                score += ((8 if phrase in heading_sequence else 0) + (4 if phrase in text_sequence else 0)) * min(rarity)
            score = round(score, 3)
            hits.append((-score, -matched, order, section, matched, score))
        hits.sort(key=lambda hit: hit[:3])
        start, limit = int(args.get("cursor", "0")), args.get("limit", 5)
        page = [{"doc": section["doc"], "title": section["title"], "heading": section["heading"],
                 "anchor": section["anchor"], "level": section["level"], "url": section["url"],
                 "matched_terms": matched, "score": score, "truncated": section["truncated"],
                 "snippet": guide_snippet(section["text"], terms)}
                for _, _, _, section, matched, score in hits[start:start + limit]]
        return {"query_terms": terms, "sections": page, "total_matches": len(hits), "limit": limit,
                "cursor": str(start), "next_cursor": str(start + limit) if start + limit < len(hits) else None,
                "guides": context, "conclusions": GUIDE_CONCLUSIONS}

    def guide_section(self, args):
        _, by_key, context = self.guide_data()
        section = by_key.get((args["doc"], args["anchor"]))
        if section is None:
            raise ToolError("Unknown guide section; pass a doc and anchor that search_guides returned")
        return {"section": section, "guides": context, "conclusions": GUIDE_CONCLUSIONS}

    def load_game_status(self):
        """Return (rows by catalogue table, source, None) or (None, source, reason).

        sdk/status_registry.json is preferred. docs/game/status.md is read only when the
        registry is absent; a registry that exists but is refused is reported, not bypassed.
        """
        if self.repo_root is None:
            return None, GAME_STATUS, "No --repo-root: sdk/status_registry.json and docs/game/status.md are not read"
        try:
            text = read_recorded_example(self.repo_root, STATUS_REGISTRY, MAX_REGISTRY)
        except FileNotFoundError:
            text = None
        except (OSError, UnicodeError, ToolError):
            return None, STATUS_REGISTRY, "sdk/status_registry.json unreadable beneath --repo-root (symlink, not a regular file or over 1 MiB)"
        if text is not None:
            try:
                return parse_status_registry(text, self.catalogue["tables"]), STATUS_REGISTRY, None
            except (ValueError, TypeError, RecursionError) as error:
                return None, STATUS_REGISTRY, "sdk/status_registry.json refused: " + str(error)
        try:
            text = read_example(self.repo_root, GAME_STATUS)
        except (OSError, UnicodeError, ToolError):
            return None, GAME_STATUS, "docs/game/status.md unavailable beneath --repo-root"
        try:
            return parse_game_status(text, self.catalogue["tables"]), GAME_STATUS, None
        except ValueError as error:
            return None, GAME_STATUS, "docs/game/status.md service table refused: " + str(error)

    def load_natives(self):
        """Return (index, entries by id and hash, None) or (None, None, reason)."""
        if self.repo_root is None:
            return None, None, "No --repo-root: sdk/native_index.json is not read"
        try:
            text = read_recorded_example(self.repo_root, NATIVE_INDEX, MAX_NATIVE_INDEX)
        except (OSError, UnicodeError, ToolError):
            return None, None, "sdk/native_index.json unavailable beneath --repo-root (missing, symlink, not a regular file or over 4 MiB)"
        try:
            index, by_key = parse_native_index(text)
        except (ValueError, TypeError, RecursionError) as error:
            return None, None, "sdk/native_index.json refused: " + str(error)
        return index, by_key, None

    def natives_context(self):
        context = {"source": "/".join(NATIVE_INDEX), "conclusions": NATIVE_CONCLUSIONS}
        if self.natives is None:
            return context | {"state": "unavailable", "reason": self.natives_reason}
        return context | {"state": "available", "row_count": self.natives["row_count"], "tiers": self.natives["tiers"]}

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
        base = {"scope": "service", "source": "/".join(self.game_status_source),
                "snapshot": "Read from --repo-root at server start; not bound to the catalogue source_sha"}
        if self.game_status is None:
            state["service_status"] = {**base, "state": "unavailable", "reason": self.game_status_reason, "rows": []}
        elif record["table"] not in self.game_status:
            state["service_status"] = {**base, "state": "no_rows", "rows": []}
        else:
            state["service_status"] = {**base, "state": "rows_found", **self.game_status[record["table"]]}
        return state

    def stability(self, table, source_header):
        """stable, experimental or unknown for a catalogue table; table is None for a record without one."""
        if not source_header.startswith("include/sc_"):
            return "stable"
        if self.game_status is None or table not in self.catalogue["tables"]:
            return "unknown"
        # A label belongs to the service: a second catalogue table of the same service and header
        # (game.loadout's sc_loadout_v1 and sc_loadout_v1_2) shares the label status rows attribute to the other.
        service = self.catalogue["tables"][table]["service"]
        names = [table] + sorted(name for name, other in self.catalogue["tables"].items()
                                 if name != table and other["service"] == service and other["source_header"] == source_header)
        labels = [self.game_status[name].get("stability") for name in names if name in self.game_status]
        return next((label for label in labels if label), "unknown")

    def stability_context(self):
        context = {"document": "docs/stability.md", "game_source": "/".join(self.game_status_source),
                   "game_state": "unavailable" if self.game_status is None else "read",
                   "conclusions": STABILITY_CONCLUSIONS}
        if self.game_status is None:
            context["reason"] = self.game_status_reason
        return context

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
                                  "size": table["size"], "member_count": len(table["members"]),
                                  "stability": self.stability(name, table["source_header"])}
                                 for name, table in sorted(self.catalogue["tables"].items())],
                    "stability_context": self.stability_context()}
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
        if name == "list_engine_natives":
            result = self.natives_context()
            if self.natives is None:
                return result | {"entries": [], "total_matches": 0, "next_cursor": None}
            query = args["query"].strip().casefold() if "query" in args else None
            hits = [entry for entry in self.natives["entries"]
                    if args.get("subsystem", entry["subsystem"]) == entry["subsystem"]
                    and args.get("tier", entry["tier"]) == entry["tier"]
                    and (query is None or query in " ".join([entry["id"], entry["hash"], entry["subsystem"], entry["source"]]
                                                             + entry.get("capability", [])).casefold())]
            start, limit = int(args.get("cursor", "0")), args.get("limit", 25)
            return result | {"entries": hits[start:start + limit], "total_matches": len(hits), "limit": limit,
                             "cursor": str(start), "next_cursor": str(start + limit) if start + limit < len(hits) else None}
        if name == "get_engine_native":
            result = self.natives_context()
            if self.natives is None:
                return result
            key = args["id_or_hash"]
            if re.fullmatch(r"_0x[0-9A-Fa-f]{16}", key):
                key = "_0x" + key[3:].upper()
            entry = self.natives_by_key.get(key)
            if entry is None:
                raise ToolError("Unknown native row id or hash; use list_engine_natives")
            return result | {"entry": entry, "tier_meaning": self.natives["tiers"][str(entry["tier"])]}
        if name == "generate_plugin_scaffold":
            return self.scaffold(args)
        if name == "search_guides":
            return self.search_guides(args)
        if name == "get_guide_section":
            return self.guide_section(args)
        if name in ("run_plugin_check", "build_plugin_project", "test_plugin_project"):
            return getattr(self.dev, name)(args)
        if name == "validate_manifest_and_widgets":
            result = {"manifest": check_manifest(args["plugin_ini"], args.get("folder"))}
            if "widgets" in args:
                for index, node in enumerate(args["widgets"]):
                    widget_node_shape(index, node)
                result["widgets"] = check_widgets(args["widgets"])
            return result | {"valid": all(part["valid"] for part in result.values()),
                             "conclusions": VALIDATION_CONCLUSIONS}
        record = self.symbol(args["symbol_or_id"])
        if name == "get_symbol":
            return {"record": record, "table_context": self.catalogue["tables"].get(record["table"]),
                    "contract_context": self.catalogue["contract_context"], "evidence_state": self.evidence_state(record)}
        if name == "get_capability_status":
            return ({key: record[key] for key in ("language_mappings", "absent_capability", "limitations")}
                    | {"status_caveat": STATUS_CAVEAT, "evidence_state": self.evidence_state(record),
                       "stability": self.stability(record["table"], record["source_header"]),
                       "stability_context": self.stability_context()})
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
            return {"tools": self.tools}
        name = params.get("name")
        if not isinstance(name, str) or name not in self.enabled:
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
    parser.add_argument("--extended-tools", action="store_true",
                        help="also offer list_engine_natives, get_engine_native, validate_manifest_and_widgets, "
                             "generate_plugin_scaffold, search_guides and get_guide_section")
    parser.add_argument("--guides", help="sdk-guides.json for the guide tools (default: sdk-guides.json beside the "
                                         "catalogue, else built in memory from --repo-root)")
    development = parser.add_argument_group(
        "development tools", "run_plugin_check, build_plugin_project and test_plugin_project run programs on this "
        "machine; they are offered only with --enable-dev-tools and at least one --project-root")
    development.add_argument("--enable-dev-tools", action="store_true")
    development.add_argument("--project-root", action="append", default=[],
                             help="a folder whose plugin folders and projects the tools may use (repeatable)")
    development.add_argument("--plugin-check", help="sco-plugin-check program (default: the SDK folder's bin/ or out/bin/)")
    development.add_argument("--cmake", help="cmake program (default: cmake on PATH)")
    development.add_argument("--ctest", help="ctest program (default: ctest beside cmake, else on PATH)")
    development.add_argument("--cmake-generator", help="passed as -G when configuring (default: cmake's own)")
    development.add_argument("--cmake-platform", help="passed as -A when configuring, e.g. x64 (default: none)")
    development.add_argument("--sco-sdk", help="SDK folder passed as -DSCO_SDK (default: --repo-root, else its sdk/)")
    development.add_argument("--dev-timeout", type=int, default=DEV_TIMEOUT,
                             help="seconds per call before the process tree is killed (default %d)" % DEV_TIMEOUT)
    development.add_argument("--dev-output-limit", type=int, default=DEV_OUTPUT,
                             help="bytes of output tail returned per step (default %d)" % DEV_OUTPUT)
    args = parser.parse_args()
    if not args.catalogue:
        parser.error("--catalogue or SCO_CORE_CATALOGUE is required")
    guides = args.guides or os.path.join(os.path.dirname(os.path.abspath(args.catalogue)), GUIDES_FILE)
    dev_options = ("project_root", "plugin_check", "cmake", "ctest", "cmake_generator", "cmake_platform", "sco_sdk")
    if not args.enable_dev_tools and (any(getattr(args, name) for name in dev_options)
                                      or args.dev_timeout != DEV_TIMEOUT or args.dev_output_limit != DEV_OUTPUT):
        parser.error("development tool options need --enable-dev-tools")
    if args.enable_dev_tools and not args.project_root:
        parser.error("--enable-dev-tools needs at least one --project-root")
    dev_tools = None
    if args.enable_dev_tools:
        try:
            dev_tools = DevTools(args.project_root, args.repo_root, args.plugin_check, args.cmake, args.ctest,
                                 args.cmake_generator, args.cmake_platform, args.sco_sdk, args.dev_timeout,
                                 args.dev_output_limit)
        except (OSError, ValueError, RuntimeError) as error:
            print("sco-core-mcp: development tools refused: " + type(error).__name__ +
                  (": " + str(error) if isinstance(error, ValueError) else ""), file=sys.stderr)
            return 1
    try:
        catalogue = load_catalogue(args.catalogue)
    except (OSError, ValueError, RecursionError) as error:
        print("sco-core-mcp: catalogue refused: " + type(error).__name__ +
              (": " + str(error) if isinstance(error, ValueError) else ""), file=sys.stderr)
        return 1
    try:
        Server(catalogue, args.repo_root, args.extended_tools, guides, args.guides is not None, dev_tools).serve(
            sys.stdin.buffer, sys.stdout.buffer)
    except BrokenPipeError:
        return 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
