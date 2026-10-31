// SPDX-License-Identifier: GPL-3.0-only
// Game-only test cases; included by the corresponding test when the game pack is enabled.
    // gameServices: the game pack publishes teleport.spatial, spawn.entities, game.vehicles, game.actors and game.world under "game" before
    // plugins load and withdraws them after. With no game image the teleport.* and spawn.* rows
    // aren't OK, so teleport.spatial answers 0 and spawn.entities says the spawner isn't available.
    CHECK(!pf.dataRoot.empty()); // storage and game services must coexist
    // A fixture-owned admission domain covers both startup and shutdown.
    // All six test reader threads enroll below; fixture modules have no
    // unmanaged reader workers. No game image or native engine is present.
    // This proves no exclusion for product or native execution.
    sco::OwnedShutdown readerGate;
    struct SyntheticBarrier {
        std::thread::id control; sco::OwnedShutdown* readers;
        sco::OwnedShutdown::Transition* transition = nullptr;
        bool beforeStop = false; unsigned calls = 0;
    };
    SyntheticBarrier barrier{std::this_thread::get_id(), &readerGate};
    const bool noImageOrRegistry = !pf.image && !sco::AcquireHostSignatureRegistry();
    CHECK(noImageOrRegistry);
    if (!noImageOrRegistry) return;
    pf.privateSession.enabled = false;
    pf.runtimeInspection.enabled = false;
    pf.gameReadersContext = &barrier;
    pf.gameReadersQuiesced = [](void* context) {
        auto* state = static_cast<SyntheticBarrier*>(context);
        if (!state || state->control != std::this_thread::get_id() ||
            state->calls > 1 || state->beforeStop != (state->calls == 1)) return false;
        if (state->beforeStop ? !state->readers->ready() :
            (!state->transition || !state->transition->valid_for(*state->readers))) return false;
        bool admitted = true;
        std::thread probe([&] { admitted = static_cast<bool>(state->readers->try_read()); });
        probe.join();
        if (admitted) return false;
        ++state->calls;
        return true;
    };
    pf.gameServices = true;
    g_log.clear();
    CHECK(sco::OwnedShutdownHostAccess::begin_startup(readerGate));
    bool fixtureStarted = false;
    std::atomic<int> fixturePhase{0};
    {
        auto transition = sco::OwnedShutdownHostAccess::transition(readerGate);
        CHECK(transition);
        if (!transition) std::abort();
        barrier.transition = &transition;
        fixtureStarted = sco::app::Start(pf);
        CHECK(fixtureStarted && barrier.calls == 1);
        if (!fixtureStarted || barrier.calls != 1 || !transition.publish_and_open(fixturePhase, 0, 1)) std::abort();
    }
    barrier.transition = nullptr;
    CHECK(api->query_service("sco.storage", 0x00010000, &table) == SCO_OK && table);
    CHECK(!Logged("[app] game services not started:"));
    if (Logged("[app] game services not started:"))
        for (const auto& line : g_log)
            if (line.rfind("[app] game services not started:", 0) == 0)
                std::printf("  %s\n", line.c_str());
    CHECK(StateOf("needsgame") == State::Loaded);   // requires game.vehicles, which the game pack published
    CHECK(api->query_service("teleport.spatial", 0x00010000, &table) == SCO_OK && table);
    if (table) {
        struct Spatial { uint32_t size; int (*player_pose)(double*, double*, uint64_t*); };
        double pos[3], rot[4]; uint64_t zone = 0;
        CHECK(static_cast<const Spatial*>(table)->size >= sizeof(Spatial));
        CHECK(static_cast<const Spatial*>(table)->player_pose(pos, rot, &zone) == 0);
    }
    CHECK(api->query_service(SC_SPAWN_SERVICE_NAME, SC_SPAWN_SERVICE_VERSION, &table) == SCO_OK && table);
    if (table) {
        const auto* sp = static_cast<const sc_spawn_service_v1*>(table);
        const double offset[3] = {};
        const double rot[4] = { 0, 0, 0, 1 };
        uint64_t id = 7;
        CHECK(sp->size == sizeof(sc_spawn_service_v1));
        const char* err = sp->spawn_near_player("DRAK_Cutlass_Black", offset, &id);
        CHECK(err && std::strcmp(err, "the spawner isn't available on this game build") == 0 && id == 0);
        id = 7;
        err = sp->spawn_as(reinterpret_cast<sco_plugin*>(&table), "DRAK_Cutlass_Black", offset, &id);
        CHECK(err && std::strcmp(err, "the spawner isn't available on this game build") == 0 && id == 0);
        CHECK(sp->class_exists("DRAK_Cutlass_Black") == 0);
        CHECK(sp->local_player_id() == 0 && sp->player_ship_id() == 0 && sp->entity_alive(1) == 0);
        CHECK(sp->set_entity_transform(reinterpret_cast<sco_plugin*>(&table), 1, 0, offset, rot) == 0);
    }
    // game.vehicles: without the spawn.* rows every function answers SCO_UNAVAILABLE with a
    // reason, its capabilities aren't ready, and off the game thread it refuses before anything.
    CHECK(api->query_service(SC_VEHICLES_SERVICE_NAME, SC_VEHICLES_SERVICE_VERSION, &table) == SCO_OK && table);
    if (table) {
        const auto* v = static_cast<const sc_vehicles_v1*>(table);
        sco_plugin* self = reinterpret_cast<sco_plugin*>(&table);
        uint64_t ship = 7;
        uint32_t count = 9, more = 9;
        CHECK(v->size == sizeof(sc_vehicles_v1));
        CHECK(v->player_ship(&ship) == SCO_UNAVAILABLE && ship == 0);
        CHECK(v->seats(1, nullptr, 0, &count, &more) == SCO_UNAVAILABLE && count == 0 && more == 0);
        CHECK(v->seat_occupant(1, 0, &ship) == SCO_UNAVAILABLE && ship == 0);
        CHECK(v->seat(self, 1, 2, 0) == SCO_UNAVAILABLE && v->eject(self, 1) == SCO_UNAVAILABLE);
        CHECK(v->power_on(self, 2) == SCO_UNAVAILABLE);
        char msg[128] = {};
        uint32_t n = sizeof(msg);
        CHECK(v->last_error(self, msg, &n) == SCO_OK && std::strstr(msg, "game.vehicles.flight_ready") && n == std::strlen(msg) + 1);
        n = 0;
        CHECK(v->last_error(self, nullptr, &n) == SCO_TOO_MANY && n == std::strlen(msg) + 1);
        n = sizeof(msg);
        CHECK(v->last_error(nullptr, msg, &n) == SCO_OK && std::strstr(msg, "game.vehicles.seats"));
        CHECK(!sco::caps::Has("game.vehicles.seats") && !sco::caps::Has("game.vehicles.seat") &&
              !sco::caps::Has("game.vehicles.flight_ready"));
        sco_result off = SCO_OK, offErr = SCO_OK;
        char offMsg[64] = {};
        std::thread([&] { auto gateRead = readerGate.try_read(); CHECK(gateRead); if (!gateRead) return;
            uint64_t id = 7;
            off = v->player_ship(&id);
            uint32_t size = sizeof(offMsg);
            offErr = v->last_error(self, offMsg, &size);
        }).join();
        CHECK(off == SCO_WRONG_THREAD && offErr == SCO_OK && std::strcmp(offMsg, "game thread only") == 0);
    }
    // game.creative: published with them. No game image, so none of its capabilities is ready: every
    // function answers SCO_UNAVAILABLE with a reason, state() too (no capability at all), and off the
    // game thread it refuses before anything else.
    CHECK(api->query_service(SC_CREATIVE_SERVICE_NAME, SC_CREATIVE_SERVICE_VERSION, &table) == SCO_OK && table);
    if (table) {
        const auto* c = static_cast<const sc_creative_v1*>(table);
        sco_plugin* self = reinterpret_cast<sco_plugin*>(&table);
        CHECK(c->size == sizeof(sc_creative_v1));
        CHECK(c->set_god_mode(self, 1) == SCO_UNAVAILABLE && c->set_fly_mode(self, 1) == SCO_UNAVAILABLE);
        CHECK(c->set_fly_speed(self, 100.0f) == SCO_UNAVAILABLE);
        CHECK(c->set_infinite_ammo(self, 1) == SCO_UNAVAILABLE && c->set_infinite_ship_ammo(self, 1) == SCO_UNAVAILABLE);
        char msg[160] = {};
        uint32_t n = sizeof(msg);
        CHECK(c->last_error(self, msg, &n) == SCO_OK && std::strstr(msg, "game.creative.ship_ammo") && n == std::strlen(msg) + 1);
        n = 0;
        CHECK(c->last_error(self, nullptr, &n) == SCO_TOO_MANY && n == std::strlen(msg) + 1);
        uint32_t flags = 9;   // state() has no capability to name: the reason says so (a query: it's the newer error)
        CHECK(c->state(self, &flags) == SCO_UNAVAILABLE && flags == 0);
        n = sizeof(msg);
        CHECK(c->last_error(self, msg, &n) == SCO_OK && std::strstr(msg, "no game.creative capability is ready"));
        CHECK(!sco::caps::Has("game.creative.god_mode") && !sco::caps::Has("game.creative.fly") &&
              !sco::caps::Has("game.creative.ammo") && !sco::caps::Has("game.creative.ship_ammo"));
        sco_result off = SCO_OK, offErr = SCO_OK;
        char offMsg[64] = {};
        std::thread([&] { auto gateRead = readerGate.try_read(); CHECK(gateRead); if (!gateRead) return;
            off = c->set_god_mode(self, 1);
            uint32_t size = sizeof(offMsg);
            offErr = c->last_error(self, offMsg, &size);
        }).join();
        CHECK(off == SCO_WRONG_THREAD && offErr == SCO_OK && std::strcmp(offMsg, "game thread only") == 0);
    }
    // game.actors: published with them. No game image, so none of its capabilities is ready:
    // every function answers SCO_UNAVAILABLE and last_error says why; off the game thread
    // SCO_WRONG_THREAD; a handle NewPlugin didn't return is SCO_BAD_ARG.
    CHECK(api->query_service(SC_ACTORS_NAME, SC_ACTORS_VERSION_1_0, &table) == SCO_OK && table);
    CHECK(!sco::caps::Has("game.actors.local_player") && !sco::caps::Has("game.actors.spawn_npc") &&
          !sco::caps::Has("game.actors.despawn"));
    if (table) {
        const auto* ga = static_cast<const sc_actors_v1*>(table);
        sco_plugin* me = sco::host::NewPlugin("actorstest");
        CHECK(me && ga->size == sizeof(sc_actors_v1));
        uint64_t actor = 7, entity = 7, id = 7;
        const double pos[3] = { 1, 2, 3 };
        char buf[256];
        uint32_t n = sizeof(buf);
        CHECK(ga->local_player(&actor, &entity) == SCO_UNAVAILABLE && actor == 0 && entity == 0);
        CHECK(ga->last_error(nullptr, buf, &n) == SCO_OK && n == std::strlen(buf) + 1 &&
              std::strstr(buf, "local_player: game.actors.local_player isn't available on this game build"));
        CHECK(ga->local_player(nullptr, nullptr) == SCO_BAD_ARG);
        sco_result off = SCO_OK;
        std::thread([&] { auto gateRead = readerGate.try_read(); CHECK(gateRead); if (!gateRead) return; off = ga->local_player(&actor, &entity); }).join();
        CHECK(off == SCO_WRONG_THREAD);
        CHECK(ga->spawn_npc(me, "Human_NPC", 1, pos, &id) == SCO_UNAVAILABLE && id == 0);
        CHECK(ga->spawn_npc(reinterpret_cast<sco_plugin*>(&table), "Human_NPC", 1, pos, &id) == SCO_BAD_ARG);
        CHECK(ga->despawn(me, 1) == SCO_UNAVAILABLE);
        n = 4;   // the size handshake: too small answers the size needed
        CHECK(ga->last_error(me, buf, &n) == SCO_TOO_MANY && n > 4);
        const uint32_t need = n;
        CHECK(ga->last_error(me, buf, &n) == SCO_OK && n == need &&
              std::strstr(buf, "despawn: game.actors.despawn isn't available on this game build"));
        CHECK(ga->last_error(me, buf, nullptr) == SCO_BAD_ARG);
        // 1.1: health and state answer SCO_UNAVAILABLE with a reason (no game image), zeroing their outputs.
        float hp = 7, hpMax = 7;
        uint32_t st = 7;
        CHECK(ga->size > offsetof(sc_actors_v1, state));
        CHECK(!sco::caps::Has("game.actors.health") && !sco::caps::Has("game.actors.state"));
        CHECK(ga->health(1, &hp, &hpMax) == SCO_UNAVAILABLE && hp == 0 && hpMax == 0);
        CHECK(ga->last_error(nullptr, buf, &(n = sizeof(buf))) == SCO_OK &&
              std::strstr(buf, "health: game.actors.health isn't available on this game build"));
        CHECK(ga->state(1, &st) == SCO_UNAVAILABLE && st == 0);
        CHECK(ga->last_error(nullptr, buf, &(n = sizeof(buf))) == SCO_OK &&
              std::strstr(buf, "state: game.actors.state isn't available on this game build"));
        CHECK(ga->health(1, nullptr, nullptr) == SCO_BAD_ARG && ga->state(1, nullptr) == SCO_BAD_ARG);
        std::thread([&] { auto gateRead = readerGate.try_read(); CHECK(gateRead); if (!gateRead) return; off = ga->health(1, &hp, &hpMax); }).join();
        CHECK(off == SCO_WRONG_THREAD);
        CHECK(api->query_service(SC_ACTORS_NAME, SC_ACTORS_VERSION_1_1, &table) == SCO_OK);   // 1.0 callers asked for 1.0 and still work
        CHECK(api->query_service(SC_ACTORS_NAME, 0x00010002u, &table) == SCO_UNAVAILABLE);   // not published
        CHECK(me && sco::Release(me) == sco::Result::Ok);   // through the release hook: no NPCs to remove
        n = sizeof(buf);
        CHECK(ga->last_error(me, buf, &n) == SCO_BAD_ARG);   // released
    }
    // game.entities: published with them. No game image, so none of its capabilities is ready:
    // every function answers SCO_UNAVAILABLE and last_error says why; off the game thread
    // SCO_WRONG_THREAD; a handle NewPlugin didn't return is SCO_BAD_ARG; alive says 0.
    CHECK(api->query_service(SC_ENTITIES_NAME, SC_ENTITIES_VERSION_1_0, &table) == SCO_OK && table);
    for (const char* cap : { "game.entities.transform", "game.entities.spawn", "game.entities.class_of",
                             "game.entities.query_radius", "game.entities.watch" })
        CHECK(!sco::caps::Has(cap));
    if (table) {
        const auto* ge = static_cast<const sc_entities_v1*>(table);
        sco_plugin* me = sco::host::NewPlugin("entitiestest");
        CHECK(me && ge->size == sizeof(sc_entities_v1));
        const double pos[3] = { 1, 2, 3 };
        const double rot[4] = { 0, 0, 0, 1 };
        double outPos[3] = { 9, 9, 9 }, outRot[4] = { 9, 9, 9, 9 };
        uint64_t id = 7, zone = 7, watchId = 7;
        uint32_t found = 7, more = 7;
        char buf[256];
        uint32_t n = sizeof(buf);
        CHECK(ge->alive(1) == 0);
        CHECK(ge->class_of(1, buf, &n) == SCO_UNAVAILABLE && n == 0);
        n = sizeof(buf);
        CHECK(ge->last_error(nullptr, buf, &n) == SCO_OK && n == std::strlen(buf) + 1 &&
              std::strstr(buf, "class_of: game.entities.class_of isn't available on this game build"));
        CHECK(ge->get_transform(1, outPos, outRot, &zone) == SCO_UNAVAILABLE && zone == 0 && outPos[0] == 0 && outRot[3] == 0);
        CHECK(ge->get_transform(1, nullptr, nullptr, nullptr) == SCO_BAD_ARG);
        CHECK(ge->set_transform(me, 1, 2, pos, rot) == SCO_UNAVAILABLE);
        CHECK(ge->spawn(me, "Human_NPC", 1, pos, rot, &id) == SCO_UNAVAILABLE && id == 0);
        CHECK(ge->despawn(me, 1) == SCO_UNAVAILABLE);
        // 1.1: the table is published as 1.1 (a 1.0 query still gets it) and keep is in it; no spawn capability, so it is unavailable.
        const void* table11 = nullptr;
        CHECK(api->query_service(SC_ENTITIES_NAME, SC_ENTITIES_VERSION_1_1, &table11) == SCO_OK && table11 == table);
        CHECK(ge->size > offsetof(sc_entities_v1, keep));
        CHECK(ge->keep(me, 1) == SCO_UNAVAILABLE);
        n = sizeof(buf);
        CHECK(ge->last_error(me, buf, &n) == SCO_OK && std::strstr(buf, "keep: game.entities.spawn isn't available"));
        CHECK(ge->watch(me, SC_ENTITY_STREAMED_IN, "AEGS_*", [](void*, uint32_t, uint64_t, const char*) {}, nullptr, &watchId) ==
                  SCO_UNAVAILABLE &&
              watchId == 0);
        n = sizeof(buf);
        CHECK(ge->last_error(me, buf, &n) == SCO_OK && std::strstr(buf, "watch: game.entities.watch isn't available"));
        CHECK(ge->unwatch(me, 1) == SCO_NOT_FOUND);
        CHECK(ge->query_radius(1, pos, 100.0, nullptr, &id, 1, &found, &more) == SCO_UNAVAILABLE && found == 0 && more == 0);
        CHECK(ge->query_radius(1, nullptr, 100.0, nullptr, &id, 1, &found, &more) == SCO_BAD_ARG);
        sco_plugin* stranger = reinterpret_cast<sco_plugin*>(&table);
        CHECK(ge->spawn(stranger, "Human_NPC", 1, pos, rot, &id) == SCO_BAD_ARG);
        CHECK(ge->set_transform(stranger, 1, 2, pos, rot) == SCO_BAD_ARG && ge->despawn(stranger, 1) == SCO_BAD_ARG &&
              ge->keep(stranger, 1) == SCO_BAD_ARG);
        CHECK(ge->watch(stranger, SC_ENTITY_STREAMED_IN, "AEGS_*", nullptr, nullptr, &watchId) == SCO_BAD_ARG);
        sco_result off = SCO_OK;
        int offAlive = 1;
        std::thread([&] { auto gateRead = readerGate.try_read(); CHECK(gateRead); if (!gateRead) return;
            uint32_t size = sizeof(buf);
            off = ge->class_of(1, buf, &size);
            offAlive = ge->alive(1);
        }).join();
        CHECK(off == SCO_WRONG_THREAD && offAlive == 0);
        n = 4;   // the size handshake: too small answers the size needed
        CHECK(ge->last_error(me, buf, &n) == SCO_TOO_MANY && n > 4);
        const uint32_t need = n;
        CHECK(ge->last_error(me, buf, &n) == SCO_OK && n == need && std::strstr(buf, "unwatch: 1 is not a watch this plugin registered"));
        CHECK(ge->last_error(me, buf, nullptr) == SCO_BAD_ARG);
        CHECK(me && sco::Release(me) == sco::Result::Ok);   // through the release hook: nothing to remove
        n = sizeof(buf);
        CHECK(ge->last_error(me, buf, &n) == SCO_BAD_ARG);   // released
    }
    // game.world: published with them. No game image, so neither capability (nor any game.* event's)
    // is ready: both queries answer SCO_UNAVAILABLE and zero what they'd fill, last_error says why;
    // a bad argument is SCO_BAD_ARG before anything else; off the game thread SCO_WRONG_THREAD.
    CHECK(api->query_service(SC_WORLD_NAME, SC_WORLD_VERSION_1_0, &table) == SCO_OK && table);
    CHECK(!sco::caps::Has("game.world.raycast") && !sco::caps::Has("game.world.camera"));
    CHECK(!sco::caps::Has("game.events.player_spawned") && !sco::caps::Has("game.events.player_died") &&
          !sco::caps::Has("game.events.zone_changed") && !sco::caps::Has("game.events.vehicle_seat"));
    if (table) {
        const auto* w = static_cast<const sc_world_v1*>(table);
        const double from[3] = { 0, 0, 1 }, dir[3] = { 0, 0, -1 };
        char msg[256];
        uint32_t n = sizeof(msg);
        CHECK(w->size == sizeof(sc_world_v1));
        sc_world_hit hit;
        std::memset(&hit, 0x5A, sizeof(hit));
        hit.size = sizeof(hit);
        CHECK(w->raycast(9, from, dir, 100.0, &hit) == SCO_UNAVAILABLE && hit.size == sizeof(hit) && hit.flags == 0 &&
              hit.entity_id == 0 && hit.pos[2] == 0 && hit.normal[0] == 0 && hit.distance == 0);
        CHECK(w->last_error(nullptr, msg, &n) == SCO_OK && n == std::strlen(msg) + 1 &&
              std::strstr(msg, "raycast: game.world.raycast isn't available on this game build"));
        double pos[3] = { 7, 7, 7 }, rot[4] = { 7, 7, 7, 7 };
        uint64_t zone = 7;
        CHECK(w->camera(pos, rot, &zone) == SCO_UNAVAILABLE && pos[0] == 0 && rot[3] == 0 && zone == 0);
        n = sizeof(msg);
        CHECK(w->last_error(nullptr, msg, &n) == SCO_OK && std::strstr(msg, "camera: game.world.camera isn't available on this game build"));
        CHECK(w->raycast(9, from, dir, 100.0, nullptr) == SCO_BAD_ARG && w->camera(nullptr, nullptr, nullptr) == SCO_BAD_ARG);
        sc_world_hit small = {};
        small.size = 8;   // an older caller's struct: refused, nothing written
        CHECK(w->raycast(9, from, dir, 100.0, &small) == SCO_BAD_ARG && small.size == 8);
        sco_result off = SCO_OK;
        std::thread([&] { auto gateRead = readerGate.try_read(); CHECK(gateRead); if (!gateRead) return; off = w->raycast(9, from, dir, 100.0, &hit); }).join();
        CHECK(off == SCO_WRONG_THREAD && hit.entity_id == 0);
        n = sizeof(msg);
        CHECK(w->last_error(nullptr, msg, &n) == SCO_OK && std::strstr(msg, "raycast: game thread only"));
        n = 4;   // the size handshake
        CHECK(w->last_error(nullptr, msg, &n) == SCO_TOO_MANY && n > 4);
        CHECK(w->last_error(nullptr, msg, nullptr) == SCO_BAD_ARG);
        CHECK(w->last_error(reinterpret_cast<sco_plugin*>(&table), msg, &(n = sizeof(msg))) == SCO_BAD_ARG);   // not a plugin handle
    }
    barrier.beforeStop = true; // all fixture-created off-thread calls above joined
    sco::Result fixtureStop = sco::Result::Failed;
    const bool readersDrained = readerGate.drain_until(std::chrono::steady_clock::now() + std::chrono::seconds(10));
    if (readersDrained) fixtureStop = sco::app::Stop();
    CHECK(readersDrained && fixtureStop == sco::Result::Ok && barrier.calls == 2);
    if (!readersDrained || fixtureStop != sco::Result::Ok || barrier.calls != 2) std::abort();
    CHECK(!readerGate.try_read());
    CHECK(api->query_service(SC_ENTITIES_NAME, SC_ENTITIES_VERSION_1_0, &table) == SCO_NOT_FOUND);
    for (const char* cap : { "game.entities.transform", "game.entities.spawn", "game.entities.class_of",
                             "game.entities.query_radius", "game.entities.watch" })
        CHECK(!sco::caps::Has(cap));
    CHECK(api->query_service(SC_WORLD_NAME, SC_WORLD_VERSION_1_0, &table) == SCO_NOT_FOUND);
    CHECK(api->query_service(SC_VEHICLES_SERVICE_NAME, SC_VEHICLES_SERVICE_VERSION, &table) == SCO_NOT_FOUND);
    CHECK(api->query_service(SC_CREATIVE_SERVICE_NAME, SC_CREATIVE_SERVICE_VERSION, &table) == SCO_NOT_FOUND);
    CHECK(api->query_service("teleport.spatial", 0x00010000, &table) == SCO_NOT_FOUND);
    CHECK(api->query_service(SC_SPAWN_SERVICE_NAME, SC_SPAWN_SERVICE_VERSION, &table) == SCO_NOT_FOUND);
    CHECK(api->query_service(SC_ACTORS_NAME, SC_ACTORS_VERSION_1_0, &table) == SCO_NOT_FOUND);
    pf.gameServices = false;
    pf.gameReadersQuiesced = nullptr;
    pf.gameReadersContext = nullptr;
