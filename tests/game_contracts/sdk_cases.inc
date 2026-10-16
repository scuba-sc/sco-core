// SPDX-License-Identifier: GPL-3.0-only
// ---- scosdk/game/actors.hpp over a stand-in game.actors, published as the game pack does ---------

static sco_plugin* g_gaSelf = nullptr;
static uint64_t g_gaDespawned = 0;
static sco_result GaLocal(uint64_t* actor, uint64_t* entity) {
    *actor = 5;
    *entity = 6;
    return SCO_OK;
}
static sco_result GaHealth(uint64_t id, float* cur, float* max) {
    if (id != 6) return SCO_NOT_FOUND;
    *cur = 12.5f;
    *max = 0;
    return SCO_OK;
}
static sco_result GaState(uint64_t, uint32_t*) { return SCO_UNAVAILABLE; }
static sco_result GaSpawn(sco_plugin* self, const char* cls, uint64_t zone, const double* pos, uint64_t* id) {
    if (self != g_gaSelf || std::strcmp(cls, "Npc") != 0 || zone != 9 || pos[2] != 3.0) return SCO_BAD_ARG;
    *id = 77;
    return SCO_OK;
}
static sco_result GaDespawn(sco_plugin* self, uint64_t id) {
    if (self != g_gaSelf || id != 77) return SCO_NOT_FOUND;
    g_gaDespawned = id;
    return SCO_OK;
}
static sco_result GaLastError(sco_plugin* self, char* out, uint32_t* io) {
    const std::string why = self ? std::string(300, 'x') : "read";   // past the wrapper's first 256 bytes
    const uint32_t cap = *io;
    *io = static_cast<uint32_t>(why.size() + 1);
    if (cap < why.size() + 1) return SCO_TOO_MANY;
    std::memcpy(out, why.c_str(), why.size() + 1);
    return SCO_OK;
}

static void TestGameActors(const sco_api* api) {
    g_gaSelf = sco::host::NewPlugin("gatest");
    sdk::game::Actors none;
    CHECK(none.Open(api, g_gaSelf) == SCO_NOT_FOUND && !none);
    uint64_t a = 1, e = 1, id = 1;
    CHECK(none.LocalPlayer(a, e) == SCO_UNAVAILABLE && a == 0 && e == 0 && none.LastError().empty());
    static const sc_actors_v1 kFake = { sizeof(sc_actors_v1), 0, GaLocal, GaSpawn, GaDespawn, GaLastError, GaHealth, GaState };
    CHECK(sco::host::ProvideGameService(SC_ACTORS_NAME, SC_ACTORS_VERSION_1_1, &kFake) == sco::Result::Ok);
    sdk::game::Actors actors;
    CHECK(actors.Open(api, g_gaSelf) == SCO_OK && actors && actors.Table() == &kFake);
    CHECK(actors.LocalPlayer(a, e) == SCO_OK && a == 5 && e == 6);
    const double pos[3] = { 1, 2, 3 };
    CHECK(actors.SpawnNpc("Npc", 9, pos, id) == SCO_OK && id == 77);
    CHECK(actors.Despawn(77) == SCO_OK && g_gaDespawned == 77);
    CHECK(actors.LastError() == std::string(300, 'x') && actors.LastReadError() == "read");
    float hp = 1, hpMax = 1;
    sc_actor_state st = SC_ACTOR_DEAD;
    CHECK(actors.Health(6, hp, hpMax) == SCO_OK && hp == 12.5f && hpMax == 0);
    CHECK(actors.Health(7, hp, hpMax) == SCO_NOT_FOUND && hp == 0 && hpMax == 0);
    CHECK(actors.State(6, st) == SCO_UNAVAILABLE && st == SC_ACTOR_ALIVE);
    // A 1.0 table (40 bytes): the 1.1 functions are past its end, so the wrapper never calls them.
    static const sc_actors_v1 kOld = { offsetof(sc_actors_v1, health), 0, GaLocal, GaSpawn, GaDespawn, GaLastError, GaHealth, GaState };
    CHECK(sco::host::WithdrawGameService(SC_ACTORS_NAME) == sco::Result::Ok);
    CHECK(sco::host::ProvideGameService(SC_ACTORS_NAME, SC_ACTORS_VERSION_1_0, &kOld) == sco::Result::Ok);
    sdk::game::Actors old;
    CHECK(old.Open(api, g_gaSelf) == SCO_OK);
    CHECK(old.LocalPlayer(a, e) == SCO_OK && old.Health(6, hp, hpMax) == SCO_UNAVAILABLE && old.State(6, st) == SCO_UNAVAILABLE);
    CHECK(sco::host::WithdrawGameService(SC_ACTORS_NAME) == sco::Result::Ok);
}

