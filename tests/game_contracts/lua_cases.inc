// SPDX-License-Identifier: GPL-3.0-only
// Game-only test cases; included by the corresponding test when the game pack is enabled.
// ---- sco.datacore (published by the host only when the product enables it) --------------------

static void TestDataCore() {
    namespace svc = sco::datacore::service;
    Write("dcnone", "assert(sco.datacore == nil, 'not published')\n");
    Loaded none = Load("dcnone");
    CHECK(none.p && none.p->state == State::Loaded);
    Unload(none);

    const fs::path data = g_root.parent_path() / "lua_datacore";
    fs::remove_all(data);
    svc::Options o;
    o.dataRoot = data;
    CHECK(svc::Start(o) == Result::Ok);
    Write("dcmod", R"(
local dc = assert(sco.datacore, "sco.datacore")
assert(dc.state() == "open")
local p = assert(dc.begin())
assert(p:set("ShipA", "speed", 2.5))
local part = assert(p:add_instance("Part", "PartX"))
assert(p:set_pointer("ShipB", "engine", part))
assert(p:set("ShipB", "label", "lua"))
assert(p:set("ShipA", "kind", { enum = "Small" }))
assert(p:set("ShipA", "maker", { ref = "BaseOne" }))
assert(p:append("ShipA", "parts", part))
assert(p:append("ShipA", "parts", nil))
local ok, err = p:set("ShipA", "speed..x", 1)
assert(not ok and err == "bad_arg", "path syntax")
assert(select(2, p:set("ShipA", "flag", {})) == "bad_arg", "a table that is no value")
assert(p:add_record("Ship", "ShipC", "ShipA"), "add_record")
assert(select(2, p:add_record("Ship", "ShipC", "ShipB")) == "bad_arg", "a name added twice")
assert(p:set("ShipC", "speed", 9.5))
assert(p:commit())
assert(select(2, p:commit()) == "bad_arg", "committed twice")
local r = p:report()
assert(#r == 11 and r[1].state == "queued" and r[1].op == 1 and r[11].op == 0, "report before the load")
local q = assert(dc.begin({ atomic = false }))
assert(q:discard())
assert(select(2, q:commit()) == "not_found", "discarded")
sco.register_command{ name = "dcmod.report", title = "Report", fn = function()
  local rr = p:report()
  return rr[#rr].state .. " " .. rr[1].state .. " " .. sco.datacore.state()
end }
)");
    Loaded l = Load("dcmod");
    CHECK(l.p && l.p->state == State::Loaded);
    if (l.p && l.p->state != State::Loaded) std::printf("  dcmod: %s\n", l.p->reason.c_str());

    const std::vector<uint8_t> file = dcb::ValuedFixture(36).Build();
    sco::datacore::Schema s;
    CHECK(s.Parse(file));
    P::ContentIndex index;
    const svc::LoadResult r = svc::Load(s, l.list, index, data);
    CHECK(r.result.status.ok() && r.result.packs.size() == 1 && r.result.packs[0].state == sco::datacore::PackState::Applied);
    CHECK(r.result.packs.size() == 1 && r.result.packs[0].applied == 10);
    const Reply rep = Invoke(g_caller, "dcmod.report");
    CHECK(rep.r == SCO_OK && rep.text == "applied applied loaded");
    Unload(l);
    svc::Stop();
}

// ---- sco.game.actors (read-only over game.actors; a stand-in table, published as the game pack does) --

static bool g_spawned = false;
static sco_result FakeLocalPlayer(uint64_t* actor, uint64_t* entity) {
    if (!g_spawned) return SCO_NOT_FOUND;
    if (actor) *actor = 11;
    if (entity) *entity = 0x8000000000000016ull;   // past INT64_MAX: comes back as a negative integer
    return SCO_OK;
}
static sco_result FakeHealth(uint64_t id, float* cur, float* max) {
    if (id != 6) return SCO_NOT_FOUND;
    *cur = 50.5f;
    *max = 0;
    return SCO_OK;
}
static sco_result FakeState(uint64_t, uint32_t*) { return SCO_UNAVAILABLE; }
static sco_result FakeSpawnNpc(sco_plugin*, const char*, uint64_t, const double*, uint64_t*) { return SCO_FAILED; }
static sco_result FakeDespawn(sco_plugin*, uint64_t) { return SCO_FAILED; }
static sco_result FakeLastError(sco_plugin* self, char* out, uint32_t* io) {
    static const char kWhy[] = "local_player: you're not spawned yet";
    if (!io || self) return SCO_BAD_ARG;
    const uint32_t cap = *io;
    *io = sizeof(kWhy);
    if (cap < sizeof(kWhy)) return SCO_TOO_MANY;
    std::memcpy(out, kWhy, sizeof(kWhy));
    return SCO_OK;
}

static void TestGameActors() {
    Write("ganone", "assert(sco.game == nil, 'not published')\n");
    Loaded none = Load("ganone");
    CHECK(none.p && none.p->state == State::Loaded);
    Unload(none);

    static sc_actors_v1 kFake = { sizeof(sc_actors_v1), 0, FakeLocalPlayer, FakeSpawnNpc, FakeDespawn, FakeLastError,
                                        FakeHealth, FakeState };
    CHECK(sco::host::ProvideGameService(SC_ACTORS_NAME, SC_ACTORS_VERSION_1_1, &kFake) == Result::Ok);
    Write("gaplay", R"(
local a = assert(sco.game and sco.game.actors, "sco.game.actors")
assert(a.spawn_npc == nil and a.despawn == nil and a.last_error == nil and a.state == nil, "read-only")
sco.register_command{ name = "gaplay.ids", title = "Ids", fn = function()
  local actor, entity, msg = sco.game.actors.local_player()
  if not actor then return entity .. ": " .. tostring(msg) end
  return actor .. " " .. entity
end }
sco.register_command{ name = "gaplay.hp", title = "Hp", fn = function()
  local cur, max, msg = sco.game.actors.health(6)
  if not cur then return max .. ": " .. tostring(msg) end
  local _, err, why = sco.game.actors.health(7)
  return string.format("%.1f %.1f / %s %s", cur, max, err, tostring(why))
end }
)");
    Loaded l = Load("gaplay");
    CHECK(l.p && l.p->state == State::Loaded);
    if (l.p && l.p->state != State::Loaded) std::printf("  gaplay: %s\n", l.p->reason.c_str());
    Reply r = Invoke(g_caller, "gaplay.ids");
    CHECK(r.r == SCO_OK && r.text == "not_found: local_player: you're not spawned yet");
    g_spawned = true;
    r = Invoke(g_caller, "gaplay.ids");
    CHECK(r.r == SCO_OK && r.text == "11 -9223372036854775786");
    if (r.text != "11 -9223372036854775786") std::printf("  gaplay.ids -> %s\n", r.text.c_str());
    r = Invoke(g_caller, "gaplay.hp");
    CHECK(r.r == SCO_OK && r.text == "50.5 0.0 / not_found local_player: you're not spawned yet");
    if (r.text != "50.5 0.0 / not_found local_player: you're not spawned yet") std::printf("  gaplay.hp -> %s\n", r.text.c_str());
    kFake.size = offsetof(sc_actors_v1, health) + 1;
    r = Invoke(g_caller, "gaplay.hp");
    CHECK(r.r == SCO_OK && r.text == "unavailable: local_player: you're not spawned yet");
    kFake.size = sizeof(kFake);
    kFake.health = nullptr;
    r = Invoke(g_caller, "gaplay.hp");
    CHECK(r.r == SCO_OK && r.text == "unavailable: local_player: you're not spawned yet");
    kFake.health = FakeHealth;
    CHECK(ReloadScript(l));   // reopens the read-only adapter with a new Lua state
    CHECK(Invoke(g_caller, "gaplay.ids").text == "11 -9223372036854775786");
    CHECK(Invoke(g_caller, "gaplay.hp").text == "50.5 0.0 / not_found local_player: you're not spawned yet");
    Unload(l);
    CHECK(sco::host::WithdrawGameService(SC_ACTORS_NAME) == Result::Ok);
    CHECK(ReloadScript(none));   // adapter registered, but no service: no leaked table from gaplay
    Unload(none);
}

// ---- sco.game.entities (read-only over game.entities; a stand-in table) --------------------------

static int FakeEntAlive(uint64_t id) { return id == 5; }
static sco_result FakeEntClassOf(uint64_t id, char* out, uint32_t* io) {
    static const char kName[] = "AEGS_Avenger_Titan";
    if (id != 5) return SCO_NOT_FOUND;
    const uint32_t cap = *io;
    *io = sizeof(kName);
    if (cap < sizeof(kName)) return SCO_TOO_MANY;
    std::memcpy(out, kName, sizeof(kName));
    return SCO_OK;
}
static sco_result FakeEntGet(uint64_t id, double* pos, double* rot, uint64_t* zone) {
    if (id != 5) return SCO_NOT_FOUND;
    pos[0] = 1; pos[1] = 2; pos[2] = 3;
    rot[0] = rot[1] = rot[2] = 0; rot[3] = 1;
    *zone = 0x8000000000000009ull;   // past INT64_MAX: comes back as a negative integer
    return SCO_OK;
}
static sco_result FakeEntSet(sco_plugin*, uint64_t, uint64_t, const double*, const double*) { return SCO_FAILED; }
static sco_result FakeEntSpawn(sco_plugin*, const char*, uint64_t, const double*, const double*, uint64_t*) { return SCO_FAILED; }
static sco_result FakeEntDespawn(sco_plugin*, uint64_t) { return SCO_FAILED; }
static sco_result FakeEntWatch(sco_plugin*, uint32_t, const char*, sc_entity_watch_fn, void*, uint64_t*) { return SCO_FAILED; }
static sco_result FakeEntUnwatch(sco_plugin*, uint64_t) { return SCO_FAILED; }
static sco_result FakeEntQuery(uint64_t, const double*, double, const char*, uint64_t*, uint32_t, uint32_t*, uint32_t*) { return SCO_FAILED; }
static sco_result FakeEntLastError(sco_plugin* self, char* out, uint32_t* io) {
    static const char kWhy[] = "class_of: 6 isn't streamed in";
    if (!io || self) return SCO_BAD_ARG;
    const uint32_t cap = *io;
    *io = sizeof(kWhy);
    if (cap < sizeof(kWhy)) return SCO_TOO_MANY;
    std::memcpy(out, kWhy, sizeof(kWhy));
    return SCO_OK;
}

// ---- sco.game.world and the game.* events (read-only; a stand-in table, published as the game pack does) --

static sco_result FakeRaycast(uint64_t zone, const double* from, const double* dir, double max, sc_world_hit* hit) {
    if (zone != 9) return SCO_NOT_FOUND;
    if (!hit || hit->size < sizeof(sc_world_hit) || max <= 0) return SCO_BAD_ARG;
    for (int i = 0; i < 3; ++i) hit->pos[i] = from[i] + dir[i] * 10.0;
    hit->distance = 10.0;
    return SCO_OK;
}
static sco_result FakeCamera(double* pos, double* rot, uint64_t* zone) {
    if (pos) { pos[0] = 1; pos[1] = 2; pos[2] = 3; }
    if (rot) { rot[0] = 0; rot[1] = 0; rot[2] = 0; rot[3] = 1; }
    if (zone) *zone = 9;
    return SCO_OK;
}
static sco_result FakeWorldLastError(sco_plugin*, char* out, uint32_t* io) {
    static const char kWhy[] = "raycast: no hit within 50.0 m";
    if (!io) return SCO_BAD_ARG;
    const uint32_t cap = *io;
    *io = sizeof(kWhy);
    if (cap < sizeof(kWhy)) return SCO_TOO_MANY;
    std::memcpy(out, kWhy, sizeof(kWhy));
    return SCO_OK;
}

// ---- sco.game.creative (read-only: state() only; a stand-in table) -------------------------------

static uint32_t g_creativeFlags = 0;
static sco_result FakeCrToggle(sco_plugin*, int) { return SCO_FAILED; }
static sco_result FakeCrSpeed(sco_plugin*, float) { return SCO_FAILED; }
static sco_result FakeCrState(sco_plugin* self, uint32_t* flags) {
    if (!self || !flags) return SCO_BAD_ARG;
    *flags = g_creativeFlags;
    return g_creativeFlags == 0x80000000u ? SCO_UNAVAILABLE : SCO_OK;
}
static sco_result FakeCrLastError(sco_plugin*, char* out, uint32_t* io) {
    static const char kWhy[] = "no game.creative capability is ready";
    const uint32_t cap = *io;
    *io = sizeof(kWhy);
    if (cap < sizeof(kWhy)) return SCO_TOO_MANY;
    std::memcpy(out, kWhy, sizeof(kWhy));
    return SCO_OK;
}

static void TestGameCreative() {
    static sc_creative_v1 kFake = { sizeof(sc_creative_v1), 0, FakeCrToggle, FakeCrToggle, FakeCrSpeed, FakeCrToggle,
                                          FakeCrToggle, FakeCrState, FakeCrLastError };
    CHECK(sco::host::ProvideGameService(SC_CREATIVE_SERVICE_NAME, SC_CREATIVE_SERVICE_VERSION, &kFake) == Result::Ok);
    Write("gacreate", R"(
local c = assert(sco.game and sco.game.creative, "sco.game.creative")
assert(c.set_god_mode == nil and c.set_fly_mode == nil and c.set_fly_speed == nil and c.set_infinite_ammo == nil
       and c.set_infinite_ship_ammo == nil and c.last_error == nil, "read-only")
sco.register_command{ name = "gacreate.state", title = "State", fn = function()
  local s, err, msg = c.state()
  if not s then return err .. ": " .. tostring(msg) end
  return string.format("%s %s %s %s", tostring(s.god_mode), tostring(s.fly), tostring(s.infinite_ammo), tostring(s.infinite_ship_ammo))
end }
)");
    Loaded l = Load("gacreate");
    CHECK(l.p && l.p->state == State::Loaded);
    if (l.p && l.p->state != State::Loaded) std::printf("  gacreate: %s\n", l.p->reason.c_str());
    g_creativeFlags = SC_CREATIVE_GOD_MODE | SC_CREATIVE_INFINITE_SHIP_AMMO;
    Reply r = Invoke(g_caller, "gacreate.state");
    CHECK(r.r == SCO_OK && r.text == "true false false true");
    if (r.text != "true false false true") std::printf("  gacreate.state -> %s\n", r.text.c_str());
    g_creativeFlags = 0x80000000u;
    r = Invoke(g_caller, "gacreate.state");
    CHECK(r.r == SCO_OK && r.text == "unavailable: no game.creative capability is ready");
    if (r.text != "unavailable: no game.creative capability is ready") std::printf("  gacreate.state -> %s\n", r.text.c_str());
    kFake.size = offsetof(sc_creative_v1, state) + sizeof(kFake.state) - 1;
    r = Invoke(g_caller, "gacreate.state");
    CHECK(r.r == SCO_OK && r.text == "unavailable: nil");
    kFake.size = sizeof(kFake);
    kFake.state = nullptr;
    kFake.last_error = nullptr;
    r = Invoke(g_caller, "gacreate.state");
    CHECK(r.r == SCO_OK && r.text == "unavailable: nil");
    kFake.state = FakeCrState;
    kFake.last_error = FakeCrLastError;
    Unload(l);
    CHECK(sco::host::WithdrawGameService(SC_CREATIVE_SERVICE_NAME) == Result::Ok);
}

static void TestGameEntities() {
    static sc_entities_v1 kFake = { sizeof(sc_entities_v1), 0, FakeEntAlive, FakeEntClassOf, FakeEntGet, FakeEntSet,
                                          FakeEntSpawn, FakeEntDespawn, FakeEntWatch, FakeEntUnwatch, FakeEntQuery, FakeEntLastError,
                                          nullptr /* keep: Lua is read-only */ };
    CHECK(sco::host::ProvideGameService(SC_ENTITIES_NAME, SC_ENTITIES_VERSION_1_0, &kFake) == Result::Ok);
    Write("gaent", R"(
local e = assert(sco.game and sco.game.entities, "sco.game.entities")
assert(e.spawn == nil and e.despawn == nil and e.set_transform == nil and e.watch == nil and e.unwatch == nil
       and e.query_radius == nil and e.last_error == nil, "read-only")
sco.register_command{ name = "gaent.partial", fn = function()
  local x, err = e.get_transform(5)
  return tostring(x) .. ":" .. tostring(err)
end }
sco.register_command{ name = "gaent.ask", title = "Ask", fn = function()
  local t = { tostring(e.alive(5)), tostring(e.alive(6)) }
  t[#t + 1] = e.class_of(5)
  local x, y, z, qx, qy, qz, qw, zone = e.get_transform(5)
  t[#t + 1] = string.format("%g,%g,%g,%g,%d", x, y, z, qw, zone)
  local n, err, msg = e.class_of(6)
  t[#t + 1] = tostring(n) .. ":" .. err .. ":" .. tostring(msg)
  return table.concat(t, " ")
end }
)");
    Loaded l = Load("gaent");
    CHECK(l.p && l.p->state == State::Loaded);
    if (l.p && l.p->state != State::Loaded) std::printf("  gaent: %s\n", l.p->reason.c_str());
    const Reply r = Invoke(g_caller, "gaent.ask");
    const char* want = "true false AEGS_Avenger_Titan 1,2,3,1,-9223372036854775799 nil:not_found:class_of: 6 isn't streamed in";
    CHECK(r.r == SCO_OK && r.text == want);
    if (r.text != want) std::printf("  gaent.ask -> %s\n", r.text.c_str());
    kFake.size = offsetof(sc_entities_v1, get_transform) + 1;
    const Reply partial = Invoke(g_caller, "gaent.partial");
    CHECK(partial.r == SCO_OK && partial.text == "nil:unavailable");
    kFake.size = sizeof(kFake);
    kFake.get_transform = nullptr;
    kFake.last_error = nullptr;
    const Reply nullMethod = Invoke(g_caller, "gaent.partial");
    CHECK(nullMethod.r == SCO_OK && nullMethod.text == "nil:unavailable");
    kFake.get_transform = FakeEntGet;
    kFake.last_error = FakeEntLastError;
    Unload(l);
    CHECK(sco::host::WithdrawGameService(SC_ENTITIES_NAME) == Result::Ok);
}

static void TestGameWorld() {
    Write("gwnone", "assert(sco.game == nil, 'not published')\n");
    Loaded none = Load("gwnone");
    CHECK(none.p && none.p->state == State::Loaded);
    Unload(none);

    static sc_world_v1 kFake = { sizeof(sc_world_v1), 0, FakeRaycast, FakeCamera, FakeWorldLastError, nullptr };
    CHECK(sco::host::ProvideGameService(SC_WORLD_NAME, SC_WORLD_VERSION_1_0, &kFake) == Result::Ok);
    Write("gwplay", R"(
local w = assert(sco.game and sco.game.world, "sco.game.world")
assert(w.last_error == nil, "queries only")
sco.register_command{ name = "gwplay.partial", fn = function()
  local h, e = w.raycast(9, 0, 0, 1, 0, 0, -1, 50)
  return tostring(h) .. ":" .. tostring(e)
end }
local seen = {}
sco.subscribe("game.actors.died", function(ev, d) seen.actor = d and (d.entity_id .. ":" .. d.killer_id) or "nil" end)
sco.register_command{ name = "gwplay.actor", title = "Actor death", fn = function() return tostring(seen.actor) end }
sco.subscribe("game.player.died", function(ev, d) seen.died = d and (d.entity_id .. ":" .. d.killer_id) or "nil" end)
sco.subscribe("game.zone.changed", function(ev, d) seen.zone = d and (d.old_zone_id .. ">" .. d.new_zone_id) or "nil" end)
sco.subscribe("game.player.spawned", function(ev, d) seen.spawn = d and (d.entity_id .. "@" .. d.zone_id) or "nil" end)
sco.register_command{ name = "gwplay.cam", title = "Cam", fn = function()
  local c, e, m = sco.game.world.camera()
  if not c then return e .. ": " .. tostring(m) end
  return string.format("%.1f %.1f %.1f %.1f %d", c.x, c.y, c.z, c.qw, c.zone)
end }
sco.register_command{ name = "gwplay.ray", title = "Ray", fn = function()
  local h = sco.game.world.raycast(9, 0, 0, 1, 0, 0, -1, 50)
  local none, e, m = sco.game.world.raycast(8, 0, 0, 1, 0, 0, -1, 50)
  return string.format("%.1f %.1f %.1f %.1f / %s %s", h.x, h.y, h.z, h.distance, tostring(e), tostring(m))
end }
sco.register_command{ name = "gwplay.seen", title = "Seen", fn = function()
  return tostring(seen.died) .. "," .. tostring(seen.zone) .. "," .. tostring(seen.spawn)
end }
)");
    Loaded l = Load("gwplay");
    CHECK(l.p && l.p->state == State::Loaded);
    if (l.p && l.p->state != State::Loaded) std::printf("  gwplay: %s\n", l.p->reason.c_str());
    Reply r = Invoke(g_caller, "gwplay.cam");
    CHECK(r.r == SCO_OK && r.text == "1.0 2.0 3.0 1.0 9");
    if (r.text != "1.0 2.0 3.0 1.0 9") std::printf("  gwplay.cam -> %s\n", r.text.c_str());
    r = Invoke(g_caller, "gwplay.ray");
    CHECK(r.r == SCO_OK && r.text == "0.0 0.0 -9.0 10.0 / not_found raycast: no hit within 50.0 m");
    if (r.text != "0.0 0.0 -9.0 10.0 / not_found raycast: no hit within 50.0 m") std::printf("  gwplay.ray -> %s\n", r.text.c_str());
    kFake.size = offsetof(sc_world_v1, raycast) + sizeof(kFake.raycast) - 1;
    r = Invoke(g_caller, "gwplay.partial");
    CHECK(r.r == SCO_OK && r.text == "nil:unavailable");
    kFake.size = sizeof(kFake);
    kFake.raycast = nullptr;
    kFake.last_error = nullptr;
    r = Invoke(g_caller, "gwplay.partial");
    CHECK(r.r == SCO_OK && r.text == "nil:unavailable");
    kFake.raycast = FakeRaycast;
    kFake.last_error = FakeWorldLastError;

    // The game.* events reach sco.subscribe handlers as tables; a payload shorter than the struct is nil.
    r = Invoke(g_caller, "gwplay.seen");
    CHECK(r.r == SCO_OK && r.text == "nil,nil,nil");
    const sc_game_player_died died = { sizeof(died), 0, 0x8000000000000016ull, 0 };   // past INT64_MAX: reads as negative
    const sc_game_zone_changed zone = { sizeof(zone), 0, 5, 6 };
    const sc_game_player_spawned spawned = { sizeof(spawned), 0, 3, 4 };
    size_t called = 0;
    CHECK(sco::Dispatch(SC_GAME_EVENT_PLAYER_DIED, &died, &called) == Result::Ok && called == 1);
    CHECK(sco::Dispatch(SC_GAME_EVENT_ZONE_CHANGED, &zone, &called) == Result::Ok && called == 1);
    CHECK(sco::Dispatch(SC_GAME_EVENT_PLAYER_SPAWNED, &spawned, &called) == Result::Ok && called == 1);
    r = Invoke(g_caller, "gwplay.seen");
    CHECK(r.r == SCO_OK && r.text == "-9223372036854775786:0,5>6,3@4");
    if (r.text != "-9223372036854775786:0,5>6,3@4") std::printf("  gwplay.seen -> %s\n", r.text.c_str());
    const sc_game_actor_died actor = { sizeof(actor), 0, 0x8000000000000016ull, 0 };
    CHECK(sco::Dispatch(SC_GAME_EVENT_ACTOR_DIED, &actor, &called) == Result::Ok && called == 1);
    r = Invoke(g_caller, "gwplay.actor");
    CHECK(r.r == SCO_OK && r.text == "-9223372036854775786:0");
    const sc_game_actor_died shortActor = { 8, 0, 1, 0 };
    CHECK(sco::Dispatch(SC_GAME_EVENT_ACTOR_DIED, &shortActor, &called) == Result::Ok && called == 1);
    r = Invoke(g_caller, "gwplay.actor");
    CHECK(r.r == SCO_OK && r.text == "nil");
    CHECK(sco::Dispatch(SC_GAME_EVENT_ACTOR_DIED, nullptr, &called) == Result::Ok && called == 1);
    r = Invoke(g_caller, "gwplay.actor");
    CHECK(r.r == SCO_OK && r.text == "nil");
    const sc_game_player_died shortDied = { 8, 0, 1, 1 };   // size 8: shorter than sc_game_player_died
    CHECK(sco::Dispatch(SC_GAME_EVENT_PLAYER_DIED, &shortDied, &called) == Result::Ok && called == 1);
    r = Invoke(g_caller, "gwplay.seen");
    CHECK(r.r == SCO_OK && r.text == "nil,5>6,3@4");
    Unload(l);
    CHECK(sco::host::WithdrawGameService(SC_WORLD_NAME) == Result::Ok);
}

// The core runtime alone: with no adapter registered, sco.datacore and sco.game never appear and
// a game.* event's data is nil, even when the game services are published.
static void TestNoAdapter() {
    static const sc_actors_v1 kNone = { sizeof(sc_actors_v1), 0, FakeLocalPlayer, FakeSpawnNpc, FakeDespawn, FakeLastError,
                                        FakeHealth, FakeState };
    CHECK(sco::host::ProvideGameService(SC_ACTORS_NAME, SC_ACTORS_VERSION_1_1, &kNone) == Result::Ok);
    Write("noadapter", R"(
        assert(sco.game == nil, "sco.game without the adapter")
        assert(sco.datacore == nil, "sco.datacore without the adapter")
        sco.subscribe("game.player.died", function(name, data) sco.log("info", "died " .. tostring(data)) end)
    )");
    Loaded l = Load("noadapter");
    CHECK(l.p && l.p->state == State::Loaded);
    const sc_game_player_died died = { sizeof(died), 0, 7, 0 };
    sco::Dispatch(SC_GAME_EVENT_PLAYER_DIED, &died);
    CHECK(Logged("[noadapter] died nil"));
    CHECK(ReloadScript(l));
    g_log.clear();
    sco::Dispatch(SC_GAME_EVENT_PLAYER_DIED, &died);
    CHECK(Logged("[noadapter] died nil"));
    Unload(l);
    CHECK(sco::host::WithdrawGameService(SC_ACTORS_NAME) == Result::Ok);
}
