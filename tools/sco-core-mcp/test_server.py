#!/usr/bin/env python3
"""Black-box stdlib tests for the catalogue-only MCP stdio server.

Run: python3 -m unittest discover -s tools/sco-core-mcp -p test_server.py -v
An actual build is exercised when SCO_CORE_MCP_TEST_CATALOGUE is set, or when
 dist/sdk-catalogue.json exists. That integration test otherwise skips explicitly.
All fixture records are synthetic; none assert runtime or game compatibility.
"""
import copy
import hashlib
import importlib
import importlib.util
import json
import os
from pathlib import Path
import queue
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import unittest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
SERVER = HERE / "server.py"
FIXTURES = HERE / "fixtures"
CATALOGUE = FIXTURES / "catalogue.json"
PROTOCOL = "2025-11-25"
TIMEOUT = 5
TOOLS = {
    "search_api", "get_symbol", "list_services", "get_example",
    "get_capability_status", "check_compatibility",
}
ORIGINAL_ORDER = ["search_api", "get_symbol", "list_services", "get_example", "get_capability_status", "check_compatibility"]
EXTENDED_ORDER = ORIGINAL_ORDER + ["list_engine_natives", "get_engine_native", "validate_manifest_and_widgets",
                                   "generate_plugin_scaffold", "search_guides", "get_guide_section"]
DEV_ORDER = ["run_plugin_check", "build_plugin_project", "test_plugin_project"]
SCAFFOLDER = ROOT / "sdk" / "tools" / "sco-new-plugin.py"
PROVENANCE_KEYS = {"schema_version", "sdk_version", "game_pack_version", "source_sha"}


def fixture():
    return json.loads(CATALOGUE.read_text(encoding="utf-8"))


SYNTHETIC_SHA = "a" * 40
OTHER_SHA = "b" * 40
GAME_STATUS = """# Synthetic status

| Table | Version | Stability | Capability | Default | Evidence |
|---|---|---|---|---|---|
| `game.sample` ([`sc_sample.h`](../../include/sc_sample.h)) | 1.1 | Stable | `game.sample.read` | ON | row resolved on 1.2.3.4 (sigcheck) |
| | | | `game.sample.watch` | OFF | OFF pending in-game confirmation |
| | | | sample field | none | BLOCKED |
| | | | `game.sample.claim` | ON | verified in game (2099-01-01) |
| `game.other` ([`sc_other.h`](../../include/sc_other.h)) | 1.0 | Experimental | `game.other.read` | ON | row resolved on 1.2.3.4 (sigcheck) |
| sample rows ([`status.md`](status.md)) | row | - | `sample.row` | ON | row resolved on 1.2.3.4 (sigcheck) |

Trailing prose.
"""
# The five-column table SDK 2.3.0 and earlier archives carry: still read, stability unknown.
GAME_STATUS_V1 = """# Synthetic status

| Table | Version | Capability | Default | Evidence |
|---|---|---|---|---|
| `game.sample` ([`sc_sample.h`](../../include/sc_sample.h)) | 1.1 | `game.sample.read` | ON | row resolved on 1.2.3.4 (sigcheck) |
| | | `game.sample.watch` | OFF | OFF pending in-game confirmation |
"""


def game_fixture():
    """The schema 1 fixture plus one synthetic game-pack table and record."""
    data = fixture()
    data["tables"]["sc_sample_v1"] = {
        "source_header": "include/sc_sample.h", "version_macro": "SC_SAMPLE_VERSION", "members": ["read"],
        "size": 16, "version": 65537, "service": "game.sample", "context": "Synthetic game-pack table."}
    record = copy.deepcopy(next(r for r in data["records"] if r["id"] == "sample-read"))
    record.update(id="game-sample-read", symbol="sc_sample_v1.read", table="sc_sample_v1", service="game.sample",
                  source_header="include/sc_sample.h", version_macro="SC_SAMPLE_VERSION", member_extent=16, table_size=16)
    data["records"].append(record)
    return data


def approval(tag, sha=SYNTHETIC_SHA, status="approved-for-publication"):
    return {"schema_version": 2, "tag": tag,
            "source": {"repo": "scubamount/sco-core", "sha": sha, "dependencies": []},
            "components_manifest_sha256": "0" * 64,
            "builder": {"workflow_path": ".github/workflows/release-candidate.yml", "workflow_sha": sha,
                        "run_id": 1, "run_attempt": 1},
            "artifacts": [], "profiles": ["core"], "distribution": {"status": status},
            "approved_by": "synthetic-approver", "approved_at": "2099-01-01T00:00:00Z"}


class Client:
    """One real child process; bounded reads without platform-specific selectors."""

    def __init__(self, catalogue=CATALOGUE, repo_root=None, use_env=False, extended=False, extra=()):
        command = [sys.executable, str(SERVER)] + (["--extended-tools"] if extended else []) + [str(a) for a in extra]
        environment = os.environ.copy()
        environment.pop("SCO_CORE_CATALOGUE", None)
        if use_env:
            environment["SCO_CORE_CATALOGUE"] = str(catalogue)
        else:
            command += ["--catalogue", str(catalogue)]
        if repo_root is not None:
            command += ["--repo-root", str(repo_root)]
        self.stderr = tempfile.TemporaryFile()
        self.process = subprocess.Popen(
            command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=self.stderr, env=environment,
        )
        assert self.process.stdin is not None
        assert self.process.stdout is not None
        self.stdin = self.process.stdin
        self.stdout = self.process.stdout
        self.responses = queue.Queue()
        self.next_id = 1
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()

    def _read(self):
        try:
            while True:
                line = self.stdout.readline(4 * 1024 * 1024 + 2)
                if not line:
                    self.responses.put(None)
                    return
                self.responses.put(line)
        except (OSError, ValueError):
            self.responses.put(None)

    def diagnostics(self):
        self.stderr.seek(0)
        return self.stderr.read().decode("utf-8", errors="replace")[-4000:]

    def send_raw(self, frame):
        self.stdin.write(frame)
        self.stdin.flush()

    def send(self, message):
        self.send_raw(json.dumps(message, ensure_ascii=False).encode("utf-8") + b"\n")

    def read(self):
        try:
            frame = self.responses.get(timeout=TIMEOUT)
        except queue.Empty as error:
            raise AssertionError("Timed out waiting for MCP response: " + self.diagnostics()) from error
        if frame is None:
            raise AssertionError("Server exited without a response: " + self.diagnostics())
        if len(frame) > 4 * 1024 * 1024:
            raise AssertionError("Server response exceeds the 4 MiB limit")
        try:
            value = json.loads(frame)
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            raise AssertionError("Stdout is not an MCP JSON frame: " + repr(frame[:200])) from error
        if not isinstance(value, dict) or value.get("jsonrpc") != "2.0":
            raise AssertionError("Invalid JSON-RPC response: " + repr(value))
        return value

    def request(self, method, params=None, request_id=None):
        if request_id is None:
            request_id = self.next_id
            self.next_id += 1
        message = {"jsonrpc": "2.0", "id": request_id, "method": method}
        if params is not None:
            message["params"] = params
        self.send(message)
        response = self.read()
        if response.get("id") != request_id:
            raise AssertionError("Unexpected response ID (notification produced a reply?): " + repr(response))
        return response

    def initialize(self, version=PROTOCOL, notify=True):
        response = self.request("initialize", {
            "protocolVersion": version,
            "capabilities": {},
            "clientInfo": {"name": "synthetic-test-client", "version": "1"},
        })
        if notify and "result" in response:
            self.send({"jsonrpc": "2.0", "method": "notifications/initialized"})
        return response

    def call(self, name, arguments=None):
        return self.request("tools/call", {"name": name, "arguments": arguments or {}})

    def close(self):
        try:
            if self.process.stdin and not self.process.stdin.closed:
                self.process.stdin.close()
            try:
                self.process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.process.terminate()
                try:
                    self.process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait(timeout=2)
        finally:
            self.reader.join(timeout=2)
            self.stdout.close()
            self.stderr.close()


class FixtureTests(unittest.TestCase):
    def test_fixture_record_keys_match_build_authority(self):
        sys.path.insert(0, str(ROOT / "tools"))
        try:
            authority = importlib.import_module("sdk_catalogue")
        finally:
            sys.path.pop(0)
        data = fixture()
        self.assertEqual(data["schema_version"], authority.SCHEMA_VERSION)
        for record in data["records"]:
            with self.subTest(symbol=record["symbol"]):
                self.assertEqual(set(record), authority.RECORD_KEYS)

    def test_fixture_top_level_schema(self):
        data = fixture()
        self.assertEqual(set(data), PROVENANCE_KEYS | {"contract_context", "tables", "records"})
        self.assertRegex(data["source_sha"], r"^[0-9a-f]{40}$")
        self.assertEqual(set(data["contract_context"]), {
            "source", "reading_rules", "entrypoint_context", "remaining_gaps",
        })

    def test_fixture_ids_sorted_unique_and_tables_consistent(self):
        data = fixture()
        ids = [record["id"] for record in data["records"]]
        self.assertEqual(ids, sorted(set(ids)))
        for record in data["records"]:
            if record["table"] is None:
                self.assertIsNone(record["member_extent"])
                self.assertIsNone(record["table_size"])
            else:
                table = data["tables"][record["table"]]
                self.assertEqual(record["table_size"], table["size"])
                self.assertLessEqual(record["member_extent"], table["size"])
                self.assertIn(record["symbol"].split(".")[-1], table["members"])

    def test_fixture_examples_are_contained_regular_files(self):
        for record in fixture()["records"]:
            for name in record["examples"]:
                path = Path(name)
                self.assertFalse(path.is_absolute())
                self.assertNotIn("..", path.parts)
                self.assertTrue((FIXTURES / path).is_file())
                self.assertFalse((FIXTURES / path).is_symlink())

    def test_fixture_language_mappings_are_explicit(self):
        for record in fixture()["records"]:
            self.assertEqual(set(record["language_mappings"]), {"c", "cpp", "csharp", "lua"})
            for mapping in record["language_mappings"].values():
                self.assertIn(mapping["status"], {"direct", "typed-wrapper", "restricted", "unsupported"})
                self.assertTrue(mapping["evidence"])
                for evidence in mapping["evidence"]:
                    self.assertEqual(set(evidence), {"path", "token"})


