// SPDX-License-Identifier: GPL-3.0-only
// Runs the actual ported delivery loop with synthetic primitive outcomes.
namespace delivery_fixture {
using DWORD=uint32_t;
constexpr double kDeliverySpawnHeight=1000;
constexpr DWORD kDeliverySettleMs=3000,kDeliveryGiveUpMs=30000,kRefreshAfterStoreMs=1500,kRefreshAfterClaimMs=1500;
constexpr int32_t kClaimTimeoutSeconds=8;
struct DeliverRequest { uint64_t atc=0; uint32_t location=0; uint64_t urn[2]={}; DWORD at=0; };
struct Delivery { int ship=-1; uint64_t atc=0; uint32_t location=0; uint64_t vehicle=0; DWORD requestedAt=0,spawnedAt=0; };
struct StoredShip { int ship=-1; uint64_t vehicle=0; uint64_t inventory[3]={}; uintptr_t cls=0; DWORD storedAt=0; uint32_t location=0; uint64_t atc=0; bool checked=false; };
std::vector<Delivery> g_deliveries;
std::vector<StoredShip> g_stored;
int spawns=0,stores=0,registrations=0,unregistrations=0,refreshes=0;
bool alive=true,available=true;
void Log(const char*,...) {}
int FleetShipIndexOfUrnId(const uint64_t id[2]) { return id[0]==123 ? 0 : -1; }
const char* FleetShipClass(int i) { return i==0 ? "synthetic_alpha" : nullptr; }
bool ShipIsBusy(int i) { return !g_deliveries.empty() || (!g_stored.empty() && i==0); }
const char* SpawnEntityNearPlayer(const char*,const double offset[3],uint64_t& id) {
    assert(offset[2]==1000); ++spawns; if(!available) return "unavailable"; id=456; return nullptr;
}
void RegisterPlayerVehicle(uint64_t id) { assert(id==456); ++registrations; }
void UnregisterPlayerVehicle(uint64_t id) { assert(id==456); ++unregistrations; }
bool EntityAlive(uint64_t) { return alive; }
uintptr_t ClassByName(const char*) { return 789; }
const char* StoreShip(uint64_t,uint64_t,uint32_t,uint64_t*) { ++stores; return available ? nullptr : "unavailable"; }
void RequestRefresh(DWORD) { ++refreshes; }
#include "delivery.inc"
void TestDelivery() {
    DeliverRequest r{1,2,{123,0},10};
    StartDelivery(r,10); assert(spawns==1 && g_deliveries.size()==1 && registrations==1);
    StartDelivery(r,20); assert(spawns==1); // duplicate in-flight refused
    RunDeliveries(3009); assert(stores==0);
    RunDeliveries(3010); assert(stores==1 && g_stored.size()==1 && refreshes==2 && unregistrations==1);
    StartDelivery(r,4000); assert(spawns==1); // duplicate stored refused
    g_stored.clear(); available=false;
    StartDelivery(r,4000); assert(g_deliveries.empty());
    available=true; StartDelivery(r,5000); available=false;
    RunDeliveries(8000); assert(g_stored.empty() && g_deliveries.empty()); // no fake store success
    available=true; alive=false; StartDelivery(r,10000);
    RunDeliveries(13000); assert(g_deliveries.size()==1);
    RunDeliveries(40000); assert(g_deliveries.empty());
    r.urn[0]=999; const int before=spawns; StartDelivery(r,50000); assert(spawns==before);
}
} // namespace delivery_fixture
// catalog_win.obj references ShipIsBusy via FleetClassBusy; the fixture keeps the
// delivery-loop definition authoritative for the synthetic build.
namespace sco::game::asop_host {
bool ShipIsBusy(int ship) { return delivery_fixture::ShipIsBusy(ship); }
}