// ---- scosdk/game/creative.hpp over a stand-in game.creative --------------------------------------

static sco_plugin* g_gcSelf = nullptr;
static uint32_t g_gcFlags = 0;
static float g_gcSpeed = 0;
static sco_result GcToggle(sco_plugin* self, int on, uint32_t bit) {
    if (self != g_gcSelf) return SCO_BAD_ARG;
    g_gcFlags = on ? (g_gcFlags | bit) : (g_gcFlags & ~bit);
    return SCO_OK;
}
static sco_result GcGod(sco_plugin* self, int on) { return GcToggle(self, on, SC_CREATIVE_GOD_MODE); }
static sco_result GcFly(sco_plugin* self, int on) { return GcToggle(self, on, SC_CREATIVE_FLY); }
static sco_result GcAmmo(sco_plugin* self, int on) { return GcToggle(self, on, SC_CREATIVE_INFINITE_AMMO); }
static sco_result GcShip(sco_plugin* self, int on) { return GcToggle(self, on, SC_CREATIVE_INFINITE_SHIP_AMMO); }
static sco_result GcSpeed(sco_plugin* self, float mps) {
    if (self != g_gcSelf || !(mps > 0 && mps <= SC_CREATIVE_FLY_SPEED_MAX)) return SCO_BAD_ARG;
    g_gcSpeed = mps;
    return SCO_OK;
}
static sco_result GcState(sco_plugin* self, uint32_t* flags) {
    if (self != g_gcSelf) return SCO_BAD_ARG;
    *flags = g_gcFlags;
    return SCO_OK;
}
static sco_result GcLastError(sco_plugin* self, char* out, uint32_t* io) {
    static const char kWhy[] = "speed must be above 0";
    if (self != g_gcSelf) return SCO_BAD_ARG;
    const uint32_t cap = *io;
    *io = sizeof(kWhy);
    if (cap < sizeof(kWhy)) return SCO_TOO_MANY;
    std::memcpy(out, kWhy, sizeof(kWhy));
    return SCO_OK;
}

static void TestGameCreative(const sco_api* api) {
    g_gcSelf = sco::host::NewPlugin("gctest");
    sdk::game::Creative none;
    CHECK(none.Open(api, g_gcSelf) == SCO_NOT_FOUND && !none);
    uint32_t flags = 7;
    CHECK(none.SetGodMode(true) == SCO_UNAVAILABLE && none.State(flags) == SCO_UNAVAILABLE && flags == 0);
    static const sc_creative_v1 kFake = { sizeof(sc_creative_v1), 0, GcGod, GcFly, GcSpeed, GcAmmo, GcShip, GcState, GcLastError };
    CHECK(sco::host::ProvideGameService(SC_CREATIVE_SERVICE_NAME, SC_CREATIVE_SERVICE_VERSION, &kFake) == sco::Result::Ok);
    sdk::game::Creative cr;
    CHECK(cr.Open(api, g_gcSelf) == SCO_OK && cr && cr.Table() == &kFake);
    CHECK(cr.SetGodMode(true) == SCO_OK && cr.SetFlyMode(true) == SCO_OK && cr.SetInfiniteAmmo(true) == SCO_OK &&
          cr.SetInfiniteShipAmmo(true) == SCO_OK);
    CHECK(cr.State(flags) == SCO_OK &&
          flags == (SC_CREATIVE_GOD_MODE | SC_CREATIVE_FLY | SC_CREATIVE_INFINITE_AMMO | SC_CREATIVE_INFINITE_SHIP_AMMO));
    CHECK(cr.SetFlyMode(false) == SCO_OK && cr.SetGodMode(false) == SCO_OK && cr.State(flags) == SCO_OK &&
          flags == (SC_CREATIVE_INFINITE_AMMO | SC_CREATIVE_INFINITE_SHIP_AMMO));
    CHECK(cr.SetFlySpeed(250.0f) == SCO_OK && g_gcSpeed == 250.0f);
    CHECK(cr.SetFlySpeed(0.0f) == SCO_BAD_ARG && g_gcSpeed == 250.0f);
    char why[64] = {};
    CHECK(cr.LastError(why) == SCO_OK && std::strcmp(why, "speed must be above 0") == 0);
    char tiny[4] = { 'x', 0, 0, 0 };
    CHECK(cr.LastError(tiny) == SCO_TOO_MANY && tiny[0] == '\0');
    CHECK(sco::host::WithdrawGameService(SC_CREATIVE_SERVICE_NAME) == sco::Result::Ok);
}

