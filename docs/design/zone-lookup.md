# Zone lookup beyond the player's chain (design note, blocked)

Status: not implemented. This note records what Core can and cannot do today, and what one new
engine fact would unlock. Nothing here was run in game.

## The need

A product's Travel page cannot reach another planet or moon. `game.spatial` 1.0
(`include/sc_game_spatial.h`) and `teleport.spatial` 1.0 (`include/sc_spatial.h`) only know zones
the player is inside, plus any streamed zone whose id the caller already holds. A product that
wants "go to Daymar" from a different planet has a name, not an id, and Core has no way to turn
that name into a zone.

## What resolves today

The rows behind `src/game/services/reads.cpp` give these zone operations, all by id or by zone
pointer:

| Operation | Input | Output |
|---|---|---|
| `ZoneFromId` | zone id | zone pointer, or 0 once the zone streamed out |
| `ZoneId` | zone pointer | zone id |
| `ZoneParent` | zone pointer | parent zone pointer |
| `ZoneName` | zone pointer | name |
| `EntityZone` | entity pointer | the zone the entity is in |
| `spawn.find_entity_by_name` | entity name | entity handle (used by the hangar and ASOP code) |

`zone_record(id)` already admits any streamed id and its full ancestry, so a caller that holds an id
can reach a zone outside the player's chain. The missing step is only getting the id.

## What was considered and why it is not enough

1. **Walk down from the system root.** Every accessor above goes from a zone to its parent. There
   is no resolved accessor that goes from a zone to its children, so a walk down cannot start.
2. **Resolve the zone through a named entity.** `find_entity_by_name` plus `EntityZone` returns
   the zone an entity is in. That is the entity's container, not the zone the entity stands for.
   For a planet entity it would most likely be the parent system zone. Nothing resolved says that
   an entity exists whose name equals a zone name, or that its zone is the named zone. The
   entity zone slot constant `kEntityOocZone` is documented as unchecked. Using it would assert a
   new engine fact. The name lookup is also documented in the code as debug-only and slow.
3. **Resolve through DataCore.** DataCore records describe content, not live zone ids. There is no
   resolved link from a record to a runtime zone id.
4. **Scan memory or guess ids.** Out of scope for Core. Ids are opaque and not derivable.

So the capability cannot be built from rows that already resolve. A guessed implementation would
look like it works for the player's own planet and fail elsewhere, which is the wrong kind of
failure. No capability was added.

## The missing engine facility

One of these, found and checked on the pinned build:

**A. Child enumeration.** A function or table walk that, given a zone pointer, yields its direct
child zones (or the streamed ones). Signature row wanted: `teleport.zone_children`, shape
`(zone) -> iterator or (zone, index) -> child`, returning zone pointers that `ZoneId` and
`ZoneName` already understand. With it, Core walks from the root of the player's ancestry down,
bounded by depth 16 and the existing 256-zone observation quota, and finds a zone by exact name.

**B. A name-to-zone lookup.** A function `(name) -> zone pointer`, the zone analogue of
`find_entity_by_name`. Row wanted: `teleport.zone_by_name`. It makes `find_zone_global` a single
call plus the existing admission of the ancestry.

Either row is a new reverse-engineered fact, so it comes from the private evidence process, not
from this repository.

## How it would be verified

1. Static: the row resolves to exactly one function on the pinned build, with the usual prologue
   and call-site checks, and fails closed on any other build.
2. Synthetic: the portable model gets tests with a fake zone tree: nested children, a cycle, a
   zone streamed out mid-walk, a duplicate name (refused as ambiguous, as `find_zone` does), a name
   of 95 bytes or more (refused), and the 256-zone quota.
3. In game, by the owner: stand on one planet, look up a moon of another planet by exact name,
   and compare the returned id with `zone_record` read after travelling there. Record the result
   as a V row. A static or synthetic pass is not game evidence.

## Contract shape when the row exists

Versioned addition, sized table, appended at the tail:

* `game.spatial` 1.1: `find_zone_global(name, out)` appended to `sc_game_spatial_v1`, with a new
  version macro `SC_GAME_SPATIAL_VERSION_1_1`, a capability `game.spatial.global_lookup` that
  defaults OFF, a contract record, an ABI test and a provenance entry.
* The result observes the unique matching streamed zone and its complete ancestry under the same
  admission rule `zone_record` uses. A zone that is not streamed in answers 0. This does not load
  an unloaded zone: loading is separate work (see `docs/game/relocation-position.md`).
* `list_zones` keeps its meaning ("zones observed so far"). Products that want the old wording can
  keep saying "places you are inside" until the capability reports on.

## Until then

Products can reach any zone whose id they already hold, and any zone in the player's own chain.
Saved spots keep working where the ids are known. Travel to another planet by name stays
unavailable, and the product should say so plainly.
