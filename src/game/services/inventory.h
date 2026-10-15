#pragma once
#include "sco/runtime.h"
#include <sc_inventory.h>
namespace sco::game::services {
// game.inventory 1.0: read-only item-port snapshot of the local player
// (docs/game/inventory.md; corpus F-4.10.196.36804-009). A host binds this backend only
// after grounding the port accessors (entity vtable enumeration, port GetItem/IsValid,
// item type-id descriptor) on its exact build; the unbound default publishes UNAVAILABLE.
struct InventoryBackend {
    void* ctx = nullptr;
    bool (*snapshot)(void* ctx, sc_inventory_snapshot* io);  // io->count = capacity in, filled out
};
Result StartGameInventory(const InventoryBackend& backend = {});
void StopGameInventory();
}  // namespace sco::game::services