// ---- scosdk/game/world.hpp over a stand-in game.world --------------------------------------------

static sco_result GwRaycast(uint64_t zone, const double* from, const double* dir, double max, sc_world_hit* hit) {
    if (!hit || hit->size != sizeof(sc_world_hit) || zone == 0) return SCO_BAD_ARG;
    if (zone != 9) return SCO_NOT_FOUND;
    for (int i = 0; i < 3; ++i) hit->pos[i] = from[i] + dir[i] * max;
    hit->distance = max;
    return SCO_OK;
}
static sco_result GwCamera(double* pos, double* rot, uint64_t* zone) {
    pos[0] = 1; pos[1] = 2; pos[2] = 3;
    rot[0] = 0; rot[1] = 0; rot[2] = 0; rot[3] = 1;
    *zone = 9;
    return SCO_OK;
}
static sco_result GwLastError(sco_plugin*, char* out, uint32_t* io) {
    static const char kWhy[] = "no hit";
    const uint32_t cap = *io;
    *io = sizeof(kWhy);
    if (cap < sizeof(kWhy)) return SCO_TOO_MANY;
    std::memcpy(out, kWhy, sizeof(kWhy));
    return SCO_OK;
}

static int g_gwFreeCamCalls = 0;
static sco_result GwFreeCam(uint32_t* set) {
    ++g_gwFreeCamCalls;
    if (!set) return SCO_BAD_ARG;
    *set = 1;
    return SCO_OK;
}

