// SPDX-License-Identifier: GPL-3.0-only
// game.hangar 1.0 on Windows: the HangarProvider built from rows, and the published table. The
// calls are the ones the ASOP host composition already makes (src/game/asop_host/fleet_win.cpp:
// CallLift, FindLiftManager, AtcDataManager, CallRequestCancel), on the rows docs/game/asop.md
// lists: lift.open_request / lift.close_request (rc13), atc.get_component and atc.request_cancel
// (rc6), with spawn.find_entity_by_name, spawn.handle_to_id and the teleport.* reads to find
// entities. No hooks, no patches, no scans: this file only calls. Both capabilities stay OFF
// (HangarProvider::probed is false) until an in-game run confirms them.
#include "hangar.h"
#include "../../api/internal.h"
#include "sco/game/reads.h"
#include "sco/host.h"
#include "sco/log.h"
#include "sco/signatures.h"
#include <cstdio>
#include <cstring>
#include <iterator>
#include <windows.h>

namespace sco::game::services {

namespace {

using LiftRequestFn   = void(__fastcall*)(uintptr_t self, uint64_t manager);
using GetAtcFn        = uint64_t*(__fastcall*)(uint64_t* out, uint64_t atcEntity);
using RequestCancelFn = void(__fastcall*)(uintptr_t atc, uint64_t player);
using HandleToIdFn    = uint64_t*(__fastcall*)(const void* handle, uint64_t* entityId);
using FindByNameFn    = uint64_t*(__fastcall*)(uintptr_t entitySystem, uint64_t* handle, const char* name);

constexpr uint64_t kPtrMask        = 0xFFFFFFFFFFFFull;
constexpr size_t   kEntityNameSlot = 0x78;   // entity vtable: name (fleet_win.cpp ReadEntityPlace)
constexpr char     kAreaPrefix[]   = "LandingArea_";
constexpr char     kLiftPrefix[]   = "LoadingPlatformManager_";

constexpr const char* kLiftRows[] = {
    "lift.open_request", "lift.close_request", "teleport.entity_system", "spawn.find_entity_by_name", "spawn.handle_to_id",
};
constexpr const char* kAtcRows[] = { "atc.get_component", "atc.request_cancel", "spawn.handle_to_id" };

struct Engine {
    LiftRequestFn   open = nullptr, close = nullptr;
    GetAtcFn        getAtc = nullptr;
    RequestCancelFn cancel = nullptr;
    HandleToIdFn    handleToId = nullptr;
    FindByNameFn    findByName = nullptr;
    uintptr_t*      entitySystem = nullptr;
};
Engine g_e;
Hangar g_hangar;
bool   g_published = false;

template <typename T> T Rd(uintptr_t p) { return *reinterpret_cast<const T*>(p); }

template <typename R, typename... A> R VCall(uintptr_t obj, size_t off, A... a) {
    using Fn = R(__fastcall*)(uintptr_t, A...);
    return reinterpret_cast<Fn>(Rd<uintptr_t>(Rd<uintptr_t>(obj) + off))(obj, a...);
}

bool RowsOk(const char* const* ids, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        const SigResult* s = SigLookup(ids[i]);
        if (!s || s->state != SigState::Ok) return false;
    }
    return true;
}

// ---- game reads and calls, under SEH -----------------------------------------------------------
//
// 1 found, 0 not there, -1 the game faulted. No C++ object with a destructor lives in these frames.

int EntityPlace(uint64_t id, char* name, size_t n, uint64_t* zoneId) {
    name[0] = 0;
    *zoneId = 0;
    __try {
        const uintptr_t e = reads::EntityFromId(id);
        if (!e) return 0;
        const char* s = VCall<const char*>(e, kEntityNameSlot);
        if (s) strncpy_s(name, n, s, _TRUNCATE);
        const uintptr_t zone = reads::EntityZone(e);
        *zoneId = zone ? reads::ZoneId(zone) : 0;
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        name[0] = 0;
        return -1;
    }
}

int FindEntityId(const char* name, uint64_t* id) {
    *id = 0;
    __try {
        const uintptr_t es = *g_e.entitySystem;
        if (!es) return 0;
        uint64_t handle = 0;
        g_e.findByName(es, &handle, name);
        if (!(handle & kPtrMask)) return 0;
        g_e.handleToId(&handle, id);
        return *id ? 1 : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *id = 0;
        return -1;
    }
}

int LocalPlayerId(uint64_t* id) {
    *id = 0;
    __try {
        uintptr_t actor = 0, entity = 0;
        if (!reads::LocalPlayer(actor, entity)) return 0;
        g_e.handleToId(reinterpret_cast<const void*>(actor + 8), id);
        return *id ? 1 : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *id = 0;
        return -1;
    }
}

int AtcDataManager(uint64_t atc, uintptr_t* manager) {
    *manager = 0;
    __try {
        uint64_t out[2] = {};
        g_e.getAtc(out, atc);
        *manager = static_cast<uintptr_t>(out[0] & kPtrMask);
        return *manager ? 1 : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *manager = 0;
        return -1;
    }
}

bool CallLift(LiftRequestFn fn, uint64_t lift) {
    __try { fn(0, lift); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool CallRequestCancel(uintptr_t manager, uint64_t player) {
    __try { g_e.cancel(manager, player); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool HasPrefix(const char* s, const char* prefix, size_t len) { return std::strncmp(s, prefix, len) == 0 && s[len]; }

// ---- the provider ------------------------------------------------------------------------------

// The lift that goes with a landing area (fleet_win.cpp FindLiftManager): LandingArea_X ->
// LoadingPlatformManager_X, accepted only in the landing area's zone.
sco_result LiftOfArea(uint64_t area, uint64_t* lift, const char** why) {
    char name[96];
    uint64_t areaZone = 0;
    const int got = EntityPlace(area, name, sizeof(name), &areaZone);
    if (got < 0) { *why = "the game faulted reading the landing area"; return SCO_FAILED; }
    if (!got) { *why = "the landing area isn't streamed in"; return SCO_NOT_FOUND; }
    if (!HasPrefix(name, kAreaPrefix, sizeof(kAreaPrefix) - 1)) { *why = "that entity isn't a landing area"; return SCO_BAD_ARG; }
    char liftName[160];
    std::snprintf(liftName, sizeof(liftName), "%s%s", kLiftPrefix, name + sizeof(kAreaPrefix) - 1);
    uint64_t id = 0;
    const int found = FindEntityId(liftName, &id);
    if (found < 0) { *why = "the game faulted looking the lift up"; return SCO_FAILED; }
    if (!found) { *why = "no ship lift goes with that landing area"; return SCO_NOT_FOUND; }
    char seen[96];
    uint64_t liftZone = 0;
    const int again = EntityPlace(id, seen, sizeof(seen), &liftZone);
    if (again < 0) { *why = "the game faulted reading the lift"; return SCO_FAILED; }
    if (!again || liftZone != areaZone) { *why = "no ship lift in the landing area's zone"; return SCO_NOT_FOUND; }
    *lift = id;
    return SCO_OK;
}

sco_result LiftRequest(uint64_t lift, uint32_t op, const char** why) {
    char name[96];
    uint64_t zone = 0;
    const int got = EntityPlace(lift, name, sizeof(name), &zone);
    if (got < 0) { *why = "the game faulted reading the lift"; return SCO_FAILED; }
    if (!got) { *why = "the lift isn't streamed in"; return SCO_NOT_FOUND; }
    if (!HasPrefix(name, kLiftPrefix, sizeof(kLiftPrefix) - 1)) { *why = "that entity isn't a ship lift"; return SCO_BAD_ARG; }
    if (!CallLift(op == SC_HANGAR_LIFT_OPEN ? g_e.open : g_e.close, lift)) { *why = "the game faulted in the lift request"; return SCO_FAILED; }
    return SCO_OK;
}

sco_result AtcCancel(uint64_t atc, const char** why) {
    char name[96];
    uint64_t zone = 0;
    const int got = EntityPlace(atc, name, sizeof(name), &zone);
    if (got < 0) { *why = "the game faulted reading the ATC"; return SCO_FAILED; }
    if (!got) { *why = "the ATC isn't streamed in"; return SCO_NOT_FOUND; }
    uint64_t player = 0;
    const int you = LocalPlayerId(&player);
    if (you < 0) { *why = "the game faulted reading the local player"; return SCO_FAILED; }
    if (!you) { *why = "you're not spawned"; return SCO_FAILED; }
    uintptr_t manager = 0;
    const int has = AtcDataManager(atc, &manager);
    if (has < 0) { *why = "the game faulted in the ATC component getter"; return SCO_FAILED; }
    if (!has) { *why = "that entity has no ATC data"; return SCO_NOT_FOUND; }
    if (!CallRequestCancel(manager, player)) { *why = "the game faulted in the ATC request cancel"; return SCO_FAILED; }
    return SCO_OK;
}

bool OnThread() { return OnGameThread(); }
bool Live(const sco_plugin* p) { return host::PluginId(p) && !detail::Released(p); }

void LogCall(const sco_plugin* self, const char* fn, sco_result r, const char* why) {
    const char* id = self ? host::PluginId(self) : nullptr;
    Log("[game] hangar: %s: %s -> %s: %s", id ? id : "?", fn, ResultName(static_cast<Result>(r)), why);
}

sco_result SvcLiftOfLandingArea(sco_plugin* self, uint64_t area, uint64_t* outLift) { return g_hangar.LiftOfLandingArea(self, area, outLift); }
sco_result SvcLiftRequest(sco_plugin* self, uint64_t lift, uint32_t op) { return g_hangar.LiftRequest(self, lift, op); }
sco_result SvcAtcCancelRequest(sco_plugin* self, uint64_t atc) { return g_hangar.AtcCancelRequest(self, atc); }
sco_result SvcLastError(sco_plugin* self, char* out, uint32_t* inoutSize) { return g_hangar.LastError(self, out, inoutSize); }

const sc_hangar_v1 kService = {
    sizeof(sc_hangar_v1), 0, SvcLiftOfLandingArea, SvcLiftRequest, SvcAtcCancelRequest, SvcLastError,
};

}  // namespace

Result StartHangar() {
    if (g_published) return Result::Ok;
    if (!OnGameThread()) return Result::WrongThread;
    HangarProvider p;
    p.on_thread = OnThread;
    p.live = Live;
    const bool readsOk = reads::Ready();
    p.lift_rows = readsOk && RowsOk(kLiftRows, std::size(kLiftRows));
    p.atc_rows  = readsOk && RowsOk(kAtcRows, std::size(kAtcRows));
    p.probed = false;
    g_e = {};
    if (p.lift_rows) {
        g_e.open         = reinterpret_cast<LiftRequestFn>(Sig("lift.open_request"));
        g_e.close        = reinterpret_cast<LiftRequestFn>(Sig("lift.close_request"));
        g_e.entitySystem = reinterpret_cast<uintptr_t*>(Sig("teleport.entity_system"));
        g_e.findByName   = reinterpret_cast<FindByNameFn>(Sig("spawn.find_entity_by_name"));
    }
    if (p.atc_rows) {
        g_e.getAtc = reinterpret_cast<GetAtcFn>(Sig("atc.get_component"));
        g_e.cancel = reinterpret_cast<RequestCancelFn>(Sig("atc.request_cancel"));
    }
    if (p.lift_rows || p.atc_rows) g_e.handleToId = reinterpret_cast<HandleToIdFn>(Sig("spawn.handle_to_id"));
    p.lift_of_area = LiftOfArea;
    p.lift_request = LiftRequest;
    p.atc_cancel = AtcCancel;
    p.log = LogCall;
    if (g_hangar.Start(p) != SCO_OK) {
        g_e = {};
        return Result::WrongThread;
    }
    const Result r = host::ProvideGameService(SC_HANGAR_SERVICE_NAME, SC_HANGAR_SERVICE_VERSION, &kService);
    if (r != Result::Ok) {
        g_hangar.Stop();
        g_e = {};
        return r;
    }
    g_published = true;
    Log("[game] game.hangar 1.0 published: game.hangar.lift and game.hangar.atc_cancel held OFF pending an in-game run");
    return Result::Ok;
}

void StopHangar() {
    if (!g_published) return;
    host::WithdrawGameService(SC_HANGAR_SERVICE_NAME);
    g_hangar.Stop();
    g_e = {};
    g_published = false;
}

void ReleaseHangarOwner(const void* owner) { g_hangar.ReleaseOwner(owner); }

}  // namespace sco::game::services
