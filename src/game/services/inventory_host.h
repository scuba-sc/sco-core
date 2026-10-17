#pragma once
// game.inventory 1.1 in the game pack: binds the item-port adapter (inventory_engine.cpp) to the
// service (inventory.cpp) and holds the capability game.inventory.read OFF by default
// (docs/game/inventory.md). Game thread.
#include "sco/runtime.h"

namespace sco::game::services {

// Publishes game.inventory with the production backend bound. The capability is on only when every
// inventory.* row is OK on this build AND the product switch is on (SetInventoryReadEnabled,
// default off; SCO_INVENTORY_READ=1 in the environment turns it on). Unready or off, the service
// still answers SCO_UNAVAILABLE. Never fails the pack: the result is the publish result.
Result StartInventory();
void   StopInventory();

// The product switch. Takes effect at the next snapshot. Any thread.
void SetInventoryReadEnabled(bool enabled);
bool InventoryReadEnabled();

}  // namespace sco::game::services
