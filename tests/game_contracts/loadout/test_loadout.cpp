// SPDX-License-Identifier: GPL-3.0-only
#include "sco/host.h"
#include "sco/caps.h"
#include "sc_loadout.h"
#include "game/services/loadout.h"
#include <cstdio>
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
    // Published as 1.1; a 1.0 query is still answered and a newer minor is not.
    const void* table11 = nullptr;
    CHECK(api->query_service(SC_LOADOUT_SERVICE_NAME, SC_LOADOUT_VERSION_1_1, &table11) == SCO_OK && table11 == table);
    const void* table12 = nullptr;
    CHECK(api->query_service(SC_LOADOUT_SERVICE_NAME, SC_LOADOUT_VERSION_1_2, &table12) == SCO_OK && table12 == table);
    CHECK(api->query_service(SC_LOADOUT_SERVICE_NAME, 0x00010003u, &table12) != SCO_OK && !table12);
    sco::game::services::StopLoadout();
    CHECK(api->query_service(SC_LOADOUT_SERVICE_NAME, SC_LOADOUT_SERVICE_VERSION, &table) == SCO_NOT_FOUND);
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
    {
        using namespace sco::game::services;
        static uint64_t session = 42;
        static bool admitted = true, complete = false;
        static unsigned begins = 0, polls = 0, discards = 0;
        static char copied[SC_LOADOUT_ITEM_NAME_BYTES]{};
        LoadoutAsyncBackend async{};
        async.verified = true;
        async.admit = [](uint64_t* out) { *out = session; return admitted; };
        async.begin = [](uint32_t, const char* name, uint64_t* token, sc_loadout_status*) {
            ++begins;
            for (unsigned i = 0; i < SC_LOADOUT_ITEM_NAME_BYTES; ++i) copied[i] = name[i];
            *token = 7; return true;
        };
        async.poll = [](uint64_t token, bool* done, sc_loadout_status* result) {
            CHECK(token == 7); ++polls; *done = complete; *result = SC_LOADOUT_APPLIED; return true;
        };
        async.discard = [](uint64_t token) { CHECK(token == 7); ++discards; };
        CHECK(StartLoadout(nullptr, &async) == sco::Result::Ok);
        const void* out = nullptr;
        CHECK(api->query_service(SC_LOADOUT_SERVICE_NAME, SC_LOADOUT_VERSION_1_2, &out) == SCO_OK);
        const auto* jobs = static_cast<const sc_loadout_v1_2*>(out);
        sc_loadout_item queued[]{ {SC_LOADOUT_SLOT_PRIMARY, 0, "synthetic_async"}, {999, 0, "synthetic_unknown"} };
        sc_loadout_request req{sizeof(req), 2, sizeof(sc_loadout_item), 0, queued};
        uint64_t job = 0, otherId = 0;
        CHECK(jobs->submit(self, &req, &job) == SCO_OK && job && begins == 0);
        CHECK(jobs->submit(self, &req, &otherId) == SCO_TOO_MANY);
        queued[0].item_class[0] = 'x'; // only the provider's copied request may reach begin
        TickLoadout(100);
        CHECK(begins == 1 && copied[0] == 's');
        sc_loadout_job_report state{}; state.size = sizeof(state);
        CHECK(jobs->status(self, job, &state) == SCO_OK && state.state == SC_LOADOUT_JOB_PENDING && !state.completed);
        sco_plugin* other = sco::host::NewPlugin("async_other");
        CHECK(jobs->status(other, job, &state) == SCO_NOT_FOUND);
        CHECK(jobs->cancel(other, job) == SCO_NOT_FOUND);
        TickLoadout(101);
        CHECK(polls == 1 && discards == 0);
        complete = true;
        TickLoadout(102);
        CHECK(discards == 1);
        TickLoadout(103);
        CHECK(jobs->status(self, job, &state) == SCO_OK && state.state == SC_LOADOUT_JOB_COMPLETE);
        CHECK(state.completed == 2 && state.report.applied == 1 && state.report.slots[1].status == SC_LOADOUT_UNSUPPORTED);
        CHECK(jobs->cancel(self, job) == SCO_OK && jobs->status(self, job, &state) == SCO_NOT_FOUND);
        CHECK(jobs->submit(self, &req, &job) == SCO_OK);
        complete = false;
        TickLoadout(104);
        ++session;
        TickLoadout(105);
        CHECK(jobs->status(self, job, &state) == SCO_OK && state.state == SC_LOADOUT_JOB_FAILED && !state.report.applied);
        CHECK(discards == 2 && jobs->cancel(self, job) == SCO_OK);
        CHECK(jobs->submit(self, &req, &job) == SCO_OK);
        TickLoadout(106);
        TickLoadout(105 + SC_LOADOUT_TIMEOUT_MS);
        CHECK(jobs->status(self, job, &state) == SCO_OK && state.state == SC_LOADOUT_JOB_FAILED);
        CHECK(jobs->cancel(self, job) == SCO_OK);
        CHECK(jobs->submit(self, &req, &job) == SCO_OK);
        TickLoadout(106 + SC_LOADOUT_TIMEOUT_MS);
        ReleaseLoadoutOwner(self);
        CHECK(jobs->status(self, job, &state) == SCO_NOT_FOUND);
        admitted = false;
        CHECK(jobs->submit(self, &req, &job) == SCO_UNAVAILABLE);
        admitted = true;
        // Every retained job consumes capacity, including terminal reports.
        sco_plugin* owners[SC_LOADOUT_MAX_JOBS]{};
        uint64_t ids[SC_LOADOUT_MAX_JOBS]{};
        for (uint32_t i = 0; i < SC_LOADOUT_MAX_JOBS; ++i) {
            char pluginId[32]{};
            std::snprintf(pluginId, sizeof(pluginId), "capacity_loadout_%u", i);
            owners[i] = sco::host::NewPlugin(pluginId);
            CHECK(jobs->submit(owners[i], &req, &ids[i]) == SCO_OK && ids[i]);
        }
        CHECK(jobs->submit(other, &req, &otherId) == SCO_TOO_MANY);
        for (uint32_t i = 0; i < SC_LOADOUT_MAX_JOBS; ++i)
            CHECK(jobs->cancel(owners[i], ids[i]) == SCO_OK);
        state.size = sizeof(state) - 1;
        CHECK(jobs->status(self, 1, &state) == SCO_BAD_ARG);
        std::thread asyncWorker([&] {
            CHECK(jobs->submit(self, &req, &job) == SCO_WRONG_THREAD);
            CHECK(jobs->cancel(self, 1) == SCO_WRONG_THREAD);
            CHECK(jobs->status(self, 1, &state) == SCO_WRONG_THREAD);
        });
        asyncWorker.join();
        StopLoadout();
        CHECK(!api->has("game.loadout.async"));
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
