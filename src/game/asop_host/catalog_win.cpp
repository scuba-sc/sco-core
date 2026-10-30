// SPDX-License-Identifier: GPL-3.0-only
// Ported from sc-offline hooks.cpp; bounded user-supplied classes, runtime GUID lookup only.
#include "engine.h"
#include "sco/game/asop.h"
#include <intrin.h>
#include <cstdlib>
#include <initializer_list>
namespace sco::game::asop_host {
constexpr size_t   kEntitlementSize = 0x150;
// The fleet ships' entitlement URN: urn:sc:platform:entitlement:uuid:<id>. CSCURN bytes +0x00
// namespace (6 platform; 0x11 = none), +0x01 type (0x16 entitlement; 0x1E = none), +0x08 kind
// (3 uuid; 5 = none), +0x20 the id's variant (2 = a 16-byte id at +0x10). The game reads a URN with
// any "none" byte as unset (0x441F50 on 4.10.196), and the terminal then offers no Deliver, Retrieve
// or Claim on the row; 0x1E11 / kind 5, used before, is exactly the list builder's unset URN.
constexpr uint16_t kUrnType = 0x1606;
constexpr uint8_t  kUrnKindUuid = 3;
constexpr uint8_t  kUrnIdGuid = 2;
constexpr uint64_t kShipUrnMarker = 0x53434F4600000000ull;
struct FleetShip { char name[64]; uint64_t guid[2]; };
// Same bound as the spawn menu (spawner.cpp kMaxMenuShips): ships.txt holds 1102 entries, and the
// old 1024 here dropped the last 78 from the fleet manager without saying so.
constexpr int kMaxFleetShips = 2048;

static FleetShip* g_ships = nullptr;
static int        g_shipCount = 0;
static uint8_t*   g_fakeEntitlements = nullptr;
static SRWLOCK    g_shipsLock = SRWLOCK_INIT;
static bool       g_shipsBuilt = false;


using EntitlementsFn=void(__fastcall*)(uintptr_t,uintptr_t);
static EntitlementsFn g_origEntitlementsResult=nullptr;
static void FindClassGuids(uintptr_t registry, const uintptr_t* classes) {
    const uintptr_t head = Rd<uintptr_t>(registry + 0x48);
    uintptr_t stack[256];
    int sp = 0;
    const uintptr_t root = Rd<uintptr_t>(head + 0x08);
    if (root && !Rd<uint8_t>(root + 0x19)) stack[sp++] = root;
    while (sp) {
        const uintptr_t n = stack[--sp];
        const uintptr_t cls = Rd<uintptr_t>(n + 0x30);
        for (int i = 0; i < g_shipCount; ++i)
            if (classes[i] == cls) { g_ships[i].guid[0] = Rd<uint64_t>(n + 0x20); g_ships[i].guid[1] = Rd<uint64_t>(n + 0x28); }
        for (size_t off : { size_t(0x00), size_t(0x10) }) {
            const uintptr_t c = Rd<uintptr_t>(n + off);
            if (c && !Rd<uint8_t>(c + 0x19) && sp < 256) stack[sp++] = c;
        }
    }
}

static int BuildFleetShips() {
    if (!EntitySystem()) return 0;
    static FleetShip ships[kMaxFleetShips];
    static uintptr_t classes[kMaxFleetShips];
    g_ships = ships;
    int missing = 0;
    const uintptr_t registry = VCall<uintptr_t>(EntitySystem(), 0xC0);
    for (size_t i=0; i<catalog.count; ++i) {
        const char* name=catalog.names[i].data();
        const uintptr_t cls=VCall<uintptr_t>(registry,0x20,name);
        if (!cls) { ++missing; continue; }
        strncpy_s(ships[g_shipCount].name,name,_TRUNCATE);
        classes[g_shipCount++]=cls;
    }
    FindClassGuids(registry, classes);

    int kept = 0;
    for (int i = 0; i < g_shipCount; ++i) {
        uint64_t guid[2] = { ships[i].guid[0], ships[i].guid[1] };
        if ((guid[0] | guid[1]) && VCall<uintptr_t>(registry, 0x18, guid) == classes[i]) ships[kept++] = ships[i];
        else Log("[fleet] no class GUID for '%s' (skipped)", ships[i].name);
    }
    g_shipCount = kept;

    g_fakeEntitlements = static_cast<uint8_t*>(calloc(kept ? kept : 1, kEntitlementSize));
    for (int i = 0; g_fakeEntitlements && i < kept; ++i) {
        uint8_t* e = g_fakeEntitlements + i * kEntitlementSize;
        *reinterpret_cast<uint16_t*>(e + 0x00) = kUrnType;
        e[0x08] = kUrnKindUuid;
        *reinterpret_cast<uint64_t*>(e + 0x10) = kShipUrnMarker | static_cast<uint32_t>(i);
        e[0x20] = kUrnIdGuid;
        e[0x50] = 3;
        *reinterpret_cast<uint32_t*>(e + 0x54) = 1;
        memcpy(e + 0x58, ships[i].guid, 16);
    }
    if (!g_fakeEntitlements) g_shipCount = 0;
    Log("[fleet] %d ships available in the fleet manager (%d unknown names)", g_shipCount, missing);
    return g_shipCount;
}

static int EnsureFleetShips() {
    AcquireSRWLockExclusive(&g_shipsLock);
    if (!g_shipsBuilt) {
        g_shipsBuilt = true;
        __try { BuildFleetShips(); } __except (EXCEPTION_EXECUTE_HANDLER) { g_shipCount = 0; Log("[fleet] fault while building the ship list"); }
    }
    const int n = g_shipCount;
    ReleaseSRWLockExclusive(&g_shipsLock);
    return n;
}

static void __fastcall Hook_EntitlementsResult(uintptr_t self, uintptr_t result) {
    if (!admitted.load()) { g_origEntitlementsResult(self,result); return; }
    bool failed = false;
    __try { failed = Rd<uint8_t>(result) != 1; } __except (EXCEPTION_EXECUTE_HANDLER) {}
    if (failed && Fleet_UseGameList()) {
        static volatile LONG told = 0;
        if (!InterlockedExchange(&told, 1))
            Log("[fleet] asop_fleet_list = game: the game's own ship list, not ships.txt (its entitlement query failed, so it lists nothing; asop_fleet_list = ships lists ships.txt)");
        g_origEntitlementsResult(self, result);
        return;
    }
    const int n = failed ? EnsureFleetShips() : 0;
    if (!n) { g_origEntitlementsResult(self, result); return; }
    struct { uint8_t ok; uint8_t pad[7]; uint8_t* begin; uint8_t* end; uint8_t* cap; uint8_t spare[64]; } fake = {};
    fake.ok = 1;
    fake.begin = g_fakeEntitlements;
    fake.end = fake.cap = g_fakeEntitlements + n * kEntitlementSize;
    static volatile LONG reported = 0;
    if (InterlockedIncrement(&reported) <= 3) Log("[fleet] online ship list unavailable -> listing %d offline ships", n);
    g_origEntitlementsResult(self, reinterpret_cast<uintptr_t>(&fake));
}

int FleetShipIndexOfUrnId(const uint64_t id[2]) {
    AcquireSRWLockShared(&g_shipsLock);
    const int count = g_shipsBuilt ? g_shipCount : 0;
    ReleaseSRWLockShared(&g_shipsLock);
    for (int i = 0; i < 2; ++i)
        for (const uint64_t q : { id[i], _byteswap_uint64(id[i]) })
            if ((q & 0xFFFFFFFF00000000ull) == kShipUrnMarker && static_cast<uint32_t>(q) < static_cast<uint32_t>(count))
                return static_cast<int>(static_cast<uint32_t>(q));
    return -1;
}

const char* FleetShipClass(int index) {
    AcquireSRWLockShared(&g_shipsLock);
    const int count = g_shipsBuilt ? g_shipCount : 0;
    ReleaseSRWLockShared(&g_shipsLock);
    return index >= 0 && index < count ? g_ships[index].name : nullptr;
}

// Selection busy gate: is any ship of this class stored, retrieved, delivering or queued
// this session? Runs on the game thread under the selection service's lock, so it may
// only take the fleet and ships locks (both ordered after it).
bool FleetClassBusy(const char* name) {
    if (!name) return false;
    AcquireSRWLockShared(&g_shipsLock);
    int index = -1;
    const int count = g_shipsBuilt ? g_shipCount : 0;
    for (int i = 0; i < count; ++i)
        if (_stricmp(g_ships[i].name, name) == 0) { index = i; break; }
    ReleaseSRWLockShared(&g_shipsLock);
    return index >= 0 && ShipIsBusy(index);
}


bool HookFunction(uint8_t* target,size_t stolen,void* detour,void** original) {
    return target && sco::hook::InstallDetour(target,stolen,detour,original)==sco::hook::Error::None;
}
uintptr_t EntitySystem() { const auto* p=reinterpret_cast<uintptr_t*>(sco::Sig("teleport.entity_system")); return p ? *p : 0; }
bool AsopEnabled() { return admitted.load(); }
bool Fleet_UseGameList() { return settings.catalog==CatalogMode::Game; }
constexpr size_t kSlotUrn=0x1698;
constexpr size_t kAsopAtcId=sco::game::asop::kKioskAtc;
using RetrieveVehicleFn=void(__fastcall*)(uintptr_t,uintptr_t);
static RetrieveVehicleFn g_origRetrieveVehicle=nullptr;
using StrCtorFn          = void*(__fastcall*)(void* out, const char* s);
using StrDtorFn          = void(__fastcall*)(void* s);
using GetATCCompFn       = uint64_t*(__fastcall*)(uint64_t* out, uint64_t entityId);
using RequestTakingOffFn = void(__fastcall*)(uintptr_t atc, uint64_t player, void* location, uint64_t vehicle,
                                             bool spawnVehicle, void* archetype, void* padFilter);
static StrCtorFn          g_strCtor = nullptr;

static StrDtorFn          g_strDtor = nullptr;

static GetATCCompFn       g_getATCComp = nullptr;
static RequestTakingOffFn g_requestTakingOff = nullptr;

// fleet.retrieve_atc: asop.fleet_retrieve checks the kiosk ATC id load (+0xC7) and the
// GetATCComponent call (+0xDD, atc.get_component); fleet.takeoff_command the g_ATC_requestTakeOff
// command whose calls give the CryString ctor / dtor and RequestTakingOff.
static bool PrepareRetrieveVehicle() {
    g_getATCComp       = reinterpret_cast<GetATCCompFn>(sco::Sig("atc.get_component"));
    g_strCtor          = reinterpret_cast<StrCtorFn>(sco::Sig("fleet.string_ctor"));
    g_strDtor          = reinterpret_cast<StrDtorFn>(sco::Sig("fleet.string_dtor"));
    g_requestTakingOff = reinterpret_cast<RequestTakingOffFn>(sco::Sig("fleet.request_taking_off"));
    return true;
}

static int SlotShipIndex(uintptr_t slot) {
    __try {
        const uintptr_t urn = slot + kSlotUrn;
        if (Rd<uint16_t>(urn) != kUrnType || Rd<uint8_t>(urn + 0x20) != kUrnIdGuid) return -1;
        const uint64_t id = Rd<uint64_t>(urn + 0x10);
        const int i = static_cast<int>(static_cast<uint32_t>(id));
        return (id & 0xFFFFFFFF00000000ull) == kShipUrnMarker && i < g_shipCount ? i : -1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

const char* RequestShipFromAtc(uint64_t atcEntity, uint64_t player, const char* shipClass) {
    if (!g_getATCComp || !g_requestTakingOff) return "the ATC functions weren't found";
    __try {
        uint64_t handle[2] = {};
        g_getATCComp(handle, atcEntity);
        const uintptr_t atc = handle[0] & kPtrMask;
        if (!atc) return "this terminal has no spaceport ATC";
        if (!player) return "player not spawned";
        alignas(16) uint8_t location[32] = {};
        void* archetype = nullptr;
        void* padFilter = nullptr;
        g_strCtor(location, "");
        g_strCtor(&archetype, shipClass);
        g_strCtor(&padFilter, "");
        g_requestTakingOff(atc, player, location, 0, true, &archetype, &padFilter);
        g_strDtor(&padFilter);
        g_strDtor(&archetype);
        g_strDtor(location);
        return nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return "fault while asking the ATC";
    }
}

static void __fastcall Hook_RetrieveVehicle(uintptr_t asop, uintptr_t slot) {
    if (!admitted.load()) { g_origRetrieveVehicle(asop,slot); return; }
    const int i = SlotShipIndex(slot);
    // With the ASOP Retrieve (fleet.cpp) on, the game's own retrieve runs: it reaches the ATC with
    // the stored ship, where asking the ATC directly for a class would lock the station's terminals.
    if (i < 0 || Fleet_RetrieveActive()) { g_origRetrieveVehicle(asop, slot); return; }
    uint64_t atc = 0;
    __try { atc = Rd<uint64_t>(asop + kAsopAtcId); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    if (const char* err = RequestShipFromAtc(atc, LocalPlayerEntityId(), g_ships[i].name))
        Log("[fleet] retrieve %s failed: %s", g_ships[i].name, err);
    else Log("[fleet] retrieve %s: asked the spaceport ATC for a hangar/pad", g_ships[i].name);
}

bool InstallCatalog() {
    if (!reads::Ready() || !sco::SigReady("fleet.entitlements_result")) return false;
    const bool installed=HookFunction(sco::Sig("fleet.entitlements_result"),5,reinterpret_cast<void*>(&Hook_EntitlementsResult),reinterpret_cast<void**>(&g_origEntitlementsResult));
    const char* rows[]={"asop.fleet_retrieve","atc.get_component","fleet.string_ctor","fleet.string_dtor","fleet.request_taking_off"};
    bool ready=true; for(const char* row:rows) ready &= sco::SigReady(row);
    if(ready && PrepareRetrieveVehicle())
        HookFunction(sco::Sig("asop.fleet_retrieve"),5,reinterpret_cast<void*>(&Hook_RetrieveVehicle),reinterpret_cast<void**>(&g_origRetrieveVehicle));
    return installed;
}
} // namespace sco::game::asop_host
