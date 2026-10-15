# sco-core SDK 2.4.0 (release notes, unpublished candidate)

Tag `sdk-v2.4.0`, combined zip `sco-sdk-2.4.0.zip`. **Status: unpublished candidate.** This page
is a draft prepared on 2026-10-15 for the maintainer. No tag, signature, approval record, candidate
run or release exists for 2.4.0, and nothing on this page authorizes one. The maintainer recorded
the candidate scope and game pack 0.6.1 in the
[SDK 2.4.0 scope record](../legal/release-decisions/2026-10-15-sdk-v2.4.0-scope.md) on 2026-10-15;
that record does not itself dispatch a candidate, create an approval record, sign, tag or publish.
Until the maintainer publishes 2.4.0, the newest published SDK is 2.3.0
([latest release](https://github.com/scubamount/sco-core/releases/latest)). The release workflow
publishes the CHANGELOG section [`## sdk-v2.4.0`](../../CHANGELOG.md) as the release's notes;
this page is the longer reference behind it.

<!-- release-scope:start -->
```json
{"tag":"sdk-v2.4.0","sdk_version":"2.4.0","game_pack_version":"0.6.1","plugin_abi":"1.1","profiles":["core","game-sc","legacy","core-source","host-source-sc"]}
```
<!-- release-scope:end -->

## Versions

| What | Version | Where |
|---|---|---|
| Kernel (the SDK) | 2.4.0 (was 2.3.0) | `sdk/VERSION`, tag `sdk-v2.4.0` |
| Plugin ABI | 1.1, unchanged | `SCO_API_MAJOR` / `SCO_API_MINOR` in `sco_api.h` (frozen; `include/sco_api.h` and `tests/abi_v1.c` byte-identical to 2.3.0) |
| `game.relocation` service table | 1.0, unchanged | `sc_relocation.h` (unchanged); production engine backend added behind the table |
| `game.quantum` service table | unchanged | `sc_quantum.h` (unchanged); default production engine backend added behind the table |
| Star Citizen game pack | 0.6.1 (was 0.6.0) | `SCO_GAME_PACK_VERSION` in `sc_game_pack.h` |
| Verified game build | unchanged from 2.3.0 | 4.10.196.36804; the new row `teleport.relocation` resolved there in a local `sco-sigcheck` run |

`VERSIONS.txt` in the zip states the same lines, generated from the headers by `sdk/package.py`.

## Coordinates and distribution

- Core SDK, combined/legacy SDK, the neutral core-source kit and the Star Citizen
  host-source kit (`host-source-sc`): 2.4.0.
- Optional game-sc public overlay: 0.6.1 (was 0.6.0), a patch release. The candidate adds no
  game service table and grows none (no public `sc_*.h` header changes), so the
  [game-pack policy](../game-pack.md) does not raise the minor; it adds production backends and
  the `teleport.relocation` row behind the existing `game.relocation` 1.0 and `game.quantum`
  tables, and the overlay's documentation and compatibility descriptor change. A patch coordinate
  gives the 2.4.0 overlay a file name of its own (`sco-sdk-game-sc-0.6.1.zip`) instead of reusing
  the 2.3.0 asset's name with different bytes. Consumers still check `COMPATIBILITY.json.game-sc`
  and the required base digest, never the version string alone.
- Frozen plugin ABI: 1.1, unchanged. Individual service versions remain independent.

The declared profiles repeat the five profiles published with 2.3.0; `sdk/components.json` and the
candidate workflow's packaging loop list the same five. Which new sources join which profile is
listed in the [scope record](../legal/release-decisions/2026-10-15-sdk-v2.4.0-scope.md). These
records supply no licence grant or counsel opinion.

## Changes since 2.3.0

The [CHANGELOG `## sdk-v2.4.0` section](../../CHANGELOG.md) holds the complete entries and their
verification limits. In short:

| Change | Where | Profiles (as packaged today) |
|---|---|---|
| `game.relocation` 1.0 production path: the v0.7.7 transport and star-system classifier behind a `RelocationBackend` seam, fail-closed on offline admission; new signature row `teleport.relocation`; product switch `sco::game::services::SetRelocationEnabled` | `src/game/services/relocation.cpp`, `relocation.h`, `relocation_model.h`, `relocation_engine.cpp` (new, Windows only), `src/game/teleport_sigs.cpp`, `include/sco/game/services.h`, [game services](../game-services.md) | host-source-sc; the doc in game-sc and host-source-sc |
| `game.quantum` default production backend under the maintainer's amended rule of 2026-10-15: four `v_qdrive2` cvars and the drive parameter cell written only while holding, after fresh offline admission, priors restored on every release path | `src/game/services/quantum_engine.cpp` (new, Windows only), `quantum_owned.cpp` / `quantum_owned.h` (new), [quantum](../quantum.md), [cvars](../game/cvars.md) | host-source-sc; the docs in game-sc (and `cvars.md` in host-source-sc) |
| Synthetic tests for both backends | `tests/game_contracts/relocation/test_provider.cpp`, `tests/test_quantum_owned.cpp` (new), `tests/test_relocation.cpp` | host-source-sc |
| Game status table and engine-native index regenerated | [evidence status](../game/status.md), `sdk/native_index.json` | repository; the native index stays on the docs site and static MCP only |

Profiles are the `sdk/components.json` classification; `legacy` includes `core` and `game-sc`, and
`host-source-sc` includes `core-source`.

## Capabilities

| Capability | Default | Evidence level |
|---|---|---|
| `game.relocation` | ON, but only while the host certifies fresh offline admission, the rows `teleport.to_camera` and `teleport.relocation` resolve, a live player and a known star system exist and the product switch is on; otherwise OFF (was OFF) | `synthetic_only` |
| `game.quantum.control` | OFF (unchanged); the default production backend now exists, but nothing has run on Windows or in the game | `synthetic_only` |

## Not in this candidate

- `p_enable_physical_quantum_travel` writes: there is no grounded route, so the quantum backend
  does not write it.
- Console command execution for quantum control.
- Any change to a public game service header, signature row other than `teleport.relocation`, or
  `sco_api` table.

The engine-native index follows the
[2026-10-14 native-index decision](../legal/release-decisions/2026-10-14-native-index.md): the
documentation site and the static MCP server only, never an SDK zip.

## Engineering limits

Nothing in this candidate has run in the game. Evidence is macOS local builds and synthetic
fixtures: the host test suite, the relocation provider tests, `test_relocation`,
`test_quantum_owned` over a fake engine and the ABI pins. The engine adapters
`relocation_engine.cpp` and `quantum_engine.cpp` are Windows-only and were not compiled in the
local builds; Linux and native Windows builds of the merged branch, MSVC compilation of those
adapters, and real-game, anti-cheat, official-network and private-LAN acceptance remain unproven.

## Checking the zip

After publication, the same checks as every release apply:

```sh
sha256sum -c SHA256SUMS
unzip -p sco-sdk-2.4.0.zip sco-sdk-2.4.0/VERSIONS.txt
```

Publication follows the [maintainer release procedure](../legal/release-checklist.md#releasing);
these notes do not authorize a candidate, a tag or a release.
