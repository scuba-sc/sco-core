// SPDX-License-Identifier: GPL-3.0-only
// Ported from sc-offline hooks.cpp; bounded user-supplied classes, runtime GUID lookup only.
#include "engine.h"
#include "fleet_transaction.h"
#include "sco/runtime.h"
#include "sco/game/asop.h"
#include <intrin.h>
#include <cstdlib>
#include <initializer_list>
#include <limits>
namespace sco::game::asop_host {
constexpr size_t   kEntitlementSize = 0x150;
// The fleet ships' entitlement URN: urn:sc:platform:entitlement:uuid:<id>. CSCURN bytes +0x00
// namespace (6 platform; 0x11 = none), +0x01 type (0x16 entitlement; 0x1E = none), +0x08 kind
// (3 uuid; 5 = none), +0x20 the id's variant (2 = a 16-byte id at +0x10). The game reads a URN with
// any "none" byte as unset (0x441F50 on 4.10.196), and the terminal then offers no Deliver, Retrieve
// or Claim on the row; 0x1E11 / kind 5, used before, is exactly the list builder's unset URN.
constexpr uint16_t kUrnType = 0x1606;
constexpr uint8_t  kUrnIdGuid = 2;
constexpr uint64_t kShipUrnMarker = kFleetUrnMarker;
static FleetTable g_table;
static auto& g_slots=g_table.slots;
static auto& g_slotCount=g_table.count;
// RebuildFleet starts at zero and refuses insertion at capacity; slot counts and
// every stable index therefore fit the int-based engine-facing lookup API.
static_assert(kFleetCapacity <= static_cast<size_t>((std::numeric_limits<int>::max)()));
static FleetBuffer g_buffer;
static SRWLOCK g_shipsLock = SRWLOCK_INIT;
static bool g_shipsBuilt = false;
static std::atomic<bool> g_initialPending{false};
using EntitlementsFn=void(__fastcall*)(uintptr_t,uintptr_t);
static EntitlementsFn g_origEntitlementsResult=nullptr;

// Keep SEH in this leaf: no C++ destructors or unpublished allocations cross faults.
static bool ResolveFleetClass(void*, const char* name, uintptr_t& cls, uint64_t guid[2]) {
    __try {
        const uintptr_t system=EntitySystem();
        if(!system) return false;
        const uintptr_t registry=VCall<uintptr_t>(system,0xC0);
        if(!registry || !(cls=VCall<uintptr_t>(registry,0x20,name))) return false;
        const uintptr_t head=Rd<uintptr_t>(registry+0x48);
        uintptr_t stack[256]; size_t sp=0;
        const uintptr_t root=Rd<uintptr_t>(head+8);
        if(root && !Rd<uint8_t>(root+0x19)) stack[sp++]=root;
        size_t visited=0;
        while(sp) {
            if(++visited>1048576) return false;
            const uintptr_t node=stack[--sp];
            if(Rd<uintptr_t>(node+0x30)==cls) {
                guid[0]=Rd<uint64_t>(node+0x20); guid[1]=Rd<uint64_t>(node+0x28);
                return (guid[0]|guid[1]) && VCall<uintptr_t>(registry,0x18,guid)==cls;
            }
            for(size_t offset : {size_t(0),size_t(0x10)}) {
                const uintptr_t child=Rd<uintptr_t>(node+offset);
                if(child && !Rd<uint8_t>(child+0x19)) {
                    if(sp==256) return false;
                    stack[sp++]=child;
                }
            }
        }
        return false;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// No engine calls or publication until the host's known game thread admits this.
bool RebuildFleetShips(const Catalog& next) {
    if(!admitted.load() || !sco::OnGameThread()) return false;
    const char* names[kMaxShips];
    for(size_t i=0;i<next.count;++i) names[i]=next.names[i].data();
    AcquireSRWLockExclusive(&g_shipsLock);
    size_t skipped=0;
    // Only initial publication preserves the legacy skip-unresolved policy.
    // Runtime replacements remain all-or-nothing (and native Reconfigure is refused).
    const bool initial=!g_shipsBuilt;
    const bool ok=RebuildFleet(g_table,g_buffer,names,next.count,ResolveFleetClass,nullptr,std::calloc,
                               initial ? &skipped : nullptr);
    if(ok) g_shipsBuilt=true;
    ReleaseSRWLockExclusive(&g_shipsLock);
    if(ok && initial && skipped) Log("[fleet] initial list skipped %zu unresolved ship classes",skipped);
    return ok;
}

static FleetBuffer EnsureFleetShips() {
    AcquireSRWLockShared(&g_shipsLock);
    const bool built=g_shipsBuilt;
    ReleaseSRWLockShared(&g_shipsLock);
    if(!built) {
        if(sco::OnGameThread()) RebuildFleetShips(catalog);
        else g_initialPending.store(true);
    }
    AcquireSRWLockShared(&g_shipsLock);
    const FleetBuffer snapshot=g_buffer;
    ReleaseSRWLockShared(&g_shipsLock);
    return snapshot;
}

void PumpInitialFleet() {
    if(!g_initialPending.exchange(false)) return;
    AcquireSRWLockShared(&g_shipsLock);
    const bool built=g_shipsBuilt;
    ReleaseSRWLockShared(&g_shipsLock);
    if(!built) RebuildFleetShips(catalog);
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
    const FleetBuffer snapshot = failed ? EnsureFleetShips() : FleetBuffer{};
    const int n = static_cast<int>(snapshot.count);
    if (!HasFleetEntitlements(snapshot)) { g_origEntitlementsResult(self, result); return; }
    struct { uint8_t ok; uint8_t pad[7]; uint8_t* begin; uint8_t* end; uint8_t* cap; uint8_t spare[64]; } fake = {};
    fake.ok = 1;
    fake.begin = snapshot.data;
    fake.end = fake.cap = snapshot.data + n * kEntitlementSize;
    static volatile LONG reported = 0;
    if (InterlockedIncrement(&reported) <= 3) Log("[fleet] online ship list unavailable -> listing %d offline ships", n);
    g_origEntitlementsResult(self, reinterpret_cast<uintptr_t>(&fake));
}

// Returns the stable slot index for a synthetic entitlement URN, or -1. Works for
// retired slots too: stored/in-transit records of a deselected class keep resolving.
int FleetShipIndexOfUrnId(const uint64_t id[2]) {
    AcquireSRWLockShared(&g_shipsLock);
    const int count = g_shipsBuilt ? static_cast<int>(g_slotCount) : 0;
    ReleaseSRWLockShared(&g_shipsLock);
    for (int i = 0; i < 2; ++i)
        for (const uint64_t q : { id[i], _byteswap_uint64(id[i]) })
            if ((q & 0xFFFFFFFF00000000ull) == kShipUrnMarker && static_cast<uint32_t>(q) < static_cast<uint32_t>(count))
                return static_cast<int>(static_cast<uint32_t>(q));
    return -1;
}

const char* FleetShipClass(int index) {
    AcquireSRWLockShared(&g_shipsLock);
    const int count = g_shipsBuilt ? static_cast<int>(g_slotCount) : 0;
    const char* name = index >= 0 && index < count ? g_slots[index].name : nullptr;
    ReleaseSRWLockShared(&g_shipsLock);
    return name;
}

// Selection busy gate: is any ship of this class stored, retrieved, delivering or queued
// this session? Runs on the game thread under the selection service's lock, so it may
// only take the fleet and ships locks (both ordered after it).
bool FleetClassBusy(const char* name) {
    if (!name || !admitted.load() || !sco::OnGameThread()) return true;
    AcquireSRWLockShared(&g_shipsLock);
    int index = -1;
    const int count = g_shipsBuilt ? static_cast<int>(g_slotCount) : 0;
    for (int i = 0; i < count; ++i)
        if (_stricmp(g_slots[i].name, name) == 0) { index = i; break; }
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
        const uint64_t id[2] = {Rd<uint64_t>(urn + 0x10),0};
        return FleetShipIndexOfUrnId(id);
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
    const char* name=FleetShipClass(i);
    if(!name) { g_origRetrieveVehicle(asop,slot); return; }
    if (const char* err = RequestShipFromAtc(atc, LocalPlayerEntityId(), name))
        Log("[fleet] retrieve %s failed: %s", name, err);
    else Log("[fleet] retrieve %s: asked the spaceport ATC for a hangar/pad", name);
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
