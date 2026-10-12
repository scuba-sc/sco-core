#!/usr/bin/env python3
"""Black-box stdlib tests for the catalogue-only MCP stdio server.

Run: python3 -m unittest discover -s tools/sco-core-mcp -p test_server.py -v
An actual build is exercised when SCO_CORE_MCP_TEST_CATALOGUE is set, or when
 dist/sdk-catalogue.json exists. That integration test otherwise skips explicitly.
All fixture records are synthetic; none assert runtime or game compatibility.
"""
import importlib
import json
import os
from pathlib import Path
import queue
import shutil
import subprocess
import sys
import tempfile
import threading
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
PROVENANCE_KEYS = {"schema_version", "sdk_version", "game_pack_version", "source_sha"}


def fixture():
    return json.loads(CATALOGUE.read_text(encoding="utf-8"))


class Client:
    """One real child process; bounded reads without platform-specific selectors."""

    def __init__(self, catalogue=CATALOGUE, repo_root=None, use_env=False):
        command = [sys.executable, str(SERVER)]
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

    def start(self, catalogue=CATALOGUE, repo_root=None, use_env=False, ready=True):
        client = Client(catalogue, repo_root, use_env)
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
                     "size": table["size"], "member_count": len(table["members"])}
                    for name, table in sorted(fixture()["tables"].items())]
        self.assertEqual(data["services"], expected)

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


if __name__ == "__main__":
    unittest.main()
