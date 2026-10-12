# sco-core SDK 2.0.0 (release notes, draft)

Tag `sdk-v2.0.0`, zip `sco-sdk-2.0.0.zip`. Prepared on 2026-10-12; **the tag is not pushed and no GitHub release exists yet.** The release workflow publishes the CHANGELOG section `## sdk-v2.0.0` as the release's notes; this page is the longer draft behind it.

## Versions

| What | Version | Where |
|---|---|---|
| Kernel (the SDK) | 2.0.0 | `sdk/VERSION`, tag `sdk-v2.0.0` |
| Plugin ABI | 1.1, unchanged | `SCO_API_MAJOR` / `SCO_API_MINOR` in `sco_api.h` (ABI declarations/layout unchanged since sdk-v1.1.0; licensing comment revised; `tests/abi_v1.c` unchanged) |
| Star Citizen game pack | 0.3.0 (was 0.2.0) | `SCO_GAME_PACK_VERSION` in `sc_game_pack.h` |
| Verified game build | 4.10.196.36804, PE timestamp `0x6ac64e6f` | rows verified by sigcheck; in-game verification pending |

`VERSIONS.txt` in the zip states the same four lines, generated from the headers by `sdk/package.py`.

## Packaging and release-process migration

SDK 2.0.0 is a major packaging/profile and release-process change, not a plugin ABI bump. ABI 1.1 is unchanged. The prepared SDK 1.2.0 was never tagged or published; all of its changes ship here instead.

Choose `sco-sdk-core-2.0.0.zip` for neutral plugin development, overlay `sco-sdk-game-sc-0.3.0.zip` for the Star Citizen extension, or use the combined `sco-sdk-2.0.0.zip` legacy profile. `sco-sdk-core-source-2.0.0.zip` is the independently buildable kernel source kit. Keep the extension's separate metadata when overlaying it; update tooling that assumed one combined archive. See [SDK profiles](../../sdk/README.md) and [building](../building.md).

Releases now build once in `release-candidate.yml` on an exact green main commit. Publication promotes only those immutable, audited bytes after the maintainer signs a complete hash-bound approval; a green candidate is not approval and does not create a tag. See the [release checklist](../legal/release-checklist.md#releasing).

## Compatibility

Plugins built with sdk-v1.1.0 keep loading: the ABI declarations/layout in `sco_api.h` and released service tables are unchanged. The licensing comment in `sco_api.h` changed, so the header is not byte-for-byte identical. Every new table is a new header with its own pin (`tests/abi_*.c`); `game.actors` 1.1 and `game.entities` 1.1 append functions to tables that were new in this release's development, and a caller that asked for 1.0 never reads past the end it knows. Check `size` before calling a function a later minor added, as the headers say.

## What is new

**Game services** (the Star Citizen game pack, owner `game`; [game services](../game-services.md), per-table status in [status](../game/status.md)):

| Service | Version | Header |
|---|---|---|
| `teleport.spatial` | 1.0 | `sc_spatial.h` |
| `spawn.entities` | 1.2 | `sc_spawn.h` |
| `game.actors` | 1.1 | `sc_actors.h` |
| `game.vehicles` | 1.0 | `sc_vehicles.h` |
| `game.world` | 1.0 | `sc_world.h` |
| `game.entities` | 1.1 | `sc_entities.h` |
| `game.creative` | 1.0 | `sc_creative.h` |
| `game.*` events | bus | `sc_game_events.h` |

Wrappers: C++ (`scosdk/game/`), C# (`Sco.Sdk.Game`) and read-only Lua (`sco.game.*`). `teleport.spatial` and `spawn.entities` were sc-offline's built-in tables and keep their names and layouts.

**Kernel and host services:** hot reload of a target and every transitively loaded dependent (`sco.reload`, `sco::app::Reload`), with whole-group refusal for unsafe or non-reloadable members, reverse-dependency unload and forward-dependency reload ([reload contract](../plugins.md#hot-reload)); typed `[settings]` in `plugin.ini` and the `sco.settings` 1.0 service; plugins load in `requires` order, service names included; a fault is blamed on the plugin whose code faulted and a provider whose table was handed out is never unmapped; command slots are recycled on unload; `sco.net` 1.0 and `sco.ipc` 1.0 with the MIT wires `sc_net.h` and `sc_ipc.h` and the bridge layouts `sc_titanlink.h` and `sc_voxel_bridge.h`; the kernel builds and tests without the game pack.

## What is not verified, and what is off

- Every game row resolves on 4.10.196.36804 in `sco-sigcheck` (316 rows: the actor-death change removed the redundant `contracts.actor_kill` row, and the post-removal run on the same exe resolves 316/316). **None of the game services above has been run in a game session since it landed.** Tables that ran in game as sc-offline built-ins on 2026-10-09 (teleport, spawn and its mover) are the same code moved, not a verification of the game pack's copy.
- Off until an in-game check: `game.entities.watch`, `game.entities.query_radius`, `game.events.vehicle_seat` (`game.vehicle.boarded` / `exited` are reserved and never fire) and `game.actors.state`.
- Not published: the camera's field of view (blocked: nothing pins which value is on screen); the entity and normal of a ray hit and the killer of `game.player.died` (zero); an actor's maximum health (zero).
- Game patches move rows: a patch that does is a game-pack patch release (`game-sc-vX.Y`), not a new kernel version.

## Upgrading

- **Plugin authors:** no ABI migration is required; select the appropriate archive profile as described above. To use a game service, query it, check the capability (`has("game.vehicles.seats")`) and handle `SCO_UNAVAILABLE`; the Lua wrappers are read-only.
- **Products (sc-offline):** set `Platform::gameServices` to start the game services, resolve the `teleport.*` and `spawn.*` rows first, and drop your own provider of `teleport.spatial` or `spawn.entities` (the second provider of a name is refused). Register the player's own vehicles with `sco::game::services::RegisterPlayerVehicle` so plugins may move them.

## Checking the zip

```sh
sha256sum -c SHA256SUMS
unzip -p sco-sdk-2.0.0.zip sco-sdk-2.0.0/VERSIONS.txt
```

Route through the appropriate reviewer before sending.