class ServerCase(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not SERVER.is_file():
            raise unittest.SkipTest("server.py has not been written yet")

    def start(self, catalogue=CATALOGUE, repo_root=None, use_env=False, ready=True, extended=False, extra=()):
        client = Client(catalogue, repo_root, use_env, extended, extra)
        self.addCleanup(client.close)
        if ready:
            response = client.initialize()
            self.assertEqual(response["result"]["protocolVersion"], PROTOCOL)
        return client

    def result(self, response, data=None, is_error=False):
        self.assertNotIn("error", response, response)
        result = response["result"]
        self.assertEqual(result.get("isError", False), is_error, result)
        structured = result["structuredContent"]
        expected = data if data is not None else fixture()
        self.assertEqual(structured["provenance"], {key: expected[key] for key in PROVENANCE_KEYS})
        text = [item["text"] for item in result["content"] if item.get("type") == "text"]
        self.assertTrue(text, result)
        self.assertEqual(json.loads(text[0]), structured)
        return structured

    def rpc_error(self, response, code):
        self.assertNotIn("result", response, response)
        self.assertEqual(response["error"]["code"], code, response)
        self.assertIsInstance(response["error"]["message"], str)
        self.assertTrue(response["error"]["message"])

    def argument_error(self, name, arguments):
        return self.result(self.start().call(name, arguments), is_error=True)

    def temporary_catalogue(self, data):
        temporary = tempfile.TemporaryDirectory(prefix="sco_mcp_test_")
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        shutil.copytree(FIXTURES / "examples", root / "examples")
        path = root / "catalogue.json"
        path.write_text(json.dumps(data), encoding="utf-8")
        return root, path


class ProtocolTests(ServerCase):
    def test_initialize_advertises_tools(self):
        result = self.start(ready=False).initialize()["result"]
        self.assertEqual(result["protocolVersion"], PROTOCOL)
        self.assertIn("tools", result["capabilities"])
        self.assertTrue(result["serverInfo"]["name"])
        self.assertTrue(result["serverInfo"]["version"])

    def test_unsupported_protocol_negotiates_only_supported_version(self):
        result = self.start(ready=False).initialize("2099-01-01")["result"]
        self.assertEqual(result["protocolVersion"], PROTOCOL)

    def test_ping_before_initialize(self):
        self.assertEqual(self.start(ready=False).request("ping")["result"], {})

    def test_tools_list_requires_initialize(self):
        self.assertIn("error", self.start(ready=False).request("tools/list"))

    def test_tools_call_requires_initialize(self):
        self.assertIn("error", self.start(ready=False).call("list_services"))

    def test_tools_require_initialized_notification(self):
        client = self.start(ready=False)
        client.initialize(notify=False)
        self.assertIn("error", client.request("tools/list"))
        client.send({"jsonrpc": "2.0", "method": "notifications/initialized"})
        self.assertIn("result", client.request("tools/list"))

    def test_unknown_method_is_method_not_found(self):
        self.rpc_error(self.start().request("unknown/method"), -32601)

    def test_unknown_notification_has_no_reply(self):
        client = self.start()
        client.send({"jsonrpc": "2.0", "method": "notifications/unknown"})
        self.assertEqual(client.request("ping")["result"], {})

    def test_malformed_json_reports_parse_error_and_recovers(self):
        client = self.start()
        client.send_raw(b'{"jsonrpc":\n')
        self.rpc_error(client.read(), -32700)
        self.assertEqual(client.request("ping")["result"], {})

    def test_invalid_utf8_reports_parse_error(self):
        client = self.start()
        client.send_raw(b'\xff\n')
        self.rpc_error(client.read(), -32700)

    def test_json_scalar_is_invalid_request(self):
        client = self.start()
        client.send(42)
        self.rpc_error(client.read(), -32600)

    def test_batch_is_invalid_request(self):
        client = self.start()
        client.send([{"jsonrpc": "2.0", "id": 80, "method": "ping"}])
        self.rpc_error(client.read(), -32600)

    def test_missing_jsonrpc_is_invalid_request(self):
        client = self.start()
        client.send({"id": 90, "method": "ping"})
        self.rpc_error(client.read(), -32600)

    def test_wrong_jsonrpc_is_invalid_request(self):
        client = self.start()
        client.send({"jsonrpc": "1.0", "id": 91, "method": "ping"})
        self.rpc_error(client.read(), -32600)

    def test_missing_method_is_invalid_request(self):
        client = self.start()
        client.send({"jsonrpc": "2.0", "id": 92})
        self.rpc_error(client.read(), -32600)

    def test_nonstring_method_is_invalid_request(self):
        client = self.start()
        client.send({"jsonrpc": "2.0", "id": 93, "method": 4})
        self.rpc_error(client.read(), -32600)

    def test_boolean_id_is_invalid_request(self):
        client = self.start()
        client.send({"jsonrpc": "2.0", "id": True, "method": "ping"})
        self.rpc_error(client.read(), -32600)

    def test_object_id_is_invalid_request(self):
        client = self.start()
        client.send({"jsonrpc": "2.0", "id": {}, "method": "ping"})
        self.rpc_error(client.read(), -32600)

    def test_string_id_is_preserved(self):
        response = self.start().request("ping", request_id="request-exact-001")
        self.assertEqual(response["id"], "request-exact-001")
        self.assertEqual(response["result"], {})

    def test_oversized_frame_is_rejected(self):
        client = self.start()
        client.send_raw(b'{"jsonrpc":"2.0","id":94,"method":"ping","padding":"' + b'x' * (70 * 1024) + b'"}\n')
        response = client.read()
        self.assertIn("error", response)
        self.assertEqual(client.request("ping")["result"], {})

    def test_unknown_tool_is_invalid_params(self):
        self.rpc_error(self.start().call("not_a_tool"), -32602)

    def test_nonobject_tool_arguments_is_invalid_params(self):
        client = self.start()
        self.rpc_error(client.request("tools/call", {"name": "search_api", "arguments": []}), -32602)

    def test_nonobject_call_params_is_invalid_params(self):
        self.rpc_error(self.start().request("tools/call", []), -32602)

    def test_environment_catalogue_source(self):
        self.result(self.start(use_env=True).call("list_services"))


class ToolTests(ServerCase):
    def test_tool_list_contains_exact_read_only_surface(self):
        tools = self.start().request("tools/list")["result"]["tools"]
        self.assertEqual({tool["name"] for tool in tools}, TOOLS)
        self.assertEqual(len(tools), len(TOOLS))
        for tool in tools:
            self.assertEqual(tool["inputSchema"]["type"], "object")
            self.assertTrue(tool["description"])
            self.assertEqual(tool["annotations"], {
                "readOnlyHint": True, "destructiveHint": False,
                "idempotentHint": True, "openWorldHint": False,
            })
            self.assertIs(tool["inputSchema"]["additionalProperties"], False)

    def test_search_symbol_returns_summary_not_full_record(self):
        data = self.result(self.start().call("search_api", {"query": "sco_sample_v1.read"}))
        self.assertEqual(data["records"][0]["symbol"], "sco_sample_v1.read")
        self.assertEqual(set(data["records"][0]), {"id", "symbol", "service", "purpose", "score"})
        self.assertGreater(data["records"][0]["score"], 0)
        self.assertEqual(data["limit"], 10)

    def test_search_purpose_text(self):
        data = self.result(self.start().call("search_api", {"query": "caller-owned buffer"}))
        self.assertTrue(data["records"])
        self.assertTrue({"sample-read", "sample-write"} & {record["id"] for record in data["records"]})

    def test_search_service_filter(self):
        data = self.result(self.start().call("search_api", {"query": "sample", "service": "sco.sample"}))
        self.assertTrue(data["records"])
        self.assertTrue(all(record["service"] == "sco.sample" for record in data["records"]))

    def test_search_language_excludes_unsupported(self):
        data = self.result(self.start().call("search_api", {"query": "sample", "language": "lua"}))
        ids = {record["id"] for record in data["records"]}
        self.assertIn("sample-read", ids)
        self.assertNotIn("sample-write", ids)

    def test_search_limit_and_total_matches(self):
        data = self.result(self.start().call("search_api", {"query": "sample", "limit": 1}))
        self.assertEqual(len(data["records"]), 1)
        self.assertEqual(data["limit"], 1)
        self.assertGreaterEqual(data["total_matches"], 2)

    def test_search_unmatched_query(self):
        data = self.result(self.start().call("search_api", {"query": "no-match-xyzzy"}))
        self.assertEqual(data["records"], [])
        self.assertEqual(data["total_matches"], 0)

    def test_search_is_deterministic(self):
        client = self.start()
        first = self.result(client.call("search_api", {"query": "sample"}))
        second = self.result(client.call("search_api", {"query": "sample"}))
        self.assertEqual(first, second)
        scores = [record["score"] for record in first["records"]]
        self.assertEqual(scores, sorted(scores, reverse=True))

    def test_get_symbol_by_symbol_preserves_all_contract_fields(self):
        data = self.result(self.start().call("get_symbol", {"symbol_or_id": "sco_sample_v1.read"}))
        expected = next(record for record in fixture()["records"] if record["id"] == "sample-read")
        self.assertEqual(data["record"], expected)
        self.assertEqual(data["table_context"], fixture()["tables"]["sco_sample_v1"])
        self.assertEqual(data["contract_context"], fixture()["contract_context"])

    def test_get_symbol_by_stable_id(self):
        data = self.result(self.start().call("get_symbol", {"symbol_or_id": "sample-write"}))
        self.assertEqual(data["record"]["symbol"], "sco_sample_v1.write")

    def test_entrypoint_has_no_table_context(self):
        data = self.result(self.start().call("get_symbol", {"symbol_or_id": "plugin-load"}))
        self.assertIsNone(data["table_context"])
        self.assertIsNone(data["record"]["table"])
        self.assertIsNone(data["record"]["member_extent"])

    def test_list_services_summarizes_real_tables(self):
        data = self.result(self.start().call("list_services"))
        expected = [{"table": name, "service": table["service"], "version": table["version"],
                     "size": table["size"], "member_count": len(table["members"]), "stability": "stable"}
                    for name, table in sorted(fixture()["tables"].items())]
        self.assertEqual(data["services"], expected)
        context = data["stability_context"]
        self.assertEqual((context["document"], context["game_state"]), ("docs/stability.md", "unavailable"))
        self.assertIn("--repo-root", context["reason"])

    def test_examples_without_root_are_paths_only(self):
        data = self.result(self.start().call("get_example", {"symbol_or_id": "sample-read"}))
        self.assertIs(data["paths_only"], True)
        self.assertEqual(data["examples"], [{"path": "examples/read.c"}, {"path": "examples/read.cpp"}])

    def test_examples_with_root_return_exact_content(self):
        data = self.result(self.start(repo_root=FIXTURES).call("get_example", {"symbol_or_id": "sample-read"}))
        self.assertIs(data["paths_only"], False)
        self.assertEqual(data["examples"], [{"path": name, "content": (FIXTURES / name).read_text(encoding="utf-8")}
                                           for name in ["examples/read.c", "examples/read.cpp"]])

    def test_example_language_filter(self):
        data = self.result(self.start(repo_root=FIXTURES).call("get_example", {"symbol_or_id": "sample-read", "language": "cpp"}))
        self.assertEqual([item["path"] for item in data["examples"]], ["examples/read.cpp"])

    def test_examples_empty_is_not_unknown_symbol(self):
        data = self.result(self.start().call("get_example", {"symbol_or_id": "api-query-service"}))
        self.assertEqual(data["examples"], [])

    def test_capability_keeps_restricted_and_unsupported_distinct(self):
        client = self.start()
        for symbol in ["sample-read", "sample-write"]:
            with self.subTest(symbol=symbol):
                data = self.result(client.call("get_capability_status", {"symbol_or_id": symbol}))
                record = next(record for record in fixture()["records"] if record["id"] == symbol)
                self.assertEqual(data["language_mappings"], record["language_mappings"])
                self.assertEqual(data["absent_capability"], record["absent_capability"])
                self.assertEqual(data["limitations"], record["limitations"])
                self.assertTrue(data["status_caveat"])

    def test_compatibility_reports_catalogue_coordinates(self):
        data = self.result(self.start().call("check_compatibility"))
        coordinates = data["catalogue_coordinates"]
        self.assertEqual(coordinates["sdk_version"], "1.1.0")
        self.assertEqual(coordinates["game_pack_version"], "0.3.0")
        self.assertTrue(data["conclusions"])

    def test_compatibility_matching_coordinates(self):
        data = self.result(self.start().call("check_compatibility", {"sdk_version": "1.1.0", "game_pack_version": "0.3.0"}))
        self.assertEqual(data["requested"], {"sdk_version": "1.1.0", "game_pack_version": "0.3.0"})
        self.assertIs(data["coordinate_matches"]["sdk_version"], True)
        self.assertIs(data["coordinate_matches"]["game_pack_version"], True)

    def test_compatibility_mismatching_coordinates(self):
        data = self.result(self.start().call("check_compatibility", {"sdk_version": "9.9.9", "game_pack_version": "0.4.0"}))
        self.assertIs(data["coordinate_matches"]["sdk_version"], False)
        self.assertIs(data["coordinate_matches"]["game_pack_version"], False)


class ArgumentTests(ServerCase):
    def test_missing_search_query(self):
        self.argument_error("search_api", {})

    def test_query_must_be_a_string(self):
        self.argument_error("search_api", {"query": 10})

    def test_query_length_bound(self):
        self.argument_error("search_api", {"query": "x" * 513})

    def test_limit_lower_bound(self):
        self.argument_error("search_api", {"query": "sample", "limit": 0})

    def test_limit_upper_bound(self):
        self.argument_error("search_api", {"query": "sample", "limit": 51})

    def test_limit_rejects_boolean(self):
        self.argument_error("search_api", {"query": "sample", "limit": True})

    def test_limit_rejects_float(self):
        self.argument_error("search_api", {"query": "sample", "limit": 1.5})

    def test_search_language_enum(self):
        self.argument_error("search_api", {"query": "sample", "language": "python"})

    def test_service_must_be_a_string(self):
        self.argument_error("search_api", {"query": "sample", "service": []})

    def test_symbol_is_required(self):
        self.argument_error("get_symbol", {})

    def test_symbol_must_be_a_string(self):
        self.argument_error("get_symbol", {"symbol_or_id": 1})

    def test_unknown_symbol_is_tool_error_with_provenance(self):
        self.argument_error("get_symbol", {"symbol_or_id": "unknown-symbol"})

    def test_unknown_capability_symbol_is_tool_error(self):
        self.argument_error("get_capability_status", {"symbol_or_id": "unknown-symbol"})

    def test_unknown_example_symbol_is_tool_error(self):
        self.argument_error("get_example", {"symbol_or_id": "unknown-symbol"})

    def test_example_language_enum(self):
        self.argument_error("get_example", {"symbol_or_id": "sample-read", "language": "python"})

    def test_sdk_version_must_be_a_string(self):
        self.argument_error("check_compatibility", {"sdk_version": 1})

    def test_game_pack_version_rejects_integer(self):
        self.argument_error("check_compatibility", {"game_pack_version": -1})

    def test_game_pack_version_rejects_boolean(self):
        self.argument_error("check_compatibility", {"game_pack_version": False})

    def test_game_pack_version_rejects_empty_string(self):
        self.argument_error("check_compatibility", {"game_pack_version": ""})

    def test_valid_argument_boundaries(self):
        client = self.start()
        self.result(client.call("search_api", {"query": "x" * 512, "limit": 50}))
        self.result(client.call("check_compatibility", {"game_pack_version": "0.0.0"}))


class ExampleSecurityTests(ServerCase):
    def example_error(self, name, prepare=None, repo_root=True):
        data = fixture()
        record = next(record for record in data["records"] if record["id"] == "sample-read")
        record["examples"] = [name]
        root, catalogue = self.temporary_catalogue(data)
        if prepare:
            prepare(root)
        client = self.start(catalogue=catalogue, repo_root=root if repo_root else None)
        result = self.result(client.call("get_example", {"symbol_or_id": "sample-read"}), data=data, is_error=True)
        self.assertNotIn("sensitive-marker", json.dumps(result))

    def test_parent_traversal_is_rejected(self):
        self.example_error("../sensitive.c")

    def test_absolute_path_is_rejected(self):
        self.example_error(Path(tempfile.gettempdir()).joinpath("sensitive.c").as_posix())

    def test_windows_absolute_path_is_rejected(self):
        self.example_error("C:\\sensitive.c")

    def test_traversal_is_rejected_even_in_paths_only_mode(self):
        self.example_error("../sensitive.c", repo_root=False)

    def test_symlink_file_is_rejected(self):
        def prepare(root):
            (root / "sensitive.c").write_text("sensitive-marker", encoding="utf-8")
            (root / "examples/link.c").symlink_to(root / "sensitive.c")
        self.example_error("examples/link.c", prepare)

    def test_symlink_directory_component_is_rejected(self):
        def prepare(root):
            (root / "linked").symlink_to(root / "examples", target_is_directory=True)
        self.example_error("linked/read.c", prepare)

    def test_directory_is_not_an_example_file(self):
        self.example_error("examples")

    def test_missing_file_is_rejected(self):
        self.example_error("examples/missing.c")

    def test_nonregular_fifo_is_rejected_without_hanging(self):
        if not hasattr(os, "mkfifo"):
            self.skipTest("Named pipes are unavailable on this platform")
        self.example_error("examples/pipe.c", lambda root: os.mkfifo(root / "examples/pipe.c"))

    def test_example_file_over_256_kib_is_rejected(self):
        self.example_error("examples/large.c", lambda root: (root / "examples/large.c").write_bytes(b"x" * (256 * 1024 + 1)))

    def test_more_than_16_examples_is_rejected(self):
        data = fixture()
        record = next(record for record in data["records"] if record["id"] == "sample-read")
        record["examples"] = ["examples/item-%02d.c" % index for index in range(17)]
        root, catalogue = self.temporary_catalogue(data)
        for name in record["examples"]:
            (root / name).write_text("/* synthetic */\n", encoding="utf-8")
        self.result(self.start(catalogue=catalogue, repo_root=root).call("get_example", {"symbol_or_id": "sample-read"}), data=data, is_error=True)

    def test_exactly_16_examples_are_allowed(self):
        data = fixture()
        record = next(record for record in data["records"] if record["id"] == "sample-read")
        record["examples"] = ["examples/item-%02d.c" % index for index in range(16)]
        root, catalogue = self.temporary_catalogue(data)
        for name in record["examples"]:
            (root / name).write_text("/* synthetic */\n", encoding="utf-8")
        result = self.result(self.start(catalogue=catalogue, repo_root=root).call("get_example", {"symbol_or_id": "sample-read"}), data=data)
        self.assertEqual(len(result["examples"]), 16)

    def test_symlinked_repository_root_is_followed_but_contained(self):
        data = fixture()
        root, catalogue = self.temporary_catalogue(data)
        alias = root / "alias"
        alias.symlink_to(root, target_is_directory=True)
        client = self.start(catalogue=catalogue, repo_root=alias)
        result = self.result(client.call("get_example", {"symbol_or_id": "sample-read"}), data=data)
        self.assertIs(result["paths_only"], False)
        self.assertEqual([item["path"] for item in result["examples"]],
                         ["examples/read.c", "examples/read.cpp"])

    def test_symlink_inside_repository_root_is_still_refused(self):
        data = fixture()
        root, catalogue = self.temporary_catalogue(data)
        (root / "linked").symlink_to(root / "examples", target_is_directory=True)
        record = next(r for r in data["records"] if r["id"] == "sample-read")
        record["examples"] = ["linked/read.c"]
        catalogue.write_text(json.dumps(data), encoding="utf-8")
        self.result(self.start(catalogue=catalogue, repo_root=root).call("get_example", {"symbol_or_id": "sample-read"}), data=data, is_error=True)


class ArchiveLayoutTests(ServerCase):
    """Records name sdk/examples/...; extracted core/legacy archives hold examples/..."""

    def layout(self, name="sdk/examples/read.c"):
        data = fixture()
        record = next(record for record in data["records"] if record["id"] == "sample-read")
        record["examples"] = [name]
        root, catalogue = self.temporary_catalogue(data)
        return data, root, catalogue

    def outside(self):
        temporary = tempfile.TemporaryDirectory(prefix="sco_mcp_outside_")
        self.addCleanup(temporary.cleanup)
        directory = Path(temporary.name)
        (directory / "examples").mkdir()
        (directory / "examples/read.c").write_text("sensitive-marker", encoding="utf-8")
        return directory

    def get_example(self, data, root, catalogue, is_error=False):
        client = self.start(catalogue=catalogue, repo_root=root)
        result = self.result(client.call("get_example", {"symbol_or_id": "sample-read"}), data=data, is_error=is_error)
        if is_error:
            self.assertNotIn("sensitive-marker", json.dumps(result))
        return result

    def test_archive_root_reads_recorded_sdk_path_without_prefix(self):
        data, root, catalogue = self.layout()
        self.assertFalse((root / "sdk").exists())
        result = self.get_example(data, root, catalogue)
        self.assertEqual(result["examples"], [{"path": "sdk/examples/read.c",
                                               "content": (FIXTURES / "examples/read.c").read_text(encoding="utf-8")}])

    def test_repository_root_reads_recorded_path_first(self):
        data, root, catalogue = self.layout()
        (root / "sdk/examples").mkdir(parents=True)
        (root / "sdk/examples/read.c").write_text("repository-layout\n", encoding="utf-8")
        result = self.get_example(data, root, catalogue)
        self.assertEqual(result["examples"], [{"path": "sdk/examples/read.c", "content": "repository-layout\n"}])

    def test_repository_root_missing_file_is_not_served_from_archive_layout(self):
        data, root, catalogue = self.layout()
        (root / "sdk/examples").mkdir(parents=True)
        (root / "examples/read.c").write_text("sensitive-marker", encoding="utf-8")
        self.get_example(data, root, catalogue, is_error=True)

    def test_symlinked_sdk_entry_is_refused_and_blocks_fallback(self):
        data, root, catalogue = self.layout()
        (root / "sdk").symlink_to(self.outside(), target_is_directory=True)
        self.get_example(data, root, catalogue, is_error=True)

    def test_dangling_sdk_symlink_blocks_fallback(self):
        data, root, catalogue = self.layout()
        (root / "sdk").symlink_to(root / "absent", target_is_directory=True)
        (root / "examples/read.c").write_text("sensitive-marker", encoding="utf-8")
        self.get_example(data, root, catalogue, is_error=True)

    def test_archive_fallback_refuses_symlinked_directory(self):
        data, root, catalogue = self.layout()
        shutil.rmtree(root / "examples")
        (root / "examples").symlink_to(self.outside() / "examples", target_is_directory=True)
        self.get_example(data, root, catalogue, is_error=True)

    def test_archive_fallback_refuses_symlinked_file(self):
        data, root, catalogue = self.layout()
        (root / "examples/read.c").unlink()
        (root / "examples/read.c").symlink_to(self.outside() / "examples/read.c")
        self.get_example(data, root, catalogue, is_error=True)

    def test_archive_fallback_does_not_admit_traversal(self):
        data, root, catalogue = self.layout("sdk/../sensitive.c")
        (root / "sensitive.c").write_text("sensitive-marker", encoding="utf-8")
        self.get_example(data, root, catalogue, is_error=True)

    def test_only_the_sdk_prefix_is_dropped(self):
        data, root, catalogue = self.layout("src/examples/read.c")
        self.get_example(data, root, catalogue, is_error=True)


class StartupTests(ServerCase):
    def rejected(self, path):
        environment = os.environ.copy()
        environment.pop("SCO_CORE_CATALOGUE", None)
        result = subprocess.run([sys.executable, str(SERVER), "--catalogue", str(path)],
                                input=b"", stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                env=environment, timeout=TIMEOUT)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, b"")
        self.assertTrue(result.stderr)

    def test_missing_catalogue_fails_without_stdout_noise(self):
        with tempfile.TemporaryDirectory(prefix="sco_mcp_missing_") as directory:
            self.rejected(Path(directory) / "absent.json")

    def test_invalid_catalogue_json_fails_without_stdout_noise(self):
        _, path = self.temporary_catalogue(fixture())
        path.write_text("{", encoding="utf-8")
        self.rejected(path)

    def test_unknown_schema_version_is_rejected(self):
        data = fixture()
        data["schema_version"] = 999
        _, path = self.temporary_catalogue(data)
        self.rejected(path)

    def test_duplicate_record_ids_are_rejected(self):
        data = fixture()
        data["records"][1]["id"] = data["records"][0]["id"]
        _, path = self.temporary_catalogue(data)
        self.rejected(path)

    def test_missing_record_contract_key_is_rejected(self):
        data = fixture()
        del data["records"][0]["ownership"]
        _, path = self.temporary_catalogue(data)
        self.rejected(path)


