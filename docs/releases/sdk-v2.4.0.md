# sco-core SDK 2.4.0 (release notes, release candidate 1)

Final tag `sdk-v2.4.0`, combined archive `sco-sdk-2.4.0.zip`.
**Status: release candidate 1 published.** On 2026-10-21 the five archives were published as the
release [`sdk-v2.4.0-rc.1`](https://github.com/scuba-sc/sco-core/releases/tag/sdk-v2.4.0-rc.1), built by the
release-candidate workflow from `main` commit `48d7055`, with every file attested. It is the only
SDK release on the release page: the 2.0.0 to 2.3.0 release downloads were withdrawn (their tags
remain in git). The final signed `sdk-v2.4.0` release is pending; nothing here authorizes approval,
signing, tagging or that publication. It is marked as the repository's latest
release. The release workflow publishes the CHANGELOG section
[`## sdk-v2.4.0`](../../CHANGELOG.md) as the final release's notes; this page is the longer
reference behind it.

<!-- release-scope:start -->
```json
{"tag":"sdk-v2.4.0","sdk_version":"2.4.0","game_pack_version":"0.8.0","plugin_abi":"1.1","profiles":["core","game-sc","legacy","core-source","host-source-sc"]}
```
<!-- release-scope:end -->

## Release candidate 1 archives

Download from [`sdk-v2.4.0-rc.1`](https://github.com/scuba-sc/sco-core/releases/tag/sdk-v2.4.0-rc.1) (files under
`https://github.com/scuba-sc/sco-core/releases/download/sdk-v2.4.0-rc.1/<file>`). The digests are that release's
`SHA256SUMS`; it also carries one `<archive>.spdx.json` SBOM per archive.

| Archive | Profile | SHA-256 |
|---|---|---|
| `sco-sdk-core-2.4.0.zip` | `core` | `68ca5ef213fe9be162b974dcb798a6da2402583daf369e26b26634b23d869f7d` |
| `sco-sdk-game-sc-0.8.0.zip` | `game-sc` (overlay on the exact core archive above) | `fafd2d2d4d86cf479c25a4623e94c7004b45e527e94dceb8354293c6a6b5a8f9` |
| `sco-sdk-2.4.0.zip` | `legacy` (combined kit) | `124751534277b89f0516af1897919c3485923811596e159a1f683bb80b1c243b` |
| `sco-sdk-core-source-2.4.0.zip` | `core-source` | `f2f7c35591ed96090240466910e3db408953a9a4f6e0a16a36da83bc2a34ee62` |
| `sco-sdk-host-source-sc-2.4.0.zip` | `host-source-sc` | `5ca76ff9dac6bbaa183c6f308d610e08d9c1b0daf6ebda6458e222277406dfc6` |

## Known issues in release candidate 1

Release candidate assets are immutable, so neither issue is fixed in them. The setup
steps for the kit are in [Set up the discovery server from the SDK kit](../agent-integration.md#set-up-the-discovery-server-from-the-sdk-kit).

- **The kit's bundled guides and embedded READMEs predate publication.** `sdk-guides.json`
  (served by the MCP tools `search_guides` and `get_guide_section`) and the README files
  inside the kit were frozen before release candidate 1 was published. They can say that
  2.4.0 is an unreleased development candidate, that its archives are not on the release
  page, that 2.3.0 is the newest published SDK, and can give 2.3.0 download steps. None of
  that is current. The [docs site](https://scuba-sc.github.io/sco-core/) and `main`
  supersede the kit's text, and the final 2.4.0 kit will be built from current docs.
  The guide links in the kit are out of date too. For a `docs/` page, the `url` that
  `search_guides` and `get_guide_section` return names the previous docs-site address
  (`scubamount.github.io`), which now returns 404, so "read the rest at its `url`" does
  not work in this kit. Use `https://scuba-sc.github.io/sco-core/` with the same path
  instead; a few older release-notes pages are not on the docs site, so read those under
  `docs/releases/` in the repository. For a guide under `sdk/docs` the `url` is a
  repository path, not a link.
- **`check_compatibility` does not accept the release candidate tag.** Its `tag` argument
  takes only `sdk-vMAJOR.MINOR.PATCH`, so `sdk-v2.4.0-rc.1` is refused as not matching the
  advertised pattern. Compare with `sdk_version` and `game_pack_version` instead. In a kit
  the `release` section of the result also reports `unavailable` or
  `no_matching_approval_record`, because the release approval records are not shipped.

## In plain words

This release adds new read-only views of the game for plugins, keeps the plugin interface exactly
as it was, and gives two existing features a real implementation. **None of it has been run in the
game.** "Checked against build 4.10.197.2878" means the game's data is found in the game file on
disk, not that a feature has been seen working.

- Plugins built for 2.3.0 keep working: the plugin interface (ABI 1.1) is byte-identical to 2.3.0.
- ABI 1.1 plugins can run on older hosts when their required services and manifest
  requirements are satisfied. Optional lookups must handle NOT_FOUND or UNAVAILABLE;
  new widget/storage features need their own version and complete-member checks.
- New: read an actor's hunger, thirst, temperature, oxygen, stun, distortion and radiation
  (`game.actor_stats`); change one stat of your own player, off by default (`game.actor_stats_write`);
  read the server's quantum-travel settings and list the 15 parameter definitions
  (`game.gamerules` 1.1, read off, no backend); look up any of 1,297 named input actions by id or name
  (`game.input` 1.0, metadata only; ids are scoped to build 4.10.197.2878); ask which game
  build is running (`game.build`); an event when an accepted mission changes
  (`game.mission.changed`, publishes nothing until a missions backend exists); an event when a loadout
  respawn completes (`game.loadout.respawn_completed`, default off, native hook unresolved); and the
  inventory snapshot with its 192 item categories (`game.inventory` 1.1, read held off).
- Changed: teleport (`game.relocation`) and quantum boost (`game.quantum`) have real implementations
  behind the same tables. Relocation turns on only when offline play is confirmed and the game's
  data is found; quantum control stays off.
- New, off by default: a fire-and-forget sound trigger (`game.audio` 1.1 `play_once`, no native
  backend yet); guarded native reads of the game's in-memory DataCore records (`game.datacore_live`,
  explicit opt-in, no writes); guarded native reads of four integer CVars (`game.cvars`, opt-in). A
  native CVar write is refused in production, and a regression test pins that.
- Contracts only, natively unavailable (maintainer scope decision, 2026-10-21): filtered single-hit
  rays, exact class-name entity lookup, value-change GameRules reads and fire-and-forget audio. Plugins
  can be written and tested against their synthetic providers today; in the game these calls answer
  unavailable until the native side is grounded, which is the first work after 2.4.0.

## Versions

Expanded-before-release optional contracts also add owner-bound `game.audio` 1.0 (grown to 1.1 by `play_once`),
copied registry/name/component-presence queries in `game.entities`1.2 and scoped
bounded multi-hit in `game.world.control`1.1. Each has a bound synthetic provider
fixture; none supplies a native engine adapter or changes production availability
to ON. Audio acceptance does not certify audible playback. Entity lookup tails
are native C/C++ only; managed additions preserve layout but add no callable route.

| What | Version | Where |
|---|---|---|
| Kernel (the SDK) | 2.4.0 (was 2.3.0) | `sdk/VERSION`, tag `sdk-v2.4.0` |
| Plugin ABI | 1.1, unchanged | `SCO_API_MAJOR` / `SCO_API_MINOR` in `sco_api.h` (frozen; `include/sco_api.h` and `tests/abi_v1.c` byte-identical to 2.3.0) |
| `sco.widgets` | 1.2 | Stable tags in a 128-byte node; copied `const char* tag` at offset 104; typed language support is operation-specific |
| `sco.storage` | 1.1 | 144-byte table with bounded table/view and column enumeration |
| `game.actor_stats` service table | 1.1 (29 raw read IDs; original write scope unchanged) | `sc_actor_stats.h` |
| `game.inventory.contents` service table | 1.0 (copied snapshots; native adapter unavailable) | `sc_container_contents.h` |
| `game.datacore_live` service table | 1.0 (explicit verified host backend; guarded native reads, opt-in, default OFF) | `sc_datacore_live.h` |
| `game.audio` service table | 1.1 (`play_once` appended, 32 to 40 bytes; owner-bound effects; native OFF) | `sc_audio.h` |
| `game.entities` service table | 1.2 (copied lookup tail; native lookup OFF) | `sc_entities.h` |
| `game.world.control` service table | 1.1 (bounded multi-hit; native OFF) | `sc_world_control.h` |
| `game.loadout` service table | 1.2 (asynchronous submit/status/cancel; opt-in) | `sc_loadout.h` |
| `game.actor_stats_write` service table | 1.0 (new) | `sc_actor_stats_write.h` |
| `game.gamerules` service table | 1.1 (new; `list_params` appended) | `sc_gamerules.h` |
| `game.build` service table | 1.0 (new) | `sc_game_build.h` |
| `game.input` service table | 1.0 (new) | `sc_input.h` |
| `game.inventory` service table | 1.1 (new; item categories, `flags`, `total_seen`) | `sc_inventory.h` |
| `game.mission.changed`, `game.loadout.respawn_completed` bus events | new records | `sc_game_events.h` |
| `game.relocation`, `game.quantum` service tables | unchanged | production backends added behind the tables |
| Star Citizen game pack | 0.8.0 (was 0.6.0 in SDK 2.3.0) | `SCO_GAME_PACK_VERSION` in `sc_game_pack.h` |
| Verified game build | 4.10.197.2878 (was 4.10.196.36804 in 2.3.0) | `SCO_GAME_PACK_VERIFIED_BUILD`; rows verified by sigcheck, in-game verification pending |

`VERSIONS.txt` in the zip states the same lines, generated from the headers by `sdk/package.py`.

## Development contracts

These contracts ship in release candidate 1. This table does not attest native
availability or game acceptance.

| Contract | Development version | Default | Evidence |
|---|---|---|---|
| `game.build` | 1.0 | ON, pure metadata | `synthetic_only`; not run in game |
| `game.actor_stats` | 1.1 (with `game.actor_stats_write` 1.0) | read ON when its rows resolve; write OFF; appended read IDs are not writable | `sigcheck_resolved` (read), `off_pending_probe` (write); not run in game |
| `game.inventory` | 1.1: `flags` and `total_seen` after `ports[]` (a 1.0-sized caller is unchanged), the 192-member `sc_item_type` enumeration with `sc_inventory_item_type_name` and `sc_inventory_port_item_type`, and a production adapter with nine new signature rows | `game.inventory.read` OFF | `synthetic_only`; not run in game; F-4.10.197.2878-011..-016 |
| `game.input` | 1.0: read-only action registry, 1,297 named actions (ids 1..1297), lookup by id and by name; no input injection | `game.input.registry` ON, which advertises metadata availability only | `synthetic_only`; ids and names re-derived statically from the 4.10.197.2878 action registry, build-scoped; not run in game; F-4.10.197.2878-050 |
| `game.gamerules` | 1.1: `list_params` appended, 15 name, group and type definitions whose names are verified on 4.10.197.2878 (types not verified); no live values; `read` is unchanged | `game.gamerules.read` OFF | `synthetic_only`; names build-scoped to 4.10.197.2878; not run in game; F-4.10.197.2878-054 |

The bus event `game.loadout.respawn_completed` is also on `main`: production capability OFF, native hook unresolved, not run in game (F-INTAKE-004). #185's in-game hold was resolved by the owner's acceptance of static evidence; inventory 1.1 is **not run in game**.

### Steps to the final release

Step 3 produced release candidate 1: the release-candidate workflow ran on `main` commit
`48d7055` and its attested archives are the `sdk-v2.4.0-rc.1` release. Step 4 remains.

1. The nine required jobs succeed on the exact `main` commit that will be built.
2. Obtain static signature evidence for the final signature implementation. The recorded 332/332 run is historical and source-bound; the current index inventory includes additional async-loadout rows. Inventory is not proof of resolution against the executable. A local `sco-sigcheck` run of the 2026-10-21 candidate source resolved 347/347 rows on 4.10.197.2878 (static only).
3. The designated human owner/operator dispatches `release-candidate.yml` on the exact fully green main commit. Local ZIP generation is separate from that candidate and its attestations.
4. Human acts: the provenance decisions are recorded
   ([2.4.0 decisions](../legal/release-decisions/2026-10-21-sdk-v2.4.0-scope.md)); the signed
   approval, counsel review, the final `sdk-v2.4.0` tag and its publication remain. None of them is done here.


## Coordinates and distribution

- Core SDK, combined/legacy SDK, the neutral core-source kit and the Star Citizen
  host-source kit (`host-source-sc`): 2.4.0.
- Optional game-sc public overlay: 0.8.0 (was 0.6.0). The combined candidate adds and extends game service tables,
  and the [game-pack policy](../game-pack.md) raises the minor for a release that adds or grows a
  game service. The overlay ships as `sco-sdk-game-sc-0.8.0.zip`, so its compatibility descriptor,
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
| `game.cvars` native reference-integer read adapter, default OFF; native writes held for object/backing lifetime evidence and refused in production (pinned by a regression test); separate fresh host-stop cleanup admission | `src/game/services/cvars_engine.*`, `cvars_production.cpp`, [docs](../game/cvars.md) | implementation and synthetic Windows host-stop fixtures in host-source-sc |
| `game.actor_stats` 1.1: read 29 raw named stats by actor; no injury-tier or normalized-unit claim | `include/sc_actor_stats.h`, [docs](../game/actor-stats.md) | header in game-sc and host-source-sc |
| `game.inventory.contents` 1.0: bounded copied stored-item snapshots, generation and optional volume; unavailable without a grounded host provider | `include/sc_container_contents.h`, [docs](../game/container-contents.md) | public contract/example in game-sc; provider in host-source-sc |
| `game.datacore_live` 1.0: host-backed pool access, fresh offline admission and separate write opt-in; guarded native counted reads, GUID record lookup and text-blob reads, default OFF, needing explicit pre-engine quiescence; native writes unavailable | `include/sc_datacore_live.h`, [docs](../game/datacore-live.md) | public contract in game-sc; provider in host-source-sc |
| `game.audio` 1.1: `play_once` fire-and-forget trigger with its own default-OFF capability; no native backend | `include/sc_audio.h`, `include/scosdk/game/audio.hpp`, [docs](../game/audio.md) | public contract in game-sc; provider seam in host-source-sc |
| `game.loadout` 1.2: copied asynchronous submit/status/cancel with native engine path behind explicit opt-in | `include/sc_loadout.h`, [docs](../game/loadout.md) | public contract in game-sc; provider in host-source-sc |
| `game.actor_stats_write` 1.0: `adjust`, `set`, `mode` for your own player, behind a product switch and fresh offline admission | `include/sc_actor_stats_write.h`, `src/game/services/`, [docs](../game/actor-stats.md) | header in game-sc and host-source-sc; implementation in host-source-sc |
| `actor_stats_probe`: in-game test plugin and the maintainer's checklist | `sdk/examples/actor_stats_probe/` | game-sc |
| `game.gamerules` 1.0 with C++20, C# and Lua read wrappers; 1.1 appends `list_params` | `include/sc_gamerules.h`, `include/scosdk/game/gamerules.hpp`, `sdk/csharp/Sco.Sdk/Game/GameRules.cs`, [docs](../game/gamerules.md) | headers and wrappers in game-sc and host-source-sc |
| `game.build` 1.0: running-exe identity and the pack's verified-build pin as data | `include/sc_game_build.h`, [docs](../game/build.md) | header in game-sc and host-source-sc |
| `game.inventory` 1.1: read-only item-port snapshot, item categories, production adapter (read held OFF) | `include/sc_inventory.h`, [docs](../game/inventory.md) | header in game-sc and host-source-sc; implementation in host-source-sc |
| `game.input` 1.0: read-only action registry, 1,297 names | `include/sc_input.h` | header in game-sc and host-source-sc |
| `game.mission.changed` and `game.loadout.respawn_completed` bus events | `include/sc_game_events.h`, [events](../game/events.md) | header in game-sc and host-source-sc |
| `game.relocation` production path (new row `teleport.relocation`, product switch `SetRelocationEnabled`) | `src/game/services/relocation*.{h,cpp}`, [game services](../game-services.md) | host-source-sc |
| `game.quantum` default production backend under the maintainer's amended rule of 2026-10-15 | `src/game/services/quantum_engine.cpp`, `quantum_owned.*`, [quantum](../quantum.md), [cvars](../game/cvars.md) | host-source-sc |
| Verified build 4.10.197.2878 | `include/sc_game_pack.h`, [game pack](../game-pack.md) | header in game-sc and host-source-sc |
| Windows diagnostics: `NtQuerySection` takes `SIZE_T` lengths | `src/ipc/diagnostics_platform_win.cpp` | host-source-sc |
| `sco-new-plugin.py` and MCP `generate_plugin_scaffold`: `--lang csharp` (NativeAOT project) and `--lang lua`; stand-in checks only, not run in game | `sdk/tools/sco-new-plugin.py`, `tools/sco-core-mcp/server.py`, [SDK README](../../sdk/README.md#generate-a-plugin-folder) | core and core-source (the C# layer it targets ships in core) |
| MCP documentation at the current release with drift gates; tooling and Windows gate hardening | `tools/`, `docs/sdk-mcp-tutorial.md` | repository |
| Guide search in the discovery MCP server: `search_guides` and `get_guide_section` behind `--extended-tools` (default six tools unchanged), over `sdk-guides.json` built by `tools/sdk_guides.py` from the public guides; not run in game | `tools/sco-core-mcp/`, [agent integration](../agent-integration.md) | `sdk-guides.json` beside `sdk-catalogue.json` in the combined kit, game-sc and host-source-sc |
| Discovery MCP server: three opt-in development tools behind `--enable-dev-tools` and `--project-root` (`run_plugin_check`, `build_plugin_project`, `test_plugin_project`); default and extended tool lists unchanged; not run in game | `tools/sco-core-mcp/`, [agent integration](../agent-integration.md) | repository only |
| Game status table and engine-native index regenerated | [evidence status](../game/status.md), `sdk/native_index.json` | repository; the native index stays on the docs site and static MCP only |

Profiles are the `sdk/components.json` classification; `legacy` includes `core` and `game-sc`, and
`host-source-sc` includes `core-source`.

## Capabilities and evidence

Four states per capability: contract (header and ABI pins), implementation, availability (default),
evidence. Every row is "not run in game".

| Capability | Default | Evidence level | What is true |
|---|---|---|---|
| `game.cvars.read` | OFF; explicit native read opt-in | `synthetic_only` | Current-build integer method identities and actual Windows adapter tested on synthetic objects; not run in game |
| `game.cvars.write` | OFF; production native port refuses writes | `synthetic_only` | Setter/restore mechanics and actual host-stop cleanup tested; native lifetime across callbacks and leases remains ungrounded |
| `game.actor_stats.read` | ON when its rows resolve | `sigcheck_resolved` | Real reads; rows resolve on 4.10.197.2878; units and ranges of each stat unconfirmed |
| `game.actor_stats.write` | OFF; product switch plus fresh offline admission | `off_pending_probe` | Real adjust through the game's own function; whether local-only writes persist is unknown; nothing is restored on unload |
| `game.gamerules.read` | OFF | `synthetic_only` | Contract and wrappers; no backend bound, so calls answer unavailable; `list_params` serves static definitions. Native value-change read held (scope decision 2026-10-21) |
| `game.audio.triggers` | OFF | `synthetic_only` | Owner-bound trigger and playback tokens on a synthetic provider; no native or audible verification |
| `game.audio.play_once` | OFF | `synthetic_only` | Fire-and-forget on a synthetic provider; OK means accepted only; no native backend (scope decision 2026-10-21) |
| `game.datacore_live` | OFF; explicit native read opt-in | `synthetic_only` | Guarded native reads tested on synthetic hosts; activation needs pre-engine quiescence; writes unavailable |
| `game.entities.lookup` | OFF | `synthetic_only` | Copied lookup on a synthetic provider; native exact class-name lookup held (scope decision 2026-10-21) |
| `game.world.raycast_filtered` | OFF | `synthetic_only` | Filtered rays with copied results on a synthetic provider; native single-hit held (scope decision 2026-10-21) |
| `game.input.registry` | ON (metadata only) | `synthetic_only` | 1,297 named actions, ids 1..1297 on 4.10.197.2878; no injection |
| `game.events.loadout_respawn_completed` | OFF | `synthetic_only` | Event contract; native hook unresolved |
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
- Native backends for filtered single-hit rays, exact class-name entity lookup, value-change
  GameRules reads and fire-and-forget audio (maintainer scope decision, 2026-10-21). Their contracts
  ship with synthetic providers; natively they answer unavailable and install no hook, because the
  evidence does not yet show that the game objects each call touches stay valid for the whole call.
  They are the first native work after 2.4.0.

The current manifest selects native-index/status metadata for game-sc, legacy
and host-source-sc, with source-matched generated catalogues. Selection is not
publication approval. The earlier
[native-index distribution restriction](../legal/release-decisions/2026-10-14-native-index.md)
requires a new applicable maintainer decision before those bytes are released.

## Engineering limits

Nothing in this candidate has run in the game. Evidence is host-side builds and tests, synthetic
backends, ABI pins and static `sco-sigcheck` runs against the build's `StarCitizen.exe` on disk.
The Windows-only engine code (`relocation_engine.cpp`, `quantum_engine.cpp`, the actor-stat
engine reads) is compiled by hosted CI, not in the local builds. Real-game, anti-cheat,
official-network and private-LAN acceptance remain unproven.

## Checking the zip

Download the archives you need and `SHA256SUMS` from release candidate 1, then run the same
checks as every release (`sha256sum --ignore-missing` skips the lines for files you did not
download):

```sh
gh release download sdk-v2.4.0-rc.1 -R scuba-sc/sco-core -p 'sco-sdk-2.4.0.zip' -p SHA256SUMS
sha256sum --ignore-missing -c SHA256SUMS
unzip -p sco-sdk-2.4.0.zip sco-sdk-2.4.0/VERSIONS.txt
```

The final release follows the [maintainer release procedure](../legal/release-checklist.md#releasing);
these notes do not authorize a tag or a release.
