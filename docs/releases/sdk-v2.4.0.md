# sco-core SDK 2.4.0 (release notes, unpublished candidate)

Tag `sdk-v2.4.0`, combined zip `sco-sdk-2.4.0.zip`. **Status: unpublished candidate, draft for the maintainer.**
No tag, signature, approval record, candidate run or release exists for 2.4.0, and nothing on this
page authorizes one. The scope below is a proposal the maintainer edits or rejects. It is written
against `main` at `c27aa8a`, which contains #196, #185 and #197 ([Landed contracts](#landed-contracts)).
Until the maintainer publishes 2.4.0, the newest published SDK is 2.3.0
([latest release](https://github.com/scubamount/sco-core/releases/latest)). The release workflow
publishes the CHANGELOG section [`## sdk-v2.4.0`](../../CHANGELOG.md) as the release's notes; this
page is the longer reference behind it.

<!-- release-scope:start -->
```json
{"tag":"sdk-v2.4.0","sdk_version":"2.4.0","game_pack_version":"0.7.0","plugin_abi":"1.1","profiles":["core","game-sc","legacy","core-source","host-source-sc"]}
```
<!-- release-scope:end -->

## In plain words

This release adds new read-only views of the game for plugins, keeps the plugin interface exactly
as it was, and gives two existing features a real implementation. **None of it has been run in the
game.** "Checked against build 4.10.197.2878" means the game's data is found in the game file on
disk, not that a feature has been seen working.

- Plugins built for 2.3.0 keep working: the plugin interface (ABI 1.1) is byte-identical to 2.3.0.
- Plugins built for 2.4.0 load on a 2.3.0 host, but the new services answer "not found" there, so a
  plugin checks before using them.
- New: read an actor's hunger, thirst, temperature, oxygen, stun, distortion and radiation
  (`game.actor_stats`); change one stat of your own player, off by default (`game.actor_stats_write`);
  read the server's quantum-travel settings and list the 17 parameter definitions
  (`game.gamerules` 1.1, read off, no backend); look up any of 1,267 named input actions by id or name
  (`game.input` 1.0, metadata only; ids above 186 are build-unresolved); ask which game
  build is running (`game.build`); an event when an accepted mission changes
  (`game.mission.changed`, publishes nothing until a missions backend exists); an event when a loadout
  respawn completes (`game.loadout.respawn_completed`, default off, native hook unresolved); and the
  inventory snapshot with its 192 item categories (`game.inventory` 1.1, read held off).
- Changed: teleport (`game.relocation`) and quantum boost (`game.quantum`) have real implementations
  behind the same tables. Relocation turns on only when offline play is confirmed and the game's
  data is found; quantum control stays off.

## Versions

| What | Version | Where |
|---|---|---|
| Kernel (the SDK) | 2.4.0 (was 2.3.0) | `sdk/VERSION`, tag `sdk-v2.4.0` |
| Plugin ABI | 1.1, unchanged | `SCO_API_MAJOR` / `SCO_API_MINOR` in `sco_api.h` (frozen; `include/sco_api.h` and `tests/abi_v1.c` byte-identical to 2.3.0) |
| `game.actor_stats` service table | 1.0 (new) | `sc_actor_stats.h` |
| `game.actor_stats_write` service table | 1.0 (new) | `sc_actor_stats_write.h` |
| `game.gamerules` service table | 1.1 (new; `list_params` appended) | `sc_gamerules.h` |
| `game.build` service table | 1.0 (new) | `sc_game_build.h` |
| `game.input` service table | 1.0 (new) | `sc_input.h` |
| `game.inventory` service table | 1.1 (new; item categories, `flags`, `total_seen`) | `sc_inventory.h` |
| `game.mission.changed`, `game.loadout.respawn_completed` bus events | new records | `sc_game_events.h` |
| `game.relocation`, `game.quantum` service tables | unchanged | production backends added behind the tables |
| Star Citizen game pack | 0.7.0 (was 0.6.0) | `SCO_GAME_PACK_VERSION` in `sc_game_pack.h` |
| Verified game build | 4.10.197.2878 (was 4.10.196.36804 in 2.3.0) | `SCO_GAME_PACK_VERIFIED_BUILD`; rows verified by sigcheck, in-game verification pending |

`VERSIONS.txt` in the zip states the same lines, generated from the headers by `sdk/package.py`.

## Landed contracts

Base: `main` at `c27aa8a` (#196, #185 and #197 merged). Everything in the Versions table above is on `main`.
The table lists the contracts a first product consumer needs.

| Contract | Version on `main` | Default | Evidence |
|---|---|---|---|
| `game.build` | 1.0 | ON, pure metadata | `synthetic_only`; not run in game |
| `game.actor_stats` | 1.0 (with `game.actor_stats_write` 1.0) | read ON when its rows resolve; write OFF | `sigcheck_resolved` (read), `off_pending_probe` (write); not run in game |
| `game.inventory` | 1.1: `flags` and `total_seen` after `ports[]` (a 1.0-sized caller is unchanged), the 192-member `sc_item_type` enumeration with `sc_inventory_item_type_name` and `sc_inventory_port_item_type`, and a production adapter with nine new signature rows | `game.inventory.read` OFF | `synthetic_only`; not run in game; F-4.10.197.2878-011..-016 |
| `game.input` | 1.0: read-only action registry, 1,267 named members, lookup by id and by name; no input injection | `game.input.registry` ON, which advertises metadata availability only | `synthetic_only`; ids above 186 are build-unresolved metadata (`SC_INPUT_ACTION_BUILD_UNRESOLVED`); `LAST` and `INVALID` share one id; not run in game; F-4.10.197.2878-033, F-INTAKE-010 |
| `game.gamerules` | 1.1: `list_params` appended, 17 copied name, group and type definitions; no live values; `read` is unchanged | `game.gamerules.read` OFF | `synthetic_only`; source build unresolved; not run in game; F-INTAKE-005 |

The bus event `game.loadout.respawn_completed` is also on `main`: production capability OFF, native hook unresolved, not run in game (F-INTAKE-004). #185's in-game hold was resolved by the owner's acceptance of static evidence; inventory 1.1 is **not run in game**.

### Prerequisites before a candidate

1. The nine required jobs succeed on the exact `main` commit that will be built.
2. The game-pack row's static 332/332 signature run (2026-10-17, source `965ae4d`; see the [game-pack row](../game-pack.md#verified-game-builds)) stays bound to that commit's signature code; it is refreshed only if the final `main` changes signature code.
3. The designated owner or operator dispatches `release-candidate.yml` on that exact, fully green `main` commit; no candidate or archive exists yet.
4. Human acts, none of them done here: the scope decision record, the signed approval, counsel review, the tag and publication.


## Coordinates and distribution

- Core SDK, combined/legacy SDK, the neutral core-source kit and the Star Citizen
  host-source kit (`host-source-sc`): 2.4.0.
- Optional game-sc public overlay: 0.7.0 (was 0.6.0). The candidate adds six game service tables,
  and the [game-pack policy](../game-pack.md) raises the minor for a release that adds or grows a
  game service. The overlay ships as `sco-sdk-game-sc-0.7.0.zip`, so its compatibility descriptor,
  which binds the exact 2.4.0 core archive SHA-256, never shares a file name with an earlier
  overlay. Consumers check `COMPATIBILITY.json.game-sc` and the required base digest, never the
  version string alone.
- Frozen plugin ABI: 1.1, unchanged. Individual service versions remain independent.

The declared profiles repeat the five profiles published with 2.3.0 (`core`, `game-sc`, `legacy`,
`core-source`, `host-source-sc`); `sdk/components.json` and the candidate workflow's packaging loop
list the same five. The assignment of new files to profiles is the `sdk/components.json`
classification. These notes supply no licence grant or counsel opinion.

## Changes since 2.3.0

The [CHANGELOG `## sdk-v2.4.0` section](../../CHANGELOG.md) holds the complete entries and their
verification limits. In short:

| Change | Where | Profiles (as packaged today) |
|---|---|---|
| `game.actor_stats` 1.0: read nine named stats by actor | `include/sc_actor_stats.h`, [docs](../game/actor-stats.md) | header in game-sc and host-source-sc |
| `game.actor_stats_write` 1.0: `adjust`, `set`, `mode` for your own player, behind a product switch and fresh offline admission | `include/sc_actor_stats_write.h`, `src/game/services/`, [docs](../game/actor-stats.md) | header in game-sc and host-source-sc; implementation in host-source-sc |
| `actor_stats_probe`: in-game test plugin and the maintainer's checklist | `sdk/examples/actor_stats_probe/` | game-sc |
| `game.gamerules` 1.0 with C++20, C# and Lua read wrappers; 1.1 appends `list_params` | `include/sc_gamerules.h`, `include/scosdk/game/gamerules.hpp`, `sdk/csharp/Sco.Sdk/Game/GameRules.cs`, [docs](../game/gamerules.md) | headers and wrappers in game-sc and host-source-sc |
| `game.build` 1.0: running-exe identity and the pack's verified-build pin as data | `include/sc_game_build.h`, [docs](../game/build.md) | header in game-sc and host-source-sc |
| `game.inventory` 1.1: read-only item-port snapshot, item categories, production adapter (read held OFF) | `include/sc_inventory.h`, [docs](../game/inventory.md) | header in game-sc and host-source-sc; implementation in host-source-sc |
| `game.input` 1.0: read-only action registry, 1,267 names | `include/sc_input.h` | header in game-sc and host-source-sc |
| `game.mission.changed` and `game.loadout.respawn_completed` bus events | `include/sc_game_events.h`, [events](../game/events.md) | header in game-sc and host-source-sc |
| `game.relocation` production path (new row `teleport.relocation`, product switch `SetRelocationEnabled`) | `src/game/services/relocation*.{h,cpp}`, [game services](../game-services.md) | host-source-sc |
| `game.quantum` default production backend under the maintainer's amended rule of 2026-10-15 | `src/game/services/quantum_engine.cpp`, `quantum_owned.*`, [quantum](../quantum.md), [cvars](../game/cvars.md) | host-source-sc |
| Verified build 4.10.197.2878 | `include/sc_game_pack.h`, [game pack](../game-pack.md) | header in game-sc and host-source-sc |
| Windows diagnostics: `NtQuerySection` takes `SIZE_T` lengths | `src/ipc/diagnostics_platform_win.cpp` | host-source-sc |
| MCP documentation at the current release with drift gates; tooling and Windows gate hardening | `tools/`, `docs/sdk-mcp-tutorial.md` | repository |
| Game status table and engine-native index regenerated | [evidence status](../game/status.md), `sdk/native_index.json` | repository; the native index stays on the docs site and static MCP only |

Profiles are the `sdk/components.json` classification; `legacy` includes `core` and `game-sc`, and
`host-source-sc` includes `core-source`.

## Capabilities and evidence

Four states per capability: contract (header and ABI pins), implementation, availability (default),
evidence. Every row is "not run in game".

| Capability | Default | Evidence level | What is true |
|---|---|---|---|
| `game.actor_stats.read` | ON when its rows resolve | `sigcheck_resolved` | Real reads; rows resolve on 4.10.197.2878; units and ranges of each stat unconfirmed |
| `game.actor_stats.write` | OFF; product switch plus fresh offline admission | `off_pending_probe` | Real adjust through the game's own function; whether local-only writes persist is unknown; nothing is restored on unload |
| `game.gamerules.read` | OFF | `synthetic_only` | Contract and wrappers; no backend bound, so calls answer unavailable; `list_params` serves static definitions |
| `game.input.registry` | ON (metadata only) | `synthetic_only` | 1,267 named actions; ids above 186 are build-unresolved; no injection |
| `game.loadout.respawn_completed` | OFF | `synthetic_only` | Event contract; native hook unresolved |
| `game.build.info` | ON | `synthetic_only` | Pure metadata, no rows |
| `game.events.mission_changed` | ON (maintainer ruling 2026-10-16, a product default) | `synthetic_only` | Fires only where a missions backend reports missions; none does today |
| `game.inventory.read` | OFF | `synthetic_only` | Adapter bound in production behind fail-closed signature-row and bounds checks; held OFF; synthetic objects only |
| `game.relocation` | ON only while offline admission is fresh, rows resolve, a live player and known system exist and the product switch is on (was OFF) | `synthetic_only` | Real implementation, never run in game |
| `game.quantum.control` | OFF (unchanged) | `synthetic_only` | Real backend that restores what it changes, never run in game |

## Not in this candidate

- A live inventory read: the adapter exists but `game.inventory.read` is held OFF by default and has not run in game.
- `p_enable_physical_quantum_travel` writes: there is no grounded route, so the quantum backend
  does not write it. Console command execution for quantum control is not offered.
- sco.net server-host and the larger session limit are not part of the kernel.
- Any change to a `sco_api` table.

The engine-native index follows the
[2026-10-14 native-index decision](../legal/release-decisions/2026-10-14-native-index.md): the
documentation site and the static MCP server only, never an SDK zip.

## Engineering limits

Nothing in this candidate has run in the game. Evidence is host-side builds and tests, synthetic
backends, ABI pins and static `sco-sigcheck` runs against the build's `StarCitizen.exe` on disk.
The Windows-only engine code (`relocation_engine.cpp`, `quantum_engine.cpp`, the actor-stat
engine reads) is compiled by hosted CI, not in the local builds. Real-game, anti-cheat,
official-network and private-LAN acceptance remain unproven.

## Checking the zip

After publication, the same checks as every release apply:

```sh
sha256sum -c SHA256SUMS
unzip -p sco-sdk-2.4.0.zip sco-sdk-2.4.0/VERSIONS.txt
```

Publication follows the [maintainer release procedure](../legal/release-checklist.md#releasing);
these notes do not authorize a candidate, a tag or a release.
