#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Local, bounded comparison of explicit SDK requirements with a built catalogue."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import sys
import tempfile
import unicodedata

_SPEC = importlib.util.spec_from_file_location("sco_mcp_validator", Path(__file__).parent / "sco-core-mcp" / "server.py")
_MCP = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(_MCP)
MAX_REQUIREMENTS = 256 * 1024
MAX_ITEMS = 512
ID = re.compile(r"[a-z0-9]+(?:-[a-z0-9]+)*\Z")
SHA = re.compile(r"[0-9a-f]{64}\Z")
SOURCE = re.compile(r"[0-9a-f]{40}\Z")


def read_file(path, limit):
    fd = os.open(path, os.O_RDONLY | getattr(os, "O_NONBLOCK", 0))
    with os.fdopen(fd, "rb") as stream:
        _MCP.require(stat.S_ISREG(os.fstat(stream.fileno()).st_mode), "input must be a regular file")
        raw = stream.read(limit + 1)
    _MCP.require(len(raw) <= limit, "input exceeds size limit")
    return raw


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def public_path(value):
    if (not isinstance(value, str) or not value or "\\" in value
            or re.match(r"^[A-Za-z]:", value)
            or any(unicodedata.category(c) == "Cc" for c in value)):
        return None
    p = PurePosixPath(value)
    if p.is_absolute() or any(x in ("", ".", "..") for x in value.split("/")):
        return None
    return value


def requirements(raw):
    data = _MCP.parse_json(raw.decode("utf-8"))
    _MCP.require(isinstance(data, dict) and set(data) == {"schema_version", "source_sha", "catalogue_sha256", "items"}, "invalid requirements keys")
    _MCP.require(type(data["schema_version"]) is int and data["schema_version"] == 1, "invalid requirements schema")
    _MCP.require(isinstance(data["source_sha"], str) and SOURCE.fullmatch(data["source_sha"]), "invalid expected source SHA")
    _MCP.require(isinstance(data["catalogue_sha256"], str) and SHA.fullmatch(data["catalogue_sha256"]), "invalid expected catalogue SHA-256")
    items = data["items"]
    _MCP.require(isinstance(items, list) and 0 < len(items) <= MAX_ITEMS, "invalid requirements item count")
    seen = set()
    for item in items:
        _MCP.require(isinstance(item, dict) and set(item) in ({"id", "language"}, {"id", "language", "accept_restricted"}), "invalid requirement keys")
        _MCP.require(isinstance(item["id"], str) and len(item["id"]) <= 256 and ID.fullmatch(item["id"]), "invalid requirement ID")
        _MCP.require(item["language"] in ("c", "cpp", "csharp", "lua") if isinstance(item["language"], str) else False, "invalid language")
        _MCP.require("accept_restricted" not in item or type(item["accept_restricted"]) is bool, "invalid restricted acknowledgement")
        key = (item["id"], item["language"])
        _MCP.require(key not in seen, "duplicate requirement")
        seen.add(key)
    return data


def catalogue(raw):
    # Validate precisely the bytes hashed, using the source-tree MCP validator.
    with tempfile.TemporaryDirectory() as directory:
        snapshot = Path(directory) / "catalogue.json"
        snapshot.write_bytes(raw)
        result = _MCP.load_catalogue(snapshot)
    _MCP.require(result["schema_version"] == 2, "catalogue schema 2 required")
    return result