static void TestGameWorld(const sco_api* api) {
    sco_plugin* self = sco::host::NewPlugin("gwtest");
    bool freeCam = true;
    sdk::game::World none;
    double pos[3], rot[4];
    uint64_t zone = 1;
    sc_world_hit hit;
    const double from[3] = { 0, 0, 1 }, down[3] = { 0, 0, -1 };
    CHECK(none.Open(api, self) == SCO_NOT_FOUND && !none);
    CHECK(none.Camera(pos, rot, zone) == SCO_UNAVAILABLE && zone == 0 && none.Raycast(9, from, down, 5.0, hit) == SCO_UNAVAILABLE &&
          none.LastError().empty());
    static const sc_world_v1 kFake = { sizeof(sc_world_v1), 0, GwRaycast, GwCamera, GwLastError, nullptr };
    CHECK(sco::host::ProvideGameService(SC_WORLD_NAME, SC_WORLD_VERSION_1_0, &kFake) == sco::Result::Ok);
    sdk::game::World world;
    CHECK(world.Open(api, self) == SCO_OK && world && world.Table() == &kFake);
    CHECK(world.Raycast(9, from, down, 5.0, hit) == SCO_OK && hit.size == sizeof(hit) && hit.pos[2] == -4.0 && hit.distance == 5.0 &&
          hit.entity_id == 0 && hit.flags == 0);
    CHECK(world.Raycast(8, from, down, 5.0, hit) == SCO_NOT_FOUND && hit.distance == 0);
    CHECK(world.Raycast(0, from, down, 5.0, hit) == SCO_BAD_ARG);
    CHECK(world.Camera(pos, rot, zone) == SCO_OK && pos[2] == 3 && rot[3] == 1 && zone == 9);
    CHECK(world.LastError() == "no hit");
    CHECK(none.FreeCameraFlag(freeCam) == SCO_UNAVAILABLE && !freeCam);
    freeCam = true;
    CHECK(world.FreeCameraFlag(freeCam) == SCO_UNAVAILABLE && !freeCam);   // a null 1.1 member
    CHECK(sco::host::WithdrawGameService(SC_WORLD_NAME) == sco::Result::Ok);
    // A 1.0 table (32 bytes): free_camera_flag is past its end, so the wrapper never calls it.
    static const sc_world_v1 kOld = { offsetof(sc_world_v1, free_camera_flag), 0, GwRaycast, GwCamera, GwLastError, GwFreeCam };
    CHECK(sco::host::ProvideGameService(SC_WORLD_NAME, SC_WORLD_VERSION_1_0, &kOld) == sco::Result::Ok);
    sdk::game::World old;
    g_gwFreeCamCalls = 0;
    freeCam = true;
    CHECK(old.Open(api, self) == SCO_OK && old.FreeCameraFlag(freeCam) == SCO_UNAVAILABLE && !freeCam && g_gwFreeCamCalls == 0);
    CHECK(old.Camera(pos, rot, zone) == SCO_OK && zone == 9);
    CHECK(sco::host::WithdrawGameService(SC_WORLD_NAME) == sco::Result::Ok);
    static const sc_world_v1 kNew = { sizeof(sc_world_v1), 0, GwRaycast, GwCamera, GwLastError, GwFreeCam };
    CHECK(sco::host::ProvideGameService(SC_WORLD_NAME, SC_WORLD_VERSION_1_1, &kNew) == sco::Result::Ok);
    sdk::game::World current;
    CHECK(current.Open(api, self) == SCO_OK && current.FreeCameraFlag(freeCam) == SCO_OK && freeCam && g_gwFreeCamCalls == 1);
    CHECK(sco::host::WithdrawGameService(SC_WORLD_NAME) == sco::Result::Ok);
}

// ---- scosdk/game/hangar.hpp over a stand-in game.hangar ------------------------------------------

static sco_plugin* g_ghSelf = nullptr;
static uint32_t g_ghOp = 0;
static uint64_t g_ghAtc = 0;
static sco_result GhLiftOf(sco_plugin* self, uint64_t area, uint64_t* lift) {
    if (self != g_ghSelf || area == 0 || !lift) return SCO_BAD_ARG;
    if (area != 11) return SCO_NOT_FOUND;
    *lift = 12;
    return SCO_OK;
}
static sco_result GhLiftRequest(sco_plugin* self, uint64_t lift, uint32_t op) {
    if (self != g_ghSelf || lift != 12 || (op != SC_HANGAR_LIFT_OPEN && op != SC_HANGAR_LIFT_CLOSE)) return SCO_BAD_ARG;
    g_ghOp = op;
    return SCO_OK;
}
static sco_result GhAtcCancel(sco_plugin* self, uint64_t atc) {
    if (self != g_ghSelf || atc == 0) return SCO_BAD_ARG;
    if (atc != 13) return SCO_NOT_FOUND;
    g_ghAtc = atc;
    return SCO_OK;
}
static sco_result GhLastError(sco_plugin* self, char* out, uint32_t* io) {
    static const char kWhy[] = "no lift goes with it";
    if (self != g_ghSelf) return SCO_BAD_ARG;
    const uint32_t cap = *io;
    *io = sizeof(kWhy);
    if (cap < sizeof(kWhy)) return SCO_TOO_MANY;
    std::memcpy(out, kWhy, sizeof(kWhy));
    return SCO_OK;
}

