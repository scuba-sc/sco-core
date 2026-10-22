// SPDX-License-Identifier: GPL-3.0-only
#include "scosdk/game/loadout.hpp"
#include "sco/host.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#define CHECK(x) do { if (!(x)) { std::printf("FAIL routes %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
void TestLoadoutRoutes(const sco_api* api, sco_plugin* self) {
    sco::sdk::game::Loadout wrapper;
    CHECK(wrapper.Open(api, self) == SCO_NOT_FOUND);
    sc_loadout_item item{SC_LOADOUT_SLOT_HEAD, 0, "synthetic_helmet"};
    sc_loadout_report report{};
    CHECK(wrapper.Apply(std::span(&item, 1), report) == SCO_UNAVAILABLE);
    uint64_t job = 0;
    sc_loadout_job_report state{};
    CHECK(wrapper.Submit(std::span(&item, 1), job) == SCO_UNAVAILABLE && job == 0);
    CHECK(wrapper.Status(1, state) == SCO_UNAVAILABLE);
    CHECK(wrapper.Cancel(1) == SCO_UNAVAILABLE);
    sc_loadout_v1 table{sizeof(table), 0, [](sco_plugin*, const sc_loadout_request*, sc_loadout_report*) { return SCO_OK; }};
    for (uint32_t size : {uint32_t(offsetof(sc_loadout_v1, apply) + 1), uint32_t(sizeof(table) - 1)}) {
        void* shortTable = std::malloc(size); CHECK(shortTable);
        std::memcpy(shortTable, &table, size); std::memcpy(shortTable, &size, sizeof(size));
        CHECK(sco::host::ProvideGameService(SC_LOADOUT_SERVICE_NAME, SC_LOADOUT_SERVICE_VERSION, shortTable) == sco::Result::Ok);
        CHECK(wrapper.Open(api, self) == SCO_OK);
        CHECK(wrapper.Apply(std::span(&item, 1), report) == SCO_UNAVAILABLE);
        CHECK(wrapper.Submit(std::span(&item, 1), job) == SCO_UNAVAILABLE);
        CHECK(wrapper.Status(1, state) == SCO_UNAVAILABLE);
        CHECK(wrapper.Cancel(1) == SCO_UNAVAILABLE);
        sco::host::WithdrawGameService(SC_LOADOUT_SERVICE_NAME); std::free(shortTable);
    }
    CHECK(sco::host::ProvideGameService(SC_LOADOUT_SERVICE_NAME, SC_LOADOUT_SERVICE_VERSION, &table) == sco::Result::Ok);
    CHECK(wrapper.Open(api, self) == SCO_OK && wrapper.Apply(std::span(&item, 1), report) == SCO_OK);
    table.apply = nullptr;
    CHECK(wrapper.Apply(std::span(&item, 1), report) == SCO_UNAVAILABLE);
    sco::host::WithdrawGameService(SC_LOADOUT_SERVICE_NAME);
    CHECK(wrapper.Open(api, self) == SCO_NOT_FOUND);
    CHECK(wrapper.Apply(std::span(&item, 1), report) == SCO_UNAVAILABLE);
    CHECK(wrapper.Open(api, self, SC_LOADOUT_VERSION_1_1) == SCO_NOT_FOUND);
    CHECK(sco::host::ProvideGameService(SC_LOADOUT_SERVICE_NAME, SC_LOADOUT_VERSION_1_0, &table) == sco::Result::Ok);
    CHECK(wrapper.Open(api, self, SC_LOADOUT_VERSION_1_1) == SCO_UNAVAILABLE);
    CHECK(wrapper.Open(api, self) == SCO_OK);
    sco::host::WithdrawGameService(SC_LOADOUT_SERVICE_NAME);
    table.apply = [](sco_plugin*, const sc_loadout_request*, sc_loadout_report*) { return SCO_OK; };
    CHECK(sco::host::ProvideGameService(SC_LOADOUT_SERVICE_NAME, SC_LOADOUT_VERSION_1_1, &table) == sco::Result::Ok);
    CHECK(wrapper.Open(api, self, SC_LOADOUT_VERSION_1_1) == SCO_OK);
    sc_loadout_item weapon{SC_LOADOUT_SLOT_PRIMARY_OPTIC, 0, "synthetic_optic"};
    CHECK(wrapper.Apply(std::span(&weapon, 1), report) == SCO_OK);
    CHECK(wrapper.Open(api, self) == SCO_OK);
    sco::host::WithdrawGameService(SC_LOADOUT_SERVICE_NAME);
    sc_loadout_v1_2 async{sizeof(async), 0, table.apply,
        [](sco_plugin*, const sc_loadout_request*, uint64_t* id) { *id = 9; return SCO_OK; },
        [](sco_plugin*, uint64_t id, sc_loadout_job_report* out) { out->completed = static_cast<uint32_t>(id); return SCO_OK; },
        [](sco_plugin*, uint64_t id) { return id == 9 ? SCO_OK : SCO_NOT_FOUND; }};
    for (uint32_t extent : {uint32_t(23), uint32_t(31), uint32_t(39), uint32_t(40)}) {
        void* memory = std::malloc(extent); CHECK(memory);
        std::memcpy(memory, &async, extent); std::memcpy(memory, &extent, sizeof(extent));
        CHECK(sco::host::ProvideGameService(SC_LOADOUT_SERVICE_NAME, SC_LOADOUT_VERSION_1_2, memory) == sco::Result::Ok);
        CHECK(wrapper.Open(api, self, SC_LOADOUT_VERSION_1_2) == SCO_OK);
        CHECK(wrapper.Submit(std::span(&item, 1), job) == (extent >= 24 ? SCO_OK : SCO_UNAVAILABLE));
        CHECK(wrapper.Status(9, state) == (extent >= 32 ? SCO_OK : SCO_UNAVAILABLE));
        CHECK(wrapper.Cancel(9) == (extent >= 40 ? SCO_OK : SCO_UNAVAILABLE));
        sco::host::WithdrawGameService(SC_LOADOUT_SERVICE_NAME); std::free(memory);
    }
    async.submit = nullptr; async.status = nullptr; async.cancel = nullptr;
    CHECK(sco::host::ProvideGameService(SC_LOADOUT_SERVICE_NAME, SC_LOADOUT_VERSION_1_2, &async) == sco::Result::Ok);
    CHECK(wrapper.Open(api, self, SC_LOADOUT_VERSION_1_2) == SCO_OK);
    CHECK(wrapper.Submit(std::span(&item, 1), job) == SCO_UNAVAILABLE);
    CHECK(wrapper.Status(9, state) == SCO_UNAVAILABLE && wrapper.Cancel(9) == SCO_UNAVAILABLE);
    sco::host::WithdrawGameService(SC_LOADOUT_SERVICE_NAME);
    CHECK(wrapper.Open(nullptr, self) == SCO_BAD_ARG);
    sco_api host = *api;
    host.query_service = nullptr;
    CHECK(wrapper.Open(&host, self) == SCO_UNAVAILABLE);
    const uint32_t shortSize = offsetof(sco_api, query_service) + sizeof(api->query_service) - 1;
    void* shortHost = std::malloc(shortSize); CHECK(shortHost);
    std::memcpy(shortHost, api, shortSize); std::memcpy(shortHost, &shortSize, sizeof(shortSize));
    CHECK(wrapper.Open(static_cast<const sco_api*>(shortHost), self) == SCO_UNAVAILABLE);
    std::free(shortHost);
    std::puts("loadout routes: full extent/null/failed reopen OK");
}
