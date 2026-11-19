# ASOP terminals, ship lifts, hangars and ATC: signature rows

These are the signature rows that sc-offline's ship terminal (ASOP), hangar and ATC modules use. This page is a row reference, not a service table, so it has no table version. The services that read these rows are `game.asop` and `game.hangar`; their capabilities and defaults are in [status](status.md). Not run in game.

The rows were written against build 4.10.196.36804 (PE timestamp `0x6ac64e6f`) and are checked by `sco-sigcheck` on every build. The RVAs below are for that build. Each pattern and in-function guard matches the game's bytes exactly. Checks marked **added** pin a field offset that the feature relies on.

- Tables: [`src/game/asop_sigs.cpp`](../../src/game/asop_sigs.cpp) (terminal, list, deliver, retrieve, insurance, the capability table), [`src/game/atc_sigs.cpp`](../../src/game/atc_sigs.cpp), [`src/game/hangar_sigs.cpp`](../../src/game/hangar_sigs.cpp).
- Header: [`include/sco/game/asop.h`](../../include/sco/game/asop.h) defines `asop::Capabilities()` and the offsets and slots that the checks pin.
- Helper: [`src/game/sig_rows.h`](../../src/game/sig_rows.h). `ResolveFn<spec>` handles "unique pattern plus byte checks at offsets", and `ResolveRip<spec>` handles "RIP target of an instruction in another row".

Resolve before patching. The rows match the game's original bytes. A hook's `jmp` over a prologue, or the `OnRequestOpen` patch at `+0x95E`, can make a later resolution report the row as `FAILED`. An installed host registry is sealed; resolving another image requires a fresh registry.

## Capability groups

A feature calls `sco::caps::SetFromSignatures(c.name, c.rows, c.count)` for its capability and runs only when that capability is ready. One broken group disables only its own feature. A row can appear in several groups.

| Capability | Rows |
|---|---|
| `asop.terminal` | `asop.on_request_open`, `asop.client_gate`, `asop.shard_gate_open`, `asop.shard_gate_validation`, `asop.validate_caller` |
| `asop.caller` | `asop.rm_request_deliver`, `asop.game` |
| `asop.list` | `asop.fetch_vehicles`, `asop.set_vehicle_data_list`, `asop.set_delivered_or_claimed`, `asop.set_retrieved`, `asop.set_spawned` |
| `asop.deliver` | `asop.rm_request_deliver`, `asop.rm_request_deliver_2`, `asop.game`, `asop.inventory_kind`, `atc.store_vehicle`, `asop.fleet_retrieve`, `atc.get_component` |
| `asop.claim_timeout` | `insurance.update_pending_requests`, `insurance.config`, `asop.fetch_vehicles` |
| `asop.retrieve` | `asop.request_vehicle`, `atc.process_request`, `atc.queue_spawn_ship`, `atc.request_cancel`, `atc.on_vehicle_spawn_cancelled`, `asop.fleet_retrieve`, `atc.get_component`, `landing.unstow_result`, `landing.unstow_success`, `respawn.on_vehicle_spawned`, `asop.set_retrieved`, `asop.set_spawned`, `asop.set_delivered_or_claimed` |
| `hangar.lift` | `lift.spawn_on_platform`, `lift.close_request`, `lift.open_request`, `lift.manager_close_handler` |
| `atc.store` | `atc.store_token_lambda`, `atc.store_vehicle`, `asop.fleet_retrieve`, `atc.get_component`, `lift.close_request`, `lift.open_request`, `lift.manager_close_handler` |
| `hangar.instance` | `hangar.services_hub_user`, `hangar.services_hub`, `hangar.request_instance`, `hangar.iim_find_hangars`, `hangar.iim_unstow_hangar`, `hangar.iim_creation_batch`, `hangar.entity_system`, `hangar.iim_type_id`, `hangar.request_permissions`, `hangar.queue_instance`, `hangar.teardown_conditions_met`, `hangar.bubble_update_instance`, `hangar.bubble_map_find` |
| `atc.tokens` | `atc.change_token_state`, `atc.clear_token`, `landing.on_reserving_entity_changed` |
| `asop.diagnostics` (log only) | `asop.shard_persisted`, `asop.provider_subscribe`, `asop.fetch_event_id`, `asop.fetch_sender`, `asop.context_sender`, `asop.mobiglas_sender`, `asop.interaction_trigger`, `asop.set_stored`, `atc.request_taking_off`, `atc.rm_request_permission`, `atc.on_permission_requested`, `atc.notify_player`, `atc.rm_multicast_notify_player`, `hangar.permissions_continuation` |

Retrieve also uses the lift. A feature that does a retrieve should require both `asop.retrieve` and `hangar.lift`.

## Rows

RVA is the address in 4.10.196.36804 as reported by `sco-sigcheck`. A blank Checks cell means the row has no check beyond its own pattern.

