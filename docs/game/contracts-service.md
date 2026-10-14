# Contracts service rows

The rows `game.contracts` 1.0 ([`sc_contracts.h`](../../include/sc_contracts.h)) reads. All of them already existed: no row was added for the service. They are in [`src/game/contracts_sigs.cpp`](../../src/game/contracts_sigs.cpp), and [contracts rows](contracts.md) documents each of them. The policy (thread, self and argument checks, the capability gate, `last_error`) is in [`src/game/services/contracts_service.cpp`](../../src/game/services/contracts_service.cpp). The engine read is in [`src/game/services/contracts_engine.cpp`](../../src/game/services/contracts_engine.cpp). The function is a read: the service never writes game memory, installs no hook and applies no patch.

The service name `game.contracts` is new; nothing else in the repository used it. The host-internal capabilities `contracts.*` ([`sco/game/contracts.h`](../../include/sco/game/contracts.h)) group rows for sc-offline and are a different namespace. The offline mission backend is the separate `game.missions` service ([`sc_missions.h`](../../include/sc_missions.h)).

**The capability is OFF by default** (evidence level `off_pending_probe`, [status](status.md)). The row resolves, but nobody has run this call from a plugin in a game session. The game pack holds the capability off until such a run confirms it. While it is off, the function answers `SCO_UNAVAILABLE` with the reason in `last_error`, and the game is not touched. Nothing on this page has been verified in game.

| Capability | Function | Rows it reads | What it does with them |
|---|---|---|---|
| `game.contracts.stream_radius` | `stream_radius` | `contracts.mission_settings` | Reads the mission settings pointer the row's pattern loads (`mov rax, [rip+X]`, RIP at +3), then the 32-bit float at `kStreamRadius` (`+0x174`, pinned by the pattern's `vmovss xmm1, [rax+0x174]`): the mission stream radius sc-offline writes (the `contracts.stream_radius` capability, [`contracts.h`](../../include/sco/game/contracts.h)). A null settings pointer answers `SCO_NOT_FOUND`. The value is returned as the game holds it; the repository documents no unit |

A product that runs offline contracts may write the stream radius; `stream_radius` reports whatever the game holds at the time of the call. [API header](../../include/sc_contracts.h), [game services](../game-services.md).

## Not wrapped

The repository does not document a plugin-callable meaning for these rows, or sc-offline is their only owner through hooks and patches:

- **Contract list and accept** (`contracts.query_reply`, `contracts.mission_system*`, `contracts.mission_generator_get`, `contracts.accept_*`, `contracts.auto_accept_caller.*`). Hook and vtable-slot targets; the mission system's fields past the generator and the broker's offer layout are listed as unchecked runtime data.
- **Mission creation, log, modules and phases** (`contracts.factory_create` .. `contracts.activate_token`). Functions sc-offline calls with objects it builds; their argument layouts are unchecked (contracts.md "Offsets sc-offline uses that no row checks").
- **Offline service, abandon, phase ends** (`contracts.mission_entity_vtbl`, `contracts.offline_*`). Vtable slots sc-offline replaces or calls.
- **Rewards and payments** (`contracts.send_rewards_authority`, `contracts.find_entry*`, `contracts.total_reward`, `contracts.*update_balance`). A patch site and calls into the wallet with undocumented arguments.
- **Shard store** (`contracts.shard_store_*`, `contracts.get_shard_graph`, `contracts.dump_shard_graph`). A raw game pointer and an offset read from code, with no documented plugin value.
- **Writing the stream radius.** sc-offline writes it; a second writer would race the product's own value.
