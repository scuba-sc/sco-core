// SPDX-License-Identifier: GPL-3.0-only
// Real neutral host/runtime/loader, independent C shared-library consumers.
// ONLY the engine response tables below are synthetic. No game implementation is linked.
#include "sco/host.h"
#include "sco/plugins.h"
#include "sco/caps.h"
#include "sco/log.h"
#include "sc_actors.h"
#include "sc_entities.h"
#include "sc_game_events.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace P = sco::plugins;
static int pass = 0, fail = 0;
#define CHECK(c) do { if (c) ++pass; else { ++fail; std::printf("FAIL %d: %s\n", __LINE__, #c); } } while (0)
static std::vector<std::string> logs;
static void Sink(const char* line) { logs.emplace_back(line); }
static bool Logged(const char* text) {
    for (const auto& line : logs) if (line.find(text) != std::string::npos) return true;
    return false;
}

// A bounded ownership/streaming model, NOT the real engine's semantics or fault safety.
static sco_plugin* npcOwner = nullptr;
static int phase = 0; // 0 absent, 1 pending, 2 streamed, 3 removing
static unsigned reads = 0, removals = 0;
static sco_result healthResult = SCO_OK, playerResult = SCO_OK;
static sco_result Player(uint64_t* actor, uint64_t* entity) {
    ++reads;
    if (actor) *actor = 0;
    if (entity) *entity = 0;
    if (!sco::OnGameThread()) return SCO_WRONG_THREAD;
    if (playerResult != SCO_OK) return playerResult;
    if (actor) *actor = 100;
    if (entity) *entity = 101;
    return SCO_OK;
}
static sco_result Health(uint64_t, float* current, float* maximum) {
    ++reads;
    if (!sco::OnGameThread()) return SCO_WRONG_THREAD;
    if (healthResult != SCO_OK) return healthResult;
    if (current) *current = 75;
    if (maximum) *maximum = 0;
    return SCO_OK;
}
static sco_result Spawn(sco_plugin* self, const char* name, uint64_t zone, const double pos[3], uint64_t* out) {
    if (out) *out = 0;
    if (!sco::OnGameThread()) return SCO_WRONG_THREAD;
    if (!self || !name || !pos || !out || !zone || !std::isfinite(pos[0]) ||
        !std::isfinite(pos[1]) || !std::isfinite(pos[2])) return SCO_BAD_ARG;
    if (phase) return SCO_TOO_MANY;
    npcOwner = self; phase = 1; *out = 700;
    return SCO_OK;
}
static sco_result Remove(sco_plugin* self, uint64_t id) {
    if (!sco::OnGameThread()) return SCO_WRONG_THREAD;
    if (!self || self != npcOwner || id != 700) return SCO_NOT_FOUND;
    npcOwner = nullptr; phase = 3; ++removals;
    return SCO_OK;
}
static int Alive(uint64_t id) { return sco::OnGameThread() && (id == 101 || (id == 700 && phase == 2)); }
static sco_result Transform(uint64_t id, double pos[3], double rot[4], uint64_t* zone) {
    ++reads;
    if (!sco::OnGameThread()) return SCO_WRONG_THREAD;
    if (!Alive(id)) return SCO_NOT_FOUND;
    if (pos) { pos[0] = 1; pos[1] = 2; pos[2] = 3; }
    if (rot) { rot[0] = rot[1] = rot[2] = 0; rot[3] = 1; }
    if (zone) *zone = 9;
    return SCO_OK;
}
static sc_actors_v1 Actors() {
    sc_actors_v1 a{};
    a.size = sizeof(a); a.local_player = Player; a.health = Health;
    a.spawn_npc = Spawn; a.despawn = Remove;
    return a;
}
static sc_entities_v1 Entities() {
    sc_entities_v1 e{};
    e.size = sizeof(e); e.alive = Alive; e.get_transform = Transform;
    return e;
}
static void PublishActors(const void* table, uint32_t version = SC_ACTORS_VERSION_1_1) {
    sco::host::WithdrawGameService(SC_ACTORS_NAME);
    CHECK(sco::host::ProvideGameService(SC_ACTORS_NAME, version, table) == sco::Result::Ok);
}
static void PublishEntities(const void* table) {
    sco::host::WithdrawGameService(SC_ENTITIES_NAME);
    CHECK(sco::host::ProvideGameService(SC_ENTITIES_NAME, SC_ENTITIES_VERSION_1_0, table) == sco::Result::Ok);
}
struct Reply { sco_result result = SCO_FAILED; unsigned calls = 0; std::string text; };
static void Done(sco_result r, const char* text, void* ctx) {
    auto& out = *static_cast<Reply*>(ctx);
    out.result = r; ++out.calls; out.text = text ? text : "";
}
static Reply Invoke(const sco_api* api, sco_plugin* self, const char* name) {
    Reply out;
    const auto r = api->invoke(self, name, nullptr, 0, Done, &out);
    CHECK(out.calls == 1 && r == out.result);
    return out;
}
static unsigned Deaths(const sco_api* api, sco_plugin* self, const char* name) {
    unsigned out = 99; uint32_t size = sizeof(out);
    CHECK(api->invoke_raw(self, name, nullptr, 0, &out, &size) == SCO_OK && size == sizeof(out));
    return out;
}

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    sco::SetLogSink(Sink); sco::SetGameThread();
    const sco_api* api = sco::host::BuildApi({"synthetic conformance host"});
    CHECK(api && api->size == sizeof(sco_api));
    P::Options opts; opts.enabled = true;
    auto plugins = P::Discover(argv[1], opts);
    CHECK(plugins.size() == 2);
    if (plugins.size() != 2 || !api) return 1;
    P::ContainCallouts(&plugins);
    for (auto& p : plugins) {
        CHECK(p.state == P::State::Ready);
        CHECK(P::LoadNative(p, api, sco::host::NewPlugin(p.folder.c_str()), opts));
        CHECK(p.state == P::State::Loaded && p.module);
    }
    auto& consumer = plugins[0]; auto& peer = plugins[1];
    CHECK(consumer.folder == "conformance" && peer.folder == "conformance_peer");
    sco::caps::Set("game.actors.health", true);
    sco::caps::Set("game.actors.spawn_npc", true);
    sco::caps::Set("game.entities.transform", true);
    auto a = Actors(); auto e = Entities();
    CHECK(Invoke(api, consumer.self, "conformance.violate").result == SCO_BAD_ARG);

    // Missing/major/minor: exercise the REAL service registry's result and cleared output.
    CHECK(Invoke(api, consumer.self, "conformance.inspect").result == SCO_NOT_FOUND);
    PublishActors(&a, 0x00020000);
    CHECK(Invoke(api, consumer.self, "conformance.inspect").result == SCO_UNAVAILABLE);
    PublishActors(&a, SC_ACTORS_VERSION_1_0);
    CHECK(Invoke(api, consumer.self, "conformance.inspect").result == SCO_UNAVAILABLE);
    const void* missing = reinterpret_cast<const void*>(1);
    CHECK(api->query_service(SC_ACTORS_NAME, SC_ACTORS_VERSION_1_1, &missing) == SCO_UNAVAILABLE && !missing);
    PublishActors(&a);
    CHECK(Invoke(api, consumer.self, "conformance.inspect").result == SCO_NOT_FOUND);
    PublishEntities(&e);
    auto good = Invoke(api, consumer.self, "conformance.inspect");
    CHECK(good.result == SCO_OK && good.text == "entity=101 health=75 max=unknown zone=9 x=1");

    // Real allocations end INSIDE each function pointer. ASan catches reading a partial tail.
    for (uint32_t size : {uint32_t(offsetof(sc_actors_v1, health) + 1),
                          uint32_t(offsetof(sc_actors_v1, health) + sizeof(a.health) - 1)}) {
        std::unique_ptr<void, decltype(&std::free)> shortTable(std::malloc(size), std::free);
        if (!shortTable) return 1;
        std::memcpy(shortTable.get(), &a, size);
        std::memcpy(shortTable.get(), &size, sizeof(size));
        PublishActors(shortTable.get());
        const auto before = reads;
        CHECK(Invoke(api, consumer.self, "conformance.inspect").result == SCO_UNAVAILABLE && reads == before);
        sco::host::WithdrawGameService(SC_ACTORS_NAME); // before the allocation dies
    }
    a.size = offsetof(sc_actors_v1, health) + sizeof(a.health);
    PublishActors(&a);
    CHECK(Invoke(api, consumer.self, "conformance.inspect").result == SCO_OK); // exact extent accepted
    a.health = nullptr;
    CHECK(Invoke(api, consumer.self, "conformance.inspect").result == SCO_UNAVAILABLE);
    a = Actors();
    e.size = offsetof(sc_entities_v1, get_transform) + sizeof(e.get_transform) - 1;
    CHECK(Invoke(api, consumer.self, "conformance.inspect").result == SCO_UNAVAILABLE);
    e = Entities(); e.get_transform = nullptr;
    CHECK(Invoke(api, consumer.self, "conformance.inspect").result == SCO_UNAVAILABLE);
    e = Entities();
    sco::caps::Set("game.actors.health", false);
    CHECK(Invoke(api, consumer.self, "conformance.inspect").result == SCO_UNAVAILABLE);
    sco::caps::Set("game.actors.health", true);
    playerResult = SCO_NOT_FOUND;
    CHECK(Invoke(api, consumer.self, "conformance.inspect").result == SCO_NOT_FOUND);
    playerResult = SCO_OK; healthResult = SCO_FAILED;
    CHECK(Invoke(api, consumer.self, "conformance.inspect").result == SCO_FAILED);
    healthResult = SCO_OK;

    // Kernel's raw-call wrong-thread failure; model's game table thread rule is separate.
    sco_result rawThread = SCO_OK, tableThread = SCO_OK;
    std::thread([&] {
        unsigned out = 0; uint32_t size = sizeof(out);
        rawThread = api->invoke_raw(consumer.self, "conformance.stats", nullptr, 0, &out, &size);
        float current = 0; tableThread = a.health(101, &current, nullptr);
    }).join();
    CHECK(rawThread == SCO_WRONG_THREAD && tableThread == SCO_WRONG_THREAD);

    // Native owner vs native peer; delayed spawn and remove are not instant engine completion.
    CHECK(Invoke(api, consumer.self, "conformance.spawn").result == SCO_OK && phase == 1);
    CHECK(Invoke(api, consumer.self, "conformance.spawn").result == SCO_TOO_MANY && npcOwner == consumer.self);
    CHECK(Invoke(api, consumer.self, "conformance.observe").text == "pending-or-gone");
    CHECK(Invoke(api, peer.self, "conformance_peer.remove").result == SCO_NOT_FOUND && npcOwner == consumer.self);
    phase = 2; // synthetic engine streams in on a later tick
    CHECK(sco::GameThreadTick(1) == sco::Result::Ok);
    CHECK(Invoke(api, consumer.self, "conformance.observe").text == "streamed");
    CHECK(Invoke(api, consumer.self, "conformance.remove").result == SCO_OK && phase == 3);
    CHECK(Invoke(api, consumer.self, "conformance.remove").result == SCO_NOT_FOUND);
    phase = 0; // synthetic engine completes the removal later
    CHECK(Invoke(api, consumer.self, "conformance.spawn").result == SCO_OK);
    CHECK(Invoke(api, consumer.self, "conformance.remove").result == SCO_OK && phase == 3); // before streaming
    phase = 0;
    CHECK(Invoke(api, consumer.self, "conformance.spawn").result == SCO_OK);

    // Callback reads a size prefix, copies complete payloads, accepts an unknown (zero) id.
    uint32_t shortPayload = sizeof(uint32_t);
    CHECK(sco::Dispatch(SC_GAME_EVENT_ACTOR_DIED, nullptr) == sco::Result::Ok);
    CHECK(sco::Dispatch(SC_GAME_EVENT_ACTOR_DIED, &shortPayload) == sco::Result::Ok);
    CHECK(Deaths(api, consumer.self, "conformance.stats") == 0);
    sc_game_actor_died died{sizeof(died), 0, 700, 0};
    CHECK(sco::Dispatch(SC_GAME_EVENT_ACTOR_DIED, &died) == sco::Result::Ok);
    died.entity_id = 0;
    CHECK(sco::Dispatch(SC_GAME_EVENT_ACTOR_DIED, &died) == sco::Result::Ok);
    CHECK(Deaths(api, consumer.self, "conformance.stats") == 2);

    // Public ABI worker invoke/task accepted, then cancelled by real loader owner cleanup.
    CHECK(Invoke(api, consumer.self, "conformance.queue").result == SCO_OK);
    CHECK(sco::GameThreadTick(2) == sco::Result::Ok && Logged("conformance task ran"));
    logs.clear();
    CHECK(Invoke(api, consumer.self, "conformance.queue").result == SCO_OK);
    Reply queued;
    sco_result admitted = SCO_FAILED;
    std::thread([&] { admitted = api->invoke(consumer.self, "conformance.inspect", nullptr, 0, Done, &queued); }).join();
    CHECK(admitted == SCO_OK && queued.calls == 0 && sco::QueuedTasks() == 2);
    auto* stale = consumer.self;
    const auto beforeUnload = removals;
    P::UnloadNative(consumer);
    CHECK(consumer.state == P::State::Unloaded && !consumer.module);
    CHECK(removals == beforeUnload + 1 && !npcOwner && phase == 3);
    CHECK(sco::QueuedTasks() == 0);
    CHECK(sco::GameThreadTick(3) == sco::Result::Ok && queued.calls == 0 && !Logged("conformance task ran"));
    CHECK(api->run_on_game_thread(stale, [](void*) {}, nullptr) == SCO_BAD_ARG);
    CHECK(api->invoke(peer.self, "conformance.inspect", nullptr, 0, nullptr, nullptr) == SCO_NOT_FOUND);
    unsigned out = 0; uint32_t outSize = sizeof(out);
    CHECK(api->invoke_raw(peer.self, "conformance.stats", nullptr, 0, &out, &outSize) == SCO_NOT_FOUND);
    CHECK(sco::Dispatch(SC_GAME_EVENT_ACTOR_DIED, &died) == sco::Result::Ok);
    CHECK(Deaths(api, peer.self, "conformance_peer.stats") == 3); // unloaded subscriber never runs
    P::UnloadNative(consumer); // repeated unload is inert
    CHECK(removals == beforeUnload + 1);
    P::UnloadAll(plugins); P::ContainCallouts(nullptr);
    CHECK(sco::SubscriptionCount() == 0);
    sco::host::WithdrawGameServices();

    // Reuse the existing native provider fixture and the centralized R1 loader gates,
    // but give it the REAL host table (not test_plugins' stand-in API).
    auto discovered = P::Discover(argv[2], opts);
    std::vector<P::Plugin> providers;
    for (auto& p : discovered) if (p.folder == "svc_provider") providers.push_back(std::move(p));
    CHECK(providers.size() == 1);
    if (providers.size() != 1) return 1;
    P::ContainCallouts(&providers);
    auto& provider = providers[0];
    CHECK(P::LoadNative(provider, api, sco::host::NewPlugin("svc_provider"), opts));
    struct Service { uint32_t size; int (*ok)(); int (*boom)(); };
    const void* table = nullptr;
    CHECK(api->query_service("svc_provider.svc", 0x00010000, &table) == SCO_OK && table);
    if (!table) return 1;
    const auto* service = static_cast<const Service*>(table);
    CHECK(service->size == sizeof(Service) && service->ok() == 7);
    std::string why;
    CHECK(!P::CanUnload(provider, why) && why.find("restart the game") != std::string::npos);
    CHECK(!P::BeginUnload({&provider}, why)); // the exact reservation used by app::Reload
    void* mapped = provider.module;
    P::UnloadNative(provider);
    CHECK(provider.state == P::State::Loaded && provider.module == mapped && service->ok() == 7);
    int pending = 0;
    CHECK(api->run_on_game_thread(provider.self, [](void* ctx) { ++*static_cast<int*>(ctx); }, &pending) == SCO_OK);
    // Deterministic attributed-fault injection, not a POSIX signal or Windows SEH claim.
    // Existing test_plugins retains the real fault/attribution tests; do not duplicate its guard.
    P::MarkCrashed(provider, "injected conformance fault", 0xC0000005);
    CHECK(provider.state == P::State::Faulted && provider.module == mapped);
    CHECK(provider.reason.find("no recovery guaranteed") != std::string::npos);
    table = reinterpret_cast<const void*>(1);
    CHECK(api->query_service("svc_provider.svc", 0x00010000, &table) == SCO_NOT_FOUND && !table);
    CHECK(api->run_on_game_thread(provider.self, [](void*) {}, nullptr) == SCO_BAD_ARG);
    CHECK(sco::GameThreadTick(4) == sco::Result::Ok && pending == 0);
    CHECK(!P::BeginUnload({&provider}, why) && why.find("restart") != std::string::npos);
    P::UnloadAll(providers); P::UnloadAll(providers);
    CHECK(provider.state == P::State::Faulted && provider.module == mapped);
    P::ContainCallouts(nullptr);
    // Never call cached service pointers after the fault: retention is not recovery.
    std::printf("sdk conformance workflows: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
