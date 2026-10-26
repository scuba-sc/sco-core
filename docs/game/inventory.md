# game.inventory — read-only item-port view

`sc_inventory.h` · service `game.inventory` · version 1.2 · game thread only ·
capability `game.inventory.read` (default **OFF**).

`snapshot` copies the local player's item-port set as the client sees it: per port,
`valid`, the attached item's item type id (field `item_class_id`) and the item's
`entity_id` (session-scoped, joins to `game.entities`). Read-only by contract:
the server owns items; this explains what is attached where, for UIs, gear presets,
and the `game.loadout` readback (#7).

**Status: synthetic evidence only. Not run in game.** The adapter below is built from static
analysis of the 4.10.197.2878 executable and is exercised against synthetic game objects. The
capability stays OFF until an in-game run confirms it.

## Grounding

Enumeration recipe (findings F-4.10.196.36804-009, corrected and extended on 4.10.197.2878 by
F-4.10.197.2878-011..-016; static analysis only, not run in game):

- The item ports belong to an item-port container, which is a **component** fetched from the
  local player's entity by its component type. The port range is read from that container,
  not from an accessor on the entity itself.
- The container keeps its ports as a vector of port references. The attached item's handle
  comes from a per-port getter; it is not at a fixed stride inside the vector.
- Per port: a validity check, the attached item, and the item's type descriptor, which yields
  the item type id. A port's item can carry a container of its own (a backpack, armour with
  pockets); the game's own port walk recurses into those.

Cross-checked against the game's own UnequipItem flow-node implementation: the adapter reads
the same runtime path the game uses on the player, never the authoring-time command layer.

**Item type id.** `item_class_id` holds the item's type: an ordinal of the game's item-type
enumeration (`EItemType`), which is a coarse category such as helmet, sidearm or ship. It is
not a per-class id: two items of one category (two different pistols) share a value. The
field keeps its ABI name; the docs call it the item type id. A name map for the item-type
values exists (192 values on 4.10.197.2878). Per-class item names are not yet established.

In 1.1, the 192 categories are exposed as the C/C++ enum `sc_item_type` (e.g. `SC_ITEM_TYPE_CHAR_ARMOR_HELMET` = 22,
`SC_ITEM_TYPE_FPS_WEAPON_SIDEARM` = 95, `SC_ITEM_TYPE_INVENTORY_CONTAINER` = 110, `SC_ITEM_TYPE_SHIP` = 167)
along with helper functions:
- `sc_inventory_port_item_type(const sc_inventory_port* port)`: maps a valid port's `item_class_id` to `sc_item_type`,
  returning `SC_ITEM_TYPE_UNKNOWN` (0xFFFFFFFF) if invalid or unmapped.
- `sc_inventory_item_type_name(uint32_t type)`: returns the canonical category name string (e.g. `"Char_Armor_Helmet"`),
  or `"UNDEFINED"` for unknown values.

## What a snapshot covers

The whole attached tree, in one documented, deterministic order: **depth first**. The player's
container's ports come in the container's own order; each port is immediately followed by the ports
of the container attached to its item (if any), which are followed by the next port. Empty ports
are included (`valid = 0`, other fields 0). A snapshot of a worn backpack with two items and an
empty pocket reads: the port holding the backpack, the backpack's ports, then the next port of the
player's container.

`out->count` is the capacity on input (at most 128) and the number of ports written on output. The
walk keeps counting past the capacity, so the caller learns how much it missed:

| field (1.1 tail) | meaning |
|---|---|
| `total_seen` | ports the walk found, written or not; always `>= count` |
| `flags & SC_INVENTORY_FLAG_TRUNCATED` | `total_seen > count`: more ports exist than were written |
| `flags & SC_INVENTORY_FLAG_LIMITED` | the walk's own limits cut the tree, so `total_seen` is a lower bound |

The walk's own limits: at most 8 container levels below the player's container, 256 distinct
containers and 4096 ports counted. A container reached twice (a cycle, or one attached to two
ports) is entered once. A nested container the adapter cannot read safely is skipped and the
result is flagged LIMITED. Nesting is not reported: ports come in walk order without a parent or
depth field.

The tail is written only when `out->size >= sizeof(sc_inventory_snapshot)`. A caller built against
1.0 passes `SC_INVENTORY_SNAPSHOT_SIZE_1_0` (3088) and gets exactly what it got before; the
service still accepts that size.

## Backend binding

The production adapter reads game memory itself (`src/game/services/inventory_engine.cpp`), on the
game thread, under SEH, with no allocation and no call into the container: it reads the container's
port vector in place and calls only the per-port accessors. Querying an entity that has no
item-port container answers `SCO_UNAVAILABLE`; nothing crashes.

It fails closed. The capability is on only when **every** `inventory.*` signature row is OK on
the build and the product switch is on (`SetInventoryReadEnabled`, or `SCO_INVENTORY_READ=1`).
Before every read the adapter checks: the container's type is registered; the component handle is
live; the container's and each port's virtual-function entries are the very functions the rows
anchor; the vector bounds are ordered, 8-aligned and at most 4096 ports; every pointer is plausible.
Any mismatch answers UNAVAILABLE (logged once), never a guess.