class AdditionalBoundaryTests(ServerCase):
    def test_initialize_advertises_only_tools(self):
        result = self.start(ready=False).initialize()["result"]
        self.assertEqual(result["capabilities"], {"tools": {}})

    def test_invalid_initialize_does_not_advance_state(self):
        client = self.start(ready=False)
        self.rpc_error(client.request("initialize", {}), -32602)
        self.assertEqual(client.initialize()["result"]["protocolVersion"], PROTOCOL)

    def test_source_derived_wrapper_alias_is_searchable(self):
        data = fixture()
        record = next(r for r in data["records"] if r["id"] == "sample-read")
        record["language_mappings"]["cpp"]["evidence"][0]["token"] = "ReadSampleAlias("
        _, path = self.temporary_catalogue(data)
        result = self.result(self.start(catalogue=path).call("search_api", {"query": "ReadSampleAlias"}), data=data)
        self.assertEqual(result["records"][0]["id"], "sample-read")

    def test_exact_symbol_ranks_above_purpose_match(self):
        data = fixture()
        next(r for r in data["records"] if r["id"] == "sample-write")["purpose"] = "Mentions sco_sample_v1.read"
        _, path = self.temporary_catalogue(data)
        result = self.result(self.start(catalogue=path).call("search_api", {"query": "sco_sample_v1.read"}), data=data)
        self.assertEqual(result["records"][0]["id"], "sample-read")

    def test_unknown_argument_is_tool_error(self):
        self.argument_error("get_example", {"symbol_or_id": "sample-read", "path": "secret.c"})
        self.argument_error("list_services", {"arbitrary": True})

    def test_blank_query_is_tool_error(self):
        self.argument_error("search_api", {"query": "   "})

    def test_rate_limit_rejects_121st_tool_call(self):
        client = self.start()
        for _ in range(120):
            self.result(client.call("list_services"))
        result = self.result(client.call("list_services"), is_error=True)
        self.assertIn("rate limit", result["error"])
        self.assertEqual(client.request("ping")["result"], {})

    def test_output_bound_returns_provenanced_error_not_truncation(self):
        data = fixture()
        next(r for r in data["records"] if r["id"] == "sample-read")["purpose"] = "x" * (2200 * 1024)
        _, path = self.temporary_catalogue(data)
        result = self.result(self.start(catalogue=path).call("get_symbol", {"symbol_or_id": "sample-read"}), data=data, is_error=True)
        self.assertIn("4 MiB", result["error"])
        self.assertNotIn("record", result)

    def test_example_exactly_256_kib_is_allowed(self):
        data = fixture()
        next(r for r in data["records"] if r["id"] == "sample-read")["examples"] = ["examples/read.c"]
        root, path = self.temporary_catalogue(data)
        (root / "examples/read.c").write_bytes(b"x" * (256 * 1024))
        result = self.result(self.start(catalogue=path, repo_root=root).call("get_example", {"symbol_or_id": "sample-read"}), data=data)
        self.assertEqual(len(result["examples"][0]["content"]), 256 * 1024)

    def test_duplicate_json_keys_and_nonfinite_numbers_are_parse_errors(self):
        client = self.start()
        for frame in [b'{"jsonrpc":"2.0","id":1,"id":2,"method":"ping"}\n',
                      b'{"jsonrpc":"2.0","id":NaN,"method":"ping"}\n']:
            client.send_raw(frame)
            self.rpc_error(client.read(), -32700)
        self.assertEqual(client.request("ping")["result"], {})

    def test_invalid_pagination_cursor_is_invalid_params(self):
        self.rpc_error(self.start().request("tools/list", {"cursor": "unknown"}), -32602)


class LanguageMetadataTests(ServerCase):
    def test_language_test_metadata_and_legacy_catalogues(self):
        server = importlib.import_module("server")
        self.assertEqual(server.load_catalogue(CATALOGUE), fixture())
        data = fixture()
        for record in data["records"]:
            record["tests"].append("tests/synthetic_language_fixture.c")
            for mapping in record["language_mappings"].values():
                mapping["tests"] = [] if mapping["status"] == "unsupported" else ["tests/synthetic_language_fixture.c"]
        _, path = self.temporary_catalogue(data)
        self.assertEqual(server.load_catalogue(path), data)
        record = data["records"][0]
        positive = next(m for m in record["language_mappings"].values() if m["status"] != "unsupported")
        positive["tests"] = []
        path.write_text(json.dumps(data), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "Missing language tests"):
            server.load_catalogue(path)
        positive["tests"] = ["tests/not_listed.c"]
        path.write_text(json.dumps(data), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "Invalid language tests"):
            server.load_catalogue(path)


class EvidenceStateTests(ServerCase):
    def game_root(self, status=GAME_STATUS, write=True):
        data = game_fixture()
        root, catalogue = self.temporary_catalogue(data)
        if write:
            (root / "docs/game").mkdir(parents=True)
            (root / "docs/game/status.md").write_text(status, encoding="utf-8")
        return data, root, catalogue

    def evidence(self, symbol, data=None, catalogue=CATALOGUE, repo_root=None):
        client = self.start(catalogue=catalogue, repo_root=repo_root)
        symbol_state = self.result(client.call("get_symbol", {"symbol_or_id": symbol}), data=data)["evidence_state"]
        capability = self.result(client.call("get_capability_status", {"symbol_or_id": symbol}), data=data)
        self.assertEqual(capability["evidence_state"], symbol_state)
        return symbol_state

    def test_neutral_records_mark_game_evidence_not_applicable(self):
        for symbol in ["sample-read", "api-query-service", "plugin-load"]:
            with self.subTest(symbol=symbol):
                state = self.evidence(symbol, repo_root=FIXTURES)
                self.assertEqual(state["scope"], "neutral")
                self.assertEqual(state["contract_status"]["state"], "documented")
                self.assertEqual(state["default_runtime_state"]["state"], "unknown")
                self.assertEqual(state["sigcheck_receipt"]["state"], "not_applicable")
                self.assertEqual(state["gameplay_receipt"]["state"], "not_applicable")
                self.assertNotIn("service_status", state)

    def test_game_record_reports_service_rows_and_never_gameplay(self):
        data, root, catalogue = self.game_root()
        state = self.evidence("game-sample-read", data, catalogue, root)
        self.assertEqual(state["scope"], "game-pack")
        self.assertEqual(state["default_runtime_state"]["state"], "unknown")
        self.assertEqual(state["sigcheck_receipt"]["state"], "unknown")
        self.assertEqual(state["gameplay_receipt"]["state"], "unknown")
        status = state["service_status"]
        self.assertEqual((status["state"], status["scope"], status["documented_version"]), ("rows_found", "service", "1.1"))
        self.assertIs(status["version_matches_catalogue"], True)
        self.assertEqual([(row["default"], row["evidence_level"]) for row in status["rows"]], [
            ("ON", "row_resolved_sigcheck"), ("OFF", "off_pending_in_game_confirmation"),
            ("none", "blocked"), ("ON", "unclassified")])
        self.assertEqual(status["rows"][0]["game_build"], "1.2.3.4")
        self.assertNotIn("game_build", status["rows"][3])

    def test_game_record_without_repo_root_is_unavailable(self):
        data, _, catalogue = self.game_root()
        status = self.evidence("game-sample-read", data, catalogue)["service_status"]
        self.assertEqual((status["state"], status["rows"]), ("unavailable", []))

    def test_game_record_absent_from_status_table_has_no_rows(self):
        data, root, catalogue = self.game_root(GAME_STATUS.replace("`game.sample`", "`game.renamed`"))
        status = self.evidence("game-sample-read", data, catalogue, root)["service_status"]
        self.assertEqual((status["state"], status["rows"]), ("no_rows", []))

    def test_mismatched_header_is_not_attributed(self):
        data, root, catalogue = self.game_root(GAME_STATUS.replace("`sc_sample.h`", "`sc_other_sample.h`"))
        self.assertEqual(self.evidence("game-sample-read", data, catalogue, root)["service_status"]["state"], "no_rows")

    def test_malformed_status_table_refuses_all_rows(self):
        for status in [GAME_STATUS.replace("| sample field | none | BLOCKED |", "| sample field | BLOCKED |"),
                       GAME_STATUS.replace("| OFF | OFF pending", "| MAYBE | OFF pending"),
                       GAME_STATUS.replace("|---|---|---|---|---|---|", ""),
                       GAME_STATUS + "\n| Table | Version | Capability | Default | Evidence |\n",
                       GAME_STATUS.replace("| 1.1 | Stable |", "| 1.1 | |"),
                       GAME_STATUS.replace("| 1.1 | Stable |", "| 1.1 | Beta |"),
                       GAME_STATUS.replace("| | | | sample field |", "| | | Stable | sample field |"),
                       GAME_STATUS.replace("| `game.sample.watch` |", "| | `game.sample.watch` |"),
                       GAME_STATUS_V1.replace("| `game.sample.watch` |", "| Stable | `game.sample.watch` |")]:
            with self.subTest(status=status[-80:]):
                data, root, catalogue = self.game_root(status)
                state = self.evidence("game-sample-read", data, catalogue, root)
                self.assertEqual((state["service_status"]["state"], state["service_status"]["rows"]), ("unavailable", []))
                self.assertEqual(state["sigcheck_receipt"]["state"], "unknown")

    def stability(self, data, catalogue, repo_root, symbol="game-sample-read"):
        client = self.start(catalogue=catalogue, repo_root=repo_root)
        services = self.result(client.call("list_services"), data=data)
        capability = self.result(client.call("get_capability_status", {"symbol_or_id": symbol}), data=data)
        return ({item["table"]: item["stability"] for item in services["services"]}, services["stability_context"],
                capability)

    def test_stability_labels_come_from_the_status_table(self):
        data, root, catalogue = self.game_root()
        labels, context, capability = self.stability(data, catalogue, root)
        self.assertEqual(labels, {"sco_api": "stable", "sco_sample_v1": "stable", "sc_sample_v1": "stable"})
        self.assertEqual((context["game_source"], context["game_state"]), ("docs/game/status.md", "read"))
        self.assertEqual(capability["stability"], "stable")
        self.assertEqual(capability["evidence_state"]["service_status"]["stability"], "stable")
        self.assertEqual(capability["stability_context"], context)
        data, root, catalogue = self.game_root(GAME_STATUS.replace("| 1.1 | Stable |", "| 1.1 | Experimental |"))
        labels, _, capability = self.stability(data, catalogue, root)
        self.assertEqual((labels["sc_sample_v1"], capability["stability"]), ("experimental", "experimental"))

    def test_neutral_records_are_stable_and_unlabelled_game_tables_unknown(self):
        data, _, catalogue = self.game_root()
        labels, context, capability = self.stability(data, catalogue, None)
        self.assertEqual(labels, {"sco_api": "stable", "sco_sample_v1": "stable", "sc_sample_v1": "unknown"})
        self.assertEqual(context["game_state"], "unavailable")
        self.assertEqual(capability["stability"], "unknown")
        for symbol in ("sample-read", "plugin-load"):
            with self.subTest(symbol=symbol):
                self.assertEqual(self.stability(data, catalogue, None, symbol)[2]["stability"], "stable")
        data, root, catalogue = self.game_root(GAME_STATUS.replace("| 1.1 | Stable |", "| 1.1 | Beta |"))
        labels, context, capability = self.stability(data, catalogue, root)
        self.assertEqual((labels["sc_sample_v1"], context["game_state"], capability["stability"]),
                         ("unknown", "unavailable", "unknown"))
        self.assertIn("refused", context["reason"])

    def test_second_table_of_a_service_shares_its_label(self):
        data = game_fixture()
        data["tables"]["sc_sample_v1_2"] = dict(data["tables"]["sc_sample_v1"], context="Synthetic second table.")
        root, catalogue = self.temporary_catalogue(data)
        (root / "docs/game").mkdir(parents=True)
        (root / "docs/game/status.md").write_text(GAME_STATUS, encoding="utf-8")
        labels = self.stability(data, catalogue, root)[0]
        self.assertEqual((labels["sc_sample_v1"], labels["sc_sample_v1_2"]), ("stable", "stable"))

    def test_five_column_status_table_is_still_read_without_stability(self):
        data, root, catalogue = self.game_root(GAME_STATUS_V1)
        labels, context, capability = self.stability(data, catalogue, root)
        status = capability["evidence_state"]["service_status"]
        self.assertEqual((status["state"], status["documented_version"], len(status["rows"])), ("rows_found", "1.1", 2))
        self.assertEqual([row["evidence_level"] for row in status["rows"]],
                         ["row_resolved_sigcheck", "off_pending_in_game_confirmation"])
        self.assertIsNone(status["stability"])
        self.assertEqual((labels["sc_sample_v1"], context["game_state"], capability["stability"]),
                         ("unknown", "read", "unknown"))

    def test_symlinked_status_document_is_refused(self):
        data, root, catalogue = self.game_root(write=False)
        (root / "docs/game").mkdir(parents=True)
        (root / "real-status.md").write_text(GAME_STATUS, encoding="utf-8")
        (root / "docs/game/status.md").symlink_to(root / "real-status.md")
        self.assertEqual(self.evidence("game-sample-read", data, catalogue, root)["service_status"]["state"], "unavailable")


