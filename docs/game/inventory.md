# game.inventory — read-only item-port view

`sc_inventory.h` · service `game.inventory` · version 1.0 · game thread only ·
capability `game.inventory.read` (default **OFF**).

`snapshot` copies the local player's item-port set as the client sees it: per port,
`valid`, the attached item's `item_class_id` (the game's opaque class type id) and the
item's `entity_id` (session-scoped, joins to `game.entities`). Read-only by contract:
the server owns items; this explains what is attached where, for UIs, gear presets,
and the `game.loadout` readback (#7).

## Grounding

Enumeration recipe (findings F-4.10.196.36804-007..-009): local player entity -> port range
(entity vtable +0x898, begin/end), per-port IsValid(+0x60)/GetItem(+0x40), item
type-id via GetTypeDesc(+0x6e8)+[8]. Cross-checked against the game's own UnequipItem
flow-node implementation — the adapter calls the SAME accessors the game calls
on the player, never the authoring-time command layer.

## Backend binding

`services::StartGameInventory(backend)`; unbound (default production) publishes
UNAVAILABLE. A host binds only with rows resolved + layout attested on its exact
build. `generation` advances on port-set change; `count` in/out is capacity/filled
(128 max, snapshot-scope semantics).

## Follow-ups

- type-id -> class-name map (candidate: the class descriptor's name field; unmapped
  today; ids ship opaque by design).
- stack-count fields (visibility TBD; not exposed in 1.0).
- containers beyond the player (ships/mounts) via the same accessor on a known
  entity id.

No C# or Lua game.inventory routes are supported in 1.0.