Signature rows (`inventory.*`, source `src/game/inventory_sigs.cpp`): the functions the adapter
relies on, a registration cache for the container's component type, and the item-type id → name
function, which is followed for eight known ids on every build so a reordered enumeration turns the
capability off.

`generation` is computed by the adapter, because the game has no change counter for the port set. It
is a digest of the ordered `(valid, item type id, entity id)` list and `total_seen`, compared with
the last snapshot: the first snapshot is generation 1 and it advances by one whenever the digest
differs. It resets when the service restarts.

## What is not established

- Stack counts and any quantity field: not exposed.
- Per-class item names: not established. The public category-name helpers describe item types, not per-class names.
- The implementation of the item descriptor accessor is not located; the adapter calls through the
  item's table, as the game's own node does, and has no signature row for it.
- Whether the player entity the shared player lookup returns has the layout the adapter reads (it is
  the entity the game's own node starts from): to be confirmed in game.
- Value ranges and any difference between the body's port tree and ship-owned containers.

## Follow-ups

- Containers beyond the player (ships/mounts) via the same accessor on a known entity id.
- A parent or depth field per port, if consumers need nesting (a further additive change).

## In-game checks (before the capability is turned on)

Nothing below has been run. Known gear gives known values: a worn helmet reports item type 22 on
its port, a sidearm in the holster 95, torso armour 24, a backpack 26, a mobiGlas 132. Each item's
`entity_id` equals its `game.entities` id. A backpack holding items shows its ports straight after
the backpack's own port, and the 128 cap, `total_seen` and TRUNCATED match this page. Equipping or
unequipping a piece advances `generation` and changes the values in the expected port only. A query
on an entity without a container answers UNAVAILABLE with no crash.

## C# and Lua consumers

The source SDK adds read-only inventory routes after published RC1; their presence does not
establish native backend readiness. Both prefer service 1.1, fall back to its 1.0 snapshot prefix
only when 1.1 is unavailable or not found, and require `game.inventory.read` for a snapshot.
A missing provider, incomplete or null snapshot member, disabled capability, or unloaded consumer
cannot invoke the backend. Calls run on the game thread and return copies owned by the caller.

C# [`Inventory`](../../sdk/csharp/Sco.Sdk/Game/Inventory.cs) offers `Open(plugin)` and
`Snapshot(out ScInventorySnapshot)`. `snapshot.Port(i)` returns a copied `ScInventoryPort`;
`item_class_id` and `entity_id` remain `ulong`. `ItemType` maps a valid port to `ScItemType`,
and `ItemTypeName` returns its canonical category name. Invalid ports or raw categories outside
0..191 map to `ScItemType.Unknown` / `"UNDEFINED"`, while the original raw field remains exact.
`HasDetails` is false for 1.0; `flags` and `total_seen` then carry no observation and stay zero.
The static `InventoryItemTypes.FromRaw(ulong)` and `Name(ulong)` helpers and `VerifiedBuild`
are usable without opening a service.

Lua [`sco.game.inventory`](../../sdk/docs/lua.md#scogameinventory) exposes `snapshot()`,
`item_types`, `item_type(raw)` and `item_type_name(raw)`. The static vocabulary is available whenever
the game adapter is registered, even without a provider. Snapshot ports retain the ABI field names
`item_class_id` and `entity_id`, plus `item_type` and `item_type_name`. `has_details` is false on 1.0,
and `flags` / `total_seen` are absent. Inventory uint64 values use a nonnegative Lua integer through
2^63-1 and an exact decimal string above it; category helpers accept either representation and
reject negative, fractional or out-of-range input. Unknown values, including values above uint32,
map to `item_types.UNKNOWN` without narrowing the preserved raw value.

The 192-name vocabulary in both languages is pinned to the existing public table for
**4.10.197.2878**. It adds no per-class ids, quantities, mutation routes, or in-game validation.

## Logical layout observations (1.2)

The optional 1.2 table embeds the original table at offset zero and adds
`snapshot_layout`. Query 1.2 explicitly and check both the member extent and
`game.inventory.layout`. The legacy version alias and 1.0/1.1 snapshot stay
unchanged. A table alone does not imply that an admitted producer is available.
The production host has no logical-layout producer and this capability is OFF;
the current provider evidence comes from synthetic host fixtures, not gameplay.

An observation copies a domain, layout key and bounded entries with stable
provider-supplied port keys and explicit equipment, storage or unknown roles.
Unknown state is not empty. COMPLETE with zero entries is a valid empty layout;
LIMITED or TRUNCATED is incomplete. Category ordinals do not identify individual
item classes, and a role cannot be inferred from the category. Entity IDs, source
epochs and revisions are transient observations, never durable preset identity.
The API does not move or equip anything. Consumers must validate copied data and
restrict advice to equipment roles; storage entries are layout context only.

Initialize metadata and every entry size, supply disjoint aligned buffers, and
check the result before reading output. Valid buffers are unchanged on refusal;
unknown tails are preserved. The C++ `sco::sdk::game::InventoryLayout` helper
performs the 1.2 lookup, capability and structural checks and copies only a valid
result. C# `InventoryLayout.Snapshot` and Lua `sco.game.inventory.layout()` expose validated copied observations with the same capability gate.
