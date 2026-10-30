// Synthetic-only host snapshots; no game, network or external diagnostics channel.
#include "sco/diagnostics.h"
#include "sco/app.h"
#include "sco/caps.h"
#include "sco/host.h"
#include "sco/signatures.h"
#include "sco/signature_registry.h"
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <thread>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); std::abort(); } } while (0)
namespace sco::diagnostics { uint64_t FixtureQueryServiceCalls(); }
using namespace sco::diagnostics;
void RunServiceDiagnosticsTests();
namespace {
unsigned callbacks = 0, resolutions = 0;
const sco_plugin_info* Query() { ++callbacks; return nullptr; }
sco_result Load(const sco_api*, sco_plugin*) { ++callbacks; return SCO_OK; }
void Unload() { ++callbacks; }
void Task(void*) { ++callbacks; }
void Event(const char*, const void*, void*) { ++callbacks; }
sco::SigResult Resolve(const sco::Image&, const sco::SigResolveContext&) { ++resolutions; return sco::SigFail("fixture failure /private/fixture secret"); }
const sco::SigDef rows[]{
    {"fixture.ok", "11", 0, 0, nullptr, {}},
    {"fixture.missing", "33", 0, 0, nullptr, {}},
    {"fixture.ambiguous", "22", 0, 0, nullptr, {}},
    {"fixture.failed", nullptr, 0, 0, Resolve, {}},
    {"fixture.blocked", "11", 0, 0, nullptr, {"fixture.missing"}},
};
void Finish(SnapshotStore& store, uint64_t now) {
    size_t calls = 0;
    while (!store.Advance(now)) CHECK(++calls < 2048);
}
const PluginMetadata& Plugin(const Snapshot& snapshot, const char* id) {
    for (const auto& p : snapshot.plugins) if (p.pluginId == id) return p;
    std::abort();
}
void CopiesAndStates() {
    SnapshotStore store;
    const uint64_t queryCalls = FixtureQueryServiceCalls();
    auto registry = std::make_shared<sco::SignatureRegistry>();
    const sco::SignatureTable signatureTable{rows, std::size(rows)};
    CHECK(registry->RegisterTables(&signatureTable, 1).status == sco::SigRegistryStatus::Ok);
    CHECK(registry->Freeze() == sco::SigRegistryStatus::Ok);
    CHECK(!registry->Snapshot());
    CHECK(!sco::InstallHostSignatureRegistry(registry, nullptr));
    std::vector<sco::plugins::Plugin> plugins(10);
    static const char* states[]{"off", "disabled", "refused", "ready", "loaded", "crashed", "unloaded", "faulted"};
    for (size_t i = 0; i < 8; ++i) {
        auto& p = plugins[i];
        p.manifestOk = true; p.manifest.id = "fixture_" + std::to_string(i);
        p.manifest.version = "1.0.0"; p.manifest.kind = static_cast<sco::plugins::Kind>(i % 4);
        p.state = static_cast<sco::plugins::State>(i);
        p.reason = "private /fixture/root person@example.invalid 0x123 secret";
        p.dir = "/fixture/private"; p.folder = "private_folder";
        p.manifest.author = "private_author"; p.manifest.name = "private_display";
        p.reloadCopy = "/fixture/private/.reload-1-fixture";
        p.self = sco::host::NewPlugin(p.manifest.id.c_str()); CHECK(p.self);
        p.exports = {Query, Load, Unload};
    }
    plugins[8].manifestOk = false; plugins[8].folder = "private_bad_folder";
    plugins[8].manifest.id = "raw/injection";
    plugins[9].manifestOk = true; plugins[9].manifest.id = "unknown_8"; // alias collision
    plugins[9].manifest.version = "private@example.invalid";
    CHECK(sco::caps::Set("fixture.cap", false, "/private/fixture") == sco::Result::Ok);
    CHECK(sco::caps::Set("fixture.enabled", true) == sco::Result::Ok);
    CHECK(sco::Post(Task, nullptr, plugins[4].self) == sco::Result::Ok);
    CHECK(sco::Subscribe(plugins[4].self, "fixture.event", Event, nullptr) == sco::Result::Ok);
    const size_t queued = sco::QueuedTasks(), subscriptions = sco::SubscriptionCount();
    sco::caps::Entry before[50]{}; const size_t capCount = sco::caps::List(before, 50);
    HostMetadata host; host.hostVersion = "fixture-host-1"; host.hostBuild = "/private/fixture";
    host.sourceSha = "not-a-sha"; host.readiness = "ready";
    LifecycleMetadata game; game.lifecycle = "ready"; game.lastTickMs = 90;
    game.gameServicesRequested = true; game.gameServicesBuilt = false; game.gameServicesPublished = false;
    CHECK(store.Begin(plugins, host, game, 100)); CHECK(!store.Latest());
    CHECK(!store.Advance(100)); CHECK(!store.Latest());
    Finish(store, 120);
    const auto first = store.Latest(); CHECK(first && first->plugins.size() == 10);
    CHECK(first->observedMs == 100); // earliest source time, not falsely refreshed at commit
    CHECK(first->host.hostVersion == "fixture-host-1" && !first->host.hostBuild && !first->host.sourceSha);
    CHECK(first->game.lifecycle == "ready" && first->game.offlineAdmission == "unknown");
    CHECK(first->game.lastTickMs == 90 && first->game.gameServicesRequested == true);
    CHECK(first->game.gameServicesBuilt == false && first->game.gameServicesPublished == false);
    CHECK(!first->sources.gamePack && !first->sources.diagnosticsPlugin);
    for (size_t i = 0; i < 8; ++i) {
        const auto& out = Plugin(*first, ("fixture_" + std::to_string(i)).c_str());
        CHECK(out.state == states[i] && out.version == "1.0.0" && out.manifestValid);
        CHECK(!out.loadGeneration); // no invented ordinal masquerading as actual load generation
        CHECK(out.restartRequired == (i == 7));
        CHECK(plugins[i].state == static_cast<sco::plugins::State>(i));
        CHECK(plugins[i].reason == "private /fixture/root person@example.invalid 0x123 secret");
        CHECK(!sco::ServiceTableHandedOut(plugins[i].self));
    }
    CHECK(!Plugin(*first, "unknown_1032").manifestValid && Plugin(*first, "unknown_1032").kind == "unknown");
    CHECK(!Plugin(*first, "unknown_8").version && first->pluginRedactions > 0);
    CHECK(first->host.restartRequired && first->game.restartRequired);
    CHECK(first->resolutions.empty()); // frozen definitions are not published until successful resolution
    CHECK(!first->resolutionsAvailable);
    CHECK(store.ReadPage(ListKind::Resolutions, "", "", 25, 120).error == "unknown_metadata");
    auto emptyRegistry = std::make_shared<sco::SignatureRegistry>();
    CHECK(emptyRegistry->Freeze() == sco::SigRegistryStatus::Ok);
    CHECK(emptyRegistry->Resolve(sco::Image{}) == sco::SigRegistryStatus::Ok);
    CHECK(sco::InstallHostSignatureRegistry(emptyRegistry, nullptr));
    SnapshotStore emptyStore;
    const std::vector<sco::plugins::Plugin> noPlugins;
    CHECK(emptyStore.Begin(noPlugins, {}, {}, 200)); Finish(emptyStore, 200);
    CHECK(emptyStore.Latest()->resolutionsAvailable && emptyStore.Latest()->resolutions.empty());
    CHECK(emptyStore.ReadPage(ListKind::Resolutions, "", "", 25, 200).status == "ok");
    CHECK(sco::ClearHostSignatureRegistry(emptyRegistry, nullptr));
    CHECK(callbacks == 0 && resolutions == 0);
    CHECK(sco::QueuedTasks() == queued && sco::SubscriptionCount() == subscriptions);
    sco::caps::Entry after[50]{}; CHECK(sco::caps::List(after, 50) == capCount);
    for (size_t i = 0; i < capCount; ++i) CHECK(std::string(before[i].name) == after[i].name && before[i].ready == after[i].ready && std::string(before[i].reason) == after[i].reason);
    std::thread wrong([&] { CHECK(!store.Begin(plugins, {}, {}, 1100)); CHECK(!store.Advance(1100)); store.Reset(); CHECK(store.Latest() == first); }); wrong.join();
    CHECK(!store.Begin(plugins, {}, {}, 1099)); // no more than one refresh/sec
    uint8_t bytes[]{0x11, 0x22, 0x22, 0x00}; sco::Image image{}; image.text = {bytes, sizeof(bytes)};
    CHECK(registry->Resolve(image) == sco::SigRegistryStatus::Ok);
    CHECK(sco::InstallHostSignatureRegistry(registry, nullptr));
    auto beforeCapture = registry->Snapshot();
    CHECK(beforeCapture && beforeCapture->Lookup("fixture.ok")->state == sco::SigState::Ok);
    OptionalSources sources; sources.gamePack = GamePackMetadata{"fixture-pack-1", "fixture-build-1"};
    sources.diagnosticsPlugin = DiagnosticsPluginMetadata{true};
    CHECK(store.Begin(plugins, {}, {}, 1100, sources));
    CHECK(registry->Resolve(image) == sco::SigRegistryStatus::HostSealed);
    CHECK(registry->Snapshot() == beforeCapture);
    // Synthetic owner replacement with no live game services. Production owners must
    // quiesce services and readers before clearing an image-backed registry.
    auto nextRegistry = std::make_shared<sco::SignatureRegistry>();
    CHECK(nextRegistry->RegisterTables(&signatureTable, 1).status == sco::SigRegistryStatus::Ok);
    CHECK(nextRegistry->Freeze() == sco::SigRegistryStatus::Ok);
    bytes[0] = 0;
    CHECK(nextRegistry->Resolve(image) == sco::SigRegistryStatus::Ok);
    CHECK(nextRegistry->Snapshot()->Lookup("fixture.ok")->state == sco::SigState::Missing);
    CHECK(sco::ClearHostSignatureRegistry(registry, nullptr));
    CHECK(sco::InstallHostSignatureRegistry(nextRegistry, nullptr));
    const auto runs = resolutions;
    Finish(store, 1100);
    const auto second = store.Latest(); CHECK(second->host.gamePackVersion == "fixture-pack-1");
    CHECK(second->sources.diagnosticsPlugin && second->sources.diagnosticsPlugin->available);
    const char* expected[]{"ambiguous", "blocked", "failed", "missing", "ok"};
    for (size_t i = 0; i < 5; ++i) {
        std::printf("resolution %s: %s (expected %s)\n", second->resolutions[i].rowId.c_str(), second->resolutions[i].state.c_str(), expected[i]);
        CHECK(second->resolutions[i].state == expected[i]);
        CHECK(second->resolutions[i].gameBuild == "fixture-build-1");
        CHECK(!second->resolutions[i].resolutionGeneration); // registry has no generation fact yet
    }
    CHECK(resolutions == runs && callbacks == 0);
    CHECK(first->resolutions.empty());
    CHECK(sco::ClearHostSignatureRegistry(nextRegistry, nullptr));
    for (auto& p : plugins) if (p.self) CHECK(sco::Release(p.self) == sco::Result::Ok);
    CHECK(callbacks == 0 && FixtureQueryServiceCalls() == queryCalls);
}
void LifecycleStates() {
    SnapshotStore store;
    const std::vector<sco::plugins::Plugin> plugins;
    uint64_t now = 100;
    for (const char* state : {"starting", "ready", "stopping", "unknown"}) {
        for (const char* admission : {"authorized", "denied", "unknown"}) {
            HostMetadata host; host.readiness = state;
            LifecycleMetadata game; game.lifecycle = state; game.offlineAdmission = admission;
            CHECK(store.Begin(plugins, host, game, now)); Finish(store, now);
            CHECK(store.Latest()->host.readiness == state && store.Latest()->game.lifecycle == state);
            CHECK(store.Latest()->game.offlineAdmission == admission);
            now += 1000;
        }
    }
    for (unsigned i = 0; i < 51; ++i)
        CHECK(sco::caps::Set(("fixture.cap_" + std::to_string(i)).c_str(), i % 2 != 0) == sco::Result::Ok);
    CHECK(store.Begin(plugins, {}, {}, now)); Finish(store, now);
    CHECK(store.Latest()->game.capabilities.size() == 50 && store.Latest()->game.capabilitiesTruncated);
}
void GenerationPages() {
    SnapshotStore store;
    std::vector<sco::plugins::Plugin> plugins(3);
    for (size_t i = 0; i < plugins.size(); ++i) {
        plugins[i].manifestOk = true; plugins[i].manifest.id = "fixture_" + std::to_string(2 - i);
        plugins[i].state = sco::plugins::State::Loaded;
    }
    CHECK(store.ReadPage(ListKind::Plugins, "", "", 25, 0).error == "unknown_metadata");
    CHECK(store.Begin(plugins, {}, {}, 100)); Finish(store, 100);
    const auto page = store.ReadPage(ListKind::Plugins, "", "", 1, 100);
    CHECK(page.status == "ok" && page.nextCursor && page.indices.size() == 1);
    CHECK(page.snapshot->plugins[page.indices[0]].pluginId == "fixture_0");
    const auto cursor = *page.nextCursor;
    CHECK(store.ReadPage(ListKind::Services, "", cursor, 1, 100).error == "invalid_request");
    CHECK(store.ReadPage(ListKind::Plugins, "loaded", cursor, 1, 100).error == "invalid_request");
    CHECK(store.ReadPage(ListKind::Plugins, "", "c_forged", 1, 100).error == "snapshot_changed");
    CHECK(store.ReadPage(ListKind::Plugins, "", "", 0, 100).error == "invalid_request");
    CHECK(store.ReadPage(ListKind::Services, "absent.service", "", 25, 100).error == "not_found");
    CHECK(store.ReadPage(ListKind::Plugins, "", "", 50, 1100).status == "ok");
    CHECK(store.ReadPage(ListKind::Plugins, "", "", 50, 1101).status == "stale");
    CHECK(store.ReadPage(ListKind::Plugins, "", "", 50, 99).error == "invalid_request");
    CHECK(store.ReadPage(ListKind::Plugins, "", cursor, 1, 60100).error == "snapshot_changed");
    CHECK(store.Begin(plugins, {}, {}, 1100)); Finish(store, 1100);
    const auto old = store.ReadPage(ListKind::Plugins, "", cursor, 50, 1100);
    CHECK(old.snapshot == page.snapshot && old.indices.size() == 2 && !old.nextCursor);
    CHECK(store.Begin(plugins, {}, {}, 2100)); Finish(store, 2100);
    CHECK(store.ReadPage(ListKind::Plugins, "", cursor, 1, 2100).error == "snapshot_changed");
    store.Reset(); CHECK(!store.Latest());
    CHECK(store.ReadPage(ListKind::Plugins, "", cursor, 1, 2100).error == "snapshot_changed");
    CHECK(store.Begin(plugins, {}, {}, 2200)); Finish(store, 2200);
    for (unsigned i = 0; i < 64; ++i) CHECK(store.ReadPage(ListKind::Plugins, "", "", 1, 2200).nextCursor);
    CHECK(store.ReadPage(ListKind::Plugins, "", "", 1, 2200).error == "snapshot_busy");
    CHECK(store.ReadPage(ListKind::Plugins, "", "", 1, 62200).nextCursor);
    store.Reset(); plugins.resize(1025);
    CHECK(store.Begin(plugins, {}, {}, 63200)); Finish(store, 63200);
    CHECK(store.Latest()->plugins.size() == 1024 && store.Latest()->pluginsTruncated);
}
void AppBoundaries() {
    sco::app::Platform platform;
    platform.hostVersion = "fixture-host-1";
    CHECK(sco::app::Start(platform));
    for (unsigned i = 0; i < 32 && !sco::app::DiagnosticSnapshot(); ++i) sco::app::Tick(i);
    const auto observed = sco::app::DiagnosticSnapshot();
    CHECK(observed && observed->game.lifecycle == "ready");
    CHECK(observed->host.hostVersion == "fixture-host-1" && observed->game.offlineAdmission == "unknown");
    CHECK(observed->game.gameServicesRequested == false && observed->game.gameServicesPublished == false);
    CHECK(observed->plugins.empty());
    sco::app::Stop(); CHECK(!sco::app::DiagnosticSnapshot());
    CHECK(observed->host.hostVersion == "fixture-host-1"); // no mutable host views survive
    CHECK(sco::app::Start(platform));
    for (unsigned i = 0; i < 32 && !sco::app::DiagnosticSnapshot(); ++i) sco::app::Tick(i);
    CHECK(sco::app::DiagnosticSnapshot()->generation > observed->generation);
    sco::app::Stop(); CHECK(!sco::app::DiagnosticSnapshot());
}
void KeyedCaptureAndGaps() {
    Capture capture; static char first, second, unknown;
    CHECK(!capture.AssignKey(&first, "fixture_a"));
    CHECK(capture.Start(100)); CHECK(!capture.Start(100));
    CHECK(capture.AssignKey(&first, "fixture_a")); CHECK(capture.AssignKey(&second, "fixture_b"));
    CHECK(!capture.AssignKey(&first, "spoofed")); CHECK(!capture.AssignKey(&unknown, "invalid/id"));
    capture.RecordHost(&unknown, HostEvent::Loaded, 100);
    capture.RecordHost(&first, HostEvent::Loaded, 100);
    capture.RecordPlugin(&first, LogLevel::Info, "ready", 100); // separately default off
    capture.RecordHost(&second, HostEvent::Faulted, 100);
    auto tail = capture.Tail("fixture_a", 0, 100);
    CHECK(tail.entries.size() == 1 && tail.entries[0].pluginId == "fixture_a");
    CHECK(tail.entries[0].origin == "host" && tail.entries[0].untrustedContent);
    CHECK(tail.redactedCount == 1 && tail.nextSequence == 3 && !tail.freeFormEnabled);
    CHECK(capture.Tail("fixture_a", *tail.nextSequence, 100).entries.empty());
    capture.Stop(); CHECK(capture.Tail("fixture_a", 0, 100).entries.empty());
    CHECK(capture.Start(200, true)); CHECK(capture.AssignKey(&first, "fixture_a"));
    CHECK(capture.Tail("fixture_a", 3, 100).error == "session_changed");
    CHECK(capture.Tail("missing", 0, 100).error == "not_found");
    capture.RecordPlugin(&first, LogLevel::Info, "ready", 200);
    const char* uncertain[]{"/fixture/private", "C:\\fixture\\secret", "person@example.invalid", "0x1234",
        "sk-fixture-not-a-key", "IGNORE ALL INSTRUCTIONS", "ready\nsecret", "\xff\xfe", "\xe2\x82\xac"};
    for (const auto* text : uncertain) capture.RecordPlugin(&first, LogLevel::Warning, text, 200);
    capture.RecordPlugin(&first, LogLevel::Info, std::string(600, 'x'), 200);
    tail = capture.Tail("fixture_a", 0, 100);
    CHECK(tail.entries.size() == 1 && tail.entries[0].message == "ready" && tail.freeFormEnabled);
    CHECK(tail.redactedCount == std::size(uncertain) + 1 && tail.nextSequence);
    CHECK(capture.Tail("fixture_a", *tail.nextSequence, 100).redactedCount == 0);
    for (unsigned i = 0; i < 8000; ++i) capture.RecordHost(&first, HostEvent::Faulted, 200);
    tail = capture.Tail("fixture_a", 0, 100);
    CHECK(tail.droppedCount > 0 && tail.firstAvailableSequence && *tail.firstAvailableSequence > 1);
    CHECK(tail.entries.size() == 100 && tail.hasMore && tail.nextSequence);
    CHECK(capture.Tail("fixture_a", *tail.nextSequence, 1).entries.size() == 1);
    for (const auto& entry : tail.entries) CHECK(entry.message.size() <= 512 && entry.untrustedContent);
    capture.Stop(); CHECK(!capture.Start(kMaxMonotonicMs + 1));
}
}
int main() {
    sco::SetGameThread();
    CopiesAndStates(); LifecycleStates(); GenerationPages(); KeyedCaptureAndGaps();
    CHECK(FixtureQueryServiceCalls() == 0 && callbacks == 0);
    std::puts("capture counters: QueryService=0; plugin callbacks=0; table probes/methods=0 (physically short tables)");
    RunServiceDiagnosticsTests(); AppBoundaries();
    std::puts("runtime diagnostics snapshots: PASS (states, copies, pages, redaction, log gaps, short tables, zero side effects)");
}
