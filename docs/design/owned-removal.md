# Retained build and owned-seat removal (G9)

## Decision pending owner

Use explicit retained ownership, **not persistence across reload**. A build consumer
spawns through `game.entities`, then calls `game.owned.retain_build` instead of
legacy `game.entities.keep`. The placement remains owned and movable while the
plugin is loaded; undo/clear calls `remove_build`. Ordinary unload removes it
through the existing entities release hook. Retained placements are excluded
from the legacy alive-based ownership pruning so an unstreamed placement does
not lose its undo/removal authority. Legacy `keep` refuses retained placements;
unretained legacy keeps still relinquish authority irreversibly. Neither this
service nor receipts adopt previously kept, player, foreign or `spawn_as` IDs.
The policy is deliberately narrower than charter decision 5's alternative
across-reload handles. Saving/recreating placements is consumer policy, not
persistence of these session IDs.

## Split authority

- `retain_build`/`remove_build`: only caller-owned `game.entities` spawns marked
  retained, not the mover's union of entities/NPCs/player vehicles.
- `remove_npc`: only caller-owned `game.actors.spawn_npc` IDs.
- `remove_seat_npc`: caller supplies ship, fresh seat index and expected NPC ID.
  Ownership is checked before seat lookup. Fresh `game.vehicles.seat_occupant`
  must still match. Empty, unknown, changed, foreign and player occupants are
  refused; seat control does not grant deletion authority. Existing actor
  despawn initiates removal, without a foreign eject or vehicle-registration path.
- `receipt`/`forget`: initiation records belong to their requesting owner. Forget
  reclaims only tracking capacity; it cannot cancel or re-own an entity.

## Receipts and lifecycle

`SCO_OK` from a removal is **initiation**, not observed disappearance. A receipt
records `SC_REMOVAL_REQUESTED` permanently; there is no proven completion provider
in this change. `alive=0` also means unstreamed/unavailable/wrong-thread and is
never consumed here. Successful initiation consumes the old removal ownership;
refusal preserves it and yields ticket 0. Receipt quota is preflighted before
mutation. Retentions and receipts each have a global 1024-slot bound. Tickets
never repeat across service stop/reopen in the same process. Explicit `forget`
can recycle receipt slots but never IDs or ownership. Ordinary unload clears
bookkeeping through a release hook and initiates existing entity/actor cleanup;
restart-required retained-provider faults still skip resource hooks. No safe
native-fault recovery or successful disappearance is claimed.

Every call, including receipt inspection, is game-thread-only. Unknown/released
self is `BAD_ARG`, missing/partial/null prerequisite callbacks are `UNAVAILABLE`,
foreign/unretained IDs and stale/foreign tickets are `NOT_FOUND`, a changed seat
is `FAILED`, full bounds are `TOO_MANY`. Underlying refusals propagate. Output
tickets clear on failure. Receipt outputs require a complete 32-byte record;
short records are untouched, sufficient output payloads clear on failure.
Queries are fresh, not cached across provider withdrawal. No plugin callback or
recursive reentry is supported between fresh seat read and despawn.

## Language and package routes

C and C++ call `sc_owned_v1` directly, checking the complete function-pointer
extent and non-NULL callback before each call. `sc_owned.h` and its C ABI pins
ship only in the game-sc extension (and legacy combined profile), not core or
core-source. Existing tables and frozen `sco_api` 1.1 are unchanged.

C# game.owned is unsupported: no managed table or wrapper is claimed.
Lua game.owned is unsupported: no mutation or receipt adapter is exposed.
This explicit route choice is not permission to bypass the sandbox with commands.

## Focused evidence and owed acceptance

`python3 tools/test_g9_owned_contract.py --build-dir <scratch-build>` is the one
named G9 gate: synthetic providers through the real host registry/release hooks,
C11/C++20/short-enum and Windows-x64 header compile, exact catalogue/package
membership and source integration assertions. It runs only `test_owned_removal`.
Fixtures use synthetic IDs and refuse partial and NULL underlying methods,
foreign authority, changed seats, unavailable providers, wrong thread, released
owners and full receipt quota. They assert no alive call can mark completion.

**Not verified:** real engine removal/disappearance, retained placement movement,
Windows/MSVC runtime, actual seat unlink/removal timing, fault/process recovery,
GPU/rendering and across-reload persistence (not supported). An owner-run offline
Windows check is owed for retain/move/undo/clear/unload, delayed materialization,
seat changes and provider refusal. This composition reuses existing guarded
primitives and does not enable new unproven engine bindings or seat-event hooks.
