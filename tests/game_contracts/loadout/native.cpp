// SPDX-License-Identifier: GPL-3.0-only
#include "sco/host.h"
#include "sco/plugins.h"
#include "sco/caps.h"
#include "sco/runtime.h"
#include "sc_loadout.h"
#include "game/services/loadout.h"
#include <initializer_list>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cstddef>
#define CHECK(x) do { if (!(x)) { std::printf("FAIL native %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
namespace {
unsigned nativeCalls = 0;
sc_loadout_status nativeReply = SC_LOADOUT_APPLIED;
sc_loadout_status NativeEquip(uint32_t, const char*) { ++nativeCalls; return nativeReply; }
bool NativeOffline() { return true; }
sco_result NativeApply(const sc_loadout_v1* svc, sco_plugin* self, std::initializer_list<uint32_t> slots,
                       sc_loadout_report* report) {
    sc_loadout_item items[SC_LOADOUT_MAX_ITEMS]{};
    uint32_t n = 0;
    for (uint32_t s : slots) { items[n] = {s, 0, "synthetic_item"}; ++n; }
    const sc_loadout_request req{sizeof(req), n, sizeof(sc_loadout_item), 0, items};
    *report = {}; report->size = sizeof(*report); nativeCalls = 0;
    return svc->apply(self, &req, report);
}
// The 1.1 weapon-slot status table, through the real registry and a live plugin handle.
void WeaponSlotRows(const sco_api* api, sco_plugin* self) {
    namespace S = sco::game::services;
    S::LoadoutBackend b{};
    b.verified = true; b.offline = NativeOffline; b.equip = NativeEquip;
    S::StopLoadout();
    CHECK(S::StartLoadout(&b) == sco::Result::Ok);
    const void* t = nullptr;
    CHECK(api->query_service(SC_LOADOUT_SERVICE_NAME, SC_LOADOUT_VERSION_1_1, &t) == SCO_OK);
    const auto* svc = static_cast<const sc_loadout_v1*>(t);
    sc_loadout_report r{};
    // Backend without the weapons attestation: 8-21 UNSUPPORTED (also past 21), body slot applied.
    CHECK(api->has("game.loadout.apply") && !api->has("game.loadout.weapons"));
    CHECK(NativeApply(svc, self, {SC_LOADOUT_SLOT_HEAD, SC_LOADOUT_SLOT_PRIMARY, SC_LOADOUT_SLOT_PRIMARY_OPTIC,
                                  SC_LOADOUT_SLOT_SIDEARM_TOOL_BATTERY, 22}, &r) == SCO_FAILED);
    CHECK(r.count == 5 && r.applied == 1 && nativeCalls == 1 && r.slots[0].status == SC_LOADOUT_APPLIED);
    for (uint32_t i = 1; i < 5; ++i) CHECK(r.slots[i].status == SC_LOADOUT_UNSUPPORTED);
    // Attested backend: capability on, slot 0 and 22+ still UNSUPPORTED with no engine call.
    b.weapons = true;
    S::StopLoadout();
    CHECK(S::StartLoadout(&b) == sco::Result::Ok);
    CHECK(api->has("game.loadout.weapons"));
    CHECK(NativeApply(svc, self, {0, 22}, &r) == SCO_FAILED && nativeCalls == 0);
    CHECK(r.slots[0].status == SC_LOADOUT_UNSUPPORTED && r.slots[1].status == SC_LOADOUT_UNSUPPORTED);
    CHECK(NativeApply(svc, self, {8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21}, &r) == SCO_OK);
    CHECK(r.count == 14 && r.applied == 14 && nativeCalls == 14);
    // Capability off while body apply is on: weapon slots UNAVAILABLE, no engine call.
    sco::caps::Set("game.loadout.weapons", false, "synthetic revocation");
    CHECK(NativeApply(svc, self, {SC_LOADOUT_SLOT_TORSO, SC_LOADOUT_SLOT_PRIMARY}, &r) == SCO_FAILED && nativeCalls == 1);
    CHECK(r.slots[0].status == SC_LOADOUT_APPLIED && r.slots[1].status == SC_LOADOUT_UNAVAILABLE);
    sco::caps::Set("game.loadout.weapons", true);
    // Backend-reported refusals: empty parent port, class missing, engine refusal; none is a provider fault.
    for (sc_loadout_status st : {SC_LOADOUT_UNSUPPORTED, SC_LOADOUT_ITEM_MISSING, SC_LOADOUT_FAILED}) {
        nativeReply = st;
        CHECK(NativeApply(svc, self, {SC_LOADOUT_SLOT_PRIMARY_OPTIC}, &r) == SCO_FAILED && nativeCalls == 1);
        CHECK(r.applied == 0 && r.slots[0].status == st);
        CHECK(api->has("game.loadout.apply") && api->has("game.loadout.weapons"));
    }
    nativeReply = SC_LOADOUT_APPLIED;
}
}
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
    WeaponSlotRows(api, p.self);
    {
        // A module built from sc_loadout.h alone receives the host-dispatched respawn event.
        namespace S = sco::game::services;
        S::LoadoutBackend b{};
        b.verified = true; b.offline = NativeOffline; b.equip = NativeEquip; b.respawnCompleted = true;
        S::StopLoadout();
        CHECK(S::StartLoadout(&b) == sco::Result::Ok);
        CHECK(api->has("game.events.loadout_respawn_completed") && S::RecordRespawnLoadoutCompleted());
        uint32_t now = 0;
        CHECK(sco::Dispatch("tick", &now) == sco::Result::Ok);
        out = {};
        CHECK(api->invoke(p.self, "loadout_native.respawn_count", nullptr, 0, done, &out) == SCO_OK);
        CHECK(out.calls == 1 && std::strcmp(out.text, "respawns=1") == 0);
        // Leave the weapon-attested body backend running, as WeaponSlotRows did.
        b.respawnCompleted = false; b.weapons = true;
        S::StopLoadout();
        CHECK(S::StartLoadout(&b) == sco::Result::Ok);
    }
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
    CHECK(sco::host::ProvideGameService(SC_LOADOUT_SERVICE_NAME, SC_LOADOUT_VERSION_1_1, realTable) == sco::Result::Ok);
    P::UnloadAll(plugins); P::UnloadAll(plugins);
    CHECK(p.state == P::State::Unloaded);
    std::puts("loadout native: real loader/registry/provider multi-slot partial workflow OK");
}