def status_for(record, cat, registry):
    if record["kind"] == "entrypoint" or not record["source_header"].startswith("include/sc_"):
        return {"scope": "not_applicable", "capabilities": []}
    table = cat["tables"][record["table"]]
    version = "%d.%d" % (table["version"] >> 16, table["version"] & 0xffff)
    matches = [e for e in registry["services"] if e["kind"] == "service" and e["service"] == record["service"] and e["header"] == table["source_header"] and e["version"] == version]
    if len(matches) != 1:
        return {"scope": "unestablished", "capabilities": []}
    method = record["symbol"][len(record["table"]) + 1:]
    caps = []
    for cap in matches[0]["capabilities"]:
        if method not in cap.get("methods", []):
            continue
        row = {"name": cap.get("name"), "default": cap["default"], "evidence_level": "unclassified" if cap["evidence_level"] == "in_game_verified" else cap["evidence_level"]}
        path = public_path(cap.get("evidence_source"))
        if path:
            row["evidence_source"] = path
        caps.append(row)
    return {"scope": "explicit_method" if caps else "unestablished", "capabilities": caps}


def make_report(req_raw, cat_raw, reg_raw):
    req = requirements(req_raw)
    _MCP.require(digest(cat_raw) == req["catalogue_sha256"], "catalogue SHA-256 mismatch")
    cat = catalogue(cat_raw)
    _MCP.require(cat["source_sha"] == req["source_sha"], "source SHA mismatch")
    _MCP.require(digest(reg_raw) == cat["status_registry_sha256"], "registry SHA-256 mismatch")
    text = reg_raw.decode("utf-8")
    # Catalogue services activate the shared validator's duplicate-row refusal.
    _MCP.parse_status_registry(text, cat["tables"])
    registry = _MCP.parse_json(text)
    records = {r["id"]: r for r in cat["records"]}
    results = []
    for item in sorted(req["items"], key=lambda i: (i["id"], i["language"])):
        result = {"id": item["id"], "language": item["language"]}
        rec = records.get(item["id"])
        if rec is None:
            result["result"] = "unknown_id"
        else:
            mapping = rec["language_mappings"][item["language"]]
            state = mapping["status"]
            if state == "unsupported":
                outcome = "unsupported"
            elif state == "restricted" and not item.get("accept_restricted", False):
                outcome = "review_required"
            else:
                outcome = "supported"
            result.update({
                "result": outcome,
                "kind": rec["kind"],
                "symbol": rec["symbol"],
                "service": rec["service"],
                "table": rec["table"],
                "table_version": cat["tables"][rec["table"]]["version"] if rec["table"] else None,
                "member_extent": rec["member_extent"],
                "language_status": state,
                "restricted_acknowledged": state == "restricted" and item.get("accept_restricted", False),
                "source_evidence": [
                    {"path": e["path"], "token": e["token"]}
                    for e in mapping["evidence"] if public_path(e["path"])
                ],
                "test_refs": sorted(p for p in mapping.get("tests", []) if public_path(p)),
                "static_status": status_for(rec, cat, registry),
            })
        results.append(result)
    return {
        "schema_version": 1,
        "result": "pass" if all(r["result"] == "supported" for r in results) else "nonpassing",
        "inputs": {
            "requirements_sha256": digest(req_raw),
            "catalogue_sha256": digest(cat_raw),
            "registry_sha256": digest(reg_raw),
            "source_sha": cat["source_sha"],
            "sdk_version": cat["sdk_version"],
            "game_pack_version": cat["game_pack_version"],
        },
        "checks": {
            "runtime": "not_run", "provider": "unestablished",
            "game": "not_run", "release": "unestablished",
        },
        "items": results,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--requirements", required=True)
    parser.add_argument("--catalogue", required=True)
    parser.add_argument("--registry", required=True)
    args = parser.parse_args()
    try:
        report = make_report(read_file(args.requirements, MAX_REQUIREMENTS), read_file(args.catalogue, _MCP.MAX_CATALOGUE), read_file(args.registry, _MCP.MAX_REGISTRY))
    except (OSError, ValueError, UnicodeError, RecursionError) as error:
        print("requirements report refused: " + str(error), file=sys.stderr)
        return 2
    print(json.dumps(report, sort_keys=True, indent=2, ensure_ascii=True, allow_nan=False))
    return 0 if report["result"] == "pass" else 1


if __name__ == "__main__":
    sys.exit(main())
