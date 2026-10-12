// Synthetic providers only. Real host registry/release and game.owned composition.
#include "sco/host.h"
#include "sco/runtime.h"
#include "sc_owned.h"
#include "sc_entities.h"
#include "sc_actors.h"
#include "sc_vehicles.h"
#include "../src/game/services/owned.h"
#include <cstdio>
#include <map>
#include <thread>
using sco::Result;
static int passed = 0, failed = 0;
#define CHECK(x) do { if (x) ++passed; else { ++failed; std::printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)
static std::map<uint64_t, const void*> builds, npcs;
static unsigned build_calls = 0, npc_calls = 0, seat_calls = 0, alive_calls = 0;
static uint64_t occupant = 0;
static sco_result removal_result = SCO_OK, seat_result = SCO_OK;
namespace sco::game::services {
bool EntitiesOwnedBy(const void* owner, uint64_t id) { return builds.count(id) && builds[id] == owner; }
bool NpcOwnedBy(const void* owner, uint64_t id) { return npcs.count(id) && npcs[id] == owner; }
}
static int Alive(uint64_t) { ++alive_calls; return 0; }
static sco_result DespawnBuild(sco_plugin* self, uint64_t id) {
    ++build_calls;
    if (removal_result != SCO_OK) return removal_result;
    if (!sco::game::services::EntitiesOwnedBy(self, id)) return SCO_NOT_FOUND;
    builds.erase(id); return SCO_OK;
}
static sco_result DespawnNpc(sco_plugin* self, uint64_t id) {
    ++npc_calls;
    if (removal_result != SCO_OK) return removal_result;
    if (!sco::game::services::NpcOwnedBy(self, id)) return SCO_NOT_FOUND;
    npcs.erase(id); return SCO_OK;
}
static sco_result Occupant(uint64_t, uint32_t, uint64_t* out) {
    ++seat_calls; *out = occupant; return seat_result;
}
static void EngineRelease(const void* self) {
    for (auto i = builds.begin(); i != builds.end();) {
        if (i->second == self) { ++build_calls; i = builds.erase(i); } else ++i;
    }
    for (auto i = npcs.begin(); i != npcs.end();) {
        if (i->second == self) { ++npc_calls; i = npcs.erase(i); } else ++i;
    }
}
int main() {
    sco::SetGameThread();
    const auto* api = sco::host::BuildApi({"synthetic-owned-test"});
    auto* self = sco::host::NewPlugin("build_test");
    auto* foreign = sco::host::NewPlugin("foreign_test");
    CHECK(api && self && foreign);
    CHECK(sco::AddReleaseHook(EngineRelease) == Result::Ok);
    sc_entities_v1 e{}; e.size = sizeof(e); e.alive = Alive; e.despawn = DespawnBuild;
    sc_actors_v1 a{}; a.size = sizeof(a); a.despawn = DespawnNpc;
    sc_vehicles_v1 v{}; v.size = sizeof(v); v.seat_occupant = Occupant;
    CHECK(sco::host::ProvideGameService(SC_ENTITIES_NAME, SC_ENTITIES_VERSION_1_1, &e) == Result::Ok);
    CHECK(sco::host::ProvideGameService(SC_ACTORS_NAME, SC_ACTORS_VERSION_1_1, &a) == Result::Ok);
    CHECK(sco::host::ProvideGameService(SC_VEHICLES_SERVICE_NAME, SC_VEHICLES_SERVICE_VERSION, &v) == Result::Ok);
    CHECK(sco::game::services::StartOwned() == Result::Ok);
    const void* raw = nullptr;
    CHECK(api->query_service(SC_OWNED_NAME, SC_OWNED_VERSION_1_0, &raw) == SCO_OK);
    if (!raw) return 1;
    const auto* o = static_cast<const sc_owned_v1*>(raw);
    CHECK(o->size == sizeof(*o));
    builds[101] = self; builds[102] = foreign; npcs[201] = self; npcs[202] = foreign;
    CHECK(o->retain_build(self, 101) == SCO_OK);
    CHECK(o->retain_build(self, 101) == SCO_OK);
    CHECK(sco::game::services::BuildRetainedBy(self, 101));
    CHECK(o->retain_build(self, 102) == SCO_NOT_FOUND);
    CHECK(o->retain_build(self, 201) == SCO_NOT_FOUND); // actors != entities
    CHECK(o->retain_build(nullptr, 101) == SCO_BAD_ARG);
    uint64_t ticket = 9;
    CHECK(o->remove_build(foreign, 101, &ticket) == SCO_NOT_FOUND && ticket == 0);
    CHECK(o->remove_npc(self, 101, &ticket) == SCO_NOT_FOUND && ticket == 0);
    removal_result = SCO_UNAVAILABLE;
    CHECK(o->remove_build(self, 101, &ticket) == SCO_UNAVAILABLE && ticket == 0);
    CHECK(sco::game::services::BuildRetainedBy(self, 101));
    removal_result = SCO_OK;
    e.size = offsetof(sc_entities_v1, despawn) + sizeof(e.despawn) - 1;
    CHECK(o->remove_build(self, 101, &ticket) == SCO_UNAVAILABLE && ticket == 0);
    e.size = sizeof(e); e.despawn = nullptr;
    CHECK(o->remove_build(self, 101, &ticket) == SCO_UNAVAILABLE && ticket == 0);
    e.despawn = DespawnBuild;
    CHECK(o->remove_build(self, 101, &ticket) == SCO_OK && ticket != 0);
    const auto first = ticket;
    CHECK(!sco::game::services::BuildRetainedBy(self, 101));
    sc_removal_receipt receipt{sizeof(receipt), 9, 9, 9, 9, 9};
    CHECK(o->receipt(self, first, &receipt) == SCO_OK);
    CHECK(receipt.state == SC_REMOVAL_REQUESTED && receipt.entity_id == 101 && receipt.kind == SC_REMOVAL_BUILD);
    CHECK(o->receipt(foreign, first, &receipt) == SCO_NOT_FOUND && receipt.ticket == 0);
    receipt.size = sizeof(receipt) - 1; receipt.ticket = 77;
    CHECK(o->receipt(self, first, &receipt) == SCO_BAD_ARG && receipt.ticket == 77);
    receipt.size = sizeof(receipt);
    CHECK(o->remove_build(self, 101, &ticket) == SCO_NOT_FOUND && ticket == 0);
    std::thread worker([&] {
        CHECK(o->retain_build(self, 101) == SCO_WRONG_THREAD);
        CHECK(o->remove_npc(self, 201, &ticket) == SCO_WRONG_THREAD && ticket == 0);
        CHECK(o->receipt(self, first, &receipt) == SCO_WRONG_THREAD && receipt.ticket == 0);
        CHECK(o->forget(self, first) == SCO_WRONG_THREAD);
    }); worker.join();
    const auto before = npc_calls;
    occupant = 202;
    CHECK(o->remove_seat_npc(self, 301, 0, 202, &ticket) == SCO_NOT_FOUND && ticket == 0);
    CHECK(o->remove_seat_npc(self, 301, 0, 201, &ticket) == SCO_FAILED && ticket == 0);
    occupant = 0; // empty/unknown must not count as success
    CHECK(o->remove_seat_npc(self, 301, 0, 201, &ticket) == SCO_FAILED && ticket == 0);
    occupant = 201; seat_result = SCO_FAILED;
    CHECK(o->remove_seat_npc(self, 301, 0, 201, &ticket) == SCO_FAILED && ticket == 0);
    seat_result = SCO_OK;
    v.size = offsetof(sc_vehicles_v1, seat_occupant) + sizeof(v.seat_occupant) - 1;
    CHECK(o->remove_seat_npc(self, 301, 0, 201, &ticket) == SCO_UNAVAILABLE);
    v.size = sizeof(v); v.seat_occupant = nullptr;
    CHECK(o->remove_seat_npc(self, 301, 0, 201, &ticket) == SCO_UNAVAILABLE);
    v.seat_occupant = Occupant;
    a.size = offsetof(sc_actors_v1, despawn) + sizeof(a.despawn) - 1;
    CHECK(o->remove_npc(self, 201, &ticket) == SCO_UNAVAILABLE);
    a.size = sizeof(a); a.despawn = nullptr;
    CHECK(o->remove_npc(self, 201, &ticket) == SCO_UNAVAILABLE);
    a.despawn = DespawnNpc;
    CHECK(npc_calls == before);
    CHECK(o->remove_seat_npc(self, 301, 0, 201, &ticket) == SCO_OK);
    CHECK(o->receipt(self, ticket, &receipt) == SCO_OK && receipt.kind == SC_REMOVAL_SEAT_NPC && receipt.state == SC_REMOVAL_REQUESTED);
    CHECK(alive_calls == 0); // No alive=0 completion shortcut, including pre-stream removals.
    CHECK(o->forget(foreign, ticket) == SCO_NOT_FOUND);
    CHECK(o->forget(self, ticket) == SCO_OK);
    CHECK(o->receipt(self, ticket, &receipt) == SCO_NOT_FOUND);
    CHECK(sco::host::WithdrawGameService(SC_ACTORS_NAME) == Result::Ok);
    npcs[203] = self;
    CHECK(o->remove_npc(self, 203, &ticket) == SCO_UNAVAILABLE && ticket == 0);
    CHECK(sco::host::ProvideGameService(SC_ACTORS_NAME, SC_ACTORS_VERSION_1_1, &a) == Result::Ok);
    // Fill bounded initiation receipts. Full quota must not call despawn or discard authority.
    for (uint64_t i = 0; i < SC_OWNED_MAX_RECEIPTS - 1; ++i) {
        npcs[1000 + i] = self;
        CHECK(o->remove_npc(self, 1000 + i, &ticket) == SCO_OK);
    }
    const auto calls = npc_calls;
    CHECK(o->remove_npc(self, 203, &ticket) == SCO_TOO_MANY && ticket == 0 && npc_calls == calls);
    CHECK(o->forget(self, first) == SCO_OK);
    CHECK(o->remove_npc(self, 203, &ticket) == SCO_OK && ticket > first);
    builds[103] = self;
    CHECK(o->retain_build(self, 103) == SCO_OK);
    CHECK(sco::Release(self) == Result::Ok);
    CHECK(!builds.count(103) && !sco::game::services::BuildRetainedBy(self, 103));
    CHECK(builds.count(102) && npcs.count(202)); // never removes foreign builds/NPCs
    CHECK(o->receipt(self, ticket, &receipt) == SCO_BAD_ARG);
    CHECK(o->remove_npc(self, 202, &ticket) == SCO_BAD_ARG && ticket == 0);
    CHECK(sco::Release(self) == Result::BadArg);
    sco::game::services::StopOwned();
    CHECK(api->query_service(SC_OWNED_NAME, SC_OWNED_VERSION_1_0, &raw) == SCO_NOT_FOUND);
    CHECK(sco::game::services::StartOwned() == Result::Ok);
    npcs[204] = foreign;
    CHECK(o->remove_npc(foreign, 204, &ticket) == SCO_OK && ticket > first);
    CHECK(o->receipt(foreign, first, &receipt) == SCO_NOT_FOUND);
    sco::game::services::StopOwned();
    sco::RemoveReleaseHook(EngineRelease);
    std::printf("owned_removal: %d passed, %d failed (synthetic providers, not engine acceptance)\n", passed, failed);
    return failed ? 1 : 0;
}