static void TestGameHangar(const sco_api* api) {
    g_ghSelf = sco::host::NewPlugin("ghtest");
    sdk::game::Hangar none;
    uint64_t lift = 1;
    CHECK(none.Open(api, g_ghSelf) == SCO_NOT_FOUND && !none);
    CHECK(none.LiftOfLandingArea(11, lift) == SCO_UNAVAILABLE && lift == 0 && none.LiftRequest(12, SC_HANGAR_LIFT_OPEN) == SCO_UNAVAILABLE &&
          none.AtcCancelRequest(13) == SCO_UNAVAILABLE && none.LastError().empty());
    static const sc_hangar_v1 kFake = { sizeof(sc_hangar_v1), 0, GhLiftOf, GhLiftRequest, GhAtcCancel, GhLastError };
    CHECK(sco::host::ProvideGameService(SC_HANGAR_SERVICE_NAME, SC_HANGAR_SERVICE_VERSION, &kFake) == sco::Result::Ok);
    sdk::game::Hangar hangar;
    CHECK(hangar.Open(api, g_ghSelf) == SCO_OK && hangar && hangar.Table() == &kFake);
    CHECK(hangar.LiftOfLandingArea(11, lift) == SCO_OK && lift == 12);
    CHECK(hangar.LiftOfLandingArea(10, lift) == SCO_NOT_FOUND && lift == 0);
    CHECK(hangar.LiftRequest(12, SC_HANGAR_LIFT_CLOSE) == SCO_OK && g_ghOp == SC_HANGAR_LIFT_CLOSE);
    CHECK(hangar.LiftRequest(12, SC_HANGAR_LIFT_OPEN) == SCO_OK && g_ghOp == SC_HANGAR_LIFT_OPEN);
    CHECK(hangar.AtcCancelRequest(13) == SCO_OK && g_ghAtc == 13 && hangar.AtcCancelRequest(14) == SCO_NOT_FOUND);
    CHECK(hangar.LastError() == "no lift goes with it");
    CHECK(sco::host::WithdrawGameService(SC_HANGAR_SERVICE_NAME) == sco::Result::Ok);
    // A table that ends before atc_cancel_request: the wrapper never calls past its end.
    static const sc_hangar_v1 kShort = { offsetof(sc_hangar_v1, atc_cancel_request), 0, GhLiftOf, GhLiftRequest, GhAtcCancel, GhLastError };
    CHECK(sco::host::ProvideGameService(SC_HANGAR_SERVICE_NAME, SC_HANGAR_SERVICE_VERSION, &kShort) == sco::Result::Ok);
    sdk::game::Hangar shortTable;
    g_ghAtc = 0;
    CHECK(shortTable.Open(api, g_ghSelf) == SCO_OK && shortTable.AtcCancelRequest(13) == SCO_UNAVAILABLE && g_ghAtc == 0);
    CHECK(shortTable.LiftOfLandingArea(11, lift) == SCO_OK && lift == 12 && shortTable.LastError().empty());
    CHECK(sco::host::WithdrawGameService(SC_HANGAR_SERVICE_NAME) == sco::Result::Ok);
}

// ---- scosdk/game/reputation.hpp over a stand-in game.reputation ----------------------------------

static sco_plugin* g_grSelf = nullptr;
static sco_result GrChecks(sco_plugin* self, sc_reputation_checks* out) {
    if (self != g_grSelf || !out || out->size < sizeof(sc_reputation_checks)) return SCO_BAD_ARG;
    out->sites = 10;
    out->intact = 9;
    out->services = 1;
    return SCO_OK;
}
static sco_result GrLastError(sco_plugin* self, char* out, uint32_t* io) {
    const std::string why(300, 'r');   // past the wrapper's first 256 bytes
    if (self != g_grSelf) return SCO_BAD_ARG;
    const uint32_t cap = *io;
    *io = static_cast<uint32_t>(why.size() + 1);
    if (cap < why.size() + 1) return SCO_TOO_MANY;
    std::memcpy(out, why.c_str(), why.size() + 1);
    return SCO_OK;
}

