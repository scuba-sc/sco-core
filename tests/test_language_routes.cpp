// SPDX-License-Identifier: GPL-3.0-only
// Public wrapper fixtures only: not a loader or a game-provider implementation test.
#include "scosdk/game/actors.hpp"
#include "scosdk/game/entities.hpp"
#include "scosdk/game/world.hpp"
#include "scosdk/game/vehicles.hpp"
#include "scosdk/game/creative.hpp"
#include "scosdk/ui.hpp"
#include "scosdk/storage.hpp"
#include "scosdk/settings.hpp"
#include "scosdk/datacore.hpp"
#include "scosdk/ipc.hpp"
#include "scosdk/net.hpp"
#include <cstdio>
#include <cstring>

static int passed, failed, calls;
#define CHECK(...) do { if (__VA_ARGS__) ++passed; else { ++failed; std::printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
static sc_actors_v1 actors;
static sc_entities_v1 entities;
static sc_world_v1 world;
static sc_vehicles_v1 vehicles;
static sc_creative_v1 creative;
static sco_ui_v1 ui;
static sco_storage_v1 storage;
static sco_settings_v1 settings;
static sco_datacore_v1 datacore;
static sco_ipc_v1 ipc;
static sco_net_v1 net;
static sco_result lookupResult = SCO_OK;
static sco_result Lookup(const char* name, uint32_t, const void** out) {
    *out = nullptr;
    if (lookupResult != SCO_OK) return lookupResult;
    if (std::strcmp(name, SC_WORLD_NAME) == 0) {
        *out = &world;
        return SCO_OK;
    }
    if (std::strcmp(name, SC_VEHICLES_SERVICE_NAME) == 0) {
        *out = &vehicles;
        return SCO_OK;
    }
    if (std::strcmp(name, SC_CREATIVE_SERVICE_NAME) == 0) {
        *out = &creative;
        return SCO_OK;
    }
    if (std::strcmp(name, SCO_UI_NAME) == 0) {
        *out = &ui;
        return SCO_OK;
    }
    if (std::strcmp(name, SCO_STORAGE_NAME) == 0) { *out = &storage; return SCO_OK; }
    if (std::strcmp(name, SCO_SETTINGS_NAME) == 0) { *out = &settings; return SCO_OK; }
    if (std::strcmp(name, SCO_DATACORE_NAME) == 0) { *out = &datacore; return SCO_OK; }
    if (std::strcmp(name, SCO_IPC_NAME) == 0) { *out = &ipc; return SCO_OK; }
    if (std::strcmp(name, SCO_NET_NAME) == 0) { *out = &net; return SCO_OK; }
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
template <class F> struct Probe;
template <class R, class... A> struct Probe<R (*)(A...)> {
    static R Call(A...) { ++calls; return static_cast<R>(SCO_FAILED); }
};
template <class T, class F, class Call>
static void CheckField(T& table, F T::* member, Call invoke) {
    const T saved = table;
    const auto offset = reinterpret_cast<const char*>(&(table.*member)) - reinterpret_cast<const char*>(&table);
    table.*member = &Probe<F>::Call;
    for (uint32_t size = static_cast<uint32_t>(offset); size < offset + sizeof(F); ++size) {
        table.size = size; calls = 0; invoke(); CHECK(calls == 0);
    }
    table.size = static_cast<uint32_t>(offset + sizeof(F));
    calls = 0; invoke(); CHECK(calls == 1);
    table.size = sizeof(T) + 16;
    calls = 0; invoke(); CHECK(calls == 1);
    table.*member = nullptr;
    calls = 0; invoke(); CHECK(calls == 0);
    table = saved;
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
    api.query_service = Lookup;
    lookupResult = SCO_OK;
    world.size = sizeof(uint32_t);
    world.raycast = [](uint64_t, const double*, const double*, double, sc_world_hit*) -> sco_result {
        ++calls;
        return SCO_OK;
    };
    sco::sdk::game::World w;
    CHECK(w.Open(&api, self) == SCO_OK);
    const double rayFrom[3]{}, rayDir[3]{0, 0, -1};
    sc_world_hit hit{};
    calls = 0;
    CHECK(w.Raycast(0, rayFrom, rayDir, 1, hit) == SCO_UNAVAILABLE);
    CHECK(calls == 0);
    sco::sdk::Plugin carrier;
    CHECK(sco::sdk::LoadPlugin(carrier, &api, self) == SCO_OK);
    vehicles.size = sizeof(uint32_t);
    vehicles.player_ship = [](uint64_t* ship) -> sco_result {
        ++calls;
        *ship = 9;
        return SCO_OK;
    };
    sco::sdk::game::Vehicles v;
    CHECK(v.Open(carrier) == SCO_OK);
    uint64_t ship = 0;
    calls = 0;
    CHECK(v.PlayerShip(ship) == SCO_UNAVAILABLE && ship == 0);
    CHECK(calls == 0);
    creative.size = sizeof(uint32_t);
    creative.set_god_mode = [](sco_plugin*, int) -> sco_result {
        ++calls;
        return SCO_OK;
    };
    sco::sdk::game::Creative c;
    CHECK(c.Open(&api, self) == SCO_OK);
    calls = 0;
    CHECK(c.SetGodMode(true) == SCO_UNAVAILABLE);
    CHECK(calls == 0);
    sco::sdk::UnloadPlugin(carrier);
    ui.size = sizeof(uint32_t);
    ui.register_tab = [](sco_plugin*, const char*, const char*, int32_t, sco_ui_draw_fn, void*) -> sco_result {
        ++calls;
        return SCO_OK;
    };
    ui.unregister_tab = [](sco_plugin*, const char*) -> sco_result { return SCO_OK; };
    sco::sdk::Ui u;
    CHECK(u.Open(&api, self) == SCO_OK);
    calls = 0;
    CHECK(u.AddTab("guard.ui", "Guard", 0, [](void*, void*) {}, nullptr) == SCO_UNAVAILABLE);
    CHECK(calls == 0);
    CHECK(sco::sdk::LoadPlugin(carrier, &api, self) == SCO_OK);
    sco::sdk::Storage st; sco::sdk::Settings se; sco::sdk::DataCore dc;
    sco::sdk::Ipc ip; sco::sdk::Net nt;
    CHECK(st.Open(&api, self) == SCO_OK && se.Open(&api, self) == SCO_OK && dc.Open(&api, self) == SCO_OK);
    CHECK(ip.Open(&api, self) == SCO_OK && nt.Open(&api, self) == SCO_OK);
    storage.size = sizeof(storage); settings.size = sizeof(settings); datacore.size = sizeof(datacore);
    ipc.size = sizeof(ipc); net.size = sizeof(net); ui.size = sizeof(ui);
    storage.close = &Probe<decltype(storage.close)>::Call;
    storage.rollback = &Probe<decltype(storage.rollback)>::Call;
    datacore.discard = &Probe<decltype(datacore.discard)>::Call;
    ipc.close = &Probe<decltype(ipc.close)>::Call;
    net.unregister_channel = &Probe<decltype(net.unregister_channel)>::Call;
    int64_t number = 17; double decimal = 2; bool flag = true;
    std::string text = "unchanged"; std::vector<std::string> keys;
    CheckField(world, &sc_world_v1::raycast, [&] { (void)w.Raycast(9, rayFrom, rayDir, 1, hit); });
    CheckField(world, &sc_world_v1::camera, [&] { (void)w.Camera(pos, rot, zone); });
    CheckField(world, &sc_world_v1::last_error, [&] { (void)w.LastError(); });
    CheckField(vehicles, &sc_vehicles_v1::player_ship, [&] { (void)v.PlayerShip(ship); });
    CheckField(vehicles, &sc_vehicles_v1::seat_occupant, [&] { (void)v.SeatOccupant(9, 0, actor); });
    CheckField(vehicles, &sc_vehicles_v1::seat, [&] { (void)v.Seat(5, 9, 0); });
    CheckField(vehicles, &sc_vehicles_v1::eject, [&] { (void)v.Eject(5); });
    CheckField(vehicles, &sc_vehicles_v1::power_on, [&] { (void)v.PowerOn(9); });
    CheckField(vehicles, &sc_vehicles_v1::last_error, [&] { char buf[32]; (void)v.LastError(buf); });
    CheckField(creative, &sc_creative_v1::set_god_mode, [&] { (void)c.SetGodMode(true); });
    CheckField(creative, &sc_creative_v1::set_fly_mode, [&] { (void)c.SetFlyMode(true); });
    CheckField(creative, &sc_creative_v1::set_fly_speed, [&] { (void)c.SetFlySpeed(1); });
    CheckField(creative, &sc_creative_v1::set_infinite_ammo, [&] { (void)c.SetInfiniteAmmo(true); });
    CheckField(creative, &sc_creative_v1::set_infinite_ship_ammo, [&] { (void)c.SetInfiniteShipAmmo(true); });
    CheckField(creative, &sc_creative_v1::state, [&] { (void)c.State(count); });
    CheckField(creative, &sc_creative_v1::last_error, [&] { char buf[32]; (void)c.LastError(buf); });
    CheckField(storage, &sco_storage_v1::put, [&] { (void)st.Put("key", "value"); });
    CheckField(storage, &sco_storage_v1::get, [&] { (void)st.Get("key", number); });
    CheckField(storage, &sco_storage_v1::del, [&] { (void)st.Delete("key"); });
    CheckField(storage, &sco_storage_v1::next_key, [&] { (void)st.Keys("", keys); });
    CheckField(storage, &sco_storage_v1::exec, [&] { (void)st.Exec("SELECT 1"); });
    CheckField(storage, &sco_storage_v1::last_error, [&] { (void)st.LastError(); });
    CheckField(settings, &sco_settings_v1::get_bool, [&] { (void)se.GetBool("key", flag); });
    CheckField(settings, &sco_settings_v1::get_int, [&] { (void)se.GetInt("key", number); });
    CheckField(settings, &sco_settings_v1::get_float, [&] { (void)se.GetFloat("key", decimal); });
    CheckField(settings, &sco_settings_v1::get_string, [&] { (void)se.GetString("key", text); });
    CheckField(settings, &sco_settings_v1::last_error, [&] { (void)se.LastError(); });
    CheckField(net, &sco_net_v1::is_active, [&] { (void)nt.Active(); });
    CheckField(net, &sco_net_v1::self_peer, [&] { (void)nt.SelfPeer(); });
    CheckField(net, &sco_net_v1::get_peers, [&] { (void)nt.Peers(); });
    CheckField(net, &sco_net_v1::get_peer_name, [&] { (void)nt.PeerName(1); });
    CheckField(net, &sco_net_v1::send_channel, [&] { (void)nt.Send("key", number); });
    storage.query = &Probe<decltype(storage.query)>::Call;
    storage.close = nullptr; calls = 0;
    CHECK(st.Query("SELECT 1").Result() == SCO_UNAVAILABLE && calls == 0);
    datacore.begin = &Probe<decltype(datacore.begin)>::Call;
    datacore.discard = nullptr; calls = 0;
    CHECK(dc.Begin().Result() == SCO_UNAVAILABLE && calls == 0);
    ipc.create = &Probe<decltype(ipc.create)>::Call;
    ipc.close = nullptr; calls = 0;
    CHECK(ip.Create("key", 4096, 1, 1).Result() == SCO_UNAVAILABLE && calls == 0);
    net.register_channel = &Probe<decltype(net.register_channel)>::Call;
    net.unregister_channel = nullptr; calls = 0;
    CHECK(nt.Channel("key", 0, 1, [](uint64_t, std::span<const std::byte>) {}).Result() == SCO_UNAVAILABLE && calls == 0);
    ui.unregister_tab = nullptr; calls = 0;
    CHECK(u.AddTab("guard.ui", "Guard", 0, [](void*, void*) {}, nullptr) == SCO_UNAVAILABLE && calls == 0);
    sco::sdk::UnloadPlugin(carrier);
    std::printf("language routes: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
