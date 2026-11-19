# Typed offline local-player loadout (`game.loadout` 1.3)

`game.loadout` equips items on the local player. `apply` equips synchronously. From 1.2, `submit`, `status` and `cancel` equip asynchronously, because native item creation and equip requests finish later.

Header: [`sc_loadout.h`](../../include/sc_loadout.h). Table version 1.3 (`SC_LOADOUT_VERSION_1_3`, `0x00010003`). `SC_LOADOUT_SERVICE_VERSION` stays 1.0, the synchronous prefix. One published table answers 1.0 to 1.3 queries.

Capabilities, all OFF by default: `game.loadout.apply`, `game.loadout.weapons`, `game.loadout.async`, `game.loadout.outfit` and the event capability `game.events.loadout_respawn_completed`. Each is described below and listed in [status](status.md). Not run in game.

## Requests and reports

The service is a separate size-prefixed table. The frozen `sco_api` 1.1 is unchanged. Requests and reports start with `size` and need the full v1 extent. The item and result records are frozen for 1.x.

- 1..16 items, with the exact `item_stride`.
- Unique numeric slots.
- `item_class`: NUL-terminated ASCII `[A-Za-z0-9_]`, 1..95 bytes.
- Reserved fields zero.

There is no XML, file or path input, raw game pointer, account inventory, entitlement, telemetry or network operation.

## Slots

Slot 0 is unassigned. Slots 1..7 are logical slots, not verified engine port names. An unknown numeric slot is valid input and is reported UNSUPPORTED without an equip call. A known slot can also be UNSUPPORTED if the adapter does not handle it. Outfit lists, spoiler confirmation and labels are consumer policy.

Version 1.1 added slots 8..21 and 1.3 added slots 22..31. The record shape is frozen, so each number encodes its parent weapon or port. The last column is the consumer-side meaning, not a verified engine port name. Values 32 and above are unassigned and stay UNSUPPORTED. Published numbers never change, so later slots can only be appended.

| Value | Name | Parent weapon, port or item port |
|---|---|---|
| 1 | `SC_LOADOUT_SLOT_HEAD` | logical head |
| 2 | `SC_LOADOUT_SLOT_TORSO` | logical torso |
| 3 | `SC_LOADOUT_SLOT_ARMS` | logical arms |
| 4 | `SC_LOADOUT_SLOT_LEGS` | logical legs |
| 5 | `SC_LOADOUT_SLOT_UNDERSUIT` | logical undersuit |
| 6 | `SC_LOADOUT_SLOT_BACKPACK` | logical backpack |
| 7 | `SC_LOADOUT_SLOT_VISOR` | logical visor |
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
| 22 | `SC_LOADOUT_SLOT_CLOTHING_TORSO_0` | `Clothing_Torso_0` |
| 23 | `SC_LOADOUT_SLOT_CLOTHING_TORSO_1` | `Clothing_Torso_1` |
| 24 | `SC_LOADOUT_SLOT_CLOTHING_TORSO_2` | `Clothing_Torso2` |
| 25 | `SC_LOADOUT_SLOT_CLOTHING_LEGS` | `Clothing_Legs` |
| 26 | `SC_LOADOUT_SLOT_CLOTHING_FEET` | `Clothing_Feet` |
| 27 | `SC_LOADOUT_SLOT_CLOTHING_HANDS` | `Clothing_Hands` |
| 28 | `SC_LOADOUT_SLOT_HAT` | `Hat_ItemPort` |
| 29 | `SC_LOADOUT_SLOT_HEAD_ACCESSORY` | `Head_Accessory_ItemPort` |
| 30 | `SC_LOADOUT_SLOT_HEAD_HORN` | `Head_Horn_ItemPort` |
| 31 | `SC_LOADOUT_SLOT_JEWELLERY` | `Jewellery_ItemPort` |

Rules for a batch:

- The batch is processed in input order. An attachment or tool slot targets whatever item occupies its parent port when that slot is processed, so put 8 and 9 before 10..21 in the same batch.
- Replacing a weapon (8 or 9) does not carry attachments over. What the new weapon spawns with is engine-defined, and the report claims nothing about it.
- There is no removal in 1.x. `item_class` is at least 1 byte, so "clear this slot" cannot be expressed, and unspecified slots stay unchanged.
- A multitool variant swap is consumer policy: send 8 or 9 with the multitool class plus the module slot (16 or 19) in the same batch. The provider has no special rule for it.
- The 16-item cap fits every realistic request. Seven body slots, two weapons and one triple per weapon make 15. All ten outfit slots fit in one request with room for 6 more.

There is no slot for hair, hair colour, eyebrow, head, body, eyes or teeth. These items overwrite the character's look or are parts other items are built to fit. 1.x has no removal, so a wrong item cannot be undone. A slot can be appended later once an in-game run shows a safe swap.

## Synchronous `apply`

C calls the full-extent, non-null `apply` member directly. It is game-thread only (command, tick or queued game-thread task). Caller data lives until return. The route uses bounded stack copies and keeps no input, work queue or cancellation. It handles slots 1..21. Outfit slots 22..31 are UNSUPPORTED with no equip call.

