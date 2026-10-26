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
    Write("ganone", "assert(sco.game.inventory and sco.game.actors == nil, 'actors not published')\n");
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

static uint32_t g_freeCamCalls = 0;
static sco_result g_freeCamResult = SCO_OK;
static uint32_t g_freeCamValue = 0;
static sco_result FakeFreeCam(uint32_t* out) {
    ++g_freeCamCalls;
    if (!out) return SCO_BAD_ARG;
    *out = g_freeCamResult == SCO_OK ? g_freeCamValue : 0u;
    return g_freeCamResult;
}

// ---- sco.game.gamerules (read-only snapshots and copied definition pages) ------------------------

static sco_result g_gamerulesResult = SCO_OK;
static uint32_t g_gamerulesValid = 1;
static uint32_t g_gamerulesReadCalls = 0, g_gamerulesListCalls = 0, g_gamerulesListBad = 0;
static bool g_gamerulesNamesChanged = false;
static sco_result g_gamerulesListResult = SCO_OK;
static sco_result FakeGrRead(sco_plugin* self, sc_gamerules_state* out) {
    ++g_gamerulesReadCalls;
    if (!self || !out || out->size < sizeof(*out)) return SCO_BAD_ARG;
    if (g_gamerulesResult != SCO_OK) return g_gamerulesResult;
    out->valid = g_gamerulesValid;
    out->generation = 7;
    out->enforce_group_qt = 1;
    out->quantum_travel_allowed = 0;
    out->quantum_boost_allowed = 1;
    out->travel_target_threshold_angle = 45.0f;
    out->boost_group_distance_threshold = 2500.0f;
    return SCO_OK;
}

static sco_result FakeGrList(sco_plugin* self, uint32_t offset, sc_gamerules_param_info* out,
                             uint32_t stride, uint32_t capacity, uint32_t* count, uint32_t* total) {
    ++g_gamerulesListCalls;
    if (!self || offset > 3 || capacity > 64 || !count || !total || count == total ||
        (capacity ? (!out || stride != sizeof(*out)) : out != nullptr)) return SCO_BAD_ARG;
    const uint32_t n = capacity < 3 - offset ? capacity : 3 - offset;
    for (uint32_t i = 0; i < n; ++i) {
        if (out[i].size != sizeof(out[i])) return SCO_BAD_ARG;
        out[i].type = offset + i == 2 ? SC_GAMERULES_PARAM_FLOAT : SC_GAMERULES_PARAM_INT;
        std::snprintf(out[i].name, sizeof(out[i].name), "%s_%u", g_gamerulesNamesChanged ? "changed" : "synthetic", offset + i);
        std::snprintf(out[i].group, sizeof(out[i].group), "%s", offset + i == 2 ? "" : "test");
    }
    *count = g_gamerulesListBad == 1 ? capacity + 1 : n;
    *total = g_gamerulesListBad == 4 ? 0 : 3;
    if (n && g_gamerulesListBad == 2) std::memset(out[0].name, 'x', sizeof(out[0].name));
    if (n && g_gamerulesListBad == 3) out[0].size = 0;
    if (n && g_gamerulesListBad == 5) std::memset(out[0].group, 'x', sizeof(out[0].group));
    return g_gamerulesListResult;
}

