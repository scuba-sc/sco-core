// SPDX-License-Identifier: GPL-3.0-only
#include "sco/host.h"
#include "sco/caps.h"
#include "sco/runtime.h"
#include "sc_loadout.h"
#include "sc_game_events.h"
#include "game/services/loadout.h"
#include "game/admission.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <initializer_list>
#include <thread>
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
void TestLoadoutRoutes(const sco_api*, sco_plugin*);
void TestLoadoutNative(const sco_api*, const char*);
int main(int argc, char** argv) {
    CHECK(argc == 2);
    sco::SetGameThread();
    const sco_api* api = sco::host::BuildApi({"synthetic loadout host"});
    sco_plugin* self = sco::host::NewPlugin("loadout_test");
    TestLoadoutRoutes(api, self);
    CHECK(sco::game::services::StartLoadout() == sco::Result::Ok);
    const void* table = nullptr;
    CHECK(api->query_service(SC_LOADOUT_SERVICE_NAME, SC_LOADOUT_SERVICE_VERSION, &table) == SCO_OK);
    const auto* service = static_cast<const sc_loadout_v1*>(table);
    sc_loadout_item item{SC_LOADOUT_SLOT_HEAD, 0, "synthetic_helmet"};
    sc_loadout_request request{sizeof(request), 1, sizeof(item), 0, &item};
    sc_loadout_report report{}; report.size = sizeof(report);
    CHECK(service->apply(self, &request, &report) == SCO_UNAVAILABLE);
    CHECK(report.count == 1 && report.applied == 0 && report.slots[0].status == SC_LOADOUT_UNAVAILABLE);
    CHECK(!api->has("game.loadout.apply"));
    // No production backend: game.loadout.weapons is off and weapon slots are UNAVAILABLE.
    CHECK(!api->has("game.loadout.weapons"));
    item.slot = SC_LOADOUT_SLOT_PRIMARY;
    CHECK(service->apply(self, &request, &report) == SCO_UNAVAILABLE && report.slots[0].status == SC_LOADOUT_UNAVAILABLE);
    item.slot = SC_LOADOUT_SLOT_HEAD;
    // Published as 1.2; the frozen synchronous prefix still answers older queries.
    const void* table11 = nullptr;
    CHECK(api->query_service(SC_LOADOUT_SERVICE_NAME, SC_LOADOUT_VERSION_1_1, &table11) == SCO_OK && table11 == table);
    const void* table12 = nullptr;
    CHECK(api->query_service(SC_LOADOUT_SERVICE_NAME, SC_LOADOUT_VERSION_1_2, &table12) == SCO_OK && table12 == table);
    sco::game::services::StopLoadout();
    CHECK(api->query_service(SC_LOADOUT_SERVICE_NAME, SC_LOADOUT_SERVICE_VERSION, &table) == SCO_NOT_FOUND);
    sc_loadout_item socketBatch[] = {{SC_LOADOUT_SLOT_HEAD, 0, "synthetic_helmet"}};
    sc_loadout_request socketRequest{sizeof(socketRequest), 1, sizeof(sc_loadout_item), 0, socketBatch};
    sc_loadout_report socketReport{}; socketReport.size = sizeof(socketReport);
    // Host socket sco_game_set_loadout_backend: real provider, admission is host::FreshAdmissionAllowed.
    namespace gh = sco::game::host;
    static unsigned socketEquips = 0;
    static gh::Admission evidence;
    sco_loadout_backend socket{sizeof(socket), 0, [](uint32_t, const char*) { ++socketEquips; return SC_LOADOUT_APPLIED; }};
    const auto socketApply = [&]() {
        const void* t = nullptr;
        CHECK(api->query_service(SC_LOADOUT_SERVICE_NAME, SC_LOADOUT_SERVICE_VERSION, &t) == SCO_OK);
        return static_cast<const sc_loadout_v1*>(t)->apply(self, &socketRequest, &socketReport);
    };
        // Null and malformed providers are refused before anything is published.
    sco_loadout_backend bad = socket;
    CHECK(sco_game_set_loadout_backend(nullptr) == SCO_BAD_ARG);
    bad.size = sizeof(bad) - 1; CHECK(sco_game_set_loadout_backend(&bad) == SCO_BAD_ARG);
    bad = socket; bad.equip = nullptr; CHECK(sco_game_set_loadout_backend(&bad) == SCO_BAD_ARG);
    CHECK(api->query_service(SC_LOADOUT_SERVICE_NAME, SC_LOADOUT_SERVICE_VERSION, &table) == SCO_NOT_FOUND);
    CHECK(!api->has("game.loadout.apply") && !api->has("game.loadout.weapons"));
    // Off the game thread nothing is installed.
    sco_result viaThread = SCO_OK;
    std::thread([&] { viaThread = sco_game_set_loadout_backend(&socket); }).join();
    CHECK(viaThread == SCO_WRONG_THREAD && api->query_service(SC_LOADOUT_SERVICE_NAME, SC_LOADOUT_SERVICE_VERSION, &table) == SCO_NOT_FOUND);
    // Default admission (host never composed one): the socket installs but every equip is refused.
    CHECK(sco_game_set_loadout_backend(&socket) == SCO_OK && api->has("game.loadout.apply"));
    CHECK(socketApply() == SCO_UNAVAILABLE && socketEquips == 0 && socketReport.slots[0].status == SC_LOADOUT_UNAVAILABLE);
    sco::game::services::StopLoadout();
    // Enabled admission with unknown evidence still refuses; positive evidence admits; revocation is live.
    gh::detail::SetFreshInspector([]() { return evidence; });
    gh::detail::SetFreshAdmissionEnabled(true);
    CHECK(sco_game_set_loadout_backend(&socket) == SCO_OK);
    CHECK(socketApply() == SCO_UNAVAILABLE && socketEquips == 0);
    evidence = {gh::Session::Offline, gh::Check::Clear, gh::Check::Clear, gh::Check::Clear};
    CHECK(socketApply() == SCO_OK && socketEquips == 1 && socketReport.applied == 1);
    CHECK(!api->has("game.loadout.weapons"));
    evidence.session = gh::Session::Online;
    CHECK(socketApply() == SCO_UNAVAILABLE && socketEquips == 1 && socketReport.slots[0].status == SC_LOADOUT_UNAVAILABLE);
    evidence.session = gh::Session::Offline;
    // A repeated install is refused and the first provider stays: no second callback, same state.
    static unsigned secondEquips = 0;
    sco_loadout_backend second{sizeof(second), 1, [](uint32_t, const char*) { ++secondEquips; return SC_LOADOUT_APPLIED; }};
    CHECK(sco_game_set_loadout_backend(&second) == SCO_UNAVAILABLE);
    CHECK(socketApply() == SCO_OK && socketEquips == 2 && secondEquips == 0 && !api->has("game.loadout.weapons"));
    sco::game::services::StopLoadout();
    // A late install after the production start (no backend) is refused, not reported as success.
    CHECK(sco::game::services::StartLoadout() == sco::Result::Ok);
    CHECK(sco_game_set_loadout_backend(&socket) == SCO_UNAVAILABLE);
    CHECK(socketApply() == SCO_UNAVAILABLE && socketEquips == 2 && !api->has("game.loadout.apply"));
    sco::game::services::StopLoadout();
    // The weapons attestation is carried through.
    CHECK(sco_game_set_loadout_backend(&second) == SCO_OK && api->has("game.loadout.weapons"));
    socketBatch[0] = {SC_LOADOUT_SLOT_PRIMARY, 0, "synthetic_rifle"};
    CHECK(socketApply() == SCO_OK && secondEquips == 1);
    sco::game::services::StopLoadout();
    gh::detail::SetFreshAdmissionEnabled(false);
    // Synthetic engine seam; real provider and host registry, not game acceptance.
    static unsigned calls = 0;
    sco::game::services::LoadoutBackend backend{};
    backend.verified = true;
    backend.offline = []() { return true; };
    backend.equip = [](uint32_t slot, const char*) {
        ++calls;
        return slot == SC_LOADOUT_SLOT_TORSO ? SC_LOADOUT_ITEM_MISSING : SC_LOADOUT_APPLIED;
    };
    // Failed publication must not advertise an enabled capability.
    const sc_loadout_v1 occupied{sizeof(occupied), 0, nullptr};
    CHECK(sco::host::ProvideGameService(SC_LOADOUT_SERVICE_NAME, SC_LOADOUT_SERVICE_VERSION, &occupied) == sco::Result::Ok);
    CHECK(sco::game::services::StartLoadout(&backend) == sco::Result::BadArg);
    CHECK(!api->has("game.loadout.apply"));
    sco::host::WithdrawGameService(SC_LOADOUT_SERVICE_NAME);
    backend.verified = false;
    CHECK(sco::game::services::StartLoadout(&backend) == sco::Result::Ok);
    sco::caps::Set("game.loadout.apply", true); // cannot override missing verification
    CHECK(service->apply(self, &request, &report) == SCO_UNAVAILABLE && calls == 0);
    sco::game::services::StopLoadout();
    backend.verified = true;
    CHECK(sco::game::services::StartLoadout(&backend) == sco::Result::Ok);
    sc_loadout_item batch[] = {{SC_LOADOUT_SLOT_HEAD, 0, "synthetic_helmet"},
                              {999, 0, "synthetic_unknown"},
                              {SC_LOADOUT_SLOT_TORSO, 0, "synthetic_missing"}};
    request.count = 3; request.items = batch;
    CHECK(service->apply(self, &request, &report) == SCO_FAILED);
    CHECK(report.count == 3 && report.applied == 1 && calls == 2);
    CHECK(report.slots[0].status == SC_LOADOUT_APPLIED);
    CHECK(report.slots[1].status == SC_LOADOUT_UNSUPPORTED);
    CHECK(report.slots[2].status == SC_LOADOUT_ITEM_MISSING);
    // game.loadout.respawn_completed: host producer seam, deferred dispatch on the real event bus.
    // Runs before the native loader test: its callout containment is torn down with that test.
    // Synthetic producer only; the native event is unverified (F-INTAKE-004, T-3), not run in game.
    {
        static const char* const kCap = "game.events.loadout_respawn_completed";
        static unsigned fired = 0;
        static uint32_t lastSize = 0, lastPad = 1;
        static bool admitted = true;
        const auto onEvent = [](const char* name, const void* data, void*) {
            CHECK(std::strcmp(name, SC_GAME_EVENT_LOADOUT_RESPAWN_COMPLETED) == 0);
            const auto* e = static_cast<const sc_game_loadout_respawn_completed*>(data);
            lastSize = e->size; lastPad = e->_pad; ++fired;
        };
        const auto tick = [] {
            uint32_t now = 0;
            CHECK(sco::Dispatch("tick", &now) == sco::Result::Ok);
        };
        CHECK(api->subscribe(self, SC_GAME_EVENT_LOADOUT_RESPAWN_COMPLETED, onEvent, nullptr) == SCO_OK);
        sco::game::services::LoadoutBackend ev{};
        ev.verified = true; ev.respawnCompleted = true;
        ev.offline = []() { return admitted; };
        ev.equip = [](uint32_t, const char*) { return SC_LOADOUT_APPLIED; };
        // Fail closed: nothing attested means no capability, no recording and no event.
        const auto expectOff = [&](const sco::game::services::LoadoutBackend* b) {
            sco::game::services::StopLoadout();
            CHECK(sco::game::services::StartLoadout(b) == sco::Result::Ok);
            CHECK(!api->has(kCap));
            sco::caps::Set(kCap, true); // cannot override a missing attestation
            CHECK(!sco::game::services::RecordRespawnLoadoutCompleted());
            tick();
            CHECK(fired == 0);
        };
        expectOff(nullptr);                                        // production composition
        auto variant = ev; variant.respawnCompleted = false; expectOff(&variant);  // verified, not attested
        variant = ev; variant.verified = false;              expectOff(&variant);  // attested, not verified
        variant = ev; variant.offline = nullptr;             expectOff(&variant);  // no admission check
        sco::game::services::StopLoadout();
        CHECK(api->unsubscribe(self, SC_GAME_EVENT_LOADOUT_RESPAWN_COMPLETED, onEvent) == SCO_OK);
        const size_t baseline = sco::SubscriptionCount();
        CHECK(api->subscribe(self, SC_GAME_EVENT_LOADOUT_RESPAWN_COMPLETED, onEvent, nullptr) == SCO_OK);
        CHECK(sco::game::services::StartLoadout(&ev) == sco::Result::Ok);
        CHECK(sco::SubscriptionCount() == baseline + 3 && api->has(kCap));
        // Recording defers: nothing runs until the tick, then exactly one sized, occurrence-only event.
        CHECK(sco::game::services::RecordRespawnLoadoutCompleted() && fired == 0);
        tick();
        CHECK(fired == 1 && lastSize == sizeof(sc_game_loadout_respawn_completed) && lastPad == 0);
        tick(); CHECK(fired == 1);                  // consumed, not replayed
        CHECK(sco::game::services::RecordRespawnLoadoutCompleted());
        CHECK(sco::game::services::RecordRespawnLoadoutCompleted());
        tick(); CHECK(fired == 2);                  // coalesced within one tick
        // The producer may be a native hook on another thread; dispatch is still the game thread's.
        std::thread hook([] { CHECK(sco::game::services::RecordRespawnLoadoutCompleted()); });
        hook.join();
        CHECK(fired == 2);
        tick(); CHECK(fired == 3);
        // Live admission is re-checked at dispatch; a refused occurrence is dropped, never replayed.
        CHECK(sco::game::services::RecordRespawnLoadoutCompleted());
        admitted = false; tick(); admitted = true; tick();
        CHECK(fired == 3);
        // A revoked capability stops recording and dispatch; re-enabling does not replay.
        sco::caps::Set(kCap, false, "synthetic revocation");
        CHECK(!sco::game::services::RecordRespawnLoadoutCompleted());
        tick(); CHECK(fired == 3);
        sco::caps::Set(kCap, true);
        CHECK(sco::game::services::RecordRespawnLoadoutCompleted());
        tick(); CHECK(fired == 4);
        // Unsubscribe is honoured at once; the host stays quiet for that plugin.
        CHECK(api->unsubscribe(self, SC_GAME_EVENT_LOADOUT_RESPAWN_COMPLETED, onEvent) == SCO_OK);
        CHECK(api->unsubscribe(self, SC_GAME_EVENT_LOADOUT_RESPAWN_COMPLETED, onEvent) != SCO_OK);
        CHECK(sco::game::services::RecordRespawnLoadoutCompleted());
        tick(); CHECK(fired == 4);
        CHECK(api->subscribe(self, SC_GAME_EVENT_LOADOUT_RESPAWN_COMPLETED, onEvent, nullptr) == SCO_OK);
        // Stop drops a pending occurrence, withdraws the tick subscription and turns the capability off.
        CHECK(sco::game::services::RecordRespawnLoadoutCompleted());
        sco::game::services::StopLoadout();
        CHECK(!api->has(kCap) && !sco::game::services::RecordRespawnLoadoutCompleted());
        CHECK(sco::SubscriptionCount() == baseline + 1);
        CHECK(sco::game::services::StartLoadout(&ev) == sco::Result::Ok);
        tick(); CHECK(fired == 4);                  // no stale replay after restart
        sco::game::services::StopLoadout();
        CHECK(sco::SubscriptionCount() == baseline + 1 && !api->has(kCap));
        CHECK(api->unsubscribe(self, SC_GAME_EVENT_LOADOUT_RESPAWN_COMPLETED, onEvent) == SCO_OK);
        CHECK(sco::SubscriptionCount() == baseline);
    }
    // Back to the verified body backend the native test expects.
    sco::game::services::StopLoadout();
    CHECK(sco::game::services::StartLoadout(&backend) == sco::Result::Ok);
    TestLoadoutNative(api, argv[1]);
    static bool offline = true;
    backend.offline = []() { return offline; };
    backend.equip = [](uint32_t, const char*) { ++calls; offline = false; return SC_LOADOUT_APPLIED; };
    sco::game::services::StopLoadout();
    CHECK(sco::game::services::StartLoadout(&backend) == sco::Result::Ok);
    const unsigned beforeAdmission = calls;
    request.count = 2; request.items = batch;
    batch[1] = {SC_LOADOUT_SLOT_TORSO, 0, "synthetic_torso"};
    CHECK(service->apply(self, &request, &report) == SCO_FAILED && calls == beforeAdmission + 1);
    CHECK(report.applied == 1 && report.slots[1].status == SC_LOADOUT_UNAVAILABLE);
    offline = true;
    backend.equip = [](uint32_t, const char*) { ++calls; return SC_LOADOUT_APPLIED; };
    sco::game::services::StopLoadout();
    CHECK(sco::game::services::StartLoadout(&backend) == sco::Result::Ok);
    static const sc_loadout_v1* nestedService = service;
    static sco_plugin* nestedSelf = self;
    backend.equip = [](uint32_t, const char*) {
        static bool nesting = false;
        if (nesting) return SC_LOADOUT_APPLIED;
        nesting = true;
        sc_loadout_item nestedItem{SC_LOADOUT_SLOT_HEAD, 0, "synthetic_nested"};
        sc_loadout_request nestedRequest{sizeof(nestedRequest), 1, sizeof(nestedItem), 0, &nestedItem};
        sc_loadout_report nestedReport{}; nestedReport.size = sizeof(nestedReport);
        CHECK(nestedService->apply(nestedSelf, &nestedRequest, &nestedReport) == SCO_UNAVAILABLE);
        CHECK(sco::game::services::StartLoadout() == sco::Result::Unavailable);
        sco::game::services::StopLoadout(); // ignored while apply owns the callback
        nesting = false; return SC_LOADOUT_APPLIED;
    };
    sco::game::services::StopLoadout();
    CHECK(sco::game::services::StartLoadout(&backend) == sco::Result::Ok);
    request.count = 1;
    CHECK(service->apply(self, &request, &report) == SCO_OK);
    backend.equip = [](uint32_t, const char*) { ++calls; return SC_LOADOUT_APPLIED; };
    sco::game::services::StopLoadout();
    CHECK(sco::game::services::StartLoadout(&backend) == sco::Result::Ok);
    const unsigned before = calls;
    request.count = 2;
    batch[1] = batch[0];
    report.applied = 77;
    CHECK(service->apply(self, &request, &report) == SCO_BAD_ARG && calls == before && report.applied == 77);
    batch[1] = {SC_LOADOUT_SLOT_TORSO, 0, "<xml/>"};
    CHECK(service->apply(self, &request, &report) == SCO_BAD_ARG && calls == before);
    for (char& c : batch[1].item_class) c = 'a';
    CHECK(service->apply(self, &request, &report) == SCO_BAD_ARG && calls == before);
    batch[1] = {SC_LOADOUT_SLOT_TORSO, 1, "synthetic_torso"};
    CHECK(service->apply(self, &request, &report) == SCO_BAD_ARG && calls == before);
    batch[1] = {SC_LOADOUT_SLOT_TORSO, 0, "synthetic_torso"};
    request.count = 1;
    backend.offline = []() { return offline; };
    // Refuse capability revocation before any engine callback.
    sco::caps::Set("game.loadout.apply", false, "synthetic revocation");
    CHECK(service->apply(self, &request, &report) == SCO_UNAVAILABLE && calls == before);
    sco::caps::Set("game.loadout.apply", true);
    // Boundaries and handles are enforced by the actual game-pack provider.
    const auto validRequest = request;
    request.count = 17;
    CHECK(service->apply(self, &request, &report) == SCO_TOO_MANY && calls == before);
    request = validRequest; request.count = 0;
    CHECK(service->apply(self, &request, &report) == SCO_BAD_ARG && calls == before);
    request = validRequest; request.size = sizeof(request) - 1;
    CHECK(service->apply(self, &request, &report) == SCO_BAD_ARG && calls == before);
    request = validRequest; request.item_stride = sizeof(item) - 1;
    CHECK(service->apply(self, &request, &report) == SCO_BAD_ARG && calls == before);
    request = validRequest; report.size = sizeof(report) - 1;
    CHECK(service->apply(self, &request, &report) == SCO_BAD_ARG && calls == before);
    report.size = sizeof(report);
    CHECK(service->apply(nullptr, &request, &report) == SCO_BAD_ARG && calls == before);
    sco_plugin* released = sco::host::NewPlugin("released_loadout"); sco::Release(released);
    CHECK(service->apply(released, &request, &report) == SCO_BAD_ARG && calls == before);
    std::thread worker([&] {
        CHECK(service->apply(self, &request, &report) == SCO_WRONG_THREAD);
        CHECK(sco::game::services::StartLoadout() == sco::Result::WrongThread);
    }); worker.join();
    CHECK(calls == before);
    // game.loadout 1.1: slots 8-21 need the backend flag AND capability game.loadout.weapons.
    {
        static unsigned wCalls = 0;
        static uint32_t wSeq[32];
        static sc_loadout_status wReply = SC_LOADOUT_APPLIED;
        backend.equip = [](uint32_t slot, const char*) {
            if (wCalls < 32) wSeq[wCalls] = slot;
            ++wCalls;
            return wReply;
        };
        sc_loadout_item wItems[SC_LOADOUT_MAX_ITEMS]{};
        sc_loadout_report wReport{};
        const auto applySlots = [&](std::initializer_list<uint32_t> slots) {
            uint32_t n = 0;
            for (uint32_t s : slots) { wItems[n] = {s, 0, "synthetic_item"}; ++n; }
            sc_loadout_request req{sizeof(req), n, sizeof(sc_loadout_item), 0, wItems};
            wReport = {}; wReport.size = sizeof(wReport);
            wCalls = 0;
            return service->apply(self, &req, &wReport);
        };
        // Unverified backend: the flag alone and a capability override both stay refused.
        backend.verified = false; backend.weapons = true;
        sco::game::services::StopLoadout();
        CHECK(sco::game::services::StartLoadout(&backend) == sco::Result::Ok);
        CHECK(!api->has("game.loadout.weapons"));
        sco::caps::Set("game.loadout.weapons", true);
        CHECK(applySlots({SC_LOADOUT_SLOT_PRIMARY}) == SCO_UNAVAILABLE && wCalls == 0);
        CHECK(wReport.slots[0].status == SC_LOADOUT_UNAVAILABLE);
        // Verified backend without the weapons attestation: 8-21 UNSUPPORTED, body slots unaffected.
        backend.verified = true; backend.weapons = false;
        sco::game::services::StopLoadout();
        CHECK(sco::game::services::StartLoadout(&backend) == sco::Result::Ok);
        CHECK(api->has("game.loadout.apply") && !api->has("game.loadout.weapons"));
        sco::caps::Set("game.loadout.weapons", true); // cannot override a missing attestation
        for (uint32_t s = SC_LOADOUT_SLOT_PRIMARY; s <= SC_LOADOUT_SLOT_SIDEARM_TOOL_BATTERY; ++s) {
            CHECK(applySlots({s}) == SCO_FAILED && wCalls == 0 && wReport.applied == 0);
            CHECK(wReport.slots[0].slot == s && wReport.slots[0].status == SC_LOADOUT_UNSUPPORTED);
        }
        CHECK(applySlots({SC_LOADOUT_SLOT_HEAD, SC_LOADOUT_SLOT_PRIMARY}) == SCO_FAILED && wCalls == 1);
        CHECK(wReport.applied == 1 && wReport.slots[0].status == SC_LOADOUT_APPLIED &&
              wReport.slots[1].status == SC_LOADOUT_UNSUPPORTED);
        // Attested backend: capability on by default for it, 0 and 22+ still UNSUPPORTED with no engine call.
        backend.weapons = true;
        sco::game::services::StopLoadout();
        CHECK(sco::game::services::StartLoadout(&backend) == sco::Result::Ok);
        CHECK(api->has("game.loadout.weapons"));
        CHECK(applySlots({0, 22, 23, 0x7fffffffu}) == SCO_FAILED && wCalls == 0 && wReport.applied == 0);
        for (uint32_t i = 0; i < 4; ++i) CHECK(wReport.slots[i].status == SC_LOADOUT_UNSUPPORTED);
        // Every weapon slot 8-21 reaches the engine, in input order.
        CHECK(applySlots({8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21}) == SCO_OK);
        CHECK(wReport.count == 14 && wReport.applied == 14 && wCalls == 14);
        for (uint32_t i = 0; i < 14; ++i)
            CHECK(wSeq[i] == 8 + i && wReport.slots[i].slot == 8 + i && wReport.slots[i].status == SC_LOADOUT_APPLIED);
        // Mixed body and weapon batch: forwarded in input order (weapon before its attachment).
        CHECK(applySlots({SC_LOADOUT_SLOT_TORSO, SC_LOADOUT_SLOT_PRIMARY, SC_LOADOUT_SLOT_PRIMARY_OPTIC,
                          SC_LOADOUT_SLOT_SIDEARM, SC_LOADOUT_SLOT_SIDEARM_BARREL}) == SCO_OK && wCalls == 5);
        const uint32_t expectedOrder[5] = {2, 8, 10, 9, 14};
        for (uint32_t i = 0; i < 5; ++i) CHECK(wSeq[i] == expectedOrder[i]);
        CHECK(applySlots({SC_LOADOUT_SLOT_PRIMARY, SC_LOADOUT_SLOT_PRIMARY}) == SCO_BAD_ARG && wCalls == 0);
        // Capability revoked while body apply stays on: 8-21 UNAVAILABLE with no engine call.
        sco::caps::Set("game.loadout.weapons", false, "synthetic revocation");
        CHECK(applySlots({SC_LOADOUT_SLOT_TORSO, SC_LOADOUT_SLOT_PRIMARY, SC_LOADOUT_SLOT_PRIMARY_OPTIC, 22}) == SCO_FAILED);
        CHECK(wCalls == 1 && wSeq[0] == SC_LOADOUT_SLOT_TORSO && wReport.applied == 1);
        CHECK(wReport.slots[0].status == SC_LOADOUT_APPLIED && wReport.slots[1].status == SC_LOADOUT_UNAVAILABLE &&
              wReport.slots[2].status == SC_LOADOUT_UNAVAILABLE && wReport.slots[3].status == SC_LOADOUT_UNSUPPORTED);
        CHECK(applySlots({SC_LOADOUT_SLOT_SIDEARM_TOOL_BATTERY}) == SCO_FAILED && wCalls == 0);
        CHECK(wReport.slots[0].status == SC_LOADOUT_UNAVAILABLE && api->has("game.loadout.apply"));
        sco::caps::Set("game.loadout.weapons", true);
        // Backend-reported refusals (empty parent port, class missing, engine refusal) are forwarded and non-fatal.
        for (sc_loadout_status st : {SC_LOADOUT_UNSUPPORTED, SC_LOADOUT_ITEM_MISSING, SC_LOADOUT_FAILED}) {
            wReply = st;
            CHECK(applySlots({SC_LOADOUT_SLOT_PRIMARY, SC_LOADOUT_SLOT_PRIMARY_OPTIC}) == SCO_FAILED && wCalls == 2);
            CHECK(wReport.applied == 0 && wReport.slots[0].status == st && wReport.slots[1].status == st);
            CHECK(api->has("game.loadout.apply") && api->has("game.loadout.weapons"));
        }
        wReply = SC_LOADOUT_APPLIED;
        CHECK(applySlots({SC_LOADOUT_SLOT_SIDEARM_TOOL_BATTERY}) == SCO_OK && wCalls == 1);
        // Offline admission still gates weapon slots.
        offline = false;
        CHECK(applySlots({SC_LOADOUT_SLOT_PRIMARY}) == SCO_UNAVAILABLE && wCalls == 0);
        CHECK(wReport.slots[0].status == SC_LOADOUT_UNAVAILABLE);
        offline = true;
        sco::game::services::StopLoadout();
        CHECK(!api->has("game.loadout.weapons"));
    }
    backend.equip = [](uint32_t, const char*) -> sc_loadout_status { ++calls; throw 1; };
    backend.weapons = true;
    sco::game::services::StopLoadout();
    CHECK(sco::game::services::StartLoadout(&backend) == sco::Result::Ok);
    CHECK(api->has("game.loadout.weapons"));
    offline = false;
    CHECK(service->apply(self, &request, &report) == SCO_UNAVAILABLE && calls == before);
    offline = true;
    CHECK(service->apply(self, &request, &report) == SCO_FAILED);
    CHECK(report.applied == 0 && report.slots[0].status == SC_LOADOUT_FAILED && calls == before + 1);
    CHECK(!api->has("game.loadout.apply") && !api->has("game.loadout.weapons")); // Fault() clears both
    CHECK(service->apply(self, &request, &report) == SCO_UNAVAILABLE && calls == before + 1);
    sco::game::services::StopLoadout();

    std::puts("loadout: publication, partial slots, validation, offline and terminal fault OK");
}