Status per slot:

| Case | Status | Engine call |
|---|---|---|
| Slot 0 or above 21 | UNSUPPORTED | no |
| Host provides 1.0 only (range ends at VISOR) | UNSUPPORTED | no |
| Backend does not declare weapon support (slots 8..21) | UNSUPPORTED | no |
| `game.loadout.weapons` OFF while body apply is ON | UNAVAILABLE | no |
| Parent port empty, or the parent lacks that port (optic on a multitool, module on a firearm) | UNSUPPORTED | no mutation |
| Class not found | ITEM_MISSING | lookup only |
| Engine refuses, fit mismatch, or the confirmation read differs | FAILED | yes |

Structural errors reject the whole batch before any equip: BAD_ARG (null, foreign or released `self`, short size, bad stride or name, duplicates) or TOO_MANY above 16. The report stays unchanged on structural errors and WRONG_THREAD.

A valid request gets input-ordered copied results and an applied count. OK means every slot is APPLIED. FAILED means a partial or wholly refused processed batch. ITEM_MISSING skips that slot and continues. Only a verified adapter may report APPLIED, and only after confirming its operation. There is no rollback or duplication guard, so do not retry applied slots blindly. Changes are player state, not plugin-owned entities, and stay when a consumer unloads.

An absent service query returns NOT_FOUND. A missing or unverified backend, missing method, revoked capability, or unknown, online or EAC-present admission returns UNAVAILABLE with all slots UNAVAILABLE and no equip call. Admission is checked before each supported slot. If it is lost mid-batch, earlier applied slots stay and later slots are UNAVAILABLE (aggregate FAILED). The report is the synchronous completion of each adapter call, not a streaming ticket or an inventory snapshot.

A recursive `apply` returns UNAVAILABLE with no nested engine call. Backend Start is refused and Stop is ignored during `apply`.

## Asynchronous `submit`, `status`, `cancel` (1.2)

Query `SC_LOADOUT_VERSION_1_2` and check each full pointer extent before using the `sc_loadout_v1_2` tail: `submit(self, request, out_job)`, `status(self, job, out_report)`, `cancel(self, job)`. C++ has `Loadout::Submit`, `Status` and `Cancel`. C# and Lua async mutation are unsupported.

- `submit` validates and copies 1..16 distinct slots, then returns an owner-scoped, process-unique job id. It does not call an equip function while borrowing the request.
- Each plugin may retain one job. At most eight jobs exist in total.
- Game-thread ticks process one slot at a time in input order. A later weapon attachment sees the previously completed parent weapon.
- `status` returns a completed prefix and per-slot results. It never advances a job or mutates the engine. PENDING is never reported as APPLIED.
- COMPLETE means processing finished, including per-slot refusals. Check the applied count and every slot result.
- A job ends as FAILED after 120 seconds, on a changed local-player session, on loss of admission, or on a provider fault. There is no retry or rollback.
- `cancel` forgets the job and releases unfinished native resources. Equipment already confirmed stays equipped. Plugin unload and host stop cancel retained jobs.
- Unknown, online or EAC-present admission refuses before any mutation.

The Windows async adapter is opt-in with `SCO_LOADOUT_ASYNC=1`, and it configures only when all required signature rows resolve. It does this per slot:

1. It uses the local spawn boundary and waits for that exact entity to become live.
2. It calls the typed port attachment once.
3. It confirms the same entity in the same owned port on a later tick.

The parent may not change while a spawn is pending. Port walks are bounded and reject duplicate names. The visor has no grounded separate port and returns UNSUPPORTED. There is no XML loader or inventory-service fallback.

Cancelling removes a spawned item that is not yet attached. Once an attachment was attempted, the uncertain state is kept rather than deleting possible equipment. Prior equipment is never removed or rolled back.

## Capabilities

| Capability | Gates | Default | Switch |
|---|---|---|---|
| `game.loadout.apply` | `apply` (slots 1..7) | OFF | A verified host backend. Production installs none. |
| `game.loadout.weapons` | slots 8..21 of `apply` | OFF | A backend that declares weapon support. |
| `game.loadout.async` | `submit`, `status`, `cancel` | OFF | `SCO_LOADOUT_ASYNC=1` and all signature rows resolved. |
| `game.loadout.outfit` | slots 22..31 of `submit` | OFF | The async route plus `SCO_LOADOUT_OUTFIT=1`. |

`game.loadout.outfit` is on only while the async route is installed and its adapter reports an outfit name mapping. The Windows adapter reports it only when `SCO_LOADOUT_OUTFIT=1` is set as well as `SCO_LOADOUT_ASYNC=1`. The port names are consumer-side data, not verified engine rows. The capability goes off on a provider fault and on Stop.

### Outfit slots (1.3)

Outfit slots 22..31 behave like this:

