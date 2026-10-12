// Only game-pack composition, with real registry/lifecycle and existing despawn primitives.
#include "owned.h"
#include "actors.h"
#include "../../api/internal.h"
#include "sco/host.h"
#include "sc_owned.h"
#include "sc_entities.h"
#include "sc_actors.h"
#include "sc_vehicles.h"
#include <cstddef>
#include <limits>

namespace sco::game::services {
namespace {
struct Placement { const void* owner{}; uint64_t id{}; };
struct Receipt { const void* owner{}; sc_removal_receipt value{}; };
Placement placements[SC_OWNED_MAX_RETAINED];
Receipt receipts[SC_OWNED_MAX_RECEIPTS];
uint64_t nextTicket = 1; // Never reset: a receipt cannot alias after service stop/reopen.
bool started = false;
sco_result Admit(sco_plugin* self) {
    if (!OnGameThread()) return SCO_WRONG_THREAD;
    if (!self || !host::PluginId(self) || detail::Released(self)) return SCO_BAD_ARG;
    return started ? SCO_OK : SCO_UNAVAILABLE;
}
template<class T> const T* Query(const char* name, uint32_t version) {
    const void* raw = nullptr;
    return QueryService(name, version, &raw) == Result::Ok ? static_cast<const T*>(raw) : nullptr;
}
#define HAS(t, type, member) ((t) && (t)->size >= offsetof(type, member) + sizeof((t)->member) && (t)->member)
Placement* Find(const void* owner, uint64_t id) {
    for (auto& p : placements) if (p.owner == owner && p.id == id) return &p;
    return nullptr;
}
void ReleaseOwner(const void* owner) {
    // Existing entity/actor release hooks remove spawns. This hook only drops bookkeeping.
    for (auto& p : placements) if (p.owner == owner) p = {};
    for (auto& r : receipts) if (r.owner == owner) r = {};
}
sco_result Retain(sco_plugin* self, uint64_t id) {
    if (const auto r = Admit(self); r != SCO_OK) return r;
    if (!id) return SCO_BAD_ARG;
    const auto* e = Query<sc_entities_v1>(SC_ENTITIES_NAME, SC_ENTITIES_VERSION_1_0);
    if (!HAS(e, sc_entities_v1, despawn)) return SCO_UNAVAILABLE;
    if (!EntitiesOwnedBy(self, id)) return SCO_NOT_FOUND;
    if (Find(self, id)) return SCO_OK;
    for (auto& p : placements) if (!p.owner) { p = {self, id}; return SCO_OK; }
    return SCO_TOO_MANY;
}
sco_result Remove(sco_plugin* self, uint64_t id, uint64_t* ticket, uint32_t kind,
                  uint64_t ship = 0, uint32_t seat = 0) {
    if (ticket) *ticket = 0;
    if (const auto r = Admit(self); r != SCO_OK) return r;
    if (!id || !ticket || (kind == SC_REMOVAL_SEAT_NPC && !ship)) return SCO_BAD_ARG;
    auto* placement = kind == SC_REMOVAL_BUILD ? Find(self, id) : nullptr;
    if (kind == SC_REMOVAL_BUILD) {
        if (!placement || !EntitiesOwnedBy(self, id)) return SCO_NOT_FOUND;
    } else if (!NpcOwnedBy(self, id)) return SCO_NOT_FOUND;
    Receipt* slot = nullptr;
    for (auto& r : receipts) if (!r.owner) { slot = &r; break; }
    if (!slot || nextTicket == std::numeric_limits<uint64_t>::max()) return SCO_TOO_MANY;
    // Capacity is reserved before calling a mutator. Underlying errors consume no receipt.
    sco_result result;
    if (kind == SC_REMOVAL_BUILD) {
        const auto* e = Query<sc_entities_v1>(SC_ENTITIES_NAME, SC_ENTITIES_VERSION_1_0);
        if (!HAS(e, sc_entities_v1, despawn)) return SCO_UNAVAILABLE;
        result = e->despawn(self, id);
    } else {
        const auto* a = Query<sc_actors_v1>(SC_ACTORS_NAME, SC_ACTORS_VERSION_1_0);
        if (!HAS(a, sc_actors_v1, despawn)) return SCO_UNAVAILABLE;
        if (kind == SC_REMOVAL_SEAT_NPC) {
            const auto* v = Query<sc_vehicles_v1>(SC_VEHICLES_SERVICE_NAME, SC_VEHICLES_SERVICE_VERSION);
            if (!HAS(v, sc_vehicles_v1, seat_occupant)) return SCO_UNAVAILABLE;
            uint64_t occupant = 0;
            const auto r = v->seat_occupant(ship, seat, &occupant);
            if (r != SCO_OK) return r;
            if (occupant != id) return SCO_FAILED;
        }
        // Game-thread-only, no user callbacks/reentry between the fresh seat read and despawn.
        result = a->despawn(self, id);
    }
    if (result != SCO_OK) return result;
    slot->owner = self;
    slot->value = {sizeof(sc_removal_receipt), SC_REMOVAL_REQUESTED, nextTicket++, id, kind, 0};
    *ticket = slot->value.ticket;
    if (placement) *placement = {};
    return SCO_OK;
}
sco_result RemoveBuild(sco_plugin* self, uint64_t id, uint64_t* ticket) {
    return Remove(self, id, ticket, SC_REMOVAL_BUILD);
}
sco_result RemoveNpc(sco_plugin* self, uint64_t id, uint64_t* ticket) {
    return Remove(self, id, ticket, SC_REMOVAL_NPC);
}
sco_result RemoveSeatNpc(sco_plugin* self, uint64_t ship, uint32_t seat, uint64_t id, uint64_t* ticket) {
    return Remove(self, id, ticket, SC_REMOVAL_SEAT_NPC, ship, seat);
}
sco_result ReceiptOf(sco_plugin* self, uint64_t ticket, sc_removal_receipt* out) {
    if (!out || out->size < sizeof(*out)) return SCO_BAD_ARG;
    *out = {sizeof(*out), 0, 0, 0, 0, 0};
    if (const auto r = Admit(self); r != SCO_OK) return r;
    if (!ticket) return SCO_BAD_ARG;
    for (const auto& r : receipts) if (r.owner == self && r.value.ticket == ticket) {
        *out = r.value; return SCO_OK;
    }
    return SCO_NOT_FOUND;
}
sco_result Forget(sco_plugin* self, uint64_t ticket) {
    if (const auto r = Admit(self); r != SCO_OK) return r;
    if (!ticket) return SCO_BAD_ARG;
    for (auto& r : receipts) if (r.owner == self && r.value.ticket == ticket) { r = {}; return SCO_OK; }
    return SCO_NOT_FOUND;
}
const sc_owned_v1 service = {sizeof(sc_owned_v1), 0, Retain, RemoveBuild, RemoveNpc, RemoveSeatNpc, ReceiptOf, Forget};
#undef HAS
}
bool BuildRetainedBy(const void* owner, uint64_t id) { return Find(owner, id) != nullptr; }
void DropBuildRetention(const void* owner, uint64_t id) { if (auto* p = Find(owner, id)) *p = {}; }
Result StartOwned() {
    if (started) return Result::Ok;
    auto r = AddReleaseHook(ReleaseOwner);
    if (r != Result::Ok) return r;
    r = host::ProvideGameService(SC_OWNED_NAME, SC_OWNED_VERSION_1_0, &service);
    if (r != Result::Ok) { RemoveReleaseHook(ReleaseOwner); return r; }
    started = true;
    return Result::Ok;
}
void StopOwned() {
    if (!started) return;
    host::WithdrawGameService(SC_OWNED_NAME);
    RemoveReleaseHook(ReleaseOwner);
    for (auto& p : placements) p = {};
    for (auto& r : receipts) r = {};
    started = false;
}
}
