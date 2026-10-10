# Creative rows

The addresses `game.creative` uses, all of them rows that already existed for sc-offline's cheats: no row was added for the service. Rows are in [`src/game/actors_sigs.cpp`](../../src/game/actors_sigs.cpp) and [`src/game/features_sigs.cpp`](../../src/game/features_sigs.cpp), the accessor constants (fly speed displacement, god mode offsets, magazine fields) in [`include/sco/game/actors.h`](../../include/sco/game/actors.h). Checked against 4.10.196.36804. [`src/game/services/creative.cpp`](../../src/game/services/creative.cpp) is sc-offline's `spawner.cpp` (`ProcessNoclip`, `ProcessGodMode`) and `ammo.cpp`, moved with the same offsets and slots.

| Feature | Rows it reads | What it does with them |
|---|---|---|
| God mode | `teleport.entity_system`, `spawn.landing_helper` (the component type table), `spawn.god_mode_call`, `spawn.set_god_mode` | Reads your actor's god mode state byte (the displacement in `spawn.set_god_mode`) and calls the game's setter with 2 (on) or 0 (off) when it differs; held every 500 ms |
| Fly | `teleport.entity_system`, `spawn.landing_helper`, `spawn.request_fly_mode`, `spawn.actor_link` | `CSCActorActionHandler::Request<SFlyMode>` on your actor's action handler with 2 (fly) or 0 (off) |
| Fly speed | `spawn.game_cvars`, `spawn.fly_speed_scaler` | Writes the `g_FlyModeSpeedScaler` field of the game's cvar block (the capability is the actors table's `spawn.fly_speed`); the value read before the first write comes back at unload |
| Infinite ammo | `teleport.entity_system`, `spawn.handle_to_id`, `ammo.set_ammo` | A `sco::hook` detour on the magazine setter (14 stolen bytes, pinned by the row's checks): a decrease on a container within four parents of your actor is raised to the container's maximum |
| Infinite ship ammo | the ammo rows and `spawn.landing_helper` | The same detour, for containers within eight parents of the ship you're aboard, plus a direct top-up of the ship's `AmmoContainerComponent`s every 500 ms |

Capabilities: `game.creative.god_mode`, `game.creative.fly`, `game.creative.ammo`, `game.creative.ship_ammo`, each set from its rows with `caps::SetFromSignatures` (a feature is also off when the `teleport.*` reads aren't ready). What the service does not carry over from sc-offline: ship ammo for the Crew & seats target ship (the product's own selection, not reachable from a plugin). [API](../api-v1.md#gamecreative-10-game-pack), [game services](../game-services.md).