SYNTHETIC_REGISTRY = {"schema_version": 2, "about": "Synthetic registry.", "services": [
    {"id": "game.sample", "kind": "service", "service": "game.sample", "header": "include/sc_sample.h", "version": "1.1",
     "stability": "stable", "stable_since": "sdk-v2.4.0",
     "capabilities": [
         {"name": "game.sample.read", "methods": ["read"], "default": "ON", "evidence_level": "sigcheck_resolved",
          "game_build": "1.2.3.4", "evidence": "row resolved on 1.2.3.4 (sigcheck)", "sig_rows": ["sample.row"],
          "evidence_source": "docs/game/status.md"},
         {"name": "game.sample.watch", "methods": [], "default": "OFF", "evidence_level": "off_pending_probe",
          "evidence": "OFF pending in-game confirmation", "spike_ref": "B1"},
         {"name": None, "display": "sample field", "methods": [], "default": "none", "evidence_level": "blocked", "evidence": "BLOCKED"},
         {"name": "game.sample.claim", "methods": [], "default": "ON", "evidence_level": "in_game_verified",
          "evidence": "verified in game (2099-01-01)"},
         {"name": "game.sample.synthetic", "methods": [], "default": "OFF", "evidence_level": "synthetic_only",
          "evidence": "Synthetic fixtures only"}]},
    {"id": "game.events", "kind": "event_bus", "service": "game.*", "header": "include/sc_sample.h", "version": "bus",
     "stability": "stable", "stable_since": "sdk-v2.4.0",
     "capabilities": [{"name": "game.events.sample", "methods": [], "default": "ON", "evidence_level": "sigcheck_resolved",
                       "evidence": "row resolved on 1.2.3.4 (sigcheck)"}]},
    {"id": "game.sample.rows", "kind": "signature_rows", "service": "game.sample", "doc": "docs/game/status.md",
     "title": "sample rows", "version": "row",
     "capabilities": [{"name": "sample.row", "methods": [], "default": "ON", "evidence_level": "sigcheck_resolved",
                       "game_build": "1.2.3.4", "evidence": "row resolved on 1.2.3.4 (sigcheck)"}]}]}


def unlabelled_registry():
    """The schema_version 1 registry of SDK 2.3.0 and earlier archives: no stability labels."""
    registry = copy.deepcopy(SYNTHETIC_REGISTRY)
    registry["schema_version"] = 1
    for entry in registry["services"]:
        entry.pop("stability", None)
        entry.pop("stable_since", None)
    return registry


class RegistryEvidenceTests(ServerCase):
    def registry_root(self, registry=SYNTHETIC_REGISTRY, status=GAME_STATUS.replace("| 1.1 |", "| 1.0 |")):
        data = game_fixture()
        root, catalogue = self.temporary_catalogue(data)
        (root / "docs/game").mkdir(parents=True)
        (root / "docs/game/status.md").write_text(status, encoding="utf-8")
        (root / "sdk").mkdir()
        if registry is not None:
            text = registry if isinstance(registry, str) else json.dumps(registry)
            (root / "sdk/status_registry.json").write_text(text, encoding="utf-8")
        return data, root, catalogue

    def state(self, data, root, catalogue):
        client = self.start(catalogue=catalogue, repo_root=root)
        state = self.result(client.call("get_symbol", {"symbol_or_id": "game-sample-read"}), data=data)["evidence_state"]
        capability = self.result(client.call("get_capability_status", {"symbol_or_id": "game-sample-read"}), data=data)
        self.assertEqual(capability["evidence_state"], state)
        return state

    def test_registry_is_preferred_and_never_reports_gameplay(self):
        state = self.state(*self.registry_root())
        status = state["service_status"]
        self.assertEqual((status["state"], status["source"], status["documented_version"]),
                         ("rows_found", "sdk/status_registry.json", "1.1"))
        self.assertIs(status["version_matches_catalogue"], True)
        self.assertEqual([row["capability"] for row in status["rows"]],
                         ["`game.sample.read` (`read`)", "`game.sample.watch`", "sample field", "`game.sample.claim`", "`game.sample.synthetic`"])
        self.assertEqual([row["evidence_level"] for row in status["rows"]],
                         ["row_resolved_sigcheck", "off_pending_in_game_confirmation", "blocked", "unclassified", "unclassified"])
        self.assertEqual([row["registry_evidence_level"] for row in status["rows"]],
                         ["sigcheck_resolved", "off_pending_probe", "blocked", "unclassified", "synthetic_only"])
        self.assertEqual((status["rows"][0]["game_build"], status["rows"][0]["sig_rows"], status["rows"][1]["spike_ref"]),
                         ("1.2.3.4", ["sample.row"], "B1"))
        self.assertEqual(state["gameplay_receipt"]["state"], "unknown")
        self.assertNotIn("in_game_verified", json.dumps(state))
        self.assertEqual(status["stability"], "stable")

    def test_registry_stability_reaches_both_tools(self):
        for label, expected in (("stable", "stable"), ("experimental", "experimental"), (None, "unknown")):
            with self.subTest(label=label):
                registry = copy.deepcopy(SYNTHETIC_REGISTRY)
                if label is None:
                    registry = unlabelled_registry()
                elif label == "experimental":
                    registry["services"][0]["stability"] = label
                    registry["services"][0].pop("stable_since")
                data, root, catalogue = self.registry_root(registry)
                client = self.start(catalogue=catalogue, repo_root=root)
                services = self.result(client.call("list_services"), data=data)
                capability = self.result(client.call("get_capability_status", {"symbol_or_id": "game-sample-read"}), data=data)
                listed = {item["table"]: item["stability"] for item in services["services"]}
                self.assertEqual((listed["sc_sample_v1"], listed["sco_api"]), (expected, "stable"))
                self.assertEqual(capability["stability"], expected)
                self.assertEqual(capability["evidence_state"]["service_status"]["stability"], label)
                self.assertEqual(services["stability_context"]["game_source"], "sdk/status_registry.json")

    def test_absent_registry_falls_back_to_status_document(self):
        status = self.state(*self.registry_root(registry=None))["service_status"]
        self.assertEqual((status["state"], status["source"], status["documented_version"]),
                         ("rows_found", "docs/game/status.md", "1.0"))
        self.assertNotIn("registry_evidence_level", status["rows"][0])

    def test_service_without_registry_entry_has_no_rows(self):
        registry = copy.deepcopy(SYNTHETIC_REGISTRY)
        registry["services"][0]["service"] = "game.renamed"
        status = self.state(*self.registry_root(registry))["service_status"]
        self.assertEqual((status["state"], status["source"], status["rows"]), ("no_rows", "sdk/status_registry.json", []))

    def test_refused_registry_is_reported_not_bypassed(self):
        def mutate(change):
            registry = copy.deepcopy(SYNTHETIC_REGISTRY)
            change(registry)
            return registry
        cases = ["{", json.dumps({**SYNTHETIC_REGISTRY, "schema_version": 3}),
                 json.dumps({**SYNTHETIC_REGISTRY, "schema_version": 1}),
                 mutate(lambda r: r["services"][0].pop("stability")),
                 mutate(lambda r: r["services"][0].pop("stable_since")),
                 mutate(lambda r: r["services"][0].update(stable_since="2.4.0")),
                 mutate(lambda r: r["services"][1].update(stability="experimental")),
                 mutate(lambda r: r["services"][2].update(stability="stable")),
                 mutate(lambda r: r["services"][0]["capabilities"][0].update(address="0x1")),
                 mutate(lambda r: r["services"][0]["capabilities"][0].update(evidence_level="verified")),
                 mutate(lambda r: r["services"][0]["capabilities"][0].update(default=["ON"])),
                 mutate(lambda r: r["services"][0].pop("header")),
                 mutate(lambda r: r["services"][0].update(stability="beta")),
                 mutate(lambda r: r["services"][0].update(stability=["stable"])),
                 mutate(lambda r: r["services"].append(copy.deepcopy(r["services"][0])))]
        for registry in cases:
            with self.subTest(registry=str(registry)[:60]):
                state = self.state(*self.registry_root(registry))
                status = state["service_status"]
                self.assertEqual((status["state"], status["source"], status["rows"]), ("unavailable", "sdk/status_registry.json", []))
                self.assertIn("sdk/status_registry.json", status["reason"])

    def test_unlabelled_schema_1_registry_is_still_read(self):
        status = self.state(*self.registry_root(unlabelled_registry()))["service_status"]
        self.assertEqual((status["state"], status["source"], len(status["rows"])), ("rows_found", "sdk/status_registry.json", 5))
        self.assertIsNone(status["stability"])
        labelled = unlabelled_registry()
        labelled["services"][0]["stability"] = "stable"
        status = self.state(*self.registry_root(labelled))["service_status"]
        self.assertEqual((status["state"], status["rows"]), ("unavailable", []))

    def test_symlinked_registry_is_refused(self):
        data, root, catalogue = self.registry_root(registry=None)
        (root / "elsewhere.json").write_text(json.dumps(SYNTHETIC_REGISTRY), encoding="utf-8")
        (root / "sdk/status_registry.json").symlink_to(root / "elsewhere.json")
        status = self.state(data, root, catalogue)["service_status"]
        self.assertEqual((status["state"], status["source"]), ("unavailable", "sdk/status_registry.json"))


def native_entry(identifier, subsystem, tier, capability=None):
    entry = {"id": identifier, "subsystem": subsystem,
             "hash": "_0x" + hashlib.sha256(("sco-core:native:" + identifier).encode("utf-8")).hexdigest()[:16].upper(),
             "tier": tier, "source": "src/game/%s_sigs.cpp" % subsystem}
    if capability:
        entry["capability"] = capability
    return entry


def native_index(entries):
    return {"schema_version": 1, "about": "Synthetic index.", "row_count": len(entries), "entries": entries,
            "tiers": {"1": "promoted", "2": "resolved-unexposed", "3": "spike hold", "4": "blocked"}}


NATIVES = [native_entry("spawn.sample_helper", "actors", 1, ["spawn.sample"]), native_entry("spawn.sample_tag", "actors", 2),
           native_entry("world.sample_ray", "world", 2), native_entry("event.sample_seat", "events", 3, ["game.events.sample_seat"])]


class EngineNativeTests(ServerCase):
    def native_root(self, index=None, text=None):
        root, catalogue = self.temporary_catalogue(fixture())
        (root / "sdk").mkdir()
        (root / "sdk/native_index.json").write_text(text if text is not None else json.dumps(index or native_index(NATIVES)),
                                                    encoding="utf-8")
        return root, catalogue

    def client(self, **kwargs):
        root, catalogue = self.native_root(**kwargs)
        return self.start(catalogue=catalogue, repo_root=root, extended=True)

    def test_extended_tools_are_opt_in_and_read_only(self):
        default = self.start()
        self.assertEqual([tool["name"] for tool in default.request("tools/list")["result"]["tools"]], ORIGINAL_ORDER)
        for name in EXTENDED_ORDER[6:]:
            with self.subTest(name=name):
                self.rpc_error(default.call(name, {}), -32602)
        tools = self.start(extended=True).request("tools/list")["result"]["tools"]
        self.assertEqual([tool["name"] for tool in tools], EXTENDED_ORDER)
        for tool in tools:
            self.assertEqual(tool["annotations"], {"readOnlyHint": True, "destructiveHint": False,
                                                   "idempotentHint": True, "openWorldHint": False})
            self.assertIs(tool["inputSchema"]["additionalProperties"], False)

    def test_without_repo_root_is_unavailable(self):
        client = self.start(extended=True)
        listed = self.result(client.call("list_engine_natives"))
        self.assertEqual((listed["state"], listed["entries"], listed["next_cursor"]), ("unavailable", [], None))
        found = self.result(client.call("get_engine_native", {"id_or_hash": "spawn.sample_tag"}))
        self.assertEqual(found["state"], "unavailable")
        self.assertNotIn("entry", found)

    def test_pages_follow_the_cursor(self):
        client = self.client()
        first = self.result(client.call("list_engine_natives", {"limit": 3}))
        self.assertEqual((first["state"], first["row_count"], first["total_matches"], first["next_cursor"]), ("available", 4, 4, "3"))
        self.assertEqual(first["entries"], NATIVES[:3])
        second = self.result(client.call("list_engine_natives", {"limit": 3, "cursor": first["next_cursor"]}))
        self.assertEqual((second["entries"], second["next_cursor"]), (NATIVES[3:], None))
        self.assertEqual(self.result(client.call("list_engine_natives", {"cursor": "99"}))["entries"], [])

    def test_filters(self):
        client = self.client()
        for arguments, expected in [({"subsystem": "actors"}, NATIVES[:2]), ({"tier": 3}, NATIVES[3:]),
                                    ({"subsystem": "actors", "tier": 2}, NATIVES[1:2]),
                                    ({"query": "GAME.EVENTS"}, NATIVES[3:]), ({"query": NATIVES[2]["hash"][3:10].lower()}, NATIVES[2:3]),
                                    ({"query": "sample", "subsystem": "world"}, NATIVES[2:3]), ({"subsystem": "absent"}, [])]:
            with self.subTest(arguments=arguments):
                result = self.result(client.call("list_engine_natives", arguments))
                self.assertEqual((result["entries"], result["total_matches"]), (expected, len(expected)))

    def test_get_by_id_or_hash(self):
        client = self.client()
        for key in [NATIVES[0]["id"], NATIVES[0]["hash"], NATIVES[0]["hash"].lower()]:
            with self.subTest(key=key):
                found = self.result(client.call("get_engine_native", {"id_or_hash": key}))
                self.assertEqual((found["entry"], found["tier_meaning"], found["source"]), (NATIVES[0], "promoted", "sdk/native_index.json"))
        for key in ["spawn.unknown", "_0x0000000000000000", "0x" + NATIVES[0]["hash"][3:]]:
            with self.subTest(key=key):
                self.result(client.call("get_engine_native", {"id_or_hash": key}), is_error=True)

    def test_refused_index_is_unavailable(self):
        wrong_hash = copy.deepcopy(NATIVES)
        wrong_hash[0]["hash"] = NATIVES[1]["hash"]
        extra_key = copy.deepcopy(NATIVES)
        extra_key[0]["pattern"] = "48 8B ?"
        cases = [{"text": "{"}, {"index": {**native_index(NATIVES), "schema_version": 2}},
                 {"index": {**native_index(NATIVES), "row_count": 5}}, {"index": native_index(wrong_hash)},
                 {"index": native_index(extra_key)}, {"index": native_index(NATIVES + NATIVES[:1])},
                 {"index": native_index([native_entry("spawn.bad_tier", "actors", 5)])}]
        for case in cases:
            with self.subTest(case=str(case)[:80]):
                listed = self.result(self.client(**case).call("list_engine_natives"))
                self.assertEqual((listed["state"], listed["entries"]), ("unavailable", []))
                self.assertIn("sdk/native_index.json", listed["reason"])

    def test_symlinked_index_is_refused(self):
        root, catalogue = self.temporary_catalogue(fixture())
        (root / "sdk").mkdir()
        (root / "index.json").write_text(json.dumps(native_index(NATIVES)), encoding="utf-8")
        (root / "sdk/native_index.json").symlink_to(root / "index.json")
        listed = self.result(self.start(catalogue=catalogue, repo_root=root, extended=True).call("list_engine_natives"))
        self.assertEqual(listed["state"], "unavailable")

    def test_argument_refusals(self):
        client = self.client()
        for name, arguments in [("list_engine_natives", {"tier": 0}), ("list_engine_natives", {"tier": 5}),
                                ("list_engine_natives", {"tier": True}), ("list_engine_natives", {"limit": 101}),
                                ("list_engine_natives", {"cursor": "01"}), ("list_engine_natives", {"cursor": "-1"}),
                                ("list_engine_natives", {"cursor": 3}), ("list_engine_natives", {"cursor": "1234567"}),
                                ("list_engine_natives", {"subsystem": ""}), ("list_engine_natives", {"address": "x"}),
                                ("get_engine_native", {}), ("get_engine_native", {"id_or_hash": "x" * 129})]:
            with self.subTest(name=name, arguments=arguments):
                self.result(client.call(name, arguments), is_error=True)

    def test_actual_repository_index(self):
        path = ROOT / "sdk/native_index.json"
        if not path.is_file():
            self.skipTest("No sdk/native_index.json in this checkout")
        index = json.loads(path.read_text(encoding="utf-8"))
        client = self.start(repo_root=ROOT, extended=True)
        listed = self.result(client.call("list_engine_natives", {"limit": 100}))
        self.assertEqual((listed["state"], listed["total_matches"], listed["row_count"]), ("available", index["row_count"], index["row_count"]))
        self.assertEqual(listed["entries"], index["entries"][:100])
        entry = index["entries"][-1]
        self.assertEqual(self.result(client.call("get_engine_native", {"id_or_hash": entry["hash"]}))["entry"], entry)