static void TestGameReputation(const sco_api* api) {
    g_grSelf = sco::host::NewPlugin("grtest");
    sdk::game::Reputation none;
    sc_reputation_checks checks = {};
    checks.sites = 7;
    CHECK(none.Open(api, g_grSelf) == SCO_NOT_FOUND && !none);
    CHECK(none.Checks(checks) == SCO_UNAVAILABLE && checks.size == sizeof(checks) && checks.sites == 0 && none.LastError().empty());
    static const sc_reputation_v1 kFake = { sizeof(sc_reputation_v1), 0, GrChecks, GrLastError };
    CHECK(sco::host::ProvideGameService(SC_REPUTATION_SERVICE_NAME, SC_REPUTATION_SERVICE_VERSION, &kFake) == sco::Result::Ok);
    sdk::game::Reputation rep;
    CHECK(rep.Open(api, g_grSelf) == SCO_OK && rep && rep.Table() == &kFake);
    CHECK(rep.Checks(checks) == SCO_OK && checks.sites == 10 && checks.intact == 9 && checks.services == 1);
    CHECK(rep.LastError() == std::string(300, 'r'));
    CHECK(sco::host::WithdrawGameService(SC_REPUTATION_SERVICE_NAME) == sco::Result::Ok);
}

// ---- scosdk/game/gamerules.hpp over a stand-in game.gamerules ------------------------------------

static sco_plugin* g_gruleSelf = nullptr;
static uint32_t g_gruleValid = 1;
static sco_result GruleRead(sco_plugin* self, sc_gamerules_state* out) {
    if (self != g_gruleSelf || !out || out->size < sizeof(sc_gamerules_state)) return SCO_BAD_ARG;
    out->valid = g_gruleValid;
    out->generation = 7;
    out->enforce_group_qt = 1;
    out->quantum_boost_allowed = 1;
    out->travel_target_threshold_angle = 45.0f;
    out->boost_group_distance_threshold = 2500.0f;
    return SCO_OK;
}

static void TestGameRules(const sco_api* api) {
    g_gruleSelf = sco::host::NewPlugin("grulestest");
    sdk::game::GameRules none;
    sc_gamerules_state state = {};
    state.valid = 1;
    CHECK(none.Open(api, g_gruleSelf) == SCO_NOT_FOUND && !none);
    CHECK(none.Read(state) == SCO_UNAVAILABLE && state.size == sizeof(state) && state.valid == 0);
    static sc_gamerules_v1 kFake = { sizeof(sc_gamerules_v1), 0, GruleRead };
    CHECK(sco::host::ProvideGameService(SC_GAMERULES_SERVICE_NAME, SC_GAMERULES_SERVICE_VERSION, &kFake) == sco::Result::Ok);
    sdk::game::GameRules rules;
    CHECK(rules.Open(api, g_gruleSelf) == SCO_OK && rules && rules.Table() == &kFake);
    CHECK(rules.Read(state) == SCO_OK && state.size == sizeof(state) && state.valid == 1 && state.generation == 7);
    CHECK(state.enforce_group_qt == 1 && state.quantum_travel_allowed == 0 && state.quantum_boost_allowed == 1);
    CHECK(state.travel_target_threshold_angle == 45.0f && state.boost_group_distance_threshold == 2500.0f);
    g_gruleValid = 0;
    CHECK(rules.Read(state) == SCO_OK && state.valid == 0);   // no parameter set received yet: the caller checks valid
    g_gruleValid = 1;
    kFake.size = offsetof(sc_gamerules_v1, read) + sizeof(kFake.read) - 1;   // a partial pointer is not the member
    CHECK(rules.Read(state) == SCO_UNAVAILABLE && state.valid == 0 && state.size == sizeof(state));
    kFake.size = sizeof(kFake);
    kFake.read = nullptr;
    CHECK(rules.Read(state) == SCO_UNAVAILABLE);
    kFake.read = GruleRead;
    CHECK(rules.Read(state) == SCO_OK);
    CHECK(sco::host::WithdrawGameService(SC_GAMERULES_SERVICE_NAME) == sco::Result::Ok);
}

