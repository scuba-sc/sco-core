// SPDX-License-Identifier: GPL-3.0-only
// Public wrapper fixtures only: not a loader or a game-provider implementation test.
#include "scosdk/game/actors.hpp"
#include "scosdk/game/entities.hpp"
#include <cstdio>
#include <cstring>

static int passed, failed, calls;
#define CHECK(...) do { if (__VA_ARGS__) ++passed; else { ++failed; std::printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
static sc_actors_v1 actors;
static sc_entities_v1 entities;
static sco_result lookupResult = SCO_OK;
static sco_result Lookup(const char* name, uint32_t, const void** out) {
    *out = nullptr;
    if (lookupResult != SCO_OK) return lookupResult;
    *out = std::strcmp(name, SC_ACTORS_NAME) == 0 ? static_cast<void*>(&actors) : static_cast<void*>(&entities);
    return SCO_OK;
}
static sco_result Local(uint64_t* a, uint64_t* e) { ++calls; *a = 5; *e = 6; return SCO_OK; }
static sco_result Health(uint64_t, float* cur, float* max) { ++calls; *cur = 12; *max = 0; return SCO_OK; }
static sco_result Spawn(sco_plugin*, const char*, uint64_t, const double*, uint64_t* id) { ++calls; *id = 77; return SCO_OK; }
static sco_result Remove(sco_plugin*, uint64_t id) { ++calls; return id == 77 ? SCO_OK : SCO_NOT_FOUND; }
static int Alive(uint64_t id) { ++calls; return id == 6 || id == 77; }
static sco_result Transform(uint64_t, double* p, double* q, uint64_t* z) {
    ++calls; p[0] = 1; p[1] = 2; p[2] = 3; q[0] = q[1] = q[2] = 0; q[3] = 1; *z = 9; return SCO_OK;
}
int main() {
    sco_api api{}; api.size = sizeof(api); api.query_service = Lookup;
    auto* self = reinterpret_cast<sco_plugin*>(&api); // opaque identity; never dereferenced
    actors.size = sizeof(actors); actors.local_player = Local; actors.health = Health;
    actors.spawn_npc = Spawn; actors.despawn = Remove;
    entities.size = sizeof(entities); entities.alive = Alive; entities.get_transform = Transform;
    sco::sdk::game::Actors a; sco::sdk::game::Entities e;
    CHECK(a.Open(&api, self) == SCO_OK && e.Open(&api, self) == SCO_OK);
    uint64_t actor = 0, entity = 0, zone = 0, npc = 0;
    float hp = 0, maximum = 1; double pos[3]{}, rot[4]{};
    CHECK(a.LocalPlayer(actor, entity) == SCO_OK && actor == 5 && entity == 6);
    CHECK(a.Health(entity, hp, maximum) == SCO_OK && hp == 12 && maximum == 0); // maximum unknown, not dead
    CHECK(e.Alive(entity) && e.GetTransform(entity, pos, rot, zone) == SCO_OK && zone == 9 && pos[2] == 3 && rot[3] == 1);
    CHECK(a.SpawnNpc("SyntheticNpc", zone, pos, npc) == SCO_OK && npc == 77);
    CHECK(e.Alive(npc) && a.Despawn(npc) == SCO_OK && a.Despawn(999) == SCO_NOT_FOUND);
    // Sweep through partial pointer extents with callable poison beyond the advertised end.
    // A read must not happen until its entire field fits. Full backing allocation makes RED safe.
    for (uint32_t size = 8; size < 16; ++size) {
        actors.size = size; entities.size = size; calls = 0;
        CHECK(a.LocalPlayer(actor, entity) == SCO_UNAVAILABLE && actor == 0 && entity == 0);
        CHECK(!e.Alive(6) && calls == 0);
    }
    for (uint32_t size = 40; size < 48; ++size) {
        actors.size = size; calls = 0;
        CHECK(a.Health(6, hp, maximum) == SCO_UNAVAILABLE && hp == 0 && maximum == 0 && calls == 0);
    }
    for (uint32_t size = 24; size < 32; ++size) {
        entities.size = size; calls = 0;
        CHECK(e.GetTransform(6, pos, rot, zone) == SCO_UNAVAILABLE && zone == 0 && pos[0] == 0 && rot[3] == 0 && calls == 0);
    }
    actors.size = sizeof(actors); entities.size = sizeof(entities);
    // Failed reopen must invalidate, never silently retain the former provider.
    lookupResult = SCO_NOT_FOUND;
    CHECK(a.Open(&api, self) == SCO_NOT_FOUND && !a && e.Open(&api, self) == SCO_NOT_FOUND && !e);
    lookupResult = SCO_OK;
    api.size = offsetof(sco_api, query_service) + 1;
    CHECK(a.Open(&api, self) == SCO_UNAVAILABLE && e.Open(&api, self) == SCO_UNAVAILABLE);
    if (failed) { std::printf("language routes: %d passed, %d failed (extent failures)\n", passed, failed); return 1; }
    // Null callable fields are unsupported, not calls through address zero.
    api.size = sizeof(api); actors = {}; entities = {};
    actors.size = sizeof(actors); entities.size = sizeof(entities);
    CHECK(a.Open(&api, self) == SCO_OK && e.Open(&api, self) == SCO_OK);
    CHECK(a.LocalPlayer(actor, entity) == SCO_UNAVAILABLE && a.Health(6, hp, maximum) == SCO_UNAVAILABLE);
    CHECK(a.SpawnNpc("SyntheticNpc", 9, pos, npc) == SCO_UNAVAILABLE && npc == 0 && a.Despawn(77) == SCO_UNAVAILABLE);
    CHECK(!e.Alive(6) && e.GetTransform(6, pos, rot, zone) == SCO_UNAVAILABLE);
    CHECK(a.LastReadError().empty() && e.LastReadError().empty());
    sc_actor_state state = SC_ACTOR_DEAD;
    CHECK(a.State(6, state) == SCO_UNAVAILABLE && state == SC_ACTOR_ALIVE);
    std::string cls = "stale";
    CHECK(e.ClassOf(6, cls) == SCO_UNAVAILABLE && cls.empty());
    CHECK(e.SetTransform(6, 9, pos, rot) == SCO_UNAVAILABLE && e.Spawn("SyntheticEntity", 9, pos, rot, npc) == SCO_UNAVAILABLE && npc == 0);
    CHECK(e.Despawn(77) == SCO_UNAVAILABLE && e.Keep(77) == SCO_UNAVAILABLE);
    uint64_t watch = 1, ids[1]{}; uint32_t count = 1, more = 1;
    CHECK(e.Watch(1, "SyntheticEntity", nullptr, nullptr, watch) == SCO_UNAVAILABLE && watch == 0 && e.Unwatch(1) == SCO_UNAVAILABLE);
    CHECK(e.QueryRadius(9, pos, 1, nullptr, ids, 1, count, more) == SCO_UNAVAILABLE && count == 0 && more == 0);
    api.query_service = nullptr;
    CHECK(a.Open(&api, self) == SCO_UNAVAILABLE && !a && e.Open(&api, self) == SCO_UNAVAILABLE && !e);
    std::printf("language routes: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