GOOD_INI = """\ufeff; synthetic manifest
id = sample_plugin
name = Sample Plugin  # trailing comment
version = 1.0.0
author = Example Author
api = 1.1
kind = native
entry = sample_plugin.dll
requires = teleport, spawn.ship
future_key = ignored

[settings]
speed = float min 0 max 10.5 default 2.5 label "Speed; fast" help "How fast"
mode = enum(slow, fast) default fast
enabled = bool default true
count = int min -3 max 3
title = string default "a # not a comment"
"""


def ini(**replace):
    text = GOOD_INI
    for old, new in replace.items():
        old = old.replace("__", " = ") if "__" in old else old
        text = text.replace(old, new)
    return text


VALID_TREE = [
    {"kind": "column", "id": 1},
    {"kind": "label", "id": 2, "parent": 1, "label": "Status"},
    {"kind": "row", "id": 3, "parent": 1},
    {"kind": "button", "id": 4, "parent": 3, "label": "Go"},
    {"kind": "toggle", "id": 5, "parent": 3, "label": "On", "value": 1},
    {"kind": "slider", "id": 6, "parent": 1, "label": "Speed", "value": 2.5, "minimum": 0, "maximum": 10},
    {"kind": "combo", "id": 7, "parent": 1, "label": "Mode", "items": ["a", "b"], "index": -1},
    {"kind": "text_input", "id": 8, "label": "Name", "text": "", "flags": 2},
    {"kind": 8, "id": 9, "items": ["h1", "h2", "c1", "c2"], "columns": 2, "flags": 1},
]


class ValidatorTests(ServerCase):
    def validate(self, client, plugin_ini=GOOD_INI, is_error=False, **arguments):
        return self.result(client.call("validate_manifest_and_widgets", {"plugin_ini": plugin_ini, **arguments}), is_error=is_error)

    def test_valid_manifest_reports_parsed_fields_and_skipped_keys(self):
        result = self.validate(self.start(extended=True), folder="sample_plugin")
        manifest = result["manifest"]
        self.assertIs(result["valid"], True)
        self.assertEqual((manifest["findings"], manifest["folder_checked"], manifest["sdk_api"]), ([], True, "1.1"))
        parsed = manifest["parsed"]
        self.assertEqual((parsed["id"], parsed["name"], parsed["requires"]), ("sample_plugin", "Sample Plugin", ["teleport", "spawn.ship"]))
        self.assertEqual([(s["name"], s["type"]) for s in parsed["settings"]],
                         [("speed", "float"), ("mode", "enum"), ("enabled", "bool"), ("count", "int"), ("title", "string")])
        self.assertEqual(manifest["notes"], ["plugin.ini line 10: unknown key 'future_key' ignored (a later minor's)"])
        self.assertNotIn("widgets", result)

    def test_manifest_refusals_follow_the_checker(self):
        client = self.start(extended=True)
        cases = [
            (ini(id__sample_plugin="id = Sample"), 2, "id must be 1-31 of [a-z0-9_]"),
            (ini(id__sample_plugin="id = game"), 2, "id is reserved"),
            (ini(**{"name = Sample Plugin  # trailing comment\n": ""}), None, "missing key 'name'"),
            (ini(**{"api = 1.1": "api = 2.0"}), None, "api 2.0, but this SDK is 1.1"),
            (ini(**{"api = 1.1": "api = 1.2"}), None, "api 1.2, but this SDK is 1.1"),
            (ini(**{"api = 1.1": "api = 1.x"}), 6, "api must be <major>.<minor>"),
            (ini(**{"kind = native": "kind = python"}), 7, "kind must be native, lua or data"),
            (ini(**{"entry = sample_plugin.dll": "entry = ../sample_plugin.dll"}), 8, "entry must be a file name"),
            (ini(**{"version = 1.0.0": "id = other"}), 4, "duplicate key 'id'"),
            (ini(**{"teleport, spawn.ship": "sample_plugin.service"}), None, "names the plugin itself (a requires cycle)"),
            (ini(**{"teleport, spawn.ship": "teleport,,spawn"}), 9, "requires must be a comma list of capability names"),
            (ini(**{"teleport, spawn.ship": "teleport, teleport"}), 9, "requires lists a capability twice"),
            (ini(**{"kind = native": "kind = data"}), None, "a data pack has no entry"),
            (ini(**{"entry = sample_plugin.dll\n": ""}), None, "missing key 'entry'"),
            (ini(**{"kind = native": "kind = data", "entry = sample_plugin.dll\n": ""}), None, "a data pack has no settings"),
            (ini(**{"default 2.5": "default 11"}), 13, "setting 'speed': default 11 is above max 10.5"),
            (ini(**{"min 0 max 10.5": "min 11 max 10.5"}), 13, "setting 'speed': min 11 is above max 10.5"),
            (ini(**{"enum(slow, fast) default fast": "enum(slow, slow)"}), 14, "enum lists a choice twice"),
            (ini(**{"enum(slow, fast) default fast": "enum(slow, fast) default medium"}), 14, "default 'medium' is not one of slow, fast"),
            (ini(**{"bool default true": "bool default yes"}), 15, "default 'yes' is not true or false"),
            (ini(**{"bool default true": "bool min 1"}), 15, "min and max apply to int and float only"),
            (ini(**{"int min -3": "int shade -3"}), 16, "unknown field 'shade'"),
            (ini(**{"int min -3 max 3": "int min"}), 16, "field 'min' needs a value"),
            (ini(**{'help "How fast"': 'help "How fast'}), 13, "a quote is not closed, or not at the start of a value"),
            (ini(**{"count = int": "Count = int"}), 16, "setting name must be 1-31 of [a-z0-9_]"),
            (ini(**{"count = int": "speed = int"}), 16, "setting 'speed': declared twice"),
            (GOOD_INI + "[settings]\n", 18, "duplicate section '[settings]'"),
            (GOOD_INI.replace("[settings]", "[settings"), 12, "a [section] line ends with ]"),
            (GOOD_INI.replace("future_key = ignored", "just words"), 10, "expected key = value"),
            ("; " + "x" * 16400 + "\n" + GOOD_INI, None, "plugin.ini: too big (over 16384 bytes)"),
        ]
        for text, line, message in cases:
            with self.subTest(message=message):
                manifest = self.validate(client, text)["manifest"]
                self.assertIs(manifest["valid"], False)
                [finding] = manifest["findings"]
                self.assertEqual(finding["line"], line)
                self.assertIn(message, finding["message"])
                if line is not None:
                    self.assertTrue(finding["message"].startswith("plugin.ini line %d: " % line), finding)

    def test_folder_must_match_id(self):
        client = self.start(extended=True)
        manifest = self.validate(client, folder="other_folder")["manifest"]
        self.assertEqual(manifest["findings"], [{"line": None, "message": "id 'sample_plugin' does not match folder 'other_folder'"}])
        for folder in ["a/b", "a\\b", ""]:
            with self.subTest(folder=folder):
                self.validate(client, folder=folder, is_error=True)

    def test_repository_manifests_match_the_checker(self):
        client = self.start(extended=True)
        manifests = sorted((ROOT / "sdk/examples").glob("*/plugin.ini")) + sorted((ROOT / "examples").glob("*/plugin.ini"))
        self.assertTrue(manifests)
        for path in manifests:
            with self.subTest(path=str(path.relative_to(ROOT))):
                manifest = self.validate(client, path.read_text(encoding="utf-8"), folder=path.parent.name)["manifest"]
                self.assertEqual(manifest["findings"], [])
                self.assertIs(manifest["valid"], True)

    def test_sdk_api_matches_the_root_header(self):
        header = (ROOT / "include/sco_api.h").read_text(encoding="utf-8")
        version = tuple(re.search(r"#define SCO_API_%s (\d+)" % part, header)[1] for part in ("MAJOR", "MINOR"))
        self.assertEqual(self.validate(self.start(extended=True))["manifest"]["sdk_api"], "%s.%s" % version)

    def test_valid_widget_tree(self):
        result = self.validate(self.start(extended=True), widgets=VALID_TREE)
        self.assertEqual(result["widgets"], {"valid": True, "node_count": 9, "findings": []})
        self.assertIs(result["valid"], True)

    def test_widget_invariant_findings(self):
        client = self.start(extended=True)

        def tree(position, **changes):
            nodes = copy.deepcopy(VALID_TREE)
            if position == len(nodes):
                nodes.append(changes)
            else:
                nodes[position].update(changes)
                nodes[position] = {k: v for k, v in nodes[position].items() if v is not None}
            return nodes
        cases = [
            (tree(3, id=2), 3, "BAD_ARG", "repeats an earlier node's id"),
            (tree(3, id=0), 3, "BAD_ARG", "id must be nonzero"),
            (tree(9, kind="label", id=10, parent=3), 9, "BAD_ARG", "is not an open row/column ancestor"),
            (tree(3, parent=2), 3, "BAD_ARG", "is not an open row/column ancestor"),
            (tree(0, parent=9), 0, "BAD_ARG", "is not an open row/column ancestor"),
            (tree(3, label=None), 3, "BAD_ARG", "label is required"),
            (tree(3, label=""), 3, "BAD_ARG", "label must be nonempty"),
            (tree(4, value=2), 4, "BAD_ARG", "a toggle value is 0 or 1"),
            (tree(5, value=11), 5, "BAD_ARG", "minimum <= value <= maximum"),
            (tree(6, index=2), 6, "BAD_ARG", "is not -1 or an item index"),
            (tree(8, items=["h1", "h2", "c1"]), 8, "BAD_ARG", "divisible by columns"),
            (tree(1, text="x"), 1, "BAD_ARG", "text applies to text_input only"),
            (tree(3, flags=2), 3, "BAD_ARG", "PASSWORD applies to text_input only"),
            (tree(1, flags=4), 1, "BAD_ARG", "flags has bits other than"),
            (tree(1, label="\u00e9" * 128), 1, "TOO_MANY", "label is 256 bytes; at most 255"),
            (tree(1, label="a\u0000b"), 1, "BAD_ARG", "contains NUL"),
            (tree(1, kind=11), 1, "BAD_ARG", "kind 11 is not 1 (label) to 10 (column)"),
            (tree(3, items=["x"]), 3, "BAD_ARG", "items apply to combo, list and table only"),
            (tree(1, value=1), 1, "BAD_ARG", "value applies to toggle and slider only"),
            (tree(8, columns=17, items=["c"] * 17), 8, "TOO_MANY", "17 columns; at most 16"),
            (tree(6, items=["i"] * 257), 6, "TOO_MANY", "257 items; at most 256"),
        ]
        for nodes, position, code, message in cases:
            with self.subTest(message=message):
                result = self.validate(client, widgets=nodes)
                self.assertIs(result["valid"], False)
                widgets = result["widgets"]
                self.assertIs(widgets["valid"], False)
                matching = [f for f in widgets["findings"] if message in f["message"]]
                self.assertTrue(matching, widgets["findings"])
                self.assertEqual((matching[0]["node"], matching[0]["id"], matching[0]["result"]),
                                 (position, nodes[position]["id"], code))

    def test_widget_schema_refusals(self):
        client = self.start(extended=True)
        node = {"kind": "label", "id": 1}
        for widgets in [[], [node] * 257, ["label"], [{**node, "size": 96}], [{"kind": "label"}], [{**node, "id": True}],
                        [{**node, "id": -1}], [{**node, "id": 2 ** 32}], [{**node, "kind": "slider2"}], [{**node, "label": 5}],
                        [{**node, "items": [1]}], [{**node, "value": "1"}], [{**node, "index": 2 ** 31}], {"0": node}]:
            with self.subTest(widgets=str(widgets)[:60]):
                self.validate(client, widgets=widgets, is_error=True)
        self.result(client.call("validate_manifest_and_widgets", {"widgets": [node]}), is_error=True)
        self.result(client.call("validate_manifest_and_widgets", {"plugin_ini": GOOD_INI, "path": "plugin.ini"}), is_error=True)


def load_scaffolder():
    """Import render_plugin the way the scaffolder documents, without writing __pycache__ into sdk/tools."""
    spec = importlib.util.spec_from_file_location("sco_new_plugin_reference", SCAFFOLDER)
    assert spec is not None and spec.loader is not None, SCAFFOLDER
    module = importlib.util.module_from_spec(spec)
    previous, sys.dont_write_bytecode = sys.dont_write_bytecode, True
    try:
        spec.loader.exec_module(module)
    finally:
        sys.dont_write_bytecode = previous
    return module


SCAFFOLD_SPECS = [
    {"id": "my_radar", "lang": "c"},
    {"id": "my_radar", "lang": "c", "with_menu": True, "services": ["game.actors", "sco.widgets"]},
    {"id": "hud_probe", "lang": "cpp", "services": ["game.actors@1.1"]},
    {"id": "hud_probe", "lang": "cpp", "with_menu": True, "services": ["sco.widgets"]},
    {"id": "plain", "lang": "cpp", "with_menu": False, "services": []},
    {"id": "cs_probe", "lang": "csharp", "with_menu": True, "services": ["game.actors@1.1", "sco.widgets"]},
    {"id": "cs_plain", "lang": "csharp"},
    {"id": "lua_probe", "lang": "lua", "with_menu": True, "services": ["sco.widgets"]},
    {"id": "lua_plain", "lang": "lua"},
]


