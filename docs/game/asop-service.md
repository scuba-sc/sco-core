# ASOP service rows

The rows `game.asop` 1.0 ([`sc_asop.h`](../../include/sc_asop.h)) reads. All of them already existed: no row was added for the service. They are in [`src/game/asop_sigs.cpp`](../../src/game/asop_sigs.cpp), and [ASOP, hangar and ATC rows](asop.md) documents each of them. The policy (thread, self and argument checks, the capability gate, `last_error`) is in [`src/game/services/asop_service.cpp`](../../src/game/services/asop_service.cpp). The engine reads are in [`src/game/services/asop_engine.cpp`](../../src/game/services/asop_engine.cpp). Both functions are reads: the service never writes game memory, installs no hook and applies no patch.

**Both capabilities are OFF by default** (evidence level `off_pending_probe`, [status](status.md)). The rows resolve, but nobody has run these calls from a plugin in a game session. The game pack holds the capabilities off until such a run confirms them. While they are off, every function answers `SCO_UNAVAILABLE` with the reason in `last_error`, and the game is not touched. Nothing on this page has been verified in game.

| Capability | Function | Rows it reads | What it does with them |
|---|---|---|---|
| `game.asop.claim_timeout` | `claim_timeout` | `insurance.update_pending_requests`, `insurance.config` (rc5) | Reads the cached `SInsuranceComponentConfig*` (`insurance.config`, the RIP target at `update_pending_requests+0x58`), then the int32 at `kInsuranceTimeout` (`+0x48`, pinned by the `+0x9F` check): the insurance request timeout in seconds, which a ship terminal claim request waits for ([`asop.h`](../../include/sco/game/asop.h)). A null configuration pointer answers `SCO_NOT_FOUND` |
| `game.asop.shard_persisted` | `shard_persisted` | `asop.on_request_open`, `asop.shard_persisted` | Reads the `IsShardPersisted` byte that `OnRequestOpen` compares at `+0x28`, the global the shard-gate checks test before they close the terminal in single-player ([asop.md](asop.md#notes-on-the-document)); non-zero answers 1 |

The ASOP host composition, when the product runs it, shortens the claim timeout to its own value (rc5); `claim_timeout` reports whatever the game holds at the time of the call. [API header](../../include/sc_asop.h), [game services](../game-services.md).

## Not wrapped

The repository does not document a plugin-callable meaning for these rows, or the ASOP host composition is their only owner through hooks:

- **Vehicle list** (`asop.fetch_vehicles`, `asop.set_vehicle_data_list`, `asop.set_delivered_or_claimed`, `asop.set_retrieved`, `asop.set_spawned`, rc4/rc15). The rows are hook targets for the host's list handling; the arguments and the list layout are not documented as plugin values. A `list_vehicles` would need both.
- **Deliver** (`asop.rm_request_deliver`, `asop.rm_request_deliver_2`, `asop.inventory_kind`, rc3). Delivery is a remote-method handler the host settles from its own loop; its request and the inventory configuration are not documented as plugin inputs. No `request_deliver`.
- **Retrieve** (`asop.request_vehicle`, rc17) and the claim itself. The kiosk fields (`kKioskAtc`, `kKioskLocation`, `kKioskRetrieving`) are read from a kiosk object that only the terminal's own handlers receive. No `claim_insurance`.
- **Terminal open and caller** (`asop.on_request_open` as a call, `asop.client_gate`, `asop.shard_gate_open`, `asop.shard_gate_validation`, `asop.validate_caller`, `asop.game`, `asop.fleet_retrieve`). Patch sites, a raw game pointer, or call targets with undocumented arguments.
- **Probes** (`asop.provider_subscribe`, `asop.fetch_event_id`, `asop.fetch_sender`, `asop.context_sender`, `asop.mobiglas_sender`, `asop.interaction_trigger`, `asop.set_stored`). Log-only (`asop.diagnostics`).
- **Writing the claim timeout.** The ASOP host writes it; a second writer would race the host's own value.