static void TestGameRules() {
    static sc_gamerules_v1 kFake = { sizeof(sc_gamerules_v1), 0, FakeGrRead, FakeGrList };
    CHECK(sco::host::ProvideGameService(SC_GAMERULES_SERVICE_NAME, SC_GAMERULES_SERVICE_VERSION, &kFake) == Result::Ok);
    Write("gagr", R"(
local g = assert(sco.game and sco.game.gamerules, "sco.game.gamerules")
local n = 0
for _ in pairs(g) do n = n + 1 end
assert(n == 2 and g.read and g.list_params, "read-only snapshot and static metadata functions")
sco.register_command{ name = "gagr.read", title = "Read", fn = function()
  local s, err = g.read()
  if not s then return err end
  return string.format("%s %d %s %s %s %.1f %.1f %.1f", tostring(s.valid), s.generation, tostring(s.enforce_group_qt),
    tostring(s.quantum_travel_allowed), tostring(s.quantum_boost_allowed), s.travel_target_threshold_angle,
    s.target_lock_angular_ai, s.boost_group_distance_threshold)
end }
local saved
sco.register_command{ name = "gagr.list", title = "List", fn = function()
  local p, err = g.list_params(0, 2)
  if not p then return err end
  assert(p.offset == 0 and p.count == 2 and p.total == 3 and not p.complete and #p.params == 2)
  assert(p.params[1].type == 1 and p.params[2].group == "test")
  return p.params[1].name
end }
sco.register_command{ name = "gagr.saved", title = "Saved", fn = function()
  if not saved then saved = assert(g.list_params(0, 1)) end
  return saved.params[1].name
end }
sco.register_command{ name = "gagr.pages", title = "Pages", fn = function()
  local p = assert(g.list_params(nil, 0))
  assert(p.count == 0 and p.total == 3 and #p.params == 0 and not p.complete)
  p = assert(g.list_params())
  assert(p.count == 3 and p.complete and #p.params == 3)
  p = assert(g.list_params(2, 64))
  assert(p.offset == 2 and p.count == 1 and p.total == 3 and p.complete)
  assert(p.params[1].type == 2 and p.params[1].group == "")
  p = assert(g.list_params(3, 1))
  assert(p.count == 0 and #p.params == 0 and p.complete)
  local bad, err = g.list_params(4, 1)
  assert(bad == nil and err == "bad_arg")
  return "pages"
end }
sco.register_command{ name = "gagr.args", title = "Args", fn = function()
  for _, args in ipairs({{-1,1},{4294967296,1},{0,-1},{0,65},{"0",1},{0,1.5},{0,1,2}}) do
    local p, err = g.list_params(table.unpack(args))
    assert(p == nil and err == "bad_arg")
  end
  return "args"
end }
)");
    Loaded l = Load("gagr");
    CHECK(l.p && l.p->state == State::Loaded);
    if (l.p && l.p->state != State::Loaded) std::printf("  gagr: %s\n", l.p->reason.c_str());
    Reply r = Invoke(g_caller, "gagr.read");
    CHECK(r.r == SCO_OK && r.text == "true 7 true false true 45.0 0.0 2500.0");
    if (r.text != "true 7 true false true 45.0 0.0 2500.0") std::printf("  gagr.read -> %s\n", r.text.c_str());
    g_gamerulesValid = 0;
    r = Invoke(g_caller, "gagr.read");
    CHECK(r.r == SCO_OK && r.text.rfind("false 7", 0) == 0);
    g_gamerulesValid = 1;
    g_gamerulesResult = SCO_UNAVAILABLE;
    r = Invoke(g_caller, "gagr.read");
    CHECK(r.r == SCO_OK && r.text == "unavailable");
    g_gamerulesResult = SCO_OK;
    kFake.size = offsetof(sc_gamerules_v1, read) + sizeof(kFake.read) - 1;   // a partial pointer is not the member
    r = Invoke(g_caller, "gagr.read");
    CHECK(r.r == SCO_OK && r.text == "unavailable");
    kFake.size = sizeof(kFake);
    kFake.read = nullptr;
    r = Invoke(g_caller, "gagr.read");
    CHECK(r.r == SCO_OK && r.text == "unavailable");
    kFake.read = FakeGrRead;
    r = Invoke(g_caller, "gagr.list");
    CHECK(r.r == SCO_OK && r.text == "unavailable" && g_gamerulesListCalls == 0);
    CHECK(sco::host::WithdrawGameService(SC_GAMERULES_SERVICE_NAME) == Result::Ok);
    CHECK(sco::host::ProvideGameService(SC_GAMERULES_SERVICE_NAME, SC_GAMERULES_VERSION_1_1, &kFake) == Result::Ok);
    g_gamerulesResult = SCO_UNAVAILABLE;
    const uint32_t readsBefore = g_gamerulesReadCalls;
    r = Invoke(g_caller, "gagr.list");
    CHECK(r.r == SCO_OK && r.text == "synthetic_0");
    r = Invoke(g_caller, "gagr.pages");
    CHECK(r.r == SCO_OK && r.text == "pages");
    const uint32_t listsBefore = g_gamerulesListCalls;
    r = Invoke(g_caller, "gagr.args");
    CHECK(r.r == SCO_OK && r.text == "args" && g_gamerulesListCalls == listsBefore);
    r = Invoke(g_caller, "gagr.saved");
    CHECK(r.r == SCO_OK && r.text == "synthetic_0");
    g_gamerulesNamesChanged = true;
    r = Invoke(g_caller, "gagr.list");
    CHECK(r.r == SCO_OK && r.text == "changed_0");
    r = Invoke(g_caller, "gagr.saved");
    CHECK(r.r == SCO_OK && r.text == "synthetic_0");
    g_gamerulesNamesChanged = false;
    for (uint32_t bad = 1; bad <= 5; ++bad) {
        g_gamerulesListBad = bad;
        r = Invoke(g_caller, "gagr.list");
        CHECK(r.r == SCO_OK && r.text == "failed");
    }
    g_gamerulesListBad = 0;
    g_gamerulesListResult = SCO_WRONG_THREAD;
    r = Invoke(g_caller, "gagr.list");
    CHECK(r.r == SCO_OK && r.text == "wrong_thread");
    g_gamerulesListResult = SCO_OK;
    const auto calls = g_gamerulesListCalls;
    kFake.size = 23;
    r = Invoke(g_caller, "gagr.list");
    CHECK(r.r == SCO_OK && r.text == "unavailable" && g_gamerulesListCalls == calls);
    kFake.size = sizeof(kFake); kFake.list_params = nullptr;
    r = Invoke(g_caller, "gagr.list");
    CHECK(r.r == SCO_OK && r.text == "unavailable" && g_gamerulesListCalls == calls);
    kFake.list_params = FakeGrList;
    CHECK(g_gamerulesReadCalls == readsBefore);
    g_gamerulesResult = SCO_OK;
    Unload(l);
    CHECK(sco::host::WithdrawGameService(SC_GAMERULES_SERVICE_NAME) == Result::Ok);
}

// ---- sco.game.inventory (public category vocabulary and caller-owned snapshots) ----------------

static uint32_t g_inventoryCalls = 0;
static uint32_t g_inventoryValid = 1;
static uint64_t g_inventoryFirstRaw = 22;
static bool g_inventoryBadSize = false, g_inventoryBadCount = false;
static sco_result g_inventoryResult = SCO_OK;
static sco_result FakeInventory(sco_plugin* self, sc_inventory_snapshot* out) {
    ++g_inventoryCalls;
    if (!self || !out || out->size < SC_INVENTORY_SNAPSHOT_SIZE_1_0 || out->count != SC_INVENTORY_MAX_PORTS) return SCO_BAD_ARG;
    if (g_inventoryResult != SCO_OK) return g_inventoryResult;
    static const uint64_t raw[] = { 22, 191, 192, UINT32_MAX, 0x100000016ull, INT64_MAX, 0x8000000000000000ull, UINT64_MAX };
    out->generation = UINT64_MAX;
    out->count = 8;
    for (uint32_t i = 0; i < out->count; ++i)
        out->ports[i] = { sizeof(sc_inventory_port), g_inventoryValid, i == 0 ? g_inventoryFirstRaw : raw[i], UINT64_MAX };
    if (out->size >= sizeof(*out)) { out->flags = 3; out->total_seen = 129; }
    if (g_inventoryBadSize) out->size = 0;
    if (g_inventoryBadCount) out->count = SC_INVENTORY_MAX_PORTS + 1;
    return SCO_OK;
}

static sco_result FakeInventoryNoTable(const char* name, uint32_t version, const void** out) {
    if (std::strcmp(name, SC_INVENTORY_SERVICE_NAME) == 0) { *out = nullptr; return SCO_OK; }
    return g_api->query_service(name, version, out);
}

static void TestInventory() {
    static sc_inventory_v1 table = { sizeof(sc_inventory_v1), 0, FakeInventory };
    std::string source = R"(
local inv = assert(sco.game.inventory)
assert(inv.item_types.UNKNOWN == 4294967295 and inv.item_type_count == 192 and inv.item_type_build == "4.10.197.2878")
local names = 0
for _ in pairs(inv.item_types) do names = names + 1 end
assert(names == 193)
for _, raw in ipairs({192, 999, 4294967295, 4294967318, 9223372036854775807, "9223372036854775808", "18446744073709551615"}) do
  local mapped, kept = inv.item_type(raw)
  assert(mapped == inv.item_types.UNKNOWN and kept == raw and inv.item_type_name(raw) == "UNDEFINED")
end
for _, raw in ipairs({-1, 1.5, {}, "", "-1", "18446744073709551616", "000000000000000000000", "22x", "22\0"}) do
  assert(not pcall(inv.item_type, raw) and not pcall(inv.item_type_name, raw))
end
sco.register_command{name="gainv.read",title="Read",fn=function()
  local s, err = inv.snapshot()
  return s and (tostring(s.has_details)..":"..s.count) or err
end}
sco.register_command{name="gainv.check",title="Check",fn=function()
  local s = assert(inv.snapshot())
  assert(s.count == 8 and #s.ports == 8 and s.generation == "18446744073709551615")
  assert(s.ports[1].item_class_id == 22 and s.ports[1].item_type == inv.item_types.Char_Armor_Helmet and s.ports[1].item_type_name == "Char_Armor_Helmet")
  local raw = {22, 191, 192, 4294967295, 4294967318, 9223372036854775807, "9223372036854775808", "18446744073709551615"}
  for i, port in ipairs(s.ports) do
    assert(port.valid and port.size == 24 and port.item_class_id == raw[i] and port.entity_id == "18446744073709551615")
    assert(port.item_type == (i <= 2 and raw[i] or inv.item_types.UNKNOWN))
  end
  if s.has_details then assert(s.size == 3096 and s.flags == 3 and s.total_seen == 129)
  else assert(s.size == 3088 and s.flags == nil and s.total_seen == nil) end
  local copied = s.ports[1].item_class_id
  assert(inv.snapshot())
  assert(s.ports[1].item_class_id == copied)
  return "ok"
end}
sco.register_command{name="gainv.invalid",title="Invalid",fn=function()
  local p = assert(inv.snapshot()).ports[1]
  assert(not p.valid and p.item_type == inv.item_types.UNKNOWN and p.item_type_name == "UNDEFINED")
  return tostring(p.item_class_id)
end}
)";
    // The actual Lua helper and exported table must agree with all public C names and ordinals.
    for (uint32_t i = 0; i < SC_ITEM_TYPE_COUNT_197; ++i) {
        const std::string name = sc_inventory_item_type_name(i);
        source += "assert(inv.item_types[\"" + name + "\"] == " + std::to_string(i) + ")\n";
        source += "assert(inv.item_type_name(" + std::to_string(i) + ") == \"" + name + "\")\n";
        source += "assert(inv.item_type(" + std::to_string(i) + ") == " + std::to_string(i) + ")\n";
    }
    Write("gainv", source);
    Loaded l = Load("gainv");
    CHECK(l.p && l.p->state == State::Loaded);
    if (l.p && l.p->state != State::Loaded) std::printf("  gainv: %s\n", l.p->reason.c_str());
    CHECK(Invoke(g_caller, "gainv.read").text == "unavailable"); // vocabulary exists without a provider
    CHECK(sco::host::ProvideGameService(SC_INVENTORY_SERVICE_NAME, SC_INVENTORY_SERVICE_VERSION_1_1, &table) == Result::Ok);
    CHECK(sco::caps::Set("game.inventory.read", true) == Result::Ok);
    CHECK(Invoke(g_caller, "gainv.check").text == "ok");
    g_inventoryValid = 0;
    g_inventoryFirstRaw = 0;
    CHECK(Invoke(g_caller, "gainv.invalid").text == "0");
    g_inventoryFirstRaw = 22;
    CHECK(Invoke(g_caller, "gainv.invalid").text == "22");
    g_inventoryValid = 1;
    CHECK(sco::host::WithdrawGameService(SC_INVENTORY_SERVICE_NAME) == Result::Ok);
    CHECK(sco::host::ProvideGameService(SC_INVENTORY_SERVICE_NAME, SC_INVENTORY_SERVICE_VERSION_1_0, &table) == Result::Ok);
    CHECK(Invoke(g_caller, "gainv.read").text == "false:8" && Invoke(g_caller, "gainv.check").text == "ok");
    CHECK(sco::host::WithdrawGameService(SC_INVENTORY_SERVICE_NAME) == Result::Ok);
    CHECK(sco::host::ProvideGameService(SC_INVENTORY_SERVICE_NAME, SC_INVENTORY_SERVICE_VERSION_1_1, &table) == Result::Ok);
    const uint32_t calls = g_inventoryCalls;
    for (uint32_t size = 0; size < sizeof(table); ++size) {
        table.size = size;
        CHECK(Invoke(g_caller, "gainv.read").text == "unavailable" && g_inventoryCalls == calls);
    }
    table.size = sizeof(table); table.snapshot = nullptr;
    CHECK(Invoke(g_caller, "gainv.read").text == "unavailable" && g_inventoryCalls == calls);
    table.snapshot = FakeInventory;
    CHECK(sco::caps::Set("game.inventory.read", false) == Result::Ok);
    CHECK(Invoke(g_caller, "gainv.read").text == "unavailable" && g_inventoryCalls == calls);
    CHECK(sco::caps::Set("game.inventory.read", true) == Result::Ok);
    g_inventoryResult = SCO_UNAVAILABLE;
    CHECK(Invoke(g_caller, "gainv.read").text == "unavailable");
    g_inventoryResult = SCO_OK; g_inventoryBadCount = true;
    CHECK(Invoke(g_caller, "gainv.read").text == "failed");
    g_inventoryBadCount = false; g_inventoryBadSize = true;
    CHECK(Invoke(g_caller, "gainv.read").text == "failed");
    g_inventoryBadSize = false;
    Unload(l);
    CHECK(Invoke(g_caller, "gainv.read").r == SCO_NOT_FOUND);
    CHECK(sco::host::WithdrawGameService(SC_INVENTORY_SERVICE_NAME) == Result::Ok);
    CHECK(sco::caps::Set("game.inventory.read", false) == Result::Ok);

    // A short or null root member cannot be called, even with a published table and ready cap.
    CHECK(sco::host::ProvideGameService(SC_INVENTORY_SERVICE_NAME, SC_INVENTORY_SERVICE_VERSION_1_1, &table) == Result::Ok);
    CHECK(sco::caps::Set("game.inventory.read", true) == Result::Ok);
    sco_api api = *g_api;
    Loaded guarded = Load("gainv", &api);
    const uint32_t before = g_inventoryCalls;
    api.size = offsetof(sco_api, has) + sizeof(api.has) - 1;
    CHECK(Invoke(g_caller, "gainv.read").text == "unavailable" && g_inventoryCalls == before);
    api.size = sizeof(api); api.has = nullptr;
    CHECK(Invoke(g_caller, "gainv.read").text == "unavailable" && g_inventoryCalls == before);
    api = *g_api; api.size = offsetof(sco_api, query_service) + sizeof(api.query_service) - 1;
    CHECK(Invoke(g_caller, "gainv.read").text == "unavailable" && g_inventoryCalls == before);
    api.size = sizeof(api); api.query_service = nullptr;
    CHECK(Invoke(g_caller, "gainv.read").text == "unavailable" && g_inventoryCalls == before);
    api = *g_api; api.query_service = FakeInventoryNoTable;
    CHECK(Invoke(g_caller, "gainv.read").text == "unavailable" && g_inventoryCalls == before);
    api = *g_api;
    Unload(guarded);
    CHECK(sco::host::WithdrawGameService(SC_INVENTORY_SERVICE_NAME) == Result::Ok);
    CHECK(sco::caps::Set("game.inventory.read", false) == Result::Ok);
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
                                          nullptr /* keep: Lua is read-only */,
                                          nullptr, nullptr, nullptr, nullptr /* native-only 1.2 lookup tail */ };
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
    Write("gwnone", "assert(sco.game.inventory and sco.game.world == nil, 'world not published')\n");
    Loaded none = Load("gwnone");
    CHECK(none.p && none.p->state == State::Loaded);
    Unload(none);

    static sc_world_v1 kFake = { sizeof(sc_world_v1), 0, FakeRaycast, FakeCamera, FakeWorldLastError, FakeFreeCam };
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
sco.register_command{ name = "gwplay.freecam", fn = function()
  local f, e, m = w.free_camera_flag()
  if f == nil then return "nil:" .. tostring(e) .. ":" .. tostring(m) end
  return tostring(f)
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

    // game.world 1.1's free_camera_flag: read-only, gated on the table's size and a non-null member.
    g_freeCamCalls = 0;
    g_freeCamValue = 1;
    r = Invoke(g_caller, "gwplay.freecam");
    CHECK(r.r == SCO_OK && r.text == "true");
    if (r.text != "true") std::printf("  gwplay.freecam -> %s\n", r.text.c_str());
    g_freeCamValue = 0;
    r = Invoke(g_caller, "gwplay.freecam");
    CHECK(r.r == SCO_OK && r.text == "false");
    CHECK(g_freeCamCalls == 2);
    g_freeCamResult = SCO_UNAVAILABLE;
    r = Invoke(g_caller, "gwplay.freecam");
    CHECK(r.r == SCO_OK && r.text == "nil:unavailable:raycast: no hit within 50.0 m");
    if (r.text != "nil:unavailable:raycast: no hit within 50.0 m") std::printf("  gwplay.freecam -> %s\n", r.text.c_str());
    g_freeCamResult = SCO_OK;
    g_freeCamCalls = 0;
    kFake.size = offsetof(sc_world_v1, free_camera_flag);   // a game.world 1.0 table
    kFake.last_error = nullptr;
    r = Invoke(g_caller, "gwplay.freecam");
    CHECK(r.r == SCO_OK && r.text == "nil:unavailable:nil");
    kFake.size = offsetof(sc_world_v1, free_camera_flag) + sizeof(kFake.free_camera_flag) - 1;
    r = Invoke(g_caller, "gwplay.freecam");
    CHECK(r.r == SCO_OK && r.text == "nil:unavailable:nil");
    CHECK(g_freeCamCalls == 0);   // never called past the table's end
    kFake.size = sizeof(kFake);
    kFake.free_camera_flag = nullptr;
    r = Invoke(g_caller, "gwplay.freecam");
    CHECK(r.r == SCO_OK && r.text == "nil:unavailable:nil");
    kFake.free_camera_flag = FakeFreeCam;
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