class ScaffoldTests(ServerCase):
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls.reference = load_scaffolder()

    def scaffold(self, client, arguments, is_error=False):
        return self.result(client.call("generate_plugin_scaffold", arguments), is_error=is_error)

    def expected(self, spec):
        return self.reference.render_plugin(copy.deepcopy(spec))

    def scaffold_root(self, source=None):
        root, catalogue = self.temporary_catalogue(fixture())
        (root / "sdk/tools").mkdir(parents=True)
        if source is not None:
            (root / "sdk/tools/sco-new-plugin.py").write_text(source, encoding="utf-8")
        return root, catalogue

    def test_files_equal_render_plugin(self):
        client = self.start(repo_root=ROOT, extended=True)
        for spec in SCAFFOLD_SPECS:
            with self.subTest(spec=spec):
                result = self.scaffold(client, spec)
                self.assertEqual((result["state"], result["source"], result["written"], result["spec"]),
                                 ("available", "sdk/tools/sco-new-plugin.py", False, spec))
                self.assertEqual(result["files"], self.expected(spec))
                self.assertIn("run in game", result["conclusions"][-1])
                use = result["conclusions"][1]
                self.assertIn({"csharp": "dotnet publish", "lua": "lua-check.lua"}.get(spec["lang"], "--target check"), use)

    def test_new_languages_files(self):
        client = self.start(repo_root=ROOT, extended=True)
        files = self.scaffold(client, {"id": "cs_probe", "lang": "csharp", "with_menu": True})["files"]
        self.assertEqual(sorted(files), ["CsProbePlugin.cs", "README.md", "cs_probe.csproj", "plugin.ini"])
        self.assertIn("<PublishAot>true</PublishAot>", files["cs_probe.csproj"])
        self.assertIn("kind = native", files["plugin.ini"])
        files = self.scaffold(client, {"id": "lua_probe", "lang": "lua"})["files"]
        self.assertEqual(sorted(files), ["CMakeLists.txt", "main.lua", "plugin.ini"])
        self.assertIn("kind = lua", files["plugin.ini"])
        self.assertIn('name = "lua_probe.ping"', files["main.lua"])

    def test_copied_scaffolder_matches_and_nothing_is_written(self):
        root, catalogue = self.scaffold_root(SCAFFOLDER.read_text(encoding="utf-8"))
        before = sorted(path.relative_to(root).as_posix() for path in root.rglob("*"))
        client = self.start(catalogue=catalogue, repo_root=root, extended=True)
        for spec in SCAFFOLD_SPECS[1:3]:
            with self.subTest(spec=spec):
                self.assertEqual(self.scaffold(client, spec)["files"], self.expected(spec))
        self.assertEqual(sorted(path.relative_to(root).as_posix() for path in root.rglob("*")), before)

    def test_unavailable_without_root_or_loadable_scaffolder(self):
        result = self.scaffold(self.start(extended=True), SCAFFOLD_SPECS[0])
        self.assertEqual((result["state"], result["files"], result["written"]), ("unavailable", {}, False))
        self.assertIn("No --repo-root", result["reason"])
        root, catalogue = self.scaffold_root()
        missing = self.scaffold(self.start(catalogue=catalogue, repo_root=root, extended=True), SCAFFOLD_SPECS[0])
        self.assertEqual((missing["state"], missing["files"]), ("unavailable", {}))
        self.assertIn("unavailable beneath --repo-root", missing["reason"])
        for source, reason in [("def render_plugin(:\n", "failed to load: SyntaxError"),
                               ("raise RuntimeError('boom')\n", "failed to load: RuntimeError"),
                               ("render_plugin = None\n", "defines no render_plugin and SpecError")]:
            with self.subTest(reason=reason):
                root, catalogue = self.scaffold_root(source)
                client = self.start(catalogue=catalogue, repo_root=root, extended=True)
                for _ in range(2):  # The failure is reported again and the server keeps serving.
                    broken = self.scaffold(client, SCAFFOLD_SPECS[0])
                    self.assertEqual((broken["state"], broken["files"]), ("unavailable", {}))
                    self.assertIn(reason, broken["reason"])

    def test_symlinked_scaffolder_is_refused(self):
        root, catalogue = self.scaffold_root()
        try:
            (root / "sdk/tools/sco-new-plugin.py").symlink_to(SCAFFOLDER)
        except OSError as error:
            self.skipTest("symlinks unavailable: " + type(error).__name__)
        result = self.scaffold(self.start(catalogue=catalogue, repo_root=root, extended=True), SCAFFOLD_SPECS[0])
        self.assertEqual((result["state"], result["files"]), ("unavailable", {}))
        self.assertIn("symlink", result["reason"])

    def test_strict_schema_and_spec_refusals(self):
        client = self.start(repo_root=ROOT, extended=True)
        base = {"id": "my_radar", "lang": "c"}
        for arguments in [{"id": "my_radar"}, {"lang": "c"}, {**base, "lang": "rust"}, {**base, "lang": "C#"},
                          {**base, "id": "My-Radar"},
                          {**base, "id": "x" * 32}, {**base, "id": 7}, {**base, "with_menu": "yes"}, {**base, "with_menu": 1},
                          {**base, "services": "game.actors"}, {**base, "services": [1]}, {**base, "services": ["Game.Actors"]},
                          {**base, "services": ["game..actors"]}, {**base, "services": ["game.actors@1"]},
                          {**base, "services": ["s%d" % n for n in range(17)]},
                          {**base, "name": "Radar"}, {**base, "author": "someone"}, {**base, "out": "radar"}]:
            with self.subTest(arguments=arguments):
                self.assertNotIn("files", self.scaffold(client, arguments, is_error=True))
        for arguments, message in [({**base, "id": "game"}, "reserved"),
                                   ({**base, "services": ["game.actors", "game.actors@1.1"]}, "listed twice"),
                                   ({**base, "services": ["a" * 64]}, "1-63 characters"),
                                   ({**base, "lang": "csharp", "id": "host"}, "reserved"),
                                   ({**base, "lang": "lua", "services": ["game.actors"]}, "lang lua can't look up services"),
                                   ({**base, "lang": "lua", "services": ["sco.widgets"]}, "lang lua can't look up services")]:
            with self.subTest(arguments=arguments):
                refused = self.scaffold(client, arguments, is_error=True)
                self.assertIn("Spec refused by sco-new-plugin", refused["error"])
                self.assertIn(message, refused["error"])
                with self.assertRaises(self.reference.SpecError):
                    self.reference.render_plugin(arguments)


class CompatibilityReleaseTests(ServerCase):
    def release_root(self, records=None, signatures=("sdk-v1.2.3",)):
        data = fixture()
        root, catalogue = self.temporary_catalogue(data)
        directory = root / "docs/legal/release-decisions"
        directory.mkdir(parents=True)
        for tag, record in (records if records is not None else {"sdk-v1.2.3": approval("sdk-v1.2.3")}).items():
            text = record if isinstance(record, str) else json.dumps(record)
            (directory / (tag + ".json")).write_text(text, encoding="utf-8")
        for tag in signatures:
            (directory / (tag + ".json.sig")).write_text("-----BEGIN SSH SIGNATURE-----\nsynthetic\n", encoding="utf-8")
        return root, catalogue

    def release(self, arguments, root, catalogue):
        return self.result(self.start(catalogue=catalogue, repo_root=root).call("check_compatibility", arguments))

    def test_existing_callers_get_no_new_sections(self):
        data = self.result(self.start().call("check_compatibility", {"sdk_version": "1.1.0"}))
        self.assertEqual(set(data), {"requested", "catalogue_coordinates", "coordinate_matches", "conclusions", "provenance"})

    def test_tag_with_matching_source_is_found_but_not_signature_verified(self):
        root, catalogue = self.release_root()
        data = self.release({"tag": "sdk-v1.2.3", "source_sha": SYNTHETIC_SHA}, root, catalogue)
        release = data["release"]
        self.assertEqual(release["state"], "matching_approval_record")
        self.assertEqual((release["signature_verification"], release["publication"]), ("not_performed", "not_checked"))
        [record] = release["records"]
        self.assertEqual(record["path"], "docs/legal/release-decisions/sdk-v1.2.3.json")
        self.assertEqual((record["format"], record["built_source_sha"], record["signature_file"]),
                         ("approval_schema_v2", SYNTHETIC_SHA, "present"))
        self.assertIs(record["built_source_sha_matches"], True)
        self.assertIs(data["coordinate_matches"]["source_sha"], False)

    def test_tag_with_other_source_does_not_match(self):
        root, catalogue = self.release_root()
        release = self.release({"tag": "sdk-v1.2.3", "source_sha": OTHER_SHA}, root, catalogue)["release"]
        self.assertEqual(release["state"], "no_matching_approval_record")
        self.assertIs(release["records"][0]["built_source_sha_matches"], False)

    def test_source_only_scan_reports_matching_records(self):
        root, catalogue = self.release_root({"sdk-v1.2.3": approval("sdk-v1.2.3"), "sdk-v1.3.0": approval("sdk-v1.3.0", OTHER_SHA),
                                             "sdk-v0.9.0": {"schema_version": 1}})
        release = self.release({"source_sha": OTHER_SHA}, root, catalogue)["release"]
        self.assertEqual(release["state"], "matching_approval_record")
        self.assertEqual([record["tag"] for record in release["records"]], ["sdk-v1.3.0"])
        self.assertEqual(release["records"][0]["signature_file"], "absent")

    def test_missing_tag_record_and_missing_root(self):
        root, catalogue = self.release_root()
        release = self.release({"tag": "sdk-v9.9.9"}, root, catalogue)["release"]
        self.assertEqual((release["state"], release["records"]), ("no_matching_approval_record", []))
        release = self.result(self.start().call("check_compatibility", {"tag": "sdk-v1.2.3"}))["release"]
        self.assertEqual((release["state"], release["records"]), ("unavailable", []))

    def test_record_formats_are_reported_without_matching(self):
        bad_tag = approval("sdk-v1.2.4")
        cases = {"sdk-v1.2.3": ({"schema_version": 1}, "legacy_unsigned_v1"),
                 "sdk-v1.2.5": ("{", "unparseable"),
                 "sdk-v1.2.6": ({**approval("sdk-v1.2.6"), "source": {"sha": "short"}}, "unrecognized"),
                 "sdk-v1.2.7": ([], "unrecognized")}
        for tag, (record, expected) in cases.items():
            with self.subTest(tag=tag):
                root, catalogue = self.release_root({tag: record})
                release = self.release({"tag": tag}, root, catalogue)["release"]
                self.assertEqual(release["state"], "no_matching_approval_record")
                self.assertEqual(release["records"][0]["format"], expected)
        root, catalogue = self.release_root({"sdk-v1.2.3": bad_tag})
        release = self.release({"tag": "sdk-v1.2.3"}, root, catalogue)["release"]
        self.assertEqual(release["state"], "no_matching_approval_record")
        self.assertIs(release["records"][0]["tag_matches_filename"], False)

    def test_symlinked_record_is_unreadable(self):
        root, catalogue = self.release_root({})
        (root / "outside.json").write_text(json.dumps(approval("sdk-v1.2.3")), encoding="utf-8")
        (root / "docs/legal/release-decisions/sdk-v1.2.3.json").symlink_to(root / "outside.json")
        release = self.release({"tag": "sdk-v1.2.3"}, root, catalogue)["release"]
        self.assertEqual(release["records"][0]["format"], "unreadable")
        self.assertEqual(release["state"], "no_matching_approval_record")

    def test_service_table_version_and_extent(self):
        client = self.start()
        data = self.result(client.call("check_compatibility", {"service": "sco.sample", "min_version": "1.0", "member_extent": 24}))
        table = data["service_table"]
        self.assertEqual((table["table"], table["catalogue_version"], table["catalogue_size"]), ("sco_sample_v1", "1.0", 24))
        self.assertIs(table["version_satisfies"], True)
        self.assertIs(table["extent_within_catalogue_size"], True)
        for arguments, key in [({"min_version": "1.1"}, "version_satisfies"), ({"min_version": "2.0"}, "version_satisfies"),
                               ({"member_extent": 25}, "extent_within_catalogue_size")]:
            with self.subTest(arguments=arguments):
                result = self.result(client.call("check_compatibility", {"service": "sco.sample", **arguments}))
                self.assertIs(result["service_table"][key], False)
        only = self.result(client.call("check_compatibility", {"service": "sco_api"}))["service_table"]
        self.assertEqual(only["catalogue_version"], "1.1")
        self.assertNotIn("version_satisfies", only)

    def test_schema_advertises_patterns_and_dependencies(self):
        tools = {tool["name"]: tool for tool in self.start().request("tools/list")["result"]["tools"]}
        schema = tools["check_compatibility"]["inputSchema"]
        self.assertEqual(schema["dependentRequired"], {"min_version": ["service"], "member_extent": ["service"]})
        self.assertTrue(all("pattern" in schema["properties"][key] for key in ("tag", "source_sha", "min_version")))

    def test_refusals(self):
        client = self.start()
        for arguments in [{"tag": "../sdk-v1.2.3"}, {"tag": "sdk-v1.2.3\n"}, {"tag": "v1.2.3"}, {"tag": "sdk-v1.2"},
                          {"source_sha": SYNTHETIC_SHA.upper()}, {"source_sha": "abc"}, {"source_sha": 1},
                          {"min_version": "1.0"}, {"member_extent": 8}, {"service": "sco.unknown"},
                          {"service": "sco.sample", "min_version": "1"}, {"service": "sco.sample", "min_version": "01.0"},
                          {"service": "sco.sample", "min_version": "70000.0"}, {"service": "sco.sample", "member_extent": 0},
                          {"service": "sco.sample", "member_extent": True}, {"service": "sco.sample", "member_extent": 65536},
                          {"table": "sco_sample_v1"}]:
            with self.subTest(arguments=arguments):
                self.result(client.call("check_compatibility", arguments), is_error=True)


GUIDE_URL = "https://scubamount.github.io/sco-core/guides/"


def guide_section(doc, heading, anchor, text, level=2, truncated=False):
    title = "Synthetic " + doc.rsplit("/", 1)[-1]
    return {"doc": doc, "title": title, "heading": heading, "anchor": anchor, "level": level, "text": text,
            "truncated": truncated, "url": GUIDE_URL + doc.rsplit("/", 1)[-1][:-3] + "/"}


GUIDE_SECTIONS = [
    guide_section("docs/alpha.md", "Synthetic alpha.md", "synthetic-alphamd", "Overview. Deploy notes live below.", 1),
    guide_section("docs/alpha.md", "Deploy a plugin", "deploy-a-plugin", "Copy the plugin folder. " + "filler " * 120 + "deploy"),
    guide_section("docs/alpha.md", "Storage", "storage", "Keys and SQL. The plugin deploy step is elsewhere."),
    guide_section("docs/beta.md", "Synthetic beta.md", "synthetic-betamd", "Beta overview about widgets.", 1),
    guide_section("docs/beta.md", "Widgets", "widgets", "Build a panel. Deploy is not covered here.", truncated=True),
]


def guides_document(sections=None, source_sha=None):
    sections = GUIDE_SECTIONS if sections is None else sections
    source_sha = fixture()["source_sha"] if source_sha is None else source_sha
    docs = []
    for section in sections:
        if not docs or docs[-1]["doc"] != section["doc"]:
            docs.append({"doc": section["doc"], "title": section["title"], "url": section["url"], "sections": 0})
        docs[-1]["sections"] += 1
    return {"schema_version": 1, "sdk_version": "9.9.9", "source_sha": source_sha, "source": "site/public-inputs.json",
            "text_limit": 4000, "docs": docs, "sections": sections}


