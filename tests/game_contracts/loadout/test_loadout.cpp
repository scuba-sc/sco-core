// SPDX-License-Identifier: GPL-3.0-only
#include "sco/host.h"
#include "sco/caps.h"
#include "sc_loadout.h"
#include "game/services/loadout.h"
#include <cstdio>
#include <cstdlib>
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
    backend.equip = [](uint32_t, const char*) { ++calls; throw 1; return SC_LOADOUT_APPLIED; };
    sco::game::services::StopLoadout();
    CHECK(sco::game::services::StartLoadout(&backend) == sco::Result::Ok);
    offline = false;
    CHECK(service->apply(self, &request, &report) == SCO_UNAVAILABLE && calls == before);
    offline = true;
    CHECK(service->apply(self, &request, &report) == SCO_FAILED);
    CHECK(report.applied == 0 && report.slots[0].status == SC_LOADOUT_FAILED && calls == before + 1);
    CHECK(!api->has("game.loadout.apply"));
    CHECK(service->apply(self, &request, &report) == SCO_UNAVAILABLE && calls == before + 1);
    sco::game::services::StopLoadout();
    std::puts("loadout: publication, partial slots, validation, offline and terminal fault OK");
}