| Row | What it is | RVA | Checks |
|---|---|---|---|
| `asop.on_request_open` | OnRequestOpen | `0x4C25A50` | `+0x28` `44 38 2D rel32`; `+0x189` `48 8B 9E F8 09 00 00` (**added**, kiosk `+0x9F8`); `+0x616` `48 8B BD D8 00 00 00`; `+0x958` `89 BE E8 09 00 00` (**added**, kiosk `+0x9E8`); `+0x95E` `E9 rel32` with target `+0xDC0`; `+0x963` `80 3D rel32 00` |
| `asop.shard_persisted` | `IsShardPersisted` global | `0x9E3FD6F` | RIP of `on_request_open+0x28` |
| `asop.client_gate` | client gate global | `0x9E3FD65` | RIP of `on_request_open+0x963` |
| `asop.shard_gate_open` | ASOP shard gate site 1 | `0x4C25A71` | must be `on_request_open+0x21` |
| `asop.shard_gate_validation` | ASOP shard gate site 2 | `0x4C2CE7E` | its `.pdata` function must be `asop.validate_caller` |
| `asop.validate_caller` | remote-method validation | `0x4C2CD60` | call target at `rm_request_deliver+0xC3` |
| `asop.fetch_vehicles` | OnRequestFetchVehicles | `0x56C9860` | |
| `asop.set_vehicle_data_list` | SetVehicleDataList | `0x56DF9F0` | |
| `asop.set_delivered_or_claimed` | SetVehicleDeliveredOrClaimedInformations | `0x56DFC10` | |
| `asop.set_retrieved` | SetVehicleRetrievedInformations | `0x56E0420` | |
| `asop.set_spawned` | SetVehicleSpawnedInformations | `0x56E06F0` | |
| `asop.rm_request_deliver` | RmAuthorityRequestDeliver | `0x4C38620` | `+0x2C` `48 8B 0D`; `+0x3F` `48 8B 01 FF 90 A0 00 00 00` (slot `0xA0` pinned); `+0xC3` `E8`; `+0xEA` `FF 90 C0 01 00 00`; `+0xF7` `48 8D 95 10 01 00 00`; `+0x105` `C6 44 24 20 ??`; `+0x10D` `4C 8B 51 08`; `+0x117` `44 38 AD 40 01 00 00` |
| `asop.game` | `gEnv->pGame` | `0x9E3F800` | RIP of `rm_request_deliver+0x2C` |
| `asop.inventory_kind` | inventory kind imm8 | `0x4C38729` | the byte at `rm_request_deliver+0x109` (`0x81` in 4.10.196) |
| `asop.rm_request_deliver_2` | Deliver, second handler | `0x4C38B80` | |
| `asop.fleet_retrieve` | Fleet manager retrieve | `0x4C354F0` | `+0xC7` `49 8B 95 F8 09 00 00`; `+0xDD` `E8` |
| `atc.get_component` | ATC component getter | `0x2D4EBF0` | call target at `fleet_retrieve+0xDD` |
| `asop.request_vehicle` | RequestVehicle | `0x4C35E50` | `+0x2F` `49 8B 96 F8 09 00 00`; **added** `+0x12F` `4D 8B 86 28 0A 00 00`, `+0x1A4` `49 8B 8E 28 0A 00 00`, `+0x5CE` `49 89 BE 28 0A 00 00` (kiosk `+0xA28`) |
| `insurance.update_pending_requests` | UpdatePendingRequests | `0x46DED20` | `+0x58` `48 8B 05 rel32`; `+0x9F` `8B 70 48` (config `+0x48`) |
| `insurance.config` | cached `SInsuranceComponentConfig*` | `0xA224430` | RIP of `update_pending_requests+0x58` |
| `asop.provider_subscribe` | Provider subscribe | `0x56C3600` | `+0x83` `8B 05 rel32` |
| `asop.fetch_event_id` | fetch event id global | `0xA223588` | RIP of `provider_subscribe+0x83` |
| `asop.fetch_sender` | Client half's fetch sender | `0x462D2D0` | |
| `asop.context_sender` | Terminal context sender | `0x4651F90` | |
| `asop.mobiglas_sender` | mobiGlas sender | `0x4D208F0` | |
| `asop.interaction_trigger` | Interaction trigger | `0x53B82B0` | |
| `asop.set_stored` | SetVehicleStoredInformations | `0x56E1000` | |
| `atc.store_vehicle` | StoreVehicle (ATC data manager) | `0x5217A60` | |
| `atc.store_token_lambda` | Store handler's token lambda | `0x505CEA0` | `+0x12` `4C 8B 02`; `+0x1F` `49 39 40 10` |
| `atc.process_request` | ATC request processing | `0x5110AA0` | |
| `atc.queue_spawn_ship` | QueueSpawnShip | `0x51C9630` | |
| `atc.request_cancel` | RequestCancel | `0x51E6640` | |
| `atc.on_vehicle_spawn_cancelled` | OnVehicleSpawnCancelled | `0x519CA30` | `+0x1C` `4C 8B E9 48 8B F2` |
| `atc.change_token_state` | ChangeTokenState | `0x50BCB10` | |
| `atc.clear_token` | ClearToken | `0x50D27E0` | |
| `atc.request_taking_off` | RequestTakingOff | `0x51EBD20` | |
| `atc.rm_request_permission` | RmAuthorityRequestPermission | `0x51F59D0` | |
| `atc.on_permission_requested` | OnPermissionRequested | `0x5191DD0` | |
| `atc.notify_player` | NotifyPlayer | `0x517E6F0` | |
| `atc.rm_multicast_notify_player` | RmMulticastNotifyPlayer, client half | `0x51FD220` | |
| `hangar.services_hub_user` | Services hub user | `0x7266790` | `+0x26` `48 8B 0D rel32` |
| `hangar.services_hub` | services hub global (`gEnv + 0x188`) | `0x9E3F8E8` | RIP of `services_hub_user+0x26` |
| `hangar.request_instance` | RequestInstanceImpl, found by string | `0x3A417F0` | the function (`.pdata`) of every `lea r8` (`4C 8D 05`) that references `"IIM_RequestInstanceImpl_Requesting"` (two in 4.10.196, at `+0xA9` and `+0x2A5`); all references must be in the same function; prologue `48 89 5C 24 10 48 89 4C 24 08 55 56 57` |
| `hangar.iim_find_hangars` | IIM continuation: find hangars | `0x3962E80` | |
| `hangar.iim_unstow_hangar` | IIM continuation: unstow hangar | `0x39691B0` | |
| `hangar.iim_creation_batch` | IIM continuation: creation batch | `0x396D170` | `+0x25` `48 8B 0D`; `+0x53` `0F B7 0D`; `+0xBE`, `+0xF4`, `+0x10C` `E8` |
| `hangar.entity_system` | entity system global (`gEnv + 0xA8`) | `0x9E3F808` | RIP of `iim_creation_batch+0x25` (equals `teleport.entity_system`) |
| `hangar.iim_type_id` | IIM component type id | `0x9C5B8C4` | RIP of `iim_creation_batch+0x53` |
| `hangar.request_permissions` | RequestPlayerHangarPermissions | `0x3A437F0` | |
| `hangar.permissions_continuation` | Permissions continuation | `0x39672A0` | |
| `hangar.queue_instance` | QueueInstance | `0x3A22D60` | |
| `hangar.teardown_conditions_met` | TearDownConditionsMet | `0x3A572D0` | |
| `hangar.bubble_update_instance` | Streaming bubble: update instance | `0x15EC470` | `+0x31` `E8` |
| `hangar.bubble_map_find` | PrimaryBubbleMap find | `0xD1FE70` | call target at `bubble_update_instance+0x31` |
| `lift.spawn_on_platform` | SvRequestSpawnVehicleInLoadingPlatform | `0x170FC40` | `+0x59` `4C 8D 0D` |
| `lift.close_request` | Lift close request | `0x170E140` | `+0xBC` `FF 90 80 03 00 00` |
| `lift.open_request` | Lift open request | `0x170F4C0` | `+0x3B` `E8` |
| `lift.manager_close_handler` | Loading platform manager close handler | `0x514B200` | |
| `landing.unstow_result` | Landing area unstow result handler | `0x5E635F0` | |
| `landing.unstow_success` | unstow success path (**added**) | `0x5ED5010` | prologue `48 8B C4 4C 89 40 18 55 41 54 41 55 48 8D A8 F8 FD FF FF 48 81 EC F0 02 00 00` (unique); `+0x2220` `49 89 BD 18 03 00 00` (landing area `+0x318`, the deferred unstow vehicle id) |
| `landing.on_reserving_entity_changed` | OnReservingEntityChanged | `0x5EDC710` | |
| `respawn.on_vehicle_spawned` | CPlayerShipRespawnManager::OnVehicleSpawned | `0x519D900` | |

Pinned values are exported as constants in `asop.h`: `kCallerLookupSlot` (`0xA0`), `kInventoryConfigSlot` (`0x1C0`), `kInventoryLookupSlot` (`0x08`), `kKioskAtc` (`0x9F8`), `kKioskLocation` (`0x9E8`), `kKioskRetrieving` (`0xA28`), `kInsuranceTimeout` (`0x48`), `kDeferredUnstow` (`0x318`), `kLiftSendEventSlot` (`0x380`) and the `OnRequestOpen` patch offsets. While the row is `OK`, the game uses exactly these values.

## Shard-gate rows

`asop.shard_gate_open` and `asop.shard_gate_validation` locate the two `IsShardPersisted` checks that close the terminal in single-player. They are a game-mode gate, not an online service or an integrity check.

The `asop.shard_persisted` global is also read by `game.asop.shard_persisted` ([ASOP service rows](asop-service.md)).
