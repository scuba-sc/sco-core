// SPDX-License-Identifier: GPL-3.0-only
#include "sco/host.h"
#include "sco/plugins.h"
#include "sc_loadout.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cstddef>
#define CHECK(x) do { if (!(x)) { std::printf("FAIL native %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
void TestLoadoutNative(const sco_api* api, const char* root) {
    namespace P = sco::plugins;
    P::Options opts; opts.enabled = true;
    auto plugins = P::Discover(root, opts);
    CHECK(plugins.size() == 1);
    auto& p = plugins[0];
    P::ContainCallouts(&plugins);
    CHECK(P::LoadNative(p, api, sco::host::NewPlugin(p.folder.c_str()), opts));
    CHECK(p.module && p.state == P::State::Loaded);
    struct Reply { sco_result result; char text[128]; unsigned calls; } out{};
    const auto done = [](sco_result r, const char* text, void* ctx) {
        auto& reply = *static_cast<Reply*>(ctx); reply.result = r; ++reply.calls;
        std::snprintf(reply.text, sizeof(reply.text), "%s", text ? text : "");
    };
    CHECK(api->invoke(p.self, "loadout_native.apply", nullptr, 0, done, &out) == SCO_FAILED);
    CHECK(out.result == SCO_FAILED && out.calls == 1 && std::strcmp(out.text, "count=3 applied=1 slots=0,1,2") == 0);
    // Exercise C's complete-extent/null guards through the actual native module.
    const void* realTable = nullptr;
    CHECK(api->query_service(SC_LOADOUT_SERVICE_NAME, SC_LOADOUT_SERVICE_VERSION, &realTable) == SCO_OK);
    auto invoke = [&](sco_result expected) {
        out = {}; CHECK(api->invoke(p.self, "loadout_native.apply", nullptr, 0, done, &out) == expected);
        CHECK(out.calls == 1 && out.result == expected);
    };
    sco::host::WithdrawGameService(SC_LOADOUT_SERVICE_NAME);
    invoke(SCO_NOT_FOUND);
    for (uint32_t extent : {uint32_t(offsetof(sc_loadout_v1, apply) + 1), uint32_t(sizeof(sc_loadout_v1) - 1)}) {
        void* shortTable = std::malloc(extent); CHECK(shortTable);
        std::memcpy(shortTable, realTable, extent); std::memcpy(shortTable, &extent, sizeof(extent));
        CHECK(sco::host::ProvideGameService(SC_LOADOUT_SERVICE_NAME, SC_LOADOUT_SERVICE_VERSION, shortTable) == sco::Result::Ok);
        invoke(SCO_UNAVAILABLE);
        sco::host::WithdrawGameService(SC_LOADOUT_SERVICE_NAME); std::free(shortTable);
    }
    sc_loadout_v1 nullTable{sizeof(nullTable), 0, nullptr};
    CHECK(sco::host::ProvideGameService(SC_LOADOUT_SERVICE_NAME, SC_LOADOUT_SERVICE_VERSION, &nullTable) == sco::Result::Ok);
    invoke(SCO_UNAVAILABLE);
    sco::host::WithdrawGameService(SC_LOADOUT_SERVICE_NAME);
    CHECK(sco::host::ProvideGameService(SC_LOADOUT_SERVICE_NAME, SC_LOADOUT_SERVICE_VERSION, realTable) == sco::Result::Ok);
    P::UnloadAll(plugins); P::UnloadAll(plugins);
    CHECK(p.state == P::State::Unloaded);
    std::puts("loadout native: real loader/registry/provider multi-slot partial workflow OK");
}
