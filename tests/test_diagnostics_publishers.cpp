// GPL-3.0-only. Synthetic publishers; no game, transport or personal identifiers.
#include "sco/diagnostics.h"
#include "sco_diagnostics_publisher.h"
#include "sco/host.h"
#include "sco/app.h"
#include "scosdk/diagnostics_publisher.hpp"
#ifndef SCO_KERNEL_ONLY
#include "sco/game/diagnostics.h"
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <array>
#include <cstddef>
#ifdef _WIN32
#include <windows.h>
#endif
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); std::abort(); } } while (0)
using namespace sco::diagnostics;
namespace sco::diagnostics { uint64_t FixtureQueryServiceCalls(); }
namespace {
unsigned callbacks = 0;
const sco_plugin_info* QueryBomb() { ++callbacks; return nullptr; }
sco_result LoadBomb(const sco_api*, sco_plugin*) { ++callbacks; return SCO_OK; }
void UnloadBomb() { ++callbacks; }
const void* fakeTable = nullptr;
sco_result fakeResult = SCO_OK;
sco_result FakeQuery(const char*, uint32_t, const void** out) { *out = fakeTable; return fakeResult; }
void WrapperExtents(sco_plugin* self) {
    sco::sdk::DiagnosticsPublisher publisher;
    sco_api api{}; api.size = sizeof(api); api.query_service = FakeQuery;
    alignas(sco_api) std::array<std::byte, offsetof(sco_api, query_service) + sizeof(api.query_service) - 1> root{};
    const uint32_t size = static_cast<uint32_t>(root.size());
    std::memcpy(root.data(), &size, sizeof(size));
    CHECK(publisher.Open(reinterpret_cast<const sco_api*>(root.data()), self) == SCO_UNAVAILABLE);
    api.query_service = nullptr; CHECK(publisher.Open(&api, self) == SCO_UNAVAILABLE);
    api.query_service = FakeQuery;
    sco_diagnostics_observation_v1 obs{sizeof(obs), SCO_DIAGNOSTICS_HEALTH_READY};
    alignas(sco_diagnostics_publisher_v1) std::array<std::byte, 15> shortTable{};
    const uint32_t shortSize = 15; std::memcpy(shortTable.data(), &shortSize, sizeof(shortSize));
    fakeTable = shortTable.data(); CHECK(publisher.Open(&api, self) == SCO_OK);
    CHECK(publisher.Submit(obs) == SCO_UNAVAILABLE);
    const sco_diagnostics_publisher_v1 nullTable{16, 0, nullptr};
    fakeTable = &nullTable; CHECK(publisher.Open(&api, self) == SCO_OK);
    CHECK(publisher.Submit(obs) == SCO_UNAVAILABLE);
    fakeTable = PublisherTable(); CHECK(publisher.Open(&api, self) == SCO_OK);
    fakeResult = SCO_NOT_FOUND; CHECK(publisher.Open(&api, self) == SCO_NOT_FOUND);
    CHECK(publisher.Submit(obs) == SCO_UNAVAILABLE); fakeResult = SCO_OK;
    fakeTable = nullptr; CHECK(publisher.Open(&api, self) == SCO_UNAVAILABLE);
}
}
int main(int argc, char** argv) {
    sco::SetGameThread();
    CHECK(!CopyPublisherMetadata(100));
    CHECK(StartPublisher({}) == sco::Result::Unavailable);
    CHECK(!sco::ServiceExists(SCO_DIAGNOSTICS_PUBLISHER_NAME));
    SourceOptions options; options.diagnosticsPlugin = true;
    options.diagnosticsPluginId = "fixture_diag";
    CHECK(StartPublisher(options) == sco::Result::Unavailable); // offline is not inferred
    options.offlineAuthorized = true;
    CHECK(StartPublisher(options) == sco::Result::Ok);
    auto* self = sco::host::NewPlugin("fixture_diag"); CHECK(self);
    WrapperExtents(self);
    auto* foreign = sco::host::NewPlugin("fixture_foreign"); CHECK(foreign);
    const auto* api = sco::host::BuildApi({"fixture-host-1"}); CHECK(api);
    sco::sdk::DiagnosticsPublisher publisher;
    CHECK(publisher.Open(api, self) == SCO_OK);
    sco_diagnostics_observation_v1 observation{sizeof(observation), SCO_DIAGNOSTICS_HEALTH_READY};
#ifdef _WIN32
    const auto beforeSubmit = GetTickCount64();
#endif
    CHECK(publisher.Submit(observation) == SCO_OK);
#ifdef _WIN32
    const auto afterSubmit = GetTickCount64();
#endif
    auto first = CopyPublisherMetadata(kMaxMonotonicMs); CHECK(first && first->available);
    CHECK(first->health == SCO_DIAGNOSTICS_HEALTH_READY && first->observedMs);
    const auto observed = *first->observedMs;
#ifdef _WIN32
    // Publisher timestamps must share the app snapshot/IPC GetTickCount64 epoch.
    // QPC-based steady_clock can otherwise be refused as a future observation.
    CHECK(observed >= beforeSubmit && observed <= afterSubmit);
#endif
    CHECK(!CopyPublisherMetadata(observed - 1)); // future source refused
    CHECK(PublisherTable()->submit(foreign, &observation) == SCO_BAD_ARG);
    CHECK(PublisherTable()->submit(reinterpret_cast<sco_plugin*>(1), &observation) == SCO_BAD_ARG);
    const uint32_t physicallyShortRecord = 4;
    CHECK(PublisherTable()->submit(self, reinterpret_cast<const sco_diagnostics_observation_v1*>(&physicallyShortRecord)) == SCO_BAD_ARG);
    observation.size = 4; CHECK(publisher.Submit(observation) == SCO_BAD_ARG);
    observation.size = sizeof(observation); observation.health = static_cast<sco_diagnostics_health>(99);
    CHECK(publisher.Submit(observation) == SCO_BAD_ARG);
    observation.health = SCO_DIAGNOSTICS_HEALTH_DEGRADED;
    CHECK(publisher.Submit(observation) == SCO_TOO_MANY); // bounded one push/sec, no overwrite
    CHECK(CopyPublisherMetadata(kMaxMonotonicMs)->health == SCO_DIAGNOSTICS_HEALTH_READY);
    std::thread wrong([&] { CHECK(publisher.Submit(observation) == SCO_WRONG_THREAD); CHECK(!CopyPublisherMetadata(kMaxMonotonicMs)); }); wrong.join();
    std::vector<sco::plugins::Plugin> list(1);
    list[0].manifestOk = true; list[0].manifest.id = "fixture_diag";
    list[0].manifest.version = "1.0.0"; list[0].state = sco::plugins::State::Loaded;
    list[0].exports = {QueryBomb, LoadBomb, UnloadBomb};
    OptionalSources sources; sources.diagnosticsPlugin = first;
#ifndef SCO_KERNEL_ONLY
    options.gamePack = true;
    options.gameBuild = "fixture-build-1";
    sources.gamePack = sco::game::diagnostics::CopyMinimal(options, observed);
    CHECK(sources.gamePack && sources.gamePack->version && sources.gamePack->gameBuild == "fixture-build-1");
    SourceOptions off; CHECK(!sco::game::diagnostics::CopyMinimal(off, observed));
    off.gamePack = true; CHECK(!sco::game::diagnostics::CopyMinimal(off, observed));
#endif
    const auto queryCalls = FixtureQueryServiceCalls();
    SnapshotStore store;
    CHECK(store.Begin(list, {}, {}, observed, sources));
    for (unsigned n = 0; !store.Advance(observed); ++n) CHECK(n < 1024);
    const auto snapshot = store.Latest(); CHECK(snapshot && snapshot->sources.diagnosticsPlugin);
    CHECK(snapshot->sources.diagnosticsPlugin->health == SCO_DIAGNOSTICS_HEALTH_READY);
    CHECK(FixtureQueryServiceCalls() == queryCalls && callbacks == 0); // capture calls no SDK/plugin functions
    CHECK(!snapshot->host.restartRequired); // advisory publisher cannot claim host faults
    StopPublisher(); CHECK(!CopyPublisherMetadata(kMaxMonotonicMs));
    CHECK(publisher.Submit(observation) == SCO_UNAVAILABLE);
    CHECK(!sco::ServiceExists(SCO_DIAGNOSTICS_PUBLISHER_NAME));
    CHECK(snapshot->sources.diagnosticsPlugin->health == SCO_DIAGNOSTICS_HEALTH_READY); // immutable
    CHECK(StartPublisher(options) == sco::Result::Ok); CHECK(!CopyPublisherMetadata(kMaxMonotonicMs));
    CHECK(publisher.Submit(observation) == SCO_OK);
    CHECK(sco::Release(self) == sco::Result::Ok);
    CHECK(!CopyPublisherMetadata(kMaxMonotonicMs));
    CHECK(publisher.Submit(observation) == SCO_BAD_ARG);
    StopPublisher(); CHECK(sco::Release(foreign) == sco::Result::Ok);
    sco::app::Platform platform; platform.hostVersion = "fixture-host-1";
    CHECK(sco::app::Start(platform));
    for (unsigned n = 0; !sco::app::DiagnosticSnapshot(); ++n) { CHECK(n < 1024); sco::app::Tick(n); }
    CHECK(!sco::app::DiagnosticSnapshot()->sources.gamePack && !sco::app::DiagnosticSnapshot()->sources.diagnosticsPlugin);
    sco::app::Stop(); CHECK(!sco::app::DiagnosticSnapshot());
    CHECK(argc == 3);
    namespace fs = std::filesystem;
    const fs::path root = fs::path(argv[2]) / "diagnostics_publishers_fixture";
    fs::remove_all(root); fs::create_directories(root / "diagnostics_publisher");
    fs::copy_file(argv[1], root / "diagnostics_publisher/publisher.module");
    { std::ofstream ini(root / "diagnostics_publisher/plugin.ini");
      ini << "id = diagnostics_publisher\nname = Synthetic diagnostic\nversion = 1.0.0\napi = 1.1\nkind = native\nentry = publisher.module\nrequires = sco.diagnostics.publisher\n"; }
    platform.pluginRoot = root; platform.pluginsEnabled = true;
    // Plugin loading does not imply source enablement: actual module refused.
    CHECK(sco::app::Start(platform));
    CHECK(sco::app::Plugins().size() == 1 && sco::app::Plugins()[0].state == sco::plugins::State::Refused);
    sco::app::Stop();
    platform.diagnosticSources.diagnosticsPlugin = true;
    platform.diagnosticSources.offlineAuthorized = true;
    platform.diagnosticSources.diagnosticsPluginId = "diagnostics_publisher";
    platform.diagnosticSources.gamePack = true;
    platform.diagnosticSources.gameBuild = "fixture-build-1";
    CHECK(sco::app::Start(platform));
    CHECK(sco::app::Plugins().size() == 1 && sco::app::Plugins()[0].state == sco::plugins::State::Loaded);
    const auto afterModuleLoad = FixtureQueryServiceCalls();
    for (unsigned n = 0; !sco::app::DiagnosticSnapshot(); ++n) { CHECK(n < 1024); sco::app::Tick(n); }
    const auto integrated = sco::app::DiagnosticSnapshot();
    CHECK(integrated->sources.diagnosticsPlugin && integrated->sources.diagnosticsPlugin->health == SCO_DIAGNOSTICS_HEALTH_READY);
    CHECK(integrated->game.offlineAdmission == "unknown"); // source options never grant endpoint admission
#ifndef SCO_KERNEL_ONLY
    CHECK(integrated->sources.gamePack && integrated->sources.gamePack->gameBuild == "fixture-build-1");
#else
    CHECK(!integrated->sources.gamePack);
#endif
    CHECK(FixtureQueryServiceCalls() == afterModuleLoad);
    sco::app::Stop(); CHECK(!sco::app::DiagnosticSnapshot());
    CHECK(!CopyPublisherMetadata(kMaxMonotonicMs) && !sco::ServiceExists(SCO_DIAGNOSTICS_PUBLISHER_NAME));
    fs::remove_all(root);
    std::puts("diagnostics publishers: PASS (default-off, offline gate, identity, fixed fields, quotas, immutable copies, unload, app boundary)");
}