// ---- scosdk/game/events.hpp: the helpers copy the payload and refuse a short one ------------------

static void TestGameEvents(const sco_api* api) {
    sco_plugin* self = sco::host::NewPlugin("gevtest");
    Consumer plugin;
    CHECK(sdk::LoadPlugin(plugin, api, self) == SCO_OK);
    sc_game_actor_died actorSeen = {};
    int actors = 0;
    sdk::Subscription actorSub = sdk::game::OnActorDied(plugin, [&](const sc_game_actor_died& e) { actorSeen = e; ++actors; });
    CHECK(actorSub.Result() == SCO_OK);
    const sc_game_actor_died actorPayload = { sizeof(actorPayload), 0, 99, 0 };
    CHECK(sco::Dispatch(SC_GAME_EVENT_ACTOR_DIED, &actorPayload) == sco::Result::Ok && actors == 1 && actorSeen.entity_id == 99);
    const sc_game_actor_died actorShort = { 8, 0, 100, 0 };
    CHECK(sco::Dispatch(SC_GAME_EVENT_ACTOR_DIED, &actorShort) == sco::Result::Ok && actors == 1);
    CHECK(sco::Dispatch(SC_GAME_EVENT_ACTOR_DIED, nullptr) == sco::Result::Ok && actors == 1);
    actorSub.Reset();
    CHECK(sco::Dispatch(SC_GAME_EVENT_ACTOR_DIED, &actorPayload) == sco::Result::Ok && actors == 1);
    sc_game_player_died seen = {};
    sc_game_zone_changed zoneSeen = {};
    sc_game_player_spawned spawnSeen = {};
    int died = 0, zones = 0, spawns = 0;
    sdk::Subscription a = sdk::game::OnPlayerDied(plugin, [&](const sc_game_player_died& e) { seen = e; ++died; });
    sdk::Subscription b = sdk::game::OnZoneChanged(plugin, [&](const sc_game_zone_changed& e) { zoneSeen = e; ++zones; });
    sdk::Subscription c = sdk::game::OnPlayerSpawned(plugin, [&](const sc_game_player_spawned& e) { spawnSeen = e; ++spawns; });
    CHECK(a.Result() == SCO_OK && b.Result() == SCO_OK && c.Result() == SCO_OK);
    const sc_game_player_died d = { sizeof(d), 0, 42, 7 };
    const sc_game_zone_changed z = { sizeof(z), 0, 5, 6 };
    const sc_game_player_spawned s = { sizeof(s), 0, 3, 4 };
    CHECK(sco::Dispatch(SC_GAME_EVENT_PLAYER_DIED, &d) == sco::Result::Ok && died == 1 && seen.entity_id == 42 && seen.killer_id == 7);
    CHECK(sco::Dispatch(SC_GAME_EVENT_ZONE_CHANGED, &z) == sco::Result::Ok && zones == 1 && zoneSeen.old_zone_id == 5 && zoneSeen.new_zone_id == 6);
    CHECK(sco::Dispatch(SC_GAME_EVENT_PLAYER_SPAWNED, &s) == sco::Result::Ok && spawns == 1 && spawnSeen.entity_id == 3 && spawnSeen.zone_id == 4);
    const sc_game_player_died shortDied = { 8, 0, 1, 1 };   // shorter than the struct: dropped, not read past its end
    CHECK(sco::Dispatch(SC_GAME_EVENT_PLAYER_DIED, &shortDied) == sco::Result::Ok && died == 1);
    CHECK(sco::Dispatch(SC_GAME_EVENT_PLAYER_DIED, nullptr) == sco::Result::Ok && died == 1);
    a.Reset();
    CHECK(sco::Dispatch(SC_GAME_EVENT_PLAYER_DIED, &d) == sco::Result::Ok && died == 1);   // unsubscribed
    b.Reset();
    c.Reset();
    sdk::UnloadPlugin(plugin);
}
