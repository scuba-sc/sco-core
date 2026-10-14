# Hangar rows

The rows `game.hangar` 1.0 ([`sc_hangar.h`](../../include/sc_hangar.h)) uses. All of them already existed: no row was added for the service. The hangar, lift and ATC rows are in [`src/game/hangar_sigs.cpp`](../../src/game/hangar_sigs.cpp), [`src/game/atc_sigs.cpp`](../../src/game/atc_sigs.cpp) and [`src/game/asop_sigs.cpp`](../../src/game/asop_sigs.cpp), and [ASOP, hangar and ATC rows](asop.md) documents each of them. The entity lookups use the `spawn.*` and `teleport.*` rows that other game services already read. The policy (thread, self and argument checks, the capability gate, `last_error`) is in [`src/game/services/hangar.cpp`](../../src/game/services/hangar.cpp). The engine calls are in [`src/game/services/hangar_engine.cpp`](../../src/game/services/hangar_engine.cpp), which makes exactly the calls that the ASOP host composition's `fleet_win.cpp` already makes. It installs no hook and writes no patch.

**Both capabilities are OFF by default** (evidence level `off_pending_probe`, [status](status.md)). The rows resolve, but nobody has run these calls from a plugin in a game session. The game pack holds the capabilities off until such a run confirms them. While they are off, every function answers `SCO_UNAVAILABLE` with the reason in `last_error`, and the game is not touched. Nothing on this page has been verified in game.

| Capability | Function | Rows it reads | What it does with them |
|---|---|---|---|
| `game.hangar.lift` | `lift_of_landing_area` | `teleport.entity_system`, `spawn.find_entity_by_name`, `spawn.handle_to_id` (and the `teleport.*` reads) | The ASOP host's own lift lookup: landing area `LandingArea_X` goes with the ship lift `LoadingPlatformManager_X`, which is accepted only when it is in the landing area's zone |
| `game.hangar.lift` | `lift_request` | `lift.open_request`, `lift.close_request` (rc13) | Checks that the entity is a ship lift (`LoadingPlatformManager_*`), then calls the game's lift open request or lift close request on it, as the ASOP host does when it raises or lowers a lift for Store and Retrieve |
| `game.hangar.atc_cancel` | `atc_cancel_request` | `atc.get_component`, `atc.request_cancel` (rc6), `spawn.handle_to_id` (and the `teleport.*` reads) | Gets the ATC entity's data manager through the game's ATC component getter, then calls the game's `RequestCancel` for the local player. The ASOP host makes the same call to cancel a Retrieve that the ATC can't finish |

Not wrapped, because the repository doesn't document a plugin-callable meaning for them, or because the ASOP host composition is their only owner:

- **Hangar instance state** (`hangar.instance`, rc7-rc11: the instance request, the IIM continuations, `QueueInstance`, `TearDownConditionsMet`, the streaming bubble). The ASOP host owns these through hooks and a synchronous hand-off. Nothing records a state that a plugin could read without those hooks.
- **Lift state.** The lift's state field is read from the component that the lift manager's close handler sees. The ASOP host must be that handler's sole owner (`lift.manager_close_handler`), so a second reader would need a second hook.
- **ATC store and retrieve** (`atc.store_vehicle`, `atc.store_token_lambda`, `atc.process_request`, `atc.queue_spawn_ship`, `atc.on_vehicle_spawn_cancelled`). Store needs the kiosk's location and the personal inventory configuration. Retrieve needs a request that the game builds. Both are ASOP host flows (rc19, rc6) whose inputs are not documented as plugin values.
- **Pad tokens** (`atc.change_token_state`, `atc.clear_token`, `landing.on_reserving_entity_changed`) and the log-only probes (`asop.diagnostics`).

The ASOP host composition, when the product runs it, drives the same lifts. A lift request from a plugin is not coordinated with its Store or Retrieve. [API header](../../include/sc_hangar.h), [game services](../game-services.md).