class GuideTests(ServerCase):
    def guide_client(self, document=None, text=None, repo_root=None):
        root, catalogue = self.temporary_catalogue(fixture())
        (root / "sdk-guides.json").write_text(text if text is not None else json.dumps(document or guides_document()),
                                              encoding="utf-8")
        return self.start(catalogue=catalogue, repo_root=repo_root, extended=True)

    def test_search_ranks_heading_above_text_and_pages(self):
        client = self.guide_client()
        first = self.result(client.call("search_guides", {"query": "How do I deploy a plugin?", "limit": 2}))
        self.assertEqual(first["query_terms"], ["deploy", "plugin"])
        self.assertEqual([(s["doc"], s["anchor"]) for s in first["sections"]],
                         [("docs/alpha.md", "deploy-a-plugin"), ("docs/alpha.md", "storage")])
        self.assertEqual((first["total_matches"], first["cursor"], first["next_cursor"]), (4, "0", "2"))
        top = first["sections"][0]
        self.assertEqual(set(top), {"doc", "title", "heading", "anchor", "level", "url", "matched_terms", "score",
                                    "truncated", "snippet"})
        self.assertEqual((top["matched_terms"], top["url"]), (2, GUIDE_URL + "alpha/"))
        self.assertLessEqual(len(top["snippet"]), 330)
        self.assertEqual(first["guides"], {"mode": "file", "source": "sdk-guides.json beside the catalogue",
                                           "source_sha": fixture()["source_sha"], "matches_catalogue_source_sha": True})
        self.assertTrue(any("nothing in them is evidence that code ran in game" in line for line in first["conclusions"]))
        second = self.result(client.call("search_guides", {"query": "How do I deploy a plugin?", "limit": 2,
                                                             "cursor": first["next_cursor"]}))
        self.assertEqual([s["anchor"] for s in second["sections"]], ["synthetic-alphamd", "widgets"])
        self.assertIsNone(second["next_cursor"])
        again = self.result(client.call("search_guides", {"query": "How do I deploy a plugin?", "limit": 2}))
        self.assertEqual(again, first)

    def test_doc_filter_and_detail(self):
        client = self.guide_client()
        found = self.result(client.call("search_guides", {"query": "widgets", "doc": "docs/beta.md"}))
        self.assertEqual([s["anchor"] for s in found["sections"]], ["widgets", "synthetic-betamd"])
        self.assertIs(found["sections"][0]["truncated"], True)
        detail = self.result(client.call("get_guide_section", {"doc": "docs/alpha.md", "anchor": "storage"}))
        self.assertEqual(detail["section"], GUIDE_SECTIONS[2])
        self.assertEqual(detail["guides"]["mode"], "file")
        nothing = self.result(client.call("search_guides", {"query": "zzzz"}))
        self.assertEqual((nothing["sections"], nothing["total_matches"], nothing["next_cursor"]), ([], 0, None))
        for name, arguments, token in (
                ("search_guides", {"query": "deploy", "doc": "docs/missing.md"}, "known docs: docs/alpha.md, docs/beta.md"),
                ("get_guide_section", {"doc": "docs/alpha.md", "anchor": "missing"}, "Unknown guide section"),
                ("search_guides", {"query": "..."}, "no searchable words")):
            with self.subTest(arguments=arguments):
                self.assertIn(token, self.result(client.call(name, arguments), is_error=True)["error"])

    def test_terms_match_whole_words_not_substrings(self):
        sections = [guide_section("docs/alpha.md", "Build it", "build-it", "Guide build steps, because the user asked.", 1),
                    guide_section("docs/alpha.md", "UI", "ui", "The ui service and its API."),
                    guide_section("docs/beta.md", "Deployment", "deployment", "Ship sco.net plugins.", 1)]
        client = self.guide_client(guides_document(sections))
        found = self.result(client.call("search_guides", {"query": "ui"}))
        self.assertEqual([s["anchor"] for s in found["sections"]], ["ui"])
        found = self.result(client.call("search_guides", {"query": "How do I use the C API?"}))
        self.assertEqual((found["query_terms"], [s["anchor"] for s in found["sections"]]), (["use", "api"], ["ui"]))
        found = self.result(client.call("search_guides", {"query": "deploy"}))
        self.assertEqual([s["anchor"] for s in found["sections"]], ["deployment"])
        found = self.result(client.call("search_guides", {"query": "net"}))
        self.assertEqual([s["anchor"] for s in found["sections"]], ["deployment"])
        self.assertEqual(self.result(client.call("search_guides", {"query": "use"}))["total_matches"], 0)

    def test_rarer_terms_outrank_common_ones(self):
        sections = [guide_section("docs/alpha.md", "Write", "write", "Text.", 1),
                    guide_section("docs/alpha.md", "Write more", "write-more", "Text."),
                    guide_section("docs/beta.md", "Lua", "lua", "Text.", 1)]
        found = self.result(self.guide_client(guides_document(sections)).call("search_guides", {"query": "write lua"}))
        self.assertEqual([s["anchor"] for s in found["sections"]], ["lua", "write", "write-more"])

    def test_stopword_only_queries_still_search(self):
        found = self.result(self.guide_client().call("search_guides", {"query": "how to"}))
        self.assertEqual(found["query_terms"], ["how", "to"])

    def test_argument_refusals(self):
        client = self.guide_client()
        for name, arguments in (("search_guides", {}), ("search_guides", {"query": "x", "limit": 0}),
                                ("search_guides", {"query": "x", "limit": 26}), ("search_guides", {"query": "x", "cursor": "01"}),
                                ("search_guides", {"query": "x" * 257}), ("search_guides", {"query": "x", "extra": 1}),
                                ("get_guide_section", {"doc": "docs/alpha.md"}), ("get_guide_section", {"doc": "", "anchor": "x"})):
            with self.subTest(name=name, arguments=arguments):
                self.result(client.call(name, arguments), is_error=True)

    def test_missing_guides_is_a_tool_error(self):
        client = self.start(extended=True)
        for name, arguments in (("search_guides", {"query": "deploy"}), ("get_guide_section", {"doc": "d", "anchor": "a"})):
            with self.subTest(name=name):
                error = self.result(client.call(name, arguments), is_error=True)["error"]
                self.assertIn("SDK guides unavailable", error)
                self.assertIn("sdk-guides.json", error)

    def test_refused_file_is_reported_not_bypassed(self):
        broken = guides_document()
        broken["sections"] = list(broken["sections"])
        broken["sections"][1] = dict(broken["sections"][1], text="x" * 4001)
        for text, token in ((json.dumps(broken), "invalid section text"), ("{", "refused"),
                            (json.dumps(dict(guides_document(), schema_version=2)), "schema_version"),
                            (json.dumps(dict(guides_document(), extra=1)), "unexpected top-level keys")):
            with self.subTest(token=token):
                client = self.guide_client(text=text, repo_root=ROOT)
                error = self.result(client.call("search_guides", {"query": "deploy"}), is_error=True)["error"]
                self.assertIn("sdk-guides.json beside the catalogue refused", error)
                self.assertIn(token, error)

    def test_other_source_sha_is_reported(self):
        client = self.guide_client(guides_document(source_sha=OTHER_SHA))
        context = self.result(client.call("search_guides", {"query": "deploy"}))["guides"]
        self.assertEqual((context["source_sha"], context["matches_catalogue_source_sha"]), (OTHER_SHA, False))

    def test_explicit_guides_file(self):
        root, catalogue = self.temporary_catalogue(fixture())
        path = root / "elsewhere.json"
        path.write_text(json.dumps(guides_document()), encoding="utf-8")
        client = self.start(catalogue=catalogue, extended=True, extra=["--guides", str(path)])
        found = self.result(client.call("search_guides", {"query": "storage"}))
        self.assertEqual((found["sections"][0]["anchor"], found["guides"]["source"]), ("storage", "--guides file"))
        missing = self.start(catalogue=catalogue, extended=True, extra=["--guides", str(root / "absent.json")])
        error = self.result(missing.call("search_guides", {"query": "storage"}), is_error=True)["error"]
        self.assertIn("--guides file unreadable", error)
        self.assertNotIn(str(root), error)

    def test_source_mode_builds_the_repository_guides_in_memory(self):
        sys.path.insert(0, str(ROOT / "tools"))
        try:
            import sdk_guides
        finally:
            sys.path.remove(str(ROOT / "tools"))
        _, sections = sdk_guides.sections(ROOT)
        _, catalogue = self.temporary_catalogue(fixture())
        client = self.start(catalogue=catalogue, repo_root=ROOT, extended=True)
        found = self.result(client.call("search_guides", {"query": "first plugin", "doc": "docs/getting-started.md"}))
        self.assertTrue(found["sections"])
        self.assertEqual(found["guides"], {"mode": "source", "source_sha": None, "matches_catalogue_source_sha": None,
                                           "source": "tools/sdk_guides.py beneath --repo-root, built in memory on first use"})
        first = found["sections"][0]
        detail = self.result(client.call("get_guide_section", {"doc": first["doc"], "anchor": first["anchor"]}))
        self.assertIn(detail["section"], sections)
        found = self.result(client.call("search_guides", {"query": "How do I write plugin.ini?"}))
        self.assertEqual((found["sections"][0]["doc"], found["sections"][0]["url"]),
                         ("sdk/docs/plugin-ini.md", "sdk/docs/plugin-ini.md"))

    def test_source_mode_without_builder_is_unavailable(self):
        root, catalogue = self.temporary_catalogue(fixture())
        client = self.start(catalogue=catalogue, repo_root=root, extended=True)
        error = self.result(client.call("search_guides", {"query": "deploy"}), is_error=True)["error"]
        self.assertIn("tools/sdk_guides.py unavailable beneath --repo-root", error)


class BuildIntegrationTests(ServerCase):
    def test_actual_build_catalogue_mcp_transcript(self):
        override = os.environ.get("SCO_CORE_MCP_TEST_CATALOGUE")
        path = Path(override) if override else ROOT / "dist/sdk-catalogue.json"
        if not path.is_file():
            if override:
                self.fail("SCO_CORE_MCP_TEST_CATALOGUE points to a missing file: " + str(path))
            self.skipTest("No build catalogue; build one or set SCO_CORE_MCP_TEST_CATALOGUE")
        data = json.loads(path.read_text(encoding="utf-8"))
        self.assertTrue(data["records"])
        client = self.start(catalogue=path)
        tools = client.request("tools/list")["result"]["tools"]
        self.assertEqual({tool["name"] for tool in tools}, TOOLS)
        record = data["records"][0]
        symbol = self.result(client.call("get_symbol", {"symbol_or_id": record["id"]}), data=data)
        self.assertEqual(symbol["record"], record)
        self.assertEqual(symbol["contract_context"], data["contract_context"])
        search = self.result(client.call("search_api", {"query": record["symbol"], "limit": 50}), data=data)
        self.assertIn(record["id"], {item["id"] for item in search["records"]})
        services = self.result(client.call("list_services"), data=data)
        self.assertEqual({item["table"] for item in services["services"]}, set(data["tables"]))
        examples = self.result(client.call("get_example", {"symbol_or_id": record["id"]}), data=data)
        self.assertIs(examples["paths_only"], True)
        self.assertEqual([item["path"] for item in examples["examples"]], record["examples"])
        capability = self.result(client.call("get_capability_status", {"symbol_or_id": record["id"]}), data=data)
        self.assertEqual(capability["language_mappings"], record["language_mappings"])
        compatibility = self.result(client.call("check_compatibility", {"sdk_version": data["sdk_version"]}), data=data)
        self.assertIs(compatibility["coordinate_matches"]["sdk_version"], True)

    def test_actual_repository_evidence_and_release_records(self):
        override = os.environ.get("SCO_CORE_MCP_TEST_CATALOGUE")
        path = Path(override) if override else ROOT / "dist/sdk-catalogue.json"
        if not path.is_file():
            self.skipTest("No build catalogue; build one or set SCO_CORE_MCP_TEST_CATALOGUE")
        data = json.loads(path.read_text(encoding="utf-8"))
        client = self.start(catalogue=path, repo_root=ROOT)
        registry = json.loads((ROOT / "sdk/status_registry.json").read_text(encoding="utf-8"))
        labels = {e["service"]: e["stability"] for e in registry["services"] if e["kind"] == "service"}
        services = self.result(client.call("list_services"), data=data)
        for item in services["services"]:
            with self.subTest(table=item["table"]):
                game = data["tables"][item["table"]]["source_header"].startswith("include/sc_")
                self.assertEqual(item["stability"], labels.get(item["service"], "unknown") if game else "stable")
        # One record per service keeps the session under the 120-call rate limit.
        for record in {r["service"]: r for r in data["records"]}.values():
            capability = self.result(client.call("get_capability_status", {"symbol_or_id": record["id"]}), data=data)
            state = capability["evidence_state"]
            with self.subTest(symbol=record["symbol"]):
                game = record["source_header"].startswith("include/sc_")
                self.assertIn(capability["stability"], {"stable", "experimental"} if not game or record["service"] in labels
                              else {"unknown"})
                self.assertEqual(state["scope"], "game-pack" if game else "neutral")
                self.assertNotEqual(state["gameplay_receipt"]["state"], "verified")
                if game:
                    self.assertIn(state["service_status"]["state"], {"rows_found", "no_rows"})
                else:
                    self.assertEqual(state["sigcheck_receipt"]["state"], "not_applicable")
                    self.assertNotIn("service_status", state)
        decisions = ROOT / "docs/legal/release-decisions"
        for record_path in sorted(decisions.glob("sdk-v*.json")):
            recorded = json.loads(record_path.read_text(encoding="utf-8"))
            if recorded.get("schema_version") != 2:
                continue
            with self.subTest(tag=record_path.stem):
                arguments = {"tag": record_path.stem, "source_sha": recorded["source"]["sha"]}
                release = self.result(client.call("check_compatibility", arguments), data=data)["release"]
                self.assertEqual(release["state"], "matching_approval_record")
                self.assertEqual(release["signature_verification"], "not_performed")
                self.assertEqual(release["records"][0]["signature_file"],
                                 "present" if record_path.with_name(record_path.name + ".sig").is_file() else "absent")

    def test_actual_guides_beside_the_build_catalogue(self):
        """sdk-guides.json beside the build catalogue (CI builds both), else the in-memory source build."""
        override = os.environ.get("SCO_CORE_MCP_TEST_CATALOGUE")
        path = Path(override) if override else ROOT / "dist/sdk-catalogue.json"
        if not path.is_file():
            self.skipTest("No build catalogue; build one or set SCO_CORE_MCP_TEST_CATALOGUE")
        data = json.loads(path.read_text(encoding="utf-8"))
        beside = path.with_name("sdk-guides.json")
        client = self.start(catalogue=path, repo_root=ROOT, extended=True)
        found = self.result(client.call("search_guides", {"query": "start your first plugin", "limit": 25}), data=data)
        self.assertIn("docs/getting-started.md", {section["doc"] for section in found["sections"]})
        if beside.is_file():
            guides = json.loads(beside.read_text(encoding="utf-8"))
            self.assertEqual(found["guides"]["mode"], "file")
            self.assertEqual(found["guides"]["source_sha"], guides["source_sha"])
            self.assertIs(found["guides"]["matches_catalogue_source_sha"], guides["source_sha"] == data["source_sha"])
        else:
            self.assertEqual(found["guides"]["mode"], "source")


# Synthetic stand-ins for the operator's programs: Python scripts the server runs with its own
# interpreter, so nothing is compiled. Each records its arguments next to its output.
FAKE_CHECK = r'''
import os, subprocess, sys, time
folder = sys.argv[1]
with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "check-ran.log"), "a") as log:
    log.write(folder + "\n")
marker = lambda name: os.path.exists(os.path.join(folder, name))
print(folder)
print("  args " + " ".join(sys.argv[2:]), flush=True)
if marker("hang"):
    subprocess.Popen([sys.executable, "-c", "import sys, time; time.sleep(4); open(sys.argv[1], 'w').write('survived')",
                      os.path.join(folder, "survived")])
    print("hanging", flush=True)
    time.sleep(60)
if marker("noisy"):
    print("FAIL early finding")
    for number in range(5000):
        print("line %05d of synthetic output" % number)
    print("FAILED: 1 failure(s)")
    sys.exit(1)
if marker("bad"):
    print("FAIL plugin.ini: missing key 'entry'")
    print("FAIL id 'bad' does not match folder 'other'")
    print("FAILED: 2 failure(s)")
    sys.exit(1)
print("OK: 0 failure(s)")
'''
FAKE_CMAKE = r'''
import json, os, sys, time
args = sys.argv[1:]
def record(build, name):
    with open(os.path.join(build, name), "w") as stream:
        json.dump({"args": args, "nodereuse": os.environ.get("MSBUILDDISABLENODEREUSE")}, stream)
if args[0] == "-S":
    source, build = args[1], args[3]
    os.makedirs(build, exist_ok=True)
    record(build, "configure-args.json")
    print("-- Configuring done")
    if os.path.exists(os.path.join(source, "fail-configure")):
        print("CMake Error: synthetic configure failure")
        sys.exit(1)
    open(os.path.join(build, "CMakeCache.txt"), "w").close()
    print("-- Generating done")
    sys.exit(0)
if args[0] == "--build":
    build = args[1]
    source = os.path.dirname(build)
    record(build, "build-args.json")
    if os.path.exists(os.path.join(source, "hang-build")):
        print("building slowly", flush=True)
        time.sleep(60)
    if os.path.exists(os.path.join(source, "fail-build")):
        print("error: synthetic compile failure")
        sys.exit(1)
    print("Build succeeded")
    sys.exit(0)
sys.exit(2)
'''
FAKE_CTEST = r'''
import json, os, sys
args = sys.argv[1:]
build = args[1]
with open(os.path.join(build, "ctest-args.json"), "w") as stream:
    json.dump(args, stream)
if os.path.exists(os.path.join(os.path.dirname(build), "fail-test")):
    print("50% tests passed, 1 tests failed out of 2")
    sys.exit(8)
print("100% tests passed, 0 tests failed out of 2")
'''
DEV_ANNOTATIONS = {"readOnlyHint": False, "destructiveHint": True, "idempotentHint": False, "openWorldHint": False}
REFUSED = "inside a --project-root"


