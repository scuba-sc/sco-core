# game.inventory — read-only item-port view

`sc_inventory.h` · service `game.inventory` · version 1.0 · game thread only ·
capability `game.inventory.read` (default **OFF**).

`snapshot` copies the local player's item-port set as the client sees it: per port,
`valid`, the attached item's item type id (field `item_class_id`) and the item's
`entity_id` (session-scoped, joins to `game.entities`). Read-only by contract:
the server owns items; this explains what is attached where, for UIs, gear presets,
and the `game.loadout` readback (#7).

## Grounding

Enumeration recipe (findings F-4.10.196.36804-007..-009, corrected on 4.10.197.2878 by
F-4.10.197.2878-011..-016; static analysis only, not run in game):

- The item ports belong to an item-port container, which is a **component** fetched from the
  local player's entity by its component type. The port range is read from that container,
  not from an accessor on the entity itself.
- The container's port vector holds references to the ports. The attached item's handle comes
  from a per-port getter; it is not at a fixed stride inside the vector.
- Per port: a validity check, the attached item, and the item's type descriptor, which yields
  the item type id.

Cross-checked against the game's own UnequipItem flow-node implementation: the adapter reads
the same runtime path the game uses on the player, never the authoring-time command layer.

**Item type id.** `item_class_id` holds the item's type: an ordinal of the game's item-type
enumeration (`EItemType`), which is a coarse category such as helmet, sidearm or ship. It is
not a per-class id: two items of one category (two different pistols) share a value. The
field keeps its ABI name; the docs call it the item type id. A name map for the item-type
values exists (192 values on 4.10.197.2878). Per-class item names are not yet established.

## Backend binding

`services::StartGameInventory(backend)`; unbound (default production) publishes
UNAVAILABLE. A host binds only with rows resolved + layout attested on its exact
build. `generation` advances on port-set change; a game-side change counter for the port
set is not yet established, so it is defined by the adapter (by comparing snapshots).
`count` in/out is capacity/filled (128 max). Whether a snapshot covers the top-level ports
only or every nested container (the game's own port walk can recurse into containers
inside attached items) is not yet settled.

## Follow-ups

- item type id -> `EItemType` name map (192 values on 4.10.197.2878): available, not
  exposed in 1.0. Per-class item names: not yet established.
- stack counts and any quantity field: not yet established; not exposed in 1.0.
- containers beyond the player (ships/mounts) via the same accessor on a known
  entity id.

No C# or Lua game.inventory routes are supported in 1.0.
