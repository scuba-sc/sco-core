#pragma once
#include "sco/runtime.h"
#include <sc_inventory.h>
namespace sco::game::services {
// game.inventory 1.0: read-only item-port snapshot of the local player
// (docs/game/inventory.md; corpus F-4.10.196.36804-009). A host binds this backend only
// after grounding the port accessors (ports read from the entity's item-port container
// component, port GetItem/IsValid,
// item type-id descriptor) on its exact build; the unbound default publishes UNAVAILABLE.
// snapshot(ctx, io): io is always a full-size sc_inventory_snapshot owned by the service;
// io->count is the capacity in (<= SC_INVENTORY_MAX_PORTS) and the ports written out, and the
// backend sets generation, flags and total_seen. false: nothing to report (UNAVAILABLE).
struct InventoryBackend {
    void* ctx = nullptr;
    bool (*snapshot)(void* ctx, sc_inventory_snapshot* io);
};

// What the published snapshot() does with a backend: validates out, runs the backend into a
// service-owned snapshot, then copies only what the caller's out->size covers (1.0 callers pass
// SC_INVENTORY_SNAPSHOT_SIZE_1_0 and never get the 1.1 tail) and keeps flags and total_seen
// consistent with count. SCO_UNAVAILABLE for no backend, out null, out->size below the 1.0 size,
// or a backend with nothing to report. Exposed for the tests; the service wraps it.
sco_result SnapshotWith(const InventoryBackend& backend, sc_inventory_snapshot* out);
Result StartGameInventory(const InventoryBackend& backend = {});
void StopGameInventory();
}  // namespace sco::game::services
