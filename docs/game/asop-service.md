# ASOP service rows

`game.asop` reads two values that sc-offline's ship terminal (ASOP) uses: the insurance claim timeout and the single-player shard flag. Both functions are reads. The service never writes game memory, installs no hook and applies no patch.

Header: [`sc_asop.h`](../../include/sc_asop.h). Table version 1.0. Capabilities `game.asop.claim_timeout` and `game.asop.shard_persisted`, both **OFF** by default; see [status](status.md). Not run in game.

The rows already existed; none was added for the service. They are in [`src/game/asop_sigs.cpp`](../../src/game/asop_sigs.cpp) and documented in [ASOP, hangar and ATC rows](asop.md). The policy (thread, self and argument checks, the capability gate, `last_error`) is in [`src/game/services/asop_service.cpp`](../../src/game/services/asop_service.cpp). The engine reads are in [`src/game/services/asop_engine.cpp`](../../src/game/services/asop_engine.cpp).

The rows resolve, but nobody has run these calls from a plugin in a game session, so the game pack holds the capabilities off. While they are off, every function answers `SCO_UNAVAILABLE` with the reason in `last_error`, and the game is not touched.

| Capability | Function | Rows it reads | What it does with them |
|---|---|---|---|
| `game.asop.claim_timeout` | `claim_timeout` | `insurance.update_pending_requests`, `insurance.config` | Reads the cached `SInsuranceComponentConfig*` (`insurance.config`, the RIP target at `update_pending_requests+0x58`), then the int32 at `kInsuranceTimeout` (`+0x48`, pinned by the `+0x9F` check): the insurance request timeout in seconds, which a ship terminal claim request waits for ([`asop.h`](../../include/sco/game/asop.h)). A null configuration pointer answers `SCO_NOT_FOUND` |
| `game.asop.shard_persisted` | `shard_persisted` | `asop.on_request_open`, `asop.shard_persisted` | Reads the `IsShardPersisted` byte that `OnRequestOpen` compares at `+0x28`, the global the shard-gate checks test before they close the terminal in single-player ([shard-gate rows](asop.md#shard-gate-rows)); non-zero answers 1 |

The ASOP host composition, when the product runs it, shortens the claim timeout to its own value; `claim_timeout` reports whatever the game holds at the time of the call. [API header](../../include/sc_asop.h), [game services](../game-services.md).

## Not wrapped

The repository does not document a plugin-callable meaning for these rows, or the ASOP host composition is their only owner through hooks:

- **Vehicle list** (`asop.fetch_vehicles`, `asop.set_vehicle_data_list`, `asop.set_delivered_or_claimed`, `asop.set_retrieved`, `asop.set_spawned`). The rows are hook targets for the host's list handling; the arguments and the list layout are not documented as plugin values. A `list_vehicles` would need both.
- **Deliver** (`asop.rm_request_deliver`, `asop.rm_request_deliver_2`, `asop.inventory_kind`). Delivery is a remote-method handler the host settles from its own loop; its request and the inventory configuration are not documented as plugin inputs. No `request_deliver`.
- **Retrieve** (`asop.request_vehicle`) and the claim itself. The kiosk fields (`kKioskAtc`, `kKioskLocation`, `kKioskRetrieving`) are read from a kiosk object that only the terminal's own handlers receive. No `claim_insurance`.
- **Terminal open and caller** (`asop.on_request_open` as a call, `asop.client_gate`, `asop.shard_gate_open`, `asop.shard_gate_validation`, `asop.validate_caller`, `asop.game`, `asop.fleet_retrieve`). Patch sites, a raw game pointer, or call targets with undocumented arguments.
- **Probes** (`asop.provider_subscribe`, `asop.fetch_event_id`, `asop.fetch_sender`, `asop.context_sender`, `asop.mobiglas_sender`, `asop.interaction_trigger`, `asop.set_stored`). Log-only (`asop.diagnostics`).
- **Writing the claim timeout.** The ASOP host writes it; a second writer would race the host's own value.