- `submit` refuses the whole request with UNAVAILABLE when it holds any slot in 22..31 and `game.loadout.outfit` is off. No job starts, so no outfit is half applied. BAD_ARG for malformed input or duplicate slots comes first. The refusal comes before the job-capacity check, so UNAVAILABLE can win over TOO_MANY.
- If the capability is revoked after a job was accepted, that job's outfit slots end UNAVAILABLE with no engine call. Its other slots proceed.
- Each slot is a lookup by name through the same bounded port search as slots 8..21. A missing port gives UNSUPPORTED. A missing item class gives ITEM_MISSING. APPLIED needs the same readback as every other slot.
- In 1.2, ids 22..31 were unassigned, and `submit` accepted them and ended the slot UNSUPPORTED. In 1.3 it refuses the request while the capability is off.

Production game-service startup does not currently publish this table (see [status](status.md)). A fixture or service started without a synchronous backend answers UNAVAILABLE for every `apply` slot.

## Thread, lifetime and faults

The host owns publication and withdraws the table after consumers unload. Do not call a borrowed table after terminal shutdown.

`LoadoutBackend` is game-pack and host code, not a plugin installation API. It supplies verified typed equip and live, fail-closed offline admission. Production passes no synchronous backend. The async route uses the separate `LoadoutAsyncBackend` and retains copied job state. Its callbacks live through Stop.

Guards catch C++ exceptions. On Windows, MSVC guard frames also use SEH. A callback fault or invalid callback result disables both mutation routes until process restart, even after Stop and Start. Uncertain native resources are retained and no more callbacks run. The guards do not promise recovery from native memory corruption or attribution outside the host loader guard.

## Respawn-completed event (`game.loadout.respawn_completed`)

An additive notification on the kernel event bus. `sc_loadout_v1` keeps its layout and version. Capability `game.events.loadout_respawn_completed` is OFF in production until a verified host adapter attests the producer. An event whose capability is off never fires, so subscribing is harmless.

- Name: `SC_GAME_EVENT_LOADOUT_RESPAWN_COMPLETED`, defined identically (guarded) in `sc_loadout.h` and `sc_game_events.h`. A plugin that copies only `sc_loadout.h` can subscribe by name with `sco_api` `subscribe`.
- Payload `sc_game_loadout_respawn_completed` (`sc_game_events.h`, 8 bytes): `size` and `_pad` only. It reports that a respawn completed. It names no entity, zone, slot or item and is not a per-slot confirmation. Use the `apply` or `status` reports for that. Check `size` before reading any field a later minor appends.
- Callbacks run on the game thread from the host tick, a frame or so after the occurrence. Occurrences recorded within one tick coalesce into one event. The pointer is valid only during the callback.
- The payload has no fields because the only evidence is a type name, `SPI_Player_OnRespawnLoadoutCompleted`, declared as an empty marker struct. Where the game raises it, its calling convention and what "completed" means for a given player are unverified. No native hook exists.

Host seam (`src/game/services/loadout.h`, never exposed to plugins):

- `LoadoutBackend::respawnCompleted` is an attestation by a verified adapter. It needs `verified` and a live `offline` admission check.
- `RecordRespawnLoadoutCompleted()` records one occurrence from the adapter's hook, from any thread, and runs no plugin code. It returns false and records nothing unless the attested backend is started and the capability is on.
- The host dispatches from its `tick` subscription on the game thread after re-checking admission. A refused occurrence is dropped, not replayed.
- A callback fault disables the event together with `apply` until process restart. Stop clears a pending occurrence, withdraws the tick subscription and turns the capability off.
- Production game-service startup refuses before `StartLoadout()` is reached, so the event never fires. A service started without a backend also leaves the capability off.

## Language and package routes

C++20 uses `scosdk/game/loadout.hpp` (`Loadout::Open/Apply`), which rejects truncated pointer tails and null callbacks and clears failed reopen state. C# is unsupported for loadout 1.0. Lua mutation is unsupported. The header, wrapper, docs and ABI fixture are in the `game-sc` and combined legacy profiles, never in the neutral core or core-source archive.

Repository provider tests compile an independent C native module from copied public headers, load it through the real host loader and call the game-pack provider with synthetic engine callbacks. A Windows synthetic-object fixture runs the async adapter, including delayed spawn, exact attachment arguments, confirmation, a missing class, admission-loss cleanup, and per-slot port lookup for each outfit slot. These are not Windows DLL, renderer, engine or gameplay checks.

## What an in-game run still has to show

- The exact per-slot engine calls, item-existence checking and local-player selection. Do not infer per-slot status from the old all-at-once XML loader.
- The build-specific slot, outfit and visor mapping, with no duplication or corruption, and the no-player and missing-item behavior.
- Current offline and EAC admission from the game-pack composition (unknown refuses), and calling conventions and SEH under MSVC.
- Where the game raises the respawn-completed occurrence, its calling convention, and its timing relative to the equip becoming visible, before any adapter sets `respawnCompleted`.
- Weapon and multitool slots 8..21 and outfit slots 22..31 each need their own evidence before `game.loadout.weapons` or `game.loadout.outfit` can default ON. Port mappings in the slot table stay unverified until then.

No arbitrary console, XML loader or account inventory fallback is part of this contract.
