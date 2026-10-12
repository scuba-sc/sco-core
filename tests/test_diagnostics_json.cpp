// SPDX-License-Identifier: GPL-3.0-only
// Synthetic closed-contract probes; callable by the parent IPC fixture.
#include "ipc/diagnostics_json.h"
#include "sco/caps.h"
#include "sco/signatures.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#define JSON_CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); std::abort(); } } while (0)
using namespace sco::diagnostics::json;
namespace {
void Parser() {
    Object o;
    JSON_CHECK(Parse(" {\"key\":\"\\u0061\",\"n\":18446744073709551615,\"nil\":null} \n", o));
    JSON_CHECK(o.strings.at("key") == "a" && o.integers.at("n") == std::numeric_limits<uint64_t>::max() && o.nulls.count("nil"));
    JSON_CHECK(Parse("{\"emoji\":\"\\ud83d\\ude00\"}", o));
    JSON_CHECK(o.strings.at("emoji") == "\xf0\x9f\x98\x80");
    const char* invalid[]{"", "[]", "null", "{\"x\":true}", "{\"x\":false}", "{\"x\":-1}", "{\"x\":1.0}", "{\"x\":1e0}", "{\"x\":NaN}", "{\"x\":Infinity}", "{\"x\":01}", "{\"x\":18446744073709551616}", "{\"x\":{}}", "{\"x\":[]}", "{\"x\":1,}", "{\"x\":1}x", "{\"x\":1,\"\\u0078\":null}", "{\"x\":\"\\ud800\"}", "{\"x\":\"\\udc00\"}", "{\"x\":\"\\ud800\\u0041\"}", "{\"x\":\"\\x41\"}", "{\"x\":\"\n\"}"};
    for (const auto* bad : invalid) { JSON_CHECK(!Parse(bad, o)); JSON_CHECK(o.strings.empty() && o.integers.empty() && o.nulls.empty()); }
    for (const auto& bad : {std::string("{\"x\":\"\xc0\xaf\"}"), std::string("{\"x\":\"\xed\xa0\x80\"}"), std::string("{\"x\":\"\xf4\x90\x80\x80\"}"), std::string("{\"x\":\"\x80\"}"), std::string("{\"\xff\":1}")}) JSON_CHECK(!Parse(bad, o));
    JSON_CHECK(!Parse(std::string(17, '[') + "0" + std::string(17, ']'), o));
    JSON_CHECK(!Parse("{\"x\":\"" + std::string(4096, 'a') + "\"}", o));
}
void Validation() {
    Object o; JSON_CHECK(Parse("{}", o));
    JSON_CHECK(Read(0x100, o) && Read(0x101, o) && Read(0x103, o) && Read(0x104, o) && Read(0x105, o));
    JSON_CHECK(!Read(0x102, o) && !Read(0x106, o));
    JSON_CHECK(Parse("{\"plugin_id\":\"fixture\",\"after_sequence\":\"18446744073709551615\",\"limit\":100}", o) && Read(0x102, o));
    JSON_CHECK(Parse("{\"state\":\"faulted\",\"limit\":50,\"cursor\":\"A_b-1\"}", o) && Read(0x101, o));
    JSON_CHECK(Parse("{\"service\":\"fixture.service\",\"limit\":1}", o) && Read(0x103, o));
    JSON_CHECK(Parse("{\"row_id\":\"fixture.row\"}", o) && Read(0x104, o));
    const char* bad[]{"{\"limit\":0}", "{\"limit\":51}", "{\"limit\":\"2\"}", "{\"cursor\":null}", "{\"cursor\":\"\"}", "{\"state\":\"Loaded\"}", "{\"path\":\"x\"}"};
    for (auto* s : bad) { JSON_CHECK(Parse(s, o)); JSON_CHECK(!Read(0x101, o)); }
    JSON_CHECK(Parse("{\"service\":\"a..b\"}", o) && !Read(0x103, o));
    JSON_CHECK(Parse("{\"plugin_id\":\"fixture\",\"after_sequence\":\"18446744073709551616\"}", o) && !Read(0x102, o));
    const std::string nonce(64, 'a');
    JSON_CHECK(Parse("{\"client_nonce\":\"" + nonce + "\",\"expected_host_build\":null}", o) && Control(1, o));
    JSON_CHECK(!Control(2, o) && !Control(3, o));
    JSON_CHECK(Parse("{\"client_nonce\":\"" + nonce + "\",\"host_nonce\":\"" + nonce + "\",\"host_build\":\"fixture-host-1\"}", o) && Control(2, o));
    JSON_CHECK(Parse("{\"client_nonce\":\"" + nonce + "\",\"host_nonce\":\"" + nonce + "\"}", o) && Control(3, o) && Control(4, o));
    JSON_CHECK(Parse("{\"reason\":\"revoked\"}", o) && Control(5, o));
    JSON_CHECK(Parse("{\"reason\":\"unexpected\"}", o) && !Control(5, o));
    JSON_CHECK(Parse("{\"client_nonce\":\"" + std::string(64, 'A') + "\",\"expected_host_build\":null}", o) && !Control(1, o));
    JSON_CHECK(Parse("{\"client_nonce\":\"" + nonce + "\",\"expected_host_build\":\"/private/fixture\"}", o) && !Control(1, o));
    JSON_CHECK(Parse("{\"client_nonce\":\"" + nonce + "\",\"expected_host_build\":\"0x123\"}", o) && !Control(1, o));
    o = {}; o.strings["limit"] = "1"; o.integers["limit"] = 1;
    JSON_CHECK(!Read(0x101, o)); // externally assembled overlapping types also fail closed
}
void Serialization() {
    using namespace sco::diagnostics;
    sco::SetGameThread();
    SnapshotStore store; Capture capture; Object empty;
    const std::string session(32, 'a');
    const auto missing = Serialize(0x100, empty, store, capture, session, 1, 100);
    JSON_CHECK(missing == Error("unknown_metadata"));
    JSON_CHECK(missing.find("\"session_id\":null") != std::string::npos);
    JSON_CHECK(Error("arbitrary \"injection") == Error("invalid_request"));
    static const sco::SigDef row[]{ {"fixture.json_row", nullptr, 0, 0, nullptr, {}} };
    JSON_CHECK(sco::RegisterSignatures(row, 1));
    JSON_CHECK(sco::caps::Set("fixture.json_cap", true) == sco::Result::Ok);
    std::vector<sco::plugins::Plugin> plugins(2);
    for (size_t i = 0; i < plugins.size(); ++i) {
        plugins[i].manifestOk = true; plugins[i].manifest.id = "fixture_" + std::to_string(i);
        plugins[i].manifest.version = "1.0"; plugins[i].manifest.kind = sco::plugins::Kind::Native;
        plugins[i].state = sco::plugins::State::Loaded;
    }
    static const uint8_t shortTable = 0; static const int serviceOwner = 0;
    JSON_CHECK(sco::ProvideService(&serviceOwner, nullptr, "fixture.json_service", 0x10002, &shortTable) == sco::Result::Ok);
    JSON_CHECK(SetServiceDiagnosticMetadata(&serviceOwner, "fixture.json_service", nullptr, "host", 1) == sco::Result::Ok);
    const bool exposedBefore = sco::ServiceTableHandedOut(&serviceOwner);
    HostMetadata host; host.hostBuild = "fixture-host-1"; host.sourceSha = std::string(40, 'a'); host.readiness = "ready";
    LifecycleMetadata game; game.lastTickMs = 90; game.lifecycle = "ready"; game.offlineAdmission = "authorized";
    game.gameServicesRequested = true; game.gameServicesBuilt = false; game.gameServicesPublished = false;
    OptionalSources sources; sources.gamePack = GamePackMetadata{"fixture-pack-1", "fixture-game-1"};
    JSON_CHECK(store.Begin(plugins, host, game, 100, sources));
    size_t steps = 0; while (!store.Advance(100)) JSON_CHECK(++steps < 2048);
    JSON_CHECK(capture.Start(100)); int key = 0; JSON_CHECK(capture.AssignKey(&key, "fixture_0"));
    capture.RecordHost(&key, HostEvent::Loaded, 100);
    const char* requests[]{"{}", "{\"limit\":1}", "{\"plugin_id\":\"fixture_0\"}", "{}", "{}", "{}"};
    for (uint16_t opcode = 0x100; opcode <= 0x105; ++opcode) {
        Object input; JSON_CHECK(Parse(requests[opcode - 0x100], input));
        auto response = Serialize(opcode, input, store, capture, session, 1, 200);
        JSON_CHECK(response.find("\"status\":\"ok\"") != std::string::npos);
        JSON_CHECK(response.find("\"session_id\":\"" + session + "\"") != std::string::npos);
        JSON_CHECK(response.find("\"generation\":\"1\"") != std::string::npos);
        JSON_CHECK(response.find("\"observed_ms\":100,\"age_ms\":100") != std::string::npos);
        JSON_CHECK(response.find("\"error\":null") != std::string::npos && response.size() <= 65432);
        if (opcode == 0x101) JSON_CHECK(response.find("\"has_more\":true,\"next_cursor\":\"c_") != std::string::npos);
        if (opcode == 0x102) JSON_CHECK(response.find("\"untrusted_content\":true") != std::string::npos);
        if (opcode == 0x105) JSON_CHECK(response.find("\"last_tick_age_ms\":110") != std::string::npos);
#ifdef SCO_DIAGNOSTICS_JSON_TEST_MAIN
        std::printf("JSON_VECTOR %u %s\n", opcode, response.c_str());
#endif
    }
    const auto before = store.Latest(); plugins[0].manifest.id = "mutated";
    JSON_CHECK(Serialize(0x101, empty, store, capture, session, 1, 200).find("fixture_0") != std::string::npos);
    JSON_CHECK(store.Latest() == before); // serialization never triggers a refresh
    JSON_CHECK(Serialize(0x100, empty, store, capture, session, 1, 1100).find("\"status\":\"ok\"") != std::string::npos);
    JSON_CHECK(Serialize(0x100, empty, store, capture, session, 1, 1101).find("\"status\":\"stale\"") != std::string::npos);
    JSON_CHECK(Serialize(0x100, empty, store, capture, session, 1, 99) == Error("invalid_request"));
    JSON_CHECK(Serialize(0x100, empty, store, capture, "bad", 1, 200) == Error("invalid_request"));
    JSON_CHECK(Serialize(0x100, empty, store, capture, session, 0, 200) == Error("invalid_request"));
    JSON_CHECK(Serialize(0x100, empty, store, capture, session, 1, kMaxMonotonicMs + 1) == Error("invalid_request"));
    Object filter; JSON_CHECK(Parse("{\"row_id\":\"fixture.missing\"}", filter));
    JSON_CHECK(Serialize(0x104, filter, store, capture, session, 1, 200) == Error("not_found"));
    JSON_CHECK(Parse("{\"cursor\":\"unknown\"}", filter));
    JSON_CHECK(Serialize(0x101, filter, store, capture, session, 1, 200) == Error("snapshot_changed"));
    plugins[1].state = sco::plugins::State::Faulted;
    JSON_CHECK(store.Begin(plugins, host, game, 1100, sources));
    steps = 0; while (!store.Advance(1100)) JSON_CHECK(++steps < 2048);
    JSON_CHECK(Serialize(0x100, empty, store, capture, session, 1, 1100).find("\"status\":\"restart_required\"") != std::string::npos);
    JSON_CHECK(Serialize(0x105, empty, store, capture, session, 1, 2101).find("\"status\":\"stale\"") != std::string::npos);
    JSON_CHECK(Parse("{\"cursor\":\"c_1\",\"limit\":1}", filter));
    const auto oldPage = Serialize(0x101, filter, store, capture, session, 1, 1100);
    JSON_CHECK(oldPage.find("\"generation\":\"1\"") != std::string::npos && oldPage.find("fixture_1") != std::string::npos);
    JSON_CHECK(oldPage.find("\"observed_ms\":100,\"age_ms\":1000") != std::string::npos);
    JSON_CHECK(sco::ServiceTableHandedOut(&serviceOwner) == exposedBefore);
    JSON_CHECK(sco::ReleaseService(&serviceOwner, "fixture.json_service") == sco::Result::Ok);
    capture.Stop();
    Object tail; JSON_CHECK(Parse("{\"plugin_id\":\"fixture_0\"}", tail));
    JSON_CHECK(Serialize(0x102, tail, store, capture, session, 1, 1100) == Error("runtime_disabled"));
    store.Reset();
}
}
void RunDiagnosticsJsonTests() { Parser(); Validation(); Serialization(); }
#ifdef SCO_DIAGNOSTICS_JSON_TEST_MAIN
int main() { RunDiagnosticsJsonTests(); std::puts("diagnostics JSON parser/schema probes passed"); }
#endif