class DevToolTests(ServerCase):
    """Opt-in run_plugin_check, build_plugin_project and test_plugin_project, with synthetic programs."""

    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="sco_mcp_dev_")
        self.addCleanup(temporary.cleanup)
        base = Path(temporary.name)
        self.tools, self.root, self.outside = base / "tools", base / "projects", base / "outside"
        for folder in (self.tools, self.root, self.outside, self.tools / "sdk" / "cmake"):
            folder.mkdir(parents=True, exist_ok=True)
        (self.tools / "sdk" / "cmake" / "sco-plugin.cmake").write_text("# synthetic\n", encoding="utf-8")
        for name, text in (("check.py", FAKE_CHECK), ("cmake.py", FAKE_CMAKE), ("ctest.py", FAKE_CTEST)):
            (self.tools / name).write_text(text, encoding="utf-8")
        (self.outside / "plugin").mkdir()
        (self.outside / "plugin" / "plugin.ini").write_text("id = plugin\n", encoding="utf-8")
        (self.outside / "CMakeLists.txt").write_text("# outside\n", encoding="utf-8")

    def options(self, timeout=None, limit=None, checker=True, cmake=True, sdk=True):
        options = ["--enable-dev-tools", "--project-root", self.root, "--cmake-generator", "Fake Generator",
                   "--cmake-platform", "x64"]
        options += ["--plugin-check", self.tools / "check.py"] if checker else []
        options += ["--cmake", self.tools / "cmake.py"] if cmake else []
        options += ["--sco-sdk", self.tools / "sdk"] if sdk else []
        options += ["--dev-timeout", timeout] if timeout is not None else []
        options += ["--dev-output-limit", limit] if limit is not None else []
        return options

    def client(self, **kwargs):
        return self.start(extra=self.options(**kwargs))

    def folder(self, name, *markers):
        folder = self.root / name
        folder.mkdir(parents=True, exist_ok=True)
        (folder / "plugin.ini").write_text("id = %s\n" % name, encoding="utf-8")
        for marker in markers:
            (folder / marker).write_text("", encoding="utf-8")
        return folder

    def project(self, name, *markers):
        project = self.folder(name, *markers)
        (project / "CMakeLists.txt").write_text("# synthetic\n", encoding="utf-8")
        return project

    def symlink(self, link, target):
        try:
            link.symlink_to(target, target_is_directory=True)
        except (OSError, NotImplementedError) as error:
            self.skipTest("cannot create a directory symlink here: " + type(error).__name__)

    def refused(self, client, name, arguments, token=REFUSED):
        result = self.result(client.call(name, arguments), is_error=True)
        self.assertIn(token, result["error"])
        return result

    def test_disabled_by_default_and_tool_lists(self):
        for extended, order in ((False, ORIGINAL_ORDER), (True, EXTENDED_ORDER)):
            client = self.start(extended=extended)
            self.assertEqual([tool["name"] for tool in client.request("tools/list")["result"]["tools"]], order)
            for name in DEV_ORDER:
                with self.subTest(extended=extended, name=name):
                    self.rpc_error(client.call(name, {"folder": str(self.root)}), -32602)
        for extended, order in ((False, ORIGINAL_ORDER), (True, EXTENDED_ORDER)):
            tools = self.start(extended=extended, extra=self.options()).request("tools/list")["result"]["tools"]
            self.assertEqual([tool["name"] for tool in tools], order + DEV_ORDER)
            for tool in tools[len(order):]:
                self.assertEqual(tool["annotations"], DEV_ANNOTATIONS)
                self.assertIs(tool["inputSchema"]["additionalProperties"], False)

    def test_startup_refusals(self):
        missing = self.root / "missing"
        for options in (["--enable-dev-tools"], ["--project-root", self.root], ["--cmake", self.tools / "cmake.py"],
                        ["--enable-dev-tools", "--project-root", missing],
                        ["--enable-dev-tools", "--project-root", self.tools / "check.py"],
                        self.options(timeout=0), self.options(limit=10)):
            with self.subTest(options=[str(option) for option in options]):
                run = subprocess.run([sys.executable, str(SERVER), "--catalogue", str(CATALOGUE)] + [str(o) for o in options],
                                     stdin=subprocess.DEVNULL, capture_output=True, timeout=30)
                self.assertNotEqual(run.returncode, 0)
                self.assertEqual(run.stdout, b"")

    def test_plugin_check_pass_fail_and_short_mode(self):
        client = self.client()
        good, bad = self.folder("good"), self.folder("bad", "bad")
        result = self.result(client.call("run_plugin_check", {"folder": str(good)}))
        self.assertEqual((result["state"], result["exit_code"], result["passed"]), ("completed", 0, True))
        self.assertEqual((result["mode"], result["arguments"], result["findings"]), ("full", [str(good.resolve())], []))
        self.assertEqual((result["summary"], result["program"]), ("OK: 0 failure(s)", "check.py"))
        self.assertFalse(result["output_truncated"])
        self.assertTrue(any("not run in game" in line for line in result["conclusions"]))
        short = self.result(client.call("run_plugin_check", {"folder": str(good), "mode": "short"}))
        self.assertEqual(short["arguments"], [str(good.resolve()), "--short-tables"])
        self.assertIn("args --short-tables", short["output"])
        failed = self.result(client.call("run_plugin_check", {"folder": "bad"}))  # relative: from the first root
        self.assertEqual((failed["exit_code"], failed["passed"], failed["summary"]), (1, False, "FAILED: 2 failure(s)"))
        self.assertEqual(failed["findings"], ["plugin.ini: missing key 'entry'", "id 'bad' does not match folder 'other'"])
        self.assertEqual(failed["folder"], str(bad.resolve()))

    def test_paths_outside_the_roots_are_refused_before_running(self):
        client = self.client()
        self.folder("inner")
        (self.root / "file.txt").write_text("", encoding="utf-8")
        outside = self.outside / "plugin"
        for folder in (str(outside), "../outside/plugin", "inner/../../outside/plugin", str(self.root / ".." / "outside" / "plugin"),
                       "missing", "file.txt", "inner\x00", str(self.tools)):
            with self.subTest(folder=folder):
                self.refused(client, "run_plugin_check", {"folder": folder})
                self.assertFalse((self.tools / "check-ran.log").exists())
        for name in ("build_plugin_project", "test_plugin_project"):
            with self.subTest(name=name):
                self.refused(client, name, {"project": str(self.outside)})
                self.refused(client, name, {"project": "../outside"})
        self.assertFalse((self.outside / "build").exists())

    def test_symlink_escape_is_refused(self):
        self.symlink(self.root / "escape", self.outside / "plugin")
        self.symlink(self.root / "projlink", self.outside)
        inner = self.folder("inner")
        self.symlink(self.root / "alias", inner)
        client = self.client()
        self.refused(client, "run_plugin_check", {"folder": "escape"})
        self.refused(client, "build_plugin_project", {"project": "projlink"})
        self.assertFalse((self.tools / "check-ran.log").exists())
        # A link that stays inside a root is followed; the result names the resolved folder.
        followed = self.result(client.call("run_plugin_check", {"folder": "alias"}))
        self.assertEqual((followed["passed"], followed["folder"]), (True, str(inner.resolve())))
        project = self.project("proj")
        self.symlink(project / "build", self.outside)
        self.refused(client, "build_plugin_project", {"project": "proj"}, "build entry")
        self.assertFalse((self.outside / "configure-args.json").exists())

    def test_timeout_kills_the_process_tree(self):
        folder = self.folder("slow", "hang")
        client = self.client(timeout=2)
        started = time.monotonic()
        result = self.result(client.call("run_plugin_check", {"folder": "slow"}))
        self.assertLess(time.monotonic() - started, 30)
        self.assertEqual((result["state"], result["timed_out"], result["passed"]), ("timed_out", True, False))
        self.assertNotEqual(result["exit_code"], 0)
        self.assertIn("hanging", result["output"])  # the grandchild had been started
        time.sleep(max(0.0, started + 7 - time.monotonic()))
        self.assertFalse((folder / "survived").exists(), "a grandchild outlived the timeout")

    def test_output_is_capped_but_findings_cover_the_whole_stream(self):
        self.folder("noisy", "noisy")
        result = self.result(self.client(limit=1024).call("run_plugin_check", {"folder": "noisy"}))
        self.assertEqual((result["exit_code"], result["passed"], result["output_limit"]), (1, False, 1024))
        self.assertTrue(result["output_truncated"])
        self.assertGreater(result["output_bytes"], 100000)
        self.assertLessEqual(len(result["output"].encode("utf-8")), 1024)
        self.assertIn("FAILED: 1 failure(s)", result["output"])
        self.assertNotIn("FAIL early finding", result["output"])
        self.assertEqual((result["findings"], result["summary"]), (["early finding"], "FAILED: 1 failure(s)"))

    def test_build_and_test_use_fixed_arguments(self):
        project = self.project("demo").resolve()
        client = self.client()
        self.refused(client, "test_plugin_project", {"project": "demo"}, "not configured")
        built = self.result(client.call("build_plugin_project", {"project": "demo"}))
        self.assertEqual((built["state"], built["exit_code"], built["passed"]), ("completed", 0, True))
        self.assertEqual([step["step"] for step in built["steps"]], ["configure", "build"])
        build = project / "build"
        configure = json.loads((build / "configure-args.json").read_text(encoding="utf-8"))
        self.assertEqual(configure, {"args": ["-S", str(project), "-B", str(build), "-G", "Fake Generator", "-A", "x64",
                                              "-DSCO_SDK=" + str(self.tools / "sdk")], "nodereuse": "1"})
        self.assertEqual(json.loads((build / "build-args.json").read_text(encoding="utf-8"))["args"],
                         ["--build", str(build.resolve()), "--config", "Release"])
        self.assertIn("Build succeeded", built["steps"][1]["output"])
        tested = self.result(client.call("test_plugin_project", {"project": str(project)}))
        self.assertEqual((tested["exit_code"], tested["passed"], tested["program"]), (0, True, "ctest.py"))
        self.assertEqual(tested["summary"], "100% tests passed, 0 tests failed out of 2")
        self.assertEqual(json.loads((build / "ctest-args.json").read_text(encoding="utf-8")),
                         ["--test-dir", str(build.resolve()), "-C", "Release", "--output-on-failure", "--no-tests=error"])

    def test_build_and_test_failures(self):
        client = self.client()
        self.project("noconf", "fail-configure")
        configure = self.result(client.call("build_plugin_project", {"project": "noconf"}))
        self.assertEqual(([s["step"] for s in configure["steps"]], configure["exit_code"], configure["passed"]),
                         (["configure"], 1, False))
        self.project("nobuild", "fail-build")
        build = self.result(client.call("build_plugin_project", {"project": "nobuild"}))
        self.assertEqual((len(build["steps"]), build["exit_code"], build["passed"]), (2, 1, False))
        self.assertIn("synthetic compile failure", build["steps"][1]["output"])
        self.project("notest", "fail-test")
        self.assertTrue(self.result(client.call("build_plugin_project", {"project": "notest"}))["passed"])
        tested = self.result(client.call("test_plugin_project", {"project": "notest"}))
        self.assertEqual((tested["exit_code"], tested["passed"]), (8, False))
        self.assertEqual(tested["summary"], "50% tests passed, 1 tests failed out of 2")
        self.folder("nolists")
        self.refused(client, "build_plugin_project", {"project": "nolists"}, "CMakeLists.txt")

    def test_build_timeout(self):
        self.project("stuck", "hang-build")
        result = self.result(self.client(timeout=2).call("build_plugin_project", {"project": "stuck"}))
        self.assertEqual((result["state"], result["passed"]), ("timed_out", False))
        self.assertEqual([step["state"] for step in result["steps"]], ["completed", "timed_out"])

    def test_unconfigured_programs_are_unavailable(self):
        self.folder("good")
        self.project("demo")
        client = self.client(checker=False, cmake=False, sdk=False)
        checked = self.result(client.call("run_plugin_check", {"folder": "good"}))
        self.assertEqual((checked["state"], checked["passed"]), ("unavailable", False))
        self.assertIn("--plugin-check", checked["reason"])
        built = self.result(client.call("build_plugin_project", {"project": "demo"}))
        self.assertEqual((built["state"], built["passed"], built["steps"]), ("unavailable", False, []))
        self.assertFalse((self.root / "demo" / "build").exists())

    def test_arguments_are_strict(self):
        client = self.client()
        self.folder("good")
        for name, arguments in (("run_plugin_check", {"folder": "good", "mode": "fast"}),
                                ("run_plugin_check", {"folder": "good", "args": ["--invoke", "x"]}),
                                ("run_plugin_check", {"folder": 1}),
                                ("run_plugin_check", {}),
                                ("build_plugin_project", {"project": "good", "generator": "Ninja"}),
                                ("test_plugin_project", {"project": ""})):
            with self.subTest(name=name, arguments=arguments):
                self.result(client.call(name, arguments), is_error=True)
        self.assertFalse((self.tools / "check-ran.log").exists())


README = HERE / "README.md"
AGENT_GUIDE = ROOT / "docs" / "agent-integration.md"
TOOL_TABLE = "| Tool | Arguments | Purpose |"
TOOL_ROW = re.compile(r"^\|\s*`([a-z_]+)`\s*\|([^|]*)\|[^|]*\|\s*$")


def documented_tools(text):
    """{tool: (required, optional)} from the rows of README tables headed TOOL_TABLE.

    The Arguments cell lists required names, then `optional:` and the optional names, or `none`.
    """
    tools, problems, in_table = {}, [], False
    for number, line in enumerate(text.splitlines(), 1):
        if line.strip() == TOOL_TABLE:
            in_table = True
            continue
        in_table = in_table and line.startswith("|")
        match = TOOL_ROW.match(line) if in_table else None
        if not match:
            if in_table and not re.match(r"^\|[-| ]+\|\s*$", line):
                problems.append(f"README line {number}: unreadable tool table row")
            continue
        name, cell = match.groups()
        required, _, optional = cell.partition("optional:")
        names = [re.findall(r"`([^`]+)`", part) for part in (required, optional)]
        if not any(names) and cell.strip() != "none":
            problems.append(f"README line {number}: {name} has no argument names and is not `none`")
        if name in tools:
            problems.append(f"README line {number}: {name} is documented twice")
        tools[name] = (names[0], names[1])
    return tools, problems


def documentation_refusals(text, listed):
    """Each difference between the README tool tables and a tools/list result, as one line."""
    documented, refusals = documented_tools(text)
    order = [tool["name"] for tool in listed]
    if list(documented) != order:
        refusals.append(f"README tool tables list {list(documented)}, tools/list returns {order}")
    for tool in listed:
        if tool["name"] not in documented:
            continue
        schema = tool["inputSchema"]
        required = list(schema.get("required", []))
        optional = [key for key in schema.get("properties", {}) if key not in required]
        doc_required, doc_optional = documented[tool["name"]]
        if (sorted(doc_required), sorted(doc_optional)) != (sorted(required), sorted(optional)):
            refusals.append(f"README documents {tool['name']} arguments {doc_required} optional "
                            f"{doc_optional}; tools/list has {required} optional {optional}")
    return refusals


class DocumentationTests(ServerCase):
    """The README's tool tables and argument names are the server's actual tools/list."""

    def listed(self):
        root = tempfile.TemporaryDirectory(prefix="sco_mcp_test_")
        self.addCleanup(root.cleanup)
        return self.start(extended=True, extra=["--enable-dev-tools", "--project-root", root.name]).request(
            "tools/list")["result"]["tools"]

    def test_readme_tool_tables_match_tools_list(self):
        self.assertEqual(documentation_refusals(README.read_text(encoding="utf-8"), self.listed()), [])

    def test_agent_guide_names_every_tool(self):
        guide = AGENT_GUIDE.read_text(encoding="utf-8")
        for name in EXTENDED_ORDER + DEV_ORDER:
            with self.subTest(name=name):
                self.assertIn(f"`{name}`" if name not in ORIGINAL_ORDER else f'"{name}"', guide)

    def test_stale_or_missing_documentation_is_refused(self):
        listed, readme = self.listed(), README.read_text(encoding="utf-8")
        stale = readme.replace("| `get_symbol` | `symbol_or_id` |", "| `get_symbol` | `symbol` |")
        self.assertNotEqual(stale, readme)
        for text, token in (
                (stale, "README documents get_symbol arguments ['symbol']"),
                (readme.replace("`id`, `lang`; optional: `with_menu`, `services`", "`id`; optional: `lang`"),
                 "generate_plugin_scaffold"),
                (readme.replace("| `get_engine_native` | `id_or_hash` |", "| `get_engine_native` | none |"),
                 "get_engine_native arguments []"),
                ("\n".join(line for line in readme.splitlines() if "| `list_services` |" not in line),
                 "README tool tables list")):
            with self.subTest(token=token):
                refusals = documentation_refusals(text, listed)
                self.assertTrue(any(token in line for line in refusals), refusals)


if __name__ == "__main__":
    unittest.main()
