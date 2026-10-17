# Typed offline local-player loadout (`game.loadout` 1.1)

**Synthetic/native-boundary checked; not run in game.** Production publishes the
table when Windows game services start, but `game.loadout.apply` stays **false**.
The game pack has no verified typed per-slot equip implementation yet. Signature
resolution or a successful query is not permission to mutate a player.

Version 1.1 adds slot vocabulary only: weapon and multitool slots 8..21 (see
[Slots](#slots)). Records are unchanged, `SC_LOADOUT_SERVICE_VERSION` stays 1.0 while
1.1 is published (`SC_LOADOUT_VERSION_1_1`), and a 1.0 query is still answered. The new
capability `game.loadout.weapons` defaults **OFF**, and production still installs no
backend, so every slot, body or weapon, answers UNAVAILABLE in game.

## Inputs and outputs

`sc_loadout.h` adds a separate size-prefixed service; frozen `sco_api` 1.1 is
unchanged. Requests and reports also start with size, requiring the full v1 extent.
The item/result records are frozen for 1.x. Supply exact `item_stride`, 1..16 items,
unique numeric slots and NUL-terminated ASCII `[A-Za-z0-9_]` class names (1..95 bytes).
Reserved fields are zero. No XML, file/path ingestion, raw game pointers, account
inventory, entitlements, telemetry or network operations are exposed.

## Slots

HEAD, TORSO, ARMS, LEGS, UNDERSUIT, BACKPACK and VISOR (1..7) identify *logical* slots,
not verified engine port names. Unknown numeric slots are valid input, reported
UNSUPPORTED without an equip call. Known slots can also be UNSUPPORTED by the adapter.
List/outfit parsing, spoiler confirmation and labels remain consumer policy.

Version 1.1 adds slots 8..21. The record shape is frozen, so each number encodes its
parent weapon. The port column is the consumer-side logical meaning, not a verified
engine port name.

| Value | Name | Parent weapon / port |
|---|---|---|
| 8 | `SC_LOADOUT_SLOT_PRIMARY` | the primary weapon itself |
| 9 | `SC_LOADOUT_SLOT_SIDEARM` | the sidearm itself |
| 10 | `SC_LOADOUT_SLOT_PRIMARY_OPTIC` | primary, `optics_attach` |
| 11 | `SC_LOADOUT_SLOT_PRIMARY_BARREL` | primary, `barrel_attach` |
| 12 | `SC_LOADOUT_SLOT_PRIMARY_UNDERBARREL` | primary, `underbarrel_attach` |
| 13 | `SC_LOADOUT_SLOT_SIDEARM_OPTIC` | sidearm, `optics_attach` |
| 14 | `SC_LOADOUT_SLOT_SIDEARM_BARREL` | sidearm, `barrel_attach` |
| 15 | `SC_LOADOUT_SLOT_SIDEARM_UNDERBARREL` | sidearm, `underbarrel_attach` |
| 16 | `SC_LOADOUT_SLOT_PRIMARY_TOOL_MODULE` | primary multitool, `module_attach` |
| 17 | `SC_LOADOUT_SLOT_PRIMARY_TOOL_CANISTER` | primary multitool, `canister_attach` |
| 18 | `SC_LOADOUT_SLOT_PRIMARY_TOOL_BATTERY` | primary multitool, `magazine_attach` |
| 19 | `SC_LOADOUT_SLOT_SIDEARM_TOOL_MODULE` | sidearm multitool, `module_attach` |
| 20 | `SC_LOADOUT_SLOT_SIDEARM_TOOL_CANISTER` | sidearm multitool, `canister_attach` |
| 21 | `SC_LOADOUT_SLOT_SIDEARM_TOOL_BATTERY` | sidearm multitool, `magazine_attach` |

Values 22 and above are unassigned and stay UNSUPPORTED. Numbers are frozen once
published, so later slots can only be appended.

Semantics:

- The batch is processed in input order. An attachment or tool slot targets whatever
  item occupies its parent port when that slot is processed, so put 8 and 9 before
  10..21 in the same batch.
- Replacing a weapon (8 or 9) does not carry attachments over. What the new weapon
  spawns with is engine-defined, and the report claims nothing about it.
- There is **no removal** in 1.x. `item_class` is 1..95 bytes, so "clear this slot" can't
  be expressed, and unspecified slots stay unchanged. A clear or detach operation is a
  2.x or separate-method question.
- A multitool variant swap is consumer policy: send 8 or 9 with the multitool class plus
  the module slot (16 or 19) in the same batch. The provider has no special rule for it.
- The 16-item cap still fits every realistic request: 7 body slots, 2 weapons, and one
  triple per weapon is 15.

Status behaviour for 8..21:

| Case | Status | Engine call |
|---|---|---|
| Slot 0 or above 21 | UNSUPPORTED | no |
| Host provides 1.0 only (range ends at VISOR) | UNSUPPORTED | no |
| Backend doesn't declare weapon support | UNSUPPORTED | no |
| `game.loadout.weapons` OFF while body apply is ON | UNAVAILABLE | no |
| Parent port empty, or the parent lacks that port (optic on a multitool, module on a firearm) | UNSUPPORTED | no mutation |
| Class not found | ITEM_MISSING | lookup only |
| Engine refuses, fit mismatch, or the confirmation read differs | FAILED | yes |

Structural errors reject the whole batch before any equip: BAD_ARG (including null,
foreign/released self, short size, bad stride/name, duplicates), or TOO_MANY above 16.
Report stays unchanged on structural errors and WRONG_THREAD. Valid requests receive
input-ordered copied results and applied count. OK means every slot APPLIED; FAILED
means a partial or wholly refused processed batch. ITEM_MISSING skips that slot and
continues. No rollback or duplication-prevention claim is made: only a verified
adapter may report APPLIED after confirming its operation. Consumers must not retry
already applied slots blindly. Successful changes are player state, not plugin-owned
entities, and remain on ordinary consumer unload.

Absent service queries return NOT_FOUND. Missing/unverified backend, missing method,
revoked capability or unknown/online/EAC-present admission returns UNAVAILABLE with
all slots UNAVAILABLE and zero engine equip calls. Admission is checked before each
supported slot; loss mid-batch preserves earlier applied slots and leaves later slots
UNAVAILABLE (aggregate FAILED). Report count/status is synchronous completion of
each adapter call, not a streaming ticket or global inventory snapshot.

## Thread, lifetime and faults

Game thread only (command/tick/queued game-thread task); caller data lives until
return. Bounded stack copies only, no retained input, work queue or cancellation.
The host owns publication and withdraws after consumers unload. Borrowed tables must
not be called after terminal shutdown. Recursive apply returns UNAVAILABLE without
a nested engine call; backend Start is refused and Stop ignored during apply.

Internal `LoadoutBackend` is game-pack/host code, **not a public plugin installation
API**. It supplies verified typed equip and live fail-closed offline admission;
current production calls `StartLoadout()` without it. Callbacks live through Stop.
Portable guards catch C++ exceptions. Windows MSVC guard frames additionally use
SEH, not locally exercised. A callback fault or invalid callback result disables
apply for the rest of the process; restart is required even after Stop/Start.
This does not guarantee native memory corruption recovery or callback attribution
outside the host loader guard.

## Respawn-completed event (`game.loadout.respawn_completed`)

Additive notification on the kernel event bus; `sc_loadout_v1` keeps its layout and
version (no table member). **OFF in production, not run in game.** Capability
`game.events.loadout_respawn_completed` is false until a verified host adapter attests
the producer; an event whose capability is off never fires, so subscribing is harmless.

- Name: `SC_GAME_EVENT_LOADOUT_RESPAWN_COMPLETED`, defined (identically, guarded) in both
  `sc_loadout.h` and `sc_game_events.h`. A plugin that copies only `sc_loadout.h` can
  subscribe by name with `sco_api` `subscribe`; the independent native fixture does.
- Payload `sc_game_loadout_respawn_completed` (`sc_game_events.h`, 8 bytes): `size` and
  `_pad` only. **Occurrence only.** It names no entity, zone, slot or item and is not a
  per-slot confirmation; use `apply` reports for those. Check `size` before reading any
  field a later minor appends.
- Callbacks run on the game thread from the host tick, a frame or so after the occurrence.
  Occurrences recorded within one tick coalesce into one event. The pointer is valid only
  during the callback.
- Why no fields: the only evidence is the type name `SPI_Player_OnRespawnLoadoutCompleted`,
  whose declaration in the intake type export is an empty marker struct (finding
  F-INTAKE-004; the export's provenance and build identity are unverified). Where the game
  raises it, its calling convention and what "completed" means for a given player are
  unverified (T-3). No native hook exists in this change.
- Host seam (`src/game/services/loadout.h`, never exposed to plugins):
  `LoadoutBackend::respawnCompleted` is an attestation by a verified adapter and needs
  `verified` and a live `offline` admission check. `RecordRespawnLoadoutCompleted()` records
  one occurrence from the adapter's hook, from any thread, running no plugin code; it
  returns false and records nothing unless the attested backend is started and the
  capability is on. The host dispatches from its `tick` subscription on the game thread
  after re-checking admission; a refused occurrence is dropped, not replayed. A callback
  fault disables the event with `apply` until process restart; Stop clears a pending
  occurrence, withdraws the tick subscription and turns the capability off.
- Production calls `StartLoadout()` with no backend, so the capability stays off and the
  event never fires.

## Language and package routes

C calls the full-extent, non-null `apply` member directly. C++20 uses
`scosdk/game/loadout.hpp` (`Loadout::Open/Apply`), which rejects complete-pointer-tail
truncation/null callbacks and clears failed reopen state. C# is unsupported for loadout 1.0;
Lua mutation is unsupported (no FFI/binding or read-only inventory route).
Header, wrapper, docs and ABI fixture are in `game-sc` and combined legacy profiles,
never the neutral core/core-source archive. Repository-only provider tests compile
an independent C native module from copied public headers, load it through the real
host loader and invoke the actual game-pack provider with synthetic engine callbacks.
These are not Windows DLL, renderer, engine or gameplay acceptance.

## Pending engine acceptance (G6 / charter 4A)

- Identify exact supported per-slot engine calls, item-existence checking and local
  player selection; do not infer individual status from the old all-at-once XML loader.
- Verify build-specific slot/outfit/head/visor mapping, no duplication/corruption,
  no-player and missing-item behavior on an owner-approved offline Windows build.
- Supply current session offline/EAC admission from game-pack composition (unknown
  refuses), and verify calling conventions/SEH with MSVC.
- Verify where the game raises the respawn-completed occurrence (F-INTAKE-004, T-3), its
  calling convention and timing relative to the equip being visible, before any adapter
  sets `respawnCompleted`.
- Only then install/attest the typed backend and enable its capability. Consumer
  extraction/engine activation and charter gameplay acceptance remain blocked.

**Owner decision (approved):** logical slots were extended for the sc-offline #127
consumer need (weapon attachments and multitool slots 8..21). The port mappings in the
slot table are still unverified, and each new slot needs its own in-game evidence
before `game.loadout.weapons` can default ON. There is no removal in 1.x. No arbitrary
console, XML loader or account inventory fallback is authorized by this contract.
